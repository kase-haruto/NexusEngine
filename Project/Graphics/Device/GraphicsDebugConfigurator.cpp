#include "GraphicsDebugConfigurator.h"

// c++
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <wrl/client.h>

// engine
#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	using Microsoft::WRL::ComPtr;

	/////////////////////////////////////////////////////////////////////////////////////////
	//	Device生成前にDebug Layerを有効化する
	/////////////////////////////////////////////////////////////////////////////////////////
	void GraphicsDebugConfigurator::ConfigureDebugLayer(
		const bool enableDebugLayer,
		const bool enableGpuBasedValidation) noexcept {
#if defined(_DEBUG) || defined(NEXUS_DEVELOP)
		if(!enableDebugLayer) {
			// 設定で無効な場合はDebug Interface自体を取得せず、初期化コストを発生させない。
			return;
		}

		ComPtr<ID3D12Debug> debug;
		const HRESULT result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
		if(FAILED(result)) {
			// 開発環境にGraphics Toolsがない場合でも、通常Deviceの生成は可能なので継続する。
			NEXUS_LOG_WARNING("DirectX", "D3D12 Debug Layer is unavailable; initialization will continue.");
			return;
		}

		// Debug Layerは生成済みDeviceへ後付けできないため、Device生成前に有効化する。
		debug->EnableDebugLayer();

		if(enableGpuBasedValidation) {
			// GPU Based Validationは高コストなため、Debug Layerとは別の明示設定でのみ有効化する。
			ComPtr<ID3D12Debug1> debug1;
			if(SUCCEEDED(debug.As(&debug1))) {
				debug1->SetEnableGPUBasedValidation(TRUE);
				NEXUS_LOG_INFO("DirectX", "GPU Based Validation enabled.");
			} else {
				NEXUS_LOG_WARNING("DirectX", "GPU Based Validation is unavailable.");
			}
		}
		NEXUS_LOG_INFO("DirectX", "D3D12 Debug Layer enabled.");
#else
		static_cast<void>(enableDebugLayer);
		static_cast<void>(enableGpuBasedValidation);
#endif
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	//	Device生成前にDREDを設定する
	/////////////////////////////////////////////////////////////////////////////////////////
	void GraphicsDebugConfigurator::ConfigureDred(const bool enableDred) noexcept {
#if defined(_DEBUG) || defined(NEXUS_DEVELOP)
		if(!enableDred) {
			// DRED追跡を必要としない構成ではDebug Interface取得を省略する。
			return;
		}

		ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
		const HRESULT result = D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings));
		if(FAILED(result)) {
			// OSまたはSDKがDRED未対応でもDevice作成は継続可能なのでWarningに留める。
			NEXUS_LOG_WARNING("DirectX", "DRED settings are unavailable; initialization will continue.");
			return;
		}

		// DREDはDevice生成時に診断用追跡を組み込むため、生成前に設定する必要がある。
		dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);

		ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings1;
		// Settings1対応環境だけBreadcrumb Contextを追加し、基底DRED対応環境との互換性を保つ。
		if(SUCCEEDED(dredSettings.As(&dredSettings1))) {
			dredSettings1->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		}
		NEXUS_LOG_INFO("DirectX", "DRED diagnostics enabled.");
#else
		static_cast<void>(enableDred);
#endif
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	//	Device生成後のInfo Queueを設定する
	/////////////////////////////////////////////////////////////////////////////////////////
	void GraphicsDebugConfigurator::ConfigureInfoQueue(ID3D12Device* const device) noexcept {
#if defined(_DEBUG) || defined(NEXUS_DEVELOP)
		if(device == nullptr) {
			// 部分初期化失敗時に誤って呼ばれてもQueryInterfaceを実行しない。
			return;
		}

		ComPtr<ID3D12InfoQueue> infoQueue;
		if(FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
			// Debug Layer未導入時はInfo Queueが得られないため、Device自体は有効として処理を続ける。
			NEXUS_LOG_WARNING("DirectX", "D3D12 Info Queue is unavailable.");
			return;
		}

		// 破損とAPIエラーだけをBreak対象とし、Warning以下はログで精査できる状態を保つ。
		// デバッガ未接続時の強制Breakはプロセスを異常停止させるため、接続状態を条件にする。
		const BOOL breakEnabled = IsDebuggerPresent() ? TRUE : FALSE;
		static_cast<void>(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, breakEnabled));
		static_cast<void>(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, breakEnabled));
		static_cast<void>(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE));
		static_cast<void>(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, FALSE));
		static_cast<void>(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_MESSAGE, FALSE));
		NEXUS_LOG_INFO("DirectX", "D3D12 Info Queue configured.");
#else
		static_cast<void>(device);
#endif
	}

} // namespace NexusEngine
