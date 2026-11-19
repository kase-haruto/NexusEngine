#pragma once

// c++
#include <cstdint>
#include <string>
#include <vector>

// engine
#include "Foundation/Math/Quaternion.h"
#include "Foundation/Math/Matrix4x4.h"
#include "Foundation/Math/Vector3.h"
#include "PoseBlendMode.h"

namespace NexusEngine {
    constexpr uint32_t kMaxSkinJoints = 65536; //< glTFのUInt16 joint index全域。GPU paletteはbufferで転送する
    constexpr uint32_t kMaxMorphTargets = 4096; //< 不正assetによる無制限確保を防ぐ上限
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
    struct MorphKeyframe {
        float time = 0;
        std::vector<float> value, inTangent, outTangent;
    };
	struct NodeAnimationChannel {
		uint32_t nodeIndex = 0;
		AnimationInterpolation translationInterpolation = AnimationInterpolation::Linear;
		AnimationInterpolation rotationInterpolation = AnimationInterpolation::Linear;
		AnimationInterpolation scaleInterpolation = AnimationInterpolation::Linear;
		std::vector<VectorKeyframe> translations;
		std::vector<QuaternionKeyframe> rotations;
		std::vector<VectorKeyframe> scales;
        AnimationInterpolation weightsInterpolation = AnimationInterpolation::Linear;
        std::vector<MorphKeyframe> weights;
	};

	/*-----------------------------------------------------------------------------------------
	 * AnimationClip
	 * - ModelResourceが所有するimmutableなnode animation keyframe群
	 * - 再生時刻やloop状態はModelInstanceへ分離する
	 *---------------------------------------------------------------------------------------*/
    /** ミキサーの入力。assetは永続参照、indexは描画snapshotへバインドした後だけ使用する。 */
    struct AnimationPoseSample {
        std::string asset;
        uint32_t index = 0;
        float time = 0, weight = 1;
        bool loop = true;
        PoseBlendMode mode = PoseBlendMode::Normalized; //< Normalized群の後へ順序付きlayerを適用する
        std::vector<std::string> maskPaths; //< asset相対のtarget joint階層path
        std::vector<uint32_t> maskNodes; //< 描画snapshotへのbinding時だけ構築する
        bool masked = false; //< 空の解決結果を全体maskと誤認しない
        double unwrappedTime = 0; //< 制御層のevent/root motion用。Rendererのtimeは常にclip範囲内
        std::string rootMotionJoint;
        Vector3 rootMotionAxes;
        bool removeRootRotation = false;
        uint32_t rootMotionNode = UINT32_MAX; //< 描画binding後だけ有効
        double motionRate = 1; //< controllerがevent区間を再構成するための合成基本速度
    };
	struct AnimationClip {
		std::string name;
		float duration = 0.0f;
		std::vector<NodeAnimationChannel> channels;
        bool hasMorphAnimation = false; //< weight channelを含むclip
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
