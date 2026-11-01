#include "ProjectContext.h"
#include "ProjectSerializer.h"
#include "ProjectJson.h"
#include "Runtime/Application/ApplicationPaths.h"

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
} // namespace NexusEngine
