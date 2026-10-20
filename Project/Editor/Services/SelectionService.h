#pragma once
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include <algorithm>
#include <span>
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * SelectionService
 * - Outliner / Details / Viewport共通の選択IDを保持する
 * - EntityやComponentの寿命を延長しない。World切替時はClearする
 *---------------------------------------------------------------------------------------*/
class SelectionService final {
public:
    /** \brief 通常クリックは置換、Ctrlクリックは選択を反転する */
    void Select(PersistentId id, bool toggle = false) {
        if(!toggle) ids_.clear();
        if(!id.IsValid()) return;
        auto it = std::ranges::find(ids_, id);
        if(it == ids_.end()) ids_.push_back(id); else if(toggle) ids_.erase(it);
    }
    bool Contains(PersistentId id) const { return std::ranges::find(ids_, id) != ids_.end(); }
    PersistentId Primary() const { return ids_.empty() ? PersistentId{} : ids_.back(); }
    std::span<const PersistentId> All() const { return ids_; }
    void Clear() { ids_.clear(); }
    /** \brief Undo復元や範囲選択のID集合を重複なく設定する */
    void Replace(std::span<const PersistentId> ids) {
        std::vector<PersistentId> replacement;
        for(auto id : ids) if(id.IsValid() && std::ranges::find(replacement, id) == replacement.end()) replacement.push_back(id);
        ids_ = std::move(replacement);
    }
private:
    std::vector<PersistentId> ids_; //< Worldをまたいで使わない永続ID集合
};
}
