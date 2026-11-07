#include <windows.h>
#include <msctf.h>
#include <cstdio>
#include <objbase.h>

using GetClassObjectFn = HRESULT (STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
using CanUnloadFn = HRESULT (STDAPICALLTYPE*)();

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
    check("export DllGetClassObject", getClassObject != nullptr);
    check("export DllCanUnloadNow", canUnload != nullptr);
    check("export DllRegisterServer", GetProcAddress(module, "DllRegisterServer") != nullptr);
    check("export DllUnregisterServer", GetProcAddress(module, "DllUnregisterServer") != nullptr);

    if (getClassObject && canUnload) {
        const CLSID clsid = { 0x9b7a4d31, 0x6d8f, 0x4d22, { 0xa1, 0x73, 0x5e, 0x91, 0x2c, 0x44, 0x8b, 0x60 } };
        IClassFactory* factory = nullptr;
        HRESULT hr = getClassObject(clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory));
        check("DllGetClassObject returns class factory", SUCCEEDED(hr) && factory != nullptr);
        check("factory prevents unload", canUnload() == S_FALSE);

        if (factory) {
            IClassFactory* unsupported = nullptr;
            hr = factory->QueryInterface(IID_IClassFactory, reinterpret_cast<void**>(&unsupported));
            check("class factory QueryInterface", SUCCEEDED(hr) && unsupported != nullptr);
            if (unsupported) unsupported->Release();

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
                    check("null callback output rejected", sink->OnTestKeyDown(nullptr, 0, 0, nullptr) == E_POINTER);
                    sink->Release();
                }
                check("Activate rejects null thread manager", tip->Activate(nullptr, TF_CLIENTID_NULL) == E_INVALIDARG);

                // Exercise the actual TSF manager supplied to TIPs by Windows.
                ITfThreadMgr* threadMgr = nullptr;
                hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                    IID_ITfThreadMgr, reinterpret_cast<void**>(&threadMgr));
                check("create TSF thread manager", SUCCEEDED(hr) && threadMgr != nullptr);
                if (threadMgr) {
                    TfClientId clientId = TF_CLIENTID_NULL;
                    hr = threadMgr->Activate(&clientId);
                    const bool threadMgrActivated = SUCCEEDED(hr) && clientId != TF_CLIENTID_NULL;
                    check("activate TSF thread manager", threadMgrActivated);
                    if (threadMgrActivated) {
                        ITfKeystrokeMgr* queriedKeyMgr = nullptr;
                        HRESULT queryHr = threadMgr->QueryInterface(IID_PPV_ARGS(&queriedKeyMgr));
                        check("thread manager exposes ITfKeystrokeMgr",
                            SUCCEEDED(queryHr) && queriedKeyMgr != nullptr);

                        // ITfThreadMgr::Activate returns the application's client ID.
                        // A text service receives a distinct ID associated with its CLSID.
                        ITfClientId* clientIdManager = nullptr;
                        HRESULT clientIdHr = threadMgr->QueryInterface(IID_PPV_ARGS(&clientIdManager));
                        TfClientId serviceClientId = TF_CLIENTID_NULL;
                        if (SUCCEEDED(clientIdHr) && clientIdManager) {
                            clientIdHr = clientIdManager->GetClientId(clsid, &serviceClientId);
                        }
                        std::printf("Application clientId = 0x%08lX; text-service clientId = 0x%08lX\\n",
                            static_cast<unsigned long>(clientId),
                            static_cast<unsigned long>(serviceClientId));
                        check("obtain distinct text-service client ID",
                            SUCCEEDED(clientIdHr) && serviceClientId != TF_CLIENTID_NULL &&
                            serviceClientId != clientId);

                        ITfKeyEventSink* directSink = nullptr;
                        HRESULT sinkHr = tip->QueryInterface(IID_PPV_ARGS(&directSink));
                        check("query direct diagnostic key sink", SUCCEEDED(sinkHr) && directSink != nullptr);
                        if (SUCCEEDED(queryHr) && queriedKeyMgr && SUCCEEDED(clientIdHr) &&
                            serviceClientId != TF_CLIENTID_NULL && directSink) {
                            HRESULT directAdviseHr = queriedKeyMgr->AdviseKeyEventSink(serviceClientId, directSink, TRUE);
                            std::printf("Direct AdviseKeyEventSink HRESULT = 0x%08lX\\n",
                                static_cast<unsigned long>(directAdviseHr));
                            check("direct TSF key sink advise with text-service ID", SUCCEEDED(directAdviseHr));
                            if (SUCCEEDED(directAdviseHr)) {
                                check("direct TSF key sink unadvise",
                                    SUCCEEDED(queriedKeyMgr->UnadviseKeyEventSink(serviceClientId)));
                            }
                        }
                        if (directSink) directSink->Release();
                        if (queriedKeyMgr) queriedKeyMgr->Release();
                        if (clientIdManager) clientIdManager->Release();

                        hr = tip->Activate(threadMgr, serviceClientId);
                        std::printf("TIP Activate HRESULT = 0x%08lX\\n",
                            static_cast<unsigned long>(hr));
                        const bool tipActivated = SUCCEEDED(hr);
                        check("TIP activation advises real TSF key sink", tipActivated);
                        if (tipActivated) {
                            check("duplicate TIP activation rejected",
                                tip->Activate(threadMgr, clientId) == TF_E_ALREADY_EXISTS);
                            check("TIP deactivation unadvises real TSF key sink", SUCCEEDED(tip->Deactivate()));
                            check("TIP deactivation is idempotent", SUCCEEDED(tip->Deactivate()));
                        }
                        check("deactivate TSF thread manager", SUCCEEDED(threadMgr->Deactivate()));
                    }
                    threadMgr->Release();
                }
                tip->Release();
            }
            factory->Release();
        }
        check("objects released; DLL can unload", canUnload() == S_OK);
    }

    const BOOL unloaded = FreeLibrary(module);
    check("FreeLibrary", unloaded != FALSE);
    CoUninitialize();
    std::printf("TSF COM smoke test: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
