#include "tsf_guids.h"
#include "tip.h"
#include "../App/Prefs.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <new>

namespace {
std::atomic<long> g_objects{0};
std::atomic<long> g_locks{0};
class VikeyClassFactory final : public IClassFactory {
public:
    VikeyClassFactory() { VikeyTsfObjectCreated(); }
    ~VikeyClassFactory() { VikeyTsfObjectReleased(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IClassFactory) {
            *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --refs_; if (!count) delete this; return count;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        return CreateVikeyTextService(iid, out);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        VikeyTsfServerLocked(lock != FALSE); return S_OK;
    }
private:
    std::atomic<ULONG> refs_{1};
};
}
void VikeyTsfObjectCreated() { ++g_objects; }
void VikeyTsfObjectReleased() { --g_objects; }
void VikeyTsfServerLocked(bool locked) { if (locked) ++g_locks; else --g_locks; }
void VikeyTsfLog(const char* fileName, const char* eventName, HRESULT hr,
                 unsigned long value1, unsigned long value2) {
    wchar_t directory[MAX_PATH] = {};
    const DWORD envLength = GetEnvironmentVariableW(L"VIKEY_TSF_LOG_DIR", directory, MAX_PATH);
    if (!envLength || envLength >= MAX_PATH) {
        const std::wstring root = vikey::prefs::appDataDir() + L"\\Logs";
        wcsncpy_s(directory, root.c_str(), _TRUNCATE);
    }
    CreateDirectoryW(directory, nullptr);
    std::wstring path(directory);
    path += L"\\";
    path += (strcmp(fileName, "tsf-tip.log") == 0 ? L"tsf-tip.log" : L"tsf.log");
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[256] = {};
    SYSTEMTIME now; GetLocalTime(&now);
    const int count = snprintf(line, sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u event=%s hr=0x%08lX a=%lu b=%lu\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        eventName, (unsigned long)hr, value1, value2);
    DWORD written = 0;
    if (count > 0) WriteFile(file, line, (DWORD)count, &written, nullptr);
    CloseHandle(file);
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
STDAPI DllCanUnloadNow(void) {
    return g_objects.load() == 0 && g_locks.load() == 0 ? S_OK : S_FALSE;
}
STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* out) {
    if (clsid != CLSID_VikeyTextService) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) VikeyClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(iid, out);
    factory->Release();
    return hr;
}
