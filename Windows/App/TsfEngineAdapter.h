#pragma once
#include <windows.h>
#include <string>
#include "Engine.h"

namespace vikey { namespace tsf {
struct EngineEdit {
    bool consume = false;
    UINT backspaces = 0;
    std::wstring text;
    bool resetSession = false;
};
class EngineAdapter final {
public:
    EngineAdapter() = default;
    bool initialize();
    void reset();
    EngineEdit processKey(UINT virtualKey, bool shift, bool capsLock,
                          bool controlOrAltOrWin);
private:
    DWORD ownerThread_ = 0;
    vKeyHookState* state_ = nullptr;
    bool initialized_ = false;
};
}} // namespace vikey::tsf
