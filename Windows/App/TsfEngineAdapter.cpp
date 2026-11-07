#include "TsfEngineAdapter.h"
#include "KeyMap.h"
#include "Engine.h"

namespace vikey { namespace tsf {
namespace {
bool isLetter(Uint16 key) {
    static constexpr Uint16 letters[] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
        KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
        KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
    };
    for (Uint16 candidate : letters) if (candidate == key) return true;
    return false;
}
bool appendEngineCharacter(std::wstring& text, Uint32 value) {
    const Uint16 ch = (value & CHAR_CODE_MASK)
        ? static_cast<Uint16>(value & 0xFFFF)
        : keyCodeToCharacter(value);
    if (!ch) return false;
    text.push_back(static_cast<wchar_t>(ch));
    return true;
}
}
bool EngineAdapter::initialize() {
    ownerThread_ = GetCurrentThreadId();
    loadTsfEngineSettings();
    state_ = static_cast<vKeyHookState*>(vKeyInit());
    if (!state_) return false;
    *state_ = {};
    startNewSession();
    initialized_ = true;
    return true;
}
void EngineAdapter::reset() {
    if (!initialized_ || GetCurrentThreadId() != ownerThread_) return;
    startNewSession();
}
EngineEdit EngineAdapter::processKey(UINT virtualKey, bool shift, bool capsLock,
                                     bool controlOrAltOrWin) {
    EngineEdit result;
    if (!initialized_ || !state_ || GetCurrentThreadId() != ownerThread_) return result;
    if (vLanguage == 0) return result;
    const Uint16 key = engineKey(virtualKey);
    if (key == KEY_EMPTY) return result;
    vAutoSwitchRequest = -1;
    const Uint8 capsStatus = shift ? 1 : (capsLock ? 2 : 0);
    vKeyHandleEvent(vKeyEvent::Keyboard, vKeyEventState::KeyDown, key,
                    capsStatus, controlOrAltOrWin);
    if (state_->code == vDoNothing) return result;
    result.consume = true;
    result.backspaces = state_->backspaceCount;
    for (int i = static_cast<int>(state_->newCharCount) - 1; i >= 0; --i) {
        if (!appendEngineCharacter(result.text, state_->charData[i])) {
            result = {};
            startNewSession();
            return result;
        }
    }
    if (state_->code == vRestore || state_->code == vRestoreAndStartNewSession) {
        const bool uppercase = shift || (capsLock && isLetter(key));
        const Uint16 ch = keyCodeToCharacter(key | (uppercase ? CAPS_MASK : 0));
        if (!ch) {
            result = {};
            startNewSession();
            return result;
        }
        result.text.push_back(static_cast<wchar_t>(ch));
    }
    if (state_->code == vRestoreAndStartNewSession) {
        startNewSession();
        result.resetSession = true;
    }
    return result;
}
}} // namespace vikey::tsf
