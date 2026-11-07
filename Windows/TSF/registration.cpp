#include "tsf_guids.h"
#include <windows.h>
#include <msctf.h>
#include <oleauto.h>
#include <cstdio>
#include <string>
#include "Prefs.h"

namespace {

void logRegistration(const char* eventName, HRESULT hr) {
    wchar_t directory[MAX_PATH] = {};
    DWORD length = GetEnvironmentVariableW(L"VIKEY_TSF_LOG_DIR", directory, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        const std::wstring root = vikey::prefs::appDataDir() + L"\\Logs";
        wcsncpy_s(directory, root.c_str(), _TRUNCATE);
    }
    CreateDirectoryW(directory, nullptr);
    const std::wstring path = std::wstring(directory) + L"\\tsf.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[256] = {};
    SYSTEMTIME now; GetLocalTime(&now);
    const int count = snprintf(line, sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u event=%s hr=0x%08lX\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        now.wMilliseconds, eventName, (unsigned long)hr);
    DWORD written = 0;
    WriteFile(file, line, (DWORD)(count > 0 ? count : 0), &written, nullptr);
    CloseHandle(file);
}

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
    ITfInputProcessorProfiles* legacyProfiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&legacyProfiles));
    if (FAILED(hr)) return hr;

    ITfInputProcessorProfileMgr* profileManager = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profileManager));
    if (FAILED(hr)) {
        legacyProfiles->Release();
        return hr;
    }

    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&registerProfiles), &module);
    wchar_t dllPath[MAX_PATH] = {};
    const DWORD pathLength = GetModuleFileNameW(module, dllPath, ARRAYSIZE(dllPath));
    if (!pathLength || pathLength >= ARRAYSIZE(dllPath)) {
        profileManager->Release();
        legacyProfiles->Release();
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const LANGID languages[] = {
        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
        MAKELANGID(LANG_VIETNAMESE, SUBLANG_DEFAULT)
    };
    for (LANGID language : languages) {
        BSTR description = nullptr;
        const HRESULT exists = legacyProfiles->GetLanguageProfileDescription(
            CLSID_VikeyTextService, language, GUID_VikeyProfile, &description);
        if (description) {
            SysFreeString(description);
            description = nullptr;
        }

        if (exists == S_OK) {
            hr = legacyProfiles->EnableLanguageProfile(
                CLSID_VikeyTextService, language, GUID_VikeyProfile, TRUE);
        } else {
            hr = profileManager->RegisterProfile(
                CLSID_VikeyTextService, language, GUID_VikeyProfile,
                L"Vikey", 5, dllPath, pathLength, 0,
                nullptr, 0, TRUE, 0);
        }
        if (FAILED(hr)) break;
    }

    if (SUCCEEDED(hr)) {
        ITfCategoryMgr* categories = nullptr;
        hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories));
        if (SUCCEEDED(hr) && categories) {
            HRESULT categoryResult = categories->RegisterCategory(
                CLSID_VikeyTextService, GUID_TFCAT_TIP_KEYBOARD, CLSID_VikeyTextService);
            if (FAILED(categoryResult) && categoryResult != TF_E_ALREADY_EXISTS)
                hr = categoryResult;
            categories->Release();
        }
    }

    if (SUCCEEDED(hr)) {
        wchar_t hostTest[2] = {};
        const DWORD hostTestLength = GetEnvironmentVariableW(
            L"VIKEY_TSF_HOST_TEST", hostTest, ARRAYSIZE(hostTest));
        if (hostTestLength == 0) {
            // Production activation is session-wide; CI test hosts explicitly activate
            // a COM instance on their own ITfThreadMgr to avoid desktop-profile timing.
            const LANGID currentLanguage = LOWORD((DWORD_PTR)GetKeyboardLayout(0));
            const LANGID targetLanguage =
                PRIMARYLANGID(currentLanguage) == LANG_VIETNAMESE
                    ? MAKELANGID(LANG_VIETNAMESE, SUBLANG_DEFAULT)
                    : MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
            const DWORD flags =
                TF_IPPMF_FORSESSION | TF_IPPMF_ENABLEPROFILE |
                TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE;
            hr = profileManager->ActivateProfile(
                TF_PROFILETYPE_INPUTPROCESSOR, targetLanguage, CLSID_VikeyTextService,
                GUID_VikeyProfile, nullptr, flags);
            logRegistration(SUCCEEDED(hr) ? "activate-profile" : "activate-profile-failed", hr);
        } else {
            logRegistration("test-host-registration-only", S_OK);
        }
    }

    profileManager->Release();
    legacyProfiles->Release();
    return hr;
}
} // namespace

extern "C" __declspec(dllexport) HRESULT __stdcall DllRegisterServer() {
    HRESULT hr = registerComClass();
    if (FAILED(hr)) { logRegistration("register-com-class-failed", hr); return hr; }
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = init == S_OK || init == S_FALSE;
    if (init == RPC_E_CHANGED_MODE) hr = registerProfiles();
    else if (SUCCEEDED(init)) hr = registerProfiles();
    else hr = init;
    if (uninitialize) CoUninitialize();
    logRegistration(SUCCEEDED(hr) ? "register-profiles" : "register-profiles-failed", hr);
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
                GUID_TFCAT_TIP_KEYBOARD, CLSID_VikeyTextService);
            categories->Release();
        }
        RegDeleteTreeW(HKEY_CURRENT_USER,
            (L"Software\\Classes\\CLSID\\" + guidString(CLSID_VikeyTextService)).c_str());
    }
    if (uninitialize) CoUninitialize();
    return hr;
}
