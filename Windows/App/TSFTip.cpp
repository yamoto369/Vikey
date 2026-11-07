#include "TSFTip.h"
#include <atomic>
#include <cwchar>
#include <new>

const CLSID CLSID_VikeyTextService =
{ 0x9b7a4d31, 0x6d8f, 0x4d22, { 0xa1, 0x73, 0x5e, 0x91, 0x2c, 0x44, 0x8b, 0x60 } };

namespace {
HMODULE g_module = nullptr;
std::atomic<long> g_objects{0};
std::atomic<long> g_locks{0};

class VikeyTextService final : public ITfTextInputProcessor, public ITfKeyEventSink {
public:
    VikeyTextService() { ++g_objects; }
    ~VikeyTextService() {
        // The TSF manager normally retains the advised sink until Unadvise.
        // Keep cleanup best-effort here; Deactivate reports failures to TSF.
        unadvise();
        --g_objects;
    }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_ITfTextInputProcessor))
            *object = static_cast<ITfTextInputProcessor*>(this);
        else if (IsEqualIID(iid, IID_ITfKeyEventSink))
            *object = static_cast<ITfKeyEventSink*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(++refs_); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG refs = static_cast<ULONG>(--refs_);
        if (!refs) delete this;
        return refs;
    }

    STDMETHODIMP Activate(ITfThreadMgr* threadMgr, TfClientId clientId) override {
        if (!threadMgr || clientId == TF_CLIENTID_NULL) return E_INVALIDARG;
        if (threadMgr_) return TF_E_ALREADY_EXISTS;
        ITfKeystrokeMgr* keyMgr = nullptr;
        HRESULT hr = threadMgr->QueryInterface(IID_PPV_ARGS(&keyMgr));
        if (FAILED(hr)) return hr;
        threadMgr_ = threadMgr;
        threadMgr_->AddRef();
        keyMgr_ = keyMgr;
        clientId_ = clientId;
        hr = keyMgr_->AdviseKeyEventSink(clientId_, this, TRUE);
        if (FAILED(hr)) {
            // Advise failed, so no sink registration exists; release the
            // interfaces acquired during activation.
            releaseManagers();
            return hr;
        }
        advised_ = true;
        return S_OK;
    }

    STDMETHODIMP Deactivate() override {
        return unadvise();
    }

    STDMETHODIMP OnSetFocus(BOOL) override { return S_OK; }
    STDMETHODIMP OnTestKeyDown(ITfContext*, WPARAM, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        return S_OK;
    }
    STDMETHODIMP OnKeyDown(ITfContext*, WPARAM, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        return S_OK;
    }
    STDMETHODIMP OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        return S_OK;
    }
    STDMETHODIMP OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        return S_OK;
    }
    STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        return S_OK;
    }

private:
    HRESULT unadvise() {
        if (advised_ && keyMgr_) {
            HRESULT hr = keyMgr_->UnadviseKeyEventSink(clientId_);
            if (FAILED(hr)) {
                // Do not release the sink's manager reference after a failed
                // unadvise: TSF may still call back into this object.
                return hr;
            }
            advised_ = false;
        }
        releaseManagers();
        return S_OK;
    }

    void releaseManagers() {
        if (keyMgr_) { keyMgr_->Release(); keyMgr_ = nullptr; }
        if (threadMgr_) { threadMgr_->Release(); threadMgr_ = nullptr; }
        clientId_ = TF_CLIENTID_NULL;
    }
    std::atomic<ULONG> refs_{1};
    ITfThreadMgr* threadMgr_ = nullptr;
    ITfKeystrokeMgr* keyMgr_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    bool advised_ = false;
};

class VikeyClassFactory final : public IClassFactory {
public:
    VikeyClassFactory() { ++g_objects; }
    ~VikeyClassFactory() { --g_objects; }
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IClassFactory))
            *object = static_cast<IClassFactory*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(++refs_); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG refs = static_cast<ULONG>(--refs_);
        if (!refs) delete this;
        return refs;
    }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** object) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* service = new (std::nothrow) VikeyTextService();
        if (!service) return E_OUTOFMEMORY;
        HRESULT hr = service->QueryInterface(iid, object);
        service->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) ++g_locks; else --g_locks;
        return S_OK;
    }
private:
    std::atomic<ULONG> refs_{1};
};
}


const GUID GUID_VikeyProfile =
{ 0x4e7c1a26, 0x8d39, 0x4f51, { 0xb2, 0x07, 0x61, 0x33, 0x9a, 0xc8, 0x42, 0x15 } };

static HRESULT registerComClass() {
    wchar_t module[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_module, module, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());
    wchar_t clsid[64] = {};
    if (StringFromGUID2(CLSID_VikeyTextService, clsid, 64) == 0) return E_FAIL;
    wchar_t keyPath[128] = {};
    if (swprintf_s(keyPath, _countof(keyPath), L"CLSID\\%s\\InprocServer32", clsid) < 0) return E_FAIL;
    HKEY key = nullptr;
    LONG error = RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    error = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(module),
        static_cast<DWORD>((wcslen(module) + 1) * sizeof(wchar_t)));
    if (error == ERROR_SUCCESS) {
        const wchar_t model[] = L"Apartment";
        error = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(model), sizeof(model));
    }
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(error);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllRegisterServer() {
    HRESULT hr = registerComClass();
    if (FAILED(hr)) return hr;
    ITfInputProcessorProfiles* profiles = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&profiles));
    if (SUCCEEDED(hr)) {
        hr = profiles->Register(CLSID_VikeyTextService);
        if (SUCCEEDED(hr)) {
            BSTR description = SysAllocString(L"Vikey Vietnamese Input");
            if (!description) hr = E_OUTOFMEMORY;
            else {
                hr = profiles->AddLanguageProfile(CLSID_VikeyTextService, 0x042A,
                    GUID_VikeyProfile, description, static_cast<ULONG>(wcslen(description)),
                    nullptr, 0, 0);
                SysFreeString(description);
            }
        }
        profiles->Release();
    }
    if (SUCCEEDED(hr)) {
        ITfCategoryMgr* categories = nullptr;
        hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITfCategoryMgr, reinterpret_cast<void**>(&categories));
        if (SUCCEEDED(hr)) {
            hr = categories->RegisterCategory(CLSID_VikeyTextService,
                GUID_TFCAT_TIP_KEYBOARD, GUID_VikeyProfile);
            categories->Release();
        }
    }
    if (FAILED(hr)) {
        wchar_t clsid[64] = {};
        StringFromGUID2(CLSID_VikeyTextService, clsid, 64);
        wchar_t keyPath[128] = {};
        swprintf_s(keyPath, _countof(keyPath), L"CLSID\\%s", clsid);
        RegDeleteTreeW(HKEY_CLASSES_ROOT, keyPath);
    }
    return hr;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllUnregisterServer() {
    HRESULT result = S_OK;
    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&profiles));
    if (SUCCEEDED(hr)) {
        hr = profiles->Unregister(CLSID_VikeyTextService);
        if (FAILED(hr)) result = hr;
        profiles->Release();
    } else result = hr;
    ITfCategoryMgr* categories = nullptr;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr, reinterpret_cast<void**>(&categories));
    if (SUCCEEDED(hr)) {
        hr = categories->UnregisterCategory(CLSID_VikeyTextService,
            GUID_TFCAT_TIP_KEYBOARD, GUID_VikeyProfile);
        if (FAILED(hr) && SUCCEEDED(result)) result = hr;
        categories->Release();
    } else if (SUCCEEDED(result)) result = hr;
    wchar_t clsid[64] = {};
    StringFromGUID2(CLSID_VikeyTextService, clsid, 64);
    wchar_t keyPath[128] = {};
    swprintf_s(keyPath, _countof(keyPath), L"CLSID\\%s", clsid);
    LONG error = RegDeleteTreeW(HKEY_CLASSES_ROOT, keyPath);
    if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND && SUCCEEDED(result))
        result = HRESULT_FROM_WIN32(error);
    return result;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_module = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** object) {
    if (!IsEqualCLSID(clsid, CLSID_VikeyTextService)) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) VikeyClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, object);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return g_objects.load() == 0 && g_locks.load() == 0 ? S_OK : S_FALSE;
}
