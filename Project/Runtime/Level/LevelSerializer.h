#pragma once

// c++
#include <filesystem>

// engine
#include "Foundation/Error/Result.h"

namespace NexusEngine {
	class Level;

	/*-----------------------------------------------------------------------------------------
	 * LevelSerializer
	 * - LevelのRuntime Componentをversion付きテキスト形式へ保存・復元する
	 * - Runtime EntityId、System状態、Renderer/GPU Resourceは永続化しない
	 *---------------------------------------------------------------------------------------*/
	class LevelSerializer final {
	public:
		[[nodiscard]] static Result<void> Save(const std::filesystem::path& path, Level& level);
		[[nodiscard]] static Result<void> Load(const std::filesystem::path& path, Level& level);
	};

} // namespace NexusEngine
