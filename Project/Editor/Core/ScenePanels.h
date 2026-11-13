#pragma once

#include "EditorSession.h"
#include "ThirdParty/imgui/imgui.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"

namespace NexusEngine {

	/** \brief 編集WorldのEntity一覧を表示する。親子ツリー表示は未実装 */
	inline void DrawHierarchy(EditorSession& session) {
		if(ImGui::Begin("Hierarchy")) {
			if(auto* level = session.GetLevel(); level != nullptr) {
				if(auto* scene = level->GetScene()) {

					PersistentId selection = session.GetSelection();

					// Hierarchyの縞模様の配色
					ImGui::PushStyleColor(
						ImGuiCol_TableRowBg,
						IM_COL32(20, 20, 20, 255));

					ImGui::PushStyleColor(
						ImGuiCol_TableRowBgAlt,
						IM_COL32(56, 56, 56, 255));

					constexpr ImGuiTableFlags flags =
						ImGuiTableFlags_RowBg |
						ImGuiTableFlags_ScrollY |
						ImGuiTableFlags_NoBordersInBody;

					if(ImGui::BeginTable(
						   "##HierarchyTable",
						   1,
						   flags,
						   ImVec2(0.0f, 0.0f))) {
						ImGui::TableSetupColumn(
							"Entity",
							ImGuiTableColumnFlags_WidthStretch);

						scene->ForEach<NameComponent, PersistentIdComponent>(
							[&](Entity,
								NameComponent&		   name,
								PersistentIdComponent& persistent) {
								ImGui::TableNextRow();
								ImGui::TableSetColumnIndex(0);

								// PersistentIdをImGuiのIDとして使用
								const auto itemId =
									std::to_string(persistent.value.high) +
									":" +
									std::to_string(persistent.value.low);

								ImGui::PushID(itemId.c_str());

								const bool selected =
									selection == persistent.value;

								if(ImGui::Selectable(
									   name.name.c_str(),
									   selected,
									   ImGuiSelectableFlags_SpanAllColumns)) {
									selection = persistent.value;
								}

								ImGui::PopID();
							});

						ImGui::EndTable();
					}

					ImGui::PopStyleColor(2);

					session.Select(selection);
				}
			}
		}

		ImGui::End();
	}


	/** \brief Local Transformを値コピーで編集し、SessionへID付き要求を渡す */
	inline void DrawTransformInspector(EditorSession& session) {
		if(ImGui::Begin("Inspector")) {
			auto entity = session.GetSelectedEntity();
			if(auto* primitive=entity.GetComponent<PrimitiveRenderComponent>()) {
				bool enabled=primitive->staticLighting;
				if(ImGui::Checkbox("Static Lighting (rebake required)",&enabled))
					static_cast<void>(session.SetStaticLighting(session.GetSelection(),enabled));
			}
			if(auto* name = entity.GetComponent<NameComponent>()) ImGui::TextUnformatted(name->name.c_str());
			if(auto* transform = entity.GetComponent<TransformComponent>()) {
				Vector3 translation = transform->translation;
				Vector3 scale = transform->scale;
				Quaternion rotation = transform->rotation;
				bool edited = ImGui::DragFloat3("Translation", &translation.x, 0.1f);
				edited |= ImGui::DragFloat4("Rotation (Quaternion)", &rotation.x, 0.01f);
				edited |= ImGui::DragFloat3("Scale", &scale.x, 0.01f);
				if(edited && !session.SetTransform(session.GetSelection(), translation, rotation, scale)) {
					ImGui::TextUnformatted("Transform rejected: invalid value or missing entity.");
				}
				if(session.IsDirty()) ImGui::TextUnformatted("Modified (saving is not yet connected)");
			} else {
				ImGui::TextUnformatted("Select an entity in Hierarchy.");
			}
		}
		ImGui::End();
	}

	inline void DrawLevelDemoGui(EditorSession& session) {
		// 互換用。LevelのUIはEditorWorkspaceのFile commandへ移動した。
		static_cast<void>(session);
	}
}
