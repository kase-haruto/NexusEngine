#pragma once

#include "EditorWorkspace.h"
#include "Core/Framework/FrameworkUpdateClient.h"
#include "Editor/Gui/Core/GuiContext.h"
#include "Editor/ImGui/ImGuiRenderer.h"
#include "EditorSession.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Model/ModelInstance.h"
#include "Runtime/Rendering/RenderSceneExtractor.h"

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
		[[nodiscard]] const ModelAssetData& GetModelAsset() const noexcept { return modelAsset_; }
		[[nodiscard]] const ModelInstance&	GetModelPose() const noexcept { return modelPose_; }

		void ShowGui();
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
		bool showLegacyDiagnostics_ = false; //< 既存描画診断の表示要求

		// demo用モデル描画
		ModelAssetData modelAsset_;									  //< Sceneで選択したCPU Asset。modelPose_より長く生存する
		ModelInstance  modelPose_;									  //< GPUを参照しないScene固有のAnimation再生状態
	};
} // namespace NexusEngine
