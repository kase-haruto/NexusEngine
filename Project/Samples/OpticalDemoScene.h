#pragma once
#include "Graphics/Model/ModelAssetData.h"
#include "Foundation/Error/Result.h"
namespace NexusEngine {
	inline constexpr uint32_t kOpticalMirrorMaterial = 5;
	inline constexpr uint32_t kOpticalGlassMaterial = 6;
	/** 反射用Sphere（左）、屈折/色吸収用Sphere（右）と色付きBackdropを配置したCPU test asset。 */
	[[nodiscard]] Result<ModelAssetData> CreateOpticalDemoScene();
	struct OpticalSphereMaterialSlots { uint32_t mirror = 0, glass = 0; };
	/** \brief 読み込み済みSponzaの既存Mesh/Nodeを保持し、独立Root Nodeの2つのSphereを追加する */
	[[nodiscard]] Result<OpticalSphereMaterialSlots> AppendSponzaOpticalSpheres(ModelAssetData& model);
}
