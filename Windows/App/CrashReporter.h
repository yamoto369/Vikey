// Local crash diagnostics. Reports are written to a Crashes folder beside Vikey.exe and never uploaded.
#pragma once
#include <windows.h>
namespace vikey { namespace crash {
void install();
void selfTestIfAsked();
}}
