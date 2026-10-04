#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "Foundation/Math/Matrix4x4.h"
#include "Graphics/Descriptor/ShaderResourceRef.h"

namespace NexusEngine {
	class MeshResource;

	enum class ShadowMode : uint8_t { Auto, RayTracing, ShadowMap };
	enum class ShadowTechnique : uint8_t { Disabled, RayTracing, ShadowMap };

	/*-----------------------------------------------------------------------------------------
	 * ShadowSettings
	 * - 描画方式と品質のAPI非依存設定を保持する
	 * - GPU Capability判定、Resource所有、Editor表示は担当しない
	 *---------------------------------------------------------------------------------------*/
	struct ShadowSettings {
		ShadowMode mode = ShadowMode::Auto; //< 対応GPUではRT、非対応ではMap
		bool enabled = true; //< falseでは影PassとAS更新を省く
		bool softShadow = false; //< Angular radiusを使った複数Rayを許可する
		uint32_t sampleCount = 1; //< Ray数。高負荷を既定値にしない
		float rayOriginBias = 0.002f; //< World単位の自己交差対策
		float rayTMin = 0.001f; //< Ray開始距離
		float rayTMax = 10000.0f; //< 最大遮蔽検索距離
		float shadowMapBias = 0.001f; //< 正規化Depth比較のBias
		uint32_t shadowMapResolution = 2048; //< Directional Light用Depth解像度
		float shadowMapExtent = 50.0f; //< Light中心からの正射影半幅、World単位
	};

	/** 方向はSurfaceからLightへの単位方向。Scene/Backendいずれの型も保持しない。 */
	struct DirectionalLightData {
		std::array<float, 3> direction { 0.0f, 1.0f, 0.0f }; //< Shadow Ray方向
		float angularRadius = 0.00465f; //< Soft Shadowの円錐半角、radian
		std::array<float, 3> color { 1.0f, 1.0f, 1.0f }; //< 線形RGB
		float intensity = 0.75f; //< 直接光強度
	};

	/*-----------------------------------------------------------------------------------------
	 * ShadowDrawItem
	 * - Raster影とASへ同じGeometry/Transformを渡す描画snapshot
	 * - Meshとpaletteは記録終了まで有効な非所有参照。Scene/Entityは保持しない
	 * - Geometry lifetimeのGPU保証はBackendがResource参照を取得して担当する
	 *---------------------------------------------------------------------------------------*/
	struct ShadowDrawItem {
		const MeshResource* mesh = nullptr; //< 共有Mesh Geometry
		Matrix4x4 world; //< InstanceのLocal->World変換
		std::span<const Matrix4x4> jointPalette; //< 空ならStatic Geometry。変形の拡張点
		uint64_t instanceId = 0; //< 上位識別方式を公開しない描画Instance識別値
	};

	struct ShadowStatistics {
		ShadowTechnique technique = ShadowTechnique::Disabled; //< 現在記録に使用する方式
		uint32_t blasCount = 0; //< キャッシュ中のBLAS数
		uint32_t tlasInstanceCount = 0; //< 今回のTLAS Instance数
		uint64_t blasBuildCount = 0; //< 累積Build数。静的再利用の確認用
		uint64_t tlasBuildCount = 0; //< 累積TLAS再構築数
		uint64_t tlasUpdateCount = 0; //< 累積Transform update数
		bool fallback = false; //< 指定RTを利用できずMapへ切替えた場合true
	};
}
