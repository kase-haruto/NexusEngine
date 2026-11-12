#pragma once

// engine
#include "Runtime/Level/LevelDocument.h"
#include "Runtime/Application/GameRuntime.h"
#include "Runtime/Reflection/ManagedScriptMetadata.h"
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
		/** \brief 編集・再生Worldが使う型登録を起動ホストから受け取る */
		explicit EditorSession(ReflectionRegistration reflection = {}) : reflection_(reflection) {}
		[[nodiscard]] ReflectionRegistration GetReflectionRegistration() const noexcept { return reflection_; }
		/** \brief 既存選択を解除して編集用Worldを生成する */
		[[nodiscard]] Result<void> Create();
        /** \brief document再読込後も同じProjectの資源Rootを引き継ぐ。再生中の変更は禁止する */
        [[nodiscard]] Result<void> SetResourceRoot(std::filesystem::path root) {
            if(IsPlaying() || !root.is_absolute()) return std::unexpected(Error(ErrorCategory::Resource,270,"Editor resource Root requires a stopped session and absolute path."));
            if(GetLevel()) { auto result=GetLevel()->SetResourceRoot(root); if(!result) return result; }
            resourceRoot_=std::move(root); return {};
        }
        /** \brief 新しいdocument/Playへ同じHostのCPU model cacheを引き継ぐ */
        bool SetPhysicsModelSource(PhysicsWorld::ModelSource source) {
            if(IsPlaying() || (GetLevel() && !GetLevel()->SetPhysicsModelSource(source))) return false;
            physicsModels_=std::move(source); return true;
        }
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
        /** \brief 編集用Worldへ入力を与えず、実行Worldだけへfocus検証済みframeを渡す */
        void SetFrameInput(const InputFrame& input) noexcept { playRuntime_.SetFrameInput(input); }
		/** \brief Editorと単体起動が共有するGame側のSystem登録処理を設定する */
		[[nodiscard]] bool SetGameLevelSetup(GameLevelSetup setup) { return playRuntime_.SetLevelSetup(std::move(setup)); }
        /** \brief PlayのRuntimeへ設定だけを転送する。編集WorldにはDLLをロードしない */
        [[nodiscard]] bool SetNativeGameModule(NativeGameModuleSettings settings) {
            if(!playRuntime_.SetNativeGameModule(std::move(settings))) return false;
            return true; // HybridではManagedのCPU Metadataも維持する。
        }
        /** \brief 複数Native Moduleも同じPlay Worldへ接続する */
        [[nodiscard]] bool SetNativeGameModules(std::vector<NativeGameModuleSettings> settings) {
            if(!playRuntime_.SetNativeGameModules(std::move(settings))) return false;
            return true;
        }
        /** \brief 編集Worldで.NETを起動せず、PlayのRuntimeへ設定だけを転送する */
        [[nodiscard]] bool SetCSharpRuntime(CSharpRuntimeSettings settings);
        /** \brief CPU Metadataを検証後、実行RuntimeへReloadを委譲する。編集Levelは変更しない */
        [[nodiscard]] Result<void> ReloadCSharp(const std::filesystem::path& assembly);
        [[nodiscard]] uint64_t PlayEpoch() const noexcept { return playEpoch_; }
        /** \brief Editor所有の値MetadataをWorkspaceへ共有する。.NET Runtimeは所有しない */
        [[nodiscard]] const std::shared_ptr<const ManagedScriptMetadata>& GetManagedScriptMetadata() const noexcept { return managedMetadata_; }
		/** \brief 編集中はTransform、再生中はGameRuntimeを更新する */
		[[nodiscard]] Result<void> UpdatePreview(float deltaTime);
		const std::optional<Error>& GetPlayError() const noexcept { return playError_; }
		/** World の同一性を保った再生成後、UI キャッシュだけを無効化する。 */
		void NotifyReinstanced() noexcept { ++generation_; }
		/** \brief 編集Worldを変更せずに再生Worldを作る */
		bool StartPlay();
		void StopPlay() noexcept { ++playEpoch_; playRuntime_.Stop(); playError_.reset(); }
		void TogglePause() noexcept { playRuntime_.TogglePause(); }
		bool IsPlaying() const noexcept { return playRuntime_.IsRunning(); }
		bool IsPaused() const noexcept { return playRuntime_.IsPaused(); }
        bool IsCSharpSuspended() const noexcept { return playRuntime_.IsCSharpSuspended(); }
        /** \brief RuntimeからManaged Reload結果の値だけを取得する */
        ManagedReloadState CSharpReloadState() const noexcept { return playRuntime_.CSharpReloadState(); }
        /** \brief 同期Native Backend用の境界。EditorがDLLやSystem stateを直接所有しない */
        [[nodiscard]] Result<void> PrepareNativePatch() { return playRuntime_.PrepareNativePatch(); }
        [[nodiscard]] Result<void> FinishNativePatch(bool applied) { return playRuntime_.FinishNativePatch(applied); }
        NativePatchState NativePatchStatus() const noexcept { return playRuntime_.NativePatchStatus(); }
		[[nodiscard]] bool			 IsDirty() const noexcept { return dirty_; }

		/** \brief IDとLocal値による編集を適用する。派生MatrixはLevel更新で再計算する */
		[[nodiscard]] bool SetTransform(PersistentId id, Vector3 translation, Quaternion rotation, Vector3 scale);
		/** \brief 静的ベイク対象か、動的なプローブ受光対象かを永続ID付きで編集する。 */
		[[nodiscard]] bool SetStaticLighting(PersistentId id, bool enabled);

	private:
		ReflectionRegistration reflection_; //< 編集・Play・復元で同じ登録関数を使う
        std::filesystem::path resourceRoot_; //< Editor hostが指定したProject Root。documentを交換しても保持する
        PhysicsWorld::ModelSource physicsModels_; //< Editor ApplicationのCPU asset cacheへの同期借用
		std::unique_ptr<LevelDocument> levelDocument_; //< 編集用World。再生用Worldとは共有しない
		GameRuntime playRuntime_; //< Game単体起動と共通の実行基盤
        std::shared_ptr<const ManagedScriptMetadata> managedMetadata_; //< sidecar由来のimmutable値。CLR参照を含まない
        uint64_t playEpoch_ = 0; //< Compile中のStop / 再Playへ古い成果物を適用させない
		std::optional<Error> playError_; //< Play開始失敗をEditor UIへ伝える

		SelectionService selection_; //< 全Panel共通の永続ID集合
		PersistentId previewCamera_;
		ViewportDiagnostics diagnostics_;
		size_t generation_ = 0; //< documentの再生成を履歴サービスへ通知する
		bool		 dirty_ = false; //< 編集操作が適用されたか
	};
} // namespace NexusEngine
