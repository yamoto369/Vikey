#include <windows.h>
#include <msctf.h>
#include <cstdio>

using GetClassObjectFn = HRESULT (STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
using CanUnloadFn = HRESULT (STDAPICALLTYPE*)();

static int failures = 0;
static void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fwprintf(stderr, L"Usage: tsf_com_test.exe <VikeyTsf.dll>\n");
        return 2;
    }
    HMODULE module = LoadLibraryW(argv[1]);
    check("LoadLibrary", module != nullptr);
    if (!module) return 1;

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
                check("Deactivate is idempotent", SUCCEEDED(tip->Deactivate()) && SUCCEEDED(tip->Deactivate()));
                tip->Release();
            }
            factory->Release();
        }
        check("objects released; DLL can unload", canUnload() == S_OK);
    }

    FreeLibrary(module);
    check("FreeLibrary", true);
    std::printf("TSF COM smoke test: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
