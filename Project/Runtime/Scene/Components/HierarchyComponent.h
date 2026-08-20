#pragma once

// engine
#include "Runtime/Scene/EntityId.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * HierarchyComponent
	 * - Transform階層における親Entityだけを保持する
	 * - 子一覧は二重管理せず、TransformSystemがSceneから必要時に評価する
	 *---------------------------------------------------------------------------------------*/
	struct HierarchyComponent {
		EntityId parent; //< 無効Idの場合はroot Entity
	};

} // namespace NexusEngine
