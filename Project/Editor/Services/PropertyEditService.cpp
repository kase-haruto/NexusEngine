#include "PropertyEditService.h"

namespace NexusEngine {
const PropertyDescriptor* PropertyEditService::Resolve(const PropertyHandle& handle) const {
    if(auto* type = registry_.Find(handle.type))
        for(const auto& property : type->properties) if(property.id == handle.property) return &property;
    return nullptr;
}
std::optional<PropertyValue> PropertyEditService::Read(const PropertyHandle& handle) const {
    auto* descriptor = Resolve(handle);
    return level_ && descriptor ? descriptor->get(level_->FindEntity(handle.entity)) : std::nullopt;
}
bool PropertyEditService::Apply(const PropertyHandle& handle, const PropertyValue& value) {
    auto* descriptor = Resolve(handle);
    return level_ && descriptor && descriptor->set && descriptor->set(level_->FindEntity(handle.entity), value);
}
void PropertyEditService::Begin(const PropertyHandle& handle) {
    if(pending_ && pending_->handle == handle) return;
    Commit();
    if(auto before = Read(handle)) pending_ = Edit{handle, *before, *before};
}
bool PropertyEditService::Preview(const PropertyHandle& handle, const PropertyValue& value) {
    Begin(handle);
    if(!pending_ || !Apply(handle, value)) return false;
    pending_->after = *Read(handle); // Setterの正規化結果を保存する。
    return true;
}
void PropertyEditService::Commit() {
    if(pending_ && pending_->before != pending_->after) {
        if(undo_.size() == 256) undo_.pop_front();
        undo_.push_back(std::move(*pending_)); redo_.clear(); ++revision_;
    }
    pending_.reset();
}
bool PropertyEditService::Undo() {
    Commit();
    if(undo_.empty() || !Apply(undo_.back().handle, undo_.back().before)) return false;
    redo_.push_back(std::move(undo_.back())); undo_.pop_back(); ++revision_; return true;
}
bool PropertyEditService::Redo() {
    Commit();
    if(redo_.empty() || !Apply(redo_.back().handle, redo_.back().after)) return false;
    undo_.push_back(std::move(redo_.back())); redo_.pop_back(); ++revision_; return true;
}
}
