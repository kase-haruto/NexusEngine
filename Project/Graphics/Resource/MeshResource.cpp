#include "MeshResource.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"
#include <atomic>

namespace NexusEngine {
    struct MeshResource::MorphResource {
        const GraphicsResourceFactory* owner = nullptr;
        VertexBuffer deltas;
        ShaderResourceRef view;
        uint32_t vertices = 0, targets = 0;
        ~MorphResource();
    };
    MeshResource::MorphResource::~MorphResource() {
        if(owner && view.IsValid()) {
            static_cast<void>(owner->RetirePersistentShaderResource(view));
        }
    }
    Result<void> MeshResource::InitializeMorph(const GraphicsResourceFactory& factory, std::span<const uint8_t> bytes, uint32_t vertices, uint32_t targets) {
        if(!vertices || !targets || bytes.size() != static_cast<uint64_t>(vertices) * targets * 36) {
            return std::unexpected(Error(ErrorCategory::Graphics, 144, "Invalid morph buffer dimensions"));
        }
        auto resource = std::make_shared<MorphResource>();
        const auto uploaded = factory.CreateVertexBuffer(resource->deltas, bytes, 36);
        if(!uploaded) {
            return uploaded;
        }
        const auto view = factory.CreatePersistentVertexBufferShaderResource(resource->deltas);
        if(!view) {
            return std::unexpected(view.error());
        }
        resource->owner = &factory;
        resource->view = *view;
        resource->vertices = vertices;
        resource->targets = targets;
        morph_ = std::move(resource);
        return {};
    }
    DeformationData MeshResource::GetDeformation() const noexcept {
        DeformationData result;
        if(morph_) {
            result.morphBuffer = morph_->view.index;
            result.vertexCount = morph_->vertices;
            result.targetCount = morph_->targets;
        }
        return result;
    }
	void MeshResource::CommitGeometryGeneration() noexcept {
		// Asset addressが同じでも再upload後は別Geometry。同一Bufferへのin-place書換えを追加する
		// 場合も、GPU書換え完了時にこの世代を進めることがキャッシュ契約になる。
		static std::atomic<uint64_t> nextGeneration { 1 };
		geometryGeneration_ = nextGeneration.fetch_add(1, std::memory_order_relaxed);
	}
	MeshResource::~MeshResource() noexcept { Shutdown(); }
	void MeshResource::ShareGeometryFrom(const MeshResource& source, const std::vector<SubmeshRange>& materialRanges) {
		submeshes_ = source.submeshes_;
		for(size_t i = 0; i < submeshes_.size(); ++i) submeshes_[i].materialSlot = materialRanges[i].materialSlot;
		vertexBuffer_.ShareFrom(source.vertexBuffer_);
		indexBuffer_.ShareFrom(source.indexBuffer_);
		geometryGeneration_ = source.geometryGeneration_;
        morph_ = source.morph_;
	}

	void MeshResource::Shutdown() noexcept {
        morph_.reset();
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
