#pragma once
#include "CurveSerializer.h"
#include <map>
#include <memory>

namespace NexusEngine {
/** \brief 保存済みCurveとpreview snapshotを別管理する小さなCPU asset cache。再生状態は所有しない。 */
class CurveAssets final {
public:
    void SetRoot(const std::filesystem::path& root);
    Result<std::shared_ptr<const FloatCurve>> Load(const std::string& reference);
    /** \brief 同一root内の参照へ未保存snapshotを設定する。既存の利用者のsnapshotは不変。 */
    Result<void> Override(const std::string& reference, const FloatCurve& curve);
    void ClearOverrides();
    void RemoveOverride(const std::string& reference, bool invalidateSaved = false);
    void Invalidate();
private:
    Result<std::filesystem::path> Key(const std::string& reference, bool readAliases = true) const;
    std::filesystem::path root_; //< Assets root。UUID/旧path aliasは共通AssetIdentityで解決する
    std::map<std::filesystem::path, std::shared_ptr<const FloatCurve>> saved_, overrides_; //< 不変snapshot
};
}
