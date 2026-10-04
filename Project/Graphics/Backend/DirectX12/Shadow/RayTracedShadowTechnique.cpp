#include "ShadowBackend.h"

#include <cstring>
#include "Graphics/Renderer/GraphicsContext.h"

namespace NexusEngine {
	Result<void> RayTracedShadowTechnique::Initialize(ShadowBackend& backend) {
		Microsoft::WRL::ComPtr<ID3D12Device5> device;
		HRESULT result = backend.device.As(&device);
		if(FAILED(result)) return std::unexpected(MakeDirectXError(62, result, "DXR Device interface acquisition failed."));
		auto library = backend.Compile(L"RayTracing/Shadow.hlsl", L"", L"lib_6_6");
		if(!library) return std::unexpected(std::move(library.error()));
		const wchar_t* names[] { L"ShadowRaygen", L"ShadowMiss", L"ShadowClosestHit" };
		D3D12_EXPORT_DESC exports[3] {};
		for(size_t index = 0; index < 3; ++index) exports[index].Name = names[index];
		D3D12_DXIL_LIBRARY_DESC dxil {};
		dxil.DXILLibrary = { library->data(), library->size() };
		dxil.NumExports = 3; dxil.pExports = exports;
		D3D12_HIT_GROUP_DESC hit {};
		hit.HitGroupExport = L"ShadowHitGroup"; hit.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
		hit.ClosestHitShaderImport = L"ShadowClosestHit";
		D3D12_RAYTRACING_SHADER_CONFIG config { sizeof(uint32_t), sizeof(float) * 2 };
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig { 1 }; // Occlusion Ray has no recursive rays.
		ID3D12RootSignature* globalRoot = backend.root.Get();
		// One global root and one config apply to every export. No per-Instance local root
		// arguments are needed for opaque shadow rays, so all Instances reuse one HitGroup record.
		D3D12_STATE_SUBOBJECT subobjects[5] {};
		subobjects[0] = { D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &dxil };
		subobjects[1] = { D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit };
		subobjects[2] = { D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &config };
		subobjects[3] = { D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRoot };
		subobjects[4] = { D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineConfig };
		D3D12_STATE_OBJECT_DESC desc { D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, 5, subobjects };
		result = device->CreateStateObject(&desc, IID_PPV_ARGS(&pipeline_));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(62, result, "DXR shadow StateObject creation failed."));
		Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> properties;
		result = pipeline_.As(&properties);
		if(FAILED(result)) return std::unexpected(MakeDirectXError(62, result, "DXR StateObject properties acquisition failed."));
		// Shader identifiers are 32 bytes, records aligned to 32 bytes, table starts to 64.
		// Three separate 64-byte records keep all starts aligned and avoid local root arguments.
		constexpr uint64_t recordSize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
		D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC buffer {};
		buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = recordSize * 3;
		buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1; buffer.SampleDesc.Count = 1;
		buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&shaderTable_));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(62, result, "DXR Shader Table allocation failed."));
		uint8_t* mapped = nullptr;
		const D3D12_RANGE noReads { 0, 0 };
		result = shaderTable_->Map(0, &noReads, reinterpret_cast<void**>(&mapped));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(62, result, "DXR Shader Table Map failed."));
		std::memset(mapped, 0, static_cast<size_t>(buffer.Width));
		const wchar_t* tableExports[] { L"ShadowRaygen", L"ShadowMiss", L"ShadowHitGroup" };
		for(size_t index = 0; index < 3; ++index) {
			const void* identifier = properties->GetShaderIdentifier(tableExports[index]);
			if(!identifier) {
				shaderTable_->Unmap(0, nullptr);
				return std::unexpected(Error(ErrorCategory::Graphics, 62, "DXR shader export identifier is missing."));
			}
			std::memcpy(mapped + index * recordSize, identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		}
		shaderTable_->Unmap(0, nullptr);
		return scene_.Initialize(backend.device.Get(), backend.framesInFlight);
	}
	Result<void> RayTracedShadowTechnique::Render(ShadowBackend& backend, GraphicsContext& context,
		ID3D12GraphicsCommandList* nativeCommands, std::span<const ShadowDrawItem> draws,
		ShadowGpuConstants constants, const ShadowSettings&) {
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commands;
		const HRESULT result = nativeCommands->QueryInterface(IID_PPV_ARGS(&commands));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(62, result, "DXR Command List4 interface is unavailable."));
		// Write/allocate constants first; after AS recording all calls are non-failing command
		// encodes. This prevents a later allocation failure from dropping newly recorded BLAS.
		auto address = backend.WriteConstants(context.GetFrameIndex(), constants);
		if(!address) return std::unexpected(std::move(address.error()));
		auto update = scene_.Update(commands.Get(), context.GetFrameIndex(), draws);
		if(!update) return update;
		recording_ = true;
		commands->SetComputeRootSignature(backend.root.Get());
		commands->SetComputeRootConstantBufferView(0, *address);
		// Root SRV contains the TLAS GPU VA, not a CPU pointer or regular Buffer SRV.
		// No extra Descriptor Heap is created for DXR, and this value never crosses the Backend.
		commands->SetComputeRootShaderResourceView(1, scene_.GetTlasAddress(context.GetFrameIndex()));
		commands->SetPipelineState1(pipeline_.Get());
		constexpr uint64_t recordSize = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
		const auto base = shaderTable_->GetGPUVirtualAddress();
		D3D12_DISPATCH_RAYS_DESC dispatch {};
		dispatch.RayGenerationShaderRecord = { base, recordSize };
		dispatch.MissShaderTable = { base + recordSize, recordSize, recordSize };
		dispatch.HitGroupTable = { base + 2 * recordSize, recordSize, recordSize };
		dispatch.Width = backend.mask.width; dispatch.Height = backend.mask.height; dispatch.Depth = 1;
		commands->DispatchRays(&dispatch);
		return {};
	}
	void RayTracedShadowTechnique::Shutdown(ShadowBackend&) noexcept {
		scene_.Shutdown(); shaderTable_.Reset(); pipeline_.Reset();
	}
	void RayTracedShadowTechnique::CompleteRecording(const bool submitted, const uint32_t frame) noexcept {
		if(recording_ && !submitted) scene_.InvalidateRecording(frame);
		recording_ = false;
	}
}
