#include "DescriptorManager.h"

namespace NexusEngine {

	Result<void> DescriptorManager::Initialize(
		ID3D12Device* const device,
		const uint32_t resourceCapacity,
		const uint32_t rtvCapacity,
		const uint32_t dsvCapacity,
		const uint32_t samplerCapacity) {
		auto result = resources_.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, resourceCapacity, true);
		if(!result) return result;
		result = renderTargets_.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, rtvCapacity, false);
		if(!result) { Shutdown(); return result; }
		result = depthStencils_.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, dsvCapacity, false);
		if(!result) { Shutdown(); return result; }
		result = samplers_.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, samplerCapacity, true);
		if(!result) { Shutdown(); return result; }
		return {};
	}

	void DescriptorManager::Shutdown() noexcept {
		samplers_.Shutdown();
		depthStencils_.Shutdown();
		renderTargets_.Shutdown();
		resources_.Shutdown();
	}

	void DescriptorManager::BindShaderVisibleHeaps(ID3D12GraphicsCommandList* const commandList) const noexcept {
		if(commandList == nullptr) return;
		ID3D12DescriptorHeap* heaps[] = { resources_.GetHeap(), samplers_.GetHeap() };
		commandList->SetDescriptorHeaps(2, heaps);
	}

} // namespace NexusEngine
