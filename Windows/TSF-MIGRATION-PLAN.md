# Windows TSF Migration Plan

## Scope and invariants

This branch migrates the Windows input integration from the current global low-level keyboard/mouse hooks (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`) to a Windows Text Services Framework (TSF) Text Input Processor (TIP).

Repository boundary: `yamoto369/Vikey` only. The source branch is `main`; this work must not modify `main` or the upstream repository.

The shared typing engine under `Engine/` is the behavioral authority. The migration must not rewrite Telex, spelling, smart-switch, undo, or auto-switch rules merely to accommodate TSF.

Non-goals for the first migration:
- Rewriting the shared engine in Rust.
- Changing typing rules or accepting new golden outputs to make tests pass.
- Removing the tray/settings application.
- Claiming compatibility with every application before compatibility has been measured.

## Current Windows architecture

- `Windows/App/main.cpp`: tray process, settings UI, message loop and lifecycle.
- `Windows/App/Core.cpp`: global keyboard/mouse hooks, key state, engine event dispatch, output reinjection, app/site switching and typing log.
- `Windows/App/KeyMap.h`: Windows virtual-key to engine-key mapping.
- `Windows/App/Site.cpp`: UI Automation for browser-site memory and password-field detection.
- `Windows/App/Prefs.cpp`: HKCU settings and migration from OpenKey.
- `Windows/Tests/livetest.cpp` + `Windows/Tests/cases.tsv`: real Windows input/output regression test.
- `Tools/autoswitch/rules/golden.py`, rule cases and simulators: shared-engine golden/regression suite.

The current hook path observes keys globally, translates them to engine events, and posts corrected output back into the focused application. TSF changes the integration contract: the TIP receives key events in a TSF context and must make edits through TSF edit sessions.

## Target architecture

1. **Vikey tray/controller process**
   - Keeps settings UI, tray icon, startup behavior and user preferences.
   - Owns TIP registration/unregistration and communicates settings to the TIP through a documented, versioned IPC/configuration boundary.
   - Does not inject keystrokes into applications.

2. **Vikey TSF TIP (in-process COM DLL)**
   - Implements the TSF text-input-processor and keystroke-sink interfaces.
   - Receives key events only when activated by TSF in a supported text context.
   - Requests read/write edit sessions and uses TSF ranges/selections for replacement, undo and composition.
   - Keeps per-context typing state isolated; resets it on focus/context/document changes.
   - Avoids blocking TSF callbacks on UI Automation, disk I/O, network I/O or cross-process waits.

3. **Shared engine adapter**
   - Maps TSF key/modifier/layout data to existing engine events.
   - Converts engine output into a minimal edit operation (replace range / insert text / preserve original key).
   - Does not duplicate typing rules.
   - Handles Unicode, surrogate pairs, dead keys, IME coexistence and keyboard layouts explicitly.

4. **Compatibility boundary**
   - TSF-native applications are the primary target.
   - Legacy/IMM32-only applications must be detected and documented. A separate, explicit fallback may be considered after the TSF path is stable; global hooks must not silently remain as the permanent default.
   - Password/secure contexts must never be modified or logged.
   - Other IMEs and input languages must retain control when they are active.

## Implementation phases

### Phase 0 — Freeze current behavior (before implementation)
- Preserve current `Tools/autoswitch` goldens and all Windows live cases as the baseline.
- Run `pwsh -NoProfile -ExecutionPolicy Bypass -File Windows/check.ps1` on Windows x64.
- Run the live test with Vikey running: `Windows/build/App/vikey_livetest.exe Windows/Tests/cases.tsv`.
- Record commit SHA, toolchain, OS build, architecture, pass/fail counts and any environment-dependent skips.
- Add test cases only when expected output is verified against the current `main` build.
- Do not update golden snapshots during migration unless a behavior change is explicitly approved.

### Phase 1 — Characterize the current Windows integration
- Add unit tests for key mapping, modifier normalization, key classification and state reset boundaries.
- Add deterministic tests for app blacklist normalization, settings defaults/migration and safe-context policy where seams can be isolated.
- Expand live tests for cursor movement, selection, navigation keys, numpad, Caps Lock, dead keys, layout changes, hotkey, app switch, password fields and concurrent focus changes.
- Keep integration-only behaviors clearly separated from engine goldens.

### Phase 2 — Introduce TSF skeleton
- Add a dedicated TSF DLL target and COM class factory, registration, activation/deactivation and clean shutdown.
- Implement and test the TSF interfaces and keystroke-sink advising/unadvising lifecycle.
- Verify registration and activation independently of text conversion.
- Build architecture-matched DLLs for x64 and ARM64; test each on a matching Windows runtime.

### Phase 3 — Port typing behavior
- Introduce a pure adapter from TSF key events to the existing engine event API.
- Use TSF edit sessions for all text reads/writes. Never call `SendInput` or depend on low-level global hooks in the TSF path.
- Port word boundaries, Telex, spelling, auto-switch, manual hotkey, undo, cursor/selection handling and per-context state incrementally.
- Preserve settings and existing registry values; add versioned TIP settings only where required.

### Phase 4 — Context, security and coexistence
- Validate password/secure edit controls, elevated apps, UAC/desktop boundaries, RDP, browser address bars and browser content.
- Validate coexistence with Microsoft IMEs, Japanese/Chinese IMEs, keyboard layout switching, dead keys, touch keyboard and accessibility tools.
- Ensure no typing log is recorded for password managers, terminals or secure fields; do not introduce text logging in the TIP.
- Ensure slow UI Automation or controller shutdown cannot block text input.

### Phase 5 — Release validation
- Run engine goldens, Windows unit tests, TSF component tests and live application matrix.
- Test x64 and ARM64 build, install, registration, upgrade, rollback and uninstall.
- Measure per-keystroke latency and memory in representative applications.
- Keep the old hook implementation isolated until parity is demonstrated; remove it only after explicit compatibility and fallback decisions.
- Produce a compatibility report and known-limitations list before proposing a PR.

## Test gates

A phase is not complete merely because it compiles.

- **Engine gate:** all existing golden/rule/simulator tests pass unchanged.
- **Adapter gate:** deterministic unit tests pass, including key-up/down, modifiers, repeats, dead keys, layout and Unicode boundaries.
- **TSF lifecycle gate:** activate/deactivate/re-activate, sink advise/unadvise, shutdown and COM registration tests pass without leaks or stale sinks.
- **Edit-session gate:** read/write lock failures, asynchronous requests, stale ranges, empty selection, replacement, selection preservation and undo are covered.
- **Live gate:** current Windows cases plus the TSF matrix pass in real TSF applications.
- **Security gate:** password/secure fields remain untouched and unlogged; other IMEs are not intercepted.
- **Architecture gate:** x64 and ARM64 artifacts register and load in matching processes.

## Risks and decisions to resolve with evidence

- TSF support is application-dependent; some legacy apps may not activate a TIP.
- TSF key sinks and edit sessions have strict thread/lock/reentrancy rules.
- A global low-level hook can observe focus and clicks that a TIP cannot observe in the same way. State reset and per-app memory must be redesigned around TSF context/document lifecycle.
- Browser site detection and password detection currently rely on UI Automation. These must remain off the keystroke/edit-session critical path and be reviewed for security and reliability.
- Undo semantics may differ across applications because TSF text stores expose different edit behavior.
- ARM64 and x64 registration/COM activation require separate validation.

## Completion criteria

The migration is ready for review only when the existing engine goldens are unchanged, the Windows baseline remains green, TSF unit/integration tests pass on x64 and ARM64, the supported-app matrix is documented, and any unsupported legacy applications have an explicit compatibility decision.
