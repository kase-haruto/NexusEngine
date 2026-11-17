#include "AnimationStudioDocument.h"
#include "Editor/Core/EditorSession.h"
#include "Graphics/Model/ModelAssetLibrary.h"
#include "Runtime/Scene/Components/AnimationPlayerComponent.h"
#include "Runtime/Scene/Components/AnimatorComponent.h"
#include "Runtime/Scene/Components/TimelinePlayerComponent.h"
#include <algorithm>

namespace NexusEngine {
void AnimationStudioDocument::Initialize(const std::filesystem::path& assetRoot) {
    root = std::filesystem::weakly_canonical(assetRoot);
}
void AnimationStudioDocument::New(int kind) {
    value = kind == 0 ? Value{AnimationClipDocument{}} : kind == 1 ? Value{AnimatorDocument{}} : Value{TimelineDocument{}};
    if(kind == 1) {
        auto& controller = std::get<AnimatorDocument>(value);
        controller.states.push_back({});
        const auto clips = Assets(".nxanim");
        if(!clips.empty()) {
            controller.states.front().asset = clips.front();
        }
    }
    reference = kind == 0 ? "Animations/NewAnimation.nxanim" : kind == 1 ? "Animations/NewController.nxcontroller" : "Animations/NewTimeline.nxtimeline";
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
        dirty = false;
        savedRevision = ++revision;
        status = "Saved: " + reference;
        error = false;
    }
    return saved;
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
        if(iterator->is_regular_file(errorCode) && iterator->path().extension() == extension) {
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
        document.source = *referenceResult;
        document.clip = clip.name;
        document.duration = clip.duration;
        auto result = SaveAnimationDocument(file, document);
        if(!result) {
            return result;
        }
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
    if(value.index() == 0) {
        auto* player = entity.GetComponent<AnimationPlayerComponent>();
        if(!player) {
            player = entity.AddComponent<AnimationPlayerComponent>();
        }
        player->PlayAsset(reference);
    } else if(value.index() == 1) {
        auto* animator = entity.GetComponent<AnimatorComponent>();
        if(!animator) {
            animator = entity.AddComponent<AnimatorComponent>();
        }
        animator->controllerAsset = reference;
    } else {
        auto* timeline = entity.GetComponent<TimelinePlayerComponent>();
        if(!timeline) {
            timeline = entity.AddComponent<TimelinePlayerComponent>();
        }
        timeline->timelineAsset = reference;
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
        time = AnimationClipTime(time, clip->duration, clip->loop);
        level->Animations().OverrideClip(reference, *clip);
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
