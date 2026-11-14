#pragma once
#include "Components/PersistentIdComponent.h"
#include <string>
#include <string_view>
#include <optional>

namespace NexusEngine {
    /** 永続IDは32桁hex。実行中Handleやポインタを保存しない。 */
    std::string FormatEntityReference(PersistentId id);
    std::optional<PersistentId> ParseEntityReference(std::string_view value);
    /** 空文字はNone。Componentは32桁hex、'|'、安定Type IDで表す。 */
    bool ValidateReferenceValue(std::string_view value, bool component);
}
