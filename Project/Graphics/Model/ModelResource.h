#pragma once

// c++
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>

// engine
#include "Foundation/Math/Matrix4x4.h"
#include "ModelAnimation.h"
#include "Graphics/Resource/SamplerDesc.h"

namespace NexusEngine {
	class GraphicsResourceFactory;
	class MeshResource;

	/** GPU Model Resourceが保持するimmutable hierarchy node。 */
	struct ModelNode {
		static constexpr uint32_t kNoParent = UINT32_MAX;

		std::string name;
		uint32_t parentIndex = kNoParent;
		ModelNodeTransform bindTransform;
		Matrix4x4 bindLocalTransform;
		std::vector<uint32_t> meshIndices;
	};

	struct ModelMaterial {
		std::string name;
		std::array<float, 4> baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
		SamplerDesc sampler;
	};

	/*-----------------------------------------------------------------------------------------
	 * ModelResource
	 * - 1 Assetから生成された複数GPU MeshとNode hierarchyを所有する
	 * - InstanceごとのWorld Transform、Animation時刻、Material parameterは所有しない
	 *---------------------------------------------------------------------------------------*/
	class ModelResource final {
	public:
		ModelResource() noexcept = default;
		~ModelResource() noexcept;
		ModelResource(const ModelResource&) = delete;
		ModelResource& operator=(const ModelResource&) = delete;
		ModelResource(ModelResource&&) = delete;
		ModelResource& operator=(ModelResource&&) = delete;

		/** \brief 全Mesh ResourceとNode metadataを破棄する */
		void Shutdown() noexcept;
		[[nodiscard]] bool IsInitialized() const noexcept;
		[[nodiscard]] uint32_t GetMeshCount() const noexcept;
		[[nodiscard]] const MeshResource* GetMesh(uint32_t index) const noexcept;
		[[nodiscard]] const std::vector<ModelNode>& GetNodes() const noexcept;
		[[nodiscard]] const std::vector<AnimationClip>& GetAnimations() const noexcept;
		[[nodiscard]] const std::vector<ModelSkin>& GetSkins() const noexcept;
		[[nodiscard]] uint32_t GetMeshSkinIndex(uint32_t meshIndex) const noexcept;
		[[nodiscard]] const std::vector<ModelMaterial>& GetMaterials() const noexcept;

	private:
		friend class GraphicsResourceFactory;
		std::vector<std::unique_ptr<MeshResource>> meshes_;
		std::vector<ModelNode> nodes_;
		std::vector<AnimationClip> animations_;
		std::vector<ModelSkin> skins_;
		std::vector<uint32_t> meshSkinIndices_;
		std::vector<ModelMaterial> materials_;
	};
} // namespace NexusEngine
