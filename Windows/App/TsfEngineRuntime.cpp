#include "TsfEngineRuntime.h"
#include "Engine.h"
#include <windows.h>
#include <mutex>
#include <vector>
#include <string>
#include <algorithm>
#include <cwchar>

int vLanguage = 1;
int vSwitchKeyStatus = 0x7A000206;
int vCheckSpelling = 1;
int vUseModernOrthography = 1;
int vRestoreIfWrongSpelling = 1;
int vFixRecommendBrowser = 0;
int vUseSmartSwitchKey = 0;
int vOtherLanguage = 0;
int vAutoSwitchByTyping = 0;

namespace vikey { namespace tsf {
bool isSecureInputContext() {
    static HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, L"Local\\VikeySecureInputState");
    static const volatile LONG* state = mapping
        ? static_cast<const volatile LONG*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(LONG)))
        : nullptr;
    // If the controller's UIA password-state bridge is unavailable, fail closed.
    if (!state) return true;
    return InterlockedCompareExchange(const_cast<volatile LONG*>(state), 0, 0) != 0;
}

bool isCurrentInputLanguageSupported() {
    HWND window = GetForegroundWindow();
    DWORD threadId = window ? GetWindowThreadProcessId(window, nullptr) : 0;
    HKL layout = GetKeyboardLayout(threadId ? threadId : GetCurrentThreadId());
    const WORD primary = PRIMARYLANGID(LOWORD(reinterpret_cast<ULONG_PTR>(layout)));
    return primary == LANG_ENGLISH || primary == LANG_VIETNAMESE || primary == LANG_NEUTRAL;
}

bool isCurrentAppBlacklisted() {
    static std::mutex mutex;
    static ULONGLONG lastRefresh = 0;
    static std::vector<std::wstring> blacklist;
    const ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (lastRefresh == 0 || now - lastRefresh >= 1000) {
            std::vector<std::wstring> next;
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Vikey", 0, KEY_READ, &key) == ERROR_SUCCESS) {
                DWORD type = 0, bytes = 0;
                if (RegQueryValueExW(key, L"AppBlacklist", nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS &&
                    type == REG_MULTI_SZ && bytes >= 2 * sizeof(wchar_t) && bytes <= 1024 * 1024) {
                    std::vector<wchar_t> data(bytes / sizeof(wchar_t) + 2, 0);
                    if (RegQueryValueExW(key, L"AppBlacklist", nullptr, &type,
                        reinterpret_cast<BYTE*>(data.data()), &bytes) == ERROR_SUCCESS && type == REG_MULTI_SZ) {
                        for (const wchar_t* item = data.data(); *item; item += wcslen(item) + 1)
                            next.emplace_back(item);
                    }
                }
                RegCloseKey(key);
            }
            blacklist.swap(next);
            lastRefresh = now;
        }
    }

    HWND window = GetForegroundWindow();
    DWORD pid = 0;
    if (window) GetWindowThreadProcessId(window, &pid);
    if (!pid) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[MAX_PATH] = {};
    DWORD length = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    if (!ok) return false;
    const wchar_t* exe = wcsrchr(path, L'\\');
    exe = exe ? exe + 1 : path;

    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& item : blacklist)
        if (_wcsicmp(item.c_str(), exe) == 0) return true;
    return false;
}
void refreshTsfEngineSettings() {
    static std::once_flag openOnce;
    static HKEY settingsKey = nullptr;
    std::call_once(openOnce, [] {
        RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Vikey", 0, KEY_READ, &settingsKey);
    });
    if (settingsKey) {
        auto read = [](const wchar_t* name, int fallback) {
            DWORD value = static_cast<DWORD>(fallback), size = sizeof(value), type = 0;
            if (RegQueryValueExW(settingsKey, name, nullptr, &type,
                reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS ||
                type != REG_DWORD || size != sizeof(value))
                return fallback;
            return static_cast<int>(value);
        };
        vLanguage = read(L"InputMethod", vLanguage) ? 1 : 0;
        vCheckSpelling = read(L"Spelling", vCheckSpelling);
        vRestoreIfWrongSpelling = read(L"RestoreIfInvalidWord", vRestoreIfWrongSpelling);
        vUseModernOrthography = read(L"ModernOrthography", vUseModernOrthography);
        vTelexStyle = read(L"TelexStyle", vTelexStyle);
        vSetCheckSpelling();
    }
    // These features depend on the legacy global hook/controller and are not
    // enabled in the in-process TIP yet.
    vFixRecommendBrowser = 0;
    vUseSmartSwitchKey = 0;
    vOtherLanguage = 0;
    vAutoSwitchByTyping = 0;
}

void loadTsfEngineSettings() {
    refreshTsfEngineSettings();
}
}}
