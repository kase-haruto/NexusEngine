#pragma once

#include <cstdint>
#include <span>

#include <d3d12.h>
#include <wrl/client.h>

#include "Foundation/Error/Result.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * VertexBuffer
	 * - Vertex dataを保持するGPU ResourceとVertexBufferViewを所有する
	 * - Primitive形状やPipelineは管理しない
	 *---------------------------------------------------------------------------------------*/
	class VertexBuffer final {
	public:
		/**
		 * \brief 第一段階としてUpload HeapへVertex dataを作成する
		 * \param device Buffer生成に使う非所有Device
		 * \param data コピーするVertex byte列
		 * \param stride 1 Vertexあたりのbyte数
		 * \return Resource生成とMap結果
		 */
		[[nodiscard]] Result<void> Initialize(ID3D12Device* device, std::span<const uint8_t> data, uint32_t stride);
		/** \brief Device破棄前にVertex Resourceを解放する */
		void Shutdown() noexcept;
		/** \brief Input Assemblerのslot 0へVertex Buffer Viewを設定する */
		void Bind(ID3D12GraphicsCommandList* commandList) const noexcept;
		/** \brief DrawInstancedへ渡すVertex数を取得する */
		[[nodiscard]] uint32_t GetVertexCount() const noexcept;

	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
		D3D12_VERTEX_BUFFER_VIEW view_ {};
		uint32_t vertexCount_ = 0;
	};

} // namespace NexusEngine
