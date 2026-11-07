#include <cstdio>
#include "../App/TSFPolicy.h"
static int failures = 0;
static void expect(const char* name, bool got, bool want) {
    if (got != want) {
        std::printf("FAIL %s: got %s expected %s\n", name,
            got ? "true" : "false", want ? "true" : "false");
        ++failures;
    }
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

    expect("default_scope_allowed", isAllowedInputScope(IS_DEFAULT), true);
    expect("text_scope_allowed", isAllowedInputScope(IS_TEXT), true);
    expect("search_scope_allowed", isAllowedInputScope(IS_SEARCH), true);
    expect("password_scope_blocked", isAllowedInputScope(IS_PASSWORD), false);
    expect("private_scope_blocked", isAllowedInputScope(IS_PRIVATE), false);
    expect("numeric_password_blocked", isAllowedInputScope(IS_NUMERIC_PASSWORD), false);
    expect("numeric_pin_blocked", isAllowedInputScope(IS_NUMERIC_PIN), false);
    expect("alphanumeric_pin_blocked", isAllowedInputScope(IS_ALPHANUMERIC_PIN), false);
    expect("lockscreen_pin_blocked", isAllowedInputScope(IS_ALPHANUMERIC_PIN_SET), false);
    expect("login_scope_blocked", isAllowedInputScope(IS_LOGINNAME), false);
    std::printf("TSF context policy: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
