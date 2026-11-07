#include "../App/TsfController.h"
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <string>

using UnregisterServerFn = HRESULT (WINAPI*)();

static bool cleanupRegistration() {
    wchar_t path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    std::wstring dllPath(path, n);
    const size_t slash = dllPath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    dllPath.resize(slash + 1);
#if defined(_M_ARM64)
    dllPath += L"VikeyTsf-arm64.dll";
#else
    dllPath += L"VikeyTsf-x64.dll";
#endif
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return false;
    HMODULE module = LoadLibraryW(dllPath.c_str());
    bool ok = module != nullptr;
    if (module) {
        auto unregisterServer = reinterpret_cast<UnregisterServerFn>(
            GetProcAddress(module, "DllUnregisterServer"));
        ok = unregisterServer && SUCCEEDED(unregisterServer());
        FreeLibrary(module);
    }
    CoUninitialize();
    return ok;
}

int wmain() {
    if (!vikey::tsf::initialize()) {
        std::puts("TSF controller initialize: FAIL");
        return 1;
    }
    const bool activated = vikey::tsf::sync(true);
    const bool reactivated = activated && !vikey::tsf::sync(false) && vikey::tsf::sync(true);
    vikey::tsf::shutdown();
    const bool cleaned = cleanupRegistration();

    std::printf("TSF controller per-user registration: %s\n", cleaned ? "PASS" : "FAIL");
    std::printf("TSF controller session activation/toggle: %s\n",
        reactivated ? "PASS" : "FAIL");
    return activated && reactivated && cleaned ? 0 : 1;
}
