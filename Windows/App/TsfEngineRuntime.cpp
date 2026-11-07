#include "TsfEngineRuntime.h"
#include "Engine.h"
#include <windows.h>
#include <mutex>

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
void loadTsfEngineSettings() {
    static std::once_flag once;
    std::call_once(once, [] {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Vikey", 0, KEY_READ, &key) == ERROR_SUCCESS) {
            auto read = [key](const wchar_t* name, int fallback) {
                DWORD value = static_cast<DWORD>(fallback), size = sizeof(value), type = 0;
                if (RegQueryValueExW(key, name, nullptr, &type,
                    reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS || type != REG_DWORD)
                    return fallback;
                return static_cast<int>(value);
            };
            vLanguage = read(L"InputMethod", 1) ? 1 : 0;
            vCheckSpelling = read(L"Spelling", 1);
            vRestoreIfWrongSpelling = read(L"RestoreIfInvalidWord", 1);
            vUseModernOrthography = read(L"ModernOrthography", 1);
            vTelexStyle = read(L"TelexStyle", 0);
            RegCloseKey(key);
        }
        vFixRecommendBrowser = 0;
        vUseSmartSwitchKey = 0;
        vOtherLanguage = 0;
        vAutoSwitchByTyping = 0;
    });
}
}}
