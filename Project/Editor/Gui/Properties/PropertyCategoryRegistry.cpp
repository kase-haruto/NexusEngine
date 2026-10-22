#include "PropertyCategoryRegistry.h"
#include <algorithm>

namespace NexusEngine {
////////////////////////////////////////////////////////////////////////////////////////////
// 起動時に表示順を確定する。PanelのDraw中には登録しない。
////////////////////////////////////////////////////////////////////////////////////////////
bool PropertyCategoryRegistry::Register(PropertyCategory category) {
    if(category.id.empty() || category.labelKey.empty() || !category.isAvailable || !category.drawPage) return false;
    if(std::ranges::any_of(categories_, [&](const auto& entry) { return entry.id == category.id; })) return false;
    categories_.push_back(std::move(category));
    std::ranges::sort(categories_, [](const auto& a, const auto& b) { return a.order == b.order ? a.id < b.id : a.order < b.order; });
    return true;
}
std::vector<const PropertyCategory*> PropertyCategoryRegistry::Available(const EditorContext& context) const {
    std::vector<const PropertyCategory*> result;
    for(const auto& category : categories_) if(category.isAvailable(context)) result.push_back(&category);
    return result;
}
}
