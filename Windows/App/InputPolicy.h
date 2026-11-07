// Pure Windows input policy helpers shared by Core.cpp and deterministic tests.
#pragma once
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>
#include "Engine.h"

namespace vikey { namespace input {
enum ModifierFlag : unsigned { Shift = 1, Control = 2, Alt = 4, Win = 8, Caps = 16 };
static const int EmptyHotkey = 0xFE0000FE;

inline bool matchesHotkey(int hotkey, unsigned activeModifiers, Uint16 key, bool checkKeyCode = true) {
    if ((hotkey & (~0x8000)) == EmptyHotkey) return false;
    if (((hotkey & 0x100) != 0) != ((activeModifiers & Control) != 0)) return false;
    if (((hotkey & 0x200) != 0) != ((activeModifiers & Alt) != 0)) return false;
    if (((hotkey & 0x400) != 0) != ((activeModifiers & Win) != 0)) return false;
    if (((hotkey & 0x800) != 0) != ((activeModifiers & Shift) != 0)) return false;
    if (checkKeyCode && (hotkey & 0xFF) != key) return false;
    return true;
}

inline std::wstring normalizedExe(std::wstring name) {
    const size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return name;
}

inline bool isBlacklisted(const std::wstring& exeName, const std::vector<std::wstring>& entries) {
    const std::wstring target = normalizedExe(exeName);
    if (target.empty()) return false;
    for (const auto& entry : entries) if (normalizedExe(entry) == target) return true;
    return false;
}

// Typing-log policy: password/unknown focus and sensitive applications are never logged.
inline bool allowsTypingLog(const std::wstring& appName, bool loggingEnabled, bool passwordOrUnknownFocus) {
    if (!loggingEnabled || passwordOrUnknownFocus) return false;
    static const wchar_t* excluded[] = {
        L"1password.exe", L"bitwarden.exe", L"keepassxc.exe", L"keepass.exe", L"lastpass.exe", L"dashlane.exe", L"enpass.exe", L"nordpass.exe",
        L"windowsterminal.exe", L"cmd.exe", L"powershell.exe", L"pwsh.exe", L"putty.exe", L"wezterm-gui.exe", L"alacritty.exe", L"conhost.exe", L"openconsole.exe", L"termius.exe"
    };
    const std::wstring app = normalizedExe(appName);
    if (app.empty()) return false;
    for (const auto* item : excluded) if (app == item) return false;
    return true;
}
}} // namespace vikey::input
