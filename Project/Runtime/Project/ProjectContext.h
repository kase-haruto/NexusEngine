#pragma once
#include "ProjectDescriptor.h"
#include "Foundation/Error/Result.h"
#include <filesystem>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ProjectContext
 * - DescriptorとProject Rootから解決した不変の起動情報を保持する
 * - Service、World、Editor、Engineインストールは所有しない
 *---------------------------------------------------------------------------------------*/
class ProjectContext final {
public:
    /** \brief Descriptorの親をRootにして各パスを解決する。実在性はServiceが検証する */
    [[nodiscard]] static Result<ProjectContext> Resolve(const std::filesystem::path& file, ProjectDescriptor descriptor);
    const ProjectDescriptor& Descriptor() const noexcept { return descriptor_; }
    const std::filesystem::path& File() const noexcept { return file_; }
    const std::filesystem::path& Root() const noexcept { return root_; }
    const std::filesystem::path& Assets() const noexcept { return assets_; }
    const std::filesystem::path& Levels() const noexcept { return levels_; }
    const std::filesystem::path& Source() const noexcept { return source_; }
    const std::filesystem::path& Config() const noexcept { return config_; }
    const std::filesystem::path& StartupLevel() const noexcept { return startupLevel_; }
private:
    ProjectDescriptor descriptor_; //< 読込時の共有メタデータのsnapshot
    std::filesystem::path file_, root_; //< 現マシン上で解決したDescriptorとRoot
    std::filesystem::path assets_, levels_, source_, config_, startupLevel_; //< 検証済みの絶対パス
};

/** \brief 実行中ホストの識別情報と要求を照合する。別Engineへのfallbackは行わない */
[[nodiscard]] Result<void> CheckProjectEngine(const ProjectEngineRequirement& requested, const ProjectEngineRequirement& running);
/** \brief 現在のRuntimeが全Moduleの初期化依存を実行できるか検証する。宣言可能なModule数とは区別する */
[[nodiscard]] Result<void> CheckProjectRuntimeModules(const ProjectDescriptor& descriptor);
}
