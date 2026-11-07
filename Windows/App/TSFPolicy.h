// Pure TSF context and input-scope eligibility policy. Unknown and sensitive
// contexts fail closed; the TIP must still verify the live context under an edit lock.
#pragma once
#include <inputscope.h>

namespace vikey { namespace tsf {
enum class TextContextKind { Unknown, NonText, ReadOnly, Editable, Password, Secure };

inline bool shouldHandleKey(TextContextKind context, bool vikeyEnabled, bool otherImeActive) {
    return vikeyEnabled && !otherImeActive && context == TextContextKind::Editable;
}

inline bool isAllowedInputScope(InputScope scope) {
    switch (scope) {
    case IS_DEFAULT:
    case IS_TEXT:
    case IS_CHAT:
    case IS_CHAT_WITHOUT_EMOJI:
    case IS_SEARCH:
    case IS_SEARCH_INCREMENTAL:
        return true;
    default:
        // Includes password, numeric/alphanumeric PIN, private, login, and
        // every scope that this version has not explicitly reviewed.
        return false;
    }
}
}} // namespace vikey::tsf
