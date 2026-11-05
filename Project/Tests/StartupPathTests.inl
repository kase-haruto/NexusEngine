#include "Editor/Core/EditorProjectPaths.h"
#include "Runtime/Application/ApplicationPaths.h"

void StartupPathTests() {
    const auto repository = std::filesystem::absolute("Generated/EditorTests/StartupPaths");
    const auto expected = (repository / "Project").lexically_normal();
    std::filesystem::create_directories(expected / "Config");
    std::filesystem::create_directories(expected / "Resources/Assets");
    std::filesystem::create_directories(expected / "Shaders");
    { std::ofstream settings(expected / "Config/Project.settings"); settings << "name \"Startup test\"\n"; }
    assert(!NexusEngine::LoadProjectSettings(expected));
    { std::ofstream settings(expected / "Config/Project.settings", std::ios::app); settings << "startupLevel \"Levels/Startup.level\"\n"; }
    assert(NexusEngine::LoadProjectSettings(expected));
    for(const auto& start : {repository, expected, repository / "Generated/Outputs/Debug",
        repository / "Generated/Outputs/Release"}) {
        const auto root = NexusEngine::FindEditorProjectRoot(start);
        assert(root && *root == expected);
    }
    assert(!NexusEngine::FindEditorProjectRoot(repository.root_path()));
    const auto movedRoot = std::filesystem::absolute(L"Generated/EditorTests/日本語 Project");
    std::filesystem::create_directories(movedRoot);
    const auto relative = std::filesystem::path{L"Assets/空白 file.gltf"};
    const auto resolved = NexusEngine::ResolveProjectPath(movedRoot, relative);
    assert(resolved && *resolved == (movedRoot / relative).lexically_normal());
    assert(NexusEngine::ResolveProjectPath(movedRoot, *resolved));
    assert(!NexusEngine::ResolveProjectPath(movedRoot, "../Outside/file.gltf"));
    assert(!NexusEngine::ResolveProjectPath(movedRoot, repository / "Outside"));
    assert(!NexusEngine::ResolveProjectPath(movedRoot, L"C:drive-relative"));
    assert(!NexusEngine::ResolveProjectPath("relativeRoot", "Assets/file"));
}
