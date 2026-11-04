// Vikey's windows (1:1 with the macOS app): main window, onboarding, About, Acknowledgements, typing log.
#pragma once
#include <windows.h>
namespace vikey { namespace ui {
void showSettings(HINSTANCE inst, bool firstRun);   // firstRun shows the onboarding instead
void showOnboarding(HINSTANCE inst);
void showAbout(HINSTANCE inst);
void showAcknowledgements(HINSTANCE inst);
void showTypingLog(HINSTANCE inst);
void showBlacklist(HINSTANCE inst);
void languageChanged(int lang);
}}
