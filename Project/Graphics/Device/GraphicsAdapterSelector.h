#pragma once

// c++
#include <dxgi1_6.h>
#include <wrl/client.h>

// engine
#include "Foundation/Error/Result.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * GraphicsAdapterSelector
	 * - DXGI Adapterを列挙し、D3D12対応Adapterを選択する
	 * - Adapterの所有先やD3D12 Deviceの生成は管理しない
	 *---------------------------------------------------------------------------------------*/
	class GraphicsAdapterSelector final {
	public:
		using AdapterPointer = Microsoft::WRL::ComPtr<IDXGIAdapter4>;

		/**
		 * \brief 設定に従ってD3D12対応Adapterを選択する
		 * \param factory Adapter列挙に使用する非所有Factory
		 * \param useWarpAdapter WARPを明示的に選択する場合true
		 * \return 選択されたAdapterまたはエラー
		 */
		[[nodiscard]] static Result<AdapterPointer> Select(IDXGIFactory4* factory, bool useWarpAdapter);

	private:
		[[nodiscard]] static bool SupportsD3D12(IDXGIAdapter1* adapter) noexcept;
	};

} // namespace NexusEngine
