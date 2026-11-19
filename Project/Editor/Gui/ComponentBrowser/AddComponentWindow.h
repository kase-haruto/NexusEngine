#pragma once
#include "ComponentBrowserModel.h"

namespace NexusEngine {
class EditorSettingsStore;
/*-----------------------------------------------------------------------------------------
 * AddComponentWindow
 * - EntityへComponentを追加する軽量Popupを管理する
 * - 検索・カテゴリ・カード・お気に入りを表示し、既存編集サービスへ操作を渡す
 * - Component生成や具体型、World所有権を管理しない
 *---------------------------------------------------------------------------------------*/
class AddComponentWindow final {
public:
    explicit AddComponentWindow(EditorSettingsStore* settings = nullptr);
    /** \brief 選択の永続IDを固定し、Detailsのボタン位置からBrowserを開く */
    void Open(const EditorContext& context);
    /** \brief Browserを描画する。Worldを交換した場合trueを返して旧Entity参照の使用を止める */
    bool Draw(const EditorContext& context);
private:
    bool DrawCard(const ComponentBrowserEntry& entry, const ComponentAddAvailability& availability,
        const EditorContext& context);
    std::vector<std::string>& Favorites();
    void ToggleFavorite(const std::string& id);
    EditorSettingsStore* settings_ = nullptr; //< Workspace所有のUser設定。Runtimeへ渡さない
    ComponentBrowserModel model_;
    std::string search_, category_, error_;
    bool favoritesOnly_ = false, focusSearch_ = false;
    uint32_t popupId_ = 0; //< EntityのImGui IDスコープに依存しないPopup識別子
    size_t levelGeneration_ = 0;
    std::vector<PersistentId> targets_; //< Open時の選択。World交換を跨ぐEntity参照を保持しない
    std::vector<std::string> localFavorites_; //< User設定未接続の単体UIでも同じ表示を提供する
};
}
