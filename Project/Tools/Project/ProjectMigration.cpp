#include "ProjectMigration.h"
#include "Runtime/Project/ProjectService.h"
#include "Runtime/Project/ProjectSerializer.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <cwctype>

namespace NexusEngine {
namespace {
using namespace ProjectJson;
Error Invalid(const std::string& text) { return Error(ErrorCategory::FileSystem, 204, text); }
Json Engine(const ProjectEngineRequirement& value) { return {{"association",value.association},{"id",value.id},{"version",value.version}}; }
ProjectEngineRequirement Engine(const Json& value) {
    Keys(value,{"association","id","version"}); return {Text(value,"association"),Text(value,"id"),Text(value,"version",true)};
}
Result<std::string> Bytes(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary | std::ios::ate);
    if(!input || input.tellg() < 0 || input.tellg() > 1024 * 1024) return std::unexpected(Invalid("Cannot read migration Descriptor (limit 1 MiB)."));
    input.seekg(0); std::string bytes{std::istreambuf_iterator<char>(input),{}};
    if(input.bad()) return std::unexpected(Invalid("Cannot read migration Descriptor."));
    return bytes;
}
bool Hash(const std::string& value) { return value.size() == 64 && std::ranges::all_of(value,[](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }); }
constexpr const char* BackupDirectories[]{"Build/Migrations","Intermediate/Migrations","Cache/Migrations","Generated/Migrations",".vs/NexusMigrations"};
Result<std::filesystem::path> BackupRoot(const std::filesystem::path& root, const Json& descriptor) {
    // Build等を共有データ名に選んだProjectでも、その中へ復旧JSONを混入させない。
    for(const auto* relative : BackupDirectories) {
        const auto top = *std::filesystem::path(relative).begin(); bool shared = false;
        for(const auto* key : {"assets","levels","source","config"}) {
            const auto data = Path(Text(descriptor.at("paths"),key));
            shared |= SameFilePath(root / top,root / *data.begin());
        }
        if(shared) continue;
        auto resolved = ResolveProjectPath(root,relative); if(resolved) return resolved;
    }
    return std::unexpected(Invalid("Cannot resolve a migration backup directory outside shared Project data."));
}
Result<ProjectMigrationPlan> Inspect(const std::filesystem::path& file, const std::filesystem::path& targetFile) {
    auto before = Sha256File(file); if(!before) return std::unexpected(before.error());
    auto project = ProjectService::Open(file); if(!project) return std::unexpected(project.error());
    auto after = Sha256File(file); if(!after) return std::unexpected(after.error());
    if(*before != *after) return std::unexpected(Invalid("Project changed during migration review. Review it again."));
    auto target = EngineManifest::Load(targetFile); if(!target) return std::unexpected(target.error());
    const auto& previous = project->Descriptor().engine;
    if(previous.association == target->identity.association && previous.id == target->identity.id && previous.version == target->identity.version)
        return std::unexpected(Invalid("Project already requests the selected Engine."));
    const auto compatibilityFile = target->sdk / "Build/Nexus.Compatibility.json";
    if(!std::filesystem::is_regular_file(compatibilityFile))
        return std::unexpected(Invalid("Selected Engine has no migration compatibility metadata. Publish/register an Engine with Nexus.Compatibility.json; the Project was not changed."));
    auto compatibility = ProjectJson::Read(compatibilityFile); if(!compatibility) return std::unexpected(compatibility.error());
    Keys(*compatibility,{"schemaVersion","projectSchema","minimumLevelFormat","maximumLevelFormat","assetStorage","cppCompatibility"});
    for(const auto* key : {"schemaVersion","projectSchema","minimumLevelFormat","maximumLevelFormat"})
        if(!compatibility->at(key).is_number_unsigned() && !compatibility->at(key).is_number_integer()) return std::unexpected(Invalid("Invalid Engine compatibility metadata."));
    const auto minimum = compatibility->at("minimumLevelFormat").get<int64_t>();
    const auto maximum = compatibility->at("maximumLevelFormat").get<int64_t>();
    if(compatibility->at("schemaVersion") != 1 || compatibility->at("projectSchema") != project->Descriptor().schemaVersion ||
       minimum < 1 || maximum < minimum || maximum > 100000 || Text(*compatibility,"assetStorage") != "loose-files-v1" || Text(*compatibility,"cppCompatibility") != "rebuild-required")
        return std::unexpected(Invalid("Target Engine does not declare a supported Descriptor/Level/Asset compatibility contract."));
    ProjectMigrationPlan plan; plan.projectFile = project->File(); plan.projectId = project->Descriptor().projectId;
    plan.targetManifest = target->manifest; plan.previous = previous; plan.target = target->identity; plan.descriptorHash = *before;
    auto manifestHash = Sha256File(target->manifest), compatibilityHash = Sha256File(compatibilityFile);
    if(!manifestHash) return std::unexpected(manifestHash.error()); if(!compatibilityHash) return std::unexpected(compatibilityHash.error());
    plan.manifestHash = *manifestHash; plan.compatibilityHash = *compatibilityHash;
    std::set<std::filesystem::path> candidates{project->StartupLevel()};
    for(const auto& entry : std::filesystem::recursive_directory_iterator(project->Levels())) {
        if(entry.is_symlink()) return std::unexpected(Invalid("Review cannot traverse linked Level entries."));
        auto extension = entry.path().extension().wstring(); std::ranges::transform(extension,extension.begin(),[](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if(entry.is_regular_file() && (extension == L".json" || extension == L".level")) candidates.insert(entry.path());
        if(candidates.size() > 4096) return std::unexpected(Invalid("Too many Levels for a single migration review."));
    }
    for(const auto& candidate : candidates) {
        auto safe = ResolveProjectPath(project->Root(),candidate); if(!safe) return std::unexpected(safe.error());
        std::ifstream input(*safe); std::string magic; int64_t version = 0;
        if(!(input >> magic >> version) || magic != "NEXUS_LEVEL" || version < minimum || version > maximum)
            return std::unexpected(Invalid("Target Engine cannot read Level format: " + Utf8(*safe)));
        auto hash = Sha256File(*safe); if(!hash) return std::unexpected(hash.error());
        plan.levels.push_back(Utf8(safe->lexically_relative(project->Root())) + "\t" + *hash);
    }
    std::ranges::sort(plan.levels);
    plan.notes = {"Descriptor schema matches; only its Engine requirement will change.",
        "Level headers are in the target range. Game component codecs require validation after rebuilding.",
        "Assets remain loose files and are not converted. Target importer compatibility must be checked.",
        "C++ API compatibility is not guaranteed. Rebuild GameRuntime and GameEditor with the selected SDK.",
        "Close this project's Editor and Runtime before applying. Recovery restores only the Descriptor; Level/Asset/Source files are never rewritten."};
    return plan;
}
}
Result<ProjectMigrationPlan> ProjectMigration::Prepare(const std::filesystem::path& project, const std::filesystem::path& targetManifest) {
    try { return Inspect(project,targetManifest); }
    catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
}
Result<void> ProjectMigration::SavePlan(const std::filesystem::path& file, const ProjectMigrationPlan& plan) {
    return ProjectJson::Write(file,{{"schemaVersion",1},{"project",Utf8(plan.projectFile)},{"targetManifest",Utf8(plan.targetManifest)},
        {"projectId",plan.projectId},{"previous",Engine(plan.previous)},{"target",Engine(plan.target)},{"descriptorHash",plan.descriptorHash},
        {"manifestHash",plan.manifestHash},{"compatibilityHash",plan.compatibilityHash},{"levels",plan.levels},{"notes",plan.notes}},false);
}
Result<ProjectMigrationPlan> ProjectMigration::LoadPlan(const std::filesystem::path& file) {
    auto json = ProjectJson::Read(file); if(!json) return std::unexpected(json.error());
    try {
        Keys(*json,{"schemaVersion","project","targetManifest","projectId","previous","target","descriptorHash","manifestHash","compatibilityHash","levels","notes"});
        if(!json->at("schemaVersion").is_number_integer() || json->at("schemaVersion") != 1) throw std::runtime_error("Unsupported migration plan schema.");
        ProjectMigrationPlan plan; plan.projectFile = Path(Text(*json,"project")); plan.targetManifest = Path(Text(*json,"targetManifest"));
        plan.projectId = Text(*json,"projectId"); plan.previous = Engine(json->at("previous")); plan.target = Engine(json->at("target"));
        plan.descriptorHash = Text(*json,"descriptorHash"); plan.manifestHash = Text(*json,"manifestHash"); plan.compatibilityHash = Text(*json,"compatibilityHash");
        plan.levels = json->at("levels").get<std::vector<std::string>>(); plan.notes = json->at("notes").get<std::vector<std::string>>();
        if(!plan.projectFile.is_absolute() || !plan.targetManifest.is_absolute() || !Hash(plan.descriptorHash) || !Hash(plan.manifestHash) || !Hash(plan.compatibilityHash) || plan.levels.size() > 4096)
            throw std::runtime_error("Invalid migration plan preconditions.");
        return plan;
    } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
}
Result<std::filesystem::path> ProjectMigration::Apply(const ProjectMigrationPlan& plan, bool acceptRebuild) {
    if(!acceptRebuild) return std::unexpected(Invalid("Review and acknowledge the rebuild/importer requirements before applying."));
    std::filesystem::path recovery;
    auto applied = WithFileLock(plan.projectFile,[&]() -> Result<void> {
        try {
            auto fresh = Inspect(plan.projectFile,plan.targetManifest); if(!fresh) return std::unexpected(fresh.error());
            if(fresh->descriptorHash != plan.descriptorHash || fresh->manifestHash != plan.manifestHash || fresh->compatibilityHash != plan.compatibilityHash || fresh->levels != plan.levels || fresh->projectId != plan.projectId ||
                Engine(fresh->previous) != Engine(plan.previous) || Engine(fresh->target) != Engine(plan.target))
                return std::unexpected(Invalid("Project, Levels or Engine changed after review. Prepare a new migration plan."));
            auto original = Bytes(plan.projectFile); if(!original) return std::unexpected(original.error());
            auto json = ProjectJson::Read(plan.projectFile); if(!json) return std::unexpected(json.error());
            auto uuid = CreateUuid(); if(!uuid) return std::unexpected(uuid.error());
            auto storage = BackupRoot(plan.projectFile.parent_path(),*json); if(!storage) return std::unexpected(storage.error());
            auto directory = ResolveProjectPath(plan.projectFile.parent_path(),*storage / *uuid);
            if(!directory) return std::unexpected(directory.error());
            std::filesystem::create_directories(directory->parent_path());
            if(!std::filesystem::create_directory(*directory)) return std::unexpected(Invalid("Migration backup directory already exists."));
            auto backup = WriteFileAtomically(*directory / "Original.nxproject",*original,false); if(!backup) return backup;
            auto oldHash = Sha256File(*directory / "Original.nxproject"); if(!oldHash) return std::unexpected(oldHash.error());
            if(*oldHash != plan.descriptorHash) return std::unexpected(Invalid("Project changed before its backup was written."));
            (*json)["engine"] = Engine(fresh->target);
            auto proposed = ProjectJson::Write(*directory / "Proposed.nxproject",*json,false); if(!proposed) return proposed;
            auto validated = ProjectSerializer::Load(*directory / "Proposed.nxproject"); if(!validated) return std::unexpected(validated.error());
            auto newHash = Sha256File(*directory / "Proposed.nxproject"); if(!newHash) return std::unexpected(newHash.error());
            recovery = *directory / "Recovery.json";
            auto receipt = ProjectJson::Write(recovery,{{"schemaVersion",1},{"projectId",plan.projectId},{"originalHash",*oldHash},{"updatedHash",*newHash}},false);
            if(!receipt) return receipt;
            auto currentHash = Sha256File(plan.projectFile); if(!currentHash) return std::unexpected(currentHash.error());
            if(*currentHash != plan.descriptorHash) return std::unexpected(Invalid("Project changed before migration commit; backup retained."));
            auto bytes = Bytes(*directory / "Proposed.nxproject"); if(!bytes) return std::unexpected(bytes.error());
            return WriteFileAtomically(plan.projectFile,*bytes);
        } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
    });
    if(!applied) return std::unexpected(applied.error());
    return recovery;
}
Result<void> ProjectMigration::Restore(const std::filesystem::path& file, const std::filesystem::path& recovery) {
    return WithFileLock(file,[&]() -> Result<void> {
        try {
            // 復旧では旧EngineやLevelの存在を要求しない。移動したProjectもUUIDと内容で検証する。
            auto current = ProjectSerializer::Load(file); if(!current) return std::unexpected(current.error());
            const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute(file)).parent_path();
            std::optional<std::filesystem::path> receiptFile;
            for(const auto* relative : BackupDirectories) {
                auto backups = ResolveProjectPath(root,relative); if(!backups) continue;
                auto candidate = ResolveProjectPath(*backups,std::filesystem::absolute(recovery)); if(candidate) { receiptFile = *candidate; break; }
            }
            if(!receiptFile || receiptFile->filename() != "Recovery.json") return std::unexpected(Invalid("Expected a Project-local migration Recovery.json."));
            auto receipt = ProjectJson::Read(*receiptFile); if(!receipt) return std::unexpected(receipt.error());
            Keys(*receipt,{"schemaVersion","projectId","originalHash","updatedHash"});
            if(!receipt->at("schemaVersion").is_number_integer() || receipt->at("schemaVersion") != 1 || Text(*receipt,"projectId") != current->projectId || !Hash(Text(*receipt,"originalHash")) || !Hash(Text(*receipt,"updatedHash")))
                return std::unexpected(Invalid("Recovery receipt does not match this Project."));
            auto originalFile = ResolveProjectPath(root,receiptFile->parent_path() / "Original.nxproject"); if(!originalFile) return std::unexpected(originalFile.error());
            auto original = ProjectSerializer::Load(*originalFile); if(!original) return std::unexpected(original.error());
            auto backupHash = Sha256File(*originalFile), currentHash = Sha256File(file);
            if(!backupHash) return std::unexpected(backupHash.error()); if(!currentHash) return std::unexpected(currentHash.error());
            if(original->projectId != current->projectId || *backupHash != Text(*receipt,"originalHash")) return std::unexpected(Invalid("Recovery backup changed."));
            if(*currentHash == *backupHash) return {};
            if(*currentHash != Text(*receipt,"updatedHash")) return std::unexpected(Invalid("Project was edited after migration. Recovery will not overwrite those edits."));
            auto bytes = Bytes(*originalFile); if(!bytes) return std::unexpected(bytes.error());
            return WriteFileAtomically(file,*bytes);
        } catch(const std::exception& exception) { return std::unexpected(Invalid(exception.what())); }
    });
}
}
