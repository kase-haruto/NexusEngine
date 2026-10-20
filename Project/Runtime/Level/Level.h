#pragma once

// c++
#include <filesystem>
#include <memory>
#include <string>
#include <optional>
#include "Graphics/Lighting/DDGISettings.h"
#include "Graphics/Environment/EnvironmentLightingData.h"

// engine
#include "Foundation/Error/Result.h"
#include "Runtime/Scene/PersistentIdGenerator.h"
#include "Runtime/Scene/Scene.h"
#include "Runtime/Scene/SystemScheduler.h"

namespace NexusEngine {
	struct LevelLightingSettings {
		std::array<float,3> skyIrradiance{1.5f,1.8f,2.4f};
		DDGISettings probes;
		EnvironmentLightingData environment;
		bool IsValid() const noexcept {
			for(float value:skyIrradiance) if(!std::isfinite(value) || value<0 || value>1000) return false;
			return probes.IsValid() && std::isfinite(environment.intensity) && environment.intensity>=0 && std::isfinite(environment.rotation);
		}
	};

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
		~Level()					   = default;
		Level(const Level&)			   = delete;
		Level& operator=(const Level&) = delete;
		Level(Level&&)				   = delete;
		Level& operator=(Level&&)	   = delete;

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
		/** \brief 完成済みWorldへ交換する。古いWorldを返し、呼出側が寿命を管理する */
		std::unique_ptr<Scene> ExchangeScene(std::unique_ptr<Scene> scene) noexcept {
			if(!scene) return {};
			scene_.swap(scene);
			state_ = LevelState::Loaded;
			return scene;
		}
		/** \brief 登録済みSystemを1 frame更新する */
		[[nodiscard]] Result<void> Update(float deltaTime);

		[[nodiscard]] LevelState		 GetState() const noexcept { return state_; }
		[[nodiscard]] const std::string& GetName() const noexcept { return name_; }
		[[nodiscard]] Scene*			 GetScene() noexcept { return scene_.get(); }
		[[nodiscard]] const Scene*		 GetScene() const noexcept { return scene_.get(); }
		[[nodiscard]] SystemScheduler&	 GetSystemScheduler() noexcept { return scheduler_; }

		void SetName(const std::string& name) noexcept { name_ = name; }
		std::optional<LevelLightingSettings>& Lighting() noexcept { return lighting_; }
		const std::optional<LevelLightingSettings>& Lighting() const noexcept { return lighting_; }

	private:
		friend class LevelSerializer;
		std::optional<LevelLightingSettings> lighting_;

		std::string			   name_;						  //< Editorと診断に使うLevel名
		std::unique_ptr<Scene> scene_;						  //< EntityとComponentを所有するECS World
		SystemScheduler		   scheduler_;					  //< Sceneへ処理を適用するSystem群
		PersistentIdGenerator  persistentIds_;				  //< 新規Entityの永続ID発行元
		LevelState			   state_ = LevelState::Unloaded; //< Worldへのアクセス可否を表す状態
	};

} // namespace NexusEngine
