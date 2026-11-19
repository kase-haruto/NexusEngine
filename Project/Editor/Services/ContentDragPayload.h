#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <span>
#include <vector>

namespace NexusEngine {
// ImGuiへpointerを渡さず、version付きUTF-8値だけを渡す。別Browserでも同じ契約を使う。
class ContentDragPayload final {
  public:
    static Result<std::string> Encode(std::span<const std::filesystem::path> files);
    static Result<std::vector<std::filesystem::path>> Decode(const void *data, size_t bytes);
    static Result<std::vector<std::filesystem::path>> Legacy(const void *data, size_t bytes);
};
} // namespace NexusEngine
