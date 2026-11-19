#include "AssetDocumentRegistry.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <algorithm>

namespace NexusEngine {
bool AssetDocumentRegistry::Register(AssetDocumentRegistration document) {
    if (document.id.empty() || !document.path || !document.dirty || !document.save ||
        std::ranges::any_of(documents_, [&](const auto &old) {
            return old.id == document.id;
        })) {
        return false;
    }
    documents_.push_back(std::move(document));
    return true;
}
void AssetDocumentRegistry::Clear() {
    documents_.clear();
}
Result<void> AssetDocumentRegistry::SaveAll() {
    std::string failures;
    for (const auto &document : documents_) {
        if (!document.dirty()) {
            continue;
        }
        const auto saved = document.save();
        if (!saved) {
            if (!failures.empty()) {
                failures += "\n";
            }
            failures += document.label + ": " + saved.error().GetMessageText();
        }
    }
    if (!failures.empty()) {
        return std::unexpected(Error(ErrorCategory::Resource, 293, std::move(failures)));
    }
    return {};
}
Result<void> AssetDocumentRegistry::Save(const std::filesystem::path &path) {
    for (const auto &document : documents_) {
        if (!document.path().empty() && SameFilePath(document.path(), path)) {
            return document.save();
        }
    }
    return std::unexpected(Error(ErrorCategory::Resource, 293, "No open editable document owns this file."));
}
bool AssetDocumentRegistry::AnyDirty() const {
    return std::ranges::any_of(documents_, [](const auto &value) {
        return value.dirty();
    });
}
bool AssetDocumentRegistry::Dirty(const std::filesystem::path &path) const {
    return std::ranges::any_of(documents_, [&](const auto &value) {
        const auto file = value.path();
        return !file.empty() && SameFilePath(file, path) && value.dirty();
    });
}
std::span<const AssetDocumentRegistration> AssetDocumentRegistry::Documents() const {
    return documents_;
}
} // namespace NexusEngine
