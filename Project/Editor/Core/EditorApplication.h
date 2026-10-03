#pragma once

#include "Core/Framework/FrameworkUpdateClient.h"
#include "Editor/Gui/Core/GuiContext.h"
#include "Editor/ImGui/ImGuiRenderer.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Model/ModelInstance.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Rendering/RenderSceneExtractor.h"

namespace NexusEngine {
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
		void DrawDockSpace();
		void DrawMainMenuBar();

		ImGuiRenderer imguiRenderer_;		   //< imgui描画
		UI::Context	  guiContext_;			   //< gui
		bool		  showDemoWindow_ = false; //< ImGui Demo Windowの表示フラグ

		Level				 level_{"EditorLevel"}; //< Editor viewportが操作するRuntime Level
		RenderSceneExtractor extractor_;			//< Runtimeから描画snapshotへの境界
		RenderScene			 renderScene_;			//< Rendererへの値snapshot。EditorApplicationと同寿命

		// demo用モデル描画
		ModelAssetData modelAsset_;								  //< Sceneで選択したCPU Asset。modelPose_より長く生存する
		ModelInstance  modelPose_;								  //< GPUを参照しないScene固有のAnimation再生状態
		Entity		   cameraEntity_;							  //< 一時的な編集用Camera。Level破棄より前に無効化する
		Vector3		   cameraRotationRadians_{};				  //< ImGuiで編集するCameraのEuler角
		Entity		   rotatingEntity_;							  //< デモSceneの回転対象。Level破棄より前に無効化する
		Vector3		   rotationRadians_{};						  //< デモ回転の累積角度。描画回数ではなくUpdateで進める
		Vector3		   rotationSpeed_{0.45f, 0.75f, 0.0f};		  //< Sceneで編集する角速度（rad/s）
		Vector3		   cameraInitTranslation_{-7.5f, 1.0f, 0.1f}; //< カメラ初期座標
		Vector3		   cameraInitRotation_{0.0f, 1.54f, 0.0f};	  //< カメラ初期回転

	};
} // namespace NexusEngine
