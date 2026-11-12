#pragma once
#include "PropertyEditService.h"
#include "Editor/Core/EditorSession.h"
#include <span>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * SceneEditService
 * - 作成・削除・親子変更を原子的なWorld交換として共通履歴に登録する
 * - 未知のComponentもRuntime snapshotで保持し、複製不可なら変更を拒否する
 *---------------------------------------------------------------------------------------*/
class SceneEditService final {
public:
    SceneEditService(EditorSession& session, PropertyEditService& properties) : session_(session), properties_(properties) {}
    /** \brief 指定Component構成のEntityを追加し選択する */
    bool Create(std::string_view kind, std::string_view name = {});
    /** \brief 選択Entityだけを削除する。残る子はrootへ戻す */
    bool DeleteSelection();
    /** \brief Local Transformを保って親を変更する。循環を拒否する */
    bool Reparent(PersistentId child, PersistentId parent);
    /** \brief 選択集合で不足するComponentを一括追加し、共通履歴へ記録する */
    bool AddComponent(const TypeDescriptor& type);
    /** \brief 同じ型でも新しい保存instanceを選択Entityへ追加し、既存Undoへ記録する */
    bool AddScriptInstance(const TypeDescriptor& type);
    /** \brief drop先Entityへ追加する。選択を変更せず、一括変更・Undoを既存transactionへ通す */
    bool AddComponent(const TypeDescriptor& type, std::span<const PersistentId> targets);
    /** \brief DLL値Componentを選択から解除する。値と関連付けを同じUndo transactionで保存する */
    bool RemoveValueComponent(const TypeDescriptor& type);
    /** \brief 選択EntityのScript関連付けを共通transactionで解除する。Undoで型と公開値を復元する */
    bool DetachScripts();
    /** \brief 指定TypeのScriptだけを共通transactionで解除し、別Scriptの保存値を維持する */
    bool DetachScript(const TypeDescriptor& type, PersistentId instance = {});
private:
    bool Execute(std::string label, const std::function<bool(Level&)>& apply);
    EditorSession& session_; //< Worldと選択の所有者
    PropertyEditService& properties_; //< 共通履歴と編集確定
};
}
