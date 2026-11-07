#pragma once
#include <windows.h>
#include <msctf.h>
#include <atomic>
#include <string>
#include "../../Engine/Driver.h"

class VikeyTextService final : public ITfTextInputProcessorEx, public ITfKeyEventSink {
public:
    VikeyTextService();
    ~VikeyTextService();
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr* manager, TfClientId client) override;
    HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr* manager, TfClientId client, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Deactivate() override;
    HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL foreground) override;
    HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* context, WPARAM vk, LPARAM flags, BOOL* eaten) override;
    HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* context, WPARAM vk, LPARAM flags, BOOL* eaten) override;
    HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext* context, WPARAM vk, LPARAM flags, BOOL* eaten) override;
    HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext* context, WPARAM vk, LPARAM flags, BOOL* eaten) override;
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext* context, REFGUID key, BOOL* eaten) override;
private:
    std::atomic<ULONG> refs_{1};
    ITfThreadMgr* threadMgr_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    bool active_ = false;
    bool settingsLoaded_ = false;
    int lastLanguageRevision_ = -1;
    int lastTypingRevision_ = -1;
    std::string contextKey_;
    std::wstring appName_;
    VikeyDriver driver_;

    HRESULT ProcessKey(ITfContext* context, WPARAM vk, LPARAM flags, BOOL* eaten);
    void SyncSettingsAndContext(ITfContext* context);
    bool IsKeyboardDisabled(ITfContext* context) const;
    bool IsBlacklistedContext(ITfContext* context) const;
    bool MatchesSwitchHotkey(WPARAM vk) const;
    void PersistLanguage(bool notifyTray);
};
HRESULT CreateVikeyTextService(REFIID iid, void** out);
void VikeyTsfObjectCreated();
void VikeyTsfObjectReleased();
void VikeyTsfServerLocked(bool locked);
void VikeyTsfLog(const char* fileName, const char* eventName, HRESULT hr = S_OK,
                 unsigned long value1 = 0, unsigned long value2 = 0);
