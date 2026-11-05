#pragma once
// Project実装専用。公開APIにJSONライブラリの型を含めない。
#include "ThirdParty/nlohmann/json.hpp"
#include "Foundation/Error/Result.h"
#include <filesystem>

namespace NexusEngine::ProjectJson {
using Json = nlohmann::json;
[[nodiscard]] Result<Json> Read(const std::filesystem::path& file);
[[nodiscard]] Result<void> Write(const std::filesystem::path& file, const Json& json, bool replace = true);
void Keys(const Json& object, std::initializer_list<const char*> allowed);
std::string Text(const Json& object, const char* key, bool allowEmpty = false);
std::string Utf8(const std::filesystem::path& path);
std::filesystem::path Path(const std::string& value);
}
