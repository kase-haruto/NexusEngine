#include "TransientDescriptorArena.h"

// c++
#include <utility>

// engine
#include "DescriptorAllocator.h"
#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidArguments = 1;
		constexpr int32_t kFrameOutOfRange = 2;
		constexpr int32_t kArenaExhausted = 3;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Heap上にFrameごとの固定領域を確保する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> TransientDescriptorArena::Initialize(
		ID3D12Device* const device,
		DescriptorAllocator* const allocator,
		const uint32_t frameCount,
		const uint32_t descriptorsPerFrame) {
		if(device == nullptr || allocator == nullptr || frameCount == 0 || descriptorsPerFrame == 0 ||
		   !allocator->IsShaderVisible() || allocator->GetHeapType() != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArguments,
				"Transient descriptor arena arguments are invalid."));
		}

		Shutdown();
		device_ = device;
		allocator_ = allocator;
		descriptorsPerFrame_ = descriptorsPerFrame;
		frames_.reserve(frameCount);

		for(uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
			auto allocation = allocator_->Allocate(descriptorsPerFrame_);
			if(!allocation) {
				// 初期化を原子的に見せるため、先に確保したFrame領域もすべて返却する。
				Shutdown();
				return std::unexpected(std::move(allocation.error()));
			}
			frames_.push_back(FrameArena { *allocation, 0 });
		}
		return {};
	}

	void TransientDescriptorArena::Shutdown() noexcept {
		if(allocator_ != nullptr) {
			for(const auto& frame : frames_) {
				if(!frame.allocation.IsValid()) continue;
				auto result = allocator_->Free(frame.allocation);
				if(!result) NEXUS_LOG_ERROR("Graphics", result.error().GetMessageText());
			}
		}
		frames_.clear();
		descriptorsPerFrame_ = 0;
		allocator_ = nullptr;
		device_ = nullptr;
	}

	void TransientDescriptorArena::ResetFrame(const uint32_t frameIndex) noexcept {
		if(frameIndex < frames_.size()) {
			// Descriptor内容は上書き時にCreate*Viewが更新するため、Heapのclearは不要。
			frames_[frameIndex].cursor = 0;
		}
	}

	Result<D3D12_GPU_DESCRIPTOR_HANDLE> TransientDescriptorArena::CreateConstantBufferView(
		const uint32_t frameIndex,
		const D3D12_GPU_VIRTUAL_ADDRESS bufferLocation,
		const uint32_t sizeInBytes) {
		if(device_ == nullptr || allocator_ == nullptr || bufferLocation == 0 || sizeInBytes == 0 ||
		   sizeInBytes % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT != 0) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArguments,
				"Transient constant buffer view arguments are invalid."));
		}
		if(frameIndex >= frames_.size()) {
			return std::unexpected(Error(ErrorCategory::Graphics, kFrameOutOfRange,
				"Transient descriptor frame index is out of range."));
		}

		auto& frame = frames_[frameIndex];
		if(frame.cursor >= descriptorsPerFrame_) {
			return std::unexpected(Error(ErrorCategory::Graphics, kArenaExhausted,
				"Transient resource descriptor capacity was exhausted for this frame."));
		}

		const auto cpuHandle = allocator_->GetCpuHandle(frame.allocation, frame.cursor);
		const auto gpuHandle = allocator_->GetGpuHandle(frame.allocation, frame.cursor);
		D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
		desc.BufferLocation = bufferLocation;
		desc.SizeInBytes = sizeInBytes;
		device_->CreateConstantBufferView(&desc, cpuHandle);

		// CreateConstantBufferViewはHRESULTを返さないため、入力検証後に初めてcursorを進める。
		++frame.cursor;
		return gpuHandle;
	}
} // namespace NexusEngine
