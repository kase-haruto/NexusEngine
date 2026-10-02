#include "MeshResource.h"

namespace NexusEngine {
	MeshResource::~MeshResource() noexcept { Shutdown(); }

	void MeshResource::Shutdown() noexcept {
		indexBuffer_.Shutdown();
		vertexBuffer_.Shutdown();
		submeshes_.clear();
	}

	bool MeshResource::IsInitialized() const noexcept {
		return vertexBuffer_.IsInitialized() && indexBuffer_.IsInitialized() && !submeshes_.empty();
	}

	const std::vector<SubmeshRange>& MeshResource::GetSubmeshes() const noexcept {
		return submeshes_;
	}
} // namespace NexusEngine
