#pragma once

// c++
#include <filesystem>
#include <memory>
#include <string>
#include <optional>
#include "Graphics/Lighting/DDGISettings.h"
#include "Graphics/Environment/EnvironmentLightingData.h"
#include "Graphics/PostProcess/ScreenEffect.h"

// engine
#include "Foundation/Error/Result.h"
#include "Runtime/Scene/PersistentIdGenerator.h"
#include "Runtime/Scene/Scene.h"
#include "Runtime/Scene/SystemScheduler.h"
#include "Runtime/Reflection/ReflectionRegistration.h"
#include "Foundation/Input/InputFrame.h"
#include "Runtime/Physics/PhysicsWorld.h"
#include "Runtime/Audio/AudioWorld.h"

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
		explicit Level(std::string name = "Level", ReflectionRegistration reflection = {});
		/** \brief このWorldと同じ型集合を新しいWorldへ引き継ぐための登録方針を返す */
		[[nodiscard]] ReflectionRegistration GetReflectionRegistration() const noexcept { return reflection_; }
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
		std::unique_ptr<Scene> ExchangeScene(std::unique_ptr<Scene> scene) noexcept;
		/** \brief 現Worldの最近傍衝突を値で取得する。無衝突時のEntityIdは無効。所有threadで呼ぶ */
		[[nodiscard]] Result<PhysicsWorld::Hit> Raycast(const std::array<float,3>& origin,
			const std::array<float,3>& direction, float distance, uint32_t layerMask = UINT32_MAX);
        /** \brief 起動前にProject資源Rootを設定する。Sceneの保存/交換では変更しない */
        [[nodiscard]] Result<void> SetResourceRoot(std::filesystem::path root);
        const std::filesystem::path& ResourceRoot() const noexcept { return resourceRoot_; }
        /** \brief 必要時に共通Audio Backendを生成する。デバイス生成はPlayでのみ行う */
        AudioWorld& Audio();
        /** \brief Hostが既存CPU asset cacheの同期借用を接続する。稼働中solverの差替えは禁止 */
        [[nodiscard]] bool SetPhysicsModelSource(PhysicsWorld::ModelSource source) {
            if(physics_) return false; physicsModels_=std::move(source); return true;
        }
        const PhysicsWorld::ModelSource& PhysicsModels() const noexcept { return physicsModels_; }
        /** \brief 直前stepの接触snapshot。複数言語から読んでも通知を消費しない */
        std::span<const PhysicsWorld::Contact> PhysicsContacts() const noexcept { return physics_ ? physics_->Contacts() : std::span<const PhysicsWorld::Contact>{}; }
        uint32_t DroppedPhysicsContacts() const noexcept { return physics_ ? physics_->DroppedContacts() : 0; }
        /** \brief 所有threadで次Levelを要求する。callback中には交換せずRuntimeがframe境界で処理する */
        [[nodiscard]] Result<void> RequestLevel(const std::filesystem::path& reference);
        /** \brief Runtimeだけがframe境界で要求を受け取り、重複実行を防ぐ */
        std::optional<std::filesystem::path> TakeLevelRequest() noexcept { auto value=std::move(nextLevel_); nextLevel_.reset(); return value; }
        /** \brief client解放時に所有voiceを止める。Audio未使用なら何も生成しない */
        void StopAudioOwner(uint64_t owner) noexcept { if(audio_) audio_->StopOwner(owner); }
        /** \brief 実行の一時停止に音声を追従させる */
        [[nodiscard]] Result<void> PauseAudio(bool paused) { return audio_ ? audio_->Pause(paused) : Result<void>{}; }
		/** \brief 登録済みSystemを1 frame更新する */
		[[nodiscard]] Result<void> Update(float deltaTime);

		[[nodiscard]] LevelState		 GetState() const noexcept { return state_; }
		[[nodiscard]] const std::string& GetName() const noexcept { return name_; }
		[[nodiscard]] Scene*			 GetScene() noexcept { return scene_.get(); }
		[[nodiscard]] const Scene*		 GetScene() const noexcept { return scene_.get(); }
		[[nodiscard]] SystemScheduler&	 GetSystemScheduler() noexcept { return scheduler_; }
        /** \brief Runtimeへ渡されたこのframeの入力。保存せず、全ゲーム言語から同じ値を参照する */
        const InputFrame& Input() const noexcept { return input_; }
        void SetFrameInput(const InputFrame& input) noexcept { input_ = input; }

		void SetName(const std::string& name) noexcept { name_ = name; }
		std::optional<LevelLightingSettings>& Lighting() noexcept { return lighting_; }
		const std::optional<LevelLightingSettings>& Lighting() const noexcept { return lighting_; }
        /** Runtime-only effects, set from GameLevelSetup. Not serialized. */
        std::vector<ScreenEffect>& ScreenEffects() noexcept { return screenEffects_; }
        const std::vector<ScreenEffect>& ScreenEffects() const noexcept { return screenEffects_; }

	private:
		friend class LevelSerializer;
		friend class GameRuntime; //< 開始前に値Metadataを確定する。稼働中の公開setterは設けない
		ReflectionRegistration reflection_; //< ホスト側の登録関数。Levelより長く生存するコードを参照する
        InputFrame input_; //< Sceneの保存/Clone対象外。Runtimeが更新し、Reloadで値を複製しない
		std::optional<LevelLightingSettings> lighting_;
        std::vector<ScreenEffect> screenEffects_;

		std::string			   name_;						  //< Editorと診断に使うLevel名
		std::unique_ptr<Scene> scene_;						  //< EntityとComponentを所有するECS World
        std::filesystem::path resourceRoot_; //< Applicationが選択したProject Root。Levelファイルへ保存しない
        std::optional<std::filesystem::path> nextLevel_; //< Project内の検証済み遷移要求。Scene保存/Cloneの対象外
        std::unique_ptr<AudioWorld> audio_; //< Level所有。Scene snapshotには複製せず、client終了時にvoiceを解除する
        PhysicsWorld::ModelSource physicsModels_; //< Host所有cacheへの借用callback。Game/Managed objectは保持しない
		std::unique_ptr<PhysicsWorld> physics_; //< 遅延生成するsolver。Systemより後、Sceneより先に破棄する
		SystemScheduler		   scheduler_;					  //< Sceneへ処理を適用するSystem群。physics_ slotを借用する
		PersistentIdGenerator  persistentIds_;				  //< 新規Entityの永続ID発行元
		LevelState			   state_ = LevelState::Unloaded; //< Worldへのアクセス可否を表す状態
	};

} // namespace NexusEngine
