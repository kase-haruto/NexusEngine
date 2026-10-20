#include "DockingLayout.h"
#include "Foundation/Logging/Logger.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include <fstream>

namespace NexusEngine {
void DockingLayout::Initialize(const std::filesystem::path& directory) {
    std::error_code error; std::filesystem::create_directories(directory, error);
    path_ = directory / "Layout.ini";
    // ImGuiの自動保存は使わずUser設定のAuto Saveを終了時に適用する。
    ImGui::GetIO().IniFilename = nullptr;
    std::ifstream file(path_, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if(!data.empty()) ImGui::LoadIniSettingsFromMemory(data.data(), data.size());
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
    ImGui::DockBuilderDockWindow("###Viewport", center);
    ImGui::DockBuilderDockWindow("###ContentBrowser", bottom);
    ImGui::DockBuilderDockWindow("###OutputLog", bottom);
    ImGui::DockBuilderDockWindow("###Terminal", bottom);
    ImGui::DockBuilderFinish(dock);
}
void DockingLayout::Save(bool enabled) {
    if(!enabled) return;
    size_t size = 0; const char* data = ImGui::SaveIniSettingsToMemory(&size);
    std::ofstream file(path_, std::ios::binary | std::ios::trunc);
    file.write(data, static_cast<std::streamsize>(size)); file.close();
    if(!file) NEXUS_LOG_WARNING("Editor", "Failed to save editor docking layout.");
}
}
