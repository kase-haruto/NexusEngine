#pragma once
#include "Editor/Core/EditorSession.h"
#include "PropertyEditService.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * TransformGizmoTransaction
 * - GizmoのWorld差分を選択EntityのLocal TRSへ変換し、一回の編集履歴にまとめる
 * - 選択された親子の二重変換と、TRSで表せないshearの適用を防ぐ
 *---------------------------------------------------------------------------------------*/
class TransformGizmoTransaction final {
public:
    TransformGizmoTransaction(EditorSession& session, PropertyEditService& properties) : session_(session), properties_(properties) {}
    /** \brief 操作開始時の選択・World・親行列を固定する */
    bool Begin(const Matrix4x4& pivot, bool translationOnly);
    /** \brief 操作開始からのWorld差分を原子的にPreviewする。shear/特異行列を拒否 */
    bool Preview(const Matrix4x4& pivot);
    void Commit();
    void Cancel();
    bool IsActive() const { return !targets_.empty(); }
    bool IsCurrent() const;
private:
    struct Target { Matrix4x4 world, parent; bool parentAffected = false; };
    EditorSession& session_; //< Worldは借用し、Componentアドレスは保持しない
    PropertyEditService& properties_; //< 全TRSを一回の履歴へ登録する
    Matrix4x4 inversePivot_;
    std::vector<Target> targets_;
    std::vector<PersistentId> selected_;
    std::vector<PropertyHandle> handles_;
    std::vector<PropertyValue> originals_;
    size_t generation_ = 0;
    bool translationOnly_ = false;
};
}
