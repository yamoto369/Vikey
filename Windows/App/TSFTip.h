#pragma once
#include <windows.h>
#include <msctf.h>

// Vikey's initial TSF COM class. The class is deliberately pass-through:
// it advises the TSF key sink but does not consume or edit text yet.
extern const CLSID CLSID_VikeyTextService;
