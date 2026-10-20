#include "Level.h"

// c++
#include <cmath>
#include <utility>

// engine
#include "LevelSerializer.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/TransformUpdateSystem.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kLevelNotLoaded = 30;
		constexpr int32_t kInvalidDeltaTime = 31;
	}

	Level::Level(std::string name) : name_(std::move(name)) {
		// 組み込みSystemはLevelごとに所有し、複数World（Editor/PIE）の状態を分離する。
		scheduler_.AddSystem<TransformUpdateSystem>();
	}

	Result<void> Level::LoadEmpty() {
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
		// World単位でStorageを解放すると、Entityごとの仮想Remove走査を避けてLevel破棄を線形解放にできる。
		scene_.reset();
		state_ = LevelState::Unloaded;
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
