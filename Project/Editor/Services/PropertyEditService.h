#pragma once
#include "Runtime/Reflection/TypeRegistry.h"
#include "Runtime/Level/Level.h"
#include <deque>

namespace NexusEngine {
struct PropertyHandle {
    PersistentId entity; //< Componentアドレスを保持しない
    std::string type, property; //< 安定Reflection ID
    friend bool operator==(const PropertyHandle&, const PropertyHandle&) = default;
};
/*-----------------------------------------------------------------------------------------
 * PropertyEditService
 * - Reflection経由で値を編集し、操作単位のUndo/Redoを提供する
 * - Level切替前にResetする。履歴内にはIDと値のみを保存する
 *---------------------------------------------------------------------------------------*/
class PropertyEditService final {
public:
    explicit PropertyEditService(const TypeRegistry& registry) : registry_(registry) {}
    /** \brief World切替時に未確定編集と履歴を破棄する */
    void Reset(Level* level) { level_ = level; undo_.clear(); redo_.clear(); pending_.reset(); }
    /** \brief 現在Worldからプロパティを再解決して値を取得する */
    std::optional<PropertyValue> Read(const PropertyHandle& handle) const;
    /** \brief Slider/Text操作の開始値を記録する */
    void Begin(const PropertyHandle& handle);
    /** \brief 検証付きの値更新をプレビューする */
    bool Preview(const PropertyHandle& handle, const PropertyValue& value);
    /** \brief 操作全体を1件の履歴へ確定する */
    void Commit();
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !pending_ && !undo_.empty(); }
    bool CanRedo() const { return !pending_ && !redo_.empty(); }
    size_t Revision() const { return revision_; }
    bool HasPendingChanges() const { return pending_ && pending_->before != pending_->after; }
private:
    struct Edit { PropertyHandle handle; PropertyValue before, after; };
    const PropertyDescriptor* Resolve(const PropertyHandle& handle) const;
    bool Apply(const PropertyHandle& handle, const PropertyValue& value);
    const TypeRegistry& registry_; //< 起動時登録した型情報
    Level* level_ = nullptr; //< EditorSession所有、Resetで寿命同期
    std::deque<Edit> undo_, redo_; //< 各最大256操作
    std::optional<Edit> pending_; //< 編集中の開始値
    size_t revision_ = 0; //< 保存後変更判定用
};
}
