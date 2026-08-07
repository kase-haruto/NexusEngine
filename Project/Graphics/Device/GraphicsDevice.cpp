#include "GraphicsDevice.h"

// c++
#include <utility>

// engine
#include "Foundation\Logging\Logger.h"
#include "GraphicsAdapterSelector.h"
#include "GraphicsDebugConfigurator.h"

namespace NexusEngine {
	namespace {

		constexpr int32_t kAlreadyInitialized = 1;
		constexpr int32_t kFactoryCreationFailed = 2;
		constexpr int32_t kDeviceCreationFailed = 3;
		constexpr int32_t kShaderModelUnsupported = 4;

	} // namespace

	GraphicsDevice::~GraphicsDevice() noexcept {
		Shutdown();
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	//	DXGI Factory、Adapter、D3D12 Deviceを初期化する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> GraphicsDevice::Initialize(const GraphicsDeviceDesc& desc) {
		// DeviceだけでなくFactoryまたはAdapterが残る部分初期化状態も二重初期化として扱う。
		if(IsInitialized() || factory_ || adapter_) {
			return std::unexpected(Error(
				ErrorCategory::Graphics, kAlreadyInitialized, "GraphicsDevice is already initialized."));
		}

		// Debug LayerとDREDはDevice生成後には有効化できないため、最初に設定する。
		GraphicsDebugConfigurator::ConfigureDebugLayer(
			desc.enableDebugLayer,
			desc.enableGpuBasedValidation);
		GraphicsDebugConfigurator::ConfigureDred(desc.enableDred);

		// Releaseには診断用Factoryの依存と実行コストを持ち込まない。
		UINT factoryFlags = 0;
#if defined(_DEBUG) || defined(NEXUS_DEVELOP)
		if(desc.enableDebugLayer) {
			factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
#endif

		// FactoryはAdapter列挙に必要なので、他のGraphics COMオブジェクトより先に所有する。
		HRESULT result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
		if(FAILED(result) && factoryFlags != 0) {
			// DXGI debug componentだけが利用不能な環境では、通常Factoryへフォールバックして起動を継続する。
			NEXUS_LOG_WARNING("DirectX", "DXGI debug factory is unavailable; retrying without debug flag.");
			result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
		}
		if(FAILED(result)) {
			// Factory生成前には後続COMオブジェクトがないが、統一したロールバック経路を通す。
			Shutdown();
			return std::unexpected(MakeDirectXError(
				kFactoryCreationFailed, result, "Failed to create DXGI Factory."));
		}

		// 列挙方針をGraphicsDevice本体から分離し、将来のLUID指定やEditor選択へ差し替え可能にする。
		auto selectedAdapter = GraphicsAdapterSelector::Select(factory_.Get(), desc.useWarpAdapter);
		if(!selectedAdapter) {
			// selectorが保持する詳細な原因を捨てず、そのままFrameworkまで伝播させる。
			Error error = std::move(selectedAdapter.error());
			Shutdown();
			return std::unexpected(std::move(error));
		}
		// expected内のComPtrをmoveし、参照カウントの不要な増減を避ける。
		adapter_ = std::move(*selectedAdapter);

		// Adapter検証と同じFeature Levelで実Deviceを作り、選択条件との不一致を防ぐ。
		result = D3D12CreateDevice(
			adapter_.Get(),
			D3D_FEATURE_LEVEL_12_0,
			IID_PPV_ARGS(&device_));
		if(FAILED(result)) {
			// 部分初期化されたCOM参照を逆順で解放して再初期化可能な状態へ戻す。
			Shutdown();
			return std::unexpected(MakeDirectXError(
				kDeviceCreationFailed, result, "Failed to create D3D12 Device."));
		}

		// Bindless Direct Heap Indexingで必要なShader Model 6.6をDevice初期化時に保証する。
		// 対応しない環境ではShader CompileやPSO生成まで進まず、明確な初期化エラーとして返す。
		D3D12_FEATURE_DATA_SHADER_MODEL shaderModel { D3D_SHADER_MODEL_6_6 };
		result = device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
		if(FAILED(result) || shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6) {
			Shutdown();
			return std::unexpected(MakeDirectXError(
				kShaderModelUnsupported,
				FAILED(result) ? result : E_NOINTERFACE,
				"Shader Model 6.6 is required for bindless descriptor indexing."));
		}

		// Info QueueはDeviceのインターフェースなので、Device生成成功後にのみ設定できる。
		GraphicsDebugConfigurator::ConfigureInfoQueue(device_.Get());
		NEXUS_LOG_INFO("Graphics", "D3D12 Device initialized.");
		return {};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	//	DirectXインターフェースを逆順で解放する
	/////////////////////////////////////////////////////////////////////////////////////////
	void GraphicsDevice::Shutdown() noexcept {
		// 将来のDevice依存オブジェクトはこの処理より前に破棄する。現段階ではDeviceから逆順に解放する。
		// ComPtr::Resetを用いることで、部分初期化と複数回のShutdownの両方を安全に扱う。
		device_.Reset();
		adapter_.Reset();
		factory_.Reset();
	}

	bool GraphicsDevice::IsInitialized() const noexcept {
		return device_ != nullptr;
	}

	ID3D12Device* GraphicsDevice::GetDevice() const noexcept {
		return device_.Get();
	}

	IDXGIFactory4* GraphicsDevice::GetFactory() const noexcept {
		return factory_.Get();
	}

	IDXGIAdapter4* GraphicsDevice::GetAdapter() const noexcept {
		return adapter_.Get();
	}

} // namespace NexusEngine
