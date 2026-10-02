#include "SystemScheduler.h"

// engine
#include "Scene.h"

namespace NexusEngine {

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
