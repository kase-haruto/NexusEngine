#pragma once
#include "Runtime/Reflection/TypeRegistry.h"
#include <span>
#include <vector>

namespace NexusEngine {
class LocalizationService;
struct EditorContext;
struct ComponentBrowserEntry {
    std::string id, name, category, categoryName, description, icon, origin, originName;
    std::string searchText; //< 正規化済み検索対象。DescriptorやEntity参照をキャッシュしない
};
struct ComponentAddAvailability {
    bool enabled = false;
    std::string reason; //< 翻訳キーまたはRuntimeが返した制約説明
};
/*-----------------------------------------------------------------------------------------
 * ComponentBrowserModel
 * - Registry変更時だけ一覧・分類・検索対象を値として構築する
 * - 追加可否は最新Worldから解決し、Component生成・Entity所有は行わない
 *---------------------------------------------------------------------------------------*/
class ComponentBrowserModel final {
public:
    /** \brief 登録変更と表示言語の切替でだけ一覧を再構築する */
    void Update(const TypeRegistry& registry, const LocalizationService& text);
    const std::vector<ComponentBrowserEntry>& Entries() const noexcept;
    const std::vector<std::pair<std::string, std::string>>& Categories() const noexcept;
    /** \brief 名前・型ID・説明・カテゴリ・検索語へ部分一致する候補を返す */
    std::vector<size_t> Filter(std::string_view query, std::string_view category,
        std::span<const std::string> favorites, bool favoritesOnly = false) const;
    /** \brief 現在の対象全体を検証し、不足分追加・複数instance・必須型を区別する */
    static ComponentAddAvailability Availability(const EditorContext& context, const TypeDescriptor& type,
        std::span<const PersistentId> targets);
private:
    const TypeRegistry* registry_ = nullptr; //< 同一世代でも異なるRegistryへの交換を検出する識別子
    const LocalizationService* text_ = nullptr;
    uint64_t generation_ = 0;
    std::string language_;
    std::vector<ComponentBrowserEntry> entries_;
    std::vector<std::pair<std::string, std::string>> categories_;
};
}
