#include "DescriptorAllocator.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidArgument = 1;
		constexpr int32_t kHeapCreationFailed = 2;
		constexpr int32_t kCapacityExceeded = 3;
		constexpr int32_t kInvalidFree = 4;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// 固定容量Descriptor HeapとCPU側割当状態を初期化する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> DescriptorAllocator::Initialize(ID3D12Device* const device, const D3D12_DESCRIPTOR_HEAP_TYPE type, const uint32_t capacity, const bool shaderVisible) {
		// 部分初期化済みHeapへの再初期化を拒否し、既存Handleが無効になることを防ぐ。
		if(device == nullptr || capacity == 0 || heap_) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Descriptor allocator arguments are invalid."));
		}
		// RTV/DSV HeapはShader Visibleにできないため、不正な組合せを初期化時に拒否する。
		if(shaderVisible && type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && type != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Descriptor heap type cannot be shader visible."));
		}
		// Heap容量は初期化後に変更しない。Heap増設によるGPU Handle切替は将来の上位Arena責務とする。
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = type;
		desc.NumDescriptors = capacity;
		desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		const HRESULT result = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kHeapCreationFailed, result, "Failed to create descriptor heap."));
		}
		// Handleのbyte増分はHeap種別とDeviceに依存するため、固定値を仮定しない。
		descriptorSize_ = device->GetDescriptorHandleIncrementSize(type);
		shaderVisible_ = shaderVisible;
		allocated_.assign(capacity, false);
		return {};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// HeapとCPU側割当情報を破棄する
	/////////////////////////////////////////////////////////////////////////////////////////
	void DescriptorAllocator::Shutdown() noexcept {
		// AllocationはHeapの寿命を超えて使用できないため、GraphicsSystemが依存Resourceを先に破棄する。
		allocated_.clear();
		heap_.Reset();
		descriptorSize_ = 0;
		shaderVisible_ = false;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Heap内から要求数の連続Descriptor領域を確保する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<DescriptorAllocation> DescriptorAllocator::Allocate(const uint32_t count) {
		if(count == 0 || count > allocated_.size()) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Descriptor allocation count is invalid."));
		}
		// 第一段階は固定容量のfirst-fit。Heapを増設せずHandleの安定性を維持する。
		for(uint32_t begin = 0; begin + count <= allocated_.size(); ++begin) {
			bool available = true;
			// 全範囲が利用可能と確定してから割当状態を変更し、部分確保を残さない。
			for(uint32_t offset = 0; offset < count; ++offset) {
				available = available && !allocated_[begin + offset];
			}
			if(!available) {
				continue;
			}
			for(uint32_t offset = 0; offset < count; ++offset) {
				allocated_[begin + offset] = true;
			}
			DescriptorAllocation allocation;
			allocation.index = begin;
			allocation.count = count;
			allocation.cpuHandle = heap_->GetCPUDescriptorHandleForHeapStart();
			allocation.cpuHandle.ptr += static_cast<SIZE_T>(begin) * descriptorSize_;
			// RTV/DSVなどCPU専用HeapではGPU Handleを0のまま維持し、誤Bindを検出しやすくする。
			if(shaderVisible_) {
				allocation.gpuHandle = heap_->GetGPUDescriptorHandleForHeapStart();
				allocation.gpuHandle.ptr += static_cast<UINT64>(begin) * descriptorSize_;
			}
			return allocation;
		}
		return std::unexpected(Error(ErrorCategory::Graphics, kCapacityExceeded, "Descriptor heap capacity is exhausted."));
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// GPU利用完了済みDescriptor領域を再利用可能な状態へ戻す
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> DescriptorAllocator::Free(const DescriptorAllocation& allocation) {
		// Indexだけでなくcountも検証し、別Heap由来または破損した範囲による越境を防ぐ。
		if(allocation.count == 0 || allocation.index + allocation.count > allocated_.size()) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidFree, "Descriptor allocation range is invalid."));
		}
		// 一つでも未割当Indexを含む場合は状態を変更せず、二重解放として返す。
		for(uint32_t offset = 0; offset < allocation.count; ++offset) {
			if(!allocated_[allocation.index + offset]) {
				return std::unexpected(Error(ErrorCategory::Graphics, kInvalidFree, "Descriptor allocation is already free."));
			}
		}
		for(uint32_t offset = 0; offset < allocation.count; ++offset) {
			allocated_[allocation.index + offset] = false;
		}
		return {};
	}

	ID3D12DescriptorHeap* DescriptorAllocator::GetHeap() const noexcept { return heap_.Get(); }

} // namespace NexusEngine
