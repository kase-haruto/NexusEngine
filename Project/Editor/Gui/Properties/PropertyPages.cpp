#include "PropertyCategoryRegistry.h"
#include "MaterialPage.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Gui/ReflectedPropertyEditor.h"
#include "Runtime/Reflection/TypeRegistry.h"
#include "Editor/Services/LocalizationService.h"
#include <algorithm>
#include <initializer_list>

namespace NexusEngine {
namespace {
bool HasType(const EditorContext& context, const char* id) {
    const auto* type = context.types.Find(id);
    return type && type->contains(context.session.GetSelectedEntity());
}
void DrawType(const EditorContext& context, const char* id, std::initializer_list<std::string_view> excluded = {}) {
    const auto* type = context.types.Find(id);
    if(!type) return;
    if(ImGui::CollapsingHeader(context.text.Label(type->labelKey, id).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        for(const auto& property : type->properties)
            if(std::ranges::find(excluded, property.id) == excluded.end()) DrawReflectedProperty(context, id, property.id);
}
// 名前は共通ヘッダが担当する。専用カテゴリのない型はReflectionから自動的に拾う。
bool IsOtherType(const EditorContext& context, const PropertyCategoryRegistry& registry, const std::string& id, const TypeDescriptor& type) {
    return id != "Name" && type.contains(context.session.GetSelectedEntity()) && !registry.HasCategoryFor(id, context);
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
void RenderingIcon(ImDrawList& list, ImVec2 p, float side, ImU32 color) {
    list.AddTriangle({p.x + side * .5f, p.y + side * .22f}, {p.x + side * .2f, p.y + side * .76f}, {p.x + side * .8f, p.y + side * .76f}, color, 1.5f);
    list.AddLine({p.x + side * .5f, p.y + side * .22f}, {p.x + side * .5f, p.y + side * .76f}, color);
}
void LightIcon(ImDrawList& list, ImVec2 p, float side, ImU32 color) {
    list.AddCircle({p.x + side * .5f, p.y + side * .5f}, side * .16f, color, 20, 1.5f);
    for(float offset : {.17f, .7f}) {
        list.AddLine({p.x + side * .5f, p.y + side * offset}, {p.x + side * .5f, p.y + side * (offset + .13f)}, color);
        list.AddLine({p.x + side * offset, p.y + side * .5f}, {p.x + side * (offset + .13f), p.y + side * .5f}, color);
    }
}
void OtherIcon(ImDrawList& list, ImVec2 p, float side, ImU32 color) {
    for(float y : {.3f, .5f, .7f}) list.AddLine({p.x + side * .25f, p.y + side * y}, {p.x + side * .75f, p.y + side * y}, color, 1.5f);
}
}
////////////////////////////////////////////////////////////////////////////////////////////
// 具体的なページの構成だけを登録する。PanelへComponent分岐や生成Factoryを追加しない。
////////////////////////////////////////////////////////////////////////////////////////////
void RegisterPropertyPages(PropertyCategoryRegistry& registry) {
    registry.Register({"Object", "properties.object", 100,
        [](const auto& context) { return HasType(context, "Transform"); },
        [](const auto& context) { DrawType(context, "Transform"); }, ObjectIcon, {"Transform"}});
    registry.Register({"Rendering", "properties.rendering", 200,
        [](const auto& context) { return HasType(context, "PrimitiveRender"); },
        [](const auto& context) {
            // 材質の既知項目だけを除外し、今後追加される描画プロパティも自動表示する。
            DrawType(context, "PrimitiveRender", {"baseColorTexture", "tint", "materialEnabled", "materialSlot", "materialBaseColor",
                "materialMetallic", "materialRoughness", "materialNormalScale", "materialEmissive", "materialOcclusion"});
        }, RenderingIcon, {"PrimitiveRender"}});
    registry.Register({"Material", "properties.material", 300, MaterialPage::IsAvailable, MaterialPage::Draw, MaterialIcon, {"PrimitiveRender"}});
    registry.Register({"Camera", "properties.camera", 400,
        [](const auto& context) { return HasType(context, "Camera"); },
        [](const auto& context) {
            DrawType(context, "Camera");
            const auto id = context.session.GetSelection();
            const bool active = context.session.GetCameraPreview() == id;
            if(ImGui::Button(context.text.Label(active ? "viewport.previewStop" : "viewport.previewStart", "CameraPreview").c_str())) {
                if(active) context.session.ClearCameraPreview();
                else context.session.SetCameraPreview(id);
            }
            ImGui::TextWrapped("%s", context.text.Text("viewport.previewHelp").c_str());
        }, CameraIcon, {"Camera"}});
    registry.Register({"Light", "properties.light", 500,
        [](const auto& context) { return HasType(context, "DirectionalLight"); },
        [](const auto& context) { DrawType(context, "DirectionalLight"); }, LightIcon, {"DirectionalLight"}});
    registry.Register({"Other", "properties.other", 10000,
        [&registry](const auto& context) {
            return std::ranges::any_of(context.types.Types(), [&](const auto& item) { return IsOtherType(context, registry, item.first, item.second); });
        },
        [&registry](const auto& context) {
            for(const auto& [id, type] : context.types.Types()) if(IsOtherType(context, registry, id, type)) DrawType(context, id.c_str());
        }, OtherIcon});
}
}
