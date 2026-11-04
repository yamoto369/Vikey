// Settings in HKCU\Software\Vikey, same key names as the macOS app's UserDefaults.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace vikey { namespace prefs {

int getInt(const wchar_t* key, int fallback);
bool has(const wchar_t* key);
void setInt(const wchar_t* key, int value);
std::vector<BYTE> getBinary(const wchar_t* key);
void setBinary(const wchar_t* key, const BYTE* data, DWORD size);
std::wstring getString(const wchar_t* key, const std::wstring& fallback);
void setString(const wchar_t* key, const std::wstring& value);
std::vector<std::wstring> getStringList(const wchar_t* key);
void setStringList(const wchar_t* key, const std::vector<std::wstring>& values);
void remove(const wchar_t* key);

/// %LOCALAPPDATA%\Vikey, created if needed.
std::wstring appDataDir();

/// Run at login via HKCU\...\Run.
bool launchAtLogin();
void setLaunchAtLogin(bool on);

/// Settings from an OpenKey install (HKCU\SOFTWARE\TuyenMai\OpenKey), copied once.
void migrateFromOpenKey();

}} // namespace
