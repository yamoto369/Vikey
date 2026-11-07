# Windows TSF implementation

This branch replaces the Windows low-level keyboard interception path with a Text Services Framework (TSF) keyboard TIP.

## Architecture
- `Vikey.exe` is only the tray/settings control process.
- `VikeyTsf-x64.dll` / `VikeyTsf-arm64.dll` implement the in-process TSF COM text service.
- `ITfKeyEventSink` receives key events; the shared `VikeyDriver` calculates edits.
- All edits are applied through `ITfContext::RequestEditSession` and `ITfRange::SetText`.
- There is no `WH_KEYBOARD_LL`, `WH_MOUSE_LL`, `SendInput`, or hook fallback in the shipping Windows keyboard path.

## Privacy and configuration
The TIP validates the focused document/context, respects the keyboard-disabled compartment and per-app blacklist settings, and reads the predefined `GUID_PROP_INPUTSCOPE` property. Windowless ACP text stores remain valid focused input targets. Password, private, numeric-password, and PIN scopes pass through unchanged. Per-app and per-site mode preferences are stored in the existing per-user registry settings.

## CI tests
- Deterministic driver contract test, without window/focus dependency: `v-i-e-t-s` -> `viét`.
- A best-effort RichEdit + SendInput smoke test exercises the registered TIP on a desktop runner; it is skipped if foreground desktop access is unavailable.
- GitHub Actions archives TSF diagnostic logs even when tests fail.

A memory-backed `ITextStoreACP` host creates the TIP COM object, explicitly calls `ActivateEx` on its own `ITfThreadMgr`, dispatches `ITfKeystrokeMgr::TestKeyDown/KeyDown`, and verifies the resulting ACP contents. The test skips desktop-wide profile activation so it is isolated from the hosted runner's global input profile.
