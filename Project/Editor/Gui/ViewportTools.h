#pragma once
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/LocalizationService.h"
#include "Graphics/Model/ModelInstance.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Renderer/RenderScene.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ViewportTools
 * - 編集専用Camera、CPU picking、Local軸Gizmoを提供する
 * - GPU資源は所有せず、値編集はPropertyEditServiceへ渡す
 *---------------------------------------------------------------------------------------*/
class ViewportTools final {
public:
    ViewportTools(EditorSession& session, PropertyEditService& properties) : session_(session), properties_(properties) {}
    /** \brief CPU Geometryの寿命を借用し、meshの境界を一度だけ計算する */
    void SetModel(const ModelAssetData& model, const ModelInstance& pose);
    /** \brief 編集時だけCameraを上書きする。Runtime再生Cameraは保持する */
    void Prepare(RenderScene& frame, float aspect);
    /** \brief 正規化Viewport座標から最近傍の三角形を選択する */
    bool Pick(float x, float y, bool toggle);
    /** \brief 選択の境界へ編集Cameraを寄せる */
    void FocusSelection();
    /** \brief Viewport上の共通ツールと、画像後の入力・Gizmoを描く */
    void DrawToolbar(const LocalizationService& text);
    void DrawOverlay(float x, float y, float width, float height);
private:
    struct Bounds { Vector3 low, high; bool valid = false; };
    EditorSession& session_; //< Worldと選択を借用する
    PropertyEditService& properties_; //< Gizmoの一操作を履歴へまとめる
    const ModelAssetData* model_ = nullptr; //< Application所有
    const ModelInstance* pose_ = nullptr; //< Application所有
    const RenderScene* frame_ = nullptr; //< 次のPrepareまで有効なsnapshot
    std::vector<Bounds> bounds_; //< meshごとのLocal AABB
    Vector3 position_{0, 2, -10}; //< 保存Sceneに含めない編集Camera位置
    float yaw_ = 0, pitch_ = 0; //< 編集Cameraの回転（radian）
    size_t generation_ = static_cast<size_t>(-1); //< Document変更時にCameraを初期化する
    int mode_ = 0; //< 0:移動、1:回転、2:拡縮
    bool navigating_ = false; //< RMB dragのcapture状態
    std::vector<PropertyHandle> dragHandles_; //< Gizmoの複数対象を永続IDで固定する
    std::vector<PropertyValue> dragValues_; //< 各対象の操作開始時のLocal値
};
}
