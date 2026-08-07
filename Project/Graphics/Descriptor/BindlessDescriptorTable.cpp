#include "BindlessDescriptorTable.h"

#include <utility>

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidArgument = 1;
		constexpr int32_t kInvalidReference = 2;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Direct Heap Indexing用Heapと安全なnull slotを初期化する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> BindlessDescriptorTable::Initialize(
		ID3D12Device* const device,
		DescriptorManager* const descriptors) {
		if(device == nullptr || descriptors == nullptr || device_ != nullptr) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Bindless descriptor table arguments are invalid."));
		}

		// ResourceとSamplerはDirectX 12で異なるHeapを要求されるため、容量と割当状態も分離する。
		descriptors_ = descriptors;
		resourceSlots_.resize(descriptors_->Resources().GetCapacity());
		samplerSlots_.resize(descriptors_->Samplers().GetCapacity());
		device_ = device;

		// 最初のAllocationがindex 0になることを利用し、無効参照が安全に指せるnull Descriptorを予約する。
		DescriptorAllocation nullResource;
		auto nullResourceResult = descriptors_->Resources().Allocate();
		if(!nullResourceResult || nullResourceResult->index != 0) {
			Shutdown();
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Failed to reserve null resource descriptor."));
		}
		nullResource = *nullResourceResult;
		D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
		nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		nullSrv.Texture2D.MipLevels = 1;
		device_->CreateShaderResourceView(nullptr, &nullSrv, nullResource.cpuHandle);
		resourceSlots_[0].active = true;

		DescriptorAllocation nullSampler;
		auto nullSamplerResult = descriptors_->Samplers().Allocate();
		if(!nullSamplerResult || nullSamplerResult->index != 0) {
			Shutdown();
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Failed to reserve null sampler descriptor."));
		}
		nullSampler = *nullSamplerResult;
		D3D12_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		device_->CreateSampler(&sampler, nullSampler.cpuHandle);
		samplerSlots_[0].active = true;
		return {};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// 全Bindless Descriptor状態を破棄する
	/////////////////////////////////////////////////////////////////////////////////////////
	void BindlessDescriptorTable::Shutdown() noexcept {
		// GPU完了は所有者が保証する。retire待ちを含む全HandleはHeap破棄と同時に無効になる。
		retired_.clear();
		resourceSlots_.clear();
		samplerSlots_.clear();
		descriptors_ = nullptr;
		device_ = nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Resource HeapへSRVを生成して世代付き参照を返す
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<ShaderResourceRef> BindlessDescriptorTable::CreateShaderResourceView(ID3D12Resource* const resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc) {
		// Descriptor APIはvoidを返すため、先に有効なDeviceと確保済みCPU Handleを保証する。
		DescriptorAllocation allocation;
		auto reference = Allocate(ShaderResourceClass::Resource, allocation);
		if(!reference) return reference;
		device_->CreateShaderResourceView(resource, &desc, allocation.cpuHandle);
		return reference;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Resource HeapへUAVを生成して世代付き参照を返す
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<ShaderResourceRef> BindlessDescriptorTable::CreateUnorderedAccessView(ID3D12Resource* const resource, ID3D12Resource* const counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc) {
		DescriptorAllocation allocation;
		auto reference = Allocate(ShaderResourceClass::Resource, allocation);
		if(!reference) return reference;
		device_->CreateUnorderedAccessView(resource, counterResource, &desc, allocation.cpuHandle);
		return reference;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Resource HeapへCBVを生成して世代付き参照を返す
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<ShaderResourceRef> BindlessDescriptorTable::CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc) {
		DescriptorAllocation allocation;
		auto reference = Allocate(ShaderResourceClass::Resource, allocation);
		if(!reference) return reference;
		device_->CreateConstantBufferView(&desc, allocation.cpuHandle);
		return reference;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Sampler HeapへSamplerを生成して世代付き参照を返す
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<ShaderResourceRef> BindlessDescriptorTable::CreateSampler(const D3D12_SAMPLER_DESC& desc) {
		DescriptorAllocation allocation;
		auto reference = Allocate(ShaderResourceClass::Sampler, allocation);
		if(!reference) return reference;
		device_->CreateSampler(&desc, allocation.cpuHandle);
		return reference;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// 参照を即時解放せず、最後のGPU利用Fenceとともにretire queueへ積む
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> BindlessDescriptorTable::Retire(const ShaderResourceRef reference, const uint64_t retireFenceValue) {
		if(!IsValid(reference) || retireFenceValue == 0) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidReference, "Bindless descriptor reference or retire fence is invalid."));
		}
		// Retire要求直後からCPU側では無効とし、新しいCommandが古い参照を記録できないようにする。
		auto& slot = GetSlots(reference.resourceClass)[reference.index];
		slot.active = false;
		slot.retiring = true;
		retired_.push_back({ reference, retireFenceValue });
		return {};
	}

	void BindlessDescriptorTable::CollectGarbage(const uint64_t completedFenceValue) noexcept {
		// 完了Fenceへ到達したslotだけAllocatorへ返し、古いCommandからの参照中再利用を防ぐ。
		for(size_t index = 0; index < retired_.size();) {
			const auto retired = retired_[index];
			if(retired.fenceValue > completedFenceValue) {
				++index;
				continue;
			}
			auto& slot = GetSlots(retired.reference.resourceClass)[retired.reference.index];
			// DescriptorAllocator::FreeはHandle値を必要とせず、元のindex/countだけで割当状態を戻す。
			DescriptorAllocation allocation { .index = retired.reference.index, .count = 1 };
			static_cast<void>(GetAllocator(retired.reference.resourceClass).Free(allocation));
			slot.retiring = false;
			// 世代を進めることで、同じindexを保持する古いShaderResourceRefをCPU側で拒否できる。
			++slot.generation;
			if(slot.generation == 0) ++slot.generation;
			// 順序を必要としないqueueなのでswap-popし、eraseによる全要素移動を避ける。
			retired_[index] = retired_.back();
			retired_.pop_back();
		}
	}

	bool BindlessDescriptorTable::IsValid(const ShaderResourceRef reference) const noexcept {
		// index 0はGPU上では安全なnullだが、所有Resource参照としては無効扱いにする。
		if(reference.index == 0 || reference.generation == 0) return false;
		const auto& slots = reference.resourceClass == ShaderResourceClass::Resource ? resourceSlots_ : samplerSlots_;
		return reference.index < slots.size() && slots[reference.index].active && !slots[reference.index].retiring &&
		       slots[reference.index].generation == reference.generation;
	}

	void BindlessDescriptorTable::Bind(ID3D12GraphicsCommandList* const commandList) const noexcept {
		// Direct Heap Indexing Shaderはこの2 HeapをResourceDescriptorHeap/SamplerDescriptorHeapとして参照する。
		if(descriptors_ != nullptr) descriptors_->BindShaderVisibleHeaps(commandList);
	}

	Result<ShaderResourceRef> BindlessDescriptorTable::Allocate(const ShaderResourceClass resourceClass, DescriptorAllocation& allocation) {
		// Heap選択ルールを一箇所へ集約し、Resource/Samplerの取り違えを各生成関数へ散らさない。
		auto allocationResult = GetAllocator(resourceClass).Allocate();
		if(!allocationResult) return std::unexpected(std::move(allocationResult.error()));
		allocation = *allocationResult;
		auto& slot = GetSlots(resourceClass)[allocation.index];
		// GPUへDescriptorを書き込む前にslotを確保するが、生成APIはvoidかつ失敗しない契約なので
		// この関数の成功後に各Create*Viewを実行すれば部分公開状態は残らない。
		slot.active = true;
		slot.retiring = false;
		return ShaderResourceRef { allocation.index, slot.generation, resourceClass };
	}

	std::vector<BindlessDescriptorTable::SlotState>& BindlessDescriptorTable::GetSlots(const ShaderResourceClass resourceClass) noexcept {
		return resourceClass == ShaderResourceClass::Resource ? resourceSlots_ : samplerSlots_;
	}

	DescriptorAllocator& BindlessDescriptorTable::GetAllocator(const ShaderResourceClass resourceClass) noexcept {
		return resourceClass == ShaderResourceClass::Resource ? descriptors_->Resources() : descriptors_->Samplers();
	}

} // namespace NexusEngine
