// Pure TSF context eligibility policy. Unknown and secure contexts fail closed.
#pragma once
namespace vikey { namespace tsf {
enum class TextContextKind { Unknown, NonText, ReadOnly, Editable, Password, Secure };
inline bool shouldHandleKey(TextContextKind context, bool vikeyEnabled, bool otherImeActive) {
    return vikeyEnabled && !otherImeActive && context == TextContextKind::Editable;
}
}} // namespace vikey::tsf
