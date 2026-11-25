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
#include "Runtime/Physics/CollisionLayerSettings.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kLevelNotLoaded = 30;
		constexpr int32_t kInvalidDeltaTime = 31;
	}

	Level::Level(std::string name, ReflectionRegistration reflection) : reflection_(reflection), name_(std::move(name)) {
		// 組み込みSystemはLevelごとに所有し、複数World（Editor/PIE）の状態を分離する。
		scheduler_.AddSystem<PhysicsUpdateSystem>(physics_,physicsModels_,collisionLayers_);
		scheduler_.AddSystem<TransformUpdateSystem>();
		scheduler_.AddSystem<AnimationUpdateSystem>(animationRuntime_, reflection_);
	}

	Result<void> Level::LoadEmpty() {
        auto configured = collisionConfiguration_.empty() ? Result<CollisionLayerSettings>{CollisionLayerSettings{}} :
            CollisionLayerSettings::Load(collisionConfiguration_);
        if(!configured) {
            return std::unexpected(configured.error());
        }
        collisionLayers_ = configured->EffectiveMatrix();
        if(!collisionConfiguration_.empty()) {
            collisionConfigurationValues_ = std::make_shared<CollisionLayerSettings>(*configured);
        }
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
        // 設定ファイルの不正をWorld交換より先に検出する。
        auto settings = CollisionLayerSettings::Load(collisionConfiguration_);
        if(!collisionConfiguration_.empty() && !settings) {
            return std::unexpected(settings.error());
        }
        auto result = LevelSerializer::Load(path, *this);
        if(result && !collisionConfiguration_.empty()) {
            static_cast<void>(SetCollisionLayers(settings->EffectiveMatrix()));
            collisionConfigurationValues_ = std::make_shared<CollisionLayerSettings>(*settings);
        }
        return result;
	}

	Result<void> Level::Save(const std::filesystem::path& path) {
		if(scene_ == nullptr) {
			return std::unexpected(Error(ErrorCategory::Scene, kLevelNotLoaded, "Cannot save an unloaded level."));
		}
		return LevelSerializer::Save(path, *this);
	}

	void Level::Unload() noexcept {
        animationRuntime_.ResetScene();
        collisionLayers_ = DefaultCollisionMatrix();
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
        animationRuntime_.ResetScene();
		if(!scene) return {};
		// Undo/Reloadの候補SceneはIDが同一でも別World。solverの一時参照を交換前に捨てる。
		physics_.reset();
		scene_.swap(scene);
		state_ = LevelState::Loaded;
		return scene;
	}

	Result<PhysicsWorld::Hit> Level::Raycast(const std::array<float,3>& origin,
		const std::array<float,3>& direction, float distance, uint32_t layerMask, bool includeTriggers, EntityId exclude) {
		if(!scene_) return std::unexpected(Error(ErrorCategory::Scene, kLevelNotLoaded, "Cannot query an unloaded level."));
		if(!physics_) physics_ = std::make_unique<PhysicsWorld>(physicsModels_, collisionLayers_);
        if(collisionConfigurationValues_) {
            // 削除済み所属IDを持つ古いLevelも、移行先レイヤーへのRaycastで取得できるようにする。
            uint32_t expanded = 0;
            for(uint32_t layer = 0; layer < 32; ++layer) {
                const auto resolved = collisionConfigurationValues_->Resolve(layer);
                if(resolved < 32 && (layerMask & (uint32_t{1} << resolved)) != 0) {
                    expanded |= uint32_t{1} << layer;
                }
            }
            layerMask = expanded;
        }
		return physics_->Raycast(*scene_, origin, direction, distance, layerMask, includeTriggers, exclude);
	}

    PhysicsWorld::WireframeStats Level::CollectColliderWireframe(const PhysicsWorld::WireframeOptions& options, std::vector<DebugDrawLine>& output) {
        output.clear();
        if(!scene_) {
            return {};
        }
        if(!physics_) {
            physics_ = std::make_unique<PhysicsWorld>(physicsModels_, collisionLayers_);
        }
        return physics_->CollectWireframe(*scene_, options, output);
    }
    Result<void> Level::SetResourceRoot(std::filesystem::path root) {
        if(!root.is_absolute() || audio_) return std::unexpected(Error(ErrorCategory::Resource,270,"Resource Root must be absolute and selected before Audio initialization."));
        auto mapping = InputMap::Load(root / "Config/InputMap.settings");
        if(!mapping) {
            return std::unexpected(mapping.error());
        }
        auto configured = SetCollisionConfiguration(root / "Config/PhysicsLayers.settings");
        if(!configured) {
            return configured;
        }
        inputMap_.Configure(std::move(*mapping));
        inputConfiguration_ = root / "Config/InputMap.settings";
        animationRuntime_.SetRoot(root / "Assets");
        resourceRoot_ = std::move(root);
        return {};
    }
    Result<void> Level::SetInputConfiguration(const std::filesystem::path& file) {
        auto mapping = InputMap::Load(file);
        if(!mapping) {
            return std::unexpected(mapping.error());
        }
        inputMap_.Configure(std::move(*mapping));
        inputConfiguration_ = file;
        return {};
    }
    const std::filesystem::path& Level::InputConfiguration() const noexcept {
        return inputConfiguration_;
    }
    void Level::SetFrameInput(const InputFrame& input) noexcept {
        input_ = input;
        inputMap_.Advance(input);
    }
    const NexusInputActionsFrameV1& Level::InputActions() const noexcept {
        return inputMap_.Frame();
    }
    bool Level::RequestInputContext(std::string_view context) noexcept {
        return inputMap_.RequestContext(context);
    }
    AnimationRuntime& Level::Animations() noexcept {
        return animationRuntime_;
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

    const CollisionMatrix& Level::CollisionLayers() const noexcept {
        return collisionLayers_;
    }
    const std::filesystem::path& Level::CollisionConfiguration() const noexcept {
        return collisionConfiguration_;
    }
    Result<void> Level::SetCollisionConfiguration(const std::filesystem::path& file) {
        auto loaded = CollisionLayerSettings::Load(file);
        if(!loaded) {
            return std::unexpected(loaded.error());
        }
        static_cast<void>(SetCollisionLayers(loaded->EffectiveMatrix()));
        collisionConfiguration_ = file;
        collisionConfigurationValues_ = std::make_shared<CollisionLayerSettings>(*loaded);
        return {};
    }
    bool Level::SetCollisionLayers(const CollisionMatrix& matrix) noexcept {
        // 対称性を検査してから交換し、無効な読込や編集で現Worldを変更しない。
        for(size_t row = 0; row < matrix.size(); ++row) {
            for(size_t column = 0; column < matrix.size(); ++column) {
                if(((matrix[row] >> column) & 1u) != ((matrix[column] >> row) & 1u)) {
                    return false;
                }
            }
        }
        if(matrix != collisionLayers_) {
            // 既存接触cacheも再生成し、禁止へ変更したpairが残らないようにする。
            physics_.reset();
            collisionLayers_ = matrix;
        }
        return true;
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
