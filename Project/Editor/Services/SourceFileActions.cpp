#include "SourceFileActions.h"
#include "AssetCatalog.h"
#include "Runtime/Project/ProjectService.h"
#include "Runtime/Project/ProjectJson.h"
#include "Foundation/Utility/String/ConvertString.h"
#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <regex>
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"

namespace NexusEngine {
bool SourceFileActions::IsSource(const std::filesystem::path& file) {
    auto extension = file.extension().wstring();
    std::ranges::transform(extension, extension.begin(), [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return extension == L".cs" || extension == L".cpp" || extension == L".cc" || extension == L".cxx" ||
        extension == L".h" || extension == L".hpp" || extension == L".hxx";
}
bool SourceFileActions::Contains(const std::filesystem::path& root, const std::filesystem::path& file) {
    if(root.empty() || !IsSource(file)) return false;
    std::error_code error;
    const auto resolvedRoot = std::filesystem::weakly_canonical(root, error);
    if(error) return false;
    const auto resolvedFile = std::filesystem::canonical(file, error);
    if(error || !std::filesystem::is_regular_file(resolvedFile, error) || error) return false;
    // segment単位で比較し、SourceOther等の共通prefixやsymlinkによる範囲外を許可しない。
    auto parent = resolvedRoot.begin();
    auto child = resolvedFile.begin();
    for(; parent != resolvedRoot.end(); ++parent, ++child)
        if(child == resolvedFile.end() || _wcsicmp(parent->c_str(), child->c_str()) != 0) return false;
    return child != resolvedFile.end();
}
std::vector<std::string> SourceFileActions::TypesFor(const std::filesystem::path& root,
    const std::filesystem::path& file, const TypeRegistry& registry) {
    std::vector<std::string> result;
    if(!Contains(root, file)) return result;
    for(const auto& [id, type] : registry.Types()) {
        if(!type.add) continue;
        for(const auto& source : type.sourceFiles) {
            const std::filesystem::path declaration{ConvertString(source)};
            auto candidate = declaration.is_absolute() ? declaration : root / declaration;
            if (!Contains(root, candidate)) {
                const auto relocated =
                    ResolveAssetReference(root, AssetCatalog::Utf8(candidate.lexically_relative(root)));
                if (relocated) {
                    candidate = *relocated;
                }
            }
            std::error_code error;
            if(Contains(root, candidate) && std::filesystem::equivalent(file, candidate, error) && !error) {
                result.push_back(id);
                break;
            }
        }
    }
    return result;
}
void SourceCreationService::Configure(std::filesystem::path project, std::function<bool()> ready,
    std::function<Result<void>(const SourceCreationRequest&)> create) {
    project_ = std::move(project);
    ready_ = std::move(ready);
    create_ = std::move(create);
}
bool SourceCreationService::Available() const noexcept {
    return !project_.empty() && static_cast<bool>(create_);
}
void SourceCreationService::SetBuildState(BuildState state, std::string error) {
    buildState_ = state;
    buildError_ = std::move(error);
}
SourceCreationService::BuildState SourceCreationService::GetBuildState() const noexcept {
    return buildState_;
}
const std::string &SourceCreationService::BuildError() const noexcept {
    return buildError_;
}
bool SourceCreationService::CanCreate() const {
    return Available() && (!ready_ || ready_());
}
Result<std::vector<SourceCreationModule>> SourceCreationService::Modules(bool cpp) const {
    if(!Available()) {
        return std::unexpected(Error(ErrorCategory::Resource, 281, "Open a game project to create sources."));
    }
    auto project = ProjectService::Open(project_);
    if(!project) {
        return std::unexpected(project.error());
    }
    std::vector<SourceCreationModule> result;
    for(const auto& module : project->Descriptor().modules) {
        if((module.language == ProjectModuleLanguage::Cpp) == cpp && module.type == ProjectModuleType::Game) {
            result.push_back({module.id, project->Root() / ProjectJson::Path(module.path), cpp});
        }
    }
    return result;
}
Result<void> SourceCreationService::Create(const SourceCreationModule& selected, std::string_view name) {
    return Create(selected, name, SourceCreationOptions{});
}
Result<void> SourceCreationService::Create(const SourceCreationModule &selected, std::string_view name,
                                           const SourceCreationOptions &options) {
    if(!CanCreate()) {
        return std::unexpected(Error(ErrorCategory::Resource, 281, "Stop Play and wait for the current project operation."));
    }
    // OS予約名・言語keywordの最終検証は共通generatorにも残し、既存fileを置換しない。
    if(name.empty() || name.size() > 64 || !((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z'))) {
        return std::unexpected(Error(ErrorCategory::Resource, 281, "Use a class name starting with an ASCII letter (maximum 64 characters)."));
    }
    for(char character : name) {
        if(!((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '_')) {
            return std::unexpected(Error(ErrorCategory::Resource, 281, "Use letters, digits and underscores; omit the extension."));
        }
    }
    // ダイアログ表示後のModule変更を再読込し、古い生成先を実行要求へ渡さない。
    auto modules = Modules(selected.cpp);
    if(!modules) {
        return std::unexpected(modules.error());
    }
    const auto found = std::ranges::find_if(*modules, [&](const auto& module) {
        return module.id == selected.id && module.directory == selected.directory;
    });
    if(found == modules->end()) {
        return std::unexpected(Error(ErrorCategory::Resource, 281, "The selected module changed. Reopen the creation dialog."));
    }
    if (options.kind != "Behaviour" && options.kind != "Class" && options.kind != "Header" &&
        options.kind != "Source") {
        return std::unexpected(Error(ErrorCategory::Resource, 281, "Unknown source kind."));
    }
    const std::regex qualified(R"([A-Za-z_][A-Za-z_0-9]*(?:(?:::|\.)[A-Za-z_][A-Za-z_0-9]*)*)");
    for (const auto &value : {options.nameSpace, options.baseClass}) {
        if (!value.empty() && (value.size() > 256 || !std::regex_match(value, qualified))) {
            return std::unexpected(Error(ErrorCategory::Resource, 281, "Invalid namespace or base class."));
        }
    }
    const auto target = options.directory.empty() ? Result<std::filesystem::path>{found->directory}
                                                  : ResolveFileWithinRoot(found->directory, options.directory);
    if (!target) {
        return std::unexpected(target.error());
    }
    const std::string className(name);
    std::error_code error;
    for(const char* extension : {".cs", ".h", ".cpp"}) {
        const auto file = *target / (className + extension);
        if(std::filesystem::exists(file, error) || error) {
            return std::unexpected(Error(ErrorCategory::Resource, 281, "A source with this name already exists or cannot be inspected."));
        }
    }
    SourceCreationRequest request{
        found->id, className, found->cpp,
        *target / (className + (found->cpp ? (options.kind == "Header" ? ".h" : ".cpp") : ".cs")), options};
    auto started = create_(request);
    if(started) {
        error_.clear();
    }
    return started;
}
void SourceCreationService::Complete(Result<std::filesystem::path> result) {
    created_.clear();
    error_.clear();
    if(result) {
        created_ = std::move(*result);
    } else {
        error_ = result.error().GetMessageText();
    }
    ++revision_;
}
uint64_t SourceCreationService::Revision() const noexcept {
    return revision_;
}
const std::filesystem::path& SourceCreationService::CreatedFile() const noexcept {
    return created_;
}
const std::string& SourceCreationService::ErrorText() const noexcept {
    return error_;
}

}
