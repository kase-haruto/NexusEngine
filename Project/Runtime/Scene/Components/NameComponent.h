#pragma once

// c++
#include <string>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * NameComponent
	 * - Entityの表示名を保持するRuntimeデータ
	 * - Entityの識別や所有権管理には使用しない
	 *---------------------------------------------------------------------------------------*/
	struct NameComponent {
		std::string name = "Entity"; //< Editor表示やデバッグに使用する名前
	};

} // namespace NexusEngine
