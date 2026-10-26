#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
NEXUS_REFLECT_COMPONENT(NameComponent, "Name", "component.name", name)

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
