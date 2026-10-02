#include "GraphicsContext.h"

// c++
#include <limits>
#include <utility>

// directx
#include <d3d12.h>

// engine
#include "Graphics/Descriptor/BindlessDescriptorTable.h"
#include "Graphics/Descriptor/TransientDescriptorArena.h"
#include "Graphics/Pipeline/GraphicsPipeline.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include "Graphics/Resource/IndexBuffer.h"
#include "Graphics/Resource/MeshResource.h"
#include "Graphics/Resource/VertexBuffer.h"

namespace NexusEngine {
	GraphicsContext::GraphicsContext(
		void* const					   nativeCommandList,
		BindlessDescriptorTable* const bindlessDescriptors,
		TransientDescriptorArena* const transientDescriptors,
		const uint32_t				   frameIndex,
		const uint32_t				   renderWidth,
		const uint32_t				   renderHeight) noexcept
		: nativeCommandList_(nativeCommandList),
		  bindlessDescriptors_(bindlessDescriptors),
		  transientDescriptors_(transientDescriptors),
		  frameIndex_(frameIndex),
		  renderWidth_(renderWidth),
		  renderHeight_(renderHeight) {}

	void GraphicsContext::SetGraphicsPipeline(const GraphicsPipeline& pipeline) noexcept {
		// Backend型への変換はこの翻訳単位だけで行い、上位RendererへCommand Listを公開しない。
		pipeline.Bind(static_cast<ID3D12GraphicsCommandList*>(nativeCommandList_));
	}

	bool GraphicsContext::SetGraphicsDescriptorTables(
		const GraphicsPipeline& pipeline,
		const ShaderResourceRef resourceTable,
		const ShaderResourceRef samplerTable) noexcept {
		if(bindlessDescriptors_ == nullptr) {
			return false;
		}
		const auto& rootSignature  = pipeline.GetRootSignature();
		const auto	resourceHandle = bindlessDescriptors_->GetGpuHandle(resourceTable);
		const auto	samplerHandle  = bindlessDescriptors_->GetGpuHandle(samplerTable);
		if((rootSignature.HasResourceTable() && resourceHandle.ptr == 0) ||
		   (rootSignature.HasSamplerTable() && samplerHandle.ptr == 0)) {
			return false;
		}
		rootSignature.BindDescriptorTables(
			static_cast<ID3D12GraphicsCommandList*>(nativeCommandList_), resourceHandle, samplerHandle);
		return true;
	}

	Result<void> GraphicsContext::SetGraphicsConstantBufferTable(
		const GraphicsPipeline& pipeline,
		const ConstantBuffer& constantBuffer) {
		constexpr int32_t kInvalidConstantBufferTable = 1;
		const auto& rootSignature = pipeline.GetRootSignature();
		if(transientDescriptors_ == nullptr || !rootSignature.HasResourceTable() ||
		   constantBuffer.GetAlignedSliceSize() > (std::numeric_limits<uint32_t>::max)()) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidConstantBufferTable,
				"Pipeline or transient constant buffer table state is invalid."));
		}

		// Buffer sliceとDescriptor Arenaに同じFrameContext indexを使い、
		// CPU書き換え可能時点とDescriptor再利用可能時点を同じFenceへ揃える。
		auto handle = transientDescriptors_->CreateConstantBufferView(
			frameIndex_, constantBuffer.GetGpuAddress(frameIndex_),
			static_cast<uint32_t>(constantBuffer.GetAlignedSliceSize()));
		if(!handle) return std::unexpected(std::move(handle.error()));

		rootSignature.BindDescriptorTables(
			static_cast<ID3D12GraphicsCommandList*>(nativeCommandList_), *handle, {});
		return {};
	}

	void GraphicsContext::SetVertexBuffer(const VertexBuffer& vertexBuffer) noexcept {
		vertexBuffer.Bind(nativeCommandList_);
	}

	void GraphicsContext::SetIndexBuffer(const IndexBuffer& indexBuffer) noexcept {
		indexBuffer.Bind(nativeCommandList_);
	}

	void GraphicsContext::SetMesh(const MeshResource& mesh) noexcept {
		mesh.vertexBuffer_.Bind(nativeCommandList_);
		mesh.indexBuffer_.Bind(nativeCommandList_);
	}

	void GraphicsContext::SetPrimitiveTopology(const PrimitiveTopology topology) noexcept {
		auto* const commandList = static_cast<ID3D12GraphicsCommandList*>(nativeCommandList_);
		switch(topology) {
		case PrimitiveTopology::TriangleList:
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			break;
		}
	}

	void GraphicsContext::Draw(
		const uint32_t vertexCount,
		const uint32_t instanceCount,
		const uint32_t firstVertex,
		const uint32_t firstInstance) noexcept {
		static_cast<ID3D12GraphicsCommandList*>(nativeCommandList_)->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void GraphicsContext::DrawIndexed(
		const uint32_t indexCount,
		const uint32_t instanceCount,
		const uint32_t firstIndex,
		const int32_t vertexOffset,
		const uint32_t firstInstance) noexcept {
		static_cast<ID3D12GraphicsCommandList*>(nativeCommandList_)->DrawIndexedInstanced(
			indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}
} // namespace NexusEngine
