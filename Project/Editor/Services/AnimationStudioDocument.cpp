#include "AnimationStudioDocument.h"
#include "Editor/Core/EditorSession.h"
#include "Graphics/Model/ModelAssetLibrary.h"
#include "Runtime/Scene/Components/AnimationPlayerComponent.h"
#include "Runtime/Scene/Components/AnimatorComponent.h"
#include "Runtime/Scene/Components/TimelinePlayerComponent.h"
#include "Runtime/Scene/Components/KeyframeAnimationComponent.h"
#include "Runtime/Scene/Components/SkeletalAnimationComponent.h"
#include <algorithm>
#include "Platform/FileSystem/AssetIdentity.h"

namespace NexusEngine {
void AnimationStudioDocument::Initialize(const std::filesystem::path& assetRoot) {
    root = std::filesystem::weakly_canonical(assetRoot);
}
void AnimationStudioDocument::New(int kind) {
    value = kind == 0 ? Value{AnimationClipDocument{}} : kind == 1 ? Value{AnimatorDocument{}} : Value{TimelineDocument{}};
    if(kind == 0) {
        std::get<AnimationClipDocument>(value).kind = AnimationClipKind::Keyframe;
    }
    if(kind == 1) {
        auto& controller = std::get<AnimatorDocument>(value);
        controller.states.push_back({});
        const auto clips = Assets(".nxanim");
        if(!clips.empty()) {
            controller.states.front().asset = clips.front();
        }
    }
    reference = kind == 0 ? "Animations/NewAnimation.nxanim" : kind == 1 ? "Animations/NewController.nxcontroller" : "Animations/NewTimeline.nxtimeline";
    identity_.clear();
    identityPath_.clear();
    const auto extension = std::filesystem::path(reference).extension().string();
    const auto prefix = reference.substr(0, reference.size() - extension.size());
    for(size_t index = 2; std::filesystem::exists(root / reference); ++index) {
        reference = prefix + std::to_string(index) + extension;
    }
    dirty = true;
    time = 0;
    selected = key = 0;
    undo_.clear();
    redo_.clear();
    ++revision;
}
Result<void> AnimationStudioDocument::Open(const std::filesystem::path& file) {
    const auto relative = file.lexically_relative(root);
    const auto text = relative.generic_u8string();
    const std::string path(reinterpret_cast<const char*>(text.data()), text.size());
    auto checked = ResolveAnimationPath(root, path);
    if(!checked) {
        return std::unexpected(checked.error());
    }
    Value candidate;
    if(file.extension() == ".nxanim") {
        auto loaded = LoadAnimationClipDocument(*checked);
        if(!loaded) {
            return std::unexpected(loaded.error());
        }
        candidate = std::move(*loaded);
    } else if(file.extension() == ".nxcontroller") {
        auto loaded = LoadAnimatorDocument(*checked);
        if(!loaded) {
            return std::unexpected(loaded.error());
        }
        candidate = std::move(*loaded);
    } else if(file.extension() == ".nxtimeline") {
        auto loaded = LoadTimelineDocument(*checked);
        if(!loaded) {
            return std::unexpected(loaded.error());
        }
        candidate = std::move(*loaded);
    } else {
        return std::unexpected(Error(ErrorCategory::Resource, 393, "Unsupported animation document."));
    }
    value = std::move(candidate);
    reference = path;
    const auto identity = EnsureAssetIdentity(root, *checked);
    identity_ = identity ? *identity : "";
    identityPath_ = reference;
    dirty = false;
    selected = key = 0;
    selectedTransition = -1;
    time = 0;
    undo_.clear();
    redo_.clear();
    ++revision;
    return {};
}
Result<void> AnimationStudioDocument::Save() {
    RefreshLocation();
    auto path = ResolveAnimationPath(root, reference);
    if(!path) {
        return std::unexpected(path.error());
    }
    const std::string expected = value.index() == 0 ? ".nxanim" : value.index() == 1 ? ".nxcontroller" : ".nxtimeline";
    if(path->extension() != expected) {
        return std::unexpected(Error(ErrorCategory::Resource, 393, "File extension must be " + expected));
    }
    std::error_code failure;
    std::filesystem::create_directories(path->parent_path(), failure);
    if(failure) {
        return std::unexpected(Error(ErrorCategory::FileSystem, 393, failure.message()));
    }
    auto saved = std::visit([&](const auto& document) { return SaveAnimationDocument(*path, document); }, value);
    if(saved) {
        const auto identity = EnsureAssetIdentity(root, *path);
        identity_ = identity ? *identity : "";
        identityPath_ = reference;
        dirty = false;
        savedRevision = ++revision;
        status = "Saved: " + reference;
        error = false;
    }
    return saved;
}
void AnimationStudioDocument::RefreshLocation() {
    if(!identity_.empty() && reference == identityPath_) {
        const auto path = ResolveAssetIdentity(root, identity_);
        if(path) {
            const auto encoded = path->lexically_relative(root).generic_u8string();
            reference.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            identityPath_ = reference;
        }
    }
}
void AnimationStudioDocument::Changed(const Value& before, bool continuous) {
    if(!editing_) {
        undo_.push_back(before);
        if(undo_.size() > 100) {
            undo_.erase(undo_.begin());
        }
    }
    editing_ = continuous;
    redo_.clear();
    dirty = true;
    ++revision;
}
void AnimationStudioDocument::EndEdit() {
    editing_ = false;
}
void AnimationStudioDocument::Undo() {
    if(!undo_.empty()) {
        redo_.push_back(value);
        value = std::move(undo_.back());
        undo_.pop_back();
        dirty = true;
        ++revision;
    }
}
void AnimationStudioDocument::Redo() {
    if(!redo_.empty()) {
        undo_.push_back(value);
        value = std::move(redo_.back());
        redo_.pop_back();
        dirty = true;
        ++revision;
    }
}
std::vector<std::string> AnimationStudioDocument::Assets(const std::string& extension) const {
    std::vector<std::string> result;
    std::error_code errorCode;
    std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
    for(; !errorCode && iterator != end && result.size() < 4096; iterator.increment(errorCode)) {
        const auto suffix = iterator->path().extension();
        const bool model = extension == "model" && (suffix == ".gltf" || suffix == ".glb");
        if(iterator->is_regular_file(errorCode) && (suffix == extension || model)) {
            const auto text = iterator->path().lexically_relative(root).generic_u8string();
            result.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
        }
    }
    std::ranges::sort(result);
    return result;
}
Result<void> AnimationStudioDocument::ImportModel(const std::filesystem::path& model) {
    ModelAssetLibrary library(root, ModelAssetData{});
    auto referenceResult = library.Reference(model);
    if(!referenceResult) {
        return std::unexpected(referenceResult.error());
    }
    auto loaded = library.Model(*referenceResult);
    if(!loaded) {
        return std::unexpected(loaded.error());
    }
    const auto folder = root / "Animations" / model.stem();
    std::filesystem::create_directories(folder);
    size_t created = 0;
    for(const auto& clip : (*loaded)->animations) {
        auto name = clip.name;
        for(auto& character : name) {
            if(std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos || static_cast<unsigned char>(character) < 32) {
                character = '_';
            }
        }
        if(name.empty()) {
            name = "Animation";
        }
        auto file = folder / std::filesystem::path(reinterpret_cast<const char8_t*>(name.c_str()));
        file += ".nxanim";
        if(std::filesystem::exists(file)) {
            continue;
        }
        AnimationClipDocument document;
        document.kind = AnimationClipKind::Skeletal;
        const auto sourceIdentity = EnsureAssetIdentity(root, model);
        document.source = sourceIdentity ? *sourceIdentity : *referenceResult;
        document.clip = clip.name;
        document.duration = clip.duration;
        auto result = SaveAnimationDocument(file, document);
        if(!result) {
            return result;
        }
        (void)EnsureAssetIdentity(root, file);
        ++created;
    }
    status = "Imported " + std::to_string(created) + " clips; existing assets were preserved.";
    return {};
}
Result<void> AnimationStudioDocument::MakeEditable() {
    auto* document = std::get_if<AnimationClipDocument>(&value);
    if(!document || document->source.empty()) {
        return {};
    }
    ModelAssetLibrary library(root, ModelAssetData{});
    auto source = library.Animation(document->source + "#animation/" + document->clip);
    if(!source) {
        return std::unexpected(source.error());
    }
    const Value before = value;
    document->skeletonSource = document->source;
    document->duration = (*source)->clip.duration;
    for(const auto& channel : (*source)->clip.channels) {
        const auto add = [&](const auto& keys, const char* property, AnimationInterpolation interpolation) {
            if(keys.empty()) {
                return;
            }
            AnimationTrack track;
            track.target = (*source)->nodePaths.at(channel.nodeIndex);
            track.component = "$node";
            track.property = property;
            track.interpolation = interpolation;
            for(const auto& frame : keys) {
                AnimationKey keyframe;
                keyframe.time = frame.time;
                keyframe.value = {frame.value.x, frame.value.y, frame.value.z};
                keyframe.inTangent = {frame.inTangent.x, frame.inTangent.y, frame.inTangent.z};
                keyframe.outTangent = {frame.outTangent.x, frame.outTangent.y, frame.outTangent.z};
                if constexpr(requires { frame.value.w; }) {
                    keyframe.value.push_back(frame.value.w);
                    keyframe.inTangent.push_back(frame.inTangent.w);
                    keyframe.outTangent.push_back(frame.outTangent.w);
                }
                track.keys.push_back(std::move(keyframe));
            }
            document->tracks.push_back(std::move(track));
        };
        add(channel.translations, "translation", channel.translationInterpolation);
        add(channel.rotations, "rotation", channel.rotationInterpolation);
        add(channel.scales, "scale", channel.scaleInterpolation);
        if(!channel.weights.empty()) {
            AnimationTrack track;
            track.target = (*source)->nodePaths.at(channel.nodeIndex);
            track.component = "$node";
            track.property = "weights";
            track.interpolation = channel.weightsInterpolation;
            for(const auto& frame : channel.weights) {
                track.keys.push_back({frame.time, frame.value, frame.inTangent, frame.outTangent});
            }
            document->tracks.push_back(std::move(track));
        }
    }
    document->source.clear();
    Changed(before, false);
    return {};
}
Result<void> AnimationStudioDocument::Attach(EditorSession& session) {
    auto saved = Save();
    if(!saved) {
        return saved;
    }
    auto entity = session.GetSelectedEntity();
    if(!entity.IsAlive()) {
        return std::unexpected(Error(ErrorCategory::Scene, 393, "Select an Entity first."));
    }
    const auto assetReference = identity_.empty() ? reference : identity_;
    if(value.index() == 0) {
        const auto kind = std::get<AnimationClipDocument>(value).kind;
        if(kind == AnimationClipKind::Keyframe) {
            auto* component = entity.GetComponent<KeyframeAnimationComponent>();
            if(!component) {
                component = entity.AddComponent<KeyframeAnimationComponent>();
            }
            component->clipAsset = assetReference;
            session.MarkDirty();
            return {};
        }
        if(kind == AnimationClipKind::Skeletal) {
            auto* component = entity.GetComponent<SkeletalAnimationComponent>();
            if(!component) {
                component = entity.AddComponent<SkeletalAnimationComponent>();
            }
            component->clipAsset = assetReference;
            session.MarkDirty();
            return {};
        }
        auto* player = entity.GetComponent<AnimationPlayerComponent>();
        if(!player) {
            player = entity.AddComponent<AnimationPlayerComponent>();
        }
        player->PlayAsset(assetReference);
    } else if(value.index() == 1) {
        if(auto* skeletal = entity.GetComponent<SkeletalAnimationComponent>()) {
            skeletal->controllerAsset = assetReference;
        } else if(auto* keyframe = entity.GetComponent<KeyframeAnimationComponent>()) {
            keyframe->controllerAsset = assetReference;
        } else {
            auto* animator = entity.GetComponent<AnimatorComponent>();
            if(!animator) {
                animator = entity.AddComponent<AnimatorComponent>();
            }
            animator->controllerAsset = assetReference;
        }
    } else {
        auto* timeline = entity.GetComponent<TimelinePlayerComponent>();
        if(!timeline) {
            timeline = entity.AddComponent<TimelinePlayerComponent>();
        }
        timeline->timelineAsset = assetReference;
    }
    session.MarkDirty();
    return {};
}
Result<void> AnimationStudioDocument::Preview(EditorSession& session, float deltaTime) {
    if(!preview || session.IsPlaying()) {
        return {};
    }
    auto valid = std::visit([](const auto& document) { return ValidateAnimationDocument(document); }, value);
    if(!valid) {
        return valid;
    }
    const auto selection = session.GetSelection();
    if(!session.HasAnimationPreview() || previewReference_ != reference || previewKind_ != value.index() || previewEntity_ != selection) {
        auto started = session.BeginAnimationPreview();
        if(!started) {
            return started;
        }
        previewRevision_ = revision;
        previewReference_ = reference;
        previewKind_ = value.index();
        previewEntity_ = selection;
    }
    auto* level = session.GetPreviewLevel();
    auto target = level->FindEntity(selection);
    if(!target.IsAlive()) {
        return std::unexpected(Error(ErrorCategory::Scene, 393, "Select an Entity to preview."));
    }
    if(playing) {
        time += deltaTime;
    }
    if(auto* clip = std::get_if<AnimationClipDocument>(&value)) {
        if(clip->kind == AnimationClipKind::Legacy) {
            time = AnimationClipTime(time, clip->duration, clip->loop);
        }
        level->Animations().OverrideClip(reference, *clip);
        target.RemoveComponent<AnimatorComponent>();
        target.RemoveComponent<TimelinePlayerComponent>();
        if(clip->kind == AnimationClipKind::Keyframe) {
            target.RemoveComponent<AnimationPlayerComponent>();
            target.RemoveComponent<SkeletalAnimationComponent>();
            auto* component = target.GetComponent<KeyframeAnimationComponent>();
            if(!component) {
                component = target.AddComponent<KeyframeAnimationComponent>();
            }
            component->clipAsset = reference;
            component->elapsed = time;
            component->playing = false;
            component->loop = clip->loop;
            return level->Update(0);
        }
        if(clip->kind == AnimationClipKind::Skeletal) {
            target.RemoveComponent<AnimationPlayerComponent>();
            target.RemoveComponent<KeyframeAnimationComponent>();
            auto* component = target.GetComponent<SkeletalAnimationComponent>();
            if(!component) {
                component = target.AddComponent<SkeletalAnimationComponent>();
            }
            component->clipAsset = reference;
            component->elapsed = time;
            component->playing = false;
            component->loop = clip->loop;
            return level->Update(0);
        }
        target.RemoveComponent<KeyframeAnimationComponent>();
        target.RemoveComponent<SkeletalAnimationComponent>();
        auto* player = target.GetComponent<AnimationPlayerComponent>();
        if(!player) {
            player = target.AddComponent<AnimationPlayerComponent>();
        }
        target.RemoveComponent<AnimatorComponent>();
        target.RemoveComponent<TimelinePlayerComponent>();
        player->PlayAsset(reference);
        player->time = time;
        player->loop = clip->loop;
        player->playing = false;
    } else if(auto* controller = std::get_if<AnimatorDocument>(&value)) {
        // Controller/Timelineの未保存draftは評価器のoverrideから使用する。
        level->Animations().OverrideController(reference, *controller);
        if(auto* skeletal = target.GetComponent<SkeletalAnimationComponent>()) {
            skeletal->controllerAsset = reference;
            skeletal->playing = playing;
            for(const auto& parameter : controller->parameters) {
                skeletal->controller.SetFloat(parameter.name, parameter.value);
            }
            skeletal->controllerParameters = skeletal->controller.Parameters();
            target.RemoveComponent<AnimatorComponent>();
            target.RemoveComponent<TimelinePlayerComponent>();
            return level->Update(playing ? deltaTime : 0);
        }
        if(auto* keyframe = target.GetComponent<KeyframeAnimationComponent>()) {
            keyframe->controllerAsset = reference;
            keyframe->playing = playing;
            for(const auto& parameter : controller->parameters) {
                keyframe->controller.SetFloat(parameter.name, parameter.value);
            }
            keyframe->controllerParameters = keyframe->controller.Parameters();
            target.RemoveComponent<AnimatorComponent>();
            target.RemoveComponent<TimelinePlayerComponent>();
            return level->Update(playing ? deltaTime : 0);
        }
        auto* animator = target.GetComponent<AnimatorComponent>();
        if(!animator) {
            animator = target.AddComponent<AnimatorComponent>();
        }
        target.RemoveComponent<TimelinePlayerComponent>();
        animator->controllerAsset = reference;
        animator->playing = playing;
        for(const auto& parameter : controller->parameters) {
            animator->SetFloat(parameter.name, parameter.value);
        }
    } else {
        const auto& timelineDocument = std::get<TimelineDocument>(value);
        time = AnimationClipTime(time, timelineDocument.duration, timelineDocument.loop);
        level->Animations().OverrideTimeline(reference, timelineDocument);
        auto* timeline = target.GetComponent<TimelinePlayerComponent>();
        if(!timeline) {
            timeline = target.AddComponent<TimelinePlayerComponent>();
        }
        timeline->timelineAsset = reference;
        timeline->playing = false;
        timeline->time = time;
    }
    return level->Update(value.index() == 1 && playing ? deltaTime : 0);
}
void AnimationStudioDocument::StopPreview(EditorSession& session) {
    preview = playing = false;
    session.EndAnimationPreview();
}
void AnimationStudioDocument::Report(const Result<void>& result) {
    if(!result) {
        status = result.error().GetMessageText();
        error = true;
    } else {
        if(error) {
            status.clear();
        }
        error = false;
    }
}
}
