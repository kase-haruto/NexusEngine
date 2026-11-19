#include "AnimationPage.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/AssetCatalog.h"
#include "Editor/Services/CurveEditDocument.h"
#include "Editor/Services/EditorCommandRegistry.h"
#include "Editor/Gui/ReflectedPropertyEditor.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Animation/RetargetProfile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include "Runtime/Scene/Components/CurveBindingComponent.h"

namespace NexusEngine {
namespace {
void Set(const EditorContext& context, const char* type, const char* property, PropertyValue value) {
    const PropertyHandle handle{context.session.GetSelection(), type, property, {}};
    context.properties.Begin(handle);
    if(!context.properties.Preview(handle, value)) {
        context.properties.Cancel();
    }
    context.properties.Commit();
}
std::string String(const EditorContext& context, const char* type, const char* property) {
    const auto value = context.properties.Read({context.session.GetSelection(), type, property, {}});
    if(value) {
        if(const auto* text = std::get_if<std::string>(&*value)) {
            return *text;
        }
    }
    return {};
}
void Asset(const EditorContext& context, const char* type, const char* property, const char* label, const char* extension) {
    const auto reference = String(context, type, property);
    std::string display = reference;
    if(context.content && IsAssetIdentity(reference)) {
        const auto path = ResolveAssetReference(context.content->Root(), reference);
        if(path) {
            display = AssetCatalog::Utf8(path->lexically_relative(context.content->Root()));
        }
    }
    ImGui::PushID(property);
    if(ImGui::BeginCombo(label, reference.empty() ? "未設定" : display.c_str())) {
        if(ImGui::Selectable("解除", reference.empty())) {
            Set(context, type, property, std::string{});
        }
        if(context.content) {
            std::error_code error;
            const auto& root = context.content->Root();
            for(std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
                it != end && !error; it.increment(error)) {
                if(it->path().extension() == extension) {
                    const auto relative = AssetCatalog::Utf8(it->path().lexically_relative(root));
                    if(ImGui::Selectable(relative.c_str(), SameAssetReference(root, relative, reference))) {
                        const auto identity = context.content->StableReference(it->path());
                        Set(context, type, property, identity ? *identity : relative);
                    }
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
}
void DrawCurveBinding(const EditorContext& context) {
    const char* type = "CurveBinding";
    Asset(context, type, "curveAsset", "Curve", ".curve");
    if(ImGui::Button("カーブを開く")) {
        context.commands.Execute("curve.editSelection.CurveBinding");
    }
    ImGui::SameLine();
    if(ImGui::Button("新規カーブ")) {
        context.commands.Execute("curve.newSelection.CurveBinding");
    }
    DrawReflectedProperty(context, type, "target");
    auto target = context.session.GetSelectedEntity();
    if(context.session.GetLevel()) {
        target = context.session.GetLevel()->Animations().Target(target, String(context, type, "target"));
    }
    const auto component = String(context, type, "targetComponent");
    const auto property = String(context, type, "targetProperty");
    const std::string selected = component + "." + property;
    if(ImGui::BeginCombo("出力先プロパティ", selected.c_str())) {
        // 現在のreflection metadataから選ぶ。型名・表示名の推測でbindingを生成しない。
        for(const auto& [id, descriptor] : context.types.Types()) {
            if(!target.IsAlive() || !descriptor.contains(target)) {
                continue;
            }
            for(const auto& candidate : descriptor.properties) {
                if(!candidate.animatable || candidate.kind == PropertyKind::Rotation) {
                    continue;
                }
                const auto label = id + "." + candidate.id;
                if(ImGui::Selectable(label.c_str(), label == selected)) {
                    const std::array<PropertyHandle, 3> handles{{{context.session.GetSelection(), type, "targetComponent", {}},
                        {context.session.GetSelection(), type, "targetProperty", {}}, {context.session.GetSelection(), type, "channel", {}}}};
                    const std::array<PropertyValue, 3> values{id, candidate.id, 0};
                    context.properties.Begin(handles);
                    if(!context.properties.PreviewValues(handles, values)) {
                        context.properties.Cancel();
                    }
                    context.properties.Commit();
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("channel: 0=X/R, 1=Y/G, 2=Z/B, 3=W/A。出力 = Curve × scale + bias");
    for(const auto* propertyId : {"channel", "outputScale", "outputBias", "manual", "input", "duration", "elapsed", "playbackSpeed", "loop", "playing"}) {
        DrawReflectedProperty(context, type, propertyId);
    }
    if(ImGui::Button("Viewportでプレビュー")) {
        if(!context.session.IsPlaying() && !context.session.HasAnimationPreview()) {
            const auto started = context.session.BeginAnimationPreview();
            if(!started) {
                ImGui::SetTooltip("%s", started.error().GetMessageText().c_str());
            }
        }
    }
    ImGui::SameLine();
    if(ImGui::Button("プレビューを終了")) {
        context.session.EndAnimationPreview();
    }
}
void Draw(const EditorContext& context, const char* type) {
    ImGui::PushID(type);
    Asset(context, type, "controllerAsset", "State Machine", ".nxcontroller");
    const auto controllerReference = String(context, type, "controllerAsset");
    if(!controllerReference.empty() && context.session.GetLevel()) {
        const auto definition = context.session.GetLevel()->Animations().Controller(controllerReference);
        if(definition) {
            auto values = nlohmann::json::parse(String(context, type, "controllerParameters"), nullptr, false);
            if(!values.is_object()) {
                values = nlohmann::json::object();
            }
            ImGui::TextDisabled("Controller Parameters (数値はEnterで確定)");
            for(const auto& parameter : (*definition)->parameters) {
                ImGui::PushID(parameter.name.c_str());
                float number = parameter.value;
                if(values.contains(parameter.name) && values[parameter.name].is_number()) {
                    number = values[parameter.name].get<float>();
                } else if(values.contains(parameter.name) && values[parameter.name].is_boolean()) {
                    number = values[parameter.name].get<bool>() ? 1.0f : 0.0f;
                }
                bool changed = false;
                if(parameter.type == "Trigger") {
                    changed = ImGui::Button(parameter.name.c_str());
                    number = changed ? 1.0f : number;
                } else if(parameter.type == "Bool") {
                    bool enabled = number != 0;
                    changed = ImGui::Checkbox(parameter.name.c_str(), &enabled);
                    number = enabled ? 1.0f : 0.0f;
                } else {
                    changed = ImGui::InputFloat(parameter.name.c_str(), &number, 0, 0, "%.4g", ImGuiInputTextFlags_EnterReturnsTrue);
                }
                if(changed && std::isfinite(number)) {
                    values[parameter.name] = parameter.type == "Int" ? std::round(number) : number;
                    Set(context, type, "controllerParameters", values.dump());
                }
                ImGui::PopID();
            }
        }
    }
    Asset(context, type, "clipAsset", "クリップ", ".nxanim");
    if(auto* level = context.session.GetLevel()) {
        const auto reference = String(context, type, "clipAsset");
        if(!reference.empty()) {
            const auto clip = level->Animations().Clip(reference);
            if(clip) {
                ImGui::TextDisabled("クリップ長: %.3f 秒", (*clip)->duration);
            } else {
                ImGui::TextWrapped("%s", clip.error().GetMessageText().c_str());
            }
        }
    }
    ImGui::SeparatorText("再生");
    if(ImGui::Button("再生")) {
        Set(context, type, "playing", true);
    }
    ImGui::SameLine();
    if(ImGui::Button("一時停止")) {
        Set(context, type, "playing", false);
    }
    ImGui::SameLine();
    if(ImGui::Button("停止")) {
        const std::array<PropertyHandle, 2> handles{{{context.session.GetSelection(), type, "playing", {}},
            {context.session.GetSelection(), type, "elapsed", {}}}};
        const std::array<PropertyValue, 2> values{false, 0.0f};
        context.properties.Begin(handles);
        if(!context.properties.PreviewValues(handles, values)) {
            context.properties.Cancel();
        }
        context.properties.Commit();
    }
    DrawReflectedProperty(context, type, "elapsed");
    DrawReflectedProperty(context, type, "playbackSpeed");
    DrawReflectedProperty(context, type, "loop");
    DrawReflectedProperty(context, type, "reverseEvents");
    if(ImGui::Button("Viewportでプレビュー")) {
        context.commands.Execute(std::string("animation.previewSelection.") + type);
    }
    ImGui::SeparatorText("Speed Curve");
    Asset(context, type, "speedCurveAsset", "速度カーブ", ".curve");
    DrawReflectedProperty(context, type, "speedCurveDuration");
    if(ImGui::Button("カーブを開く")) {
        context.commands.Execute(std::string("curve.editSelection.") + type);
    }
    ImGui::SameLine();
    if(ImGui::Button("新規カーブ")) {
        context.commands.Execute(std::string("curve.newSelection.") + type);
    }
    std::shared_ptr<const FloatCurve> savedCurve;
    const FloatCurve* previewCurve = nullptr;
    const auto curveReference = String(context, type, "speedCurveAsset");
    if(context.curves && context.content && SameAssetReference(context.content->Root(), context.curves->AssetReference(), curveReference)) {
        previewCurve = &context.curves->Value();
    } else if(!curveReference.empty() && context.session.GetLevel()) {
        const auto loaded = context.session.GetLevel()->Animations().Curves().Load(curveReference);
        if(loaded) {
            savedCurve = *loaded;
            previewCurve = savedCurve.get();
        }
    }
    if(previewCurve) {
        const auto origin = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy({width, 48});
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, {origin.x + width, origin.y + 48}, IM_COL32(24, 28, 35, 255));
        double low = 0, high = 1;
        for(int i = 0; i <= 64; ++i) {
            const double value = previewCurve->Evaluate(i / 64.0);
            low = (std::min)(low, value);
            high = (std::max)(high, value);
        }
        ImVec2 previous{};
        for(int i = 0; i <= 64; ++i) {
            const double x = i / 64.0;
            const ImVec2 point{origin.x + width * static_cast<float>(x), origin.y + 44 -
                40 * static_cast<float>((previewCurve->Evaluate(x) - low) / (high - low))};
            if(i > 0) {
                draw->AddLine(previous, point, IM_COL32(94, 202, 242, 255), 1.5f);
            }
            previous = point;
        }
        ImGui::TextDisabled("共有Curveの編集は同じアセットの利用先へ反映されます。");
    }
    if(std::string_view(type) == "SkeletalAnimation") {
        ImGui::SeparatorText("ブレンド / Crossfade");
        Asset(context, type, "layerAsset", "Animation Layers", ".nxlayers");
        if(ImGui::SmallButton("Rig / Layerを編集")) {
            context.commands.Execute("animation.rig.open");
        }
        ImGui::SeparatorText("Root Motion");
        DrawReflectedProperty(context, type, "rootMotion");
        const auto rootJoint = String(context, type, "rootMotionJoint");
        const auto label = rootJoint.empty() ? std::string("移動を抽出するjointを選択") : DisplaySkeletonPath(rootJoint);
        if(ImGui::BeginCombo("Root Joint", label.c_str())) {
            const auto entity = context.session.GetSelectedEntity();
            const auto* model = entity.GetComponent<PrimitiveRenderComponent>();
            if(model && context.session.GetLevel()) {
                const auto joints = context.session.GetLevel()->Animations().SkeletonJoints(model->meshAsset);
                if(joints) {
                    for(const auto& joint : *joints) {
                        if(ImGui::Selectable(DisplaySkeletonPath(joint).c_str(), joint == rootJoint)) {
                            Set(context, type, "rootMotionJoint", joint);
                        }
                    }
                }
            }
            ImGui::EndCombo();
        }
        DrawReflectedProperty(context, type, "rootMotionAxes");
        DrawReflectedProperty(context, type, "rootMotionRotation");
        DrawReflectedProperty(context, type, "applyRootMotion");
        ImGui::BeginDisabled(!controllerReference.empty());
        Asset(context, type, "blendClipAsset", "遷移先クリップ", ".nxanim");
        DrawReflectedProperty(context, type, "blendSpeed");
        DrawReflectedProperty(context, type, "blendLoop");
        DrawReflectedProperty(context, type, "blendWeight");
        DrawReflectedProperty(context, type, "crossfadeDuration");
        if(ImGui::Button("Crossfadeをプレビュー")) {
            context.commands.Execute("animation.previewCrossfade.SkeletalAnimation");
        }
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}
}
void RegisterAnimationPage(PropertyCategoryRegistry& registry) {
    registry.Register({"CurveBinding", "properties.animation", 600,
        [](const auto& context) {
            const auto* descriptor = context.types.Find("CurveBinding");
            return descriptor && descriptor->contains(context.session.GetSelectedEntity());
        }, [](const auto& context) {
            DrawCurveBinding(context);
        }, {}, {"CurveBinding"}});
    for(const auto* type : {"KeyframeAnimation", "SkeletalAnimation"}) {
        const std::string id = type;
        registry.Register({id, "properties.animation", 600,
            [id](const auto& context) {
                const auto* descriptor = context.types.Find(id);
                return descriptor && descriptor->contains(context.session.GetSelectedEntity());
            }, [id](const auto& context) {
                Draw(context, id.c_str());
            }, {}, {id}});
    }
}
}
