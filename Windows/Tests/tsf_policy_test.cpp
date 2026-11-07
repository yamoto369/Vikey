#include <cstdio>
#include "../App/TSFPolicy.h"
static int failures = 0;
static void expect(const char* name, bool got, bool want) {
    if (got != want) { std::printf("FAIL %s: got %s expected %s\\n", name, got ? "true" : "false", want ? "true" : "false"); ++failures; }
}
int main() {
    using namespace vikey::tsf;
    expect("editable_enabled", shouldHandleKey(TextContextKind::Editable, true, false), true);
    expect("disabled", shouldHandleKey(TextContextKind::Editable, false, false), false);
    expect("other_ime_active", shouldHandleKey(TextContextKind::Editable, true, true), false);
    expect("unknown_context_fails_closed", shouldHandleKey(TextContextKind::Unknown, true, false), false);
    expect("non_text_context", shouldHandleKey(TextContextKind::NonText, true, false), false);
    expect("readonly_context", shouldHandleKey(TextContextKind::ReadOnly, true, false), false);
    expect("password_context", shouldHandleKey(TextContextKind::Password, true, false), false);
    expect("secure_context", shouldHandleKey(TextContextKind::Secure, true, false), false);
    expect("password_with_other_ime", shouldHandleKey(TextContextKind::Password, true, true), false);
    std::printf("TSF context policy: %s (%d failures)\\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
