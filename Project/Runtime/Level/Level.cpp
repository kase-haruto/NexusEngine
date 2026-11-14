#include "Level.h"

// c++
#include <cmath>
#include <utility>

// engine
#include "LevelSerializer.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/TransformUpdateSystem.h"
#include "Runtime/Scene/AnimationUpdateSystem.h"
#include "Runtime/Physics/PhysicsUpdateSystem.h"
#include "Runtime/Application/ApplicationPaths.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kLevelNotLoaded = 30;
		constexpr int32_t kInvalidDeltaTime = 31;
	}

	Level::Level(std::string name, ReflectionRegistration reflection) : reflection_(reflection), name_(std::move(name)) {
		// 組み込みSystemはLevelごとに所有し、複数World（Editor/PIE）の状態を分離する。
		scheduler_.AddSystem<PhysicsUpdateSystem>(physics_,physicsModels_);
		scheduler_.AddSystem<TransformUpdateSystem>();
		scheduler_.AddSystem<AnimationUpdateSystem>();
	}

	Result<void> Level::LoadEmpty() {
        nextLevel_.reset();
        audio_.reset();
		// 同じアドレスへSceneが再配置されても旧bodyを引き継がないよう、破棄前に解除する。
		physics_.reset();
        screenEffects_.clear();
		lighting_.reset();
		scene_ = std::make_unique<Scene>();
		state_ = LevelState::Loaded;
		return {};
	}

	Result<void> Level::Load(const std::filesystem::path& path) {
		return LevelSerializer::Load(path, *this);
	}

	Result<void> Level::Save(const std::filesystem::path& path) {
		if(scene_ == nullptr) {
			return std::unexpected(Error(ErrorCategory::Scene, kLevelNotLoaded, "Cannot save an unloaded level."));
		}
		return LevelSerializer::Save(path, *this);
	}

	void Level::Unload() noexcept {
        nextLevel_.reset();
        audio_.reset();
		physics_.reset();
        screenEffects_.clear();
		lighting_.reset();
		// World単位でStorageを解放すると、Entityごとの仮想Remove走査を避けてLevel破棄を線形解放にできる。
		scene_.reset();
		state_ = LevelState::Unloaded;
	}

	std::unique_ptr<Scene> Level::ExchangeScene(std::unique_ptr<Scene> scene) noexcept {
		if(!scene) return {};
		// Undo/Reloadの候補SceneはIDが同一でも別World。solverの一時参照を交換前に捨てる。
		physics_.reset();
		scene_.swap(scene);
		state_ = LevelState::Loaded;
		return scene;
	}

	Result<PhysicsWorld::Hit> Level::Raycast(const std::array<float,3>& origin,
		const std::array<float,3>& direction, float distance, uint32_t layerMask) {
		if(!scene_) return std::unexpected(Error(ErrorCategory::Scene, kLevelNotLoaded, "Cannot query an unloaded level."));
		if(!physics_) physics_ = std::make_unique<PhysicsWorld>(physicsModels_);
		return physics_->Raycast(*scene_, origin, direction, distance, layerMask);
	}

    Result<void> Level::SetResourceRoot(std::filesystem::path root) {
        if(!root.is_absolute() || audio_) return std::unexpected(Error(ErrorCategory::Resource,270,"Resource Root must be absolute and selected before Audio initialization."));
        resourceRoot_=std::move(root); return {};
    }
    AudioWorld& Level::Audio() {
        if(!audio_) audio_=std::make_unique<AudioWorld>(resourceRoot_);
        return *audio_;
    }
    Result<void> Level::RequestLevel(const std::filesystem::path& reference) {
        auto path=ResolveProjectPath(resourceRoot_,reference); if(!path) return std::unexpected(path.error());
        std::error_code error;
        if(!std::filesystem::is_regular_file(*path,error) || error) return std::unexpected(Error(ErrorCategory::Resource,273,"Requested Level file does not exist."));
        if(nextLevel_ && *nextLevel_!=*path) return std::unexpected(Error(ErrorCategory::Resource,273,"Another Level transition is already queued for this frame."));
        nextLevel_=std::move(*path); return {};
    }

	Entity Level::CreateEntity(std::string name) {
		if(scene_ == nullptr) {
			return {};
		}
		Entity entity = scene_->CreateEntity(std::move(name));
		try {
			if(entity.AddComponent<PersistentIdComponent>(PersistentIdComponent { persistentIds_.Generate() }) != nullptr) {
				return entity;
			}
		} catch(...) {
			static_cast<void>(scene_->DestroyEntity(entity));
			throw;
		}
		static_cast<void>(scene_->DestroyEntity(entity));
		return {};
	}

	bool Level::DestroyEntity(const Entity entity) {
		return scene_ != nullptr && scene_->DestroyEntity(entity);
	}

	Entity Level::FindEntity(const PersistentId id) {
		Entity result;
		if(scene_ == nullptr || !id.IsValid()) return result;
		scene_->ForEach<PersistentIdComponent>([&](Entity entity, PersistentIdComponent& persistent) {
			if(persistent.value == id) result = entity;
		});
		return result;
	}

	Result<void> Level::Update(const float deltaTime) {
		if(scene_ == nullptr) {
			return std::unexpected(Error(ErrorCategory::Scene, kLevelNotLoaded, "Cannot update an unloaded level."));
		}
		if(!std::isfinite(deltaTime) || deltaTime < 0.0f) {
			return std::unexpected(Error(ErrorCategory::Scene, kInvalidDeltaTime, "Level delta time must be finite and non-negative."));
		}
		return scheduler_.Update(*scene_, deltaTime);
	}

} // namespace NexusEngine
