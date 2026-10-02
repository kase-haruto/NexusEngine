#include "ModelResource.h"

// engine
#include "Graphics/Resource/MeshResource.h"

namespace NexusEngine {
	ModelResource::~ModelResource() noexcept { Shutdown(); }

	void ModelResource::Shutdown() noexcept {
		meshes_.clear();
		nodes_.clear();
		animations_.clear();
		skins_.clear();
		meshSkinIndices_.clear();
		materials_.clear();
	}

	bool ModelResource::IsInitialized() const noexcept { return !meshes_.empty(); }
	uint32_t ModelResource::GetMeshCount() const noexcept {
		return static_cast<uint32_t>(meshes_.size());
	}
	const MeshResource* ModelResource::GetMesh(const uint32_t index) const noexcept {
		return index < meshes_.size() ? meshes_[index].get() : nullptr;
	}
	const std::vector<ModelNode>& ModelResource::GetNodes() const noexcept { return nodes_; }
	const std::vector<AnimationClip>& ModelResource::GetAnimations() const noexcept { return animations_; }
	const std::vector<ModelSkin>& ModelResource::GetSkins() const noexcept { return skins_; }
	uint32_t ModelResource::GetMeshSkinIndex(const uint32_t meshIndex) const noexcept {
		return meshIndex < meshSkinIndices_.size() ? meshSkinIndices_[meshIndex] : UINT32_MAX;
	}
	const std::vector<ModelMaterial>& ModelResource::GetMaterials() const noexcept { return materials_; }
} // namespace NexusEngine
