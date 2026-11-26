#pragma once
#include "Runtime/Reflection/TypeRegistry.h"
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * SourceFileActions
 * - Content Browserのソースと共通Reflectionの明示的な宣言位置を照合する
 * - Componentの所有・生成は既存SceneEditServiceへ委譲し、ファイル名から実行型を推測しない
 *---------------------------------------------------------------------------------------*/
struct SourceCreationModule {
    std::string id;
    std::filesystem::path directory;
    bool cpp = false;
};
struct SourceCreationOptions {
    std::string kind = "Behaviour", nameSpace, baseClass, baseHeader, directory, cloneFrom, cloneClass;
};
struct SourceCreationRequest {
    std::string module, name;
    bool cpp = false;
    std::filesystem::path file; //< 検証済みModule内の生成先
    SourceCreationOptions options;
};
/** Browserと既存Project jobの値による接続。生成・登録はAdd-GameSourceへ委譲する。 */
class SourceCreationService final {
public:
    enum class BuildState { Idle, Building, Succeeded, Failed };
    /** ProjectのBuildだけを追跡し、生成やPackageの結果を混同しない。 */
    void SetBuildState(BuildState state, std::string error = {});
    BuildState GetBuildState() const noexcept;
    const std::string &BuildError() const noexcept;
    void Configure(std::filesystem::path project, std::function<bool()> ready,
        std::function<Result<void>(const SourceCreationRequest&)> create);
    bool Available() const noexcept;
    bool CanCreate() const;
    Result<std::vector<SourceCreationModule>> Modules(bool cpp) const;
    Result<void> Create(const SourceCreationModule& module, std::string_view name);
    Result<void> Create(const SourceCreationModule &module, std::string_view name,
                        const SourceCreationOptions &options);
    void Complete(Result<std::filesystem::path> result);
    uint64_t Revision() const noexcept;
    const std::filesystem::path& CreatedFile() const noexcept;
    const std::string& ErrorText() const noexcept;
private:
    std::filesystem::path project_, created_;
    std::function<bool()> ready_;
    std::function<Result<void>(const SourceCreationRequest&)> create_;
    std::string error_;
    BuildState buildState_ = BuildState::Idle;
    std::string buildError_;
    uint64_t revision_ = 0;
};
class SourceFileActions final {
public:
    /** \brief Browserがソースとして扱う拡張子か調べる */
    [[nodiscard]] static bool IsSource(const std::filesystem::path& file);
    /** \brief Root内の実在するソースか検査する。外部symlinkや古いProjectのパスを拒否する */
    [[nodiscard]] static bool Contains(const std::filesystem::path& root, const std::filesystem::path& file);
    /** \brief 現在のRegistryからアタッチ可能な型IDを値で返す。Reloadを跨ぐDescriptor参照を保存しない */
    [[nodiscard]] static std::vector<std::string> TypesFor(const std::filesystem::path& root,
        const std::filesystem::path& file, const TypeRegistry& registry);
};
}
