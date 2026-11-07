#include "TsfController.h"
#include "TsfDiagnostics.h"
#include <windows.h>
#include <msctf.h>
#include <objbase.h>
#include <string>

namespace {
const CLSID kVikeyClsid =
    { 0x9b7a4d31, 0x6d8f, 0x4d22, { 0xa1, 0x73, 0x5e, 0x91, 0x2c, 0x44, 0x8b, 0x60 } };
const GUID kVikeyProfile =
    { 0x4e7c1a26, 0x8d39, 0x4f51, { 0xb2, 0x07, 0x61, 0x33, 0x9a, 0xc8, 0x42, 0x15 } };
HMODULE g_tipModule = nullptr;
using RegisterServerFn = HRESULT (WINAPI*)();
RegisterServerFn g_registerServer = nullptr;
bool g_comInitialized = false;
ITfThreadMgr* g_threadMgr = nullptr;
ITfInputProcessorProfiles* g_profiles = nullptr;
TfClientId g_clientId = TF_CLIENTID_NULL;
bool g_ready = false;
LANGID g_activeLanguage = 0;

#if defined(_M_ARM64)
const wchar_t* kTipFileName = L"VikeyTsf-arm64.dll";
#else
const wchar_t* kTipFileName = L"VikeyTsf-x64.dll";
#endif

std::wstring modulePath() {
    wchar_t path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    std::wstring result(path, n);
    const size_t slash = result.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    result.resize(slash + 1);
    result += kTipFileName;
    return result;
}

LANGID foregroundLanguage() {
    HWND foreground = GetForegroundWindow();
    DWORD threadId = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    HKL layout = GetKeyboardLayout(threadId ? threadId : GetCurrentThreadId());
    return LOWORD(reinterpret_cast<ULONG_PTR>(layout));
}

bool supportedLanguage(LANGID language) {
    const WORD primary = PRIMARYLANGID(language);
    return primary == LANG_ENGLISH || primary == LANG_VIETNAMESE || primary == LANG_NEUTRAL;
}

HRESULT updateComClassPath(const std::wstring& dllPath) {
    wchar_t clsid[64] = {};
    if (!StringFromGUID2(kVikeyClsid, clsid, _countof(clsid))) return E_FAIL;
    const std::wstring keyPath = std::wstring(L"Software\\Classes\\CLSID\\") + clsid + L"\\InprocServer32";
    HKEY key = nullptr;
    LONG error = RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS) { vikey::tsf::diagnostics::result(L"COM registry key creation failed", HRESULT_FROM_WIN32(error), error); return HRESULT_FROM_WIN32(error); }
    error = RegSetValueExW(key, nullptr, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(dllPath.c_str()),
        static_cast<DWORD>((dllPath.size() + 1) * sizeof(wchar_t)));
    if (error == ERROR_SUCCESS) {
        const wchar_t threading[] = L"Apartment";
        error = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(threading), sizeof(threading));
    }
    RegCloseKey(key);
    vikey::tsf::diagnostics::result(L"COM registry path update", HRESULT_FROM_WIN32(error), error);
    return HRESULT_FROM_WIN32(error);
}

bool isProcessorRegistered(ITfInputProcessorProfiles* profiles) {
    IEnumGUID* enumerator = nullptr;
    if (FAILED(profiles->EnumInputProcessorInfo(&enumerator)) || !enumerator) return false;
    bool found = false;
    GUID item = GUID_NULL;
    ULONG fetched = 0;
    while (enumerator->Next(1, &item, &fetched) == S_OK && fetched == 1) {
        if (IsEqualGUID(item, kVikeyClsid)) { found = true; break; }
    }
    enumerator->Release();
    return found;
}

bool hasLanguageProfile(ITfInputProcessorProfiles* profiles, LANGID language) {
    IEnumTfLanguageProfiles* enumerator = nullptr;
    HRESULT hr = profiles->EnumLanguageProfiles(language, &enumerator);
    if (FAILED(hr) || !enumerator) return false;
    bool found = false;
    TF_LANGUAGEPROFILE profile{};
    ULONG fetched = 0;
    while (enumerator->Next(1, &profile, &fetched) == S_OK && fetched == 1) {
        if (IsEqualGUID(profile.clsid, kVikeyClsid) &&
            IsEqualGUID(profile.guidProfile, kVikeyProfile)) {
            found = true;
            break;
        }
    }
    enumerator->Release();
    return found;
}

bool ensureLanguageProfile(ITfInputProcessorProfiles* profiles, LANGID language) {
    if (!hasLanguageProfile(profiles, language)) {
        BSTR description = SysAllocString(L"Vikey Vietnamese Input");
        if (!description) { vikey::tsf::diagnostics::result(L"SysAllocString failed", E_OUTOFMEMORY); return false; }
        HRESULT addHr = profiles->AddLanguageProfile(kVikeyClsid, language, kVikeyProfile,
            description, static_cast<ULONG>(SysStringLen(description)), nullptr, 0, 0);
        SysFreeString(description);
        if (FAILED(addHr) && !hasLanguageProfile(profiles, language)) { vikey::tsf::diagnostics::result(L"AddLanguageProfile failed", addHr); return false; }
    }

    BOOL enabled = FALSE;
    HRESULT hr = profiles->IsEnabledLanguageProfile(kVikeyClsid, language, kVikeyProfile, &enabled);
    if (FAILED(hr)) { vikey::tsf::diagnostics::result(L"IsEnabledLanguageProfile failed", hr); return false; }
    if (!enabled) {
        hr = profiles->EnableLanguageProfile(kVikeyClsid, language, kVikeyProfile, TRUE);
        if (FAILED(hr)) { vikey::tsf::diagnostics::result(L"EnableLanguageProfile failed", hr); return false; }
    }
    vikey::tsf::diagnostics::write(L"Language profile is registered and enabled");
    return true;
}

bool ensureKeyboardCategory() {
    ITfCategoryMgr* categories = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&categories));
    if (FAILED(hr) || !categories) { vikey::tsf::diagnostics::result(L"Create TSF category manager failed", hr); return false; }
    IEnumGUID* enumerator = nullptr;
    hr = categories->EnumCategoriesInItem(kVikeyProfile, &enumerator);
    bool found = false;
    if (SUCCEEDED(hr) && enumerator) {
        GUID category = GUID_NULL;
        ULONG fetched = 0;
        while (enumerator->Next(1, &category, &fetched) == S_OK && fetched == 1) {
            if (IsEqualGUID(category, GUID_TFCAT_TIP_KEYBOARD)) { found = true; break; }
        }
        enumerator->Release();
    }
    if (!found) hr = categories->RegisterCategory(kVikeyClsid, GUID_TFCAT_TIP_KEYBOARD, kVikeyProfile);
    categories->Release();
    vikey::tsf::diagnostics::result(L"Register keyboard TIP category", hr);
    return SUCCEEDED(hr);
}

bool ensureRegistered(const std::wstring& dllPath) {
    vikey::tsf::diagnostics::write((L"Registering COM class for DLL: " + dllPath).c_str());
    HRESULT hr = updateComClassPath(dllPath);
    if (FAILED(hr)) return false;

    ITfInputProcessorProfiles* profiles = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&profiles));
    if (FAILED(hr) || !profiles) { vikey::tsf::diagnostics::result(L"Create TSF profiles manager failed", hr); return false; }

    if (!isProcessorRegistered(profiles)) {
        if (!g_registerServer) { vikey::tsf::diagnostics::write(L"DllRegisterServer export missing"); profiles->Release(); return false; }
        const HRESULT registerHr = g_registerServer();
        vikey::tsf::diagnostics::result(L"DllRegisterServer", registerHr);
        if (FAILED(registerHr)) { profiles->Release(); return false; }
        profiles->Release();
        profiles = nullptr;
        hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&profiles));
        if (FAILED(hr) || !profiles) { vikey::tsf::diagnostics::result(L"Recreate TSF profiles manager failed", hr); return false; }
    } else {
        vikey::tsf::diagnostics::write(L"COM input processor already registered");
    }

    // Keep the legacy Vietnamese profile registered, and add a profile for
    // the user's current keyboard language. This lets TSF operate without
    // changing the active Windows keyboard layout.
    if (!ensureLanguageProfile(profiles, 0x042A)) { vikey::tsf::diagnostics::write(L"Vietnamese profile setup failed");
        profiles->Release();
        return false;
    }
    LANGID current = 0;
    const HRESULT currentHr = profiles->GetCurrentLanguage(&current);
    vikey::tsf::diagnostics::result(L"GetCurrentLanguage during registration", currentHr);
    const bool profileReady = SUCCEEDED(currentHr) && current && ensureLanguageProfile(profiles, current);
    profiles->Release();
    if (!profileReady) { vikey::tsf::diagnostics::write(L"Current language profile setup failed"); return false; }
    const bool categoryReady = ensureKeyboardCategory();
    vikey::tsf::diagnostics::write(categoryReady ? L"COM and TSF profile registration completed" : L"TSF keyboard category registration failed");
    return categoryReady;
}

bool isActiveProfile(LANGID language) {
    if (!g_profiles) return false;
    LANGID activeLanguage = 0;
    GUID activeProfile = GUID_NULL;
    HRESULT hr = g_profiles->GetActiveLanguageProfile(kVikeyClsid, &activeLanguage, &activeProfile);
    return SUCCEEDED(hr) && activeLanguage == language &&
        IsEqualGUID(activeProfile, kVikeyProfile);
}

bool deactivateActiveProfile() {
    if (!g_activeLanguage) return true;
    ITfInputProcessorProfileMgr* manager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
    if (FAILED(hr) || !manager) { g_activeLanguage = 0; return false; }
    hr = manager->DeactivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, g_activeLanguage,
        kVikeyClsid, kVikeyProfile, nullptr, TF_IPPMF_FORSESSION | TF_IPPMF_DISABLEPROFILE);
    manager->Release();
    g_activeLanguage = 0;
    // E_FAIL can mean that Windows already deactivated the profile after a
    // keyboard-layout change; the requested state is still inactive.
    return SUCCEEDED(hr) || hr == E_FAIL;
}
}

namespace vikey { namespace tsf {
bool initialize() {
    if (g_ready) { vikey::tsf::diagnostics::write(L"initialize called: already ready"); return true; }
    vikey::tsf::diagnostics::write(L"========== TSF initialization started ==========");
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    vikey::tsf::diagnostics::result(L"CoInitializeEx", hr);
    if (FAILED(hr)) return false;
    g_comInitialized = true;

    const std::wstring path = modulePath();
    if (path.empty()) { vikey::tsf::diagnostics::write(L"Cannot determine DLL path"); shutdown(); return false; }
    vikey::tsf::diagnostics::write((L"TSF DLL path: " + path).c_str());
    SetLastError(ERROR_SUCCESS);
    g_tipModule = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_tipModule) { const DWORD error = GetLastError(); vikey::tsf::diagnostics::result(L"LoadLibraryExW failed", HRESULT_FROM_WIN32(error), error); shutdown(); return false; }
    vikey::tsf::diagnostics::write(L"LoadLibraryExW succeeded");
    SetLastError(ERROR_SUCCESS);
    g_registerServer = reinterpret_cast<RegisterServerFn>(GetProcAddress(g_tipModule, "DllRegisterServer"));
    if (!g_registerServer) { const DWORD error = GetLastError(); vikey::tsf::diagnostics::result(L"GetProcAddress(DllRegisterServer) failed", HRESULT_FROM_WIN32(error), error); shutdown(); return false; }
    vikey::tsf::diagnostics::write(L"DllRegisterServer export found");
    if (!ensureRegistered(path)) { vikey::tsf::diagnostics::write(L"ensureRegistered failed"); shutdown(); return false; }

    // Register and enable the language profile before activating TSF on
    // this thread. This follows the documented TSF profile activation order.
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&g_profiles));
    if (FAILED(hr) || !g_profiles) { vikey::tsf::diagnostics::result(L"Create TSF profiles manager failed", hr); shutdown(); return false; }
    LANGID currentLanguage = 0;
    hr = g_profiles->GetCurrentLanguage(&currentLanguage);
    vikey::tsf::diagnostics::result(L"GetCurrentLanguage", hr);
    if (FAILED(hr) || !currentLanguage || !ensureLanguageProfile(g_profiles, currentLanguage)) {
        vikey::tsf::diagnostics::write(L"Current language profile unavailable"); shutdown(); return false;
    }

    hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&g_threadMgr));
    if (FAILED(hr) || !g_threadMgr) { vikey::tsf::diagnostics::result(L"Create TSF thread manager failed", hr); shutdown(); return false; }
    vikey::tsf::diagnostics::write(L"TSF thread manager created");
    hr = g_threadMgr->Activate(&g_clientId);
    vikey::tsf::diagnostics::result(L"ITfThreadMgr::Activate", hr);
    if (FAILED(hr) || g_clientId == TF_CLIENTID_NULL) { vikey::tsf::diagnostics::write(L"TSF thread manager activation failed"); shutdown(); return false; }

    g_ready = true;
    vikey::tsf::diagnostics::write(L"========== TSF initialization succeeded ==========");
    return true;
}

bool sync(bool vietnameseEnabled) {
    static int lastState = -1;
    if (!g_ready) { if (lastState != 0) vikey::tsf::diagnostics::write(L"TSF sync: controller not initialized"); lastState = 0; return false; }
    if (!vietnameseEnabled) {
        deactivateActiveProfile();
        if (lastState != 1) vikey::tsf::diagnostics::write(L"TSF profile inactive: Vietnamese mode disabled");
        lastState = 1;
        return false;
    }

    LANGID language = 0;
    HRESULT hr = g_profiles ? g_profiles->GetCurrentLanguage(&language) : E_UNEXPECTED;
    const LANGID foreground = foregroundLanguage();
    // TSF profile activation requires the caller's current input language to match.
    // If an application has a different per-window keyboard layout, retain
    // the legacy hook for that window rather than changing its layout.
    if (!language || language != foreground || !supportedLanguage(language)) {
        deactivateActiveProfile();
        if (lastState != 2) { wchar_t msg[256] = {}; swprintf_s(msg, _countof(msg), L"TSF not activated: current=0x%04X foreground=0x%04X supported=%d", language, foreground, supportedLanguage(language) ? 1 : 0); vikey::tsf::diagnostics::write(msg); }
        lastState = 2;
        return false;
    }
    if (g_activeLanguage == language && isActiveProfile(language)) { if (lastState != 3) vikey::tsf::diagnostics::write(L"TSF profile active"); lastState = 3; return true; }
    // Another input processor may have replaced Vikey since the previous
    // timer tick. Do not claim the hook is safely bypassed unless TSF confirms
    // that Vikey is still the active keyboard profile.
    if (g_activeLanguage == language) g_activeLanguage = 0;
    deactivateActiveProfile();
    if (FAILED(hr) || !g_profiles) { vikey::tsf::diagnostics::result(L"GetCurrentLanguage during sync failed", hr); lastState = 4; return false; }
    const bool ready = ensureLanguageProfile(g_profiles, language);
    if (!ready) { if (lastState != 5) vikey::tsf::diagnostics::write(L"TSF sync: language profile setup failed"); lastState = 5; return false; }

    hr = g_profiles->ActivateLanguageProfile(kVikeyClsid, language, kVikeyProfile);
    vikey::tsf::diagnostics::result(L"ActivateLanguageProfile", hr);
    if (FAILED(hr) || !isActiveProfile(language)) { if (lastState != 6) vikey::tsf::diagnostics::write(L"TSF profile did not become active"); lastState = 6; return false; }

    g_activeLanguage = language;
    if (lastState != 3) vikey::tsf::diagnostics::write(L"TSF profile activation succeeded and is active");
    lastState = 3;
    return true;
}

void shutdown() {
    vikey::tsf::diagnostics::write(L"TSF shutdown requested");
    deactivateActiveProfile();
    if (g_threadMgr) {
        if (g_clientId != TF_CLIENTID_NULL) g_threadMgr->Deactivate();
        g_threadMgr->Release();
        g_threadMgr = nullptr;
        g_clientId = TF_CLIENTID_NULL;
    }
    if (g_profiles) {
        g_profiles->Release();
        g_profiles = nullptr;
    }
    g_ready = false;
    g_registerServer = nullptr;
    if (g_tipModule) { FreeLibrary(g_tipModule); g_tipModule = nullptr; }
    if (g_comInitialized) { CoUninitialize(); g_comInitialized = false; }
}
}}
