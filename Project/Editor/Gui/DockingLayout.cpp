#include "DockingLayout.h"
#include "Foundation/Logging/Logger.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include <fstream>
#include <Windows.h>

namespace NexusEngine {
void DockingLayout::Initialize(const std::filesystem::path& directory, std::string_view profile) {
    std::error_code error; std::filesystem::create_directories(directory, error);
    directory_ = directory;
    // ImGuiの自動保存は使わずUser設定のAuto Saveを終了時に適用する。
    ImGui::GetIO().IniFilename = nullptr;
    SwitchProfile(profile, false);
}
void DockingLayout::SwitchProfile(std::string_view profile, bool saveCurrent) {
    if(profile != "General" && profile != "Lighting" && profile != "Animation") return;
    if(saveCurrent && !path_.empty()) Save(true);
    path_ = directory_ / (std::string(profile) + ".ini");
    std::ifstream file(path_, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if(!data.empty()) { ImGui::LoadIniSettingsFromMemory(data.data(), data.size()); reset_ = false; }
    else reset_ = true;
}
void DockingLayout::Draw() {
    const ImGuiID dock = ImGui::GetID("NexusEditorDockspace");
    ImGui::DockSpaceOverViewport(dock, ImGui::GetMainViewport());
    if(!reset_) return;
    reset_ = false;
    // 既存layoutを明示Resetした時だけ再構築し、ユーザーの配置を毎frame上書きしない。
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = dock, left, right, bottom;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.3f, &bottom, &center);
    ImGui::DockBuilderDockWindow("###Outliner", left);
    ImGui::DockBuilderDockWindow("###Details", right);
    ImGuiID properties;
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, &properties, &right);
    ImGui::DockBuilderDockWindow("###Properties", properties);
    ImGui::DockBuilderDockWindow("###Viewport", center);
    ImGui::DockBuilderDockWindow("###ContentBrowser", bottom);
    ImGui::DockBuilderDockWindow("###OutputLog", bottom);
    ImGui::DockBuilderDockWindow("###Terminal", bottom);
    ImGui::DockBuilderFinish(dock);
}
void DockingLayout::Save(bool enabled) {
    if(!enabled) return;
    size_t size = 0; const char* data = ImGui::SaveIniSettingsToMemory(&size);
    auto temporary = path_; temporary += ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(data, static_cast<std::streamsize>(size)); file.close();
    if(!file || !MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        NEXUS_LOG_WARNING("Editor", "Failed to save editor docking layout.");
}
}
