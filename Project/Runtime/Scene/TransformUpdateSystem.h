#pragma once

// c++
#include <array>
#include <typeindex>

// engine
#include "SystemScheduler.h"
#include "TransformSystem.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * TransformUpdateSystem
	 * - TransformSystemをLevelのSystem Schedulerへ接続するAdapter
	 * - 階層編集APIやSystem実行順の所有は担当しない
	 *---------------------------------------------------------------------------------------*/
	class TransformUpdateSystem final : public ISceneSystem {
	public:
		[[nodiscard]] SystemPhase GetPhase() const noexcept override { return SystemPhase::Transform; }
		[[nodiscard]] std::span<const std::type_index> GetReadComponents() const noexcept override;
		[[nodiscard]] std::span<const std::type_index> GetWriteComponents() const noexcept override;
		[[nodiscard]] Result<void> Update(Scene& scene, float deltaTime) override;

	private:
		TransformSystem transformSystem_; //< 既存の階層検証とMatrix更新実装
	};

} // namespace NexusEngine
