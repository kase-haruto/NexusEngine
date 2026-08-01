#include "GraphicsAdapterSelector.h"

// c++
#include <d3d12.h>
#include <string>

// engine
#include "Foundation/Logging/Logger.h"
#include "Foundation/Utility/String/ConvertString.h"

namespace NexusEngine {
	using Microsoft::WRL::ComPtr;

	namespace {

		constexpr int32_t kFactoryUnavailable = 1;
		constexpr int32_t kWarpUnavailable	   = 2;
		constexpr int32_t kAdapterUnavailable = 3;

		void LogAdapter(IDXGIAdapter1* const adapter) {
			DXGI_ADAPTER_DESC1 description = {};
			// 情報取得失敗はAdapter選択成功を覆す致命的条件ではないため、ログだけを省略する。
			if(FAILED(adapter->GetDesc1(&description))) {
				return;
			}

			std::string message = "Selected adapter: ";
			message.append(ConvertString(description.Description));
			message.append(", dedicated video memory: ");
			message.append(std::to_string(description.DedicatedVideoMemory / (1024ULL * 1024ULL)));
			message.append(" MiB.");
			NEXUS_LOG_INFO("Graphics", message);
		}

	} // namespace

	/////////////////////////////////////////////////////////////////////////////////////////
	//	D3D12対応Adapterを選択する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<GraphicsAdapterSelector::AdapterPointer> GraphicsAdapterSelector::Select(
		IDXGIFactory4* const factory,
		const bool useWarpAdapter) {
		if(factory == nullptr) {
			return std::unexpected(Error(
				ErrorCategory::Graphics, kFactoryUnavailable, "DXGI Factory is unavailable."));
		}

		if(useWarpAdapter) {
			// WARPはSoftware Adapter除外ループを通さず、明示要求された場合だけ専用APIで取得する。
			ComPtr<IDXGIAdapter4> warpAdapter;
			const HRESULT result = factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));
			if(FAILED(result) || !SupportsD3D12(warpAdapter.Get())) {
				return std::unexpected(MakeDirectXError(
					kWarpUnavailable, result, "Failed to select a D3D12 compatible WARP adapter."));
			}
			LogAdapter(warpAdapter.Get());
			return warpAdapter;
		}

		// QueryInterface失敗は古いOSで正常に起こり得るため、エラーにせず通常列挙へ切り替える。
		ComPtr<IDXGIFactory6> factory6;
		static_cast<void>(factory->QueryInterface(IID_PPV_ARGS(&factory6)));

		for(UINT index = 0;; ++index) {
			// 候補は各反復だけComPtrで所有し、不採用Adapterを次の反復まで保持しない。
			ComPtr<IDXGIAdapter1> candidate;
			HRESULT result = DXGI_ERROR_NOT_FOUND;

			// Factory6ではOSへ高性能GPU優先を明示し、旧環境では通常列挙へフォールバックする。
			if(factory6) {
				result = factory6->EnumAdapterByGpuPreference(
					index,
					DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
					IID_PPV_ARGS(&candidate));
			} else {
				result = factory->EnumAdapters1(index, &candidate);
			}

			if(result == DXGI_ERROR_NOT_FOUND) {
				// DXGIが列挙終端を通知した場合だけ探索を終了する。
				break;
			}
			if(FAILED(result)) {
				// 一つのAdapter取得失敗で全探索を中断せず、次候補を試す。
				continue;
			}

			DXGI_ADAPTER_DESC1 description = {};
			if(FAILED(candidate->GetDesc1(&description))) {
				// Flagsを確認できないAdapterはSoftware判定不能なので、安全側で候補から外す。
				continue;
			}

			// Software Adapterは通常選択から除外し、WARPは明示設定時だけ利用する。
			if((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
				continue;
			}

			// Deviceをまだ所有せずにFeature Level 12.0対応だけを検証する。
			if(!SupportsD3D12(candidate.Get())) {
				continue;
			}

			ComPtr<IDXGIAdapter4> adapter4;
			if(FAILED(candidate.As(&adapter4))) {
				// 公開するインターフェース型へ昇格できない古いAdapterは採用しない。
				continue;
			}

			LogAdapter(adapter4.Get());
			return adapter4;
		}

		return std::unexpected(Error(
			ErrorCategory::Graphics, kAdapterUnavailable, "No D3D12 compatible hardware adapter was found."));
	}

	bool GraphicsAdapterSelector::SupportsD3D12(IDXGIAdapter1* const adapter) noexcept {
		if(adapter == nullptr) {
			return false;
		}

		// 出力先をnullptrにした検証呼び出しで、Deviceを所有せずFeature Level対応だけを確認する。
		// 実Deviceの生成とログ設定はGraphicsDeviceへ一元化する。
		return SUCCEEDED(D3D12CreateDevice(
			adapter,
			D3D_FEATURE_LEVEL_12_0,
			__uuidof(ID3D12Device),
			nullptr));
	}

} // namespace NexusEngine
