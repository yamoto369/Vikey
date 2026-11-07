#include "tip.h"
#include "tsf_guids.h"
#include "../App/Prefs.h"
#include "../App/KeyMap.h"
#include <inputscope.h>
#include <oleauto.h>
#include <algorithm>
#include <new>
#include <string>
#include <vector>

extern int vAutoSwitchModeByTyping;

namespace {
constexpr UINT WM_VIKEY_LANGUAGE = WM_APP + 7;
constexpr unsigned VIKEY_MOD_CONTROL = 0x100, VIKEY_MOD_ALT = 0x200, VIKEY_MOD_WIN = 0x400, VIKEY_MOD_SHIFT = 0x800;

bool isTextKey(WPARAM vk) {
    return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') ||
        (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) ||
        vk == VK_BACK || vk == VK_SPACE || vk == VK_RETURN ||
        vk == VK_OEM_1 || vk == VK_OEM_2 || vk == VK_OEM_3 ||
        vk == VK_OEM_4 || vk == VK_OEM_5 || vk == VK_OEM_6 ||
        vk == VK_OEM_7 || vk == VK_OEM_COMMA || vk == VK_OEM_PERIOD ||
        vk == VK_OEM_MINUS || vk == VK_OEM_PLUS;
}
bool isNavigationKey(WPARAM vk) {
    return vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
        vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
        vk == VK_DELETE || vk == VK_INSERT;
}
bool isPassthroughWordBreak(WPARAM vk) {
    return vk == VK_DECIMAL || vk == VK_ADD || vk == VK_SUBTRACT ||
        vk == VK_MULTIPLY || vk == VK_DIVIDE || vk == VK_SEPARATOR ||
        vk == VK_OEM_102 || vk == VK_OEM_8 || vk == VK_CLEAR;
}
bool isPrivateScope(ITfContext* context, TfEditCookie cookie, ITfRange* range) {
    ITfReadOnlyProperty* property = nullptr;
    if (FAILED(context->GetAppProperty(GUID_VikeyInputScope, &property)) || !property)
        return false;
    VARIANT value; VariantInit(&value);
    bool privateInput = false;
    if (SUCCEEDED(property->GetValue(cookie, range, &value)) &&
        (value.vt == VT_UNKNOWN || value.vt == VT_DISPATCH) && value.punkVal) {
        ITfInputScope* scope = nullptr;
        if (SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&scope))) && scope) {
            InputScope* scopes = nullptr;
            UINT count = 0;
            if (SUCCEEDED(scope->GetInputScopes(&scopes, &count)) && scopes) {
                for (UINT i = 0; i < count; ++i) {
                    if (scopes[i] == IS_PASSWORD || scopes[i] == IS_PRIVATE ||
                        scopes[i] == IS_NUMERIC_PASSWORD || scopes[i] == IS_NUMERIC_PIN ||
                        scopes[i] == IS_ALPHANUMERIC_PIN || scopes[i] == IS_ALPHANUMERIC_PIN_SET) {
                        privateInput = true;
                        break;
                    }
                }
                CoTaskMemFree(scopes);
            }
            scope->Release();
        }
    }
    VariantClear(&value);
    property->Release();
    return privateInput;
}

class TextEditSession final : public ITfEditSession {
public:
    TextEditSession(ITfContext* context, const VKEdit& edit, bool* privateInput)
        : context_(context), edit_(edit), privateInput_(privateInput) { context_->AddRef(); }
    ~TextEditSession() { context_->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfEditSession) {
            *out = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --refs_;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
        TF_SELECTION selection = {};
        ULONG fetched = 0;
        HRESULT hr = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
        if (FAILED(hr) || fetched != 1 || !selection.range)
            return FAILED(hr) ? hr : TF_E_NOSELECTION;

        if (isPrivateScope(context_, cookie, selection.range)) {
            if (privateInput_) *privateInput_ = true;
            selection.range->Release();
            return S_FALSE;
        }
        if (!edit_.deleteCount && edit_.text.empty()) {
            selection.range->Release();
            return S_OK;
        }

        BOOL empty = FALSE;
        hr = selection.range->IsEmpty(cookie, &empty);
        // A selected range is replaced as-is. Backspace on a collapsed selection
        // expands the edit range to the left before inserting the engine's result.
        if (SUCCEEDED(hr) && empty && edit_.deleteCount > 0) {
            LONG shifted = 0;
            hr = selection.range->ShiftStart(cookie, -edit_.deleteCount, &shifted, nullptr);
            if (SUCCEEDED(hr) && shifted != -edit_.deleteCount) hr = TF_E_INVALIDPOS;
        }
        if (SUCCEEDED(hr)) {
            hr = selection.range->SetText(cookie, 0,
                reinterpret_cast<const WCHAR*>(edit_.text.c_str()), (LONG)edit_.text.size());
        }
        if (SUCCEEDED(hr)) {
            hr = selection.range->Collapse(cookie, TF_ANCHOR_END);
            if (SUCCEEDED(hr)) {
                selection.style.ase = TF_AE_NONE;
                selection.style.fInterimChar = FALSE;
                hr = context_->SetSelection(cookie, 1, &selection);
            }
        }
        selection.range->Release();
        return hr;
    }
private:
    std::atomic<ULONG> refs_{1};
    ITfContext* context_;
    VKEdit edit_;
    bool* privateInput_;
};

std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    if (length) WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), length, nullptr, nullptr);
    return result;
}
std::wstring exeForContext(ITfContext* context) {
    ITfContextView* view = nullptr;
    if (FAILED(context->GetActiveView(&view)) || !view) return {};
    HWND window = nullptr; view->GetWnd(&window); view->Release();
    DWORD pid = 0; if (window) GetWindowThreadProcessId(window, &pid);
    if (!pid) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t path[MAX_PATH] = {}; DWORD length = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, path, &length)) {
        std::wstring full(path);
        const size_t slash = full.find_last_of(L"\\");
        result = slash == std::wstring::npos ? full : full.substr(slash + 1);
    }
    CloseHandle(process);
    return result;
}
bool exeBlacklisted(const std::wstring& exe) {
    std::wstring target = exe;
    std::transform(target.begin(), target.end(), target.begin(), ::towlower);
    for (auto entry : vikey::prefs::getStringList(L"AppBlacklist")) {
        const size_t slash = entry.find_last_of(L"\\/");
        if (slash != std::wstring::npos) entry = entry.substr(slash + 1);
        std::transform(entry.begin(), entry.end(), entry.begin(), ::towlower);
        if (!target.empty() && target == entry) return true;
    }
    return false;
}
double nowSeconds() { return (double)GetTickCount64() / 1000.0; }
}

VikeyTextService::VikeyTextService() { VikeyTsfObjectCreated(); }
VikeyTextService::~VikeyTextService() {
    Deactivate();
    VikeyTsfObjectReleased();
}
HRESULT VikeyTextService::QueryInterface(REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor)
        *out = static_cast<ITfTextInputProcessor*>(this);
    else if (iid == IID_ITfTextInputProcessorEx)
        *out = static_cast<ITfTextInputProcessorEx*>(this);
    else if (iid == IID_ITfKeyEventSink)
        *out = static_cast<ITfKeyEventSink*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}
ULONG VikeyTextService::AddRef() { return ++refs_; }
ULONG VikeyTextService::Release() {
    const ULONG count = --refs_;
    if (!count) delete this;
    return count;
}
HRESULT CreateVikeyTextService(REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    auto* service = new (std::nothrow) VikeyTextService();
    if (!service) return E_OUTOFMEMORY;
    const HRESULT hr = service->QueryInterface(iid, out);
    service->Release();
    return hr;
}
HRESULT VikeyTextService::Activate(ITfThreadMgr* manager, TfClientId client) {
    return ActivateEx(manager, client, 0);
}
HRESULT VikeyTextService::ActivateEx(ITfThreadMgr* manager, TfClientId client, DWORD) {
    if (!manager) return E_INVALIDARG;
    if (active_) return S_OK;
    threadMgr_ = manager; threadMgr_->AddRef(); clientId_ = client;
    settingsLoaded_ = false;
    ITfKeystrokeMgr* keyManager = nullptr;
    HRESULT hr = threadMgr_->QueryInterface(IID_PPV_ARGS(&keyManager));
    if (SUCCEEDED(hr)) {
        hr = keyManager->AdviseKeyEventSink(clientId_, this, TRUE);
        keyManager->Release();
    }
    if (FAILED(hr)) {
        threadMgr_->Release(); threadMgr_ = nullptr; clientId_ = TF_CLIENTID_NULL;
        return hr;
    }
    active_ = true;
    VikeyTsfLog("tsf-tip.log", "activate", S_OK, (unsigned long)clientId_, 0);
    return S_OK;
}
HRESULT VikeyTextService::Deactivate() {
    if (active_ && threadMgr_) {
        ITfKeystrokeMgr* keyManager = nullptr;
        if (SUCCEEDED(threadMgr_->QueryInterface(IID_PPV_ARGS(&keyManager)))) {
            keyManager->UnadviseKeyEventSink(clientId_);
            keyManager->Release();
        }
    }
    active_ = false;
    if (threadMgr_) { threadMgr_->Release(); threadMgr_ = nullptr; }
    clientId_ = TF_CLIENTID_NULL;
    settingsLoaded_ = false;
    return S_OK;
}
HRESULT VikeyTextService::OnSetFocus(BOOL) { return S_OK; }

bool VikeyTextService::IsKeyboardDisabled() const {
    if (!threadMgr_) return true;
    ITfCompartmentMgr* compartments = nullptr;
    if (FAILED(threadMgr_->QueryInterface(IID_PPV_ARGS(&compartments))) || !compartments) return false;
    bool disabled = false;
    const GUID* ids[] = { &GUID_COMPARTMENT_KEYBOARD_DISABLED, &GUID_COMPARTMENT_EMPTYCONTEXT };
    for (const GUID* id : ids) {
        ITfCompartment* compartment = nullptr;
        if (SUCCEEDED(compartments->GetCompartment(*id, &compartment)) && compartment) {
            VARIANT value; VariantInit(&value);
            if (SUCCEEDED(compartment->GetValue(&value)) &&
                ((value.vt == VT_I4 && value.lVal != 0) ||
                 (value.vt == VT_UI4 && value.ulVal != 0))) {
                disabled = true;
                VikeyTsfLog("tsf-tip.log", "keyboard-compartment-set", S_OK,
                    IsEqualGUID(*id, GUID_COMPARTMENT_KEYBOARD_DISABLED) ? 1UL : 2UL,
                    value.vt == VT_I4 ? (unsigned long)value.lVal : (unsigned long)value.ulVal);
            }
            VariantClear(&value);
            compartment->Release();
        }
        if (disabled) break;
    }
    compartments->Release();
    return disabled;
}
bool VikeyTextService::IsBlacklistedContext(ITfContext* context) const {
    return exeBlacklisted(exeForContext(context));
}
void VikeyTextService::SyncSettingsAndContext(ITfContext* context) {
    const int languageRevision = vikey::prefs::getInt(L"LanguageRevision", 0);
    const int typingRevision = vikey::prefs::getInt(L"TypingRevision", 0);
    const int language = vikey::prefs::getInt(L"InputMethod", 1) ? 1 : 0;
    if (!settingsLoaded_ || languageRevision != lastLanguageRevision_) {
        driver_.resetContext();
        driver_.setLanguage(language);
        lastLanguageRevision_ = languageRevision;
        settingsLoaded_ = true;
        const auto data = vikey::prefs::getBinary(L"smartSwitchKey");
        initSmartSwitchKey(data.data(), (int)data.size());
    }
    if (typingRevision != lastTypingRevision_) {
        driver_.resetContext();
        lastTypingRevision_ = typingRevision;
    }
    vCheckSpelling = vikey::prefs::getInt(L"Spelling", 1); vSetCheckSpelling();
    vRestoreIfWrongSpelling = vikey::prefs::getInt(L"RestoreIfInvalidWord", 1);
    vUseModernOrthography = vikey::prefs::getInt(L"ModernOrthography", 1);
    vTelexStyle = vikey::prefs::getInt(L"TelexStyle", 0);
    vOtherLanguage = vikey::prefs::getInt(L"OtherLanguage", 1);
    vFixRecommendBrowser = vikey::prefs::getInt(L"FixRecommendBrowser", 1);
    vUseSmartSwitchKey = vikey::prefs::getInt(L"UseSmartSwitchKey", 1);
    vAutoSwitchByTyping = vikey::prefs::getInt(L"AutoSwitchByTyping", 1);
    vAutoSwitchModeByTyping = vikey::prefs::getInt(L"AutoSwitchModeByTyping", 1);
    vSwitchKeyStatus = vikey::prefs::getInt(L"SwitchKeyStatus", 0);
    if (!vSwitchKeyStatus) vSwitchKeyStatus = 0x7A000206;
    driver_.autoSwitch = vAutoSwitchByTyping != 0;
    driver_.autoSwitchModeByTyping = vAutoSwitchModeByTyping != 0;
    driver_.pauseAfterManual = vikey::prefs::getInt(L"PauseAutoAfterManual", 1) ? 5.0 : 0.0;

    const std::wstring nextApp = exeForContext(context);
    const std::wstring site = vikey::prefs::getString(L"CurrentSiteHost", L"");
    const std::string nextKey = toUtf8(nextApp + (site.empty() ? L"" : L"|" + site));
    if (nextKey != contextKey_) {
        driver_.resetContext();
        contextKey_ = nextKey;
        appName_ = nextApp;
        if (!contextKey_.empty()) {
            const int saved = getAppInputMethodStatus(contextKey_, driver_.language());
            if (saved >= 0) driver_.setLanguage(saved & 1);
        }
    }
}
bool VikeyTextService::MatchesSwitchHotkey(WPARAM vk) const {
    const int configured = vikey::prefs::getInt(L"SwitchKeyStatus", 0);
    const int hotkey = configured ? configured : 0x7A000206;
    const Uint16 key = vikey::engineKey((DWORD)vk);
    if ((hotkey & 0xFF) == 0xFE || (hotkey & 0xFF) != key) return false;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;
    return control == ((hotkey & VIKEY_MOD_CONTROL) != 0) &&
        alt == ((hotkey & VIKEY_MOD_ALT) != 0) &&
        win == ((hotkey & VIKEY_MOD_WIN) != 0) &&
        shift == ((hotkey & VIKEY_MOD_SHIFT) != 0);
}
void VikeyTextService::PersistLanguage(bool notifyTray) {
    const int language = driver_.language() ? 1 : 0;
    vikey::prefs::setInt(L"InputMethod", language);
    const int revision = vikey::prefs::getInt(L"LanguageRevision", 0) + 1;
    vikey::prefs::setInt(L"LanguageRevision", revision);
    lastLanguageRevision_ = revision;
    if (!contextKey_.empty() && vUseSmartSwitchKey && !exeBlacklisted(appName_)) {
        setAppInputMethodStatus(contextKey_, language);
        std::vector<Byte> data;
        getSmartSwitchKeySaveData(data);
        vikey::prefs::setBinary(L"smartSwitchKey", data.data(), (DWORD)data.size());
    }
    if (notifyTray) {
        HWND tray = FindWindowW(L"VikeyTray", nullptr);
        if (tray) PostMessageW(tray, WM_VIKEY_LANGUAGE, (WPARAM)language, 0);
    }
}
HRESULT VikeyTextService::OnTestKeyDown(ITfContext* context, WPARAM vk, LPARAM, BOOL* eaten) {
    if (!eaten) return E_POINTER;
    *eaten = FALSE;
    if (!active_ || !context) {
        VikeyTsfLog("tsf-tip.log", "test-key-no-context", E_UNEXPECTED, (unsigned long)vk);
        return S_OK;
    }
    if (IsKeyboardDisabled()) {
        VikeyTsfLog("tsf-tip.log", "test-key-disabled", S_OK, (unsigned long)vk);
        return S_OK;
    }
    if (IsBlacklistedContext(context)) {
        VikeyTsfLog("tsf-tip.log", "test-key-blacklisted", S_OK, (unsigned long)vk);
        return S_OK;
    }
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
        (GetKeyState(VK_RWIN) & 0x8000) != 0;
    if (control || alt || win) {
        *eaten = TRUE; // let OnKeyDown decide whether to switch or pass the shortcut through
        return S_OK;
    }
    *eaten = (isTextKey(vk) || isNavigationKey(vk) || isPassthroughWordBreak(vk)) ? TRUE : FALSE;
    VikeyTsfLog("tsf-tip.log", "test-key-routed", S_OK, (unsigned long)vk, *eaten ? 1UL : 0UL);
    return S_OK;
}
HRESULT VikeyTextService::OnKeyDown(ITfContext* context, WPARAM vk, LPARAM flags, BOOL* eaten) {
    if (!eaten) return E_POINTER;
    *eaten = FALSE;
    return ProcessKey(context, vk, flags, eaten);
}
HRESULT VikeyTextService::ProcessKey(ITfContext* context, WPARAM vk, LPARAM, BOOL* eaten) {
    if (!context || !active_) return E_INVALIDARG;
    if (IsKeyboardDisabled() || IsBlacklistedContext(context)) {
        driver_.resetContext();
        return S_OK;
    }
    SyncSettingsAndContext(context);
    if (vOtherLanguage) {
        const HKL layout = GetKeyboardLayout(0);
        const WORD primary = PRIMARYLANGID(LOWORD((DWORD_PTR)layout));
        if (primary != LANG_ENGLISH && primary != LANG_VIETNAMESE && primary != LANG_NEUTRAL) {
            driver_.resetContext();
            return S_OK;
        }
    }
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
        (GetKeyState(VK_RWIN) & 0x8000) != 0;
    if ((control || alt || win) && MatchesSwitchHotkey(vk)) {
        driver_.manualSwitch(nowSeconds());
        PersistLanguage(true);
        *eaten = TRUE;
        VikeyTsfLog("tsf-tip.log", "hotkey-switch", S_OK, (unsigned long)vk, (unsigned long)driver_.language());
        return S_OK;
    }
    if (control || alt || win || isNavigationKey(vk) || isPassthroughWordBreak(vk) || !isTextKey(vk)) {
        driver_.resetContext();
        return S_OK; // the application still receives its original shortcut/navigation key
    }
    if (vk == VK_TAB) {
        driver_.resetContext();
        return S_OK;
    }

    const Uint16 key = vikey::engineKey((DWORD)vk);
    if (key == KEY_EMPTY) {
        driver_.resetContext();
        return S_OK;
    }
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
    const VKEdit edit = driver_.pressWithCaps(key, shift, caps, nowSeconds());
    bool privateInput = false;
    auto* session = new (std::nothrow) TextEditSession(context, edit, &privateInput);
    if (!session) { driver_.resetContext(); return E_OUTOFMEMORY; }
    HRESULT sessionResult = E_FAIL;
    const HRESULT request = context->RequestEditSession(clientId_, session,
        TF_ES_SYNC | TF_ES_READWRITE, &sessionResult);
    session->Release();

    if (FAILED(request) || FAILED(sessionResult) || privateInput) {
        driver_.resetContext();
        VikeyTsfLog("tsf-tip.log",
            privateInput ? "private-scope-pass-through" : "edit-session-failed",
            FAILED(request) ? request : sessionResult, (unsigned long)vk, privateInput ? 1 : 0);
        *eaten = FALSE;
        return S_OK; // pass through to the app; no global-hook fallback exists
    }
    if (edit.languageChanged) PersistLanguage(true);
    *eaten = TRUE;
    VikeyTsfLog("tsf-tip.log", "key-edited", S_OK, (unsigned long)vk,
        ((unsigned long)edit.deleteCount << 16) | (unsigned long)edit.text.size());
    return S_OK;
}
HRESULT VikeyTextService::OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) {
    if (!eaten) return E_POINTER;
    *eaten = FALSE;
    return S_OK;
}
HRESULT VikeyTextService::OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) {
    if (!eaten) return E_POINTER;
    *eaten = FALSE;
    return S_OK;
}
HRESULT VikeyTextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) {
    if (!eaten) return E_POINTER;
    *eaten = FALSE;
    return S_OK;
}
