#include "ContentItemRegistry.h"
#include <algorithm>
#include <cctype>

namespace NexusEngine {
ContentItemRegistry::ContentItemRegistry() {
    Register({"Unknown", "asset.file", "file", {}});
    Register({"Folder", "asset.folder", "folder", {}});
    Register({"Model", "asset.model", "asset_model", {".gltf", ".glb", ".obj"}, ContentPreview::Model});
    Register({"Texture",
              "asset.texture",
              "asset_texture",
              {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".hdr", ".dds"},
              ContentPreview::Texture});
    Register({"Material", "asset.material", "asset_material", {".mat", ".nxmat"}});
    Register({"Level", "asset.level", "asset_level", {".level"}});
    Register({"Prefab", "asset.prefab", "asset_prefab", {".prefab", ".nxprefab"}});
    Register({"Animation", "asset.animation", "asset_animation", {".nxanim"}});
    Register({"Animator", "asset.animator", "asset_animation", {".nxcontroller"}});
    Register({"Timeline", "asset.timeline", "asset_animation", {".nxtimeline"}});
    Register({"Curve", "asset.curve", "asset_curve", {".curve"}});
    Register({"Audio", "asset.audio", "asset_audio", {".wav", ".ogg", ".mp3", ".flac"}});
    Register({"Shader", "asset.shader", "asset_shader", {".hlsl", ".hlsli"}});
    Register({"Cpp", "asset.cpp", "source_cpp", {".cpp", ".cc", ".cxx"}, ContentPreview::None, true});
    Register({"Header", "asset.header", "source_header", {".h", ".hpp", ".hxx"}, ContentPreview::None, true});
    Register({"CSharp", "asset.csharp", "source_csharp", {".cs"}, ContentPreview::None, true});
    for (auto &type : types_) {
        type.moveAcrossFolders = type.id == "Texture" || type.id == "Audio" || type.id == "Curve";
    }
}
bool ContentItemRegistry::Register(ContentItemType type) {
    if (type.id.empty() || type.labelKey.empty() || type.icon.empty()) {
        return false;
    }
    if (std::ranges::any_of(types_, [&](const auto &item) {
            return item.id == type.id || item.labelKey == type.labelKey;
        })) {
        return false;
    }
    // 拡張登録でも小文字化し、OS上の拡張子の大小文字差をUIに漏らさない。
    for (auto &extension : type.extensions) {
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    }
    types_.push_back(std::move(type));
    ++generation_;
    return true;
}
const ContentItemType &ContentItemRegistry::Classify(const std::filesystem::path &path, bool directory) const {
    if (directory) {
        return Find("asset.folder");
    }
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension == ".json" && path.parent_path().filename() == "Levels") {
        return Find("asset.level");
    }
    for (const auto &type : types_) {
        if (std::ranges::find(type.extensions, extension) != type.extensions.end()) {
            return type;
        }
    }
    return types_.front();
}
const ContentItemType &ContentItemRegistry::Find(std::string_view labelKey) const {
    const auto found = std::ranges::find(types_, labelKey, &ContentItemType::labelKey);
    return found == types_.end() ? types_.front() : *found;
}
ContentItemRegistry ContentItemRegistry::MetadataOnly() const {
    auto copy = *this;
    // 非同期indexはDLL由来callbackの寿命を延ばさず、表示情報だけを所有する。
    for (auto &type : copy.types_) {
        type.open = {};
        type.actions.clear();
    }
    return copy;
}
size_t ContentItemRegistry::Generation() const noexcept {
    return generation_;
}
std::span<const ContentItemType> ContentItemRegistry::Types() const {
    return types_;
}
} // namespace NexusEngine
