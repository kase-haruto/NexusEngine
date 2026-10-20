#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace NexusEngine {
	/*-----------------------------------------------------------------------------------------
	 * DDGISettings
	 * - 動的照度フィールドの配置・更新品質をSceneからRendererへ渡す
	 * - GPU資源とDescriptorの所有はBackendへ閉じる
	 *---------------------------------------------------------------------------------------*/
	struct DDGISettings {
		bool					enabled	   = false;					//< 動的プローブGIを選択する
		bool					freeze	   = false;					//< 初期化後のプローブ更新を停止する
		bool					rotateRays = false;					//< 時間的な積分精度を上げる。既定は固定方向でちらつきを抑える
		bool					relocation = true;					//< 表面付近・形状内部のプローブを移動する
		bool					feedback   = true;					//< 前フレームの照度から多重反射を更新する
		uint64_t				revision   = 0;						//< 履歴の明示リセット
		std::array<float, 3>	origin{-12, 0, -5};					//< 最小格子点のWorld位置
		std::array<float, 3>	spacing{2, 2, 2};					//< XYZの格子間隔
		std::array<uint32_t, 3> counts{13, 5, 6};					//< XYZの点数
		uint32_t				raysPerProbe = 128;					//< 各プローブの球面サンプル数
		float					hysteresis	 = .97f;				//< 履歴の重み。大きいほど安定し、追従は遅くなる
		float					intensity	 = 1;					//< 描画時の間接拡散倍率。フィードバックへは掛けない
		float					normalBias = .03f, viewBias = .01f; //< 受光位置の自己遮蔽回避
		float					maxDistance = 100;					//< プローブレイの最大距離
		/** \brief 有限値・テクスチャ寸法・レイ数の予算を検証する。 */
		[[nodiscard]] bool IsValid() const noexcept {
			uint64_t total = 1;
			for(size_t n = 0; n < 3; ++n) {
				if(counts[n] < 2 || counts[n] > 16 || !std::isfinite(origin[n]) || !std::isfinite(spacing[n]) || spacing[n] < .05f || spacing[n] > 1000) return false;
				total *= counts[n];
			}
			return total <= 2048 && raysPerProbe >= 32 && raysPerProbe <= 256 && raysPerProbe % 32 == 0 &&
				   std::isfinite(hysteresis) && hysteresis >= 0 && hysteresis <= .995f && std::isfinite(intensity) && intensity >= 0 && intensity <= 10 &&
				   std::isfinite(normalBias) && normalBias >= 0 && normalBias <= 1 && std::isfinite(viewBias) && viewBias >= 0 && viewBias <= 1 &&
				   std::isfinite(maxDistance) && maxDistance >= 1 && maxDistance <= 1000;
		}
	};
	/** Graphics内の転送値。HLSLと80byte一致。Sceneは設定値だけを保持する。 */
	struct DDGIDrawData {
		std::array<uint32_t, 4> textures{};		   //< 照度・距離モーメント・配置状態・Sampler
		std::array<float, 4>	originIntensity{}; //< 格子の始点XYZ・表示強度
		std::array<float, 4>	spacingEnabled{};  //< 格子間隔XYZ・有効な履歴の有無
		std::array<uint32_t, 4> counts{};		   //< XYZ個数とレイ数
		std::array<float, 4>	sampling{};		   //< normal bias・view bias・最大距離・距離重みの指数
	};
	static_assert(sizeof(DDGIDrawData) == 80);
} // namespace NexusEngine
