#include "RetargetProfile.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace NexusEngine {
bool RetargetJointMapping::operator==(const RetargetJointMapping&) const = default;
bool RetargetProfile::operator==(const RetargetProfile&) const = default;
namespace {
Error InvalidRetarget(const std::string& message) {
    return Error(ErrorCategory::Resource, 394, "Retarget: " + message);
}
bool Finite(Vector3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
Quaternion Inverse(Quaternion value) {
    value = value.Normalized();
    return {-value.x, -value.y, -value.z, value.w};
}
Vector3 Rotate(Quaternion axis, Vector3 value) {
    const Quaternion point{value.x, value.y, value.z, 0};
    const auto result = Quaternion::Multiply(Quaternion::Multiply(axis, point), Inverse(axis));
    return {result.x, result.y, result.z};
}
Vector3 Product(Vector3 first, Vector3 second) {
    return {first.x * second.x, first.y * second.y, first.z * second.z};
}
}
std::vector<std::string> SkeletonNodePaths(const ModelAssetData& model) {
    std::vector<std::string> paths;
    for(const auto& node : model.nodes) {
        if(node.parentIndex != ModelNodeAssetData::kNoParent && node.parentIndex >= paths.size()) {
            return {};
        }
        const std::string parent = node.parentIndex == ModelNodeAssetData::kNoParent ? "" : paths[node.parentIndex];
        paths.push_back(parent + std::to_string(node.name.size()) + ":" + node.name + "/");
    }
    return paths;
}
std::string DisplaySkeletonPath(const std::string& path) {
    std::string result;
    size_t cursor = 0;
    while(cursor < path.size()) {
        const auto separator = path.find(':', cursor);
        if(separator == path.npos) {
            return path;
        }
        size_t length = 0;
        const auto parsed = std::from_chars(path.data() + cursor, path.data() + separator, length);
        if(parsed.ec != std::errc{} || parsed.ptr != path.data() + separator || length > path.size() - separator - 1) {
            return path;
        }
        const size_t end = separator + 1 + length;
        if(end >= path.size() || path[end] != '/') {
            return path;
        }
        if(!result.empty()) {
            result += '/';
        }
        result += path.substr(separator + 1, length);
        cursor = end + 1;
    }
    return result;
}
std::string SkeletonSignature(const ModelAssetData& model) {
    // 骨格変更検出用の決定的identity。認証hashとしては使わない。
    uint64_t hash = 14695981039346656037ull;
    const auto append = [&](uint32_t value) {
        for(int byte = 0; byte < 4; ++byte) {
            hash ^= static_cast<uint8_t>(value >> (byte * 8));
            hash *= 1099511628211ull;
        }
    };
    append(static_cast<uint32_t>(model.nodes.size()));
    for(const auto& node : model.nodes) {
        append(static_cast<uint32_t>(node.name.size()));
        for(unsigned char character : node.name) {
            append(character);
        }
        append(node.parentIndex);
        const auto& rest = node.localTransform;
        for(float value : {rest.translation.x, rest.translation.y, rest.translation.z,
            rest.rotation.x, rest.rotation.y, rest.rotation.z, rest.rotation.w, rest.scale.x, rest.scale.y, rest.scale.z}) {
            append(std::bit_cast<uint32_t>(value == 0 ? 0.0f : value));
        }
    }
    for(const auto& skin : model.skins) {
        append(static_cast<uint32_t>(skin.joints.size()));
        for(auto joint : skin.joints) {
            append(joint);
        }
        for(const auto& matrix : skin.inverseBindMatrices) {
            for(float value : matrix.elements) {
                append(std::bit_cast<uint32_t>(value == 0 ? 0.0f : value));
            }
        }
    }
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << hash;
    return text.str();
}
Result<void> ValidateRetargetProfile(const RetargetProfile& profile) {
    if(profile.joints.empty() || profile.joints.size() > 65536) {
        return std::unexpected(InvalidRetarget("joint mappings required"));
    }
    std::set<std::string> sources, targets;
    for(const auto& joint : profile.joints) {
        if(joint.source.empty() || joint.target.empty() || joint.source.size() > 16384 || joint.target.size() > 16384 ||
            !sources.insert(joint.source).second || !targets.insert(joint.target).second || !Finite(joint.translationScale) ||
            std::abs(joint.translationScale.x) > 1e6f || std::abs(joint.translationScale.y) > 1e6f || std::abs(joint.translationScale.z) > 1e6f ||
            !std::isfinite(joint.axis.LengthSquared()) || std::abs(joint.axis.LengthSquared() - 1) > .001f) {
            return std::unexpected(InvalidRetarget("invalid or duplicate mapping/axis/translation scale"));
        }
    }
    return {};
}
Result<RetargetProfile> LoadRetargetProfile(const std::filesystem::path& file) {
    try {
        if(std::filesystem::file_size(file) > 32 * 1024 * 1024) {
            return std::unexpected(InvalidRetarget("file too large"));
        }
        std::ifstream input(NativeFilePath(file));
        const auto json = nlohmann::json::parse(input);
        if(json.at("version") != 1 || !json.at("joints").is_array()) {
            return std::unexpected(InvalidRetarget("unsupported format"));
        }
        RetargetProfile profile;
        profile.sourceModel = json.at("sourceModel");
        profile.targetModel = json.at("targetModel");
        profile.sourceSignature = json.value("sourceSignature", "");
        profile.targetSignature = json.value("targetSignature", "");
        for(const auto& entry : json.at("joints")) {
            const auto axis = entry.value("axis", std::vector<float>{0, 0, 0, 1});
            const auto scale = entry.value("translationScale", std::vector<float>{1, 1, 1});
            if(axis.size() != 4 || scale.size() != 3) {
                return std::unexpected(InvalidRetarget("invalid mapping dimensions"));
            }
            profile.joints.push_back({entry.at("source"), entry.at("target"),
                {axis[0], axis[1], axis[2], axis[3]}, {scale[0], scale[1], scale[2]}});
        }
        auto valid = ValidateRetargetProfile(profile);
        if(!valid) {
            return std::unexpected(valid.error());
        }
        return profile;
    } catch(const std::exception& error) {
        return std::unexpected(InvalidRetarget(error.what()));
    }
}
Result<void> SaveRetargetProfile(const std::filesystem::path& file, const RetargetProfile& profile) {
    auto valid = ValidateRetargetProfile(profile);
    if(!valid) {
        return valid;
    }
    nlohmann::json json{{"version", 1}, {"sourceModel", profile.sourceModel}, {"targetModel", profile.targetModel},
        {"sourceSignature", profile.sourceSignature}, {"targetSignature", profile.targetSignature}, {"joints", nlohmann::json::array()}};
    for(const auto& joint : profile.joints) {
        json["joints"].push_back({{"source", joint.source}, {"target", joint.target},
            {"axis", {joint.axis.x, joint.axis.y, joint.axis.z, joint.axis.w}},
            {"translationScale", {joint.translationScale.x, joint.translationScale.y, joint.translationScale.z}}});
    }
    return WriteFileAtomically(file, json.dump(2) + "\n");
}
RetargetProfile SuggestRetargetProfile(const ModelAssetData& source, const ModelAssetData& target) {
    RetargetProfile profile;
    profile.sourceSignature = SkeletonSignature(source);
    profile.targetSignature = SkeletonSignature(target);
    const auto sourcePaths = SkeletonNodePaths(source), targetPaths = SkeletonNodePaths(target);
    for(size_t index = 0; index < source.nodes.size() && index < sourcePaths.size(); ++index) {
        const auto& name = source.nodes[index].name;
        const auto found = std::ranges::find(target.nodes, name, &ModelNodeAssetData::name);
        if(found != target.nodes.end() && std::ranges::count(source.nodes, name, &ModelNodeAssetData::name) == 1 &&
            std::ranges::count(target.nodes, name, &ModelNodeAssetData::name) == 1) {
            const size_t targetIndex = static_cast<size_t>(found - target.nodes.begin());
            if(targetIndex < targetPaths.size()) {
                profile.joints.push_back({sourcePaths[index], targetPaths[targetIndex]});
            }
        }
    }
    return profile;
}
Result<AnimationClip> RetargetAnimation(const AnimationAsset& source, const ModelAssetData& target, const RetargetProfile& profile) {
    auto valid = ValidateRetargetProfile(profile);
    if(!valid) {
        return std::unexpected(valid.error());
    }
    const auto targetPaths = SkeletonNodePaths(target);
    if(source.sourceRestPose.size() != source.nodePaths.size() || targetPaths.size() != target.nodes.size()) {
        return std::unexpected(InvalidRetarget("rest pose metadata required"));
    }
    if(!profile.targetSignature.empty() && profile.targetSignature != SkeletonSignature(target)) {
        return std::unexpected(InvalidRetarget("target skeleton changed; review profile"));
    }
    if(!profile.sourceSignature.empty() && profile.sourceSignature != source.skeletonSignature) {
        return std::unexpected(InvalidRetarget("source skeleton changed; review profile"));
    }
    AnimationClip bound = source.clip;
    for(auto& channel : bound.channels) {
        if(channel.nodeIndex >= source.nodePaths.size()) {
            return std::unexpected(InvalidRetarget("invalid source node"));
        }
        const auto mapping = std::ranges::find(profile.joints, source.nodePaths[channel.nodeIndex], &RetargetJointMapping::source);
        if(mapping == profile.joints.end()) {
            return std::unexpected(InvalidRetarget("animated joint has no mapping"));
        }
        const auto found = std::ranges::find(targetPaths, mapping->target);
        if(found == targetPaths.end() || std::ranges::count(targetPaths, mapping->target) != 1 ||
            std::ranges::count(source.nodePaths, mapping->source) != 1) {
            return std::unexpected(InvalidRetarget("joint mapping is missing or ambiguous"));
        }
        const auto& sourceRest = source.sourceRestPose[channel.nodeIndex];
        const auto targetIndex = static_cast<size_t>(found - targetPaths.begin());
        const auto& targetRest = target.nodes[targetIndex].localTransform;
        if(!Finite(sourceRest.scale) || std::abs(sourceRest.scale.x) < 1e-8f ||
            std::abs(sourceRest.scale.y) < 1e-8f || std::abs(sourceRest.scale.z) < 1e-8f) {
            return std::unexpected(InvalidRetarget("source rest scale is singular"));
        }
        const auto axis = mapping->axis.Normalized();
        const auto rotationLeft = Quaternion::Multiply(Quaternion::Multiply(targetRest.rotation, axis), Inverse(sourceRest.rotation));
        for(auto& key : channel.translations) {
            key.value = targetRest.translation + Product(Rotate(axis, key.value - sourceRest.translation), mapping->translationScale);
            key.inTangent = Product(Rotate(axis, key.inTangent), mapping->translationScale);
            key.outTangent = Product(Rotate(axis, key.outTangent), mapping->translationScale);
        }
        const Vector3 scaleRatio{targetRest.scale.x / sourceRest.scale.x, targetRest.scale.y / sourceRest.scale.y, targetRest.scale.z / sourceRest.scale.z};
        for(auto& key : channel.scales) {
            key.value = Product(key.value, scaleRatio);
            key.inTangent = Product(key.inTangent, scaleRatio);
            key.outTangent = Product(key.outTangent, scaleRatio);
        }
        for(auto& key : channel.rotations) {
            key.value = Quaternion::Multiply(Quaternion::Multiply(rotationLeft, key.value), Inverse(axis)).Normalized();
            key.inTangent = Quaternion::Multiply(Quaternion::Multiply(rotationLeft, key.inTangent), Inverse(axis));
            key.outTangent = Quaternion::Multiply(Quaternion::Multiply(rotationLeft, key.outTangent), Inverse(axis));
        }
        channel.nodeIndex = static_cast<uint32_t>(targetIndex);
        if(!channel.weights.empty() && channel.weights.front().value.size() != target.nodes[targetIndex].morphWeights.size()) {
            return std::unexpected(InvalidRetarget("morph target dimensions differ"));
        }
    }
    return bound;
}
Result<AnimationClip> BindCompatibleAnimation(const AnimationAsset& source, const ModelAssetData& target) {
    const auto paths = SkeletonNodePaths(target);
    const auto matching = [](const Matrix4x4& first, const Matrix4x4& second) {
        for(size_t index = 0; index < first.elements.size(); ++index) {
            const float tolerance = 1e-4f * (std::max)(1.0f, (std::max)(std::abs(first.elements[index]), std::abs(second.elements[index])));
            if(std::abs(first.elements[index] - second.elements[index]) > tolerance) {
                return false;
            }
        }
        return true;
    };
    const auto resolve = [&](uint32_t index) -> Result<uint32_t> {
        if(index >= source.nodePaths.size()) {
            return std::unexpected(InvalidRetarget("source node index is invalid"));
        }
        const auto& path = source.nodePaths[index];
        const auto found = std::ranges::find(paths, path);
        if(found == paths.end() || std::ranges::count(paths, path) != 1 || std::ranges::count(source.nodePaths, path) != 1) {
            return std::unexpected(InvalidRetarget("skeleton hierarchy differs; explicit profile required"));
        }
        const auto targetIndex = static_cast<uint32_t>(found - paths.begin());
        if(!source.sourceRestPose.empty()) {
            if(index >= source.sourceRestPose.size()) {
                return std::unexpected(InvalidRetarget("source rest pose metadata is invalid"));
            }
            const auto& first = source.sourceRestPose[index];
            const auto& second = target.nodes[targetIndex].localTransform;
            if(!matching(MakeAffineMatrix(first.scale, first.rotation, first.translation), MakeAffineMatrix(second.scale, second.rotation, second.translation))) {
                return std::unexpected(InvalidRetarget("rest pose differs; explicit profile required"));
            }
        }
        return targetIndex;
    };
    for(const auto& skin : source.sourceSkins) {
        if(skin.joints.size() != skin.inverseBindMatrices.size()) {
            return std::unexpected(InvalidRetarget("invalid source inverse bind matrices"));
        }
        for(size_t joint = 0; joint < skin.joints.size(); ++joint) {
            auto targetJoint = resolve(skin.joints[joint]);
            if(!targetJoint) {
                return std::unexpected(targetJoint.error());
            }
            bool found = false;
            for(const auto& targetSkin : target.skins) {
                for(size_t entry = 0; entry < targetSkin.joints.size() && entry < targetSkin.inverseBindMatrices.size(); ++entry) {
                    found |= targetSkin.joints[entry] == *targetJoint && matching(skin.inverseBindMatrices[joint], targetSkin.inverseBindMatrices[entry]);
                }
            }
            if(!found) {
                return std::unexpected(InvalidRetarget("inverse bind pose differs; explicit profile required"));
            }
        }
    }
    AnimationClip bound = source.clip;
    for(auto& channel : bound.channels) {
        auto node = resolve(channel.nodeIndex);
        if(!node) {
            return std::unexpected(node.error());
        }
        channel.nodeIndex = *node;
        if(!channel.weights.empty() && channel.weights.front().value.size() != target.nodes[*node].morphWeights.size()) {
            return std::unexpected(InvalidRetarget("morph target dimensions differ"));
        }
    }
    return bound;
}
}
