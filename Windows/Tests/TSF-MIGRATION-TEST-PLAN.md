# Windows TSF Migration — Golden Master and Test Contract

## Purpose

This document defines the pre-migration behavior contract. It is written against the current Windows hook implementation on the source `main` branch. The current behavior is the reference; TSF must reproduce it unless a difference is explicitly documented and approved.

Do not regenerate or overwrite existing engine golden files during the migration simply to make a failing test pass.

## Existing executable baseline (must remain unchanged)

Run on a Windows machine with the repository's supported MSVC/CMake/Ninja/Python toolchain:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File Windows/check.ps1
```

This currently exercises:
- Default Telex rule cases.
- Simple Telex style 1 and 2.
- Previously reported engine regressions.
- Golden snapshots, Telex syllables and typing metrics.
- Driver/simulator output parity.
- Undo regression tests.
- Realistic typing simulation across three seeds plus synthetic Vietnamese.
- Fat-finger and auto-correct quality gates.
- Engine performance budget.

The baseline is authoritative only if it passes on the unmodified source revision. Record the exact source SHA and output before changing the Windows input layer.

## Existing Windows live golden master

Run with the current Vikey Windows app running:

```powershell
Windows/build/App/vikey_livetest.exe Windows/Tests/cases.tsv
```

The TSV format is:

`case-name<TAB>start-language(vi|en)<TAB>key-sequence<TAB>expected-text`

The current checked-in cases cover:
- Telex basics, tone placement and modern orthography.
- Invalid-word restoration.
- Vietnamese-to-English and English-to-Vietnamese auto-switch.
- Double-key correction.
- Undo by Backspace and continuing after undo.
- Programming identifier guard.
- Manual hotkey.
- Digit and numpad behavior.
- Look-forward conversion.
- Capitalization, acronym and Caps Lock behavior.
- Navigation keys that terminate the current word.

These cases are the first golden master for text output. Keep them byte-for-byte stable unless a verified baseline defect is found and approved.

## Required detailed unit-test suites

The TSF work must add deterministic tests for the following seams. Prefer pure functions and injected interfaces; tests must not require a foreground desktop unless explicitly categorized as live tests.

### A. Windows key-event adapter

| Area | Cases |
|---|---|
| Key identity | A–Z, 0–9, numpad 0–9, punctuation, OEM keys, navigation, function and unknown keys |
| State | key-down, key-up, repeat, injected/virtual events, modifier-only events |
| Modifiers | Shift, Caps Lock, Ctrl, Alt, Win, combinations and modifier release ordering |
| Layout | US, Vietnamese, Dvorak, layout change mid-word, scan code vs virtual key |
| Special input | dead keys, AltGr, Unicode input, surrogate pairs, IME-owned keys |
| Boundaries | word break, cursor movement, selection change, focus/context change |

### A2. TSF context eligibility policy (implemented as pure tests)\n\n- Vikey disabled: pass keys through.\n- Unknown, non-text, read-only, password and secure contexts: pass keys through.\n- Another IME active: do not intercept.\n- Only an enabled editable text context with no competing IME may be handled.\n\n### B. TSF COM lifecycle

- COM class factory: supported IID, unsupported IID, reference counting and aggregation policy.
- TIP activation: valid client ID, repeated activation, activation failure and partial initialization cleanup.
- Keystroke sink: advise, duplicate advise, unadvise, stale sink and deactivation while a key is pending.
- Shutdown: repeated shutdown, context destruction, document manager change and thread exit.
- Reentrancy: callbacks during edit-session completion and nested TSF notifications.
- Registration: register/unregister, per-user install, repair and rollback.

### C. Edit-session and text-range behavior

- Read-only and read/write edit-session grants and denials.
- Synchronous and asynchronous session requests.
- Stale/invalid ranges and context destruction before callback.
- Empty selection, non-empty selection, replacement, insertion and deletion.
- Unicode combining marks, Vietnamese precomposed characters, supplementary characters and surrogate pairs.
- Selection/caret preservation after conversion.
- Backspace undo, undo after auto-switch, and undo after context switch.
- Application-specific failure to grant an edit session must fail open: preserve the user's original key and never corrupt text.

### D. Typing state and parity

- Fresh context starts with no stale partial word.
- Switching focus/document/context resets partial-word and undo state.
- Manual language switch and auto-switch produce the same output as the baseline.
- Per-app/per-site preference lookup and fallback defaults.
- Blacklisted app bypasses conversion.
- Other active IME/input language is not intercepted.
- Repeated key and fast typing do not lose, duplicate or reorder characters.

### E. Security and privacy

- Password and secure-text controls: no conversion, no mutation, no key/text logging.
- Password managers and terminal apps: no key/text logging.
- Secure desktop/UAC and elevated process boundary: no crash or unintended input capture.
- Logging disabled by default; enabling the existing typing log retains its exclusions.
- No network request, telemetry or new background text collection introduced by the TIP.

### F. Live compatibility matrix

Run separately from unit tests and record application/version/build:

- Notepad (Win32 Edit control), WordPad if available, Microsoft Word.
- Windows Terminal, classic console, PowerShell and VS Code.
- Chrome, Edge and Firefox: ordinary text fields, contenteditable, address bar and password fields.
- Settings and other modern WinUI/UWP text fields.
- Japanese/Chinese IME active, Windows touch keyboard, remote desktop.
- x64 and ARM64 Windows, including x64 emulated apps on ARM64 where applicable.

For each app record: TIP activated, Telex, auto-switch, undo, caret/selection, password bypass, app switch, latency, crashes and known limitations.

## Golden master acceptance rules

1. The existing engine golden files and current `Windows/Tests/cases.tsv` outputs are immutable during initial TSF implementation.
2. New expected outputs must first be run against the current hook build and reviewed; never infer expected output from the intended TSF behavior.
3. Compare exact Unicode text, not normalized ASCII or stripped punctuation.
4. A failed edit session must preserve the original user input rather than silently dropping it.
5. Any unavoidable TSF-vs-hook behavior difference must have a dedicated test, a documented reason and explicit approval before changing the golden.
6. Test reports must include architecture, Windows build, application/version, pass/fail/skip counts and the exact commit SHA.

## Baseline status at branch creation

- Source baseline: `main` at branch creation (see branch history).
- Existing engine and Windows live suites are present in the repository.
- Tests have not yet been executed in a Windows desktop environment in this work session. No pass status is claimed.
- The current live test is a hook-oriented harness; TSF-specific COM/edit-session tests still need to be implemented before the TSF adapter is considered verified.
