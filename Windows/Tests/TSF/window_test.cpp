#include <windows.h>
#include <richedit.h>
#include <cstdio>
#include <string>

static void logLine(const char* line) {
    wchar_t directory[MAX_PATH] = {};
    DWORD length = GetEnvironmentVariableW(L"VIKEY_TSF_LOG_DIR", directory, MAX_PATH);
    if (!length || length >= MAX_PATH) wcscpy_s(directory, L"Logs");
    CreateDirectoryW(directory, nullptr);
    const std::wstring path = std::wstring(directory) + L"\\tsf-window-test.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, line, (DWORD)strlen(line), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}
static std::wstring siblingTipPath() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe);
    const size_t slash = path.find_last_of(L"\\/");
    path.resize(slash == std::wstring::npos ? 0 : slash + 1);
#if defined(_M_ARM64)
    path += L"..\\..\\App\\VikeyTsf-arm64.dll";
#else
    path += L"..\\..\\App\\VikeyTsf-x64.dll";
#endif
    wchar_t full[MAX_PATH] = {};
    GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    return full;
}
int wmain() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return 2;
    HMODULE tip = LoadLibraryW(siblingTipPath().c_str());
    if (!tip) { logLine("SKIP: could not load built TSF TIP"); return 77; }
    using RegisterFn = HRESULT (STDAPICALLTYPE*)();
    const auto registerFn = reinterpret_cast<RegisterFn>(GetProcAddress(tip, "DllRegisterServer"));
    if (!registerFn || FAILED(registerFn())) {
        logLine("FAIL: DllRegisterServer failed");
        FreeLibrary(tip);
        return 1;
    }

    LoadLibraryW(L"Msftedit.dll");
    HWND window = CreateWindowExW(WS_EX_TOPMOST, MSFTEDIT_CLASS, L"",
        WS_POPUP | WS_VISIBLE | WS_BORDER | ES_MULTILINE,
        120, 120, 520, 160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) { logLine("SKIP: RichEdit window unavailable"); FreeLibrary(tip); return 77; }
    SetForegroundWindow(window);
    SetFocus(window);
    Sleep(200);
    if (GetForegroundWindow() != window) {
        logLine("SKIP: runner did not grant foreground desktop access");
        DestroyWindow(window);
        FreeLibrary(tip);
        return 77;
    }

    INPUT input[10] = {};
    const WORD keys[] = {'V', 'I', 'E', 'T', 'S'};
    for (int i = 0; i < 5; ++i) {
        input[i * 2].type = INPUT_KEYBOARD; input[i * 2].ki.wVk = keys[i];
        input[i * 2 + 1].type = INPUT_KEYBOARD;
        input[i * 2 + 1].ki.wVk = keys[i]; input[i * 2 + 1].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    const UINT sent = SendInput(10, input, sizeof(INPUT));
    Sleep(300);
    wchar_t text[64] = {};
    GetWindowTextW(window, text, 64);
    DestroyWindow(window);
    FreeLibrary(tip);
    if (sent != 10) { logLine("SKIP: SendInput did not deliver all key events"); return 77; }
    if (std::wstring(text) != L"viét") {
        logLine("FAIL: RichEdit text does not equal viét");
        return 1;
    }
    logLine("PASS: RichEdit received viét through the active TSF profile");
    return 0;
}
