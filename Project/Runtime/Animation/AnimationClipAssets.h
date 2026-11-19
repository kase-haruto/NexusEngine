#pragma once
#include "AnimationDocument.h"
#include <memory>

namespace NexusEngine {
/** \brief 不変clip documentの共有cache。Entity clock、pose、GPU資源は所有しない。 */
class AnimationClipAssets final {
public:
    void SetRoot(const std::filesystem::path& root);
    void Invalidate();
    Result<std::shared_ptr<const AnimationClipDocument>> Load(const std::string& reference);
    void Override(const std::string& reference, const AnimationClipDocument& document);
private:
    Result<std::filesystem::path> Key(const std::string& reference) const;
    std::filesystem::path root_; //< Assets範囲
    std::map<std::filesystem::path, std::shared_ptr<const AnimationClipDocument>> clips_; //< 正規化した不変snapshot
};
}
