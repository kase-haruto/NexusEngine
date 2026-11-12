#include "SystemScheduler.h"

// engine
#include "Scene.h"

namespace NexusEngine {
    ISceneSystem& SystemScheduler::AddOwnedSystem(std::unique_ptr<ISceneSystem> system) {
        if(executing_ || !system) throw std::logic_error("System registration requires an idle scheduler and a valid owner.");
        auto& reference = *system;
        // DLL adapterも従来Systemと同じphase・登録順で実行し、別System実行基盤を作らない。
        auto position = systems_.begin();
        while(position != systems_.end() && (*position)->GetPhase() <= system->GetPhase()) ++position;
        systems_.insert(position, std::move(system)); return reference;
    }
    bool SystemScheduler::RemoveSystem(ISceneSystem& system) noexcept {
        if(executing_) return false;
        for(auto position = systems_.begin(); position != systems_.end(); ++position) if(position->get() == &system) {
            // callbackは呼出元が停止済み。Engine adapterの破棄もDLL解放より前に完了させる。
            systems_.erase(position); return true;
        }
        return false;
    }

	Result<void> SystemScheduler::Update(Scene& scene, const float deltaTime) {
		if(executing_) {
			return std::unexpected(Error(ErrorCategory::Scene, 32, "System scheduler update cannot be reentered."));
		}
		// Systemが例外を投げても登録禁止状態を解除するため、状態はscopeで復元する。
		struct ExecutionGuard {
			bool& state;
			~ExecutionGuard() { state = false; }
		} guard { executing_ };
		executing_ = true;
		for(const auto& system : systems_) {
			auto result = system->Update(scene, deltaTime);
			if(!result) {
				return result;
			}
		}
		return {};
	}

} // namespace NexusEngine
