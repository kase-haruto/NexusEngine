#pragma once

// c++
#include <array>
#include <cstdint>

// engine
#include "Foundation/Math/Matrix4x4.h"
#include "Graphics/Model/ModelAnimation.h"

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
		float padding = 0.0f; //< HLSL 16byte register境界用padding
		std::array<float, 4> tint = { 1.0f, 1.0f, 1.0f, 1.0f }; //< Vertex colorとTextureへ乗算するRGBA
		std::array<Matrix4x4, kMaxSkinJoints> jointPalette = {}; //< 現在姿勢のSkin Matrix
		uint32_t shadowMaskIndex = 0; //< 共通Visibility Mask、0は全可視
		std::array<uint32_t, 3> shadowPadding {}; //< HLSL 16byte境界
		std::array<float, 4> lightDirectionIntensity { 0.0f, 1.0f, 0.0f, 1.0f }; //< Surface->Light方向/強度
		std::array<float, 4> lightColor { 1, 1, 1, 0 }; //< 線形RGBとpadding
		uint32_t opticalRadianceIndex = 0; //< Reflection/Transmission合成Texture
		std::array<uint32_t, 3> opticalPadding {};
	};

	static_assert(sizeof(PrimitiveDrawData) == 4320);
} // namespace NexusEngine
