#include "EditorSettings.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace NexusEngine {
namespace {
using Values = std::map<std::string, std::string>;
Values Read(const std::filesystem::path& path) {
    Values values;
    std::ifstream file(path);
    std::string line, key, value;
    while(std::getline(file, line)) {
        std::istringstream stream(line);
        if(stream >> key >> std::quoted(value)) values[key] = value;
    }
    return values;
}
bool Write(const std::filesystem::path& path, const Values& values) {
    // 同じvolumeへ書いてから置換し、書込み途中の設定を次回起動で読まない。
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if(error) return false;
    auto temp = path; temp += ".tmp";
    std::ofstream file(temp, std::ios::trunc | std::ios::binary);
    for(const auto& [key, value] : values) file << key << ' ' << std::quoted(value) << '\n';
    file.close();
    if(!file) return false;
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
float Number(const Values& values, const char* key, float fallback, float low, float high) {
    auto it = values.find(key);
    if(it == values.end()) return fallback;
    try { float value = std::stof(it->second); return std::isfinite(value) ? std::clamp(value, low, high) : fallback; }
    catch(...) { return fallback; }
}
void String(const Values& values, const char* key, std::string& output) {
    if(auto it = values.find(key); it != values.end()) output = it->second;
}
}
void EditorSettingsStore::Load(const std::filesystem::path& root) {
    user = User{}; editor = Editor{}; project = Project{};
    projectDirectory_ = root / "Config";
    wchar_t* local = nullptr; size_t count = 0;
    _wdupenv_s(&local, &count, L"LOCALAPPDATA");
    userDirectory_ = (local ? std::filesystem::path(local) : std::filesystem::temp_directory_path()) / L"NexusEngine/Editor";
    std::free(local);
    const auto p = Read(projectDirectory_ / "Project.settings");
    String(p, "name", project.name); String(p, "startupLevel", project.startupLevel); String(p, "assetPath", project.assetPath);
    const auto e = Read(projectDirectory_ / "Editor.settings");
    if(e.contains("grid")) editor.grid = e.at("grid") == "true";
    if(e.contains("gizmo")) editor.gizmo = e.at("gizmo") == "true";
    if(e.contains("viewport")) editor.viewport = e.at("viewport") == "true";
    const auto u = Read(userDirectory_ / "User.settings");
    String(u, "language", user.language); String(u, "theme", user.theme);
    if(user.language != "ja") user.language = "en";
    if(user.theme != "Light") user.theme = "Dark";
    user.fontSize = Number(u, "fontSize", 18, 10, 40);
    user.uiScale = Number(u, "uiScale", 1, 0.5f, 3);
    if(u.contains("autoSaveLayout")) user.autoSaveLayout = u.at("autoSaveLayout") == "true";
    for(const auto& [key, value] : u) if(key.starts_with("panel.")) user.panels[key.substr(6)] = value == "true";
    user.recentLevels.clear();
    for(size_t i = 0; i < 10; ++i) {
        auto it = u.find("recent." + std::to_string(i));
        if(it != u.end() && !it->second.empty()) user.recentLevels.push_back(it->second);
    }
}
bool EditorSettingsStore::SaveUser() const {
    Values values {{"language", user.language}, {"theme", user.theme}, {"fontSize", std::to_string(user.fontSize)},
        {"uiScale", std::to_string(user.uiScale)}, {"autoSaveLayout", user.autoSaveLayout ? "true" : "false"}};
    for(const auto& [key, open] : user.panels) values["panel." + key] = open ? "true" : "false";
    for(size_t i = 0; i < std::min<size_t>(user.recentLevels.size(), 10); ++i) values["recent." + std::to_string(i)] = user.recentLevels[i];
    return Write(userDirectory_ / "User.settings", values);
}
bool EditorSettingsStore::SaveProject() const {
    return Write(projectDirectory_ / "Project.settings", {{"name", project.name}, {"startupLevel", project.startupLevel}, {"assetPath", project.assetPath}});
}
bool EditorSettingsStore::SaveEditor() const {
    return Write(projectDirectory_ / "Editor.settings", {{"grid", editor.grid ? "true" : "false"},
        {"gizmo", editor.gizmo ? "true" : "false"}, {"viewport", editor.viewport ? "true" : "false"}});
}
}
