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
				// ForEach内で別のForEachを行わず、選択要求だけを収集する。
				PersistentId selection = session.GetSelection();
				scene->ForEach<NameComponent, PersistentIdComponent>(
					[&](Entity, NameComponent& name, PersistentIdComponent& persistent) {
						const auto itemId = std::to_string(persistent.value.high) + ":" + std::to_string(persistent.value.low);
						ImGui::PushID(itemId.c_str());
						if(ImGui::Selectable(name.name.c_str(), selection == persistent.value)) selection = persistent.value;
						ImGui::PopID();
					});
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
