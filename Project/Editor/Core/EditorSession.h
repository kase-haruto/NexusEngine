#pragma once

// engine
#include "Runtime/Level/LevelDocument.h"
#include "Runtime/Application/GameRuntime.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"

// c++ standard
#include <cmath>
#include "Editor/Services/SelectionService.h"
#include "Editor/Services/ViewportDiagnostics.h"
#include "Graphics/Renderer/RenderScene.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * EditorSession
	 * - 編集用Level、永続IDによる選択、編集状態を所有する
	 * - ImGui、GPU、通信には依存しない。同一Process内の編集を担当する
	 *---------------------------------------------------------------------------------------*/
	class EditorSession final {
	public:
		/** \brief 既存選択を解除して編集用Worldを生成する */
		[[nodiscard]] Result<void> Create();
		/** Open an existing level document; preserve the current session on failure. */
		[[nodiscard]] Result<void> Open(const std::filesystem::path& path);
		/** \brief World参照を破棄する前に選択状態を解除する */
		void Close() noexcept;
		/** \brief 現在のWorldで解決できる永続IDだけを選択する */
		void Select(PersistentId id);

		[[nodiscard]] PersistentId GetSelection() const noexcept { return selection_.Primary(); }
		SelectionService& Selection() noexcept { return selection_; }
		// エディタ専用の表示状態。プレビュー指定でLevelや選択を変更しない。
		bool SetCameraPreview(PersistentId id);
		void ClearCameraPreview() noexcept { previewCamera_ = {}; }
		PersistentId GetCameraPreview() const noexcept { return previewCamera_; }
		Result<RenderCamera> ResolveCameraPreview(float aspect);
		ViewportDiagnostics& Diagnostics() noexcept { return diagnostics_; }
		/** \brief サービス経由の編集を変更状態へ反映する */
		void MarkDirty() noexcept { dirty_ = true; }
		void MarkSaved() noexcept { dirty_ = false; }
		size_t Generation() const noexcept { return generation_; }
		[[nodiscard]] Entity		 GetSelectedEntity();
		[[nodiscard]] LevelDocument* GetLevelDocument() const noexcept { return levelDocument_.get(); }
		[[nodiscard]] Level*		 GetLevel() noexcept { return levelDocument_ ? levelDocument_->GetLevel() : nullptr; }
		/** \brief 描画・更新用World。再生中は編集Worldの複製を返す */
		Level* GetPreviewLevel() noexcept { return IsPlaying() ? playRuntime_.GetLevel() : GetLevel(); }
		Level* GetPlayLevel() noexcept { return playRuntime_.GetLevel(); }
		/** \brief Editorと単体起動が共有するGame側のSystem登録処理を設定する */
		[[nodiscard]] bool SetGameLevelSetup(GameLevelSetup setup) { return playRuntime_.SetLevelSetup(std::move(setup)); }
		/** \brief 編集中はTransform、再生中はGameRuntimeを更新する */
		[[nodiscard]] Result<void> UpdatePreview(float deltaTime);
		const std::optional<Error>& GetPlayError() const noexcept { return playError_; }
		/** World の同一性を保った再生成後、UI キャッシュだけを無効化する。 */
		void NotifyReinstanced() noexcept { ++generation_; }
		/** \brief 編集Worldを変更せずに再生Worldを作る */
		bool StartPlay();
		void StopPlay() noexcept { playRuntime_.Stop(); playError_.reset(); }
		void TogglePause() noexcept { playRuntime_.TogglePause(); }
		bool IsPlaying() const noexcept { return playRuntime_.IsRunning(); }
		bool IsPaused() const noexcept { return playRuntime_.IsPaused(); }
		[[nodiscard]] bool			 IsDirty() const noexcept { return dirty_; }

		/** \brief IDとLocal値による編集を適用する。派生MatrixはLevel更新で再計算する */
		[[nodiscard]] bool SetTransform(PersistentId id, Vector3 translation, Quaternion rotation, Vector3 scale);
		/** \brief 静的ベイク対象か、動的なプローブ受光対象かを永続ID付きで編集する。 */
		[[nodiscard]] bool SetStaticLighting(PersistentId id, bool enabled);

	private:
		std::unique_ptr<LevelDocument> levelDocument_; //< 編集用World。再生用Worldとは共有しない
		GameRuntime playRuntime_; //< Game単体起動と共通の実行基盤
		std::optional<Error> playError_; //< Play開始失敗をEditor UIへ伝える

		SelectionService selection_; //< 全Panel共通の永続ID集合
		PersistentId previewCamera_;
		ViewportDiagnostics diagnostics_;
		size_t generation_ = 0; //< documentの再生成を履歴サービスへ通知する
		bool		 dirty_ = false; //< 編集操作が適用されたか
	};
} // namespace NexusEngine
