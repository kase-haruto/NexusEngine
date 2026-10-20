#pragma once

// c++
#include <cstdint>
#include <vector>

// engine
#include "Foundation/Error/Result.h"
#include "Foundation/Math/Matrix4x4.h"
#include "ModelAnimation.h"

namespace NexusEngine {
	struct ModelAssetData;

	/*-----------------------------------------------------------------------------------------
	 * ModelInstance
	 * - 共有CPU ModelAssetDataを参照し、instance固有のAnimation再生状態とnode姿勢を保持する
	 * - GPU Resource、描画Command、Asset keyframeは所有しない
	 *---------------------------------------------------------------------------------------*/
	class ModelInstance final {
	public:
		/** \brief CPU Assetのbind poseから姿勢を初期化する。Assetはinstanceより長く生存すること */
		[[nodiscard]] Result<void> Initialize(const ModelAssetData& resource);
		void Shutdown() noexcept;

		/** \brief 指定Clipを先頭から再生する */
		[[nodiscard]] Result<void> Play(uint32_t animationIndex, bool loop = true) noexcept;
		/** \brief 再生時刻を進め、TRS補間後のnode階層行列を更新する */
		void Update(float deltaTime) noexcept;

		[[nodiscard]] const ModelAssetData* GetAsset() const noexcept;
		[[nodiscard]] const std::vector<Matrix4x4>& GetNodeWorldTransforms() const noexcept;
		[[nodiscard]] const std::vector<Matrix4x4>* GetSkinPalette(uint32_t skinIndex) const noexcept;

	private:
		void EvaluatePose() noexcept;

		const ModelAssetData* resource_ = nullptr; //< Asset側に所有権を持たない参照
		std::vector<ModelNodeTransform> localTransforms_; //< 現在のnode local姿勢
		std::vector<Matrix4x4> nodeWorldTransforms_; //< hierarchy解決済みModel空間行列
		std::vector<std::vector<Matrix4x4>> skinPalettes_; //< Skinごとの現在Joint Palette
		uint32_t animationIndex_ = UINT32_MAX; //< 現在再生中のClip。UINT32_MAXはbind pose
		float animationTime_ = 0.0f; //< Clip内の現在時刻（秒）
		bool loop_ = true;
	};
} // namespace NexusEngine
