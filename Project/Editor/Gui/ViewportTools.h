#pragma once
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/LocalizationService.h"
#include "Graphics/Model/ModelInstance.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Renderer/RenderScene.h"
#include "Editor/Services/TransformGizmoTransaction.h"
#include <optional>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ViewportTools
 * - 編集専用Camera、CPU picking、ImGuizmoによるLocal操作を提供する
 * - GPU資源は所有せず、値編集はPropertyEditServiceへ渡す
 *---------------------------------------------------------------------------------------*/
class ViewportTools final {
public:
    ViewportTools(EditorSession& session, PropertyEditService& properties) : session_(session), properties_(properties), gizmo_(session, properties) {}
    /** \brief CPU Geometryの寿命を借用し、meshの境界を一度だけ計算する */
    void SetModel(const ModelAssetData& model, const ModelInstance& pose);
    /** \brief 編集時だけCameraを上書きする。Runtime再生Cameraは保持する */
    void Prepare(RenderScene& frame, float aspect);
    /** \brief 正規化Viewport座標から最近傍の三角形を選択する */
    bool Pick(float x, float y, bool toggle);
    /** \brief 選択の境界へ編集Cameraを寄せる */
    void FocusSelection();
    /** \brief 主選択の表示形状の中心とEntityの軸/scaleからGizmo行列を作る。形状がなければEntity原点 */
    std::optional<Matrix4x4> GetSelectionPivot() const;
    /** \brief Viewport上の共通ツールと、画像後の入力・Gizmoを描く */
    void DrawToolbar(const LocalizationService& text);
    void DrawOverlay(float x, float y, float width, float height);
    /** \brief ImGuizmoの操作中はWorkspaceの自動確定・Shortcutを抑止する */
    bool IsManipulating() const { return gizmo_.IsActive(); }
    /** \brief Panelが閉じられた場合もrelease/cancelを完了する */
    void CompleteInteraction();
private:
    struct Bounds { Vector3 low, high; bool valid = false; };
    /** \brief NodeのOffsetを含むEntity空間の境界。World AABBを中心にして回転中にpivotをずらさない */
    std::optional<Bounds> GetEntityLocalBounds(Entity entity) const;
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
    TransformGizmoTransaction gizmo_; //< ImGuizmoの一操作とLocal TRS履歴の接続
    bool blockedUntilRelease_ = false; //< Escape取消直後の同じmouse pressで再開しない
    bool gizmoRejected_ = false; //< TRSで表せない操作をViewportへ通知する
    bool gizmoOwnsMouse_ = false; //< Gizmoが掴んだpressをrelease/取消までScene pickingへ渡さない
};
}
