#include "AnimationClipAssets.h"
#include <algorithm>
#include <cwctype>
#include "Graphics/Model/GltfAnimationMetadata.h"
#include "Platform/FileSystem/AssetIdentity.h"

namespace NexusEngine {
AnimationClipKind EffectiveAnimationClipKind(const AnimationClipDocument& document) {
    if(document.kind != AnimationClipKind::Legacy) {
        return document.kind;
    }
    const bool skeletal = !document.source.empty() || !document.retargetProfile.empty() || !document.skeletonSource.empty() || std::ranges::any_of(document.tracks, [](const auto& track) {
        return track.component == "$node";
    });
    const bool property = std::ranges::any_of(document.tracks, [](const auto& track) {
        return track.component != "$node";
    });
    if(skeletal && property) {
        return AnimationClipKind::Legacy;
    }
    return skeletal ? AnimationClipKind::Skeletal : AnimationClipKind::Keyframe;
}
void AnimationClipAssets::SetRoot(const std::filesystem::path& root) {
    if(root_ != root) {
        Invalidate();
        root_ = root;
    }
}
void AnimationClipAssets::Invalidate() {
    clips_.clear();
}
Result<std::shared_ptr<const AnimationClipDocument>> AnimationClipAssets::Load(const std::string& reference) {
    auto path = Key(reference);
    if(!path) {
        return std::unexpected(path.error());
    }
    if(const auto found = clips_.find(*path); found != clips_.end()) {
        return found->second;
    }
    auto document = LoadAnimationClipDocument(*path);
    if(!document) {
        return std::unexpected(document.error());
    }
    if(!document->durationExplicit && !document->source.empty()) {
        auto source = ResolveAssetReference(root_, document->source);
        if(!source) {
            return std::unexpected(source.error());
        }
        auto duration = GltfAnimationDuration(*source, document->clip);
        if(!duration) {
            return std::unexpected(duration.error());
        }
        document->duration = *duration;
        document->durationExplicit = true;
        auto valid = ValidateAnimationDocument(*document);
        if(!valid) {
            return std::unexpected(valid.error());
        }
    }
    auto asset = std::make_shared<const AnimationClipDocument>(std::move(*document));
    clips_.emplace(*path, asset);
    return asset;
}
void AnimationClipAssets::Override(const std::string& reference, const AnimationClipDocument& document) {
    auto key = Key(reference);
    if(key && ValidateAnimationDocument(document)) {
        clips_[*key] = std::make_shared<const AnimationClipDocument>(document);
    }
}
Result<std::filesystem::path> AnimationClipAssets::Key(const std::string& reference) const {
    auto path = ResolveAssetReference(root_, reference);
    if(!path) {
        return std::unexpected(path.error());
    }
    auto native = path->native();
    for(auto& character : native) {
        character = static_cast<wchar_t>(std::towlower(character));
    }
    auto normalized = std::filesystem::path(native);
    if(normalized.extension() != ".nxanim") {
        return std::unexpected(Error(ErrorCategory::Resource, 390, "Clip requires a .nxanim asset"));
    }
    return normalized;
}
}
