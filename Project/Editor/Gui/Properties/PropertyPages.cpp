#include "PropertyCategoryRegistry.h"
#include "MaterialPage.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Gui/ReflectedPropertyEditor.h"
#include "Runtime/Reflection/TypeRegistry.h"
#include "Editor/Services/LocalizationService.h"

namespace NexusEngine {
namespace {
bool HasType(const EditorContext& context, const char* id) {
    const auto* type = context.types.Find(id);
    return type && type->contains(context.session.GetSelectedEntity());
}
void DrawType(const EditorContext& context, const char* id) {
    const auto* type = context.types.Find(id);
    if(!type) return;
    if(ImGui::CollapsingHeader(context.text.Label(type->labelKey, id).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        for(const auto& property : type->properties) DrawReflectedProperty(context, id, property.id);
}
// 専用Icon fontは既存Editorにないため、公開DrawList APIでDPI追従する図形を描く。
void ObjectIcon(ImDrawList& list, ImVec2 p, float side, ImU32 color) {
    list.AddRect({p.x + side * .25f, p.y + side * .25f}, {p.x + side * .75f, p.y + side * .75f}, color, side * .05f, 0, 1.5f);
    list.AddLine({p.x + side * .25f, p.y + side * .25f}, {p.x + side * .65f, p.y + side * .4f}, color);
    list.AddLine({p.x + side * .65f, p.y + side * .4f}, {p.x + side * .65f, p.y + side * .75f}, color);
}
void MaterialIcon(ImDrawList& list, ImVec2 p, float side, ImU32 color) {
    const ImVec2 center{p.x + side * .5f, p.y + side * .5f};
    list.AddCircle(center, side * .27f, color, 24, 1.5f);
    list.AddEllipse(center, {side * .13f, side * .27f}, color, .5f, 24, 1);
    list.AddLine({p.x + side * .25f, center.y}, {p.x + side * .75f, center.y}, color);
}
void CameraIcon(ImDrawList& list, ImVec2 p, float side, ImU32 color) {
    list.AddRect({p.x + side * .2f, p.y + side * .32f}, {p.x + side * .65f, p.y + side * .68f}, color, 2, 0, 1.5f);
    list.AddTriangle({p.x + side * .65f, p.y + side * .5f}, {p.x + side * .83f, p.y + side * .3f}, {p.x + side * .83f, p.y + side * .7f}, color);
}
}
////////////////////////////////////////////////////////////////////////////////////////////
// 具体的なページの構成だけを登録する。PanelへComponent分岐や生成Factoryを追加しない。
////////////////////////////////////////////////////////////////////////////////////////////
void RegisterPropertyPages(PropertyCategoryRegistry& registry) {
    registry.Register({"Object", "properties.object", 100,
        [](const auto& context) { return HasType(context, "Transform"); },
        [](const auto& context) { DrawType(context, "Name"); DrawType(context, "Transform"); }, ObjectIcon});
    registry.Register({"Material", "properties.material", 300, MaterialPage::IsAvailable, MaterialPage::Draw, MaterialIcon});
    registry.Register({"Camera", "properties.camera", 400,
        [](const auto& context) { return HasType(context, "Camera"); },
        [](const auto& context) { DrawType(context, "Camera"); }, CameraIcon});
}
}
