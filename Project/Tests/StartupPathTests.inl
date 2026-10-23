#include "Editor/Core/EditorProjectPaths.h"

void StartupPathTests() {
    const auto repository = std::filesystem::absolute("Generated/EditorTests/StartupPaths");
    const auto expected = (repository / "Project").lexically_normal();
    std::filesystem::create_directories(expected / "Config");
    std::filesystem::create_directories(expected / "Resources/Assets");
    std::filesystem::create_directories(expected / "Shaders");
    { std::ofstream settings(expected / "Config/Project.settings"); settings << "name \"Startup test\"\n"; }
    for(const auto& start : {repository, expected, repository / "Generated/Outputs/Debug",
        repository / "Generated/Outputs/Release"}) {
        const auto root = NexusEngine::FindEditorProjectRoot(start);
        assert(root && *root == expected);
    }
    assert(!NexusEngine::FindEditorProjectRoot(repository.root_path()));
}
