#include <windows.h>
#include <msctf.h>
#include <textstor.h>
#include <textstor.h>
#include <oleauto.h>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <new>
#include <cstring>
#include <string>

#include "../../TSF/tsf_guids.h"

class MemoryTextStore final : public ITextStoreACP {
public:
    MemoryTextStore() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITextStoreACP) {
            *out = static_cast<ITextStoreACP*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --refs_;
        if (!count) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE AdviseSink(REFIID iid, IUnknown* unknown, DWORD mask) override {
        if (!unknown || iid != IID_ITextStoreACPSink) return E_INVALIDARG;
        if (sink_) return E_UNEXPECTED;
        HRESULT hr = unknown->QueryInterface(iid, reinterpret_cast<void**>(&sink_));
        if (SUCCEEDED(hr)) sinkMask_ = mask;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE UnadviseSink(IUnknown* unknown) override {
        if (!sink_) return E_UNEXPECTED;
        IUnknown* existing = nullptr;
        HRESULT hr = sink_->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&existing));
        if (FAILED(hr)) return hr;
        const bool same = existing == unknown;
        existing->Release();
        if (!same) return E_UNEXPECTED;
        sink_->Release();
        sink_ = nullptr;
        sinkMask_ = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestLock(DWORD lockFlags, HRESULT* sessionResult) override {
        if (!sessionResult) return E_POINTER;
        *sessionResult = E_UNEXPECTED;
        if (!sink_) return E_UNEXPECTED;
        if (lockFlags_ != 0) {
            *sessionResult = TS_E_SYNCHRONOUS;
            return S_OK;
        }
        lockFlags_ = lockFlags;
        *sessionResult = sink_->OnLockGranted(lockFlags);
        lockFlags_ = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStatus(TS_STATUS* status) override {
        if (!status) return E_POINTER;
        status->dwDynamicFlags = 0;
        status->dwStaticFlags = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInsert(LONG start, LONG end, ULONG count,
                                           LONG* resultStart, LONG* resultEnd) override {
        if (!resultStart || !resultEnd) return E_POINTER;
        if (start < 0 || end < start || end > (LONG)text_.size()) return TS_E_INVALIDPOS;
        *resultStart = start;
        *resultEnd = start + (LONG)count;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSelection(ULONG index, ULONG count,
                                             TS_SELECTION_ACP* selections,
                                             ULONG* fetched) override {
        if (!fetched) return E_POINTER;
        *fetched = 0;
        if (!HasReadLock()) return TS_E_NOLOCK;
        if (!selections || count == 0) return E_INVALIDARG;
        if (index != TF_DEFAULT_SELECTION && index != 0) return TS_E_NOSELECTION;
        selections[0].acpStart = selectionStart_;
        selections[0].acpEnd = selectionEnd_;
        selections[0].style.ase = TS_AE_END;
        selections[0].style.fInterimChar = FALSE;
        *fetched = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSelection(ULONG count,
                                             const TS_SELECTION_ACP* selections) override {
        if (!HasWriteLock()) return TS_E_NOLOCK;
        if (!selections || count != 1) return E_INVALIDARG;
        const LONG length = (LONG)text_.size();
        if (selections[0].acpStart < 0 || selections[0].acpEnd < selections[0].acpStart ||
            selections[0].acpEnd > length) return TS_E_INVALIDPOS;
        selectionStart_ = selections[0].acpStart;
        selectionEnd_ = selections[0].acpEnd;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetText(LONG start, LONG end, WCHAR* plain,
                                       ULONG plainCapacity, ULONG* plainCopied,
                                       TS_RUNINFO* runs, ULONG runCapacity,
                                       ULONG* runsCopied, LONG* nextAcp) override {
        if (plainCopied) *plainCopied = 0;
        if (runsCopied) *runsCopied = 0;
        if (nextAcp) *nextAcp = start;
        if (!HasReadLock()) return TS_E_NOLOCK;
        const LONG length = (LONG)text_.size();
        if (start < 0 || start > length) return TS_E_INVALIDPOS;
        if (end < 0 || end > length) end = length;
        if (end < start) return TS_E_INVALIDPOS;
        const ULONG available = (ULONG)(end - start);
        const ULONG copied = plain ? (available < plainCapacity ? available : plainCapacity) : 0;
        if (copied) std::memcpy(plain, text_.data() + start, copied * sizeof(WCHAR));
        if (plainCopied) *plainCopied = copied;
        if (runs && runCapacity && copied) {
            runs[0].uCount = copied;
            runs[0].type = TS_RT_PLAIN;
            if (runsCopied) *runsCopied = 1;
        }
        if (nextAcp) *nextAcp = start + (LONG)copied;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetText(DWORD, LONG start, LONG end, const WCHAR* value,
                                       ULONG count, TS_TEXTCHANGE* change) override {
        if (!HasWriteLock()) return TS_E_NOLOCK;
        const LONG length = (LONG)text_.size();
        if (start < 0 || end < start || end > length || (!value && count)) return TS_E_INVALIDPOS;
        text_.replace((size_t)start, (size_t)(end - start), value ? value : L"", count);
        selectionStart_ = selectionEnd_ = start + (LONG)count;
        if (change) {
            change->acpStart = start;
            change->acpOldEnd = end;
            change->acpNewEnd = start + (LONG)count;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFormattedText(LONG, LONG, IDataObject** object) override {
        if (object) *object = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetEmbedded(LONG, REFGUID, REFIID, IUnknown** object) override {
        if (object) *object = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* insertable) override {
        if (!insertable) return E_POINTER;
        *insertable = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE InsertTextAtSelection(DWORD, const WCHAR* value, ULONG count,
                                                      LONG* start, LONG* end,
                                                      TS_TEXTCHANGE* change) override {
        if (!HasWriteLock()) return TS_E_NOLOCK;
        if (!value && count) return E_INVALIDARG;
        const LONG oldStart = selectionStart_, oldEnd = selectionEnd_;
        text_.replace((size_t)oldStart, (size_t)(oldEnd - oldStart), value ? value : L"", count);
        selectionStart_ = selectionEnd_ = oldStart + (LONG)count;
        if (start) *start = oldStart;
        if (end) *end = selectionEnd_;
        if (change) {
            change->acpStart = oldStart;
            change->acpOldEnd = oldEnd;
            change->acpNewEnd = selectionEnd_;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*,
                                                           TS_TEXTCHANGE*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestAttrsTransitioningAtPosition(LONG, ULONG,
                                                                    const TS_ATTRID*, DWORD) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindNextAttrTransition(LONG, LONG halt, ULONG, const TS_ATTRID*,
                                                       DWORD, LONG* next, BOOL* found,
                                                       LONG* offset) override {
        if (next) *next = halt;
        if (found) *found = FALSE;
        if (offset) *offset = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG* copied) override {
        if (copied) *copied = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEndACP(LONG* end) override {
        if (!end) return E_POINTER;
        *end = (LONG)text_.size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetActiveView(TsViewCookie* cookie) override {
        if (!cookie) return E_POINTER;
        *cookie = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetTextExt(TsViewCookie, LONG, LONG, RECT* rect, BOOL* clipped) override {
        if (!rect || !clipped) return E_POINTER;
        *rect = RECT{0, 0, 1, 1};
        *clipped = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetScreenExt(TsViewCookie, RECT* rect) override {
        if (!rect) return E_POINTER;
        *rect = RECT{0, 0, 1, 1};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetWnd(TsViewCookie, HWND* window) override {
        if (!window) return E_POINTER;
        *window = nullptr;
        return S_OK;
    }

    const std::wstring& Text() const { return text_; }

private:
    ~MemoryTextStore() {
        if (sink_) sink_->Release();
    }
    bool HasReadLock() const { return (lockFlags_ & TS_LF_READ) != 0; }
    bool HasWriteLock() const {
        return (lockFlags_ & TS_LF_READWRITE) == TS_LF_READWRITE;
    }
    std::atomic<ULONG> refs_{1};
    std::wstring text_;
    LONG selectionStart_ = 0;
    LONG selectionEnd_ = 0;
    DWORD lockFlags_ = 0;
    DWORD sinkMask_ = 0;
    ITextStoreACPSink* sink_ = nullptr;
};

static void writeLog(const char* message) {
    wchar_t dir[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"VIKEY_TSF_LOG_DIR", dir, MAX_PATH);
    if (!n || n >= MAX_PATH) wcscpy_s(dir, L"Logs");
    CreateDirectoryW(dir, nullptr);
    std::wstring path = std::wstring(dir) + L"\\tsf-textstore.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, message, (DWORD)strlen(message), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}
static std::wstring tipDllPath() {
    wchar_t module[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    std::wstring path(module);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    path.resize(slash + 1);
#if defined(_M_ARM64)
    path += L"..\\..\\App\\VikeyTsf-arm64.dll";
#else
    path += L"..\\..\\App\\VikeyTsf-x64.dll";
#endif
    wchar_t full[MAX_PATH] = {};
    if (!GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr)) return {};
    return full;
}

int wmain() {
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comInit) && comInit != RPC_E_CHANGED_MODE) {
        writeLog("FAIL: CoInitializeEx");
        return 2;
    }

    HMODULE tipModule = LoadLibraryW(tipDllPath().c_str());
    if (!tipModule) {
        writeLog("FAIL: LoadLibrary(TIP)");
        if (SUCCEEDED(comInit)) CoUninitialize();
        return 2;
    }
    using RegisterFn = HRESULT (STDAPICALLTYPE*)();
    using UnregisterFn = HRESULT (STDAPICALLTYPE*)();
    const auto registerTip = reinterpret_cast<RegisterFn>(GetProcAddress(tipModule, "DllRegisterServer"));
    const auto unregisterTip = reinterpret_cast<UnregisterFn>(GetProcAddress(tipModule, "DllUnregisterServer"));
    HRESULT hr = registerTip ? registerTip() : E_NOINTERFACE;
    if (FAILED(hr)) {
        writeLog("FAIL: DllRegisterServer");
        FreeLibrary(tipModule);
        if (SUCCEEDED(comInit)) CoUninitialize();
        return 2;
    }

    ITfThreadMgr* threadManager = nullptr;
    TfClientId clientId = TF_CLIENTID_NULL;
    ITfDocumentMgr* documentManager = nullptr;
    ITfContext* context = nullptr;
    ITfKeystrokeMgr* keystrokes = nullptr;
    ITfTextInputProcessorEx* tip = nullptr;
    auto* store = new (std::nothrow) MemoryTextStore();
    if (!store) {
        writeLog("FAIL: allocate ITextStoreACP");
        hr = E_OUTOFMEMORY;
        goto Cleanup;
    }

    hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&threadManager));
    if (FAILED(hr)) { writeLog("FAIL: create TSF thread manager"); goto Cleanup; }
    hr = threadManager->Activate(&clientId);
    if (FAILED(hr)) { writeLog("FAIL: activate TSF thread manager"); goto Cleanup; }
    hr = threadManager->CreateDocumentMgr(&documentManager);
    if (FAILED(hr)) { writeLog("FAIL: create document manager"); goto Cleanup; }
    TfEditCookie textStoreCookie = TF_INVALID_EDIT_COOKIE;
    hr = documentManager->CreateContext(clientId, 0, static_cast<ITextStoreACP*>(store),
                                         &context, &textStoreCookie);
    if (FAILED(hr)) { writeLog("FAIL: create TSF context from memory text store"); goto Cleanup; }
    hr = documentManager->Push(context);
    if (FAILED(hr)) { writeLog("FAIL: push TSF context"); goto Cleanup; }
    hr = threadManager->SetFocus(documentManager);
    if (FAILED(hr)) { writeLog("FAIL: set focused document manager"); goto Cleanup; }
    hr = threadManager->QueryInterface(IID_PPV_ARGS(&keystrokes));
    if (FAILED(hr)) { writeLog("FAIL: query ITfKeystrokeMgr"); goto Cleanup; }

    hr = CoCreateInstance(CLSID_VikeyTextService, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&tip));
    if (FAILED(hr)) { writeLog("FAIL: CoCreateInstance(Vikey TIP)"); goto Cleanup; }
    hr = tip->ActivateEx(threadManager, clientId, 0);
    if (FAILED(hr)) { writeLog("FAIL: activate Vikey TIP"); goto Cleanup; }

    {
        const WPARAM keys[] = {'V', 'I', 'E', 'T', 'S'};
        for (WPARAM key : keys) {
            BOOL eaten = FALSE;
            hr = keystrokes->TestKeyDown(key, 0, &eaten);
            if (FAILED(hr) || !eaten) {
                writeLog("FAIL: TSF TestKeyDown did not claim a Telex key");
                goto Cleanup;
            }
            eaten = FALSE;
            hr = keystrokes->KeyDown(key, 0, &eaten);
            if (FAILED(hr) || !eaten) {
                writeLog("FAIL: TSF KeyDown did not consume a Telex key");
                goto Cleanup;
            }
        }
    }

    if (store->Text() != L"vi\u00E9t") {
        writeLog("FAIL: text store result differs from vi\u00E9t");
        hr = E_FAIL;
        goto Cleanup;
    }
    writeLog("PASS: TSF TestKeyDown/KeyDown through ITextStoreACP produced vi\u00E9t");
    hr = S_OK;

Cleanup:
    if (tip) { tip->Deactivate(); tip->Release(); }
    if (keystrokes) keystrokes->Release();
    if (documentManager) documentManager->Pop(TF_POPF_ALL);
    if (context) context->Release();
    if (documentManager) documentManager->Release();
    if (threadManager) {
        threadManager->Deactivate();
        threadManager->Release();
    }
    if (store) store->Release();
    if (unregisterTip) unregisterTip();
    FreeLibrary(tipModule);
    if (SUCCEEDED(comInit)) CoUninitialize();
    return FAILED(hr) ? 1 : 0;
}
