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
