#pragma once

// c++
#include <cstdint>
#include <array>
#include <string>
#include <vector>

// engine
#include "Foundation/Math/Matrix4x4.h"
#include "Graphics/Model/ModelAnimation.h"
#include "Graphics/Resource/IndexBuffer.h"
#include "Graphics/Resource/MeshResource.h"
#include "Graphics/Resource/SamplerDesc.h"

namespace NexusEngine {
	/** Static model pipelineへ渡す共通頂点形式。Loaderごとの差をGPU境界へ持ち込まない。 */
	struct ModelVertex {
		float position[3] = {};
		float normal[3] = { 0.0f, 1.0f, 0.0f };
		float texcoord[2] = {};
		float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		float jointIndices[4] = {};
		float jointWeights[4] = {};
	};

	/** Loaderが生成するGPU upload前の1 Mesh分CPU data。 */
	struct MeshAssetData {
		static constexpr uint32_t kNoSkin = UINT32_MAX;

		std::vector<uint8_t> vertexData;
		uint32_t vertexStride = 0;
		std::vector<uint8_t> indexData;
		IndexFormat indexFormat = IndexFormat::UInt16;
		std::vector<SubmeshRange> submeshes;
		uint32_t skinIndex = kNoSkin;
	};

	/** glTF等のLoaderが生成するCPU側Material。画像はGPU upload前のencoded byte列で保持する。 */
	struct ModelMaterialAssetData {
		std::string name;
		std::array<float, 4> baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
		std::vector<uint8_t> baseColorImageData;
		SamplerDesc sampler;
	};

	/** Model hierarchy上のNode。Meshを持たないTransform Nodeも許可する。 */
	struct ModelNodeAssetData {
		static constexpr uint32_t kNoParent = UINT32_MAX;

		std::string name;
		uint32_t parentIndex = kNoParent;
		ModelNodeTransform localTransform;
		std::vector<uint32_t> meshIndices;
	};

	/*-----------------------------------------------------------------------------------------
	 * ModelAssetData
	 * - File LoaderとGPU Resource生成を分離するCPU側中間表現
	 * - 1 Model内の複数MeshとNode hierarchyを保持する
	 *---------------------------------------------------------------------------------------*/
	struct ModelAssetData {
		std::vector<MeshAssetData> meshes;
		std::vector<ModelNodeAssetData> nodes;
		std::vector<AnimationClip> animations;
		std::vector<ModelSkin> skins;
		std::vector<ModelMaterialAssetData> materials;
	};
} // namespace NexusEngine
