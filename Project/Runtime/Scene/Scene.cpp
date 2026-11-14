#include "Scene.h"

// c++
#include <utility>
#include <stdexcept>
#include "EntityReference.h"
#include <charconv>
#include <format>
#include <cmath>
#include <cfloat>

namespace NexusEngine {
    GameTimeState& Scene::Time() noexcept {
        return time_;
    }
    const GameTimeState& Scene::Time() const noexcept {
        return time_;
    }
    bool GameTimeState::BeginFrame(float unscaledDelta) {
        const double scaled = static_cast<double>(unscaledDelta) * timeScale;
        if(!std::isfinite(timeScale) || timeScale < 0 || timeScale > 100 || !std::isfinite(fixedDeltaTime) || fixedDeltaTime < .001f || fixedDeltaTime > 1 ||
            !std::isfinite(unscaledDelta) || unscaledDelta < 0 || !std::isfinite(scaled) || scaled > FLT_MAX || frame == UINT64_MAX) {
            return false;
        }
        activeFixedDelta = fixedDeltaTime;
        unscaledDeltaTime = unscaledDelta;
        deltaTime = static_cast<float>(scaled);
        unscaledElapsed += unscaledDelta;
        elapsed += scaled;
        accumulator += scaled;
        // TimeScale=0では間隔を変更しても蓄積残量を消費しない。再開後のstepへ持ち越す。
        const double steps = timeScale == 0 ? 0 : std::floor((accumulator + fixedDeltaTime * 1e-6) / fixedDeltaTime);
        pendingSteps = static_cast<uint32_t>(std::min(steps, 64.0));
        if(steps > 64) {
            droppedFixedTime += (steps - 64) * fixedDeltaTime;
        }
        accumulator = std::max(0.0, accumulator - steps * fixedDeltaTime);
        inFixedStep = false;
        ++frame;
        return true;
    }
    std::string FormatEntityReference(PersistentId id) {
        if(!id.IsValid()) {
            return {};
        }
        return std::format("{:016x}{:016x}", id.high, id.low);
    }
    std::optional<PersistentId> ParseEntityReference(std::string_view value) {
        if(value.empty()) {
            return PersistentId{};
        }
        if(value.size() != 32) {
            return {};
        }
        PersistentId id;
        const auto high = std::from_chars(value.data(), value.data() + 16, id.high, 16);
        const auto low = std::from_chars(value.data() + 16, value.data() + 32, id.low, 16);
        if(high.ec != std::errc{} || high.ptr != value.data() + 16 || low.ec != std::errc{} || low.ptr != value.data() + 32 || !id.IsValid()) {
            return {};
        }
        return id;
    }
    bool ValidateReferenceValue(std::string_view value, bool component) {
        if(value.empty()) {
            return true;
        }
        if(!component) {
            return ParseEntityReference(value).has_value();
        }
        if(value.size() < 34 || value.size() > 545 || value[32] != '|' || !ParseEntityReference(value.substr(0, 32))) {
            return false;
        }
        for(unsigned char character : value.substr(33)) {
            if(character < 32 || character == 127) {
                return false;
            }
        }
        return true;
    }
    bool Scene::AddDebugLines(uint64_t owner, std::span<const DebugDrawLine> lines, float duration) {
        if(lines.size() > 1024 - debugLines_.size()) {
            return false;
        }
        // 先に必要領域を確保し、確保失敗で途中まで描画要求が残ることを防ぐ。
        const auto required = debugLines_.size() + lines.size();
        if(required > debugLines_.capacity()) {
            debugLines_.reserve(std::min(size_t{1024}, std::max(required, debugLines_.capacity() * 2)));
        }
        for(const auto& line : lines) {
            debugLines_.push_back({line, owner, duration, false});
        }
        return true;
    }
    void Scene::ClearDebugLines(uint64_t owner) {
        std::erase_if(debugLines_, [owner](const DebugEntry& entry) { return entry.owner == owner; });
    }
    void Scene::AdvanceDebugDraw(float deltaTime) {
        std::erase_if(debugLines_, [deltaTime](DebugEntry& entry) {
            if(entry.remaining <= 0) {
                return entry.presented;
            }
            entry.remaining -= deltaTime;
            return entry.remaining <= 0;
        });
    }
    void Scene::CopyDebugLines(std::vector<DebugDrawLine>& output) {
        output.clear();
        output.reserve(debugLines_.size());
        for(auto& entry : debugLines_) {
            output.push_back(entry.line);
            entry.presented = true;
        }
    }
	Scene::EntityLayout Scene::CaptureEntityLayout() const {
		EntityLayout result;
		for(const auto& slot : entitySlots_) {
			result.generations.push_back(slot.generation); result.alive.push_back(slot.alive ? uint8_t{1} : uint8_t{0});
		}
		result.freeIndices = freeEntityIndices_;
		return result;
	}
	void Scene::RestoreEntityLayout(const EntityLayout& layout) {
		if(iterationDepth_ || !componentStorages_.empty()) throw std::logic_error("Entity layout requires empty component storages");
		if(layout.generations.size() != layout.alive.size() || layout.generations.size() >= EntityId::kInvalidIndex)
			throw std::runtime_error("Invalid reinstanced entity layout");
		std::vector<EntitySlot> slots; slots.reserve(layout.generations.size());
		std::vector<bool> free(layout.generations.size(), false);
		for(auto index : layout.freeIndices) {
			if(index >= free.size() || free[index] || layout.alive[index])
				throw std::runtime_error("Invalid reinstanced entity free list");
			free[index] = true;
		}
		size_t living = 0;
		for(size_t i = 0; i < layout.generations.size(); ++i) {
			if(layout.generations[i] == 0 || layout.alive[i] > 1 ||
				(!layout.alive[i] && layout.generations[i] != UINT32_MAX && !free[i]))
				throw std::runtime_error("Invalid reinstanced entity slot");
			slots.push_back({layout.generations[i], layout.alive[i] != 0}); living += layout.alive[i];
		}
		auto freeIndices = layout.freeIndices;
		entitySlots_ = std::move(slots); freeEntityIndices_ = std::move(freeIndices); livingEntityCount_ = living;
	}
	std::unique_ptr<Scene> Scene::Clone() const {
		if(iterationDepth_ != 0) return {};
		auto copy = std::make_unique<Scene>();
		copy->entitySlots_ = entitySlots_;
		copy->freeEntityIndices_ = freeEntityIndices_;
		copy->livingEntityCount_ = livingEntityCount_;
        // Reloadの候補Worldにも時計を引き継ぎ、保存参照と同じ実行時点から再開する。
        copy->time_ = time_;
		for(const auto& [type, storage] : componentStorages_) {
			auto values = storage->Clone();
			if(!values) return {};
			copy->componentStorages_.emplace(type, std::move(values));
		}
		return copy;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// NameとTransformを持つEntityを生成する
	/////////////////////////////////////////////////////////////////////////////////////////
	Entity Scene::CreateEntity(std::string name) {
		if(iterationDepth_ != 0) {
			throw std::logic_error("Entity creation is not allowed during component iteration.");
		}
		uint32_t entityIndex = 0;
		if(!freeEntityIndices_.empty()) {
			entityIndex = freeEntityIndices_.back();
			freeEntityIndices_.pop_back();
		} else {
			if(entitySlots_.size() >= EntityId::kInvalidIndex) {
				throw std::overflow_error("Scene entity capacity is exhausted.");
			}
			entityIndex = static_cast<uint32_t>(entitySlots_.size());
			entitySlots_.push_back({});
		}

		auto& slot = entitySlots_[entityIndex];
		slot.alive = true;
		++livingEntityCount_;
		const EntityId id { entityIndex, slot.generation };

		try {
			auto* nameComponent = AddComponent<NameComponent>(id);
			nameComponent->name = std::move(name);
			static_cast<void>(AddComponent<TransformComponent>(id));
		} catch(...) {
			// Component構築失敗時にEntityだけを公開状態へ残さない。
			static_cast<void>(DestroyEntity(Entity(this, id)));
			throw;
		}
		return Entity(this, id);
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Entityを無効化し、全型Storageから所有Componentを除去する
	/////////////////////////////////////////////////////////////////////////////////////////
	bool Scene::DestroyEntity(const Entity entity) {
		if(iterationDepth_ != 0 || entity.scene_ != this || !IsAlive(entity.id_)) {
			return false;
		}

		// free list拡張に失敗した場合はEntityとComponentを変更せず、呼び出し側へ例外を伝える。
		// 世代をwrapさせると最古のHandleが再び有効になる。最大世代slotは永久退役させる。
		if(entitySlots_[entity.id_.index].generation != (std::numeric_limits<uint32_t>::max)()) {
			freeEntityIndices_.push_back(entity.id_.index);
		}
		for(auto& [componentType, storage] : componentStorages_) {
			static_cast<void>(componentType);
			storage->Remove(entity.id_.index);
		}

		auto& slot = entitySlots_[entity.id_.index];
		slot.alive = false;
		if(slot.generation != (std::numeric_limits<uint32_t>::max)()) {
			++slot.generation;
		}
		--livingEntityCount_;
		return true;
	}

	bool Scene::IsAlive(const EntityId id) const noexcept {
		return id.IsValid() && id.index < entitySlots_.size() &&
		       entitySlots_[id.index].alive && entitySlots_[id.index].generation == id.generation;
	}

} // namespace NexusEngine
