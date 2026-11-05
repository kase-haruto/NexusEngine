#include "ProjectRegistry.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Project/ProjectService.h"
#include "Runtime/Project/ProjectSerializer.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>

namespace NexusEngine {
using namespace ProjectJson;
Result<std::filesystem::path> ProjectRegistry::DefaultFile() {
    wchar_t* value = nullptr; size_t count = 0;
    _wdupenv_s(&value, &count, L"LOCALAPPDATA");
    std::filesystem::path root = value ? value : L""; std::free(value);
    if(root.empty() || !root.is_absolute()) return std::unexpected(Error(ErrorCategory::FileSystem, 165, "LOCALAPPDATA is unavailable; specify a Registry file."));
    return root / "NexusEngine/Projects/RecentProjects.json";
}

////////////////////////////
// Registryの保存形式とProjectの現在の実在状態を分ける
////////////////////////////
Result<std::vector<RecentProject>> ProjectRegistry::Read() const {
    std::error_code error;
    const bool exists = std::filesystem::exists(file_, error);
    if(error) return std::unexpected(Error(ErrorCategory::FileSystem, 165, "Cannot inspect Registry: " + error.message()));
    if(!exists) return std::vector<RecentProject>{};
    auto json = ProjectJson::Read(file_); if(!json) return std::unexpected(json.error());
    try {
        Keys(*json, {"schemaVersion", "projects"});
        if(!json->at("schemaVersion").is_number_integer() || json->at("schemaVersion") != 1 || !json->at("projects").is_array() || json->at("projects").size() > 64)
            throw std::runtime_error("Invalid Registry schema or project count.");
        std::vector<RecentProject> projects;
        for(const auto& value : json->at("projects")) {
            Keys(value, {"file", "projectId", "name", "engine", "lastUsedUtc"});
            RecentProject project;
            project.file = Path(Text(value, "file"));
            if(!project.file.is_absolute()) throw std::runtime_error("Registry paths must be absolute.");
            project.file = std::filesystem::weakly_canonical(project.file);
            project.projectId = Text(value, "projectId"); project.name = Text(value, "name");
            const auto& engine = value.at("engine"); Keys(engine, {"association", "id", "version"});
            project.engine = {Text(engine, "association"), Text(engine, "id"), Text(engine, "version", true)};
            const auto& time = value.at("lastUsedUtc");
            if(!time.is_number_integer() || time < 0 || time > (std::numeric_limits<int64_t>::max)()) throw std::runtime_error("Invalid recent-project timestamp.");
            project.lastUsedUtc = time.get<int64_t>();
            ProjectDescriptor metadata; metadata.projectId = project.projectId; metadata.name = project.name; metadata.engine = project.engine;
            auto valid = ProjectSerializer::Validate(metadata); if(!valid) throw std::runtime_error(valid.error().GetMessageText());
            for(const auto& previous : projects) if(SameFilePath(previous.file, project.file)) throw std::runtime_error("Duplicate Registry path.");
            if(std::filesystem::exists(project.file)) {
                project.state = RecentProjectState::Invalid;
                auto current = ProjectService::Open(project.file);
                project.state = current && current->Descriptor().projectId == project.projectId ? RecentProjectState::Available : RecentProjectState::Invalid;
            }
            projects.push_back(std::move(project));
        }
        std::ranges::stable_sort(projects, [](const auto& first, const auto& second) { return first.lastUsedUtc > second.lastUsedUtc; });
        return projects;
    } catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::FileSystem, 165, Utf8(file_) + ": " + exception.what())); }
}

Result<void> ProjectRegistry::Save(const std::vector<RecentProject>& projects) const {
    Json values = Json::array();
    for(const auto& project : projects) values.push_back({{"file", Utf8(project.file)}, {"projectId", project.projectId}, {"name", project.name},
        {"engine", {{"association", project.engine.association}, {"id", project.engine.id}, {"version", project.engine.version}}}, {"lastUsedUtc", project.lastUsedUtc}});
    std::error_code error;
    std::filesystem::create_directories(file_.parent_path(), error);
    if(error) return std::unexpected(Error(ErrorCategory::FileSystem, 165, "Cannot create Registry directory: " + error.message()));
    return ProjectJson::Write(file_, Json{{"schemaVersion", 1}, {"projects", values}});
}

////////////////////////////
// 同じパスは更新し、移動済みProjectは欠損している古い記録だけを置き換える
////////////////////////////
Result<void> ProjectRegistry::Register(const ProjectContext& project) const {
    auto opened = ProjectService::Open(project.File()); if(!opened) return std::unexpected(opened.error());
    if(Utf8(opened->File()).size() > 4096) return std::unexpected(Error(ErrorCategory::FileSystem, 165, "Recent-project path exceeds the Registry format limit."));
    if(opened->Descriptor().projectId != project.Descriptor().projectId) return std::unexpected(Error(ErrorCategory::FileSystem, 165, "Project identity changed before registration."));
    return WithFileLock(file_, [&]() -> Result<void> {
        auto projects = Read(); if(!projects) return std::unexpected(projects.error());
        const auto& descriptor = opened->Descriptor();
        std::erase_if(*projects, [&](const auto& previous) { return SameFilePath(previous.file, opened->File()) ||
            (previous.projectId == descriptor.projectId && previous.state == RecentProjectState::Missing); });
        projects->insert(projects->begin(), RecentProject{opened->File(), descriptor.projectId, descriptor.name, descriptor.engine,
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(), RecentProjectState::Available});
        if(projects->size() > 64) projects->resize(64);
        return Save(*projects);
    });
}

Result<void> ProjectRegistry::Remove(const std::filesystem::path& projectFile) const {
    std::error_code error;
    auto path = std::filesystem::absolute(projectFile, error);
    if(error) return std::unexpected(Error(ErrorCategory::FileSystem, 165, "Cannot resolve recent-project path."));
    path = std::filesystem::weakly_canonical(path, error);
    if(error) return std::unexpected(Error(ErrorCategory::FileSystem, 165, "Cannot resolve recent-project path."));
    return WithFileLock(file_, [&]() -> Result<void> {
        auto projects = Read(); if(!projects) return std::unexpected(projects.error());
        std::erase_if(*projects, [&](const auto& project) { return SameFilePath(project.file, path); });
        return Save(*projects);
    });
}
}
