#pragma once
#include "PropertyCategoryRegistry.h"

namespace NexusEngine {
/** \brief 二つの再生componentに専用Detailsを登録する。値更新は共通PropertyHandleで行う。 */
void RegisterAnimationPage(PropertyCategoryRegistry& registry);
}
