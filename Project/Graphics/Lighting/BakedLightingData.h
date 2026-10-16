#pragma once
#include "Foundation/Error/Result.h"
#include "Foundation/Math/Matrix4x4.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>
namespace NexusEngine {
	/*-----------------------------------------------------------------------------------------
	 * BakedLightingSettings
	 * - Sceneが指定するベイク品質と成果物の参照を保持する
	 * - GPU資源、Descriptor、実行時GI計算は所有しない
	 *---------------------------------------------------------------------------------------*/
	struct BakedLightingSettings {
		bool					enabled = false; //< 通常描画の事前計算GI。未ロード時は既存環境光へ戻る
		std::filesystem::path	cachePath; //< 配布・読み込み可能な成果物のパス
		uint64_t				revision   = 0;	  //< 明示的な再読み込み要求
		uint32_t				samples	   = 256; //< ベイク時だけ使う経路数
		uint32_t				maxBounces = 3; //< 受光面からの最大拡散交差回数
		uint32_t				chartSize  = 4; //< 三角形ごとの正方形。外周1/4は余白
		std::array<uint32_t, 3> probeCounts{6, 4, 6}; //< XYZの点数
		float					intensity		= 1; //< 実行時の間接拡散倍率
		bool					automaticBounds = true; //< 静的形状の境界から配置範囲を決める
		std::array<float, 3>	probeMin{-10, -1, -10}, probeMax{10, 8, 10}; //< 明示配置するWorld範囲
		/** \brief 品質・寸法・配置・強度の有限値と予算上限を検証する。 */
		[[nodiscard]] bool		IsValid() const noexcept {
			if(samples < 16 || samples > 4096 || maxBounces < 1 || maxBounces > 8 ||
			   (chartSize != 4 && chartSize != 8 && chartSize != 16) || !std::isfinite(intensity) || intensity < 0 || intensity > 10) return false;
			uint64_t count = 1;
			for(size_t i = 0; i < 3; ++i) {
				if(probeCounts[i] < 2 || probeCounts[i] > 32 || !std::isfinite(probeMin[i]) || !std::isfinite(probeMax[i]) || probeMax[i] <= probeMin[i]) return false;
				count *= probeCounts[i];
			}
			return count <= 4096;
		}
	};
	/** HLSLと80byte一致。Sceneには置かず、Graphics内の転送だけに使用する。 */
	struct BakedLightingDrawData {
		std::array<uint32_t, 4> textures{}; //< アトラス・SH・距離・Sampler
		std::array<float, 4>	originIntensity{};
		std::array<float, 4>	spacingEnabled{};
		std::array<uint32_t, 4> counts{};			 //< XYZ個数と距離cubeの一辺
		std::array<float, 4>	lightmapTransform{}; //< UVのscale・bias。scale=0ならプローブ
	};
	static_assert(sizeof(BakedLightingDrawData) == 80);
	/** 保存時の静的インスタンスとアトラス領域の対応。移動後の誤参照を防ぐ。 */
	struct BakedLightmapBinding {
		uint64_t			 object = 0;
		uint32_t			 node = 0, mesh = 0;
		Matrix4x4			 world;
		std::array<float, 4> scaleBias{};
	};
	/*-----------------------------------------------------------------------------------------
	 * BakedLightingAsset
	 * - ライトマップ、2次SHプローブ、遮蔽距離を保持するCPU成果物
	 * - GPU資源、Sceneの所有、実行時レイ追跡は担当しない
	 *---------------------------------------------------------------------------------------*/
	struct BakedLightingAsset {
		uint64_t						  signature = 0;
		uint32_t						  atlasSize = 0;
		std::array<uint32_t, 3>			  probeCounts{};
		std::array<float, 3>			  origin{}, spacing{};
		std::vector<std::array<float, 4>> lightmap;	 //< RGBはE/π、Aは空の可視率
		std::vector<std::array<float, 4>> probes;	 //< 各点9個のSHと有効フラグ1個
		std::vector<std::array<float, 4>> distances; //< 各点の6面8×8距離を4要素ずつ格納
		std::vector<BakedLightmapBinding> bindings;
	};
	/** \brief バージョン・寸法・checksumを検証してベイク済み成果物を読み込む。 */
	[[nodiscard]] Result<BakedLightingAsset> LoadBakedLighting(const std::filesystem::path&);
	/** \brief ベイク済み成果物を保存する。失敗時にはパス付きエラーを返す。 */
	[[nodiscard]] Result<void> SaveBakedLighting(const BakedLightingAsset&, const std::filesystem::path&);
} // namespace NexusEngine
