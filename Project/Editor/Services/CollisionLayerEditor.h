#pragma once
#include "Runtime/Physics/CollisionLayerSettings.h"
#include "EditorHistory.h"
#include "Editor/Gui/Panels/IEditorPanel.h"

namespace NexusEngine {
/** Project設定専用の履歴。Levelの切替・保存履歴から分離し、適用時に原子的に保存する。 */
class CollisionLayerEditor final {
public:
    Result<void> Initialize(EditorSession& session, const std::filesystem::path& file);
    const CollisionLayerSettings& Settings() const noexcept;
    bool Apply(const CollisionLayerSettings& next);
    bool Undo();
    bool Redo();
    bool CanUndo() const;
    bool CanRedo() const;
    std::vector<uint32_t> VisibleLayers() const;
    size_t Usage(uint32_t layer) const;
    bool DeleteLayer(uint32_t source, uint32_t destination);
    bool BindLevel();
    std::string error;
    uint32_t selected = 0;
    bool focused = false;
private:
    bool Publish(const CollisionLayerSettings& next);
    CollisionLayerSettings settings_;
    std::filesystem::path file_;
    EditorSession* session_ = nullptr;
    EditorHistory history_;
};
/** 名前一覧・三角形マトリクス・選択レイヤー詳細の共通設定画面。 */
class CollisionLayersPanel final : public IEditorPanel {
public:
    explicit CollisionLayersPanel(CollisionLayerEditor& editor);
    std::string_view Id() const noexcept override;
    std::string_view TitleKey() const noexcept override;
    std::array<float, 2> InitialSize() const noexcept override;
    void Draw(const EditorContext& context) override;
private:
    CollisionLayerEditor& editor_;
    std::string search_;
    std::array<char, 97> name_{};
    std::array<char, 97> addName_{};
    std::string displayedName_; //< Undo/Redoで同じ選択の名前が変化した場合も入力欄を更新する
    uint32_t nameLayer_ = 32;
    uint32_t destination_ = 0;
};
}
