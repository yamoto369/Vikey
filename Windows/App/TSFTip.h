#pragma once
#include <windows.h>
#include <msctf.h>

// Vikey in-process TSF text service. All document changes are performed
// under TSF edit sessions; unsupported and sensitive contexts remain pass-through.
extern const CLSID CLSID_VikeyTextService;
