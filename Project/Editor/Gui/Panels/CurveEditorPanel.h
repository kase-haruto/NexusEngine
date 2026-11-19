#pragma once
#include "IEditorPanel.h"
#include "Editor/Services/CurveEditDocument.h"
#include <set>

namespace NexusEngine {
/** \brief 汎用Curveの直接編集画面。Animation/Entityを所有せずdocumentだけを編集する。 */
class CurveEditorPanel final : public IEditorPanel {
public:
    CurveEditorPanel(CurveEditDocument& document, std::filesystem::path root);
    std::string_view Id() const noexcept override;
    std::string_view TitleKey() const noexcept override;
    std::array<float, 2> InitialSize() const noexcept override;
    bool IsScrollable() const noexcept override;
    void Draw(const EditorContext& context) override;
    /** \brief 未保存確認を経由して外部から開く。 */
    void RequestOpen(const std::filesystem::path& file);
private:
    void DrawGraph();
    void DrawSelection();
    void FrameAll();
    void DeleteSelected();
    void AddKey(double x, double y);
    void CopySelected();
    void PasteSelected(double x);
    void DrawEditingTools();
    void Apply(const Result<void>& result);
    CurveEditDocument& document_; //< Workspace所有。panelより長命
    std::filesystem::path root_, pending_; //< asset pickerと保留中のOpen
    std::set<size_t> selected_; //< UI状態。assetには保存しない
    std::vector<CurveKey> dragKeys_; //< drag開始値。Undoはdocumentがまとめる
    std::array<double, 2> dragMouse_{}, minimum_{-.1, -.1}, maximum_{1.1, 2.1}; //< 汎用座標
    std::array<char, 2048> path_{}; //< 保存先のUTF-8相対path
    std::string status_; //< UI診断
    std::string displayedReference_; //< 外部の新規作成/保存先変更をpath欄へ反映する
    std::string pendingSave_; //< 別assetの上書きは確認後に実行する
    int dragKey_ = -1, dragTangent_ = 0; //< -1=in、1=out、0=key移動
    bool pendingNew_ = false, fitted_ = false; //< 破壊的なdocument交換は確認後
    bool snap_ = false, marquee_ = false; //< group移動のsnapと範囲選択
    double snapX_ = .1, snapY_ = .1, pasteX_ = 0;
    int moveAxis_ = 0, dragAxis_ = 0; //< 0 XY / 1 X / 2 Y。Shiftはdrag開始方向を固定
    std::array<double, 2> marqueeStart_{};
    std::set<size_t> marqueeBase_; //< Ctrl/Shiftで範囲選択へ追加する前の選択
    std::vector<std::pair<std::string, FloatCurve>> overlays_; //< 読取専用の比較snapshot
};
}
