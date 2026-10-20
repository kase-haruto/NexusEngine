#pragma once

// c++
#include <cstdint>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * PersistentId
	 * - Level保存データ内でEntityを安定して識別する128 bit値
	 * - 実行時のEntityId、Component位置、生成順序には依存しない
	 *---------------------------------------------------------------------------------------*/
	struct PersistentId {
		uint64_t high = 0; //< Processをまたぐ識別空間を作る上位値
		uint64_t low = 0;  //< 同一Process内で重複を避ける連番値

		[[nodiscard]] constexpr bool IsValid() const noexcept { return high != 0 || low != 0; }
		[[nodiscard]] friend constexpr bool operator==(PersistentId, PersistentId) noexcept = default;
	};

	/*-----------------------------------------------------------------------------------------
	 * PersistentIdComponent
	 * - Serialize、Prefab、Editor参照に使うEntityの永続識別子を保持する
	 * - Runtime Entity Handleの有効性確認には使用しない
	 *---------------------------------------------------------------------------------------*/
	struct PersistentIdComponent {
		PersistentId value; //< Levelファイルへ保存する安定ID
	};

} // namespace NexusEngine
