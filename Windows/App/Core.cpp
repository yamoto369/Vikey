#include "Core.h"
#include "Prefs.h"
#include "Engine.h"
#include <algorithm>
#include <string>
#include <vector>

int vLanguage = 1, vSwitchKeyStatus = 0x7A000206, vCheckSpelling = 1, vUseModernOrthography = 1;
int vRestoreIfWrongSpelling = 1, vFixRecommendBrowser = 1, vUseSmartSwitchKey = 1, vOtherLanguage = 1;
int vAutoSwitchByTyping = 1, vAutoSwitchModeByTyping = 1, vPerformLayoutCompat = 0;

namespace vikey { namespace core {
namespace {
bool g_running = false;
std::function<void(int)> g_observer;
std::wstring g_frontApp = L"UnknownApp";
std::wstring g_siteHost;

std::wstring normalizeExe(std::wstring value) {
    const size_t slash = value.find_last_of(L"\\/");
    if (slash != std::wstring::npos) value = value.substr(slash + 1);
    std::transform(value.begin(), value.end(), value.begin(), ::towlower);
    return value;
}
bool blacklisted(const std::wstring& exe) {
    const std::wstring target = normalizeExe(exe);
    for (const auto& item : prefs::getStringList(L"AppBlacklist"))
        if (normalizeExe(item) == target) return true;
    return false;
}
std::wstring queryForegroundExe() {
    HWND window = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!pid || pid == GetCurrentProcessId()) return g_frontApp;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return L"UnknownApp";
    wchar_t path[MAX_PATH] = {};
    DWORD length = MAX_PATH;
    std::wstring result = L"UnknownApp";
    if (QueryFullProcessImageNameW(process, 0, path, &length)) {
        std::wstring full(path);
        const size_t slash = full.find_last_of(L"\\");
        result = slash == std::wstring::npos ? full : full.substr(slash + 1);
    }
    CloseHandle(process);
    return result;
}
std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    if (length) WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), length, nullptr, nullptr);
    return result;
}
std::string contextKey() {
    std::wstring key = g_frontApp;
    if (!g_siteHost.empty()) key += L"|" + g_siteHost;
    return toUtf8(key);
}
void bumpLanguageRevision() {
    prefs::setInt(L"LanguageRevision", prefs::getInt(L"LanguageRevision", 0) + 1);
}
void publishLanguage(bool bumpRevision) {
    prefs::setInt(L"InputMethod", vLanguage);
    if (bumpRevision) bumpLanguageRevision();
    if (g_observer) g_observer(vLanguage);
}
void saveSmartSwitchTable() {
    std::vector<Byte> data;
    getSmartSwitchKeySaveData(data);
    prefs::setBinary(L"smartSwitchKey", data.data(), (DWORD)data.size());
}
void rememberLanguageForContext() {
    if (!vUseSmartSwitchKey || blacklisted(g_frontApp)) return;
    setAppInputMethodStatus(contextKey(), vLanguage);
    saveSmartSwitchTable();
}
void applySmartSwitch() {
    if (!vUseSmartSwitchKey || blacklisted(g_frontApp)) return;
    const int status = getAppInputMethodStatus(contextKey(), vLanguage);
    if (status >= 0 && (status & 1) != vLanguage) {
        vLanguage = status & 1;
        publishLanguage(true);
    }
}
std::wstring tsfDllPath() {
    wchar_t module[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, module, MAX_PATH)) return {};
    std::wstring path(module);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    path.resize(slash + 1);
#if defined(_M_ARM64)
    path += L"VikeyTsf-arm64.dll";
#else
    path += L"VikeyTsf-x64.dll";
#endif
    return path;
}
bool registerTsfTip() {
    const std::wstring path = tsfDllPath();
    HMODULE module = LoadLibraryW(path.c_str());
    if (!module) return false;
    using RegisterFn = HRESULT (STDAPICALLTYPE*)();
    const auto fn = reinterpret_cast<RegisterFn>(GetProcAddress(module, "DllRegisterServer"));
    const HRESULT hr = fn ? fn() : E_NOINTERFACE;
    FreeLibrary(module);
    return SUCCEEDED(hr);
}
} // namespace

bool start() {
    if (g_running) return true;
    reloadSettings();
    const std::vector<BYTE> data = prefs::getBinary(L"smartSwitchKey");
    initSmartSwitchKey(data.data(), (int)data.size());
    g_frontApp = queryForegroundExe();
    g_running = registerTsfTip();
    return g_running;
}
void stop() { flushTypingLog(); g_running = false; }
bool isRunning() { return g_running; }
int language() { return vLanguage; }
void setLanguageObserver(std::function<void(int)> observer) { g_observer = std::move(observer); }

void setLanguageByUser(int lang) {
    lang = lang ? 1 : 0;
    if (vLanguage == lang) return;
    vLanguage = lang;
    rememberLanguageForContext();
    publishLanguage(true);
}
void setLanguageQuiet(int lang) {
    lang = lang ? 1 : 0;
    if (vLanguage == lang) return;
    vLanguage = lang;
    publishLanguage(true);
}
void notifyLanguageFromTip(int lang) {
    vLanguage = lang ? 1 : 0;
    if (g_observer) g_observer(vLanguage);
}
void reloadSettings() {
    vLanguage = prefs::getInt(L"InputMethod", 1) ? 1 : 0;
    vCheckSpelling = prefs::getInt(L"Spelling", 1); vSetCheckSpelling();
    vRestoreIfWrongSpelling = prefs::getInt(L"RestoreIfInvalidWord", 1);
    vUseModernOrthography = prefs::getInt(L"ModernOrthography", 1);
    vTelexStyle = prefs::getInt(L"TelexStyle", 0);
    vUseSmartSwitchKey = prefs::getInt(L"UseSmartSwitchKey", 1);
    vAutoSwitchByTyping = prefs::getInt(L"AutoSwitchByTyping", 1);
    vAutoSwitchModeByTyping = prefs::getInt(L"AutoSwitchModeByTyping", 1);
    vOtherLanguage = prefs::getInt(L"OtherLanguage", 1);
    vFixRecommendBrowser = prefs::getInt(L"FixRecommendBrowser", 1);
    const int hotkey = prefs::getInt(L"SwitchKeyStatus", 0);
    vSwitchKeyStatus = hotkey ? hotkey : 0x7A000206;
    if (g_observer) g_observer(vLanguage);
}
void onForegroundChanged() {
    const std::wstring next = queryForegroundExe();
    if (next == g_frontApp) return;
    g_frontApp = next;
    g_siteHost.clear();
    prefs::setString(L"CurrentSiteHost", L"");
    resetTyping();
    applySmartSwitch();
}
void onSiteChanged(const std::wstring& host) {
    if (host == g_siteHost) return;
    g_siteHost = host;
    prefs::setString(L"CurrentSiteHost", host);
    resetTyping();
    applySmartSwitch();
}
std::wstring frontAppName() { return g_frontApp; }
bool isFrontAppBlacklisted() { return blacklisted(g_frontApp); }
void addFrontAppToBlacklist() {
    if (g_frontApp.empty() || g_frontApp == L"UnknownApp") return;
    auto values = prefs::getStringList(L"AppBlacklist");
    const std::wstring exe = normalizeExe(g_frontApp);
    for (const auto& item : values) if (normalizeExe(item) == exe) return;
    values.push_back(exe);
    prefs::setStringList(L"AppBlacklist", values);
    resetTyping();
}
void removeAppFromBlacklist(const std::wstring& exeName) {
    const std::wstring target = normalizeExe(exeName);
    auto values = prefs::getStringList(L"AppBlacklist");
    values.erase(std::remove_if(values.begin(), values.end(), [&](const std::wstring& value) {
        return normalizeExe(value) == target;
    }), values.end());
    prefs::setStringList(L"AppBlacklist", values);
    resetTyping();
}
std::vector<std::wstring> blacklistedApps() { return prefs::getStringList(L"AppBlacklist"); }
void resetTyping() {
    prefs::setInt(L"TypingRevision", prefs::getInt(L"TypingRevision", 0) + 1);
}
std::wstring typingLogPath() { return prefs::appDataDir() + L"\\typing-log.jsonl"; }
void flushTypingLog() {}
void clearTypingLog() {
    const std::wstring path = typingLogPath();
    DeleteFileW(path.c_str());
    DeleteFileW((prefs::appDataDir() + L"\\typing-log.1.jsonl").c_str());
}
}} // namespace
