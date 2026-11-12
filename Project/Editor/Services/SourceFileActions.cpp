#include "SourceFileActions.h"
#include "Foundation/Utility/String/ConvertString.h"
#include <algorithm>
#include <cwctype>
#include <cwchar>

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
            const auto candidate = declaration.is_absolute() ? declaration : root / declaration;
            std::error_code error;
            if(Contains(root, candidate) && std::filesystem::equivalent(file, candidate, error) && !error) {
                result.push_back(id);
                break;
            }
        }
    }
    return result;
}
}
