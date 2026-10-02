#pragma once

// c++
#include <cstdint>
#include <string>
#include <vector>

// engine
#include "Foundation/Math/Quaternion.h"
#include "Foundation/Math/Matrix4x4.h"
#include "Foundation/Math/Vector3.h"

namespace NexusEngine {
	inline constexpr uint32_t kMaxSkinJoints = 64;
	enum class AnimationInterpolation : uint8_t {
		Step,
		Linear,
		CubicSpline
	};

	/** Model nodeの分解済みLocal Transform。Animation channelが各要素を上書きする。 */
	struct ModelNodeTransform {
		Vector3 translation;
		Quaternion rotation;
		Vector3 scale = Vector3::One();
	};

	struct VectorKeyframe {
		float time = 0.0f;
		Vector3 value;
		Vector3 inTangent;
		Vector3 outTangent;
	};

	struct QuaternionKeyframe {
		float time = 0.0f;
		Quaternion value;
		Quaternion inTangent { 0.0f, 0.0f, 0.0f, 0.0f };
		Quaternion outTangent { 0.0f, 0.0f, 0.0f, 0.0f };
	};

	/** 1 nodeに対するTRS Animation。空のtrackはbind pose値を維持する。 */
	struct NodeAnimationChannel {
		uint32_t nodeIndex = 0;
		AnimationInterpolation translationInterpolation = AnimationInterpolation::Linear;
		AnimationInterpolation rotationInterpolation = AnimationInterpolation::Linear;
		AnimationInterpolation scaleInterpolation = AnimationInterpolation::Linear;
		std::vector<VectorKeyframe> translations;
		std::vector<QuaternionKeyframe> rotations;
		std::vector<VectorKeyframe> scales;
	};

	/*-----------------------------------------------------------------------------------------
	 * AnimationClip
	 * - ModelResourceが所有するimmutableなnode animation keyframe群
	 * - 再生時刻やloop状態はModelInstanceへ分離する
	 *---------------------------------------------------------------------------------------*/
	struct AnimationClip {
		std::string name;
		float duration = 0.0f;
		std::vector<NodeAnimationChannel> channels;
	};

	/*-----------------------------------------------------------------------------------------
	 * ModelSkin
	 * - Joint nodeとmesh bind poseからJoint空間へ戻すInverse Bind Matrixを保持する
	 * - 頂点WeightやinstanceごとのPaletteは所有しない
	 *---------------------------------------------------------------------------------------*/
	struct ModelSkin {
		std::string name;
		std::vector<uint32_t> joints;
		std::vector<Matrix4x4> inverseBindMatrices;
	};
} // namespace NexusEngine
