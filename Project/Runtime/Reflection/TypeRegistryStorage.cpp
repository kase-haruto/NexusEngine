#include "TypeRegistry.h"
#include <algorithm>
#include <atomic>

// 型登録と世代の管理は、Engine型の具体的な登録処理から分離する。
namespace NexusEngine {
namespace {
std::atomic<uint64_t> registryGeneration {0};
}
bool TypeRegistry::Register(TypeDescriptor type) {
    auto id = type.id;
    if(id.empty() || !types_.emplace(std::move(id), std::move(type)).second) {
        return false;
    }
    generation_ = ++registryGeneration;
    return true;
}
bool TypeRegistry::Unregister(std::string_view id) {
    const auto found = types_.find(id);
    if(found == types_.end()) {
        return false;
    }
    types_.erase(found);
    generation_ = ++registryGeneration;
    return true;
}
uint64_t TypeRegistry::Generation() const noexcept {
    return generation_;
}
void TypeRegistry::MergeGenerated(TypeDescriptor type) {
    const auto found = types_.find(type.id);
    if(found == types_.end()) {
        Register(std::move(type));
        return;
    }
    auto& current = found->second;
    // 手書き登録で追加を公開していない必須型には、生成器から解除権限を補完しない。
    if(!current.remove && (current.add || current.addInstance)) {
        current.remove = std::move(type.remove);
    }
    // 具体型を知らないEditorへ、生成宣言が持つ表示ヒントだけを補完する。
    for(auto member : {&ComponentEditorMetadata::displayName, &ComponentEditorMetadata::category,
        &ComponentEditorMetadata::description, &ComponentEditorMetadata::icon, &ComponentEditorMetadata::origin}) {
        if((current.editor.*member).empty()) {
            current.editor.*member = std::move(type.editor.*member);
        }
    }
    if(current.editor.keywords.empty()) {
        current.editor.keywords = std::move(type.editor.keywords);
    }
    for(auto& source : type.sourceFiles) {
        if(std::ranges::find(current.sourceFiles, source) == current.sourceFiles.end()) {
            current.sourceFiles.push_back(std::move(source));
        }
    }
    for(auto& property : type.properties) {
        const bool exists = std::ranges::any_of(current.properties, [&](const auto& item) {
            return item.id == property.id;
        });
        if(!exists) {
            current.properties.push_back(std::move(property));
        }
    }
    generation_ = ++registryGeneration;
}
}
