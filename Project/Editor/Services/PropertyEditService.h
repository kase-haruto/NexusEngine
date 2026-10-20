#pragma once
#include "Runtime/Reflection/TypeRegistry.h"
#include "Runtime/Level/Level.h"
#include "EditorHistory.h"
#include <span>

namespace NexusEngine {
struct PropertyHandle {
    PersistentId entity; //< Componentアドレスを保持しない
    std::string type, property; //< 安定Reflection ID
    friend bool operator==(const PropertyHandle&, const PropertyHandle&) = default;
};
/*-----------------------------------------------------------------------------------------
 * PropertyEditService
 * - Reflection経由の検証・複数対象編集・取消を共通履歴へ接続する
 * - Levelより先にResetし、履歴には永続IDと値を保存する
 *---------------------------------------------------------------------------------------*/
class PropertyEditService final {
public:
    explicit PropertyEditService(const TypeRegistry& registry) : registry_(registry) {}
    /** \brief Document切替時に未確定操作と履歴を破棄する */
    void Reset(Level* level) { pending_.clear(); history_.Reset(); level_ = level; }
    std::optional<PropertyValue> Read(const PropertyHandle& handle) const;
    /** \brief 単一または複数のpropertyを一つの操作として開始する */
    void Begin(const PropertyHandle& handle) { Begin(std::span(&handle, 1)); }
    void Begin(std::span<const PropertyHandle> handles);
    bool Preview(const PropertyHandle& handle, const PropertyValue& value) { return Preview(std::span(&handle, 1), value); }
    /** \brief 全対象を検証し、失敗時はそのPreviewの前の値へ戻す */
    bool Preview(std::span<const PropertyHandle> handles, const PropertyValue& value);
    /** \brief 異なる型・値の複数propertyを同じtransactionとして更新する */
    bool PreviewValues(std::span<const PropertyHandle> handles, std::span<const PropertyValue> values);
    void Commit();
    /** \brief 操作開始時の値へ戻す。失敗時は未確定状態を保持する */
    bool Cancel();
    bool Undo() { Commit(); return history_.Undo(); }
    bool Redo() { Commit(); return history_.Redo(); }
    bool CanUndo() const { return history_.CanUndo() || HasPendingChanges(); }
    bool CanRedo() const { return pending_.empty() && history_.CanRedo(); }
    size_t Revision() const { return history_.Revision(); }
    bool HasPendingChanges() const;
    bool IsDirty() const { return history_.IsDirty() || HasPendingChanges(); }
    void MarkSaved() { Commit(); history_.MarkSaved(); }
    EditorHistory& History() { return history_; }
private:
    struct Edit { PropertyHandle handle; PropertyValue before, after; };
    const PropertyDescriptor* Resolve(const PropertyHandle& handle) const;
    bool Apply(const PropertyHandle& handle, const PropertyValue& value);
    bool ApplyEdits(const std::vector<Edit>& edits, bool before);
    const TypeRegistry& registry_; //< Workspace所有
    Level* level_ = nullptr; //< Session所有。Worldのポインタをcacheしない
    EditorHistory history_; //< 構造編集と共有する履歴
    std::vector<Edit> pending_; //< 複数propertyの開始値と最新値
};
}
