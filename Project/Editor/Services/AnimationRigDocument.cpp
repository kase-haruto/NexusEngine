#include "AnimationRigDocument.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"

namespace NexusEngine {
void AnimationRigDocument::Initialize(const std::filesystem::path& root) {
    root_ = std::filesystem::absolute(root).lexically_normal();
}
void AnimationRigDocument::New(bool layers) {
    before_.reset();
    history_.Reset();
    draft_ = layers ? Value{AnimationLayerDocument{}} : Value{RetargetProfile{}};
    const std::string prefix = layers ? "Animations/NewLayers" : "Rigs/NewRig";
    const std::string extension = layers ? ".nxlayers" : ".nxretarget";
    reference_ = prefix + extension;
    for(size_t index = 2; std::filesystem::exists(root_ / reference_); ++index) {
        reference_ = prefix + std::to_string(index) + extension;
    }
    fresh_ = true;
    identity_.clear();
}
Result<void> AnimationRigDocument::Open(const std::filesystem::path& file) {
    const auto relative = file.lexically_relative(root_).generic_u8string();
    const std::string reference(reinterpret_cast<const char*>(relative.data()), relative.size());
    auto path = ResolveFileWithinRoot(root_, reference);
    if(!path) {
        return std::unexpected(path.error());
    }
    Value loaded;
    if(path->extension() == ".nxretarget") {
        auto value = LoadRetargetProfile(*path);
        if(!value) {
            return std::unexpected(value.error());
        }
        loaded = std::move(*value);
    } else if(path->extension() == ".nxlayers") {
        auto value = LoadAnimationLayers(*path);
        if(!value) {
            return std::unexpected(value.error());
        }
        loaded = std::move(*value);
    } else {
        return std::unexpected(Error(ErrorCategory::Resource, 396, "Choose .nxretarget or .nxlayers"));
    }
    draft_ = std::move(loaded);
    reference_ = reference;
    const auto identity = EnsureAssetIdentity(root_, *path);
    identity_ = identity ? *identity : "";
    before_.reset();
    history_.Reset();
    fresh_ = false;
    return {};
}
Result<void> AnimationRigDocument::Save(const std::string& reference) {
    EndEdit();
    const bool currentLocation = reference == reference_;
    RefreshLocation();
    const auto destination = currentLocation ? reference_ : reference;
    auto path = ResolveFileWithinRoot(root_, destination);
    const std::string extension = draft_.index() == 0 ? ".nxretarget" : ".nxlayers";
    if(!path || path->extension() != extension) {
        return std::unexpected(Error(ErrorCategory::Resource, 396, "Save extension does not match the asset"));
    }
    std::error_code error;
    std::filesystem::create_directories(path->parent_path(), error);
    if(error) {
        return std::unexpected(Error(ErrorCategory::FileSystem, 396, error.message()));
    }
    const auto saved = draft_.index() == 0 ? SaveRetargetProfile(*path, std::get<RetargetProfile>(draft_)) :
        SaveAnimationLayers(*path, std::get<AnimationLayerDocument>(draft_));
    if(saved) {
        reference_ = destination;
        const auto identity = EnsureAssetIdentity(root_, *path);
        identity_ = identity ? *identity : "";
        fresh_ = false;
        history_.MarkSaved();
        ++savedRevision_;
    }
    return saved;
}
void AnimationRigDocument::BeginEdit() {
    if(!before_) {
        before_ = draft_;
    }
}
size_t AnimationRigDocument::SavedRevision() const noexcept {
    return savedRevision_;
}
void AnimationRigDocument::EndEdit() {
    if(!before_) {
        return;
    }
    const auto previous = *before_;
    const auto next = draft_;
    before_.reset();
    if(previous != next) {
        history_.Record({"animation.rig.edit", [this, previous] {
            draft_ = previous;
            return true;
        }, [this, next] {
            draft_ = next;
            return true;
        }});
    }
}
bool AnimationRigDocument::Undo() {
    EndEdit();
    return history_.Undo();
}
bool AnimationRigDocument::Redo() {
    EndEdit();
    return history_.Redo();
}
bool AnimationRigDocument::IsDirty() const {
    return fresh_ || history_.IsDirty() || (before_ && *before_ != draft_);
}
const std::string& AnimationRigDocument::Reference() const {
    return reference_;
}
AnimationRigDocument::Value& AnimationRigDocument::Draft() {
    return draft_;
}
const AnimationRigDocument::Value& AnimationRigDocument::Draft() const {
    return draft_;
}
void AnimationRigDocument::RefreshLocation() {
    if(!identity_.empty()) {
        const auto path = ResolveAssetIdentity(root_, identity_);
        if(path) {
            const auto encoded = path->lexically_relative(root_).generic_u8string();
            reference_.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        }
    }
}
}
