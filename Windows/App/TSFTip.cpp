#include "TSFTip.h"
#include "TsfEngineAdapter.h"
#include "KeyMap.h"
#include "TSFPolicy.h"
#include "TsfEngineRuntime.h"
#include "TsfDiagnostics.h"
#include <inputscope.h>
#include <vector>
#include <string>
#include <climits>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <new>
#include <utility>

const CLSID CLSID_VikeyTextService =
{ 0x9b7a4d31, 0x6d8f, 0x4d22, { 0xa1, 0x73, 0x5e, 0x91, 0x2c, 0x44, 0x8b, 0x60 } };


namespace {
// GUID_PROP_INPUTSCOPE is not exported by every Windows SDK uuid.lib. Keep the
// documented TSF property GUID local so the TIP has no optional GUID import.
static const GUID kGuidPropInputScope =
    { 0x1713dd5a, 0x68e7, 0x4a5b, { 0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d } };

// The TIP runs inside whatever application has focus, so it cannot use the
// controller's log directory. Write beside this DLL instead, and only once per
// process for each reason, to explain why a key was not turned into Vietnamese.
static HMODULE thisModule() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&thisModule), &module);
    return module;
}

static void writeTipLine(const wchar_t* text);

static void tipNote(unsigned id, const wchar_t* text) {
    static std::atomic<unsigned long long> seen{0};
    const unsigned long long bit = 1ULL << (id & 63);
    if (seen.fetch_or(bit) & bit) return;
    writeTipLine(text);
}

// Per-key detail, bounded so a long typing session cannot grow the log without limit.
static void tipTrace(const wchar_t* text) {
    static std::atomic<unsigned> lines{0};
    if (lines.fetch_add(1) >= 600) return;
    writeTipLine(text);
}

static void writeTipLine(const wchar_t* text) {
    wchar_t dll[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(thisModule(), dll, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    std::wstring path(dll, length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    path.resize(slash);
    path += L"\\Logs";
    CreateDirectoryW(path.c_str(), nullptr);
    path += L"\\tsf-tip.log";

    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* exeName = wcsrchr(exe, L'\\');
    exeName = exeName ? exeName + 1 : exe;

    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t line[1024] = {};
    const int chars = swprintf_s(line, _countof(line),
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%s pid=%lu] %s\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        time.wMilliseconds, exeName, static_cast<unsigned long>(GetCurrentProcessId()),
        text ? text : L"");
    if (chars <= 0) return;
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, chars, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return;
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line, chars, &utf8[0], bytes, nullptr, nullptr);
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

static bool isSafeInputContext(TfEditCookie cookie, ITfRange* range) {
    if (!range) return false;
    ITfContext* context = nullptr;
    HRESULT hr = range->GetContext(&context);
    if (FAILED(hr) || !context) return false;
    ITfReadOnlyProperty* property = nullptr;
    hr = context->GetAppProperty(kGuidPropInputScope, &property);
    context->Release();
    if (FAILED(hr) || !property) { tipNote(19, L"input scope: application exposes no input-scope property"); return false; }

    VARIANT value;
    VariantInit(&value);
    hr = property->GetValue(cookie, range, &value);
    property->Release();
    // TSF reports "this range has no value for the property" either as success or as
    // E_FAIL, depending on the text store, always with an empty VARIANT.
    if ((value.vt == VT_EMPTY || value.vt == VT_NULL) && (hr == S_OK || hr == S_FALSE || hr == E_FAIL)) {
        // Most applications never declare an input scope, which means an ordinary
        // text field. Password and other sensitive fields declare an explicit
        // scope (checked below) and are also flagged by the controller's UI
        // Automation check (isSecureInputContext).
        tipNote(20, L"input scope: none declared, treated as plain text");
        VariantClear(&value);
        return true;
    }
    if (FAILED(hr) || value.vt != VT_UNKNOWN || !value.punkVal) {
        wchar_t note[160] = {};
        swprintf_s(note, _countof(note), L"input scope: property has no value (hr=0x%08lX vt=%u), treated as unsafe",
            static_cast<unsigned long>(hr), static_cast<unsigned>(value.vt));
        tipNote(20, note);
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
            wchar_t note[128] = {};
            swprintf_s(note, _countof(note), L"input scope: scope %d is not allowed", static_cast<int>(scopes[i]));
            tipNote(21, note);
            safe = false;
            break;
        }
    }
    CoTaskMemFree(scopes);
    return safe;
}

// Keys that type a printable character and end the current word: Space, digits, punctuation.
static bool isWordBreakCharacter(UINT vk) {
    return vk == VK_SPACE || (vk >= '0' && vk <= '9') ||
           (vk >= 0xBA && vk <= 0xC0) || (vk >= 0xDB && vk <= 0xDF);
}

// The character this key types with the active layout and the current Shift/Caps Lock state.
static std::wstring typedCharacter(UINT vk) {
    BYTE state[256] = {};
    if (GetKeyState(VK_SHIFT) & 0x8000) state[VK_SHIFT] = 0x80;
    if (GetKeyState(VK_CAPITAL) & 0x0001) state[VK_CAPITAL] = 0x01;
    wchar_t buffer[8] = {};
    const HKL layout = GetKeyboardLayout(0);
    const UINT scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, layout);
    // Flag 0x4: do not change the keyboard's dead-key state.
    const int count = ToUnicodeEx(vk, scan, state, buffer, _countof(buffer), 0x4, layout);
    return count == 1 ? std::wstring(buffer, 1) : std::wstring();
}

// Types plain text at the caret. Used when a claimed key cannot be turned into its Vietnamese
// edit: TSF no longer delivers a claimed key, so the character must be inserted here or it is lost.
class VikeyInsertSession final : public ITfEditSession {
public:
    VikeyInsertSession(ITfContext* context, std::wstring text) : context_(context), text_(std::move(text)) {
        if (context_) context_->AddRef();
    }
    ~VikeyInsertSession() { if (context_) context_->Release(); }
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
    bool inserted() const { return inserted_; }
    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
        if (!context_ || text_.empty()) return E_UNEXPECTED;
        TF_SELECTION selection{};
        ULONG fetched = 0;
        HRESULT hr = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
        if (FAILED(hr) || fetched != 1 || !selection.range) {
            if (selection.range) selection.range->Release();
            return FAILED(hr) ? hr : TF_E_NOSELECTION;
        }
        hr = selection.range->SetText(cookie, 0, text_.c_str(), static_cast<LONG>(text_.size()));
        if (SUCCEEDED(hr)) {
            inserted_ = true;
            if (SUCCEEDED(selection.range->Collapse(cookie, TF_ANCHOR_END))) {
                TF_SELECTION caret{};
                caret.range = selection.range;
                caret.style.ase = TF_AE_END;
                caret.style.fInterimChar = FALSE;
                context_->SetSelection(cookie, 1, &caret);
            }
        }
        selection.range->Release();
        return hr;
    }
private:
    std::atomic<ULONG> refs_{1};
    ITfContext* context_ = nullptr;
    std::wstring text_;
    bool inserted_ = false;
};

// Read-only check run from OnTestKeyDown, so a key is only claimed when the
// focused field is an editable, empty-selection, non-sensitive text context.
class VikeyContextProbe final : public ITfEditSession {
public:
    explicit VikeyContextProbe(ITfContext* context) : context_(context) {
        if (context_) context_->AddRef();
    }
    ~VikeyContextProbe() { if (context_) context_->Release(); }
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
    bool safe() const { return safe_; }

    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
        safe_ = false;
        if (!context_) return E_UNEXPECTED;
        TF_STATUS status{};
        HRESULT hr = context_->GetStatus(&status);
        if (FAILED(hr)) { tipNote(8, L"probe: ITfContext::GetStatus failed"); return hr; }
        if (status.dwDynamicFlags & TF_SD_READONLY) { tipNote(9, L"probe: context is read-only"); return S_OK; }
        TF_SELECTION selection{};
        ULONG fetched = 0;
        hr = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
        if (FAILED(hr) || fetched != 1 || !selection.range) {
            if (selection.range) selection.range->Release();
            tipNote(10, L"probe: no selection available (GetSelection failed)");
            return S_OK;
        }
        BOOL empty = FALSE;
        hr = selection.range->IsEmpty(cookie, &empty);
        if (SUCCEEDED(hr) && empty) safe_ = isSafeInputContext(cookie, selection.range);
        else if (SUCCEEDED(hr)) tipNote(12, L"probe: text is selected, key left to the application");
        selection.range->Release();
        return S_OK;
    }
private:
    std::atomic<ULONG> refs_{1};
    ITfContext* context_ = nullptr;
    bool safe_ = false;
};

class VikeyEditSession final : public ITfEditSession {
public:
    VikeyEditSession(ITfContext* context, vikey::tsf::EngineAdapter* adapter,
                     const vikey::tsf::EngineEdit& edit)
        : context_(context), adapter_(adapter), edit_(edit) {
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
    bool textNotReachable() const { return unreachable_; }

    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
        if (!context_ || !adapter_) return E_UNEXPECTED;
        TF_STATUS status{};
        HRESULT hr = context_->GetStatus(&status);
        if (FAILED(hr)) { tipNote(8, L"edit session: ITfContext::GetStatus failed"); return hr; }
        if (status.dwDynamicFlags & TF_SD_READONLY) { tipNote(9, L"edit session: context is read-only"); return TS_E_READONLY; }

        TF_SELECTION selection{};
        ULONG fetched = 0;
        hr = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
        if (FAILED(hr) || fetched != 1 || !selection.range) {
            tipNote(10, L"edit session: no selection available (GetSelection failed)");
            if (selection.range) selection.range->Release();
            adapter_->reset();
            return FAILED(hr) ? hr : TF_E_NOSELECTION;
        }

        BOOL selectionEmpty = FALSE;
        hr = selection.range->IsEmpty(cookie, &selectionEmpty);
        unsigned declinedId = 0;
        const wchar_t* declined = nullptr;
        if (FAILED(hr)) { declinedId = 11; declined = L"edit session: IsEmpty failed"; }
        else if (!selectionEmpty) { declinedId = 12; declined = L"edit session: text is selected, key left to the application"; }
        else if (!isSafeInputContext(cookie, selection.range)) { declinedId = 13; declined = L"edit session: input scope not allowed or unavailable (see the input scope line)"; }
        else if (vikey::tsf::isSecureInputContext()) { declinedId = 14; declined = L"edit session: controller marks the focused field as secure/unknown"; }
        else if (!vikey::tsf::isControllerRunning()) { declinedId = 15; declined = L"edit session: Vikey controller not reachable"; }
        else if (!vikey::tsf::shouldHandleKey(vikey::tsf::TextContextKind::Editable, vLanguage != 0, false)) { declinedId = 16; declined = L"edit session: policy refused the key"; }
        if (declined) {
            tipNote(declinedId, declined);
            selection.range->Release();
            adapter_->reset();
            return FAILED(hr) ? hr : S_OK;
        }

        const vikey::tsf::EngineEdit& edit = edit_;
        if (!edit.consume) {
            tipNote(17, L"edit session: engine ran and passed the key through (normal for plain letters)");
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
                unreachable_ = true;
                wchar_t note[160] = {};
                swprintf_s(note, _countof(note), L"edit session: ShiftStart could not move back (requested=%ld shifted=%ld hr=0x%08lX)",
                    static_cast<long>(requested), static_cast<long>(shifted), static_cast<unsigned long>(hr));
                tipTrace(note);
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
        tipNote(18, L"edit session: first Vietnamese edit applied");
        return S_OK;
    }

private:
    std::atomic<ULONG> refs_{1};
    ITfContext* context_ = nullptr;
    vikey::tsf::EngineAdapter* adapter_ = nullptr;
    vikey::tsf::EngineEdit edit_;
    bool applied_ = false;
    bool unreachable_ = false;
};

// Keeps the word being typed inside a TSF composition and rewrites the whole composition text on
// every key. Nothing has to be read back from the application, so it also works in controls that
// do not expose their text to TSF (Win32 EDIT, Notepad++, other IMM-bridged windows).
class VikeyComposeSession final : public ITfEditSession {
public:
    // text == nullptr ends the composition; otherwise the composition text becomes *text.
    VikeyComposeSession(ITfContext* context, ITfComposition** slot, ITfCompositionSink* sink,
                        const std::wstring* text, bool endAfter = false)
        : context_(context), slot_(slot), sink_(sink), endAfter_(endAfter) {
        if (context_) context_->AddRef();
        if (text) { text_ = *text; end_ = false; }
    }
    ~VikeyComposeSession() { if (context_) context_->Release(); }
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
    bool done() const { return done_; }
    STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
        if (!context_ || !slot_) return E_UNEXPECTED;
        if (end_) {
            if (*slot_) {
                (*slot_)->EndComposition(cookie);
                (*slot_)->Release();
                *slot_ = nullptr;
            }
            done_ = true;
            return S_OK;
        }
        if (!*slot_) {
            TF_SELECTION selection{};
            ULONG fetched = 0;
            HRESULT hr = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
            if (FAILED(hr) || fetched != 1 || !selection.range) {
                if (selection.range) selection.range->Release();
                tipNote(26, L"compose: no selection to start a composition at");
                return FAILED(hr) ? hr : TF_E_NOSELECTION;
            }
            ITfContextComposition* owner = nullptr;
            hr = context_->QueryInterface(IID_PPV_ARGS(&owner));
            if (SUCCEEDED(hr)) {
                hr = owner->StartComposition(cookie, selection.range, sink_, slot_);
                owner->Release();
            }
            selection.range->Release();
            if (FAILED(hr) || !*slot_) {
                wchar_t note[120] = {};
                swprintf_s(note, _countof(note), L"compose: StartComposition failed (hr=0x%08lX)", static_cast<unsigned long>(hr));
                tipNote(27, note);
                *slot_ = nullptr;
                return FAILED(hr) ? hr : E_FAIL;
            }
        }
        ITfRange* range = nullptr;
        HRESULT hr = (*slot_)->GetRange(&range);
        if (FAILED(hr) || !range) return FAILED(hr) ? hr : E_FAIL;
        hr = range->SetText(cookie, 0, text_.c_str(), static_cast<LONG>(text_.size()));
        if (SUCCEEDED(hr)) {
            TF_SELECTION caret{};
            if (SUCCEEDED(range->Collapse(cookie, TF_ANCHOR_END))) {
                caret.range = range;
                caret.style.ase = TF_AE_END;
                caret.style.fInterimChar = FALSE;
                context_->SetSelection(cookie, 1, &caret);
            }
            done_ = true;
            tipNote(18, L"compose: first composition update applied");
        }
        range->Release();
        if (SUCCEEDED(hr) && endAfter_ && *slot_) {
            (*slot_)->EndComposition(cookie);
            (*slot_)->Release();
            *slot_ = nullptr;
        }
        return hr;
    }
private:
    std::atomic<ULONG> refs_{1};
    ITfContext* context_ = nullptr;
    ITfComposition** slot_ = nullptr;
    ITfCompositionSink* sink_ = nullptr;
    std::wstring text_;
    bool end_ = true;
    bool endAfter_ = false;
    bool done_ = false;
};
}

namespace {
HMODULE g_module = nullptr;
std::atomic<long> g_objects{0};
std::atomic<long> g_locks{0};

class VikeyTextService final : public ITfTextInputProcessor, public ITfKeyEventSink, public ITfCompositionSink {
public:
    VikeyTextService() { ++g_objects; }
    ~VikeyTextService() {
        // The TSF manager normally retains the advised sink until Unadvise.
        // Keep cleanup best-effort here; Deactivate reports failures to TSF.
        unadvise();
        dropComposition();
        --g_objects;
    }

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_ITfTextInputProcessor))
            *object = static_cast<ITfTextInputProcessor*>(this);
        else if (IsEqualIID(iid, IID_ITfKeyEventSink))
            *object = static_cast<ITfKeyEventSink*>(this);
        else if (IsEqualIID(iid, IID_ITfCompositionSink))
            *object = static_cast<ITfCompositionSink*>(this);
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
        // The adapter owns per-thread engine state. Initialize it on the TSF
        // activation thread before the key sink can receive callbacks.
        if (!adapter_.initialize()) return E_FAIL;
        ITfKeystrokeMgr* keyMgr = nullptr;
        HRESULT hr = threadMgr->QueryInterface(IID_PPV_ARGS(&keyMgr));
        if (FAILED(hr)) { adapter_.reset(); return hr; }
        threadMgr_ = threadMgr;
        threadMgr_->AddRef();
        keyMgr_ = keyMgr;
        clientId_ = clientId;
        hr = keyMgr_->AdviseKeyEventSink(clientId_, this, TRUE);
        if (FAILED(hr)) {
            // Advise failed, so no sink registration exists; release the
            // interfaces acquired during activation.
            releaseManagers();
            adapter_.reset();
            return hr;
        }
        advised_ = true;
        composeProgram_ = programNeedsComposition();
        tipNote(33, L"Activate: key event sink installed in this process");
        return S_OK;
    }

    STDMETHODIMP OnCompositionTerminated(TfEditCookie, ITfComposition*) override {
        // The application ended the composition (click, focus change...): the word is committed.
        dropComposition();
        adapter_.reset();
        return S_OK;
    }

    STDMETHODIMP Deactivate() override {
        adapter_.reset();
        hasPending_ = false;
        dropComposition();
        return unadvise();
    }

    STDMETHODIMP OnSetFocus(BOOL) override {
        tipNote(34, L"OnSetFocus: this process has keyboard focus");
        adapter_.reset();
        hasPending_ = false;
        dropComposition();
        observedLanguage_ = vLanguage;
        return S_OK;
    }
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = prepare(context, static_cast<UINT>(key)) ? TRUE : FALSE;
        testedKey_ = static_cast<UINT>(key);
        testedUnclaimed_ = !*eaten;
        return S_OK;
    }
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        if (!context) return S_OK;
        if (testedUnclaimed_ && testedKey_ == static_cast<UINT>(key)) {
            // OnTestKeyDown already looked at this key and left it to the application.
            testedUnclaimed_ = false;
            return S_OK;
        }
        testedUnclaimed_ = false;
        if (!hasPending_ || pendingKey_ != static_cast<UINT>(key)) {
            // Some applications call KeyDown without TestKeyDown first.
            tipNote(23, L"OnKeyDown arrived without OnTestKeyDown; handling the key here");
            if (!prepare(context, static_cast<UINT>(key))) return S_OK;
        }
        hasPending_ = false;
        const UINT vk = static_cast<UINT>(key);
        if (pendingDirect_) {
            auto* session = new (std::nothrow) VikeyEditSession(context, &adapter_, pendingEdit_);
            if (!session) return E_OUTOFMEMORY;
            HRESULT sessionResult = E_FAIL;
            const HRESULT hr = context->RequestEditSession(clientId_, session,
                TF_ES_SYNC | TF_ES_READWRITE, &sessionResult);
            if (SUCCEEDED(hr) && SUCCEEDED(sessionResult) && session->applied()) {
                *eaten = TRUE;
            } else {
                if (session->textNotReachable()) {
                    // The control keeps its text to itself (Notepad++, Win32 EDIT): from now on this
                    // context is edited through a composition.
                    markNeedsComposition(context);
                    tipNote(30, L"context does not expose its text; switching it to composition editing");
                } else {
                    wchar_t note[180] = {};
                    swprintf_s(note, _countof(note), L"edit did not apply (request hr=0x%08lX, session hr=0x%08lX)",
                        static_cast<unsigned long>(hr), static_cast<unsigned long>(sessionResult));
                    tipNote(24, note);
                }
                adapter_.reset();
                // TSF no longer delivers a claimed key, so type it here.
                *eaten = insertPlainCharacter(context, vk) ? TRUE : FALSE;
                if (!*eaten) tipNote(25, L"the plain character could not be typed either; the key was lost");
            }
            session->Release();
            return S_OK;
        }
        const bool ending = pendingText_.empty();
        auto* session = new (std::nothrow) VikeyComposeSession(context, &composition_,
            static_cast<ITfCompositionSink*>(this), ending ? nullptr : &pendingText_, pendingEnd_);
        if (!session) return E_OUTOFMEMORY;
        if (!composition_ && composeContext_ != context) {
            if (composeContext_) composeContext_->Release();
            composeContext_ = context;
            composeContext_->AddRef();
        }
        HRESULT sessionResult = E_FAIL;
        const HRESULT hr = context->RequestEditSession(clientId_, session,
            TF_ES_SYNC | TF_ES_READWRITE, &sessionResult);
        if (SUCCEEDED(hr) && SUCCEEDED(sessionResult) && session->done()) {
            if (ending || pendingEnd_) dropComposition(); else word_ = pendingText_;
            *eaten = TRUE;
        } else {
            wchar_t note[180] = {};
            swprintf_s(note, _countof(note), L"composition update failed (request hr=0x%08lX, session hr=0x%08lX); typing the plain character instead",
                static_cast<unsigned long>(hr), static_cast<unsigned long>(sessionResult));
            tipNote(31, note);
            dropComposition();
            adapter_.reset();
            *eaten = vk == VK_BACK ? TRUE : (insertPlainCharacter(context, vk) ? TRUE : FALSE);
            if (!*eaten) tipNote(25, L"the plain character could not be typed either; the key was lost");
        }
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
    // Applications that keep their text to themselves (Notepad++, Win32 EDIT) are remembered by
    // program name, so only the very first word typed in such a program is lost.
    static bool programNeedsComposition() {
        wchar_t path[MAX_PATH] = {};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
        const wchar_t* name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        DWORD value = 0, size = sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Vikey\\TsfComposeApps", name,
                            RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS && value == 1;
    }
    static void rememberProgramNeedsComposition() {
        wchar_t path[MAX_PATH] = {};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
        const wchar_t* name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Vikey\\TsfComposeApps", 0, nullptr, 0,
                            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
        const DWORD one = 1;
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
        RegCloseKey(key);
    }
    bool needsComposition(ITfContext*) const { return composeProgram_; }
    void markNeedsComposition(ITfContext*) {
        if (composeProgram_) return;
        composeProgram_ = true;
        rememberProgramNeedsComposition();
    }

    // Decides what a key does. Returns true when the key is claimed (the application must not get it)
    // and leaves the edit to apply in pendingDirect_/pendingEdit_ or pendingText_.
    bool prepare(ITfContext* context, UINT key) {
        hasPending_ = false;
        refreshRuntimeSettings();
        tipNote(0, L"key handler reached: TIP is loaded and receiving keys");
        if (!context) { tipNote(1, L"declined: null context"); return false; }
        if (vLanguage == 0) { tipNote(2, L"declined: Vietnamese input is off (InputMethod=0)"); return false; }
        if (vikey::tsf::isCurrentAppBlacklisted()) { tipNote(3, L"declined: foreground app is blacklisted"); return false; }
        if (!vikey::tsf::isCurrentInputLanguageSupported()) { tipNote(4, L"declined: current keyboard language not supported"); return false; }
        if (!vikey::tsf::isControllerRunning()) {
            tipNote(5, L"declined: Vikey controller not reachable (shared state cannot be opened, or controller is elevated/not running)");
            return false;
        }
        if (vikey::tsf::isSecureInputContext()) { tipNote(6, L"declined: controller marks the focused field as secure/unknown"); return false; }
        if (vikey::engineKey(key) == KEY_EMPTY) return false;

        const bool composeMode = needsComposition(context);
        if (composition_ && (composeContext_ != context || !composeMode)) dropComposition();
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        const bool modified =
            (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
            (GetKeyState(VK_MENU) & 0x8000) != 0 ||
            (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
            (GetKeyState(VK_RWIN) & 0x8000) != 0;
        if (modified || !contextAllowsEditing(context)) {
            endComposition();
            adapter_.reset();
            return false;
        }
        vikey::tsf::EngineEdit edit = adapter_.processKey(key, shift, capsLock, modified);
        {
            wchar_t note[160] = {};
            swprintf_s(note, _countof(note), L"engine vk=0x%02X consume=%d backspaces=%u textLength=%u mode=%s",
                static_cast<unsigned>(key), edit.consume ? 1 : 0, static_cast<unsigned>(edit.backspaces),
                static_cast<unsigned>(edit.text.size()), composeMode ? L"composition" : L"direct");
            tipTrace(note);
        }

        if (!composeMode) {
            // Applications that expose their text: the engine's edit is applied in place and plain
            // letters are left alone, so no composition (and no underline) is ever shown.
            if (!edit.consume) return false;
            pendingKey_ = key;
            pendingEdit_ = std::move(edit);
            pendingDirect_ = true;
            hasPending_ = true;
            tipNote(7, L"claimed a key for a Vietnamese edit");
            return true;
        }

        const bool isLetter = key >= 'A' && key <= 'Z';
        const bool composing = composition_ != nullptr;
        std::wstring target = word_;
        bool claim = false;
        pendingEnd_ = false;
        if (key == VK_BACK) {
            if (composing && !target.empty()) { target.pop_back(); claim = true; }
        } else if (edit.consume) {
            if (edit.backspaces > target.size() && !isLetter) {
                tipNote(28, L"engine wants to rewrite text outside the composition; key passed through");
            } else {
                const size_t back = edit.backspaces < target.size() ? edit.backspaces : target.size();
                if (back != edit.backspaces) tipNote(29, L"engine asked to delete more than the composition holds; clamped");
                target.erase(target.size() - back);
                target += edit.text;
                claim = true;
            }
        } else if (isLetter) {
            const std::wstring raw = typedCharacter(key);
            if (!raw.empty()) { target += raw; claim = true; }
        } else if (composing && isWordBreakCharacter(key)) {
            // The key that ends the word is typed together with it, in one edit, so the application
            // cannot see the space before the word it follows (IMM-bridged controls do that).
            const std::wstring raw = typedCharacter(key);
            if (!raw.empty()) { target += raw; claim = true; pendingEnd_ = true; }
        }
        if (!claim) {
            endComposition();
            return false;
        }
        pendingKey_ = key;
        pendingText_ = std::move(target);
        pendingDirect_ = false;
        hasPending_ = true;
        tipNote(32, L"claimed a key for the composition");
        return true;
    }

    bool insertPlainCharacter(ITfContext* context, UINT vk) {
        const std::wstring text = typedCharacter(vk);
        if (text.empty()) return false;
        auto* insert = new (std::nothrow) VikeyInsertSession(context, text);
        if (!insert) return false;
        HRESULT sessionResult = E_FAIL;
        const HRESULT hr = context->RequestEditSession(clientId_, insert,
            TF_ES_SYNC | TF_ES_READWRITE, &sessionResult);
        const bool done = SUCCEEDED(hr) && SUCCEEDED(sessionResult) && insert->inserted();
        insert->Release();
        return done;
    }

    void dropComposition() {
        if (composition_) { composition_->Release(); composition_ = nullptr; }
        if (composeContext_) { composeContext_->Release(); composeContext_ = nullptr; }
        word_.clear();
    }

    // Commits the word being typed: the text stays, it just stops being a composition.
    void endComposition() {
        if (composition_ && composeContext_) {
            auto* session = new (std::nothrow) VikeyComposeSession(composeContext_, &composition_,
                static_cast<ITfCompositionSink*>(this), nullptr);
            if (session) {
                HRESULT sessionResult = E_FAIL;
                composeContext_->RequestEditSession(clientId_, session, TF_ES_SYNC | TF_ES_READWRITE, &sessionResult);
                session->Release();
            }
        }
        dropComposition();
    }

    bool contextAllowsEditing(ITfContext* context) {
        auto* probe = new (std::nothrow) VikeyContextProbe(context);
        if (!probe) return false;
        HRESULT sessionResult = E_FAIL;
        HRESULT hr = context->RequestEditSession(clientId_, probe,
            TF_ES_SYNC | TF_ES_READ, &sessionResult);
        const bool safe = SUCCEEDED(hr) && SUCCEEDED(sessionResult) && probe->safe();
        if (FAILED(hr) || FAILED(sessionResult)) {
            wchar_t note[160] = {};
            swprintf_s(note, _countof(note), L"probe: read edit session could not run (request hr=0x%08lX, session hr=0x%08lX)",
                static_cast<unsigned long>(hr), static_cast<unsigned long>(sessionResult));
            tipNote(22, note);
        }
        probe->Release();
        return safe;
    }

    void refreshRuntimeSettings() {
        vikey::tsf::refreshTsfEngineSettings();
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
    bool hasPending_ = false;
    bool pendingEnd_ = false;
    bool pendingDirect_ = true;
    vikey::tsf::EngineEdit pendingEdit_;
    UINT testedKey_ = 0;
    bool testedUnclaimed_ = false;
    bool composeProgram_ = false;
    UINT pendingKey_ = 0;
    std::wstring pendingText_;
    ITfComposition* composition_ = nullptr;
    ITfContext* composeContext_ = nullptr;   // holds a reference while a composition exists
    std::wstring word_;                      // the text of the composition, as the application shows it
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
    vikey::tsf::diagnostics::write(L"DllRegisterServer: registerComClass started");
    wchar_t module[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_module, module, MAX_PATH)) {
        const DWORD error = GetLastError();
        const HRESULT hr = HRESULT_FROM_WIN32(error);
        vikey::tsf::diagnostics::result(L"DllRegisterServer GetModuleFileNameW", hr, error);
        return hr;
    }
    wchar_t clsid[64] = {};
    if (StringFromGUID2(CLSID_VikeyTextService, clsid, 64) == 0) {
        vikey::tsf::diagnostics::write(L"DllRegisterServer StringFromGUID2 failed");
        return E_FAIL;
    }
    wchar_t keyPath[128] = {};
    if (swprintf_s(keyPath, _countof(keyPath), L"CLSID\\%s\\InprocServer32", clsid) < 0) {
        vikey::tsf::diagnostics::write(L"DllRegisterServer registry path formatting failed");
        return E_FAIL;
    }
    HKEY key = nullptr;
    LONG error = RegCreateKeyExW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes") + L"\\" + keyPath).c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS) {
        vikey::tsf::diagnostics::result(L"DllRegisterServer RegCreateKeyExW", HRESULT_FROM_WIN32(error), error);
        return HRESULT_FROM_WIN32(error);
    }
    error = RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(module),
        static_cast<DWORD>((wcslen(module) + 1) * sizeof(wchar_t)));
    if (error == ERROR_SUCCESS) {
        const wchar_t model[] = L"Apartment";
        error = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(model), sizeof(model));
    }
    RegCloseKey(key);
    const HRESULT hr = HRESULT_FROM_WIN32(error);
    vikey::tsf::diagnostics::result(L"DllRegisterServer registerComClass registry write", hr, error);
    return hr;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllRegisterServer() {
    vikey::tsf::diagnostics::write(L"DllRegisterServer entered");
    HRESULT hr = registerComClass();
    vikey::tsf::diagnostics::result(L"DllRegisterServer registerComClass", hr);
    if (FAILED(hr)) return hr;

    ITfInputProcessorProfiles* profiles = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&profiles));
    vikey::tsf::diagnostics::result(L"DllRegisterServer CoCreateInstance(InputProcessorProfiles)", hr);
    if (SUCCEEDED(hr)) {
        hr = profiles->Register(CLSID_VikeyTextService);
        vikey::tsf::diagnostics::result(L"DllRegisterServer ITfInputProcessorProfiles::Register", hr);
        if (SUCCEEDED(hr)) {
            BSTR description = SysAllocString(L"Vikey Vietnamese Input");
            if (!description) {
                hr = E_OUTOFMEMORY;
                vikey::tsf::diagnostics::result(L"DllRegisterServer SysAllocString", hr);
            } else {
                hr = profiles->AddLanguageProfile(CLSID_VikeyTextService, 0x042A,
                    GUID_VikeyProfile, description, static_cast<ULONG>(wcslen(description)),
                    nullptr, 0, 0);
                vikey::tsf::diagnostics::result(L"DllRegisterServer AddLanguageProfile(vi-VN)", hr);
                SysFreeString(description);
            }
        }
        profiles->Release();
    }

    if (SUCCEEDED(hr)) {
        ITfCategoryMgr* categories = nullptr;
        hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITfCategoryMgr, reinterpret_cast<void**>(&categories));
        vikey::tsf::diagnostics::result(L"DllRegisterServer CoCreateInstance(CategoryMgr)", hr);
        if (SUCCEEDED(hr)) {
            hr = categories->RegisterCategory(CLSID_VikeyTextService,
                GUID_TFCAT_TIP_KEYBOARD, GUID_VikeyProfile);
            vikey::tsf::diagnostics::result(L"DllRegisterServer RegisterCategory(keyboard)", hr);
            categories->Release();
        }
    }

    vikey::tsf::diagnostics::result(L"DllRegisterServer final result", hr);
    if (FAILED(hr)) {
        vikey::tsf::diagnostics::write(L"DllRegisterServer failed; removing partial CLSID registration");
        wchar_t clsid[64] = {};
        StringFromGUID2(CLSID_VikeyTextService, clsid, 64);
        wchar_t keyPath[128] = {};
        swprintf_s(keyPath, _countof(keyPath), L"CLSID\\%s", clsid);
        RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes") + L"\\" + keyPath).c_str());
    } else {
        vikey::tsf::diagnostics::write(L"DllRegisterServer completed successfully");
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
