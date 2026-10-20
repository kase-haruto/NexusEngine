#include "PropertyEditService.h"
#include <algorithm>

namespace NexusEngine {
const PropertyDescriptor* PropertyEditService::Resolve(const PropertyHandle& handle) const {
    if(auto* type = registry_.Find(handle.type))
        for(const auto& property : type->properties) if(property.id == handle.property) return &property;
    return nullptr;
}
std::optional<PropertyValue> PropertyEditService::Read(const PropertyHandle& handle) const {
    auto* descriptor = Resolve(handle);
    return level_ && descriptor && descriptor->get ? descriptor->get(level_->FindEntity(handle.entity)) : std::nullopt;
}
bool PropertyEditService::Apply(const PropertyHandle& handle, const PropertyValue& value) {
    auto* descriptor = Resolve(handle);
    return level_ && descriptor && descriptor->set && descriptor->set(level_->FindEntity(handle.entity), value);
}
void PropertyEditService::Begin(std::span<const PropertyHandle> handles) {
    if(handles.size() == pending_.size() && std::equal(handles.begin(), handles.end(), pending_.begin(),
        [](const auto& handle, const auto& edit) { return handle == edit.handle; })) return;
    Commit();
    std::vector<Edit> edits;
    for(const auto& handle : handles) {
        auto value = Read(handle);
        if(!value) return;
        edits.push_back({handle, *value, *value});
    }
    pending_ = std::move(edits);
}
bool PropertyEditService::ApplyEdits(const std::vector<Edit>& edits, bool before) {
    std::vector<PropertyValue> previous;
    previous.reserve(edits.size());
    for(const auto& edit : edits) {
        auto value = Read(edit.handle);
        if(!value) return false;
        previous.push_back(*value);
    }
    // Near/Farやprimary切替は相互検証を持つ。適用順に依存しないよう、先に適用できる値から処理する。
    const auto applyValues = [&](const std::vector<PropertyValue>& values) {
        std::vector<bool> complete(edits.size()); size_t remaining = edits.size();
        while(remaining) {
            bool progress = false;
            for(size_t i = 0; i < edits.size(); ++i) if(!complete[i] && Apply(edits[i].handle,values[i])) {
                complete[i] = true; --remaining; progress = true;
            }
            if(!progress) return false;
        }
        return true;
    };
    std::vector<PropertyValue> requested;
    for(const auto& edit : edits) requested.push_back(before ? edit.before : edit.after);
    if(applyValues(requested)) return true;
    // 失敗setterは元を変更しない契約。依存する値も元の有効状態へ順序を解決して戻す。
    applyValues(previous); return false;
}
bool PropertyEditService::Preview(std::span<const PropertyHandle> handles, const PropertyValue& value) {
    const std::vector<PropertyValue> values(handles.size(),value);
    return PreviewValues(handles,values);
}
bool PropertyEditService::PreviewValues(std::span<const PropertyHandle> handles, std::span<const PropertyValue> values) {
    if(handles.size() != values.size()) return false;
    Begin(handles);
    if(pending_.empty() || pending_.size() != handles.size()) return false;
    auto edits = pending_;
    for(size_t i = 0; i < edits.size(); ++i) edits[i].after = values[i];
    if(!ApplyEdits(edits, false)) return false;
    for(auto& edit : pending_) edit.after = *Read(edit.handle);
    return true;
}
bool PropertyEditService::HasPendingChanges() const {
    return std::ranges::any_of(pending_, [](const auto& edit) { return edit.before != edit.after; });
}
void PropertyEditService::Commit() {
    if(HasPendingChanges()) {
        const auto edits = pending_;
        history_.Record({"edit.property", [this, edits] { return ApplyEdits(edits, true); },
            [this, edits] { return ApplyEdits(edits, false); }});
    }
    pending_.clear();
}
bool PropertyEditService::Cancel() {
    if(!ApplyEdits(pending_, true)) return false;
    pending_.clear(); return true;
}
}
