#pragma once
namespace vikey { namespace tsf {
void loadTsfEngineSettings();
void refreshTsfEngineSettings();
bool isCurrentAppBlacklisted();
bool isCurrentInputLanguageSupported();
bool isSecureInputContext();
bool isControllerRunning();
}}
