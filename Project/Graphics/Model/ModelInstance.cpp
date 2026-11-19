#include "ModelInstance.h"
#include "PoseMath.h"

// c++
#include <algorithm>
#include <cmath>

// engine
#include "ModelAssetData.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidModelInstance = 1;

		[[nodiscard]] Vector3 Lerp(const Vector3& left, const Vector3& right, const float amount) noexcept {
			return left + (right - left) * amount;
		}

		[[nodiscard]] Vector3 CubicInterpolate(
			const VectorKeyframe& left,
			const VectorKeyframe& right,
			const float amount,
			const float interval) noexcept {
			const float amount2 = amount * amount;
			const float amount3 = amount2 * amount;
			return left.value * (2.0f * amount3 - 3.0f * amount2 + 1.0f) +
				left.outTangent * (interval * (amount3 - 2.0f * amount2 + amount)) +
				right.value * (-2.0f * amount3 + 3.0f * amount2) +
				right.inTangent * (interval * (amount3 - amount2));
		}

		[[nodiscard]] Quaternion CubicInterpolate(
			const QuaternionKeyframe& left,
			const QuaternionKeyframe& right,
			const float amount,
			const float interval) noexcept {
			const float amount2 = amount * amount;
			const float amount3 = amount2 * amount;
			const float h00 = 2.0f * amount3 - 3.0f * amount2 + 1.0f;
			const float h10 = interval * (amount3 - 2.0f * amount2 + amount);
			const float h01 = -2.0f * amount3 + 3.0f * amount2;
			const float h11 = interval * (amount3 - amount2);
			return Quaternion {
				left.value.x * h00 + left.outTangent.x * h10 + right.value.x * h01 + right.inTangent.x * h11,
				left.value.y * h00 + left.outTangent.y * h10 + right.value.y * h01 + right.inTangent.y * h11,
				left.value.z * h00 + left.outTangent.z * h10 + right.value.z * h01 + right.inTangent.z * h11,
				left.value.w * h00 + left.outTangent.w * h10 + right.value.w * h01 + right.inTangent.w * h11
			}.Normalized();
		}

		template<typename Keyframe, typename Value, typename LinearInterpolator, typename CubicInterpolator>
		[[nodiscard]] Value SampleTrack(
			const std::vector<Keyframe>& track,
			const float time,
			const Value& fallback,
			const AnimationInterpolation interpolation,
			LinearInterpolator linearInterpolate,
			CubicInterpolator cubicInterpolate) noexcept {
			if(track.empty()) return fallback;
			if(time <= track.front().time) return track.front().value;
			if(time >= track.back().time) return track.back().value;
			const auto upper = std::upper_bound(track.begin(), track.end(), time,
				[](const float sampleTime, const Keyframe& key) { return sampleTime < key.time; });
			const auto lower = upper - 1;
			const float interval = upper->time - lower->time;
			const float amount = interval > 0.0f ? (time - lower->time) / interval : 0.0f;
			if(interpolation == AnimationInterpolation::Step) return lower->value;
			if(interpolation == AnimationInterpolation::CubicSpline) {
				return cubicInterpolate(*lower, *upper, amount, interval);
			}
			return linearInterpolate(lower->value, upper->value, amount);
		}
	} // namespace

	Result<void> ModelInstance::Initialize(const ModelAssetData& resource) {
		if(resource_ != nullptr || (resource.nodes.empty() &&
			(!resource.meshes.empty() || !resource.skins.empty() || !resource.animations.empty()))) {
			return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance,
				"Model instance requires valid CPU asset nodes and must not be initialized twice."));
		}

		// GPU生成に依存せず姿勢を評価するため、CPU階層とSkinの参照範囲をここで検証する。
		for(std::size_t index = 0; index < resource.nodes.size(); ++index) {
			const auto parent = resource.nodes[index].parentIndex;
			if(parent != ModelNodeAssetData::kNoParent && parent >= index)
				return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance, "Invalid CPU model hierarchy."));
		}
		for(const auto& skin : resource.skins) {
			if(skin.joints.size() != skin.inverseBindMatrices.size())
				return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance, "Invalid CPU skin matrices."));
			for(const auto joint : skin.joints) {
				if(joint >= resource.nodes.size())
					return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance, "Invalid CPU skin joint."));
			}
		}
        for(const auto& node : resource.nodes) {
            if(node.morphWeights.size() > kMaxMorphTargets || std::ranges::any_of(node.morphWeights, [](float weight) {
                return !std::isfinite(weight);
            })) {
                return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance, "Invalid default morph weights"));
            }
        }
        for(const auto& clip : resource.animations) {
            for(const auto& channel : clip.channels) {
                float previous = -1;
                for(const auto& key : channel.weights) {
                    const size_t count = channel.nodeIndex < resource.nodes.size() ? resource.nodes[channel.nodeIndex].morphWeights.size() : 0;
                    const auto finite = [](const auto& values) {
                        return std::ranges::all_of(values, [](float value) {
                            return std::isfinite(value);
                        });
                    };
                    if(!count || key.value.size() != count || !finite(key.value) || !std::isfinite(key.time) || key.time < 0 || key.time <= previous || key.time > clip.duration ||
                        (channel.weightsInterpolation == AnimationInterpolation::CubicSpline && (key.inTangent.size() != count || key.outTangent.size() != count || !finite(key.inTangent) || !finite(key.outTangent)))) {
                        return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance, "Invalid morph keyframe dimensions or values"));
                    }
                    previous = key.time;
                }
            }
        }
		resource_ = &resource;
        morphWeights_.resize(resource.nodes.size());
        blendMorphWeights_.resize(resource.nodes.size());
		localTransforms_.reserve(resource.nodes.size());
		for(const ModelNodeAssetData& node : resource.nodes) localTransforms_.push_back(node.localTransform);
		blendTransforms_.resize(resource.nodes.size());
		nodeWorldTransforms_.resize(resource.nodes.size());
		skinPalettes_.resize(resource.skins.size());
		for(std::size_t skinIndex = 0; skinIndex < resource.skins.size(); ++skinIndex) {
			skinPalettes_[skinIndex].resize(resource.skins[skinIndex].joints.size());
		}
		EvaluatePose();
		return {};
	}

	void ModelInstance::Shutdown() noexcept {
		resource_ = nullptr;
		localTransforms_.clear();
        morphWeights_.clear();
        blendMorphWeights_.clear();
		blendTransforms_.clear();
		nodeWorldTransforms_.clear();
		skinPalettes_.clear();
		animationIndex_ = UINT32_MAX;
		animationTime_ = 0.0f;
	}

	Result<void> ModelInstance::Play(const uint32_t animationIndex, const bool loop) noexcept {
		if(resource_ == nullptr || animationIndex >= resource_->animations.size()) {
			return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance,
				"Model animation index is invalid."));
		}
		animationIndex_ = animationIndex;
		animationTime_ = 0.0f;
		loop_ = loop;
		EvaluatePose();
		return {};
	}

	void ModelInstance::Update(const float deltaTime) noexcept {
		if(!std::isfinite(deltaTime)) return;
		if(resource_ == nullptr || animationIndex_ >= resource_->animations.size()) return;
		const AnimationClip& clip = resource_->animations[animationIndex_];
		animationTime_ += (std::max)(deltaTime, 0.0f);
		if(clip.duration > 0.0f) {
			animationTime_ = loop_ ? std::fmod(animationTime_, clip.duration)
				: (std::min)(animationTime_, clip.duration);
		}
		EvaluatePose();
	}

	Result<void> ModelInstance::Sample(uint32_t animationIndex, float time, bool loop) noexcept {
		if(!resource_ || animationIndex >= resource_->animations.size() || !std::isfinite(time) || time < 0)
			return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance, "Invalid animation sample."));
		animationIndex_ = animationIndex;
		loop_ = loop;
		const float duration = resource_->animations[animationIndex].duration;
		animationTime_ = duration > 0 ? (loop ? std::fmod(time, duration) : (std::min)(time, duration)) : 0;
		EvaluatePose();
		return {};
	}

	const ModelAssetData* ModelInstance::GetAsset() const noexcept { return resource_; }
	const std::vector<Matrix4x4>& ModelInstance::GetNodeWorldTransforms() const noexcept {
		return nodeWorldTransforms_;
	}
	const std::vector<Matrix4x4>* ModelInstance::GetSkinPalette(const uint32_t skinIndex) const noexcept {
		return skinIndex < skinPalettes_.size() ? &skinPalettes_[skinIndex] : nullptr;
	}

	Result<void> ModelInstance::SampleBlend(uint32_t first, float firstTime, bool firstLoop,
		uint32_t second, float secondTime, bool secondLoop, float weight) noexcept {
		// 入力の検証を先に完了し、失敗時は直前の姿勢を維持する。
		if(!resource_ || first >= resource_->animations.size() || second >= resource_->animations.size() ||
			!std::isfinite(firstTime) || firstTime < 0 || !std::isfinite(secondTime) || secondTime < 0 ||
			!std::isfinite(weight) || weight < 0 || weight > 1) {
			return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance, "Invalid animation blend sample."));
		}
		animationIndex_ = first;
		animationTime_ = firstTime;
		loop_ = firstLoop;
		EvaluateLocalPose(first, firstTime, firstLoop, localTransforms_);
        EvaluateMorphPose(first, firstTime, firstLoop, morphWeights_);
        EvaluateMorphPose(second, secondTime, secondLoop, blendMorphWeights_);
        for(size_t node = 0; node < morphWeights_.size(); ++node) {
            for(size_t target = 0; target < morphWeights_[node].size(); ++target) {
                morphWeights_[node][target] += (blendMorphWeights_[node][target] - morphWeights_[node][target]) * weight;
            }
        }
		// 各クリップをbind poseから評価し、trackが片方にしかない場合も過去の姿勢を残さない。
		auto& target = blendTransforms_;
		EvaluateLocalPose(second, secondTime, secondLoop, target);
		for(std::size_t index = 0; index < target.size(); ++index) {
			auto& pose = localTransforms_[index];
			pose.translation = Lerp(pose.translation, target[index].translation, weight);
			pose.scale = Lerp(pose.scale, target[index].scale, weight);
			pose.rotation = BlendPoseRotation(pose.rotation, target[index].rotation, weight);
		}
		UpdateMatrices();
		return {};
	}

    Result<void> ModelInstance::SampleLayers(std::span<const AnimationPoseSample> layers) noexcept {
        float total = 0;
        bool ordered = false;
        for(const auto& layer : layers) {
            if(!resource_ || layer.index >= resource_->animations.size() || !std::isfinite(layer.time) || layer.time < 0 ||
                !std::isfinite(layer.weight) || layer.weight < 0 ||
                static_cast<int>(layer.mode) < 0 || static_cast<int>(layer.mode) > 2 ||
                (layer.mode != PoseBlendMode::Normalized && layer.weight > 1) ||
                (ordered && layer.mode == PoseBlendMode::Normalized)) {
                return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance, "Invalid animation layer."));
            }
            ordered |= layer.mode != PoseBlendMode::Normalized;
            if(layer.mode == PoseBlendMode::Normalized) {
                total += layer.weight;
            }
            for(auto node : layer.maskNodes) {
                if(node >= resource_->nodes.size()) {
                    return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance, "Invalid layer mask node."));
                }
            }
        }
        if(!resource_ || !std::isfinite(total) || (total <= 0 && !ordered)) {
            return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance, "Animation layers require positive total weight."));
        }
        // layerだけの再生もbind poseを基準にする。過去frameの結果を加算しない。
        for(size_t node = 0; node < localTransforms_.size(); ++node) {
            localTransforms_[node] = resource_->nodes[node].localTransform;
            morphWeights_[node] = resource_->nodes[node].morphWeights;
        }
        float accumulated = (std::max)(0.0f, 1 - total);
        for(const auto& layer : layers) {
            if(layer.weight == 0) {
                continue;
            }
            EvaluateLocalPose(layer.index, layer.time, layer.loop, blendTransforms_);
            EvaluateMorphPose(layer.index, layer.time, layer.loop, blendMorphWeights_);
            const float weight = layer.mode == PoseBlendMode::Normalized ? layer.weight / (accumulated + layer.weight) : layer.weight;
            for(size_t node = 0; node < localTransforms_.size(); ++node) {
                if(layer.masked && std::ranges::find(layer.maskNodes, static_cast<uint32_t>(node)) == layer.maskNodes.end()) {
                    continue;
                }
                if(layer.mode != PoseBlendMode::Additive) {
                    auto& pose = localTransforms_[node];
                    const auto& target = blendTransforms_[node];
                    pose.translation = Lerp(pose.translation, target.translation, weight);
                    pose.scale = Lerp(pose.scale, target.scale, weight);
                    pose.rotation = BlendPoseRotation(pose.rotation, target.rotation, weight);
                } else {
                    auto& pose = localTransforms_[node];
                    const auto& target = blendTransforms_[node];
                    const auto& rest = resource_->nodes[node].localTransform;
                    pose.translation = pose.translation + (target.translation - rest.translation) * weight;
                    // 零scaleの軸は加算倍率を定義できないため、その軸のscaleを維持する。
                    const auto ratio = [](float value, float reference) {
                        return std::abs(reference) > 1e-8f ? value / reference : 1.0f;
                    };
                    const Vector3 scaleDelta{ratio(target.scale.x, rest.scale.x), ratio(target.scale.y, rest.scale.y), ratio(target.scale.z, rest.scale.z)};
                    const auto factor = Lerp(Vector3::One(), scaleDelta, weight);
                    pose.scale = {pose.scale.x * factor.x, pose.scale.y * factor.y, pose.scale.z * factor.z};
                    const auto restRotation = rest.rotation.Normalized();
                    const Quaternion inverseRest{-restRotation.x, -restRotation.y, -restRotation.z, restRotation.w};
                    const auto delta = Quaternion::Multiply(target.rotation, inverseRest).Normalized();
                    pose.rotation = Quaternion::Multiply(BlendPoseRotation(Quaternion::Identity(), delta, weight), pose.rotation).Normalized();
                }
                for(size_t target = 0; target < morphWeights_[node].size(); ++target) {
                    const float reference = layer.mode == PoseBlendMode::Additive ? resource_->nodes[node].morphWeights[target] : morphWeights_[node][target];
                    morphWeights_[node][target] += (blendMorphWeights_[node][target] - reference) * weight;
                }
            }
            if(layer.mode == PoseBlendMode::Normalized) {
                accumulated += layer.weight;
            }
        }
        UpdateMatrices();
        StripRootMotion(layers);
        return {};
    }
    void ModelInstance::StripRootMotion(std::span<const AnimationPoseSample> layers) noexcept {
        std::vector<uint32_t> processed;
        for(const auto& layer : layers) {
            const auto root = layer.rootMotionNode;
            if(root >= resource_->nodes.size() || std::ranges::find(processed, root) != processed.end()) {
                continue;
            }
            processed.push_back(root);
            auto bindWorld = MakeAffineMatrix(resource_->nodes[root].localTransform.scale,
                resource_->nodes[root].localTransform.rotation, resource_->nodes[root].localTransform.translation);
            Quaternion bindRotation = resource_->nodes[root].localTransform.rotation;
            Quaternion parentRotation;
            auto ancestor = resource_->nodes[root].parentIndex;
            while(ancestor != ModelNodeAssetData::kNoParent) {
                const auto& rest = resource_->nodes[ancestor].localTransform;
                bindWorld = Multiply(bindWorld, MakeAffineMatrix(rest.scale, rest.rotation, rest.translation));
                bindRotation = Quaternion::Multiply(rest.rotation, bindRotation).Normalized();
                parentRotation = Quaternion::Multiply(localTransforms_[ancestor].rotation, parentRotation).Normalized();
                ancestor = resource_->nodes[ancestor].parentIndex;
            }
            const auto parent = resource_->nodes[root].parentIndex;
            const auto parentWorld = parent == ModelNodeAssetData::kNoParent ? Matrix4x4::Identity() : nodeWorldTransforms_[parent];
            const auto inverse = TryInverse(parentWorld);
            if(!inverse) {
                continue;
            }
            const auto& current = nodeWorldTransforms_[root];
            const auto axes = layer.rootMotionAxes;
            const Vector3 world{current.At(3, 0) + (bindWorld.At(3, 0) - current.At(3, 0)) * axes.x,
                current.At(3, 1) + (bindWorld.At(3, 1) - current.At(3, 1)) * axes.y,
                current.At(3, 2) + (bindWorld.At(3, 2) - current.At(3, 2)) * axes.z};
            auto& pose = localTransforms_[root];
            pose.translation = {world.x * inverse->At(0, 0) + world.y * inverse->At(1, 0) + world.z * inverse->At(2, 0) + inverse->At(3, 0),
                world.x * inverse->At(0, 1) + world.y * inverse->At(1, 1) + world.z * inverse->At(2, 1) + inverse->At(3, 1),
                world.x * inverse->At(0, 2) + world.y * inverse->At(1, 2) + world.z * inverse->At(2, 2) + inverse->At(3, 2)};
            if(layer.removeRootRotation) {
                const Quaternion inverseRotation{-parentRotation.x, -parentRotation.y, -parentRotation.z, parentRotation.w};
                pose.rotation = Quaternion::Multiply(inverseRotation, bindRotation).Normalized();
            }
            UpdateMatrices();
        }
    }
    const std::vector<ModelNodeTransform>& ModelInstance::GetLocalTransforms() const noexcept {
        return localTransforms_;
    }
	void ModelInstance::EvaluatePose() noexcept {
		if(resource_ == nullptr) {
			return;
		}
		EvaluateLocalPose(animationIndex_, animationTime_, loop_, localTransforms_);
        EvaluateMorphPose(animationIndex_, animationTime_, loop_, morphWeights_);
		UpdateMatrices();
	}

	void ModelInstance::EvaluateLocalPose(uint32_t clipIndex, float time, bool loop, std::vector<ModelNodeTransform>& transforms) noexcept {
		if(resource_ == nullptr) {
			return;
		}
		const auto& nodes = resource_->nodes;
		for(std::size_t index = 0; index < nodes.size(); ++index) {
			transforms[index] = nodes[index].localTransform;
		}

		if(clipIndex < resource_->animations.size()) {
			const AnimationClip& clip = resource_->animations[clipIndex];
			const float duration = clip.duration;
			time = duration > 0 ? (loop ? std::fmod(time, duration) : (std::min)(time, duration)) : 0;
			for(const NodeAnimationChannel& channel : clip.channels) {
				if(channel.nodeIndex >= transforms.size()) {
					continue;
				}
				ModelNodeTransform& transform = transforms[channel.nodeIndex];
				transform.translation = SampleTrack(
					channel.translations, time, transform.translation,
					channel.translationInterpolation, Lerp,
					static_cast<Vector3(*)(const VectorKeyframe&, const VectorKeyframe&, float, float)>(CubicInterpolate));
				transform.scale = SampleTrack(
					channel.scales, time, transform.scale,
					channel.scaleInterpolation, Lerp,
					static_cast<Vector3(*)(const VectorKeyframe&, const VectorKeyframe&, float, float)>(CubicInterpolate));
				transform.rotation = SampleTrack(
					channel.rotations, time, transform.rotation,
					channel.rotationInterpolation, BlendPoseRotation,
					static_cast<Quaternion(*)(const QuaternionKeyframe&, const QuaternionKeyframe&, float, float)>(CubicInterpolate));
			}
		}

	}

    const std::vector<float>& ModelInstance::GetMorphWeights(uint32_t node) const noexcept {
        static const std::vector<float> empty;
        return node < morphWeights_.size() ? morphWeights_[node] : empty;
    }
    void ModelInstance::EvaluateMorphPose(uint32_t clipIndex, float time, bool loop, std::vector<std::vector<float>>& weights) noexcept {
        for(size_t node = 0; node < resource_->nodes.size(); ++node) {
            weights[node] = resource_->nodes[node].morphWeights;
        }
        if(clipIndex >= resource_->animations.size()) {
            return;
        }
        const auto& clip = resource_->animations[clipIndex];
        time = clip.duration > 0 ? (loop ? std::fmod(time, clip.duration) : (std::min)(time, clip.duration)) : 0;
        for(const auto& channel : clip.channels) {
            if(channel.nodeIndex >= weights.size() || channel.weights.empty()) {
                continue;
            }
            const auto& keys = channel.weights;
            auto& output = weights[channel.nodeIndex];
            if(time <= keys.front().time || keys.size() == 1) {
                output = keys.front().value;
                continue;
            }
            if(time >= keys.back().time) {
                output = keys.back().value;
                continue;
            }
            const auto next = std::upper_bound(keys.begin(), keys.end(), time, [](float value, const MorphKeyframe& key) {
                return value < key.time;
            });
            const auto& a = *(next - 1);
            const auto& b = *next;
            const float duration = b.time - a.time;
            const float u = (time - a.time) / duration;
            for(size_t target = 0; target < output.size(); ++target) {
                if(channel.weightsInterpolation == AnimationInterpolation::Step) {
                    output[target] = a.value[target];
                } else if(channel.weightsInterpolation == AnimationInterpolation::CubicSpline) {
                    const float u2 = u * u, u3 = u2 * u;
                    output[target] = (2 * u3 - 3 * u2 + 1) * a.value[target] + (u3 - 2 * u2 + u) * duration * a.outTangent[target] +
                        (-2 * u3 + 3 * u2) * b.value[target] + (u3 - u2) * duration * b.inTangent[target];
                } else {
                    output[target] = a.value[target] + (b.value[target] - a.value[target]) * u;
                }
            }
        }
    }
	void ModelInstance::UpdateMatrices() noexcept {
		const auto& nodes = resource_->nodes;
		// Initialize時に親indexが子より前になることを検証済みなので、1 passでhierarchyを合成できる。
		for(std::size_t index = 0; index < nodes.size(); ++index) {
			nodeWorldTransforms_[index] = MakeAffineMatrix(
				localTransforms_[index].scale, localTransforms_[index].rotation, localTransforms_[index].translation);
			if(nodes[index].parentIndex != ModelNodeAssetData::kNoParent) {
				nodeWorldTransforms_[index] = Multiply(
					nodeWorldTransforms_[index], nodeWorldTransforms_[nodes[index].parentIndex]);
			}
		}

		// row-vector規約ではvertex * inverseBind * currentJointの順でModel空間へ戻す。
		for(std::size_t skinIndex = 0; skinIndex < resource_->skins.size(); ++skinIndex) {
			const ModelSkin& skin = resource_->skins[skinIndex];
			for(std::size_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex) {
				skinPalettes_[skinIndex][jointIndex] = Multiply(
					skin.inverseBindMatrices[jointIndex], nodeWorldTransforms_[skin.joints[jointIndex]]);
			}
		}
	}
} // namespace NexusEngine
