#pragma once
#include <windows.h>

struct VikeyTsfSharedState {
    volatile LONG secureContext;
    DWORD controllerProcessId;
};

static const wchar_t VIKEY_TSF_SHARED_STATE_NAME[] = L"Local\\VikeySecureInputState";
