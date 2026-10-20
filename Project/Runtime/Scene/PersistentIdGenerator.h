#pragma once

// c++
#include <atomic>
#include <cstdint>

// engine
#include "Components/PersistentIdComponent.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * PersistentIdGenerator
	 * - 新規Entity用のProcess-localな重複しない永続IDを生成する
	 * - IDの保存、Entityとの関連付け、Runtime EntityId管理は担当しない
	 *---------------------------------------------------------------------------------------*/
	class PersistentIdGenerator final {
	public:
		PersistentIdGenerator();

		/** \brief 新しい永続IDを生成する \return このGenerator内で重複しないID */
		[[nodiscard]] PersistentId Generate() noexcept;

	private:
		uint64_t namespaceValue_ = 0;       //< OS entropyから作る64 bit名前空間
		std::atomic<uint64_t> nextValue_ { 1 }; //< 将来の並列Entity生成でも重複しない連番
	};

} // namespace NexusEngine
