#pragma once

// c++
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

// engine
#include "Foundation/Error/Result.h"
#include "GraphicsDeviceDesc.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * GraphicsDevice
	 * - DXGI Factory、選択Adapter、DirectX 12 Deviceを生成所有する
	 * - Command Queue、SwapChain、Descriptor、GPUリソース、描画処理は管理しない
	 *---------------------------------------------------------------------------------------*/
	class GraphicsDevice final {
	public:
		GraphicsDevice() noexcept = default;
		~GraphicsDevice() noexcept;
		GraphicsDevice(const GraphicsDevice&) = delete;
		GraphicsDevice& operator=(const GraphicsDevice&) = delete;
		GraphicsDevice(GraphicsDevice&&) = delete;
		GraphicsDevice& operator=(GraphicsDevice&&) = delete;

		/**
		 * \brief DirectX 12デバイスを初期化する
		 * \param desc デバッグ機能やAdapter選択に使用する設定
		 * \return 初期化に成功した場合は成功結果、失敗した場合はエラー情報
		 * \note Debug LayerとDREDの設定はDevice生成前に行う
		 */
		[[nodiscard]] Result<void> Initialize(const GraphicsDeviceDesc& desc = {});

		/**
		 * \brief 所有するDirectXインターフェースを解放する
		 */
		void Shutdown() noexcept;

		[[nodiscard]] bool IsInitialized() const noexcept;
		[[nodiscard]] ID3D12Device* GetDevice() const noexcept;
		[[nodiscard]] IDXGIFactory4* GetFactory() const noexcept;
		[[nodiscard]] IDXGIAdapter4* GetAdapter() const noexcept;

	private:
		Microsoft::WRL::ComPtr<IDXGIFactory4> factory_; //< Adapter列挙と将来のSwapChain生成に使用するFactory
		Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_; //< Device生成元として選択したGPU Adapter
		Microsoft::WRL::ComPtr<ID3D12Device>  device_;	 //< 後続Graphics機能が非所有参照するD3D12 Device
	};

} // namespace NexusEngine
