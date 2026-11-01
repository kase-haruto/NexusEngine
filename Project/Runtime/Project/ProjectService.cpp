#include "ProjectService.h"
#include "ProjectSerializer.h"
#include "ProjectJson.h"
#include "Platform/FileSystem/AtomicFile.h"

namespace NexusEngine {
////////////////////////////
// 読込時の検証だけを行い、Descriptorは更新しない
////////////////////////////
Result<ProjectContext> ProjectService::Open(const std::filesystem::path& file) {
    auto descriptor = ProjectSerializer::Load(file);
    if(!descriptor) return std::unexpected(descriptor.error());
    auto context = ProjectContext::Resolve(file, std::move(*descriptor));
    if(!context) return context;
    std::error_code error;
    for(const auto* path : {&context->Assets(), &context->Levels(), &context->Source(), &context->Config()}) {
        if(!std::filesystem::is_directory(*path, error) || error)
            return std::unexpected(Error(ErrorCategory::FileSystem, 164, "Missing project directory: " + ProjectJson::Utf8(*path)));
    }
    if(!std::filesystem::is_regular_file(context->StartupLevel(), error) || error)
        return std::unexpected(Error(ErrorCategory::FileSystem, 164, "Missing startup Level: " + ProjectJson::Utf8(context->StartupLevel())));
    return context;
}

////////////////////////////
// 新規stagingを完成させてから同一volume内でProjectを公開する
////////////////////////////
Result<ProjectContext> ProjectService::Create(const std::filesystem::path& root, ProjectDescriptor descriptor, const Initializer& initialize) {
    if(!initialize) return std::unexpected(Error(ErrorCategory::FileSystem, 164, "Project initializer is required."));
    auto uuid = CreateUuid(); if(!uuid) return std::unexpected(uuid.error());
    if(descriptor.projectId.empty()) descriptor.projectId = *uuid;
    auto valid = ProjectSerializer::Validate(descriptor); if(!valid) return std::unexpected(valid.error());
    std::error_code error;
    auto destination = std::filesystem::absolute(root, error);
    if(error) return std::unexpected(Error(ErrorCategory::FileSystem, 164, "Cannot resolve new Project root: " + error.message()));
    destination = std::filesystem::weakly_canonical(destination, error);
    if(destination.filename().empty()) destination = destination.parent_path();
    if(error || root.empty() || std::filesystem::exists(destination, error) || error || !std::filesystem::is_directory(destination.parent_path(), error) || error)
        return std::unexpected(Error(ErrorCategory::FileSystem, 164, "Project root must be new and its parent must exist."));
    const auto staging = destination.parent_path() / (".nexus-project-" + *uuid);
    if(!std::filesystem::create_directory(staging, error) || error)
        return std::unexpected(Error(ErrorCategory::FileSystem, 164, "Cannot create project staging directory."));
    bool owned = true;
    // この関数で新規作成した一意のstagingだけを削除対象にする。既存Rootは触らない。
    struct Cleanup {
        const std::filesystem::path& path; bool& owned;
        ~Cleanup() { if(owned) { std::error_code ignored; std::filesystem::remove_all(path, ignored); } }
    } cleanup{staging, owned};
    try {
        auto fileName = destination.filename(); fileName += ".nxproject";
        auto context = ProjectContext::Resolve(staging / fileName, descriptor);
        if(!context) return context;
        for(const auto* directory : {&context->Assets(), &context->Levels(), &context->Source(), &context->Config()}) {
            std::filesystem::create_directories(*directory, error);
            if(error) return std::unexpected(Error(ErrorCategory::FileSystem, 164, "Cannot create project directory: " + error.message()));
        }
        auto initialized = initialize(*context);
        if(!initialized) return std::unexpected(initialized.error());
        auto saved = ProjectSerializer::Save(context->File(), descriptor, false);
        if(!saved) return std::unexpected(saved.error());
        auto checked = Open(context->File()); if(!checked) return checked;
        // Gitは空Directoryを記録しない。clone後もDescriptorの必須Directoryを復元できるようにする。
        for(const auto* directory : {&checked->Assets(), &checked->Levels(), &checked->Source(), &checked->Config()}) {
            if(std::filesystem::is_empty(*directory)) {
                auto keep = WriteFileAtomically(*directory / ".gitkeep","",false);
                if(!keep) return std::unexpected(keep.error());
            }
        }
        // 共有データに選ばれたDirectoryを、既定の成果物名だけでGit除外しない。
        std::string ignore;
        for(const char* generated : {"Build", "Intermediate", "Cache", "Generated", ".vs"}) {
            bool shared = false;
            for(const auto* data : {&descriptor.paths.assets, &descriptor.paths.levels, &descriptor.paths.source, &descriptor.paths.config}) {
                const auto relative = ProjectJson::Path(*data);
                shared |= SameFilePath(staging / generated, staging / *relative.begin());
            }
            if(!shared) ignore += std::string("/") + generated + "/\n";
        }
        ignore += "*.user\n/Nexus.Engine.local.props\n";
        auto ignored = WriteFileAtomically(staging / ".gitignore", ignore, false);
        if(!ignored) return std::unexpected(ignored.error());
        auto published = PublishDirectory(staging, destination);
        if(!published) return std::unexpected(published.error());
        owned = false;
        return Open(destination / fileName);
    } catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::FileSystem, 164, std::string("Project creation failed: ") + exception.what())); }
    catch(...) { return std::unexpected(Error(ErrorCategory::FileSystem, 164, "Project initializer failed with an unknown exception.")); }
}
}
