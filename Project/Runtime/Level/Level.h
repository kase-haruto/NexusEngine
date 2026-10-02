#pragma once

// c++
#include <filesystem>
#include <memory>
#include <string>

// engine
#include "Foundation/Error/Result.h"
#include "Runtime/Scene/PersistentIdGenerator.h"
#include "Runtime/Scene/Scene.h"
#include "Runtime/Scene/SystemScheduler.h"

namespace NexusEngine {

	enum class LevelState : uint8_t {
		Unloaded,
		Loaded
	};

	/*-----------------------------------------------------------------------------------------
	 * Level
	 * - 1つのゲーム空間に属するECS SceneとSystem実行基盤を所有する
	 * - Scene遷移、Renderer、Editor UI、Asset/GPU Resourceは管理しない
	 *---------------------------------------------------------------------------------------*/
	class Level final {
	public:
		explicit Level(std::string name = "Level");
		~Level() = default;
		Level(const Level&) = delete;
		Level& operator=(const Level&) = delete;
		Level(Level&&) = delete;
		Level& operator=(Level&&) = delete;

		/** \brief 空のECS Worldを生成してLevelをLoaded状態にする */
		[[nodiscard]] Result<void> LoadEmpty();
		/** \brief ファイルからLevelを復元する \param path Levelファイルのパス */
		[[nodiscard]] Result<void> Load(const std::filesystem::path& path);
		/** \brief 現在のLevelを保存する \param path 出力先Levelファイル */
		[[nodiscard]] Result<void> Save(const std::filesystem::path& path);
		/** \brief ECS Worldを一括破棄しUnloaded状態へ戻す */
		void Unload() noexcept;

		/** \brief 永続IDを持つEntityを生成する \return Unloaded時は無効Entity */
		[[nodiscard]] Entity CreateEntity(std::string name = "Entity");
		/** \brief このLevelに属するEntityを破棄する */
		bool DestroyEntity(Entity entity);
		/** \brief 保存IDを現在WorldのHandleへ解決する。Editor用cold path、O(Entity数) */
		[[nodiscard]] Entity FindEntity(PersistentId id);
		/** \brief 登録済みSystemを1 frame更新する */
		[[nodiscard]] Result<void> Update(float deltaTime);

		[[nodiscard]] LevelState GetState() const noexcept { return state_; }
		[[nodiscard]] const std::string& GetName() const noexcept { return name_; }
		[[nodiscard]] Scene* GetScene() noexcept { return scene_.get(); }
		[[nodiscard]] const Scene* GetScene() const noexcept { return scene_.get(); }
		[[nodiscard]] SystemScheduler& GetSystemScheduler() noexcept { return scheduler_; }

	private:
		friend class LevelSerializer;

		std::string name_;                       //< Editorと診断に使うLevel名
		std::unique_ptr<Scene> scene_;            //< EntityとComponentを所有するECS World
		SystemScheduler scheduler_;               //< Sceneへ処理を適用するSystem群
		PersistentIdGenerator persistentIds_;     //< 新規Entityの永続ID発行元
		LevelState state_ = LevelState::Unloaded; //< Worldへのアクセス可否を表す状態
	};

} // namespace NexusEngine
