#pragma once

#include <vector>

#include "Graphics/Descriptor/DescriptorHandle.h"
#include "Graphics/Renderer/GraphicsRenderExtension.h"

struct ImGui_ImplDX12_InitInfo;

namespace NexusEngine {
	class DescriptorAllocator;

	/*-----------------------------------------------------------------------------------------
	 * ImGuiRenderer
	 * - Dear ImGui ContextとWin32/DX12 backendの初期化、フレーム、描画、終了を担当する
	 * - Device、Queue、Window、DescriptorAllocator本体は所有しない
	 *---------------------------------------------------------------------------------------*/
	class ImGuiRenderer final : public IGraphicsRenderExtension {
	public:
		[[nodiscard]] Result<void> Initialize(const GraphicsRenderExtensionContext& context) override;
		void BeginFrame() override;
		void Record(ID3D12GraphicsCommandList* commandList) override;
		void Shutdown() noexcept override;
		[[nodiscard]] bool HandleWindowMessage(void* window, uint32_t message, uintptr_t wParam, intptr_t lParam) noexcept;

	private:
		static void AllocateDescriptor(::ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu);
		static void FreeDescriptor(::ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);

		DescriptorAllocator* descriptors_ = nullptr; //< GraphicsSystem所有の共有Resource allocator
		std::vector<DescriptorHandle> allocations_; //< Backendへ貸し出したDescriptor
		bool initialized_ = false; //< Contextと両Backendの初期化完了状態
	};
} // namespace NexusEngine
