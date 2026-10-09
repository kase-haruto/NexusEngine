#include "OutlinerPanel.h"

// external
#include <imgui.h>

// engine
#include "../Widgets/SearchBar.h"

void NexusEngine::OutlinerPanel::ShowGui() {
	ImGui::Begin("Outliner", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	NexusEngine::UI::SearchBar("OutlinerSearch", searchText_, "Search...");


	ImGui::End();
}
