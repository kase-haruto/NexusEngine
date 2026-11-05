#pragma once
#include "EngineRegistry.h"
#include <stop_token>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * EnginePublisher
 * - ビルド済みDevelopmentを別Rootの固定SDKへ発行する
 * - 既存Installed Engineを更新せず、登録やProjectの版変更は担当しない
 *---------------------------------------------------------------------------------------*/
class EnginePublisher final {
public:
    /** \brief 専用stagingで配置とホスト起動を検証し、新しいInstalled Engineを公開する */
    [[nodiscard]] static Result<EngineInstallation> Publish(const EngineInstallation& source,
        const std::filesystem::path& destination, const ProjectEngineRequirement& installed,
        const std::filesystem::path& log, std::stop_token stop = {});
};
}
