#include "RayTracingScene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "Graphics/Resource/MeshResource.h"
#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidInput = 40;
		constexpr int32_t kAsAllocationFailed = 41;
		constexpr int32_t kAsMapFailed = 42;

		Result<void> CreateBuffer(ID3D12Device* device, uint64_t size, D3D12_HEAP_TYPE heapType,
			D3D12_RESOURCE_STATES state, Microsoft::WRL::ComPtr<ID3D12Resource>& resource) {
			D3D12_HEAP_PROPERTIES heap {};
			heap.Type = heapType;
			D3D12_RESOURCE_DESC desc {};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = size;
			desc.Height = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.SampleDesc.Count = 1;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			// Build result/scratchはUAV書込み対象。Upload InstanceはCPU書込み専用。
			desc.Flags = heapType == D3D12_HEAP_TYPE_DEFAULT ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
			const HRESULT result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
				&desc, state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS ? D3D12_RESOURCE_STATE_COMMON : state,
				nullptr, IID_PPV_ARGS(&resource));
			if(FAILED(result)) {
				NEXUS_LOG_ERROR("RayTracing", "Failed to allocate acceleration structure result/scratch/instance buffer.");
				return std::unexpected(MakeDirectXError(kAsAllocationFailed, result, "AS buffer allocation failed."));
			}
			return {};
		}
		void UavBarrier(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource) {
			// AS ResourceはRAYTRACING_ACCELERATION_STRUCTUREから遷移しない。
			// UAV orderingでBuild書込みがTLAS Build/DispatchRaysの読みに先行することを保証する。
			D3D12_RESOURCE_BARRIER barrier {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			barrier.UAV.pResource = resource;
			commands->ResourceBarrier(1, &barrier);
		}
		void ScratchBarrier(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource, bool begin) {
			// Modern runtimes create ordinary Buffers in COMMON regardless of InitialState.
			// Explicitly bracket Build scratch usage and return to COMMON, so the next slot
			// reuse does not depend on implicit promotion/decay behavior of GPU VA accesses.
			D3D12_RESOURCE_BARRIER barrier {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = resource;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = begin ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			barrier.Transition.StateAfter = begin ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COMMON;
			commands->ResourceBarrier(1, &barrier);
		}
	}

	Result<void> RayTracingScene::Initialize(ID3D12Device* device, const uint32_t frameCount) {
		if(device == nullptr || frameCount == 0 || device_) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidInput, "Invalid ray tracing scene initialization."));
		}
		const HRESULT result = device->QueryInterface(IID_PPV_ARGS(&device_));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kInvalidInput, result, "ID3D12Device5 is unavailable."));
		frames_.resize(frameCount);
		return {};
	}

	Result<std::shared_ptr<RayTracingScene::Blas>> RayTracingScene::GetOrCreateBlas(const MeshResource& mesh) {
		const uint64_t generation = mesh.GetGeometryGeneration();
		if(auto found = cache_.find(generation); found != cache_.end()) return found->second;
		if(!mesh.IsInitialized() || generation == 0 || mesh.vertexBuffer_.GetStride() < sizeof(float) * 3) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidInput, "BLAS requires a valid triangle Mesh with float3 position at offset zero."));
		}
		auto blas = std::make_shared<Blas>();
		// Copying COM ownership is essential: a Scene snapshot is non-owning, but the GPU may
		// still read these Buffers after an Asset is replaced on the CPU.
		blas->vertices = static_cast<ID3D12Resource*>(mesh.vertexBuffer_.GetNativeResource());
		blas->indices = static_cast<ID3D12Resource*>(mesh.indexBuffer_.GetNativeResource());
		for(const SubmeshRange& range : mesh.submeshes_) {
			if(range.indexCount % 3 != 0 || range.vertexOffset < 0 ||
				static_cast<uint32_t>(range.vertexOffset) >= mesh.vertexBuffer_.GetVertexCount()) {
				return std::unexpected(Error(ErrorCategory::Graphics, kInvalidInput, "BLAS submesh must contain triangles and a valid non-negative vertex offset."));
			}
			D3D12_RAYTRACING_GEOMETRY_DESC geometry {};
			geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			// Current Material pipeline is opaque. Alpha test/procedural hit evaluation requires
			// a separate future geometry classification, not silent treatment as opaque foliage.
			geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			geometry.Triangles.VertexCount = mesh.vertexBuffer_.GetVertexCount() - static_cast<uint32_t>(range.vertexOffset);
			geometry.Triangles.VertexBuffer.StartAddress = blas->vertices->GetGPUVirtualAddress() +
				static_cast<uint64_t>(range.vertexOffset) * mesh.vertexBuffer_.GetStride();
			geometry.Triangles.VertexBuffer.StrideInBytes = mesh.vertexBuffer_.GetStride();
			const bool wideIndex = mesh.indexBuffer_.GetFormat() == IndexFormat::UInt32;
			geometry.Triangles.IndexFormat = wideIndex ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
			geometry.Triangles.IndexCount = range.indexCount;
			geometry.Triangles.IndexBuffer = blas->indices->GetGPUVirtualAddress() +
				static_cast<uint64_t>(range.firstIndex) * (wideIndex ? 4U : 2U);
			blas->geometries.push_back(geometry);
		}
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs {};
		inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		inputs.NumDescs = static_cast<UINT>(blas->geometries.size());
		inputs.pGeometryDescs = blas->geometries.data();
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes {};
		device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &sizes);
		if(sizes.ResultDataMaxSizeInBytes == 0 || sizes.ScratchDataSizeInBytes == 0) {
			return std::unexpected(Error(ErrorCategory::Graphics, kAsAllocationFailed, "BLAS prebuild sizes are zero; geometry or device support is invalid."));
		}
		auto result = CreateBuffer(device_.Get(), sizes.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, blas->result);
		if(!result) return std::unexpected(std::move(result.error()));
		result = CreateBuffer(device_.Get(), sizes.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, blas->scratch);
		if(!result) return std::unexpected(std::move(result.error()));
		cache_.emplace(generation, blas);
		return blas;
	}

	Result<void> RayTracingScene::Update(ID3D12GraphicsCommandList4* commands, const uint32_t frameIndex,
		const std::span<const ShadowDrawItem> instances, const std::span<const uint32_t> hitGroupOffsets) {
		if(!device_ || commands == nullptr || frameIndex >= frames_.size() || instances.size() > 0xFFFFFFU) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidInput, "Invalid TLAS command context or instance count."));
		}
		auto& frame = frames_[frameIndex];
		// The caller waited this slot's Fence, so the previous TLAS and its BLAS references
		// are no longer executing. Other slots keep their own shared references independently.
		frame.current.clear();
		frame.references.clear();
		frame.pendingBuilds.clear();
		frame.pendingBuilds.reserve(instances.size());
		frame.references.reserve(instances.size());
		frame.current.reserve(instances.size());
		for(const auto& instance : instances) {
			if(instance.mesh == nullptr || !instance.jointPalette.empty()) {
				return std::unexpected(Error(ErrorCategory::Graphics, kInvalidInput,
					"RT Geometry requires a Mesh; skinned geometry must provide a deformed buffer before AS build."));
			}
			for(const float value : instance.world.elements) {
				if(!std::isfinite(value)) return std::unexpected(Error(ErrorCategory::Graphics, kInvalidInput, "TLAS instance transform is not finite."));
			}
			auto blas = GetOrCreateBlas(*instance.mesh);
			if(!blas) return std::unexpected(std::move(blas.error()));
			D3D12_RAYTRACING_INSTANCE_DESC desc {};
			// Engine uses row-vector matrices. DXR consumes a row-major 3x4 column-vector
			// affine transform, so transpose the upper three output coordinates including translation.
			for(size_t row = 0; row < 3; ++row) {
				for(size_t column = 0; column < 4; ++column) desc.Transform[row][column] = instance.world.At(column, row);
			}
			desc.InstanceID = static_cast<UINT>(frame.current.size());
			// Material-aware passes map each BLAS geometry to a local ShaderTable record.
			// Shadows leave offsets empty and reuse their single occlusion hit group.
			if(!hitGroupOffsets.empty()) {
				if(hitGroupOffsets.size() != instances.size() || hitGroupOffsets[frame.current.size()] > 0xFFFFFFU)
					return std::unexpected(Error(ErrorCategory::Graphics, kInvalidInput, "Invalid material hit-group offsets."));
				desc.InstanceContributionToHitGroupIndex = hitGroupOffsets[frame.current.size()];
			}
			desc.InstanceMask = 0xFF;
			desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
			desc.AccelerationStructure = (*blas)->result->GetGPUVirtualAddress();
			frame.current.push_back(desc);
			frame.references.push_back(*blas);
		}
		statistics_.tlasInstanceCount = static_cast<uint32_t>(frame.current.size());
		if(frame.current.empty()) {
			frame.previous.clear();
			frame.built = false;
			cache_.clear(); // Other frame references retain BLAS until their own Fence completes.
			statistics_.blasCount = 0;
			return {};
		}
		const bool sameCount = frame.previous.size() == frame.current.size();
		bool topologyUnchanged = sameCount;
		if(sameCount) {
			for(size_t index = 0; index < frame.current.size(); ++index) {
				if(frame.previous[index].AccelerationStructure != frame.current[index].AccelerationStructure) topologyUnchanged = false;
			}
		}
		const bool unchanged = frame.built && sameCount && std::memcmp(frame.previous.data(), frame.current.data(),
			frame.current.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC)) == 0;
		if(!unchanged) {
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs {};
			inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			inputs.NumDescs = static_cast<UINT>(frame.current.size());
			inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes {};
			device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &sizes);
			const uint64_t scratchSize = (std::max)(sizes.ScratchDataSizeInBytes, sizes.UpdateScratchDataSizeInBytes);
			if(sizes.ResultDataMaxSizeInBytes == 0 || scratchSize == 0) {
				return std::unexpected(Error(ErrorCategory::Graphics, kAsAllocationFailed, "TLAS prebuild returned invalid sizes."));
			}
			const uint64_t uploadSize = frame.current.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
			bool reallocated = false;
			if(!frame.result || frame.result->GetDesc().Width < sizes.ResultDataMaxSizeInBytes) {
				auto result = CreateBuffer(device_.Get(), sizes.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
					D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, frame.result);
				if(!result) return result;
				reallocated = true;
			}
			if(!frame.scratch || frame.scratch->GetDesc().Width < scratchSize) {
				auto result = CreateBuffer(device_.Get(), scratchSize, D3D12_HEAP_TYPE_DEFAULT,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS, frame.scratch);
				if(!result) return result;
			}
			if(!frame.instances || frame.instances->GetDesc().Width < uploadSize) {
				auto result = CreateBuffer(device_.Get(), uploadSize, D3D12_HEAP_TYPE_UPLOAD,
					D3D12_RESOURCE_STATE_GENERIC_READ, frame.instances);
				if(!result) return result;
			}
			void* mapped = nullptr;
			const D3D12_RANGE noCpuReads { 0, 0 };
			const HRESULT mapResult = frame.instances->Map(0, &noCpuReads, &mapped);
			if(FAILED(mapResult)) return std::unexpected(MakeDirectXError(kAsMapFailed, mapResult, "TLAS instance upload Map failed."));
			std::memcpy(mapped, frame.current.data(), static_cast<size_t>(uploadSize));
			frame.instances->Unmap(0, nullptr);

			// Allocate all dependencies before issuing any Build. Once a command refers to a
			// Resource, the Frame/cache retains it even if subsequent shadow recording fails.
			for(auto& blas : frame.references) {
				if(blas->built) continue;
				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build {};
				build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
				build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
				build.Inputs.NumDescs = static_cast<UINT>(blas->geometries.size());
				build.Inputs.pGeometryDescs = blas->geometries.data();
				build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
				build.DestAccelerationStructureData = blas->result->GetGPUVirtualAddress();
				build.ScratchAccelerationStructureData = blas->scratch->GetGPUVirtualAddress();
				ScratchBarrier(commands, blas->scratch.Get(), true);
				commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
				ScratchBarrier(commands, blas->scratch.Get(), false);
				UavBarrier(commands, blas->result.Get());
				blas->built = true;
				frame.pendingBuilds.push_back(blas);
				++statistics_.blasBuildCount;
			}
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build {};
			build.Inputs = inputs;
			build.Inputs.InstanceDescs = frame.instances->GetGPUVirtualAddress();
			build.DestAccelerationStructureData = frame.result->GetGPUVirtualAddress();
			build.ScratchAccelerationStructureData = frame.scratch->GetGPUVirtualAddress();
			if(frame.built && topologyUnchanged && !reallocated) {
				// ALLOW_UPDATE was set on the original build. Updating in-place is safe only
				// because this FrameContext's previous GPU work has completed.
				build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
				build.SourceAccelerationStructureData = build.DestAccelerationStructureData;
				++statistics_.tlasUpdateCount;
			} else {
				++statistics_.tlasBuildCount;
			}
			ScratchBarrier(commands, frame.scratch.Get(), true);
			commands->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
			ScratchBarrier(commands, frame.scratch.Get(), false);
			UavBarrier(commands, frame.result.Get());
			frame.previous.assign(frame.current.begin(), frame.current.end());
			frame.built = true;
		}
		// Remove unused cache entries without deleting BLAS still referenced by in-flight
		// TLAS slots. Avoid rescanning Scene; current Frame references are the authoritative set.
		for(auto it = cache_.begin(); it != cache_.end();) {
			if(it->second.use_count() == 1) it = cache_.erase(it);
			else ++it;
		}
		statistics_.blasCount = static_cast<uint32_t>(cache_.size());
		return {};
	}

	void RayTracingScene::InvalidateRecording(const uint32_t frameIndex) noexcept {
		if(frameIndex >= frames_.size()) return;
		auto& frame = frames_[frameIndex];
		for(auto& blas : frame.pendingBuilds) blas->built = false;
		frame.pendingBuilds.clear();
		frame.built = false;
	}
	D3D12_GPU_VIRTUAL_ADDRESS RayTracingScene::GetTlasAddress(const uint32_t frameIndex) const noexcept {
		return frameIndex < frames_.size() && frames_[frameIndex].built && frames_[frameIndex].result
			? frames_[frameIndex].result->GetGPUVirtualAddress() : 0;
	}
	ShadowStatistics RayTracingScene::GetStatistics() const noexcept { return statistics_; }
	void RayTracingScene::Shutdown() noexcept {
		frames_.clear();
		cache_.clear();
		device_.Reset();
		statistics_ = {};
	}
	void RayTracingScene::ResetGeometry() noexcept {
		for(auto& frame : frames_) frame = Frame{};
		cache_.clear();
		statistics_ = {};
	}
	void RayTracingScene::RetainGeometry(std::span<const uint64_t> generations) {
		for(auto& frame : frames_) {
			frame.references.clear(); frame.pendingBuilds.clear(); frame.previous.clear(); frame.current.clear(); frame.built = false;
		}
		std::erase_if(cache_, [&](const auto& entry) {
			return std::find(generations.begin(), generations.end(), entry.first) == generations.end();
		});
		statistics_.blasCount = static_cast<uint32_t>(cache_.size());
	}
}
