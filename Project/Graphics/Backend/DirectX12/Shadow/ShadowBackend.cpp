#include "ShadowBackend.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <string>

#include "Foundation/Logging/Logger.h"
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Descriptor/BindlessDescriptorTable.h"
#include "Graphics/Descriptor/DescriptorManager.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Renderer/GraphicsContext.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/Shader.h"

namespace NexusEngine {
	void ShadowBackend::ResetGeometry() noexcept { if(rayTracing_) rayTracing_->ResetGeometry(); }
	void ShadowBackend::RetainGeometry(std::span<const uint64_t> generations) { if(rayTracing_) rayTracing_->RetainGeometry(generations); }
	namespace {
		constexpr int32_t kShadowFailure = 60;
		void LogRayTracingError(const Error& error) {
			NEXUS_LOG_ERROR("RayTracing", error.GetMessageText() + " [code=" + std::to_string(error.GetCode()) +
				", native=" + std::to_string(error.GetNativeCode()) + "]");
		}
		Matrix4x4 MakeLightMatrix(const DirectionalLightData& light, const Matrix4x4& inverseVp, float extent) {
			// Fit a configurable orthographic volume around the camera frustum's center.
			// Directional lights have no position, so only the camera anchor and ray direction matter.
			Vector3 center;
			float w = inverseVp.At(3, 3) + 0.5f * inverseVp.At(2, 3);
			if(std::abs(w) > 1.0e-8f) {
				center = { (inverseVp.At(3, 0) + 0.5f * inverseVp.At(2, 0)) / w,
					(inverseVp.At(3, 1) + 0.5f * inverseVp.At(2, 1)) / w,
					(inverseVp.At(3, 2) + 0.5f * inverseVp.At(2, 2)) / w };
			}
			const Vector3 towardsLight { light.direction[0], light.direction[1], light.direction[2] };
			const Vector3 forward = towardsLight.Normalized() * -1.0f;
			const Vector3 up = std::abs(forward.y) > 0.99f ? Vector3::Right() : Vector3::Up();
			const Vector3 right = Vector3::Cross(up, forward).Normalized();
			const Vector3 correctedUp = Vector3::Cross(forward, right);
			const Vector3 eye = center - forward * extent;
			Matrix4x4 view;
			for(size_t row = 0; row < 3; ++row) {
				const float x[3] { right.x, right.y, right.z };
				const float y[3] { correctedUp.x, correctedUp.y, correctedUp.z };
				const float z[3] { forward.x, forward.y, forward.z };
				view.At(row, 0) = x[row]; view.At(row, 1) = y[row]; view.At(row, 2) = z[row];
			}
			view.At(3, 0) = -Vector3::Dot(eye, right);
			view.At(3, 1) = -Vector3::Dot(eye, correctedUp);
			view.At(3, 2) = -Vector3::Dot(eye, forward);
			Matrix4x4 projection;
			projection.At(0, 0) = 1.0f / extent;
			projection.At(1, 1) = 1.0f / extent;
			projection.At(2, 2) = 1.0f / (2.0f * extent);
			return Multiply(view, projection);
		}
	}

	Result<std::vector<uint8_t>> ShadowBackend::Compile(const wchar_t* relativePath, const wchar_t* entry, const wchar_t* profile) {
		const auto stage = profile[0] == L'l' ? ShaderStage::RayTracingLibrary :
			(profile[0] == L'c' ? ShaderStage::Compute : ShaderStage::Vertex);
		Shader shader;
		auto result = shader.Initialize({ shaderDirectory_ / relativePath, entry, profile }, stage);
		if(!result) return std::unexpected(std::move(result.error()));
		// The small fixed Shadow root contract is Backend-only, but it is still verified
		// against central Shader reflection so a shader edit cannot silently change registers.
		for(const auto& binding : shader.GetMetadata().resources) {
			const bool constant = binding.type == ShaderResourceType::ConstantBuffer && binding.bindPoint == 0 &&
				binding.constantBufferSize <= sizeof(ShadowGpuConstants);
			const bool acceleration = binding.type == ShaderResourceType::AccelerationStructure && binding.bindPoint == 0;
			if((!constant && !acceleration) || binding.registerSpace != 0 || binding.bindCount != 1) {
				return std::unexpected(Error(ErrorCategory::Graphics, kShadowFailure, "Shadow shader reflection does not match the Backend root binding contract."));
			}
		}
		const auto bytecode = shader.GetBytecode();
		return std::vector<uint8_t>(bytecode.begin(), bytecode.end());
	}

	Result<void> ShadowBackend::Initialize(void* nativeDevice, CommandQueue* nativeQueue,
		DescriptorManager* descriptorManager, BindlessDescriptorTable* bindlessTable,
		GraphicsResourceFactory* factory, uint32_t frameCount, GraphicsCapabilities caps,
		const std::filesystem::path& shaders) {
		if(!nativeDevice || !nativeQueue || !descriptorManager || !bindlessTable || !factory || frameCount == 0) {
			return std::unexpected(Error(ErrorCategory::Graphics, kShadowFailure, "Shadow backend dependencies are missing."));
		}
		device = static_cast<ID3D12Device*>(nativeDevice); queue = nativeQueue; descriptors = descriptorManager;
		bindless = bindlessTable; resources = factory; framesInFlight = frameCount;
		capabilities = caps; shaderDirectory_ = shaders;
		// Root CBV avoids allocating a Descriptor per shadow Draw. Two root parameters are
		// common to all techniques: b0 constants and t0 TLAS (unused by map/raster).
		// Direct Heap Indexing addresses SRV/UAV resources from the existing shared heap.
		D3D12_ROOT_PARAMETER1 parameters[2] {};
		parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		parameters[0].Descriptor.ShaderRegister = 0;
		parameters[0].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		parameters[1].Descriptor.ShaderRegister = 0;
		parameters[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC signature {};
		signature.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		signature.Desc_1_1.NumParameters = 2;
		signature.Desc_1_1.pParameters = parameters;
		signature.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
		Microsoft::WRL::ComPtr<ID3DBlob> serialized, errors;
		HRESULT nativeResult = D3D12SerializeVersionedRootSignature(&signature, &serialized, &errors);
		if(FAILED(nativeResult)) return std::unexpected(MakeDirectXError(kShadowFailure, nativeResult, "Shadow root signature serialization failed."));
		nativeResult = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root));
		if(FAILED(nativeResult)) return std::unexpected(MakeDirectXError(kShadowFailure, nativeResult, "Shadow root signature creation failed."));
		auto shader = Compile(L"Shadow/Depth.VS.hlsl", L"main", L"vs_6_6");
		if(!shader) return std::unexpected(std::move(shader.error()));
		// The same immutable layout as Forward ModelVertex. Depth does not need material attributes.
		const D3D12_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(ModelVertex, jointIndices), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(ModelVertex, jointWeights), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc {};
		desc.pRootSignature = root.Get(); desc.VS = { shader->data(), shader->size() };
		desc.InputLayout = { layout, 3 }; desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.SampleMask = UINT_MAX; desc.SampleDesc.Count = 1; desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		desc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_BACK, FALSE, 0, 0, 0, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF };
		for(auto& blend : desc.BlendState.RenderTarget) {
			blend.SrcBlend = D3D12_BLEND_ONE; blend.DestBlend = D3D12_BLEND_ZERO; blend.BlendOp = D3D12_BLEND_OP_ADD;
			blend.SrcBlendAlpha = D3D12_BLEND_ONE; blend.DestBlendAlpha = D3D12_BLEND_ZERO; blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			blend.LogicOp = D3D12_LOGIC_OP_NOOP; blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		}
		desc.DepthStencilState.DepthEnable = TRUE; desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		nativeResult = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&depthPipeline_));
		if(FAILED(nativeResult)) return std::unexpected(MakeDirectXError(kShadowFailure, nativeResult, "Shadow depth PSO creation failed."));
		map_ = std::make_unique<ShadowMapTechnique>();
		return map_->Initialize(*this);
	}

	Result<void> ShadowBackend::CreateTexture(ShadowTexture& texture, const uint32_t width, const uint32_t height, const bool depth) {
		D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC desc {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = width; desc.Height = height;
		desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1;
		desc.Format = depth ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32_FLOAT;
		desc.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		D3D12_CLEAR_VALUE clear {}; clear.Format = DXGI_FORMAT_D32_FLOAT; clear.DepthStencil.Depth = 1;
		texture.state = depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		const HRESULT result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			texture.state, depth ? &clear : nullptr, IID_PPV_ARGS(&texture.resource));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kShadowFailure, result, "Shadow texture allocation failed."));
		D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
		srv.Format = DXGI_FORMAT_R32_FLOAT; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
		auto view = bindless->CreateShaderResourceView(texture.resource.Get(), srv);
		if(!view) return std::unexpected(std::move(view.error()));
		texture.srv = *view;
		if(depth) {
			auto allocation = descriptors->DepthStencils().Allocate();
			if(!allocation) return std::unexpected(std::move(allocation.error()));
			texture.dsv = *allocation;
			D3D12_DEPTH_STENCIL_VIEW_DESC dsv {}; dsv.Format = DXGI_FORMAT_D32_FLOAT;
			dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			device->CreateDepthStencilView(texture.resource.Get(), &dsv, descriptors->DepthStencils().GetCpuHandle(texture.dsv));
		} else {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav {}; uav.Format = DXGI_FORMAT_R32_FLOAT;
			uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			auto allocation = bindless->CreateUnorderedAccessView(texture.resource.Get(), nullptr, uav);
			if(!allocation) return std::unexpected(std::move(allocation.error()));
			texture.uav = *allocation;
		}
		texture.width = width; texture.height = height;
		return {};
	}
	void ShadowBackend::ReleaseTexture(ShadowTexture& texture) noexcept {
		// Caller guarantees GPU idle. Retire through the existing generation-aware path,
		// even though this Fence is already completed, to avoid bypassing allocator bookkeeping.
		if(bindless && queue) {
			for(auto reference : { texture.srv, texture.uav }) {
				if(!reference.IsValid()) continue;
				auto result = bindless->Retire(reference, queue->GetCompletedFenceValue());
				if(!result) NEXUS_LOG_ERROR("Shadow", result.error().GetMessageText());
			}
			bindless->CollectGarbage(queue->GetCompletedFenceValue());
		}
		if(descriptors && texture.dsv.count != 0) {
			auto result = descriptors->DepthStencils().Free(texture.dsv);
			if(!result) NEXUS_LOG_ERROR("Shadow", result.error().GetMessageText());
		}
		texture = {};
	}
	void ShadowBackend::Transition(ID3D12GraphicsCommandList* commands, ShadowTexture& texture, D3D12_RESOURCE_STATES state) {
		if(texture.state == state) return;
		D3D12_RESOURCE_BARRIER barrier {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texture.resource.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = texture.state; barrier.Transition.StateAfter = state;
		commands->ResourceBarrier(1, &barrier);
		texture.state = state;
	}
	Result<uint64_t> ShadowBackend::WriteConstants(const uint32_t frame, const ShadowGpuConstants& constants) {
		if(constantCursor_ == constants_.size()) {
			auto buffer = std::make_unique<ConstantBuffer>();
			auto result = resources->CreateConstantBuffer(*buffer, sizeof(ShadowGpuConstants));
			if(!result) return std::unexpected(std::move(result.error()));
			constants_.push_back(std::move(buffer));
		}
		auto& buffer = *constants_[constantCursor_++];
		auto result = buffer.Write(frame, { reinterpret_cast<const uint8_t*>(&constants), sizeof(constants) });
		if(!result) return std::unexpected(std::move(result.error()));
		return buffer.GetGpuAddress(frame);
	}
	Result<void> ShadowBackend::Rasterize(GraphicsContext& context, ID3D12GraphicsCommandList* commands,
		ShadowTexture& target, const Matrix4x4& viewProjection, std::span<const ShadowDrawItem> draws, ShadowGpuConstants constants) {
		Transition(commands, target, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		const auto dsv = descriptors->DepthStencils().GetCpuHandle(target.dsv);
		commands->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
		commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
		const D3D12_VIEWPORT viewport { 0, 0, static_cast<float>(target.width), static_cast<float>(target.height), 0, 1 };
		const D3D12_RECT scissor { 0, 0, static_cast<LONG>(target.width), static_cast<LONG>(target.height) };
		commands->RSSetViewports(1, &viewport); commands->RSSetScissorRects(1, &scissor);
		commands->SetGraphicsRootSignature(root.Get()); commands->SetPipelineState(depthPipeline_.Get());
		context.SetPrimitiveTopology(PrimitiveTopology::TriangleList);
		for(const auto& draw : draws) {
			if(!draw.mesh || draw.jointPalette.size() > constants.jointPalette.size()) {
				return std::unexpected(Error(ErrorCategory::Graphics, kShadowFailure, "Invalid shadow mesh/palette."));
			}
			constants.worldViewProjection = Multiply(draw.world, viewProjection);
			constants.dimensions[2] = draw.jointPalette.empty() ? 0U : 1U;
			std::copy(draw.jointPalette.begin(), draw.jointPalette.end(), constants.jointPalette.begin());
			auto address = WriteConstants(context.GetFrameIndex(), constants);
			if(!address) return std::unexpected(std::move(address.error()));
			commands->SetGraphicsRootConstantBufferView(0, *address);
			context.SetMesh(*draw.mesh);
			for(const auto& submesh : draw.mesh->GetSubmeshes()) {
				context.DrawIndexed(submesh.indexCount, 1, submesh.firstIndex, submesh.vertexOffset);
			}
		}
		// Depth Write must complete before Compute/Raygen reads camera surface/light map.
		Transition(commands, target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		return {};
	}

	Result<ShaderResourceRef> ShadowBackend::Render(GraphicsContext& context, void* nativeCommands,
		const Matrix4x4& vp, const DirectionalLightData& light, const std::span<const ShadowDrawItem> draws,
		const ShadowSettings& settings) {
		auto* commands = static_cast<ID3D12GraphicsCommandList*>(nativeCommands);
		constantCursor_ = 0;
		if(!settings.enabled || draws.empty()) {
			selected_ = ShadowTechnique::Disabled; current_ = nullptr; fallback_ = false;
			return ShaderResourceRef {};
		}
		const auto inverse = TryInverse(vp);
		const Vector3 lightVector { light.direction[0], light.direction[1], light.direction[2] };
		if(!inverse || !std::isfinite(lightVector.LengthSquared()) || lightVector.LengthSquared() <= 0 ||
			!std::isfinite(light.angularRadius) || light.angularRadius < 0 || light.angularRadius > 1.57f) {
			return std::unexpected(Error(ErrorCategory::Graphics, kShadowFailure, "Invalid shadow camera or light."));
		}
		if(!surface.resource || !mask.resource || !surface.srv.IsValid() || !mask.uav.IsValid() ||
			surface.width != context.GetRenderWidth() || surface.height != context.GetRenderHeight() ||
			mask.width != context.GetRenderWidth() || mask.height != context.GetRenderHeight()) {
			// Resize is infrequent; keep the existing engine's Queue-idle resize contract.
			// Shared textures are ordered on the Direct Queue; Frame constants/TLAS stay separate.
			auto wait = queue->WaitForIdle(); if(!wait) return std::unexpected(std::move(wait.error()));
			ReleaseTexture(surface); ReleaseTexture(mask);
			auto result = CreateTexture(surface, context.GetRenderWidth(), context.GetRenderHeight(), true);
			if(!result) return std::unexpected(std::move(result.error()));
			result = CreateTexture(mask, context.GetRenderWidth(), context.GetRenderHeight(), false);
			if(!result) return std::unexpected(std::move(result.error()));
		}
		const bool wasFallback = fallback_;
		const bool wantRt = settings.mode != ShadowMode::ShadowMap;
		// Existing Skinning is Vertex Shader-only. Until a deformed Geometry buffer is
		// available, Map rasterizes the same palette instead of tracing incorrect bind-pose BLAS.
		const bool skinned = std::any_of(draws.begin(), draws.end(), [](const auto& draw) { return !draw.jointPalette.empty(); });
		fallback_ = wantRt && (!capabilities.SupportsRayTracing() || rtFailed_ || skinned);
		if(wantRt && !fallback_ && !rayTracing_) {
			rayTracing_ = std::make_unique<RayTracedShadowTechnique>();
			auto result = rayTracing_->Initialize(*this);
			if(!result) {
				LogRayTracingError(result.error());
				rtFailed_ = true; fallback_ = true;
			}
		}
		current_ = wantRt && !fallback_ ? static_cast<IShadowTechnique*>(rayTracing_.get()) : map_.get();
		if(fallback_ && (!wasFallback || previousMode_ != settings.mode || selected_ != current_->GetTechnique())) {
			NEXUS_LOG_WARNING("Shadow", "Ray Tracing unavailable. Falling back to Shadow Map.");
			if(skinned) NEXUS_LOG_WARNING("Shadow", "Skinned geometry has no deformed AS input buffer; using rasterized palette shadows.");
		}
		previousMode_ = settings.mode;
		if(selected_ != current_->GetTechnique()) {
			selected_ = current_->GetTechnique();
			NEXUS_LOG_INFO("Shadow", selected_ == ShadowTechnique::RayTracing ? "Selected Shadow Technique: RayTracing." : "Selected Shadow Technique: ShadowMap.");
		}
		ShadowGpuConstants constants;
		initialSurfaceState_ = surface.state;
		initialMaskState_ = mask.state;
		recording_ = true;
		constants.inverseViewProjection = *inverse;
		constants.lightViewProjection = MakeLightMatrix(light, *inverse, settings.shadowMapExtent);
		const auto direction = lightVector.Normalized();
		constants.lightDirectionAngularRadius = { direction.x, direction.y, direction.z, settings.softShadow ? light.angularRadius : 0.0f };
		constants.biases = { settings.rayOriginBias, settings.rayTMin, settings.rayTMax, settings.shadowMapBias };
		constants.indices = { surface.srv.index, 0, mask.uav.index, settings.softShadow ? settings.sampleCount : 1U };
		constants.dimensions = { mask.width, mask.height, 0, 0 };
		auto result = Rasterize(context, commands, surface, vp, draws, constants);
		if(!result) return std::unexpected(std::move(result.error()));
		Transition(commands, mask, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		result = current_->Render(*this, context, commands, draws, constants, settings);
		if(!result && selected_ == ShadowTechnique::RayTracing) {
			LogRayTracingError(result.error());
			NEXUS_LOG_WARNING("Shadow", "Ray Tracing unavailable. Falling back to Shadow Map.");
			rtFailed_ = true; fallback_ = true; current_ = map_.get(); selected_ = ShadowTechnique::ShadowMap;
			result = current_->Render(*this, context, commands, draws, constants, settings);
		}
		if(!result) return std::unexpected(std::move(result.error()));
		// UAV->SRV transition orders all writes and makes visibility readable by Forward PS.
		Transition(commands, mask, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		return mask.srv;
	}
	ShadowStatistics ShadowBackend::GetStatistics() const noexcept {
		auto statistics = rayTracing_ ? rayTracing_->GetStatistics() : ShadowStatistics {};
		statistics.technique = selected_; statistics.fallback = fallback_;
		if(selected_ != ShadowTechnique::RayTracing) statistics.tlasInstanceCount = 0;
		return statistics;
	}
	void ShadowBackend::CompleteRecording(void* const owner, const bool submitted, const uint32_t frame) noexcept {
		auto& backend = *static_cast<ShadowBackend*>(owner);
		if(backend.recording_ && !submitted) {
			backend.surface.state = backend.initialSurfaceState_;
			backend.mask.state = backend.initialMaskState_;
		}
		if(backend.map_) backend.map_->CompleteRecording(submitted);
		if(backend.rayTracing_) backend.rayTracing_->CompleteRecording(submitted, frame);
		backend.recording_ = false;
	}
	void ShadowBackend::Shutdown() noexcept {
		// Resource owners can be torn down after Frame commands have been submitted, so
		// synchronize here as well as in GraphicsSystem shutdown. Partial init has no GPU work.
		if(queue && device) {
			auto wait = queue->WaitForIdle();
			if(!wait) NEXUS_LOG_ERROR("Shadow", wait.error().GetMessageText());
		}
		if(rayTracing_) rayTracing_->Shutdown(*this);
		if(map_) map_->Shutdown(*this);
		rayTracing_.reset(); map_.reset(); current_ = nullptr;
		ReleaseTexture(surface); ReleaseTexture(mask); constants_.clear();
		depthPipeline_.Reset(); root.Reset(); device.Reset();
		resources = nullptr; queue = nullptr; descriptors = nullptr; bindless = nullptr;
	}
}
