#include <windows.h>
#include <msctf.h>
#include <cstdio>
#include <objbase.h>

using GetClassObjectFn = HRESULT (STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
using CanUnloadFn = HRESULT (STDAPICALLTYPE*)();
using DllRegistrationFn = HRESULT (STDAPICALLTYPE*)();

static int failures = 0;
static void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

int wmain(int argc, wchar_t** argv) {
    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    check("COM apartment initialized", SUCCEEDED(comHr));
    if (FAILED(comHr)) return 1;

    if (argc != 2) {
        CoUninitialize();
        std::fprintf(stderr, "Usage: tsf_com_test.exe <VikeyTsf.dll>\n");
        return 2;
    }

    HMODULE module = LoadLibraryW(argv[1]);
    check("LoadLibrary", module != nullptr);
    if (!module) { CoUninitialize(); return 1; }

    auto getClassObject = reinterpret_cast<GetClassObjectFn>(
        GetProcAddress(module, "DllGetClassObject"));
    auto canUnload = reinterpret_cast<CanUnloadFn>(
        GetProcAddress(module, "DllCanUnloadNow"));
    auto registerServer = reinterpret_cast<DllRegistrationFn>(
        GetProcAddress(module, "DllRegisterServer"));
    auto unregisterServer = reinterpret_cast<DllRegistrationFn>(
        GetProcAddress(module, "DllUnregisterServer"));

    check("export DllGetClassObject", getClassObject != nullptr);
    check("export DllCanUnloadNow", canUnload != nullptr);
    check("export DllRegisterServer", registerServer != nullptr);
    check("export DllUnregisterServer", unregisterServer != nullptr);

    const CLSID clsid = { 0x9b7a4d31, 0x6d8f, 0x4d22, { 0xa1, 0x73, 0x5e, 0x91, 0x2c, 0x44, 0x8b, 0x60 } };
    const GUID profileGuid = { 0x4e7c1a26, 0x8d39, 0x4f51, { 0xb2, 0x07, 0x61, 0x33, 0x9a, 0xc8, 0x42, 0x15 } };
    constexpr LANGID kVietnameseLangId = 0x042A;

    if (getClassObject && canUnload && registerServer && unregisterServer) {
        IClassFactory* factory = nullptr;
        HRESULT hr = getClassObject(clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory));
        check("DllGetClassObject returns class factory", SUCCEEDED(hr) && factory != nullptr);
        check("factory prevents unload", canUnload() == S_FALSE);

        if (factory) {
            IClassFactory* factoryCopy = nullptr;
            hr = factory->QueryInterface(IID_IClassFactory, reinterpret_cast<void**>(&factoryCopy));
            check("class factory QueryInterface", SUCCEEDED(hr) && factoryCopy != nullptr);
            if (factoryCopy) factoryCopy->Release();

            IUnknown* aggregated = nullptr;
            hr = factory->CreateInstance(reinterpret_cast<IUnknown*>(factory), IID_IUnknown,
                reinterpret_cast<void**>(&aggregated));
            check("aggregation rejected", hr == CLASS_E_NOAGGREGATION && aggregated == nullptr);

            ITfTextInputProcessor* tip = nullptr;
            hr = factory->CreateInstance(nullptr, IID_ITfTextInputProcessor, reinterpret_cast<void**>(&tip));
            check("create ITfTextInputProcessor", SUCCEEDED(hr) && tip != nullptr);
            if (tip) {
                ITfKeyEventSink* sink = nullptr;
                hr = tip->QueryInterface(IID_ITfKeyEventSink, reinterpret_cast<void**>(&sink));
                check("TIP exposes ITfKeyEventSink", SUCCEEDED(hr) && sink != nullptr);
                if (sink) {
                    BOOL eaten = TRUE;
                    hr = sink->OnTestKeyDown(nullptr, 0, 0, &eaten);
                    check("key test callback is pass-through", SUCCEEDED(hr) && eaten == FALSE);
                    check("null callback output rejected",
                        sink->OnTestKeyDown(nullptr, 0, 0, nullptr) == E_POINTER);
                    sink->Release();
                }
                check("Activate rejects null thread manager",
                    tip->Activate(nullptr, TF_CLIENTID_NULL) == E_INVALIDARG);
                tip->Release();
            }
            factory->Release();
        }

        // Exercise the real TSF activation path. A TIP's client ID must be
        // supplied by TSF when it activates the registered language profile;
        // GetClientId(CLSID) is not a substitute for that activation callback.
        bool registered = false;
        HRESULT registerHr = registerServer();
        std::printf("DllRegisterServer HRESULT = 0x%08lX\n", static_cast<unsigned long>(registerHr));
        check("register TSF text service", SUCCEEDED(registerHr));
        registered = SUCCEEDED(registerHr);

        ITfInputProcessorProfiles* profiles = nullptr;
        ITfThreadMgr* threadMgr = nullptr;
        bool threadMgrActivated = false;

        if (registered) {
            hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&profiles));
            check("create TSF input processor profiles", SUCCEEDED(hr) && profiles != nullptr);
        }

        if (profiles) {
            LANGID currentLangId = 0;
            hr = profiles->GetCurrentLanguage(&currentLangId);
            check("get current input language", SUCCEEDED(hr) && currentLangId != 0);

            if (SUCCEEDED(hr) && currentLangId != kVietnameseLangId) {
                const wchar_t description[] = L"Vikey TSF lifecycle test";
                hr = profiles->AddLanguageProfile(clsid, currentLangId, profileGuid,
                    description, static_cast<ULONG>(_countof(description) - 1), nullptr, 0, 0);
                std::printf("Add test language profile HRESULT = 0x%08lX\n",
                    static_cast<unsigned long>(hr));
                check("add test profile for current language", SUCCEEDED(hr));
            }

            if (SUCCEEDED(hr)) {
                hr = profiles->EnableLanguageProfile(clsid, currentLangId, profileGuid, TRUE);
                std::printf("EnableLanguageProfile HRESULT = 0x%08lX\n",
                    static_cast<unsigned long>(hr));
                check("enable test language profile", SUCCEEDED(hr));
            }
        }

        if (profiles) {
            hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&threadMgr));
            check("create TSF thread manager for profile activation", SUCCEEDED(hr) && threadMgr != nullptr);
            if (threadMgr) {
                TfClientId applicationClientId = TF_CLIENTID_NULL;
                hr = threadMgr->Activate(&applicationClientId);
                threadMgrActivated = SUCCEEDED(hr) && applicationClientId != TF_CLIENTID_NULL;
                check("activate TSF thread manager", threadMgrActivated);
            }
        }

        if (profiles && threadMgrActivated) {
            hr = profiles->ActivateLanguageProfile(clsid,
                currentLangId, profileGuid);
            std::printf("ActivateLanguageProfile HRESULT = 0x%08lX\n",
                static_cast<unsigned long>(hr));
            check("TSF activates registered Vikey profile", SUCCEEDED(hr));

            LANGID activeLangId = 0;
            GUID activeProfile = GUID_NULL;
            HRESULT activeHr = profiles->GetActiveLanguageProfile(clsid, &activeLangId, &activeProfile);
            check("Vikey profile reported active",
                SUCCEEDED(activeHr) && activeLangId == currentLangId &&
                IsEqualGUID(activeProfile, profileGuid));
        }

        if (threadMgr) {
            if (threadMgrActivated)
                check("deactivate TSF thread manager", SUCCEEDED(threadMgr->Deactivate()));
            threadMgr->Release();
        }
        if (profiles) profiles->Release();

        if (registered) {
            HRESULT unregisterHr = unregisterServer();
            std::printf("DllUnregisterServer HRESULT = 0x%08lX\n",
                static_cast<unsigned long>(unregisterHr));
            check("unregister TSF test registration", SUCCEEDED(unregisterHr));
        }

        check("objects released; DLL can unload", canUnload() == S_OK);
    }

    const BOOL unloaded = FreeLibrary(module);
    check("FreeLibrary", unloaded != FALSE);
    CoUninitialize();
    std::printf("TSF COM smoke test: %s (%d failures)\n",
        failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
