#include "ModelInstance.h"

// c++
#include <algorithm>
#include <cmath>

// engine
#include "ModelResource.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidModelInstance = 1;

		[[nodiscard]] Vector3 Lerp(const Vector3& left, const Vector3& right, const float amount) noexcept {
			return left + (right - left) * amount;
		}

		[[nodiscard]] Quaternion Slerp(Quaternion left, Quaternion right, const float amount) noexcept {
			float dot = left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w;
			if(dot < 0.0f) {
				dot = -dot;
				right = { -right.x, -right.y, -right.z, -right.w };
			}
			if(dot > 0.9995f) {
				return Quaternion {
					left.x + (right.x - left.x) * amount,
					left.y + (right.y - left.y) * amount,
					left.z + (right.z - left.z) * amount,
					left.w + (right.w - left.w) * amount
				}.Normalized();
			}
			const float angle = std::acos((std::clamp)(dot, -1.0f, 1.0f));
			const float sine = std::sin(angle);
			const float leftWeight = std::sin((1.0f - amount) * angle) / sine;
			const float rightWeight = std::sin(amount * angle) / sine;
			return Quaternion {
				left.x * leftWeight + right.x * rightWeight,
				left.y * leftWeight + right.y * rightWeight,
				left.z * leftWeight + right.z * rightWeight,
				left.w * leftWeight + right.w * rightWeight
			}.Normalized();
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

	Result<void> ModelInstance::Initialize(const ModelResource& resource) {
		if(resource_ != nullptr || !resource.IsInitialized()) {
			return std::unexpected(Error(ErrorCategory::Resource, kInvalidModelInstance,
				"Cannot initialize a model instance from an uninitialized resource."));
		}
		resource_ = &resource;
		localTransforms_.reserve(resource.GetNodes().size());
		for(const ModelNode& node : resource.GetNodes()) localTransforms_.push_back(node.bindTransform);
		nodeWorldTransforms_.resize(resource.GetNodes().size());
		skinPalettes_.resize(resource.GetSkins().size());
		for(std::size_t skinIndex = 0; skinIndex < resource.GetSkins().size(); ++skinIndex) {
			skinPalettes_[skinIndex].resize(resource.GetSkins()[skinIndex].joints.size());
		}
		EvaluatePose();
		return {};
	}

	void ModelInstance::Shutdown() noexcept {
		resource_ = nullptr;
		localTransforms_.clear();
		nodeWorldTransforms_.clear();
		skinPalettes_.clear();
		animationIndex_ = UINT32_MAX;
		animationTime_ = 0.0f;
	}

	Result<void> ModelInstance::Play(const uint32_t animationIndex, const bool loop) noexcept {
		if(resource_ == nullptr || animationIndex >= resource_->GetAnimations().size()) {
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
		if(resource_ == nullptr || animationIndex_ >= resource_->GetAnimations().size()) return;
		const AnimationClip& clip = resource_->GetAnimations()[animationIndex_];
		animationTime_ += (std::max)(deltaTime, 0.0f);
		if(clip.duration > 0.0f) {
			animationTime_ = loop_ ? std::fmod(animationTime_, clip.duration)
				: (std::min)(animationTime_, clip.duration);
		}
		EvaluatePose();
	}

	const ModelResource* ModelInstance::GetResource() const noexcept { return resource_; }
	const std::vector<Matrix4x4>& ModelInstance::GetNodeWorldTransforms() const noexcept {
		return nodeWorldTransforms_;
	}
	const std::vector<Matrix4x4>* ModelInstance::GetSkinPalette(const uint32_t skinIndex) const noexcept {
		return skinIndex < skinPalettes_.size() ? &skinPalettes_[skinIndex] : nullptr;
	}

	void ModelInstance::EvaluatePose() noexcept {
		if(resource_ == nullptr) return;
		const auto& nodes = resource_->GetNodes();
		for(std::size_t index = 0; index < nodes.size(); ++index) localTransforms_[index] = nodes[index].bindTransform;

		if(animationIndex_ < resource_->GetAnimations().size()) {
			const AnimationClip& clip = resource_->GetAnimations()[animationIndex_];
			for(const NodeAnimationChannel& channel : clip.channels) {
				if(channel.nodeIndex >= localTransforms_.size()) continue;
				ModelNodeTransform& transform = localTransforms_[channel.nodeIndex];
				transform.translation = SampleTrack(
					channel.translations, animationTime_, transform.translation,
					channel.translationInterpolation, Lerp,
					static_cast<Vector3(*)(const VectorKeyframe&, const VectorKeyframe&, float, float)>(CubicInterpolate));
				transform.scale = SampleTrack(
					channel.scales, animationTime_, transform.scale,
					channel.scaleInterpolation, Lerp,
					static_cast<Vector3(*)(const VectorKeyframe&, const VectorKeyframe&, float, float)>(CubicInterpolate));
				transform.rotation = SampleTrack(
					channel.rotations, animationTime_, transform.rotation,
					channel.rotationInterpolation, Slerp,
					static_cast<Quaternion(*)(const QuaternionKeyframe&, const QuaternionKeyframe&, float, float)>(CubicInterpolate));
			}
		}

		// Resource生成時に親indexが子より前になることを検証済みなので、1 passでhierarchyを合成できる。
		for(std::size_t index = 0; index < nodes.size(); ++index) {
			nodeWorldTransforms_[index] = MakeAffineMatrix(
				localTransforms_[index].scale, localTransforms_[index].rotation, localTransforms_[index].translation);
			if(nodes[index].parentIndex != ModelNode::kNoParent) {
				nodeWorldTransforms_[index] = Multiply(
					nodeWorldTransforms_[index], nodeWorldTransforms_[nodes[index].parentIndex]);
			}
		}

		// row-vector規約ではvertex * inverseBind * currentJointの順でModel空間へ戻す。
		for(std::size_t skinIndex = 0; skinIndex < resource_->GetSkins().size(); ++skinIndex) {
			const ModelSkin& skin = resource_->GetSkins()[skinIndex];
			for(std::size_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex) {
				skinPalettes_[skinIndex][jointIndex] = Multiply(
					skin.inverseBindMatrices[jointIndex], nodeWorldTransforms_[skin.joints[jointIndex]]);
			}
		}
	}
} // namespace NexusEngine
