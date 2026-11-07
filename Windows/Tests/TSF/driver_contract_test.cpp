#include "Engine.h"
#include "Driver.h"
#include "KeyMap.h"
#include <iostream>
#include <string>

int vLanguage = 1, vSwitchKeyStatus = 0x7A000206, vCheckSpelling = 1, vUseModernOrthography = 1;
int vRestoreIfWrongSpelling = 1, vFixRecommendBrowser = 1, vUseSmartSwitchKey = 1, vOtherLanguage = 1;
int vAutoSwitchByTyping = 1, vAutoSwitchModeByTyping = 1, vPerformLayoutCompat = 0;

static bool applyEdit(std::u16string& text, const VKEdit& edit) {
    if (edit.deleteCount < 0 || (size_t)edit.deleteCount > text.size()) return false;
    if (edit.deleteCount) text.erase(text.size() - edit.deleteCount);
    text += edit.text;
    return true;
}
int main() {
    vLanguage = 1;
    vCheckSpelling = 1; vSetCheckSpelling();
    vRestoreIfWrongSpelling = 1; vUseModernOrthography = 1;
    vTelexStyle = 0; vAutoSwitchByTyping = 1;
    VikeyDriver driver;
    driver.setLanguage(1);
    std::u16string text;
    const DWORD keys[] = {'V', 'I', 'E', 'T', 'S'};
    for (DWORD vk : keys) {
        if (!applyEdit(text, driver.press(vikey::engineKey(vk), false))) {
            std::cerr << "Invalid edit range from VikeyDriver\n";
            return 2;
        }
    }
    if (text != u"viét") {
        std::cerr << "Expected vi\u00E9t; got UTF-16 length " << text.size() << "\n";
        return 1;
    }
    std::cout << "PASS: v-i-e-t-s => vi\u00E9t\n";
    return 0;
}
