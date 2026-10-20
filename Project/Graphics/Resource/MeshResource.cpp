#include "MeshResource.h"
#include <atomic>

namespace NexusEngine {
	void MeshResource::CommitGeometryGeneration() noexcept {
		// Asset addressが同じでも再upload後は別Geometry。同一Bufferへのin-place書換えを追加する
		// 場合も、GPU書換え完了時にこの世代を進めることがキャッシュ契約になる。
		static std::atomic<uint64_t> nextGeneration { 1 };
		geometryGeneration_ = nextGeneration.fetch_add(1, std::memory_order_relaxed);
	}
	MeshResource::~MeshResource() noexcept { Shutdown(); }

	void MeshResource::Shutdown() noexcept {
		indexBuffer_.Shutdown();
		vertexBuffer_.Shutdown();
		submeshes_.clear();
		geometryGeneration_ = 0;
	}

	bool MeshResource::IsInitialized() const noexcept {
		return vertexBuffer_.IsInitialized() && indexBuffer_.IsInitialized() && !submeshes_.empty();
	}

	const std::vector<SubmeshRange>& MeshResource::GetSubmeshes() const noexcept {
		return submeshes_;
	}
} // namespace NexusEngine
