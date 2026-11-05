#define _HAS_STD_BYTE 0
#include "CrashReporter.h"
#include "Prefs.h"
#include "resource.h"
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <exception>
#include <string>
#include <algorithm>

#ifndef VIKEY_BUILD
#define VIKEY_BUILD "0"
#endif
#if defined(_M_ARM64)
#define VIKEY_ARCH "arm64"
#elif defined(_M_X64)
#define VIKEY_ARCH "x64"
#else
#define VIKEY_ARCH "x86"
#endif

namespace vikey { namespace crash {


// Everything the crash handler needs is prepared at install time: it runs in a broken process and should not allocate.
static wchar_t _dir[MAX_PATH];
static char _os[160];
static volatile LONG _writing = 0;
static DWORD _mainThread = 0;

// ---- helpers ------------------------------------------------------------------------------------
static std::string narrow(const std::wstring& w) { int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s; }
static std::wstring widen(const std::string& s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w; }

static std::wstring regString(HKEY root, const wchar_t* path, const wchar_t* name) {
    wchar_t buf[128] = {0}; DWORD size = sizeof(buf);
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return L"";
    return buf;
}
static DWORD regDword(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD v = 0, size = sizeof(v);
    RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &v, &size); return v;
}

/// "Windows 11 Pro 25H2 (10.0.26200.7019, arm64)"
static std::string windowsVersion() {
    typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW v = {sizeof(v)};
    if (auto f = (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) f(&v);
    const wchar_t* cv = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::wstring edition = regString(HKEY_LOCAL_MACHINE, cv, L"EditionID"), display = regString(HKEY_LOCAL_MACHINE, cv, L"DisplayVersion");
    DWORD ubr = regDword(HKEY_LOCAL_MACHINE, cv, L"UBR");
    const char* machine = "unknown";
    USHORT pm = 0, nm = 0;
    typedef BOOL (WINAPI *IsWow64Process2Fn)(HANDLE, USHORT*, USHORT*);
    if (auto f = (IsWow64Process2Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2")) f(GetCurrentProcess(), &pm, &nm);
    if (nm == IMAGE_FILE_MACHINE_ARM64) machine = "arm64"; else if (nm == IMAGE_FILE_MACHINE_AMD64) machine = "x64"; else if (nm == IMAGE_FILE_MACHINE_I386) machine = "x86";
    char buf[160];
    _snprintf_s(buf, _TRUNCATE, "Windows %s%s%s%s%s (%lu.%lu.%lu.%lu, %s)", v.dwBuildNumber >= 22000 ? "11" : "10",
        edition.empty() ? "" : " ", narrow(edition).c_str(), display.empty() ? "" : " ", narrow(display).c_str(),
        v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, ubr, machine);
    return buf;
}

// ---- writing the report (in the crashing process) -----------------------------------------------
struct Out { char buf[16384]; int len = 0;
    void add(const char* fmt, ...) { va_list a; va_start(a, fmt); int n = _vsnprintf_s(buf + len, sizeof(buf) - len, _TRUNCATE, fmt, a); va_end(a); if (n > 0) len += n; else len = (int)strlen(buf); }
};
static Out _out;   // static: no allocation, and a stack overflow leaves little room

/// "Vikey.exe+0x1a2b3": the module file name (no folder) and the offset in it.
static void addLocation(Out& o, DWORD64 pc) {
    HMODULE m = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)pc, &m) && m) {
        wchar_t path[MAX_PATH] = {0}; GetModuleFileNameW(m, path, MAX_PATH);
        const wchar_t* name = wcsrchr(path, L'\\'); name = name ? name + 1 : path;
        char n8[MAX_PATH * 3] = {0}; WideCharToMultiByte(CP_UTF8, 0, name, -1, n8, sizeof(n8), nullptr, nullptr);
        o.add("%s+0x%llx", n8, (unsigned long long)(pc - (DWORD64)m));
    } else o.add("0x%016llx (no module)", (unsigned long long)pc);
}

static DWORD64 pcOf(const CONTEXT& c) {
#if defined(_M_ARM64)
    return c.Pc;
#elif defined(_M_X64)
    return c.Rip;
#else
    return c.Eip;
#endif
}

/// Walks the stack with the unwind data in each module (RtlVirtualUnwind): no DbgHelp, no symbols, no allocation.
static void walk(Out& o, CONTEXT& ctx);
static void addStack(Out& o, CONTEXT ctx) {
    __try { walk(o, ctx); } __except (EXCEPTION_EXECUTE_HANDLER) { o.add("  (stack walk stopped: bad frame)\n"); }
}
static void walk(Out& o, CONTEXT& ctx) {
    o.add("\nStack (module+offset):\n");
#if defined(_M_X64) || defined(_M_ARM64)
    for (int i = 0; i < 64; i++) {
        DWORD64 pc = pcOf(ctx);
        if (!pc) break;
        o.add("%3d  ", i); addLocation(o, pc); o.add("\n");
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(pc, &base, nullptr);
        if (fn) {
            PVOID handlerData = nullptr; DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, pc, fn, &ctx, &handlerData, &frame, nullptr);
        } else {   // a leaf function: the return address is on top of the stack (x64) or in the link register (ARM64)
#if defined(_M_X64)
            if (!ctx.Rsp) break;
            ctx.Rip = *(DWORD64*)ctx.Rsp; ctx.Rsp += 8;
#else
            if (ctx.Lr == ctx.Pc) break;
            ctx.Pc = ctx.Lr;
#endif
        }
        if (pcOf(ctx) == pc) break;
    }
#else
    o.add("  (stack walk not supported on this architecture)\n");
#endif
}

static const char* codeName(DWORD c) {
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN (fail fast)";
    case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
    case 0xE06D7363: return "C++ exception";
    default: return "";
    }
}

/// reason: nullptr for a hardware/SEH exception (rec is set), else "std::terminate", "abort", ...
static void writeReport(const char* reason, EXCEPTION_RECORD* rec, CONTEXT* ctx) {
    if (InterlockedExchange(&_writing, 1)) return;   // one report per crash
    Out& o = _out; o.len = 0; o.buf[0] = 0;
    SYSTEMTIME t; GetLocalTime(&t);
    o.add("Vikey crash report\n");
    o.add("Vikey: %s (%s) %s\n", VIKEY_VERSION, VIKEY_BUILD, VIKEY_ARCH);
    o.add("OS: %s\n", _os);
    o.add("Time: %04d-%02d-%02d %02d:%02d:%02d (local)\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    o.add("Thread: %lu%s\n", GetCurrentThreadId(), GetCurrentThreadId() == _mainThread ? " (main)" : "");
    if (rec) {
        o.add("Exception: 0x%08lX %s", rec->ExceptionCode, codeName(rec->ExceptionCode));
        if ((rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && rec->NumberParameters >= 2) {
            ULONG_PTR kind = rec->ExceptionInformation[0];
            o.add(" (%s at 0x%llx)", kind == 0 ? "read" : kind == 1 ? "write" : kind == 8 ? "execute" : "access", (unsigned long long)rec->ExceptionInformation[1]);
        }
        o.add("\nAddress: "); addLocation(o, (DWORD64)rec->ExceptionAddress); o.add("\n");
    } else {
        o.add("Exception: %s\n", reason);
    }
    if (ctx) addStack(o, *ctx);

    wchar_t path[MAX_PATH];
    _snwprintf_s(path, _TRUNCATE, L"%s\\Vikey-%04d-%02d-%02d-%02d%02d%02d.txt", _dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) { DWORD w = 0; WriteFile(f, o.buf, (DWORD)o.len, &w, nullptr); FlushFileBuffers(f); CloseHandle(f); }
}

static LONG WINAPI onException(EXCEPTION_POINTERS* ep) {
    writeReport(nullptr, ep->ExceptionRecord, ep->ContextRecord);
    return EXCEPTION_EXECUTE_HANDLER;   // quit without the Windows error dialog
}

/// Software failures (no exception record): report from here, then end the process.
[[noreturn]] static void fail(const char* reason) {
    CONTEXT ctx; RtlCaptureContext(&ctx);
    writeReport(reason, nullptr, &ctx);
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) {}
}
static void onTerminate() { fail("std::terminate"); }
static void onPurecall() { fail("pure virtual function call"); }
static void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) { fail("invalid parameter (CRT)"); }
static void onAbort(int) { fail("abort"); }

void install() {
    std::wstring d = prefs::appDataDir() + L"\\Crashes";
    CreateDirectoryW(d.c_str(), nullptr);
    wcsncpy_s(_dir, d.c_str(), _TRUNCATE);
    _mainThread = GetCurrentThreadId();
    strncpy_s(_os, windowsVersion().c_str(), _TRUNCATE);
    ULONG guarantee = 64 * 1024; SetThreadStackGuarantee(&guarantee);   // room for the handler after a stack overflow
    SetUnhandledExceptionFilter(onException);
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPurecall);
    _set_invalid_parameter_handler(onInvalidParameter);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    signal(SIGABRT, onAbort);
}

__declspec(noinline) static void crashNow(volatile int* p) { *p = 42; }
void selfTestIfAsked() {
    const wchar_t* cl = GetCommandLineW();
    if (wcsstr(cl, L"--crash-self-test=terminate")) std::terminate();
    if (wcsstr(cl, L"--crash-self-test")) crashNow(nullptr);
}

}} // namespace
