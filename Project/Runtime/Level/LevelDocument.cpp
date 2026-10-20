#include "LevelDocument.h"

namespace NexusEngine {
Result<void> LevelDocument::Create(std::optional<std::string> name) {
    auto level = std::make_unique<Level>(name.value_or("New Level"));
    auto result = level->LoadEmpty();
    if(!result) return result;
    level_ = std::move(level); filePath_.reset(); return {};
}
Result<void> LevelDocument::Open(const std::filesystem::path& path) {
    auto level = std::make_unique<Level>();
    auto result = level->Load(path);
    if(!result) return result;
    level_ = std::move(level); filePath_ = path; return {};
}
Result<void> LevelDocument::Save() {
    // 名前から保存先を再生成するとSaveAs後も別ファイルへ保存されるため禁止する。
    if(!filePath_) return std::unexpected(Error(ErrorCategory::Scene, 90, "Save As is required."));
    return SaveAs(*filePath_);
}
Result<void> LevelDocument::SaveAs(const std::filesystem::path& path) {
    if(!level_ || path.empty()) return std::unexpected(Error(ErrorCategory::Scene, 91, "No level or empty save path."));
    auto result = level_->Save(path);
    if(result) filePath_ = path;
    return result;
}
}
