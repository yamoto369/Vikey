// Windows application-side control plane. Text processing is performed only by the TSF TIP.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

namespace vikey { namespace core {
bool start();
void stop();
bool isRunning();
int language();
void setLanguageByUser(int lang);
void setLanguageQuiet(int lang);
void notifyLanguageFromTip(int lang);
void reloadSettings();
void onForegroundChanged();
void onSiteChanged(const std::wstring& host);
std::wstring frontAppName();
bool isFrontAppBlacklisted();
void addFrontAppToBlacklist();
void removeAppFromBlacklist(const std::wstring& exeName);
std::vector<std::wstring> blacklistedApps();
void setLanguageObserver(std::function<void(int)> observer);
void resetTyping();
std::wstring typingLogPath();
void flushTypingLog();
void clearTypingLog();
}} // namespace
