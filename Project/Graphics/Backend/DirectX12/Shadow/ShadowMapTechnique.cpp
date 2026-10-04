#include "ShadowBackend.h"

#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Renderer/GraphicsContext.h"

namespace NexusEngine {
	Result<void> ShadowMapTechnique::Initialize(ShadowBackend& backend) {
		auto shader = backend.Compile(L"Shadow/Resolve.CS.hlsl", L"main", L"cs_6_6");
		if(!shader) return std::unexpected(std::move(shader.error()));
		D3D12_COMPUTE_PIPELINE_STATE_DESC desc {};
		desc.pRootSignature = backend.root.Get(); desc.CS = { shader->data(), shader->size() };
		const HRESULT result = backend.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&resolve_));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(61, result, "Shadow Map resolve PSO creation failed."));
		return {};
	}
	Result<void> ShadowMapTechnique::Render(ShadowBackend& backend, GraphicsContext& context,
		ID3D12GraphicsCommandList* commands, std::span<const ShadowDrawItem> draws,
		ShadowGpuConstants constants, const ShadowSettings& settings) {
		if(depth_.width != settings.shadowMapResolution) {
			// Resolution edits are rare; old Map and its descriptor can still be in-flight.
			auto wait = backend.queue->WaitForIdle(); if(!wait) return wait;
			backend.ReleaseTexture(depth_);
			auto result = backend.CreateTexture(depth_, settings.shadowMapResolution, settings.shadowMapResolution, true);
			if(!result) return result;
		}
		constants.indices[1] = depth_.srv.index;
		initialState_ = depth_.state;
		recording_ = true;
		auto result = backend.Rasterize(context, commands, depth_, constants.lightViewProjection, draws, constants);
		if(!result) return result;
		constants.dimensions[2] = 0;
		auto address = backend.WriteConstants(context.GetFrameIndex(), constants);
		if(!address) return std::unexpected(std::move(address.error()));
		commands->SetComputeRootSignature(backend.root.Get());
		commands->SetComputeRootConstantBufferView(0, *address);
		commands->SetPipelineState(resolve_.Get());
		commands->Dispatch((backend.mask.width + 7) / 8, (backend.mask.height + 7) / 8, 1);
		return {};
	}
	void ShadowMapTechnique::Shutdown(ShadowBackend& backend) noexcept {
		backend.ReleaseTexture(depth_); resolve_.Reset();
	}
	void ShadowMapTechnique::CompleteRecording(const bool submitted) noexcept {
		if(recording_ && !submitted) depth_.state = initialState_;
		recording_ = false;
	}
}
