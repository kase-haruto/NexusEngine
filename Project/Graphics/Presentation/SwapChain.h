#pragma once

// c++
#include <array>
#include <cstdint>

// directx
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

// engine
#include "Foundation/Error/Result.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * SwapChain
	 * - DXGI SwapChain、BackBuffer、SwapChain専用RTV Heapを所有する
	 * - Command記録、Queue同期、Windowイベント処理は管理しない
	 *---------------------------------------------------------------------------------------*/
	class SwapChain final {
	public:
		static constexpr uint32_t kBufferCount = 2;

		SwapChain() noexcept = default;
		~SwapChain() noexcept;
		SwapChain(const SwapChain&)			   = delete;

		SwapChain& operator=(const SwapChain&) = delete;

		/**
		 * \brief SwapChainとBackBufferを初期化する
		 * \param factory DXGI Factory
		 * \param device Direct3D12デバイス
		 * \param queue Direct Command Queue
		 * \param nativeWindow ウィンドウのネイティブハンドル
		 * \param width 描画幅
		 * \param height 描画高
		 * \return 初期化結果
		 */
		[[nodiscard]] Result<void> Initialize(
			IDXGIFactory4*		factory,
			ID3D12Device*		device,
			ID3D12CommandQueue* queue,
			void*				nativeWindow,
			uint32_t			width,
			uint32_t			height);
		/**
		 * \brief SwapChainとBackBufferを解放する
		 */
		void Shutdown() noexcept;
		/**
		 * \brief SwapChainとBackBufferを指定サイズへリサイズする
		 * \param device Direct3D12デバイス
		 * \param width 描画幅
		 * \param height 描画高
		 * \return リサイズ結果
		 */
		[[nodiscard]] Result<void> Resize(ID3D12Device* device, uint32_t width, uint32_t height);
		/**
		 * \brief SwapChainの現在の描画幅を取得する
		 * \return 描画幅
		 */
		[[nodiscard]] uint32_t GetWidth() const noexcept;
		/**
		 * \brief SwapChainの現在の描画高を取得する
		 * \return 描画高
		 */
		[[nodiscard]] uint32_t GetHeight() const noexcept;
		/**
		 * \brief SwapChainにフレームを提示する
		 * \param enableVSync VSyncを有効にするか
		 * \return 提示結果
		 */
		[[nodiscard]] Result<void> Present(bool enableVSync);
		/**
		 * \brief SwapChainの現在のフレームインデックスを取得する
		 * \return フレームインデックス
		 */
		[[nodiscard]] uint32_t GetCurrentFrameIndex() const noexcept;
		/**
		 * \brief SwapChainの現在のBackBufferを取得する
		 * \return BackBufferリソース
		 */
		[[nodiscard]] ID3D12Resource* GetCurrentBackBuffer() const noexcept;
		/**
		 * \brief SwapChainの現在のRTVを取得する
		 * \return RTVハンドル
		 */
		[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRtv() const noexcept;

	private:
		/**
		 * \brief SwapChainのBackBufferを生成する
		 * \param device Direct3D12デバイス
		 * \return BackBuffer生成結果
		 */
		[[nodiscard]] Result<void> CreateRenderTargets(ID3D12Device* device);
		/**
		 * \brief SwapChainのBackBufferを解放する
		 */
		void ReleaseRenderTargets() noexcept;

		Microsoft::WRL::ComPtr<IDXGISwapChain3>							 swapChain_;   //< 表示とBackBufferローテーション
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>					 rtvHeap_;	   //< BackBuffer専用RTV Heap
		std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kBufferCount> backBuffers_; //< 表示用Buffer

		uint32_t rtvDescriptorSize_ = 0; //< RTVハンドル間隔
		uint32_t width_				= 0; //< 現在の描画幅
		uint32_t height_			= 0; //< 現在の描画高
	};

} // namespace NexusEngine
