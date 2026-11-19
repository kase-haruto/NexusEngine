#include "CurveAssets.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include <cwctype>

namespace NexusEngine {
void CurveAssets::SetRoot(const std::filesystem::path& root) {
    if(root_ != root) {
        Invalidate();
        root_ = root;
    }
}
Result<std::filesystem::path> CurveAssets::Key(const std::string& reference, bool readAliases) const {
    auto path = readAliases ? ResolveAssetReference(root_, reference) : ResolveFileWithinRoot(root_, reference);
    if(!path) {
        return std::unexpected(path.error());
    }
    auto native = path->native();
    // Windowsの大文字差・区切り差・./で同じassetの別snapshotを生成しない。
    for(auto& character : native) {
        character = static_cast<wchar_t>(std::towlower(character));
    }
    auto key = std::filesystem::path(native);
    if(key.extension() != ".curve") {
        return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve asset requires .curve extension"));
    }
    return key;
}
Result<std::shared_ptr<const FloatCurve>> CurveAssets::Load(const std::string& reference) {
    // 未保存draftはfileの存在や、同名旧assetの履歴より優先する。
    auto direct = Key(reference, false);
    if(direct) {
        if(const auto found = overrides_.find(*direct); found != overrides_.end()) {
            return found->second;
        }
    }
    auto key = Key(reference);
    if(!key) {
        return std::unexpected(key.error());
    }
    if(const auto found = overrides_.find(*key); found != overrides_.end()) {
        return found->second;
    }
    if(const auto found = saved_.find(*key); found != saved_.end()) {
        return found->second;
    }
    auto curve = LoadFloatCurve(*key);
    if(!curve) {
        return std::unexpected(curve.error());
    }
    auto asset = std::make_shared<const FloatCurve>(std::move(*curve));
    saved_.emplace(*key, asset);
    return asset;
}
Result<void> CurveAssets::Override(const std::string& reference, const FloatCurve& curve) {
    auto key = Key(reference, false);
    if(!key) {
        return std::unexpected(key.error());
    }
    if(const auto found = overrides_.find(*key); found == overrides_.end() || *found->second != curve) {
        overrides_[*key] = std::make_shared<const FloatCurve>(curve);
    }
    return {};
}
void CurveAssets::ClearOverrides() {
    overrides_.clear();
}
void CurveAssets::RemoveOverride(const std::string& reference, bool invalidateSaved) {
    // 改名前のpathに残ったoverrideも解除する。読込用aliasを保存先と混同しない。
    auto original = ResolveFileWithinRoot(root_, reference);
    if(original) {
        auto native = original->native();
        for(auto& character : native) {
            character = static_cast<wchar_t>(std::towlower(character));
        }
        overrides_.erase(std::filesystem::path(native));
        if(invalidateSaved) {
            saved_.erase(std::filesystem::path(native));
        }
    }
    auto key = Key(reference);
    if(key) {
        overrides_.erase(*key);
        if(invalidateSaved) {
            saved_.erase(*key);
        }
    }
}
void CurveAssets::Invalidate() {
    saved_.clear();
    overrides_.clear();
}
}
