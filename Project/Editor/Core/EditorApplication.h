#pragma once

#include "EditorWorkspace.h"
#include "Core/Framework/FrameworkUpdateClient.h"
#include "Editor/Gui/Core/GuiContext.h"
#include "Editor/ImGui/ImGuiRenderer.h"
#include "EditorSession.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Model/ModelInstance.h"
#include "Graphics/Model/ModelAssetLibrary.h"
#include "Graphics/Renderer/PrimitiveRenderer.h"
#include "Runtime/Rendering/RenderSceneExtractor.h"
#include <set>
#include <future>
#include "Graphics/Resource/WicImageDecoder.h"

namespace NexusEngine {
	class ShadowSystem;
	class OpticalSystem;
	class PrimitiveRenderer;
	/*-----------------------------------------------------------------------------------------
	 * EditorApplication
	 * - Editor UIの構築とImGui描画基盤の接続を担当する
	 * - Win32/DX12固有処理はImGuiRendererへ委譲する
	 *---------------------------------------------------------------------------------------*/
	class EditorApplication final : public IGraphicsRenderExtension, public IFrameworkUpdateClient {

	public:
		/** \brief GPU初期化前にデモSceneとCPUモデルを作成する。モデル選択の編集箇所はこの実装 */
		[[nodiscard]] Result<void>			CreateScene(const std::filesystem::path& assetDirectory);
		[[nodiscard]] const ModelAssetData& GetModelAsset() const noexcept { return *modelAsset_; }
		[[nodiscard]] const ModelInstance&	GetModelPose() const noexcept { return modelPose_; }

		void ShowGui();
		/** \brief 初期化前に追加Panel/Commandの登録処理を接続する */
		void AddEditorExtension(std::function<void(EditorWorkspace&)> extension) { editorExtensions_.push_back(std::move(extension)); }
		/** \brief 既存Renderer診断画面を必要時だけ描く */
		void ShowLegacyDiagnostics();
		/** \brief Application所有Rendererの設定窓口を非所有接続する */
		void SetShadowSystem(ShadowSystem* shadows) noexcept {
			shadows_ = shadows;
		}
		void SetOpticalSystem(OpticalSystem* optics) noexcept {
			optics_ = optics;
		}
		void SetMaterialRenderer(PrimitiveRenderer* renderer) noexcept {
			materialRenderer_ = renderer;
		}

		/**
		 * @brief 初期化
		 * @param context
		 * @return 失敗した場合の原因
		 */
		[[nodiscard]] Result<void> Initialize(const GraphicsRenderExtensionContext& context) override;
		/**
		 * @brief 開始フレーム
		 */
		void BeginFrame() override;
		/** \brief Editorが所有するRuntime Levelを更新する */
		[[nodiscard]] Result<void>		 Update(float deltaTime) override;
		[[nodiscard]] Result<void>		 PrepareRender(uint32_t width, uint32_t height) override;
		[[nodiscard]] const RenderScene& GetRenderScene() const noexcept { return renderScene_; }
		/**
		 * @brief commandListに記録する
		 * @param context
		 */
		void Record(GraphicsContext& context) override;
		/**
		 * @brief 停止
		 */
		void Shutdown() noexcept override;
		/**
		 * @brief  Win32 Window MessageをImGuiへ転送する
		 * @param window
		 * @param message
		 * @param wParam
		 * @param lParam
		 * @return
		 */
		[[nodiscard]] bool HandleWindowMessage(
			void*	  window,
			uint32_t  message,
			uintptr_t wParam,
			intptr_t  lParam) noexcept;

	private:
		struct PreparedAssets {
			std::unique_ptr<ModelAssetLibrary> library;
			std::shared_ptr<const ModelAssetData> asset;
			ModelInstance pose;
			std::unique_ptr<PrimitiveRenderer> renderer, preview;
			double loadMilliseconds = 0, rendererMilliseconds = 0;
			~PreparedAssets() {
				if(preview) preview->Shutdown();
				if(renderer) renderer->Shutdown();
			}
		};
		Result<void> PrepareAssets(std::vector<ModelAssetLibrary::Request> requests);
		Result<void> PrepareCameraPreview(bool requested);
		void FinishAssetPreparation() noexcept;
		ImTextureRef RequestAssetPreview(const std::string& key, const std::string& reference, bool texture, int node);
		std::future<Result<std::unique_ptr<PreparedAssets>>> assetBuild_; //< 同時実行は1件。旧資源を描画しながら準備する
		std::stop_source assetBuildCancellation_;
		std::future<Result<std::unique_ptr<PreparedAssets>>> previewBuild_; //< カメラ表示の開始時も同期ロードしない
		size_t buildingPreviewAssetRevision_ = 0;
		std::vector<ModelAssetLibrary::Request> requestedAssets_;
		size_t assetRequestRevision_ = 0, buildingAssetRevision_ = 0, resolvedAssetRequestRevision_ = 0, failedAssetRequestRevision_ = 0;
		std::future<Result<DecodedImage>> thumbnailBuild_; //< サムネイルの生成もUIスレッドを使わない
		std::string thumbnailBuildKey_;
		std::set<std::string> failedThumbnails_;
		[[nodiscard]] Result<void> CreateWorkspace(float dpiScale);
		ReflectionValue CaptureReinstancing();
		void ReleaseReinstancing() noexcept;
		void RestoreReinstancing(const ReflectionValue& snapshot);
		size_t sceneGeneration_ = 0;
        std::unique_ptr<ModelAssetLibrary> modelLibrary_; //< CPU Sourceと材質Variantの所有者
        std::unique_ptr<PrimitiveRenderer> cameraPreviewRenderer_; //< プレビュー専用GPU資源。GraphicsSystem停止まで保持する
        RenderScene cameraPreviewScene_;
        size_t previewAssetRevision_ = 0;
        bool previewInitializationFailed_ = false;
        size_t uploadedAssetRevision_ = 1; //< GPUへ反映済みsnapshot
        std::set<std::string> reportedAssetErrors_; //< 同じ欠損参照による毎Frameのログ連打を防ぐ
		std::array<char, 1024> environmentPath_ {}; //< Editor入力。RendererはScene設定だけを参照する
		std::array<char, 1024> bakedPath_ {}; //< ベイク成果物の入力パス。GPU参照は保持しない
		GraphicsSystem* graphicsSystem_ = nullptr; //< Non-owning post-process controls.
		ShadowSystem*	   shadows_			   = nullptr; //< Runtime設定/診断のみ参照する。ImGuiはEditor内に限定
		OpticalSystem*	   optics_			   = nullptr; //< Optical Passの設定のみを編集する非所有参照
		PrimitiveRenderer* materialRenderer_   = nullptr; //< 光学Materialの設定窓口、非所有
		uint32_t		   mirrorMaterialSlot_ = 0;
		uint32_t		   glassMaterialSlot_  = 0;

		ImGuiRenderer imguiRenderer_;		   //< imgui描画
		UI::Context	  guiContext_;			   //< gui
		bool		  showDemoWindow_ = false; //< ImGui Demo Windowの表示フラグ

		EditorSession		 session_;	   //< 編集Worldと選択状態を所有する
		RenderSceneExtractor extractor_;   //< Runtimeから描画snapshotへの境界
		RenderScene			 renderScene_; //< Rendererへの値snapshot。EditorApplicationと同寿命

		// panels
		std::unique_ptr<EditorWorkspace> workspace_; //< PanelとEditorサービスの組立
		std::vector<std::function<void(EditorWorkspace&)>> editorExtensions_; //< Workspace初期化時に一度呼ぶ機能登録
		bool showLegacyDiagnostics_ = false; //< 既存描画診断の表示要求
		std::optional<ModelInstance> editPose_; //< 再生中だけ保持する編集時のAnimation状態

		// demo用モデル描画
		std::shared_ptr<const ModelAssetData> modelAsset_; //< Libraryと共有するCPU snapshot。modelPose_より長く生存する
		ModelInstance  modelPose_;									  //< GPUを参照しないScene固有のAnimation再生状態
	};
} // namespace NexusEngine
