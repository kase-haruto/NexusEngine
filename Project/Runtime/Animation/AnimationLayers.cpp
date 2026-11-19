#include "AnimationLayers.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <cmath>
#include <cwctype>
#include <fstream>

namespace NexusEngine {
bool AnimationLayerDefinition::operator==(const AnimationLayerDefinition&) const = default;
bool AnimationLayerDocument::operator==(const AnimationLayerDocument&) const = default;
namespace {
Error InvalidLayers(const std::string& message) {
    return Error(ErrorCategory::Resource, 395, "Animation layers: " + message);
}
}
Result<void> ValidateAnimationLayers(const AnimationLayerDocument& document) {
    if(document.layers.size() > 32) {
        return std::unexpected(InvalidLayers("at most 32 ordered layers are supported"));
    }
    for(const auto& layer : document.layers) {
        if(layer.name.empty() || layer.clip.empty() || layer.mask.size() > 65536 ||
            !std::isfinite(layer.weight) || layer.weight < 0 || layer.weight > 1 ||
            !std::isfinite(layer.speed) || std::abs(layer.speed) > 1e6f ||
            !std::isfinite(layer.curveDuration) || layer.curveDuration < 0 || layer.curveDuration > 1e12f ||
            (layer.mode != PoseBlendMode::Override && layer.mode != PoseBlendMode::Additive)) {
            return std::unexpected(InvalidLayers("invalid name/clip/mode/weight/speed/curve duration"));
        }
        for(const auto& path : layer.mask) {
            if(path.empty() || path.size() > 16384 || path.back() != '/') {
                return std::unexpected(InvalidLayers("mask requires canonical joint paths"));
            }
        }
    }
    return {};
}
Result<AnimationLayerDocument> LoadAnimationLayers(const std::filesystem::path& file) {
    try {
        if(std::filesystem::file_size(file) > 32 * 1024 * 1024) {
            return std::unexpected(InvalidLayers("file too large"));
        }
        std::ifstream input(NativeFilePath(file));
        const auto json = nlohmann::json::parse(input);
        if(json.at("version") != 1 || !json.at("layers").is_array() || json.at("layers").size() > 32) {
            return std::unexpected(InvalidLayers("unsupported document"));
        }
        AnimationLayerDocument document;
        for(const auto& entry : json.at("layers")) {
            AnimationLayerDefinition layer;
            layer.name = entry.at("name");
            layer.clip = entry.at("clip");
            layer.speedCurve = entry.value("speedCurve", "");
            layer.weightCurve = entry.value("weightCurve", "");
            layer.weight = entry.value("weight", 1.0f);
            layer.speed = entry.value("speed", 1.0f);
            layer.curveDuration = entry.value("curveDuration", 0.0f);
            layer.loop = entry.value("loop", true);
            layer.enabled = entry.value("enabled", true);
            layer.mode = static_cast<PoseBlendMode>(entry.value("mode", 1));
            layer.mask = entry.value("mask", std::vector<std::string>{});
            document.layers.push_back(std::move(layer));
        }
        auto valid = ValidateAnimationLayers(document);
        if(!valid) {
            return std::unexpected(valid.error());
        }
        return document;
    } catch(const std::exception& error) {
        return std::unexpected(InvalidLayers(error.what()));
    }
}
Result<void> SaveAnimationLayers(const std::filesystem::path& file, const AnimationLayerDocument& document) {
    auto valid = ValidateAnimationLayers(document);
    if(!valid) {
        return valid;
    }
    nlohmann::json json{{"version", 1}, {"layers", nlohmann::json::array()}};
    for(const auto& layer : document.layers) {
        json["layers"].push_back({{"name", layer.name}, {"clip", layer.clip}, {"mode", static_cast<int>(layer.mode)},
            {"weight", layer.weight}, {"speed", layer.speed}, {"curveDuration", layer.curveDuration},
            {"enabled", layer.enabled}, {"loop", layer.loop}, {"mask", layer.mask},
            {"speedCurve", layer.speedCurve}, {"weightCurve", layer.weightCurve}});
    }
    return WriteFileAtomically(file, json.dump(2) + "\n");
}
void AnimationLayerAssets::SetRoot(const std::filesystem::path& root) {
    if(root_ != root) {
        root_ = root;
        Invalidate();
    }
}
void AnimationLayerAssets::Invalidate() {
    saved_.clear();
}
Result<std::shared_ptr<const AnimationLayerDocument>> AnimationLayerAssets::Load(const std::string& reference) {
    auto path = ResolveAssetReference(root_, reference);
    if(!path) {
        return std::unexpected(path.error());
    }
    auto native = path->native();
    for(auto& character : native) {
        character = static_cast<wchar_t>(std::towlower(character));
    }
    const auto key = std::filesystem::path(native);
    if(key.extension() != ".nxlayers") {
        return std::unexpected(InvalidLayers("choose a .nxlayers asset"));
    }
    if(const auto found = saved_.find(key); found != saved_.end()) {
        return found->second;
    }
    auto document = LoadAnimationLayers(key);
    if(!document) {
        return std::unexpected(document.error());
    }
    auto snapshot = std::make_shared<const AnimationLayerDocument>(std::move(*document));
    saved_.emplace(key, snapshot);
    return snapshot;
}
}
