// End-to-end TSF typing test. It drives the real TSF stack in-process, with no window or
// focus involved:
//   1. TsfController registers the TIP DLL and activates the Vikey profile (as the tray app does);
//   2. a foreground window is created and a document with a minimal ITextStoreACP is associated with
//      and focused in it, standing in for an app;
//   3. keys go through ITfKeystrokeMgr::TestKeyDown/KeyDown exactly like a TSF-aware host does;
//      a key the TIP does not claim is "typed" by this host, as an application would;
//   4. the text left in the store is compared with the expected Vietnamese.
// A claimed key that is then not delivered is dropped, which is what a real application does.
#include "../App/TsfController.h"
#include "../App/TsfSharedState.h"
#include <windows.h>
#include <objbase.h>
#include <olectl.h>
#include <msctf.h>
#include <textstor.h>
#include <inputscope.h>
#include <cstdio>
#include <cwctype>
#include <string>

using UnregisterServerFn = HRESULT (WINAPI*)();

static int g_failures = 0;
static void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_failures;
}
static std::string utf8(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}
static std::string escaped(const std::wstring& w) {
    std::string out;
    char buf[16];
    for (wchar_t c : w) {
        if (c < 0x80 && c >= 0x20) { out.push_back(static_cast<char>(c)); continue; }
        std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c));
        out += buf;
    }
    return out;
}
static void pump(int milliseconds) {
    const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(milliseconds);
    do {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(5);
    } while (GetTickCount64() < end);
}

static HWND g_window = nullptr;
static HWND g_edit = nullptr;
static LRESULT CALLBACK testWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SETFOCUS && g_edit) { SetFocus(g_edit); return 0; }
    return DefWindowProcW(h, m, w, l);
}
// A visible top-level window with an EDIT control, brought to the foreground the way livetest does it.
static bool createForegroundWindow() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = testWndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"VikeyTsfTypingTest";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    g_window = CreateWindowExW(WS_EX_TOPMOST, L"VikeyTsfTypingTest", L"Vikey TSF typing test",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 600, 300, nullptr, nullptr, inst, nullptr);
    if (!g_window) return false;
    g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
        10, 10, 560, 240, g_window, nullptr, inst, nullptr);
    for (int i = 0; i < 10 && GetForegroundWindow() != g_window; ++i) {
        const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const DWORD me = GetCurrentThreadId();
        if (foregroundThread && foregroundThread != me) AttachThreadInput(foregroundThread, me, TRUE);
        SetForegroundWindow(g_window);
        BringWindowToTop(g_window);
        SetActiveWindow(g_window);
        SetFocus(g_edit);
        if (foregroundThread && foregroundThread != me) AttachThreadInput(foregroundThread, me, FALSE);
        pump(200);
    }
    const bool front = GetForegroundWindow() == g_window;
    std::printf("test window: foreground=%s focus=%s\n", front ? "yes" : "NO",
        GetFocus() == g_edit ? "edit" : "elsewhere");
    return front;
}
static void sendKey(WORD vk) {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    pump(30);
}

// GUID_PROP_INPUTSCOPE, kept local like the TIP does.
static const GUID kGuidPropInputScope =
    { 0x1713dd5a, 0x68e7, 0x4a5b, { 0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d } };

class MockInputScope final : public ITfInputScope {
public:
    explicit MockInputScope(InputScope scope) : scope_(scope) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, __uuidof(ITfInputScope))) *object = static_cast<ITfInputScope*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(++refs_); }
    STDMETHODIMP_(ULONG) Release() override { const ULONG r = static_cast<ULONG>(--refs_); if (!r) delete this; return r; }
    STDMETHODIMP GetInputScopes(InputScope** scopes, UINT* count) override {
        if (!scopes || !count) return E_POINTER;
        auto* list = static_cast<InputScope*>(CoTaskMemAlloc(sizeof(InputScope)));
        if (!list) return E_OUTOFMEMORY;
        list[0] = scope_;
        *scopes = list;
        *count = 1;
        return S_OK;
    }
    STDMETHODIMP GetPhrase(BSTR**, UINT*) override { return E_NOTIMPL; }
    STDMETHODIMP GetRegularExpression(BSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP GetSRGS(BSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP GetXML(BSTR*) override { return E_NOTIMPL; }
private:
    LONG refs_ = 1;
    InputScope scope_;
};

// Smallest text store TSF will accept: one plain-text run, one caret, synchronous locks.
class MockTextStore final : public ITextStoreACP {
public:
    explicit MockTextStore(bool declareScope, InputScope scope = IS_DEFAULT)
        : declareScope_(declareScope), scope_(scope) {}
    ~MockTextStore() { if (sink_) sink_->Release(); }

    std::wstring text;
    LONG caret = 0;

    // What a plain application does with a key the input method did not claim.
    void hostInsert(wchar_t c) { text.insert(static_cast<size_t>(caret), 1, c); ++caret; }
    void hostBackspace() { if (caret > 0) { text.erase(static_cast<size_t>(caret - 1), 1); --caret; } }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, __uuidof(ITextStoreACP))) *object = static_cast<ITextStoreACP*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(++refs_); }
    STDMETHODIMP_(ULONG) Release() override { const ULONG r = static_cast<ULONG>(--refs_); if (!r) delete this; return r; }

    STDMETHODIMP AdviseSink(REFIID iid, IUnknown* unk, DWORD) override {
        if (!unk) return E_INVALIDARG;
        if (!IsEqualIID(iid, __uuidof(ITextStoreACPSink))) return E_INVALIDARG;
        if (sink_) return CONNECT_E_ADVISELIMIT;
        return unk->QueryInterface(__uuidof(ITextStoreACPSink), reinterpret_cast<void**>(&sink_));
    }
    STDMETHODIMP UnadviseSink(IUnknown*) override {
        if (sink_) { sink_->Release(); sink_ = nullptr; }
        return S_OK;
    }
    STDMETHODIMP RequestLock(DWORD flags, HRESULT* session) override {
        if (!session) return E_INVALIDARG;
        if (!sink_) return E_UNEXPECTED;
        if (lock_) {
            if (flags & TS_LF_SYNC) { *session = TS_E_SYNCHRONOUS; return S_OK; }
            pending_ |= flags;
            *session = TS_S_ASYNC;
            return S_OK;
        }
        lock_ = flags & TS_LF_READWRITE;
        *session = sink_->OnLockGranted(flags);
        lock_ = 0;
        while (pending_) {
            const DWORD next = pending_;
            pending_ = 0;
            lock_ = next & TS_LF_READWRITE;
            sink_->OnLockGranted(next);
            lock_ = 0;
        }
        return S_OK;
    }
    STDMETHODIMP GetStatus(TS_STATUS* status) override {
        if (!status) return E_INVALIDARG;
        status->dwDynamicFlags = 0;
        status->dwStaticFlags = 0;
        return S_OK;
    }
    STDMETHODIMP QueryInsert(LONG start, LONG end, ULONG, LONG* resultStart, LONG* resultEnd) override {
        if (!resultStart || !resultEnd) return E_INVALIDARG;
        *resultStart = start;
        *resultEnd = end;
        return S_OK;
    }
    STDMETHODIMP GetSelection(ULONG index, ULONG count, TS_SELECTION_ACP* selection, ULONG* fetched) override {
        if (!selection || !fetched) return E_INVALIDARG;
        if (!(lock_ & TS_LF_READ)) return TS_E_NOLOCK;
        *fetched = 0;
        if (index != TS_DEFAULT_SELECTION && index != 0) return TS_E_NOSELECTION;
        if (count < 1) return S_OK;
        selection[0].acpStart = caret;
        selection[0].acpEnd = caret;
        selection[0].style.ase = TS_AE_END;
        selection[0].style.fInterimChar = FALSE;
        *fetched = 1;
        return S_OK;
    }
    STDMETHODIMP SetSelection(ULONG count, const TS_SELECTION_ACP* selection) override {
        if (!(lock_ & TS_LF_READWRITE)) return TS_E_NOLOCK;
        if (count >= 1 && selection) caret = selection[0].acpEnd;
        return S_OK;
    }
    STDMETHODIMP GetText(LONG start, LONG end, WCHAR* plain, ULONG plainRequested, ULONG* plainReturned,
                         TS_RUNINFO* runs, ULONG runsRequested, ULONG* runsReturned, LONG* next) override {
        if (!plainReturned || !runsReturned || !next) return E_INVALIDARG;
        if (!(lock_ & TS_LF_READ)) return TS_E_NOLOCK;
        const LONG length = static_cast<LONG>(text.size());
        if (end == -1) end = length;
        if (start < 0 || start > length || end > length || start > end) return TS_E_INVALIDPOS;
        ULONG copy = static_cast<ULONG>(end - start);
        if (plain && copy > plainRequested) copy = plainRequested;
        if (plain) for (ULONG i = 0; i < copy; ++i) plain[i] = text[static_cast<size_t>(start) + i];
        *plainReturned = copy;
        *runsReturned = 0;
        if (runs && runsRequested >= 1 && copy > 0) {
            runs[0].uCount = copy;
            runs[0].type = TS_RT_PLAIN;
            *runsReturned = 1;
        }
        *next = start + static_cast<LONG>(copy);
        return S_OK;
    }
    STDMETHODIMP SetText(DWORD, LONG start, LONG end, const WCHAR* chars, ULONG count, TS_TEXTCHANGE* change) override {
        if (!(lock_ & TS_LF_READWRITE)) return TS_E_NOLOCK;
        const LONG length = static_cast<LONG>(text.size());
        if (start < 0 || end > length || start > end) return TS_E_INVALIDPOS;
        text.replace(static_cast<size_t>(start), static_cast<size_t>(end - start), chars ? std::wstring(chars, count) : std::wstring());
        const LONG delta = static_cast<LONG>(count) - (end - start);
        if (caret >= start && caret <= end) caret = start + static_cast<LONG>(count);
        else if (caret > end) caret += delta;
        if (change) { change->acpStart = start; change->acpOldEnd = end; change->acpNewEnd = start + static_cast<LONG>(count); }
        return S_OK;
    }
    STDMETHODIMP GetFormattedText(LONG, LONG, IDataObject**) override { return E_NOTIMPL; }
    STDMETHODIMP GetEmbedded(LONG, REFGUID, REFIID, IUnknown**) override { return E_NOTIMPL; }
    STDMETHODIMP QueryInsertEmbedded(const GUID*, const FORMATETC*, BOOL* insertable) override {
        if (insertable) *insertable = FALSE;
        return S_OK;
    }
    STDMETHODIMP InsertEmbedded(DWORD, LONG, LONG, IDataObject*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP InsertTextAtSelection(DWORD flags, const WCHAR* chars, ULONG count, LONG* start, LONG* end, TS_TEXTCHANGE* change) override {
        if (!(lock_ & TS_LF_READWRITE)) return TS_E_NOLOCK;
        const LONG at = caret;
        if (!(flags & TS_IAS_QUERYONLY)) {
            TS_TEXTCHANGE local{};
            const HRESULT hr = SetText(0, at, at, chars, count, change ? change : &local);
            if (FAILED(hr)) return hr;
        }
        if (start) *start = at;
        if (end) *end = at + static_cast<LONG>(count);
        return S_OK;
    }
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD, IDataObject*, LONG*, LONG*, TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP RequestSupportedAttrs(DWORD, ULONG count, const TS_ATTRID* filter) override { noteScopeRequest(count, filter); return S_OK; }
    STDMETHODIMP RequestAttrsAtPosition(LONG, ULONG count, const TS_ATTRID* filter, DWORD) override { noteScopeRequest(count, filter); return S_OK; }
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD) override { return S_OK; }
    STDMETHODIMP FindNextAttrTransition(LONG, LONG, ULONG, const TS_ATTRID*, DWORD, LONG* next, BOOL* found, LONG* offset) override {
        if (next) *next = 0;
        if (found) *found = FALSE;
        if (offset) *offset = 0;
        return S_OK;
    }
    STDMETHODIMP RetrieveRequestedAttrs(ULONG count, TS_ATTRVAL* values, ULONG* fetched) override {
        if (!values || !fetched) return E_INVALIDARG;
        *fetched = 0;
        if (declareScope_ && scopeRequested_ && count >= 1) {
            values[0].idAttr = kGuidPropInputScope;
            values[0].dwOverlapId = 0;
            VariantInit(&values[0].varValue);
            values[0].varValue.vt = VT_UNKNOWN;
            values[0].varValue.punkVal = new MockInputScope(scope_);
            *fetched = 1;
        }
        scopeRequested_ = false;
        return S_OK;
    }
    STDMETHODIMP GetEndACP(LONG* acp) override {
        if (!acp) return E_INVALIDARG;
        if (!(lock_ & TS_LF_READ)) return TS_E_NOLOCK;
        *acp = static_cast<LONG>(text.size());
        return S_OK;
    }
    STDMETHODIMP GetActiveView(TsViewCookie* view) override { if (!view) return E_INVALIDARG; *view = 0; return S_OK; }
    STDMETHODIMP GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG*) override { return E_NOTIMPL; }
    STDMETHODIMP GetTextExt(TsViewCookie, LONG, LONG, RECT*, BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP GetScreenExt(TsViewCookie, RECT*) override { return E_NOTIMPL; }
    STDMETHODIMP GetWnd(TsViewCookie, HWND* window) override { if (window) *window = nullptr; return S_OK; }

private:
    void noteScopeRequest(ULONG count, const TS_ATTRID* filter) {
        if (!filter) { scopeRequested_ = true; return; }
        for (ULONG i = 0; i < count; ++i)
            if (IsEqualGUID(filter[i], kGuidPropInputScope)) scopeRequested_ = true;
    }
    LONG refs_ = 1;
    ITextStoreACPSink* sink_ = nullptr;
    DWORD lock_ = 0;
    DWORD pending_ = 0;
    bool declareScope_;
    bool scopeRequested_ = false;
    InputScope scope_;
};

struct Counters { int claimed = 0; int delivered = 0; int dropped = 0; int passedThrough = 0; };

// One key press as a TSF-aware application handles it.
static void pressKey(ITfKeystrokeMgr* keys, MockTextStore* store, WORD vk, Counters& counters) {
    const LPARAM lParam = 1 | (static_cast<LPARAM>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16);
    BOOL claimed = FALSE;
    HRESULT hr = keys->TestKeyDown(vk, lParam, &claimed);
    if (SUCCEEDED(hr) && claimed) {
        ++counters.claimed;
        BOOL handled = FALSE;
        hr = keys->KeyDown(vk, lParam, &handled);
        if (SUCCEEDED(hr) && handled) ++counters.delivered;
        else ++counters.dropped;   // claimed by TestKeyDown, not handled by KeyDown: the application never sees the key
        return;
    }
    ++counters.passedThrough;
    if (vk >= 'A' && vk <= 'Z') store->hostInsert(static_cast<wchar_t>(vk - 'A' + L'a'));
    else if (vk >= '0' && vk <= '9') store->hostInsert(static_cast<wchar_t>(vk));
    else if (vk == VK_SPACE) store->hostInsert(L' ');
    else if (vk == VK_BACK) store->hostBackspace();
}

static void typeText(ITfKeystrokeMgr* keys, MockTextStore* store, const std::wstring& typed, Counters& counters) {
    for (wchar_t c : typed) {
        if (c == L'\b') { pressKey(keys, store, VK_BACK, counters); continue; }
        if (c == L' ') { pressKey(keys, store, VK_SPACE, counters); continue; }
        pressKey(keys, store, static_cast<WORD>(towupper(c)), counters);
    }
}

// Opens a focused document backed by `store` and types into it.
static bool runScenario(ITfThreadMgr* threadMgr, TfClientId client, const char* name, MockTextStore* store,
                        const std::wstring& typed, const std::wstring& expected, bool expectClaims) {
    ITfDocumentMgr* document = nullptr;
    ITfContext* context = nullptr;
    TfEditCookie cookie = 0;
    HRESULT hr = threadMgr->CreateDocumentMgr(&document);
    if (FAILED(hr) || !document) { std::printf("FAIL %s: CreateDocumentMgr hr=0x%08lX\n", name, static_cast<unsigned long>(hr)); ++g_failures; return false; }
    hr = document->CreateContext(client, 0, static_cast<IUnknown*>(static_cast<ITextStoreACP*>(store)), &context, &cookie);
    if (FAILED(hr) || !context) { std::printf("FAIL %s: CreateContext hr=0x%08lX\n", name, static_cast<unsigned long>(hr)); document->Release(); ++g_failures; return false; }
    hr = document->Push(context);
    ITfDocumentMgr* previousDocument = nullptr;
    if (SUCCEEDED(hr) && g_window) hr = threadMgr->AssociateFocus(g_window, document, &previousDocument);
    if (SUCCEEDED(hr)) hr = threadMgr->SetFocus(document);
    if (FAILED(hr)) { std::printf("FAIL %s: Push/SetFocus hr=0x%08lX\n", name, static_cast<unsigned long>(hr)); context->Release(); document->Release(); ++g_failures; return false; }
    pump(300);

    ITfKeystrokeMgr* keys = nullptr;
    hr = threadMgr->QueryInterface(__uuidof(ITfKeystrokeMgr), reinterpret_cast<void**>(&keys));
    if (FAILED(hr) || !keys) { std::printf("FAIL %s: no ITfKeystrokeMgr\n", name); context->Release(); document->Release(); ++g_failures; return false; }
    CLSID foreground = {};
    const HRESULT fgHr = keys->GetForeground(&foreground);
    wchar_t fgText[64] = {};
    StringFromGUID2(foreground, fgText, _countof(fgText));
    std::printf("%s: foreground TIP hr=0x%08lX %s\n", name, static_cast<unsigned long>(fgHr), utf8(fgText).c_str());

    Counters counters;
    typeText(keys, store, typed, counters);
    pump(100);
    std::printf("%s: typed=\"%s\" result=\"%s\" (%s) claimed=%d delivered=%d dropped=%d passed=%d\n", name,
        utf8(typed).c_str(), utf8(store->text).c_str(), escaped(store->text).c_str(),
        counters.claimed, counters.delivered, counters.dropped, counters.passedThrough);
    const bool textOk = store->text == expected;
    const bool noDrops = counters.dropped == 0;
    const bool claimsOk = expectClaims ? counters.claimed > 0 : counters.claimed == 0;
    const std::string label(name);
    check((label + ": text is \"" + utf8(expected) + "\"").c_str(), textOk);
    check((label + ": no key claimed but dropped").c_str(), noDrops);
    check((label + (expectClaims ? ": TIP claimed Vietnamese edits" : ": TIP claimed nothing")).c_str(), claimsOk);

    keys->Release();
    if (g_window) {
        ITfDocumentMgr* ours = nullptr;
        threadMgr->AssociateFocus(g_window, previousDocument, &ours);
        if (ours) ours->Release();
    }
    if (previousDocument) previousDocument->Release();
    context->Release();
    document->Release();
    return textOk && noDrops && claimsOk;
}

// The path a user takes: real key events delivered by the system to a Win32 EDIT control.
static void runEditScenario(const char* name, const std::wstring& typed, const std::wstring& expected) {
    if (!g_window || !g_edit) { std::printf("FAIL %s: no test window\n", name); ++g_failures; return; }
    SetWindowTextW(g_edit, L"");
    SetFocus(g_edit);
    SendMessageW(g_edit, EM_SETSEL, 0, 0);
    pump(200);
    for (wchar_t c : typed) {
        if (c == L'\b') sendKey(VK_BACK);
        else if (c == L' ') sendKey(VK_SPACE);
        else sendKey(static_cast<WORD>(towupper(c)));
    }
    pump(300);
    const int length = GetWindowTextLengthW(g_edit);
    std::wstring got(static_cast<size_t>(length), L'\0');
    if (length > 0) GetWindowTextW(g_edit, &got[0], length + 1);
    std::printf("%s: typed=\"%s\" result=\"%s\" (%s) foreground=%s\n", name, utf8(typed).c_str(),
        utf8(got).c_str(), escaped(got).c_str(), GetForegroundWindow() == g_window ? "yes" : "NO");
    check((std::string(name) + ": text is \"" + utf8(expected) + "\"").c_str(), got == expected);
}

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
        auto unregisterServer = reinterpret_cast<UnregisterServerFn>(GetProcAddress(module, "DllUnregisterServer"));
        ok = unregisterServer && SUCCEEDED(unregisterServer());
        FreeLibrary(module);
    }
    CoUninitialize();
    return ok;
}

int wmain() {
    std::printf("TSF typing test: Caps Lock is %s\n", (GetKeyState(VK_CAPITAL) & 1) ? "ON (results may differ)" : "off");

    // The tray app owns this shared state; the TIP only types while the controller is reachable
    // and the focused field is not flagged secure.
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        sizeof(VikeyTsfSharedState), VIKEY_TSF_SHARED_STATE_NAME);
    auto* shared = mapping ? static_cast<VikeyTsfSharedState*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VikeyTsfSharedState))) : nullptr;
    if (!shared) { std::puts("FAIL cannot create the controller shared state"); return 1; }
    shared->controllerProcessId = GetCurrentProcessId();
    InterlockedExchange(&shared->secureContext, 0);

    if (!vikey::tsf::initialize()) { std::puts("FAIL TSF controller initialize"); return 1; }
    const bool active = vikey::tsf::sync(true);
    check("Vikey TSF profile is active", active);

    ITfThreadMgr* threadMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&threadMgr));
    TfClientId client = TF_CLIENTID_NULL;
    if (SUCCEEDED(hr) && threadMgr) hr = threadMgr->Activate(&client);
    if (FAILED(hr) || !threadMgr || client == TF_CLIENTID_NULL) {
        std::printf("FAIL cannot get an activated ITfThreadMgr hr=0x%08lX\n", static_cast<unsigned long>(hr));
        vikey::tsf::shutdown();
        return 1;
    }

    const bool foreground = createForegroundWindow();
    check("test window is the foreground window", foreground);

    {   // An ordinary text field: no input scope declared.
        auto* store = new MockTextStore(false);
        runScenario(threadMgr, client, "plain text field", store, L"vieetj nams ddi", L"việt nám đi", true);
        store->Release();
    }
    {   // Backspace must pass straight through to the application.
        auto* store = new MockTextStore(false);
        runScenario(threadMgr, client, "backspace and plain letters", store, L"abc\b\bd", L"ad", false);
        store->Release();
    }
    {   // A password field declares its scope: never transformed, never dropped.
        auto* store = new MockTextStore(true, IS_PASSWORD);
        runScenario(threadMgr, client, "password field", store, L"vieetj", L"vieetj", false);
        store->Release();
    }

    runEditScenario("win32 EDIT control through SendInput", L"vieetj nams ddi", L"vi\u1ec7t n\u00e1m \u0111i");

    if (g_window) DestroyWindow(g_window);
    threadMgr->Deactivate();
    threadMgr->Release();
    vikey::tsf::shutdown();
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    const bool cleaned = cleanupRegistration();
    check("registration cleaned up", cleaned);
    std::printf("TSF typing test: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
