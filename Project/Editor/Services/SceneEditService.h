#pragma once
#include "PropertyEditService.h"
#include "Editor/Core/EditorSession.h"

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
    bool Create(std::string_view kind);
    /** \brief 選択Entityだけを削除する。残る子はrootへ戻す */
    bool DeleteSelection();
    /** \brief Local Transformを保って親を変更する。循環を拒否する */
    bool Reparent(PersistentId child, PersistentId parent);
private:
    bool Execute(std::string label, const std::function<bool(Level&)>& apply);
    EditorSession& session_; //< Worldと選択の所有者
    PropertyEditService& properties_; //< 共通履歴と編集確定
};
}
