// Deterministic unit tests for the Windows-to-engine key adapter.
// This test deliberately has no hooks, desktop, foreground-window or TSF dependency.
#include <cstdio>
#include "../App/KeyMap.h"

static int failures = 0;
static void expect(const char* name, Uint16 got, Uint16 want) {
    if (got != want) {
        std::printf("FAIL %s: got %u, expected %u\n", name, (unsigned)got, (unsigned)want);
        ++failures;
    }
}
static void expectBool(const char* name, bool got, bool want) {
    if (got != want) {
        std::printf("FAIL %s: got %s, expected %s\n", name, got ? "true" : "false", want ? "true" : "false");
        ++failures;
    }
}

int main() {
    // Alphabet: Windows VK letters intentionally map directly to the engine's letter identities.
    const Uint16 letters[] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,
        KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
    for (unsigned i = 0; i < 26; ++i) {
        char name[32]; std::snprintf(name, sizeof(name), "letter_%c", 'A' + i);
        expect(name, engineKey('A' + i), letters[i]);
    }

    const Uint16 digits[] = {KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9};
    for (unsigned i = 0; i < 10; ++i) {
        char name[32]; std::snprintf(name, sizeof(name), "digit_%u", i);
        expect(name, engineKey('0' + i), digits[i]);
        std::snprintf(name, sizeof(name), "numpad_digit_%u", i);
        expect(name, engineKey(VK_NUMPAD0 + i), digits[i]);
    }

    expect("space", engineKey(VK_SPACE), KEY_SPACE);
    expect("return", engineKey(VK_RETURN), KEY_RETURN);
    expect("backspace", engineKey(VK_BACK), KEY_DELETE);
    expect("tab", engineKey(VK_TAB), KEY_TAB);
    expect("escape", engineKey(VK_ESCAPE), KEY_ESC);
    expect("left", engineKey(VK_LEFT), KEY_LEFT);
    expect("right", engineKey(VK_RIGHT), KEY_RIGHT);
    expect("up", engineKey(VK_UP), KEY_UP);
    expect("down", engineKey(VK_DOWN), KEY_DOWN);

    const DWORD oem[] = {VK_OEM_1,VK_OEM_2,VK_OEM_3,VK_OEM_4,VK_OEM_5,VK_OEM_6,VK_OEM_7,
        VK_OEM_COMMA,VK_OEM_PERIOD,VK_OEM_MINUS,VK_OEM_PLUS};
    const Uint16 mapped[] = {KEY_SEMICOLON,KEY_SLASH,KEY_BACKQUOTE,KEY_LEFT_BRACKET,KEY_BACK_SLASH,
        KEY_RIGHT_BRACKET,KEY_QUOTE,KEY_COMMA,KEY_DOT,KEY_MINUS,KEY_EQUALS};
    for (unsigned i = 0; i < sizeof(oem)/sizeof(oem[0]); ++i) {
        char name[32]; std::snprintf(name, sizeof(name), "oem_%u", i);
        expect(name, engineKey(oem[i]), mapped[i]);
    }

    const DWORD navigation[] = {VK_INSERT,VK_HOME,VK_END,VK_DELETE,VK_PRIOR,VK_NEXT,VK_SNAPSHOT,VK_PRINT,
        VK_SELECT,VK_HELP,VK_EXECUTE,VK_NUMLOCK,VK_SCROLL};
    for (unsigned i = 0; i < sizeof(navigation)/sizeof(navigation[0]); ++i) {
        char name[32]; std::snprintf(name, sizeof(name), "navigation_break_%u", i);
        expect(name, engineKey(navigation[i]), KEY_ESC);
    }

    const DWORD unmodeled[] = {VK_DECIMAL,VK_ADD,VK_SUBTRACT,VK_MULTIPLY,VK_DIVIDE,VK_SEPARATOR,VK_OEM_102,VK_OEM_8,VK_CLEAR};
    for (unsigned i = 0; i < sizeof(unmodeled)/sizeof(unmodeled[0]); ++i) {
        char name[32]; std::snprintf(name, sizeof(name), "unmodeled_break_%u", i);
        expectBool(name, unmodeledKeyEndsWord(unmodeled[i]), true);
        expect(name, engineKey(unmodeled[i]), KEY_EMPTY);
    }
    expectBool("ordinary_letter_does_not_force_break", unmodeledKeyEndsWord('A'), false);
    expectBool("unknown_key_does_not_force_break", unmodeledKeyEndsWord(0xFF), false);
    expect("unknown_key", engineKey(0xFF), KEY_EMPTY);

    std::printf("Windows key adapter: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
