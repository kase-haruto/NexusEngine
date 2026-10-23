#pragma once

// c++
#include <array>
#include <cstdint>

// engine
#include "Foundation/Math/Matrix4x4.h"
#include "Graphics/Model/ModelAnimation.h"
#include "Graphics/Environment/EnvironmentLightingData.h"
#include "Graphics/Lighting/BakedLightingData.h"
#include "Graphics/Lighting/DDGISettings.h"

namespace NexusEngine {
	/*-----------------------------------------------------------------------------------------
	 * PrimitiveDrawData
	 * - Primitive 1 Drawの変換行列とMaterial parameterを1つのCBVへ転送する
	 * - HLSL側PrimitiveDrawData.hlsliとメンバ順・サイズを一致させる
	 *---------------------------------------------------------------------------------------*/
	struct PrimitiveDrawData {
		Matrix4x4 worldViewProjection; //< Local座標からClip座標へのrow-vector変換
		Matrix4x4 worldInverseTranspose; //< 非一様Scaleでも方向を保つ法線変換行列
		uint32_t textureIndex = 0; //< ResourceDescriptorHeap上のTexture index
		uint32_t samplerIndex = 0; //< SamplerDescriptorHeap上のSampler index
		uint32_t skinningEnabled = 0; //< Joint Paletteを適用するDrawでは1
        float padding = 0.0f; //< HLSL 16byte境界
		std::array<float, 4> tint = { 1.0f, 1.0f, 1.0f, 1.0f }; //< Vertex colorとTextureへ乗算するRGBA
		std::array<Matrix4x4, kMaxSkinJoints> jointPalette = {}; //< 現在姿勢のSkin Matrix
		uint32_t shadowMaskIndex = 0; //< 共通Visibility Mask、0は全可視
        std::array<uint32_t, 3> shadowPadding {}; //< HLSL 16byte境界
		std::array<float, 4> lightDirectionIntensity { 0.0f, 1.0f, 0.0f, 1.0f }; //< Surface->Light方向/強度
		std::array<float, 4> lightColor { 1, 1, 1, 0 }; //< 線形RGBとpadding
		uint32_t opticalRadianceIndex = 0; //< Reflection/Transmission合成Texture
		uint32_t diffuseGIIndex = 0; //< 間接拡散放射。alpha=0は未計算、1～2は有効フラグと鏡面環境の可視率
		std::array<uint32_t, 2> opticalPadding {};
		Matrix4x4 world;
		std::array<float, 4> cameraPosition {};
		std::array<float, 4> ambientIrradiance {0.03f, 0.03f, 0.03f, 0};
		std::array<float, 4> pbrParameters {0, 1, 1, 0}; // metallic, roughness, normal scale
		uint32_t normalTextureIndex = 0, normalSamplerIndex = 0;
		uint32_t metallicRoughnessTextureIndex = 0, metallicRoughnessSamplerIndex = 0;
		std::array<float, 4> fogColorEnabled {};
		std::array<float, 4> fogDistances {8, 40, 0, 0};
		std::array<float, 4> emissiveOcclusion {0, 0, 0, 1};
		std::array<uint32_t, 4> extraMaps {}; //< emissive/sampler/AO/sampler
		EnvironmentDrawData environment;
		BakedLightingDrawData bakedLighting;
		DDGIDrawData ddgi;
	};

	static_assert(sizeof(PrimitiveDrawData) == 4720);
} // namespace NexusEngine
