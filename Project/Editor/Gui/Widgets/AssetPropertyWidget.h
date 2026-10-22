#pragma once
#include "Editor/Services/AssetCatalog.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Gui/Core/GuiItem.h"
#include "Runtime/Reflection/TypeRegistry.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Foundation/Error/Result.h"
#include "ThirdParty/imgui/imgui.h"
#include <functional>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * AssetPropertyWidget
 * - 型付きAsset選択、Drop、Node一覧とプレビューの共通UI
 * - ファイル検証と画像生成はApplicationが接続する。GPU資源は所有しない
 *---------------------------------------------------------------------------------------*/
class AssetPropertyWidget final {
public:
    using Validate = std::function<Result<std::string>(const std::filesystem::path&, bool)>;
    using Model = std::function<const ModelAssetData*(const std::string&)>;
    using Preview = std::function<ImTextureRef(const std::string&, bool, int)>;
    void Configure(std::filesystem::path root, Validate validate, Model model, Preview preview);
    /** \brief 一回の選択をActivated/Changed/Committedとして返す */
    UI::ItemResult Draw(const UI::ItemOptions&, PropertyValue&, const PropertyDescriptor&, const LocalizationService&,
        const std::string& mesh, int node);
    /** \brief Browserのhoverにも同じcache画像を表示する */
    void DrawPreview(const std::string& path, bool texture, int node, float size = 112);
    /** \brief Source材質の表示用CPUデータ。返却値は呼出frame内のみ使用する */
    const ModelAssetData* SourceModel(const std::string& mesh) const { return model_ ? model_(mesh) : nullptr; }
private:
    std::unique_ptr<AssetCatalog> catalog_; //< Picker専用の移動状態
    Validate validate_; Model model_; Preview preview_; //< Applicationへの非所有操作窓口
    std::string error_, search_; //< 不正Dropも理由を表示する
};
}
