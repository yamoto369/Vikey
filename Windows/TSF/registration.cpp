#include "tsf_guids.h"
#include <windows.h>
#include <msctf.h>
#include <oleauto.h>
#include <string>

namespace {
std::wstring guidString(REFGUID guid) {
    wchar_t value[64] = {};
    StringFromGUID2(guid, value, 64);
    return value;
}
HRESULT registerComClass() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&registerComClass), &module))
        return HRESULT_FROM_WIN32(GetLastError());
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return HRESULT_FROM_WIN32(GetLastError());
    const std::wstring keyPath = L"Software\\Classes\\CLSID\\" + guidString(CLSID_VikeyTextService);
    HKEY clsidKey = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &clsidKey, nullptr);
    if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
    HKEY serverKey = nullptr;
    result = RegCreateKeyExW(clsidKey, L"InprocServer32", 0, nullptr, 0,
        KEY_WRITE, nullptr, &serverKey, nullptr);
    if (result == ERROR_SUCCESS) {
        result = RegSetValueExW(serverKey, nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(path), (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
        if (result == ERROR_SUCCESS) {
            const wchar_t* model = L"Apartment";
            result = RegSetValueExW(serverKey, L"ThreadingModel", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(model), (DWORD)((wcslen(model) + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(serverKey);
    }
    RegCloseKey(clsidKey);
    return result == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(result);
}
HRESULT addProfile(ITfInputProcessorProfiles* profiles, LANGID language) {
    BSTR description = nullptr;
    const HRESULT found = profiles->GetLanguageProfileDescription(
        CLSID_VikeyTextService, language, GUID_VikeyProfile, &description);
    if (description) SysFreeString(description);
    if (SUCCEEDED(found)) return S_OK;
    return profiles->AddLanguageProfile(CLSID_VikeyTextService, language, GUID_VikeyProfile,
        L"Vikey", 5, nullptr, 0, 0);
}
HRESULT registerProfiles() {
    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (FAILED(hr)) return hr;
    BSTR existing = nullptr;
    HRESULT profileExists = profiles->GetLanguageProfileDescription(
        CLSID_VikeyTextService, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
        GUID_VikeyProfile, &existing);
    if (existing) { SysFreeString(existing); existing = nullptr; }
    if (FAILED(profileExists)) {
        profileExists = profiles->GetLanguageProfileDescription(
            CLSID_VikeyTextService, MAKELANGID(LANG_VIETNAMESE, SUBLANG_DEFAULT),
            GUID_VikeyProfile, &existing);
        if (existing) SysFreeString(existing);
    }
    if (FAILED(profileExists)) hr = profiles->Register(CLSID_VikeyTextService);
    const LANGID languages[] = {
        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
        MAKELANGID(LANG_VIETNAMESE, SUBLANG_DEFAULT)
    };
    for (LANGID language : languages) if (SUCCEEDED(hr)) hr = addProfile(profiles, language);
    if (SUCCEEDED(hr)) {
        ITfCategoryMgr* categories = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories)))) {
            categories->RegisterCategory(CLSID_VikeyTextService, GUID_TFCAT_TIP_KEYBOARD, GUID_VikeyCategory);
            categories->Release();
        }
        const LANGID userLanguage = GetUserDefaultLangID();
        const LANGID activeLanguage = PRIMARYLANGID(userLanguage) == LANG_VIETNAMESE
            ? MAKELANGID(LANG_VIETNAMESE, SUBLANG_DEFAULT)
            : MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
        hr = profiles->ActivateLanguageProfile(CLSID_VikeyTextService, activeLanguage, GUID_VikeyProfile);
    }
    profiles->Release();
    return hr;
}
}
extern "C" __declspec(dllexport) HRESULT __stdcall DllRegisterServer() {
    HRESULT hr = registerComClass();
    if (FAILED(hr)) return hr;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = init == S_OK || init == S_FALSE;
    if (init == RPC_E_CHANGED_MODE) hr = registerProfiles();
    else if (SUCCEEDED(init)) hr = registerProfiles();
    else hr = init;
    if (uninitialize) CoUninitialize();
    return hr;
}
extern "C" __declspec(dllexport) HRESULT __stdcall DllUnregisterServer() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = init == S_OK || init == S_FALSE;
    HRESULT hr = SUCCEEDED(init) || init == RPC_E_CHANGED_MODE ? S_OK : init;
    if (SUCCEEDED(hr)) {
        ITfInputProcessorProfiles* profiles = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles)))) {
            profiles->Unregister(CLSID_VikeyTextService);
            profiles->Release();
        }
        ITfCategoryMgr* categories = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories)))) {
            categories->UnregisterCategory(CLSID_VikeyTextService,
                GUID_TFCAT_TIP_KEYBOARD, GUID_VikeyCategory);
            categories->Release();
        }
        RegDeleteTreeW(HKEY_CURRENT_USER,
            (L"Software\\Classes\\CLSID\\" + guidString(CLSID_VikeyTextService)).c_str());
    }
    if (uninitialize) CoUninitialize();
    return hr;
}
