#pragma once
#include "Editor/Gui/Panels/IEditorPanel.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * MaterialPage
 * - Source材質Slotの表示とEntity固有Overrideの編集構成を担当する
 * - 値入力は共通Reflection UI、履歴とDirtyは共通編集サービスへ委譲する
 * - 共有AssetおよびGPU資源は所有・変更しない
 *---------------------------------------------------------------------------------------*/
class MaterialPage final {
public:
    /** \brief 主選択にPrimitiveRenderのReflection記述が存在するか確認する */
    static bool IsAvailable(const EditorContext& context);
    /** \brief 主選択の材質Slot、Surface、Texture参照を描画する */
    static void Draw(const EditorContext& context);
};
}
