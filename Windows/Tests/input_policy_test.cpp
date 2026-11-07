#include <cstdio>
#include <vector>
#include <string>
#include "../App/InputPolicy.h"

static int failures = 0;
static void expect(const char* name, bool got, bool want) {
    if (got != want) {
        std::printf("FAIL %s: got %s, expected %s\n", name, got ? "true" : "false", want ? "true" : "false");
        ++failures;
    }
}
static int hotkey(Uint16 key, bool ctrl=false, bool alt=false, bool win=false, bool shift=false) {
    return key | (ctrl ? 0x100 : 0) | (alt ? 0x200 : 0) | (win ? 0x400 : 0) | (shift ? 0x800 : 0);
}
int main() {
    using namespace vikey::input;
    const Uint16 z = KEY_Z;
    expect("alt_z_exact", matchesHotkey(hotkey(z, false, true), Alt, z), true);
    expect("alt_z_missing_alt", matchesHotkey(hotkey(z, false, true), 0, z), false);
    expect("alt_z_extra_shift", matchesHotkey(hotkey(z, false, true), Alt|Shift, z), false);
    expect("alt_z_wrong_key", matchesHotkey(hotkey(z, false, true), Alt, KEY_A), false);
    expect("ctrl_shift_combo", matchesHotkey(hotkey(KEY_F, true, false, false, true), Control|Shift, KEY_F), true);
    expect("ctrl_shift_missing_shift", matchesHotkey(hotkey(KEY_F, true, false, false, true), Control, KEY_F), false);
    expect("win_combo", matchesHotkey(hotkey(KEY_SPACE, false, false, true), Win, KEY_SPACE), true);
    expect("all_modifiers", matchesHotkey(hotkey(KEY_A, true, true, true, true), Control|Alt|Win|Shift, KEY_A), true);
    expect("caps_not_hotkey_modifier", matchesHotkey(hotkey(z, false, true), Alt|Caps, z), true);
    expect("modifier_only_ignores_key", matchesHotkey(hotkey(0xFE, false, true), Alt, KEY_A, false), true);
    expect("modifier_only_rejects_extra", matchesHotkey(hotkey(0xFE, false, true), Alt|Control, KEY_A, false), false);
    expect("empty_hotkey_disabled", matchesHotkey(EmptyHotkey, 0, KEY_A), false);
    expect("empty_hotkey_disabled_beep", matchesHotkey(EmptyHotkey|0x8000, 0, KEY_A), false);

    expect("normalize_full_path", normalizedExe(L"C:\\Program Files\\App\\VSCODE.EXE") == L"vscode.exe", true);
    expect("normalize_forward_slash", normalizedExe(L"/usr/bin/Terminal.EXE") == L"terminal.exe", true);
    expect("normalize_bare_name", normalizedExe(L"Notepad.EXE") == L"notepad.exe", true);
    const std::vector<std::wstring> entries = {L"CMD.EXE", L"C:\\Tools\\Code.EXE", L"pwsh.exe"};
    expect("blacklist_case_insensitive", isBlacklisted(L"cmd.exe", entries), true);
    expect("blacklist_path_matches_basename", isBlacklisted(L"C:\\Windows\\System32\\CMD.EXE", entries), true);
    expect("blacklist_entry_path_matches", isBlacklisted(L"code.exe", entries), true);
    expect("blacklist_non_member", isBlacklisted(L"notepad.exe", entries), false);
    expect("blacklist_empty_name", isBlacklisted(L"", entries), false);
    expect("blacklist_empty_list", isBlacklisted(L"cmd.exe", {}), false);

    // Typing-log security policy: disabled by setting, password/unknown focus, or sensitive app.
    expect("log_enabled_plain_app", allowsTypingLog(L"notepad.exe", true, false), true);
    expect("log_disabled_setting", allowsTypingLog(L"notepad.exe", false, false), false);
    expect("log_password_focus", allowsTypingLog(L"notepad.exe", true, true), false);
    expect("log_unknown_focus_fails_closed", allowsTypingLog(L"notepad.exe", true, true), false);
    expect("log_empty_app_fails_closed", allowsTypingLog(L"", true, false), false);
    const wchar_t* sensitive[] = {
        L"1password.exe", L"bitwarden.exe", L"keepassxc.exe", L"keepass.exe", L"lastpass.exe", L"dashlane.exe", L"enpass.exe", L"nordpass.exe",
        L"windowsterminal.exe", L"cmd.exe", L"powershell.exe", L"pwsh.exe", L"putty.exe", L"wezterm-gui.exe", L"alacritty.exe", L"conhost.exe", L"openconsole.exe", L"termius.exe"
    };
    for (const auto* app : sensitive) {
        std::wstring label = L"log_excluded_";
        label += app;
        std::string name(label.begin(), label.end());
        expect(name.c_str(), allowsTypingLog(app, true, false), false);
    }
    expect("log_excluded_app_path", allowsTypingLog(L"C:\\Windows\\System32\\CMD.EXE", true, false), false);
    expect("log_case_insensitive_exclusion", allowsTypingLog(L"PowerShell.EXE", true, false), false);
    std::printf("Windows input policy: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
