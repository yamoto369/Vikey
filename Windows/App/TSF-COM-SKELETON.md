# TSF COM skeleton

This phase introduces a loadable in-process COM DLL and a minimal text service implementation.

- CLSID: `{9B7A4D31-6D8F-4D22-A173-5E912C448B60}`
- Interfaces: `ITfTextInputProcessor`, `ITfKeyEventSink`, `IClassFactory`.
- Activation advises the thread's TSF keystroke sink; deactivation unadvises and releases interfaces.
- Key test/dispatch callbacks are intentionally pass-through (`eaten = FALSE`). No text is consumed, rewritten, logged or injected.
- The target does not yet register a TSF language profile/category. COM class registration and TSF profile registration will be added as a separate installer/registration step after lifecycle tests exist.

Do not enable this DLL in a user installation yet. It is a development skeleton, not a usable input method.
