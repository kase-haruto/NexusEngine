#include "AnimationPoseAssets.h"
#include "RetargetProfile.h"
#include "Graphics/Model/ModelLoader.h"
#include "Graphics/Model/ModelInstance.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace NexusEngine {
namespace {
Error PoseError(const std::string& message) {
    return Error(ErrorCategory::Resource, 398, "Animation pose: " + message);
}
Quaternion InverseRotation(Quaternion value) {
    value = value.Normalized();
    return {-value.x, -value.y, -value.z, value.w};
}
Vector3 RotateVector(Quaternion rotation, Vector3 value) {
    const auto result = Quaternion::Multiply(Quaternion::Multiply(rotation, {value.x, value.y, value.z, 0}), InverseRotation(rotation));
    return {result.x, result.y, result.z};
}
RootMotionDelta ComposeMotion(const RootMotionDelta& first, const RootMotionDelta& second) {
    return {RotateVector(second.rotation, first.translation) + second.translation,
        Quaternion::Multiply(second.rotation, first.rotation).Normalized()};
}
RootMotionDelta InverseMotion(const RootMotionDelta& value) {
    const auto rotation = InverseRotation(value.rotation);
    return {RotateVector(rotation, value.translation * -1.0f), rotation};
}
RootMotionDelta PowerMotion(RootMotionDelta value, int64_t count) {
    if(count < 0) {
        count = -count;
        value = InverseMotion(value);
    }
    RootMotionDelta result;
    while(count > 0) {
        if(count & 1) {
            result = ComposeMotion(result, value);
        }
        value = ComposeMotion(value, value);
        count >>= 1;
    }
    return result;
}
}
RootMotionDelta RepeatRootMotion(const RootMotionDelta& delta, uint64_t count) {
    return PowerMotion(delta, static_cast<int64_t>((std::min)(count, uint64_t{1000000000000})));
}
RootMotionDelta ComposeRootMotion(const RootMotionDelta& first, const RootMotionDelta& second) {
    return ComposeMotion(first, second);
}
Result<AnimationAsset> BuildAuthoredAnimation(const std::string& reference, const AnimationClipDocument& document) {
    auto valid = ValidateAnimationDocument(document);
    if(!valid) {
        return std::unexpected(valid.error());
    }
    AnimationAsset asset;
    asset.reference = reference;
    asset.source = document.skeletonSource;
    asset.retargetProfile = document.retargetProfile;
    asset.clip.name = reference;
    asset.clip.duration = document.duration;
    for(const auto& track : document.tracks) {
        if(track.component != "$node") {
            continue;
        }
        const auto found = std::ranges::find(asset.nodePaths, track.target);
        const auto node = static_cast<uint32_t>(found - asset.nodePaths.begin());
        if(found == asset.nodePaths.end()) {
            asset.nodePaths.push_back(track.target);
            asset.clip.channels.push_back({node});
        }
        auto& channel = asset.clip.channels[node];
        for(const auto& key : track.keys) {
            const auto vector = [](const std::vector<float>& value) {
                return value.size() >= 3 ? Vector3{value[0], value[1], value[2]} : Vector3{};
            };
            if(track.property == "rotation" && key.value.size() == 4) {
                QuaternionKeyframe frame;
                frame.time = key.time;
                frame.value = Quaternion{key.value[0], key.value[1], key.value[2], key.value[3]}.Normalized();
                if(key.inTangent.size() == 4) {
                    frame.inTangent = {key.inTangent[0], key.inTangent[1], key.inTangent[2], key.inTangent[3]};
                }
                if(key.outTangent.size() == 4) {
                    frame.outTangent = {key.outTangent[0], key.outTangent[1], key.outTangent[2], key.outTangent[3]};
                }
                channel.rotationInterpolation = track.interpolation;
                channel.rotations.push_back(frame);
            } else if(track.property == "translation" && key.value.size() == 3) {
                channel.translationInterpolation = track.interpolation;
                channel.translations.push_back({key.time, vector(key.value), vector(key.inTangent), vector(key.outTangent)});
            } else if(track.property == "scale" && key.value.size() == 3) {
                channel.scaleInterpolation = track.interpolation;
                channel.scales.push_back({key.time, vector(key.value), vector(key.inTangent), vector(key.outTangent)});
            } else if(track.property == "weights") {
                channel.weightsInterpolation = track.interpolation;
                channel.weights.push_back({key.time, key.value, key.inTangent, key.outTangent});
                asset.clip.hasMorphAnimation = true;
            } else {
                return std::unexpected(PoseError("unsupported authored joint property/dimension"));
            }
        }
    }
    return asset;
}
Result<void> AttachSkeletonMetadata(AnimationAsset& asset, const ModelAssetData& model) {
    const auto paths = SkeletonNodePaths(model);
    for(auto& channel : asset.clip.channels) {
        if(asset.nodePaths.empty()) {
            if(channel.nodeIndex >= paths.size()) {
                return std::unexpected(PoseError("source joint index is invalid"));
            }
        } else {
            if(channel.nodeIndex >= asset.nodePaths.size()) {
                return std::unexpected(PoseError("authored joint index is invalid"));
            }
            const auto found = std::ranges::find(paths, asset.nodePaths[channel.nodeIndex]);
            if(found == paths.end() || std::ranges::count(paths, *found) != 1) {
                return std::unexpected(PoseError("authored joint is missing or ambiguous"));
            }
            channel.nodeIndex = static_cast<uint32_t>(found - paths.begin());
        }
    }
    asset.nodePaths = paths;
    asset.sourceRestPose.clear();
    for(const auto& node : model.nodes) {
        asset.sourceRestPose.push_back(node.localTransform);
    }
    asset.sourceSkins = model.skins;
    asset.skeletonSignature = SkeletonSignature(model);
    return {};
}
void AnimationPoseAssets::SetRoot(const std::filesystem::path& root) {
    if(root_ != root) {
        root_ = root;
        Invalidate();
    }
}
void AnimationPoseAssets::Invalidate() {
    models_.clear();
    poses_.clear();
}
Result<std::vector<std::string>> AnimationPoseAssets::ModelJoints(const std::string& reference) {
    auto model = Model(reference);
    if(!model) {
        return std::unexpected(model.error());
    }
    return SkeletonNodePaths(**model);
}
Result<std::shared_ptr<const ModelAssetData>> AnimationPoseAssets::Model(const std::string& reference) {
    auto path = ResolveAssetReference(root_, reference);
    if(!path) {
        return std::unexpected(path.error());
    }
    auto native = path->native();
    for(auto& character : native) {
        character = static_cast<wchar_t>(std::towlower(character));
    }
    const auto key = std::filesystem::path(native);
    if(const auto found = models_.find(key); found != models_.end()) {
        return found->second;
    }
    auto model = ModelLoader{}.LoadGltf(key, false, false);
    if(!model) {
        return std::unexpected(model.error());
    }
    auto asset = std::make_shared<const ModelAssetData>(std::move(*model));
    models_.emplace(key, asset);
    return asset;
}
Result<std::shared_ptr<const ModelAssetData>> AnimationPoseAssets::Load(const std::string& reference,
    const std::string& targetReference, AnimationClipAssets& clips) {
    const auto key = std::make_pair(reference, targetReference);
    if(const auto found = poses_.find(key); found != poses_.end()) {
        return found->second;
    }
    auto document = clips.Load(reference);
    if(!document) {
        return std::unexpected(document.error());
    }
    auto target = Model(targetReference);
    if(!target) {
        return std::unexpected(target.error());
    }
    std::optional<RetargetProfile> profile;
    if(!(*document)->retargetProfile.empty()) {
        auto path = ResolveAssetReference(root_, (*document)->retargetProfile);
        if(!path) {
            return std::unexpected(path.error());
        }
        auto loaded = LoadRetargetProfile(*path);
        if(!loaded) {
            return std::unexpected(loaded.error());
        }
        auto expectedTarget = ResolveAssetReference(root_, loaded->targetModel);
        auto actualTarget = ResolveAssetReference(root_, targetReference);
        if(!expectedTarget || !actualTarget || !SameFilePath(*expectedTarget, *actualTarget)) {
            return std::unexpected(PoseError("retarget target does not match the entity model"));
        }
        profile = std::move(*loaded);
    }
    const auto sourceReference = !(*document)->source.empty() ? (*document)->source :
        !(*document)->skeletonSource.empty() ? (*document)->skeletonSource : profile ? profile->sourceModel : targetReference;
    auto source = Model(sourceReference);
    if(!source) {
        return std::unexpected(source.error());
    }
    AnimationAsset animation;
    if(!(*document)->source.empty()) {
        const auto clip = std::ranges::find((*source)->animations, (*document)->clip, &AnimationClip::name);
        if(clip == (*source)->animations.end() || std::ranges::count((*source)->animations, (*document)->clip, &AnimationClip::name) != 1) {
            return std::unexpected(PoseError("source clip missing or ambiguous"));
        }
        animation.clip = *clip;
    } else {
        auto authored = BuildAuthoredAnimation(reference, **document);
        if(!authored) {
            return std::unexpected(authored.error());
        }
        animation = std::move(*authored);
    }
    auto metadata = AttachSkeletonMetadata(animation, **source);
    if(!metadata) {
        return std::unexpected(metadata.error());
    }
    if(profile) {
        auto expectedSource = ResolveAssetReference(root_, profile->sourceModel);
        auto actualSource = ResolveAssetReference(root_, sourceReference);
        if(!expectedSource || !actualSource || !SameFilePath(*expectedSource, *actualSource)) {
            return std::unexpected(PoseError("retarget source does not match the clip"));
        }
    }
    auto bound = profile ? RetargetAnimation(animation, **target, *profile) : BindCompatibleAnimation(animation, **target);
    if(!bound) {
        return std::unexpected(bound.error());
    }
    auto pose = std::make_shared<ModelAssetData>();
    pose->nodes = (*target)->nodes;
    pose->animations.push_back(std::move(*bound));
    // geometryを持たないCPU snapshotだけを保持する。
    for(auto& node : pose->nodes) {
        node.meshIndices.clear();
    }
    std::shared_ptr<const ModelAssetData> immutable = std::move(pose);
    poses_.emplace(key, immutable);
    return immutable;
}
Result<RootMotionDelta> ExtractRootMotion(const ModelAssetData& asset, uint32_t root, double begin, double end, bool loop) {
    if(root >= asset.nodes.size() || asset.animations.empty() || !std::isfinite(begin) || !std::isfinite(end)) {
        return std::unexpected(PoseError("invalid root motion range"));
    }
    const double duration = asset.animations.front().duration;
    if(duration <= 0 || begin == end) {
        return RootMotionDelta{};
    }
    ModelInstance sampler;
    auto initialized = sampler.Initialize(asset);
    if(!initialized) {
        return std::unexpected(initialized.error());
    }
    const auto sample = [&](double time) -> RootMotionDelta {
        (void)sampler.Sample(0, static_cast<float>(time), false);
        const auto& matrix = sampler.GetNodeWorldTransforms()[root];
        Quaternion rotation;
        uint32_t node = root;
        for(;;) {
            rotation = Quaternion::Multiply(sampler.GetLocalTransforms()[node].rotation, rotation).Normalized();
            node = asset.nodes[node].parentIndex;
            if(node == ModelNodeAssetData::kNoParent) {
                break;
            }
        }
        return {{matrix.At(3, 0), matrix.At(3, 1), matrix.At(3, 2)}, rotation};
    };
    const auto start = sample(0), finish = sample(duration);
    const auto cycle = ComposeMotion(InverseMotion(start), finish);
    const auto absolute = [&](double time) -> Result<RootMotionDelta> {
        if(!loop) {
            return ComposeMotion(InverseMotion(start), sample(std::clamp(time, 0.0, duration)));
        }
        const double turns = std::floor(time / duration);
        if(std::abs(turns) > 1e12) {
            return std::unexpected(PoseError("root motion range exceeds 1e12 loops"));
        }
        const double wrapped = std::clamp(time - turns * duration, 0.0, duration);
        const auto partial = ComposeMotion(InverseMotion(start), sample(wrapped));
        return ComposeMotion(partial, PowerMotion(cycle, static_cast<int64_t>(turns)));
    };
    auto first = absolute(begin), second = absolute(end);
    if(!first || !second) {
        return std::unexpected(!first ? first.error() : second.error());
    }
    const auto delta = ComposeMotion(*second, InverseMotion(*first));
    if(!std::isfinite(delta.translation.LengthSquared()) || !std::isfinite(delta.rotation.LengthSquared())) {
        return std::unexpected(PoseError("root motion overflow"));
    }
    return delta;
}
}
