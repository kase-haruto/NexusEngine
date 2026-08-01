#pragma once

// c++
#include <expected>

// engine
#include "Error.h"

namespace NexusEngine {

	/**
	 * \brief 成功値またはエンジン共通エラーを保持する
	 * \tparam T 成功時の値型
	 */
	template<typename T>
	using Result = std::expected<T, Error>;

} // namespace NexusEngine
