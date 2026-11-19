#pragma once
namespace NexusEngine {
struct EditorContext;
/*-----------------------------------------------------------------------------------------
 * DrawRemoveComponentMenu
 * - 選択Entityに存在する登録型・instanceだけを解除候補として表示する
 * - Component生成・破棄は既存SceneEditServiceへ委譲し、具体型を参照しない
 *---------------------------------------------------------------------------------------*/
/** \brief 解除ボタンとメニューを描画する。World交換に成功した場合trueを返す */
bool DrawRemoveComponentMenu(const EditorContext& context);
}
