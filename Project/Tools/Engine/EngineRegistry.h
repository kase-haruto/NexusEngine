#pragma once
#include "Runtime/Project/ProjectDescriptor.h"
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <optional>
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * EngineInstallation
 * - 検証済みEngineの識別情報と、このマシンで解決した配置を保持する
 * - Projectや実行中のWorldを変更しない
 *---------------------------------------------------------------------------------------*/
struct EngineInstallation {
    ProjectEngineRequirement identity;
    std::string configuration, buildId, toolset, runtimeLibrary;
    std::filesystem::path manifest, root, sdk, data, templates, editor, runtime;
    std::vector<std::filesystem::path> runtimeFiles; //< Gameと同じDirectoryへ配置するEngine動的依存
};
struct RegisteredEngine {
    ProjectEngineRequirement identity;
    std::string configuration, buildId, error;
    std::filesystem::path manifest;
    std::optional<EngineInstallation> installation; //< 未配置・破損Engineも一覧へ残す
};
class EngineManifest final {
public:
    /** \brief Engine配置を検証する。Installedでは固定inventoryも照合する */
    [[nodiscard]] static Result<EngineInstallation> Load(const std::filesystem::path& file);
    /** \brief ソースツリーの明示登録用manifestを作る。既存Engine成果物は変更しない */
    [[nodiscard]] static Result<std::filesystem::path> DescribeDevelopment(const std::filesystem::path& repository,
        const std::string& configuration);
};

/*-----------------------------------------------------------------------------------------
 * EngineRegistry
 * - ユーザー固有のEngine登録と明示的な既定値を管理する
 * - 要求と異なるEngineやconfigurationへfallbackしない
 *---------------------------------------------------------------------------------------*/
class EngineRegistry final {
public:
    explicit EngineRegistry(std::filesystem::path file) : file_(std::filesystem::absolute(file).lexically_normal()) {}
    [[nodiscard]] static Result<std::filesystem::path> DefaultFile();
    [[nodiscard]] Result<std::vector<RegisteredEngine>> Read() const;
    [[nodiscard]] Result<void> Register(const std::filesystem::path& manifest, bool makeDefault = false) const;
    [[nodiscard]] Result<void> Remove(const std::filesystem::path& manifest) const;
    [[nodiscard]] Result<void> SetDefault(const std::filesystem::path& manifest) const;
    [[nodiscard]] Result<std::optional<EngineInstallation>> Default() const;
    /** \brief id / association / version / configurationが一致する一件だけを返す */
    [[nodiscard]] Result<EngineInstallation> Resolve(const ProjectEngineRequirement& requested, const std::string& configuration) const;
private:
    std::filesystem::path file_;
};
}
