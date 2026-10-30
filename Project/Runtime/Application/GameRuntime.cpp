#include "GameRuntime.h"
#include "Runtime/Scene/TransformSystem.h"
#include <cmath>
#include <exception>

namespace NexusEngine {
bool GameRuntime::SetLevelSetup(GameLevelSetup setup) {
    if(level_ || starting_) return false;
    setup_ = std::move(setup);
    return true;
}

Result<void> GameRuntime::Start(std::unique_ptr<Level> level) {
    if(level_ || starting_ || !level || !level->GetScene())
        return std::unexpected(Error(ErrorCategory::Framework, 40, "Game runtime requires a loaded level and a stopped session."));
    starting_ = true;
    // Setupの失敗・例外で半構築のLevelを公開しない。古い編集Worldも変更しない。
    struct Reset { bool& flag; ~Reset() { flag = false; } } reset{starting_};
    try {
        if(setup_) {
            auto result = setup_(*level);
            if(!result) return result;
        }
        if(!level->GetScene())
            return std::unexpected(Error(ErrorCategory::Framework, 41, "Game setup unloaded its level."));
        // 初期Matrixだけを確定する。ゲームSystemをUpdate(0)で余分に実行しない。
        auto transforms = TransformSystem{}.Update(*level->GetScene());
        if(!transforms) return transforms;
        level_ = std::move(level);
        paused_ = false;
        return {};
    } catch(const std::exception& exception) {
        return std::unexpected(Error(ErrorCategory::Framework, 42, std::string("Game level setup failed: ") + exception.what()));
    } catch(...) {
        return std::unexpected(Error(ErrorCategory::Framework, 42, "Game level setup failed with an unknown exception."));
    }
}

void GameRuntime::Stop() noexcept {
    level_.reset();
    paused_ = false;
}

Result<void> GameRuntime::Update(float deltaTime) {
    if(!std::isfinite(deltaTime) || deltaTime < 0)
        return std::unexpected(Error(ErrorCategory::Framework, 43, "Game delta time must be finite and non-negative."));
    if(!level_ || paused_) return {};
    return level_->Update(deltaTime);
}
}
