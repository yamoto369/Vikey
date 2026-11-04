// The keyboard core on Windows: the low-level keyboard/mouse hooks that feed the Telex engine,
// smart switch per app/website, auto switch by typing, and the typing log. A port of
// App/Core/VikeyCore.mm (macOS) with the same behavior, decision for decision.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

namespace vikey { namespace core {

bool start();                       // install hooks (needs a message loop on the calling thread)
void stop();
bool isRunning();
int language();                     // 0 English, 1 Vietnamese
void setLanguageByUser(int lang);   // from the tray menu / settings window
void setLanguageQuiet(int lang);    // tests: set the language without the 5 s pause or a log entry
void reloadSettings();              // after a setting changed in the registry
void onForegroundChanged();         // called by the foreground WinEvent hook
void onSiteChanged(const std::wstring& host);   // per-site memory (Phase 3)
std::wstring frontAppName();
bool isFrontAppBlacklisted();
void addFrontAppToBlacklist();
void removeAppFromBlacklist(const std::wstring& exeName);
std::vector<std::wstring> blacklistedApps();

/// Called on the UI thread whenever the language changes (menu icon, settings window).
void setLanguageObserver(std::function<void(int)> f);

/// Forget the word being typed (the cursor moved to another field without a click).
void resetTyping();

// Typing log
std::wstring typingLogPath();
void flushTypingLog();
void clearTypingLog();

}} // namespace
