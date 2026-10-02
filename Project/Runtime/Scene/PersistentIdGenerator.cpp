#include "PersistentIdGenerator.h"

// c++
#include <cstdint>
#include <random>

namespace NexusEngine {

	PersistentIdGenerator::PersistentIdGenerator() {
		// addressと時刻はProcess再起動で再現し得るため、保存IDの名前空間にはOS entropyを使う。
		std::random_device entropy;
		namespaceValue_ = (static_cast<uint64_t>(entropy()) << 32) | static_cast<uint64_t>(entropy());
		if(namespaceValue_ == 0) {
			namespaceValue_ = 1;
		}
	}

	PersistentId PersistentIdGenerator::Generate() noexcept {
		return { namespaceValue_, nextValue_.fetch_add(1, std::memory_order_relaxed) };
	}

} // namespace NexusEngine
