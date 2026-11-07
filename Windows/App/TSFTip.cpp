#include "TSFTip.h"
#include "TsfEngineAdapter.h"
#include "KeyMap.h"
#include "TSFPolicy.h"
#include "TsfEngineRuntime.h"
#include <inputscope.h>
#include <vector>
#include <string>
#include <climits>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <new>

const CLSID CLSID_VikeyTextService =
{ 0x9b7a4d31, 0x6d8f, 0x4d22, { 0xa1, 0x73, 0x5e, 0x91, 0x2c, 0x44, 0x8b, 0x60 } };


namespace {
// GUID_PROP_INPUTSCOPE is not exported by every Windows SDK uuid.lib. Keep the
// documented TSF property GUID local so the TIP has no optional GUID import.
static const GUID kGuidPropInputScope =
    { 0x1713dd5a, 0x68e7, 0x4a5b, { 0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d } };

class VikeyEditSession final : public ITfEditSession {
public:
    VikeyEditSession(ITfContext* context, vikey::tsf::EngineAdapter* adapter,
                     UINT virtualKey, bool shift, bool capsLock, bool modified)
        : context_(context), adapter_(adapter), virtualKey_(virtualKey),
          shift_(shift), capsLock_(capsLock), modified_(modified) {
        if (context_) context_->AddRef();
    }
    ~VikeyEditSession() { if (context_) context_->Release(); }
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_ITfEditSession))
            *object = static_cast<ITfEditSession*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG refs = --refs_;
        if (!refs) delete this;
        return refs;
    }
    bool applied() const { return applied_; }

    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
        if (!context_ || !adapter_) return E_UNEXPECTED;
        TF_STATUS status{};
        HRESULT hr = context_->GetStatus(&status);
        if (FAILED(hr)) return hr;
        if (status.dwDynamicFlags & TF_SD_READONLY) return TS_E_READONLY;

        TF_SELECTION selection{};
        ULONG fetched = 0;
        hr = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
        if (FAILED(hr) || fetched != 1 || !selection.range) {
            if (selection.range) selection.range->Release();
            adapter_->reset();
            return FAILED(hr) ? hr : TF_E_NOSELECTION;
        }

        BOOL selectionEmpty = FALSE;
        hr = selection.range->IsEmpty(cookie, &selectionEmpty);
        if (FAILED(hr) || !selectionEmpty || !isSafeInputContext(cookie, selection.range) ||
            !vikey::tsf::shouldHandleKey(vikey::tsf::TextContextKind::Editable, vLanguage != 0, false)) {
            selection.range->Release();
            adapter_->reset();
            return FAILED(hr) ? hr : S_OK;
        }

        vikey::tsf::EngineEdit edit =
            adapter_->processKey(virtualKey_, shift_, capsLock_, modified_);
        if (!edit.consume) {
            selection.range->Release();
            return S_OK;
        }

        ITfRange* replacement = nullptr;
        hr = selection.range->Clone(&replacement);
        selection.range->Release();
        if (FAILED(hr) || !replacement) {
            adapter_->reset();
            return FAILED(hr) ? hr : E_FAIL;
        }

        if (edit.backspaces > 0) {
            if (edit.backspaces > static_cast<UINT>(LONG_MAX)) {
                replacement->Release();
                adapter_->reset();
                return E_INVALIDARG;
            }
            LONG shifted = 0;
            const LONG requested = -static_cast<LONG>(edit.backspaces);
            hr = replacement->ShiftStart(cookie, requested, &shifted, nullptr);
            if (FAILED(hr) || shifted != requested) {
                replacement->Release();
                adapter_->reset();
                return FAILED(hr) ? hr : TF_E_INVALIDPOS;
            }
        }

        if (edit.backspaces > 0 || !edit.text.empty()) {
            hr = replacement->SetText(cookie, 0, edit.text.c_str(),
                                       static_cast<LONG>(edit.text.size()));
            if (FAILED(hr)) {
                replacement->Release();
                adapter_->reset();
                return hr;
            }
        }

        // Once SetText succeeds the key has been applied. Keep it eaten even
        // if the host refuses a subsequent caret update, preventing duplicates.
        hr = replacement->Collapse(cookie, TF_ANCHOR_END);
        if (SUCCEEDED(hr)) {
            TF_SELECTION caret{};
            caret.range = replacement;
            caret.style.ase = TF_AE_END;
            caret.style.fInterimChar = FALSE;
            context_->SetSelection(cookie, 1, &caret);
        }
        replacement->Release();
        applied_ = true;
        return S_OK;
    }

private:
    static bool isSafeInputContext(TfEditCookie cookie, ITfRange* range) {
        if (!range) return false;
        ITfContext* context = nullptr;
        HRESULT hr = range->GetContext(&context);
        if (FAILED(hr) || !context) return false;
        ITfReadOnlyProperty* property = nullptr;
        hr = context->GetAppProperty(kGuidPropInputScope, &property);
        context->Release();
        if (FAILED(hr) || !property) return false;

        VARIANT value;
        VariantInit(&value);
        hr = property->GetValue(cookie, range, &value);
        property->Release();
        if (FAILED(hr) || value.vt != VT_UNKNOWN || !value.punkVal) {
            VariantClear(&value);
            return false;
        }

        ITfInputScope* inputScope = nullptr;
        hr = value.punkVal->QueryInterface(IID_PPV_ARGS(&inputScope));
        VariantClear(&value);
        if (FAILED(hr) || !inputScope) return false;

        InputScope* scopes = nullptr;
        UINT count = 0;
        hr = inputScope->GetInputScopes(&scopes, &count);
        inputScope->Release();
        if (FAILED(hr) || !scopes || count == 0 || count > 32) {
            if (scopes) CoTaskMemFree(scopes);
            return false;
        }

        bool safe = true;
        for (UINT i = 0; i < count; ++i) {
            if (!vikey::tsf::isAllowedInputScope(scopes[i])) {
                safe = false;
                break;
            }
        }
        CoTaskMemFree(scopes);
        return safe;
    }

    std::atomic<ULONG> refs_{1};
    ITfContext* context_ = nullptr;
    vikey::tsf::EngineAdapter* adapter_ = nullptr;
    UINT virtualKey_ = 0;
    bool shift_ = false, capsLock_ = false, modified_ = false, applied_ = false;
};
}

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
        adapter_.reset();
        return unadvise();
    }

    STDMETHODIMP OnSetFocus(BOOL) override {
        adapter_.reset();
        observedLanguage_ = vLanguage;
        return S_OK;
    }
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        refreshRuntimeSettings();
        if (!context || vLanguage == 0 ||
            vikey::engineKey(static_cast<UINT>(key)) == KEY_EMPTY) return S_OK;
        *eaten = TRUE; // Request OnKeyDown; actual consumption is decided in the edit session.
        return S_OK;
    }
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        refreshTsfEngineSettings();
        if (!context || vLanguage == 0 ||
            vikey::engineKey(static_cast<UINT>(key)) == KEY_EMPTY) return S_OK;

        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        const bool modified =
            (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
            (GetKeyState(VK_MENU) & 0x8000) != 0 ||
            (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
            (GetKeyState(VK_RWIN) & 0x8000) != 0;

        auto* session = new (std::nothrow) VikeyEditSession(
            context, &adapter_, static_cast<UINT>(key), shift, capsLock, modified);
        if (!session) return E_OUTOFMEMORY;
        HRESULT sessionResult = E_FAIL;
        HRESULT hr = context->RequestEditSession(clientId_, session,
            TF_ES_SYNC | TF_ES_READWRITE, &sessionResult);
        if (SUCCEEDED(hr) && SUCCEEDED(sessionResult))
            *eaten = session->applied() ? TRUE : FALSE;
        else
            adapter_.reset();
        session->Release();
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
    void refreshRuntimeSettings() {
        refreshTsfEngineSettings();
        if (observedLanguage_ != vLanguage) {
            adapter_.reset();
            observedLanguage_ = vLanguage;
        }
    }

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
    vikey::tsf::EngineAdapter adapter_;
    int observedLanguage_ = -1;
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

// Prints per-step HRESULTs to stderr when VIKEY_TSF_DIAG is set (used by the
// CI smoke test to show which unregister step fails).
static void diagUnregisterStep(const char* step, HRESULT hr) {
    wchar_t flag[2] = {};
    if (GetEnvironmentVariableW(L"VIKEY_TSF_DIAG", flag, _countof(flag)) == 0) return;
    std::fprintf(stderr, "[VikeyTsf] DllUnregisterServer %s HRESULT = 0x%08lX\n",
        step, static_cast<unsigned long>(hr));
    std::fflush(stderr);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllUnregisterServer() {
    HRESULT result = S_OK;

    // Release any TSF-created TIP instance before removing its registration.
    // Unregistering a service while TSF still owns an activated instance can
    // fail even after the application's thread manager has been deactivated.
    ITfInputProcessorProfileMgr* profileMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profileMgr));
    if (SUCCEEDED(hr)) {
        // ReleaseInputProcessor is best-effort here. TSF may return E_FAIL
        // when no ITfTextInputProcessorEx instance is retained; the actual
        // unregister operations below determine whether cleanup succeeded.
        profileMgr->ReleaseInputProcessor(CLSID_VikeyTextService, 0);
        profileMgr->Release();
    } else {
        result = hr;
    }

    // Remove the category first: ITfInputProcessorProfiles::Unregister deletes
    // the whole TIP registration, after which UnregisterCategory has nothing
    // left to remove and can report E_FAIL.
    ITfCategoryMgr* categories = nullptr;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&categories));
    diagUnregisterStep("CoCreateInstance(CategoryMgr)", hr);
    if (SUCCEEDED(hr)) {
        hr = categories->UnregisterCategory(CLSID_VikeyTextService,
            GUID_TFCAT_TIP_KEYBOARD, GUID_VikeyProfile);
        diagUnregisterStep("UnregisterCategory", hr);
        // A category that is already gone is not a cleanup failure.
        if (FAILED(hr) && hr != E_FAIL && SUCCEEDED(result)) result = hr;
        categories->Release();
    } else if (SUCCEEDED(result)) {
        result = hr;
    }

    ITfInputProcessorProfiles* profiles = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&profiles));
    diagUnregisterStep("CoCreateInstance(InputProcessorProfiles)", hr);
    if (SUCCEEDED(hr)) {
        hr = profiles->Unregister(CLSID_VikeyTextService);
        diagUnregisterStep("Unregister", hr);
        if (FAILED(hr) && SUCCEEDED(result)) result = hr;
        profiles->Release();
    } else if (SUCCEEDED(result)) {
        result = hr;
    }

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
