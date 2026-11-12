#include "ProjectContext.h"
#include "ProjectSerializer.h"
#include "ProjectJson.h"
#include "Runtime/Application/ApplicationPaths.h"
#include <algorithm>

namespace NexusEngine {
////////////////////////////
// 共有パスを現在のDescriptor位置から解決する
////////////////////////////
Result<ProjectContext> ProjectContext::Resolve(const std::filesystem::path& file, ProjectDescriptor descriptor) {
    auto valid = ProjectSerializer::Validate(descriptor);
    if(!valid) return std::unexpected(valid.error());
    if(file.empty()) return std::unexpected(Error(ErrorCategory::FileSystem, 162, "Descriptor path is empty."));
    std::error_code error;
    ProjectContext context;
    context.file_ = std::filesystem::absolute(file, error);
    if(error) return std::unexpected(Error(ErrorCategory::FileSystem, 162, "Cannot resolve descriptor path: " + error.message()));
    context.file_ = std::filesystem::weakly_canonical(context.file_, error);
    if(error) return std::unexpected(Error(ErrorCategory::FileSystem, 162, "Cannot resolve descriptor path: " + error.message()));
    context.root_ = context.file_.parent_path();
    const auto& paths = descriptor.paths;
    const std::pair<const std::string*, std::filesystem::path*> directories[] = {
        {&paths.assets, &context.assets_}, {&paths.levels, &context.levels_}, {&paths.source, &context.source_},
        {&paths.config, &context.config_}, {&descriptor.startupLevel, &context.startupLevel_}};
    for(auto [reference, destination] : directories) {
        auto resolved = ResolveProjectPath(context.root_, ProjectJson::Path(*reference));
        if(!resolved) return std::unexpected(resolved.error());
        *destination = *resolved;
    }
    // 起動Levelは設定されたLevel保存先の内側に限定する。
    auto level = ResolveProjectPath(context.levels_, context.startupLevel_);
    if(!level) return std::unexpected(level.error());
    // 宣言だけのModuleも許可するが、既存symlink経由でRoot外を参照させない。
    // 実体の作成・ロードはTools / Runtimeの責務なので、ここでは存在を要求しない。
    for(const auto& module : descriptor.modules) {
        auto resolved = ResolveProjectPath(context.root_, ProjectJson::Path(module.path));
        if(!resolved) return std::unexpected(resolved.error());
    }
    context.descriptor_ = std::move(descriptor);
    return context;
}

Result<void> CheckProjectEngine(const ProjectEngineRequirement& requested, const ProjectEngineRequirement& running) {
    if(requested.association != running.association || requested.id != running.id ||
       (!requested.version.empty() && requested.version != running.version))
        return std::unexpected(Error(ErrorCategory::Framework, 163, "Project requires Engine " + requested.id + " " + requested.version +
            " (" + requested.association + "); this host is " + running.id + " " + running.version + " (" + running.association + "). No fallback was used."));
    if(requested.association == "installed" && requested.version.empty())
        return std::unexpected(Error(ErrorCategory::Framework, 163, "Installed Engine requires an exact version."));
    return {};
}
Result<void> CheckProjectRuntimeModules(const ProjectDescriptor& descriptor) {
    auto valid = ProjectSerializer::Validate(descriptor); if(!valid) return valid;
    // 現在はNative APIの公開をManaged OnCreateより先に完了する。依存宣言でその順序を逆転させない。
    for(const auto& module : descriptor.modules) if(module.language == ProjectModuleLanguage::Cpp)
        for(const auto& dependency : module.dependencies)
            for(const auto& target : descriptor.modules) if(target.id == dependency && target.language == ProjectModuleLanguage::CSharp)
                return std::unexpected(Error(ErrorCategory::Framework, 164, "Current Runtime starts Native before Managed; a Native Module cannot depend on Managed initialization."));
    return {};
}
} // namespace NexusEngine
