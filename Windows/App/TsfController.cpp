#include "TsfController.h"
#include <windows.h>
#include <msctf.h>
#include <objbase.h>
#include <string>
#include <cstdio>

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

LANGID currentThreadLanguage() {
    HKL layout = GetKeyboardLayout(GetCurrentThreadId());
    return LOWORD(reinterpret_cast<ULONG_PTR>(layout));
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
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    error = RegSetValueExW(key, nullptr, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(dllPath.c_str()),
        static_cast<DWORD>((dllPath.size() + 1) * sizeof(wchar_t)));
    if (error == ERROR_SUCCESS) {
        const wchar_t threading[] = L"Apartment";
        error = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(threading), sizeof(threading));
    }
    RegCloseKey(key);
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

bool ensureLanguageProfile(ITfInputProcessorProfiles* profiles, LANGID language) {
    BOOL enabled = FALSE;
    HRESULT hr = profiles->IsEnabledLanguageProfile(kVikeyClsid, language, kVikeyProfile, &enabled);
    if (FAILED(hr)) {
        BSTR description = SysAllocString(L"Vikey Vietnamese Input");
        if (!description) return false;
        hr = profiles->AddLanguageProfile(kVikeyClsid, language, kVikeyProfile,
            description, static_cast<ULONG>(SysStringLen(description)), nullptr, 0, 0);
        SysFreeString(description);
        if (FAILED(hr)) {
            // Another Vikey instance or a previous interrupted startup may
            // already have created the profile; re-query before failing.
            enabled = FALSE;
            hr = profiles->IsEnabledLanguageProfile(kVikeyClsid, language, kVikeyProfile, &enabled);
            if (FAILED(hr)) return false;
        }
    }
    if (!enabled) {
        hr = profiles->EnableLanguageProfile(kVikeyClsid, language, kVikeyProfile, TRUE);
        if (FAILED(hr)) return false;
    }
    return true;
}

bool ensureKeyboardCategory() {
    ITfCategoryMgr* categories = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&categories));
    if (FAILED(hr) || !categories) return false;
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
    return SUCCEEDED(hr);
}

bool ensureRegistered(const std::wstring& dllPath) {
    HRESULT hr = updateComClassPath(dllPath);
    if (FAILED(hr)) return false;

    ITfInputProcessorProfiles* profiles = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&profiles));
    if (FAILED(hr) || !profiles) return false;

    if (!isProcessorRegistered(profiles)) {
        if (!g_registerServer || FAILED(g_registerServer())) {
            profiles->Release();
            return false;
        }
        profiles->Release();
        profiles = nullptr;
        hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&profiles));
        if (FAILED(hr) || !profiles) return false;
    }

    // Keep the legacy Vietnamese profile registered, and add a profile for
    // the user's current keyboard language. This lets TSF operate without
    // changing the active Windows keyboard layout.
    if (!ensureLanguageProfile(profiles, 0x042A)) {
        profiles->Release();
        return false;
    }
    const LANGID current = currentThreadLanguage();
    const bool profileReady = current && ensureLanguageProfile(profiles, current);
    profiles->Release();
    return profileReady && ensureKeyboardCategory();
}

bool isActiveProfile(LANGID language) {
    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (FAILED(hr) || !profiles) return false;
    LANGID activeLanguage = 0;
    GUID activeProfile = GUID_NULL;
    hr = profiles->GetActiveLanguageProfile(kVikeyClsid, &activeLanguage, &activeProfile);
    profiles->Release();
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
    if (g_ready) return true;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return false;
    g_comInitialized = true;

    const std::wstring path = modulePath();
    if (path.empty()) { shutdown(); return false; }
    g_tipModule = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_tipModule) { shutdown(); return false; }
    g_registerServer = reinterpret_cast<RegisterServerFn>(GetProcAddress(g_tipModule, "DllRegisterServer"));
    if (!g_registerServer || !ensureRegistered(path)) { shutdown(); return false; }

    // Profile activation requires a TSF thread manager on the calling thread.
    hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&g_threadMgr));
    if (FAILED(hr) || !g_threadMgr) { shutdown(); return false; }
    hr = g_threadMgr->Activate(&g_clientId);
    if (FAILED(hr) || g_clientId == TF_CLIENTID_NULL) { shutdown(); return false; }

    g_ready = true;
    return true;
}

bool sync(bool vietnameseEnabled) {
    if (!g_ready) return false;
    if (!vietnameseEnabled) {
        deactivateActiveProfile();
        return false;
    }

    const LANGID language = currentThreadLanguage();
    const LANGID foreground = foregroundLanguage();
    // ActivateProfile requires the caller's current input language to match.
    // If an application has a different per-window keyboard layout, retain
    // the legacy hook for that window rather than changing its layout.
    if (!language || language != foreground || !supportedLanguage(language)) {
        std::fprintf(stderr, "[TsfController] language mismatch/unsupported: current=%04X foreground=%04X\\n", language, foreground);
        deactivateActiveProfile();
        return false;
    }
    if (g_activeLanguage == language && isActiveProfile(language)) return true;
    // Another input processor may have replaced Vikey since the previous
    // timer tick. Do not claim the hook is safely bypassed unless TSF confirms
    // that Vikey is still the active keyboard profile.
    if (g_activeLanguage == language) g_activeLanguage = 0;
    deactivateActiveProfile();
    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (FAILED(hr) || !profiles) return false;
    const bool ready = ensureLanguageProfile(profiles, language);
    if (!ready) {
        profiles->Release();
        std::fprintf(stderr, "[TsfController] language profile setup failed\\n");
        return false;
    }

    hr = profiles->ActivateLanguageProfile(kVikeyClsid, language, kVikeyProfile);
    profiles->Release();
    std::fprintf(stderr, "[TsfController] ActivateLanguageProfile: 0x%08lX\\n", static_cast<unsigned long>(hr));
    if (FAILED(hr) || !isActiveProfile(language)) return false;

    g_activeLanguage = language;
    return true;
}

void shutdown() {
    deactivateActiveProfile();
    if (g_threadMgr) {
        if (g_clientId != TF_CLIENTID_NULL) g_threadMgr->Deactivate();
        g_threadMgr->Release();
        g_threadMgr = nullptr;
        g_clientId = TF_CLIENTID_NULL;
    }
    g_ready = false;
    g_registerServer = nullptr;
    if (g_tipModule) { FreeLibrary(g_tipModule); g_tipModule = nullptr; }
    if (g_comInitialized) { CoUninitialize(); g_comInitialized = false; }
}
}}
