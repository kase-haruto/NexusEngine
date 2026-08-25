#include "NexusFramework.h"

// c++
#include <exception>
#include <string>

// engine
#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	namespace {

		constexpr int kSuccessExitCode = 0;
		constexpr int kInitializationFailureExitCode = 1;
		constexpr int kUnhandledExceptionExitCode = 2;
		constexpr int kRuntimeFailureExitCode = 3;
		constexpr int32_t kInvalidState = 1;
		constexpr int32_t kUnsupportedMode = 2;

		std::string DescribeError(const Error& error) {
			std::string message = error.GetMessageText();
			message.append(" [code=");
			message.append(std::to_string(error.GetCode()));
			message.append(", native=");
			message.append(std::to_string(error.GetNativeCode()));
			message.append("]");
			return message;
		}

	} // namespace

	NexusFramework::~NexusFramework() noexcept {
		Shutdown();
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	//	エンジンのライフサイクルを実行する
	/////////////////////////////////////////////////////////////////////////////////////////
	int NexusFramework::Run(const FrameworkDesc& desc) noexcept {
		// 初期化失敗そのものも記録できるよう、他サブシステムより先に既定Sinkを準備する。
		// Loggerは多重初期化を内部で無視するため、Runの入口から安全に呼び出せる。
		Logger::Get().InitializeDefaultSinks();

		try {
			// 初期化エラーは例外に変換せずResultのまま扱い、終了コード1へ統一する。
			auto initializeResult = Initialize(desc);
			if(!initializeResult) {
				NEXUS_LOG_CRITICAL("Framework", DescribeError(initializeResult.error()));
				// Initialize内でロールバック済みの場合も、Shutdownは冪等なので安全に呼べる。
				Shutdown();
				return kInitializationFailureExitCode;
			}

			// Runはフレームの順序だけを定義し、個別機能の詳細は各段階へ委譲する。
			// 将来EditorとGameで処理が分岐しても、このライフサイクル自体は維持する。
			while(IsRunning()) {
				BeginFrame();
				auto eventResult = ProcessEvents();
				if(!eventResult) {
					NEXUS_LOG_CRITICAL("Framework", DescribeError(eventResult.error()));
					state_ = FrameworkState::Failed;
					Shutdown();
					return kRuntimeFailureExitCode;
				}

				// WM_CLOSEを受けたフレームではUpdate以降を実行せず、破棄対象へ触れない。
				if(!IsRunning()) {
					break;
				}
				Update();
				auto renderResult = Render();
				if(!renderResult) {
					NEXUS_LOG_CRITICAL("Framework", DescribeError(renderResult.error()));
					state_ = FrameworkState::Failed;
					Shutdown();
					return kRuntimeFailureExitCode;
				}
				EndFrame();
			}

			Shutdown();
			return kSuccessExitCode;
		} catch(const std::exception& exception) {
			// Resultで表現されない予期しない標準例外は、アプリケーション境界で終了コードへ変換する。
			NEXUS_LOG_CRITICAL("Framework", exception.what());
		} catch(...) {
			// DLLや外部ライブラリから非標準例外が来ても、終了処理を必ず通す。
			NEXUS_LOG_CRITICAL("Framework", "Unhandled non-standard exception.");
		}

		// 例外経路でも状態をFailedへ確定してから、部分初期化済みの所有物を解放する。
		state_ = FrameworkState::Failed;
		Shutdown();
		return kUnhandledExceptionExitCode;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	//	一度だけ実行するサブシステム初期化
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> NexusFramework::Initialize(const FrameworkDesc& desc) {
		// Frameworkの再利用はサブシステム側の残留状態を招くため、1インスタンス1回に制限する。
		if(state_ != FrameworkState::Uninitialized) {
			return std::unexpected(Error(
				ErrorCategory::Framework, kInvalidState, "Framework can only be initialized once."));
		}

		// サブシステムの初期化を始める前に状態を変更し、再入を検出可能にする。
		state_ = FrameworkState::Initializing;
		mode_ = desc.mode;
		NEXUS_LOG_INFO("Framework", "NexusEngine initialization started.");

		// EditorはGameと同じWindow/Graphics基盤を使い、Editor固有処理をRenderExtensionへ委譲する。
		// Windowを持たないHeadless実行には専用ループが必要なため、現時点では拒否する。
		if(mode_ == FrameworkMode::Headless) {
			state_ = FrameworkState::Failed;
			return std::unexpected(Error(
				ErrorCategory::Framework, kUnsupportedMode, "Requested Framework mode is not implemented."));
		}

		// Windowはイベントループと終了要求の起点なので、Graphicsより先に確立する。
		auto windowResult = window_.Initialize(desc.window);
		if(!windowResult) {
			// Window自身が内部ロールバックを完了しているため、原因を保ったまま上位へ返す。
			state_ = FrameworkState::Failed;
			return std::unexpected(std::move(windowResult.error()));
		}

		// GraphicsへProjectWindow自体を渡さず、Surface生成に必要な値だけを境界Descriptorへ変換する。
		const WindowSurfaceDesc surface {
			.nativeHandle = window_.GetNativeHandle(),
			.width = window_.GetClientWidth(),
			.height = window_.GetClientHeight()
		};
		auto graphicsResult = graphicsSystem_.Initialize(surface, desc.graphics);
		if(!graphicsResult) {
			// 初期化済みのWindowだけを逆順で戻し、部分初期化状態を残さない。
			window_.Shutdown();
			state_ = FrameworkState::Failed;
			return std::unexpected(std::move(graphicsResult.error()));
		}

		// Scene/Primitiveなど通常描画はBackend非依存Renderer境界へ接続する。
		// Applicationが所有権を保持し、FrameworkはGraphicsSystemとの寿命順だけを調整する。
		if(desc.renderer != nullptr) {
			auto rendererResult = graphicsSystem_.AttachRenderer(desc.renderer);
			if(!rendererResult) {
				graphicsSystem_.Shutdown();
				window_.Shutdown();
				state_ = FrameworkState::Failed;
				return std::unexpected(std::move(rendererResult.error()));
			}
		}

		// Editor UIなどの具象拡張はApplicationが所有し、Frameworkは任意拡張として接続する。
		if(desc.renderExtension != nullptr) {
			auto extensionResult = graphicsSystem_.AttachRenderExtension(desc.renderExtension);
			if(!extensionResult) {
				graphicsSystem_.Shutdown();
				window_.Shutdown();
				state_ = FrameworkState::Failed;
				return std::unexpected(std::move(extensionResult.error()));
			}
		}
		window_.SetMessageHandler(desc.messageHandler, desc.messageHandlerUserData);

		// 必須サブシステムがすべて成功した時点だけRunningへ遷移させる。
		state_ = FrameworkState::Running;
		NEXUS_LOG_INFO("Framework", "NexusEngine initialization completed.");
		return {};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	//	初期化と逆順にサブシステムを終了する
	/////////////////////////////////////////////////////////////////////////////////////////
	void NexusFramework::Shutdown() noexcept {
		// 未初期化、終了済み、終了処理中は何もせず、明示呼び出しとデストラクタを両立させる。
		if(state_ == FrameworkState::Uninitialized ||
		   state_ == FrameworkState::Stopped ||
		   state_ == FrameworkState::ShuttingDown) {
			return;
		}

		// 状態を先に変更し、デストラクタ経由を含む二重終了を防止する。
		state_ = FrameworkState::ShuttingDown;
		NEXUS_LOG_INFO("Framework", "NexusEngine shutdown started.");

		// 依存側から先に破棄する。将来SwapChainを追加する場合もWindowより前に解放する。
		window_.SetMessageHandler(nullptr, nullptr);
		graphicsSystem_.Shutdown();
		window_.Shutdown();

		// 全所有物の解放後にStoppedへ遷移し、外部から終了完了を観測可能にする。
		state_ = FrameworkState::Stopped;
		NEXUS_LOG_INFO("Framework", "NexusEngine shutdown completed.");
	}

	bool NexusFramework::IsRunning() const noexcept {
		// Framework状態とWindow終了要求の両方を満たす場合だけ次フレームへ進む。
		return state_ == FrameworkState::Running && window_.IsRunning();
	}

	void NexusFramework::BeginFrame() noexcept {
		// Command AllocatorやFrame Resource導入時のフレーム開始境界。
	}

	Result<void> NexusFramework::ProcessEvents() {
		// Windowが保持する終了状態はIsRunningで直後に評価し、Framework側へ重複状態を持たせない。
		static_cast<void>(window_.ProcessEvents());
		if(const auto resize = window_.ConsumeResizeEvent(); resize.has_value()) {
			return graphicsSystem_.Resize(resize->width, resize->height);
		}
		return {};
	}

	void NexusFramework::Update() noexcept {
		// GameまたはEditorの更新処理を接続する拡張境界。
	}

	Result<void> NexusFramework::Render() {
		return graphicsSystem_.RenderFrame();
	}

	void NexusFramework::EndFrame() noexcept {
		// Presentやフレーム同期を接続する拡張境界。
	}

	FrameworkState NexusFramework::GetState() const noexcept {
		return state_;
	}

} // namespace NexusEngine
