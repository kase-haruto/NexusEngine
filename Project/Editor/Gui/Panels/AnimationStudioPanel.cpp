#include "AnimationStudioPanel.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/PropertyEditService.h"
#include "Runtime/Scene/Components/AnimatorComponent.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <set>

namespace NexusEngine {
namespace {
bool TextField(const char* label, std::string& value) {
    std::array<char, 2048> buffer{};
    std::memcpy(buffer.data(), value.data(), (std::min)(value.size(), buffer.size()-1));
    if(ImGui::InputText(label, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}
bool Choice(const char* label, std::string& value, std::initializer_list<const char*> choices) {
    bool changed = false;
    if(ImGui::BeginCombo(label, value.c_str())) {
        for(const auto* choice : choices) {
            if(ImGui::Selectable(choice, value == choice)) {
                value = choice;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}
std::vector<float> PropertyNumbers(const PropertyValue& value) {
    return std::visit([](const auto& item) -> std::vector<float> {
        using T = std::decay_t<decltype(item)>;
        if constexpr(std::is_same_v<T, float> || std::is_same_v<T, int> || std::is_same_v<T, bool>) {
            return {static_cast<float>(item)};
        } else if constexpr(std::is_same_v<T, std::array<float, 2>> || std::is_same_v<T, std::array<float, 3>> || std::is_same_v<T, std::array<float, 4>>) {
            return {item.begin(), item.end()};
        } else {
            return {};
        }
    }, value);
}
std::string TrackLabel(const AnimationTrack& track) {
    if(track.component != "$node") {
        return (track.target.empty() ? "" : track.target + "/") + track.component + "." + track.property;
    }
    std::string path;
    size_t offset = 0;
    while(offset < track.target.size()) {
        const auto colon = track.target.find(':', offset);
        if(colon == std::string::npos) {
            break;
        }
        size_t length = 0;
        try {
            length = std::stoul(track.target.substr(offset, colon-offset));
        } catch(const std::exception&) {
            break;
        }
        if(colon+1+length > track.target.size()) {
            break;
        }
        if(!path.empty()) {
            path += "/";
        }
        path += track.target.substr(colon+1,length);
        offset = colon+length+2;
    }
    return path + "." + track.property;
}
const PropertyDescriptor* FindProperty(const TypeRegistry& types, const AnimationTrack& track) {
    const auto* type = types.Find(track.component);
    if(!type) {
        return nullptr;
    }
    const auto found = std::ranges::find(type->properties, track.property, &PropertyDescriptor::id);
    return found == type->properties.end() ? nullptr : &*found;
}
void InsertKey(AnimationTrack& track, float time, const std::vector<float>& value) {
    const auto found = std::ranges::find_if(track.keys, [time](const auto& key) { return std::abs(key.time - time) < .0001f; });
    if(found != track.keys.end()) {
        found->value = value;
    } else {
        track.keys.push_back({time, value, {}, {}});
        std::ranges::sort(track.keys, {}, &AnimationKey::time);
    }
}
}
AnimationStudioPanel::AnimationStudioPanel(AnimationStudioDocument& document) : document_(document) {
}
std::string_view AnimationStudioPanel::Id() const noexcept {
    return "AnimationStudio";
}
std::string_view AnimationStudioPanel::TitleKey() const noexcept {
    return "panel.animationStudio";
}
std::array<float, 2> AnimationStudioPanel::InitialSize() const noexcept {
    return {1200, 750};
}
bool AnimationStudioPanel::AssetField(const char* label, std::string& value, const char* extension) {
    ImGui::PushID(label);
    bool changed = TextField(label, value);
    if(ImGui::BeginDragDropTarget()) {
        if(const auto* payload = ImGui::AcceptDragDropPayload("NEXUS_ASSET_PATH")) {
            const auto path = std::filesystem::path(reinterpret_cast<const char8_t*>(payload->Data));
            const auto relative = path.lexically_relative(document_.root);
            if(!relative.empty() && !relative.is_absolute() && *relative.begin() != ".." && path.extension() == extension) {
                const auto text = relative.generic_u8string();
                value.assign(reinterpret_cast<const char*>(text.data()), text.size());
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    if(ImGui::Button("選択")) {
        ImGui::OpenPopup("AssetPicker");
    }
    if(ImGui::BeginPopup("AssetPicker")) {
        for(const auto& asset : document_.Assets(extension)) {
            if(ImGui::Selectable(asset.c_str(), value == asset)) {
                value = asset;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}
void AnimationStudioPanel::Draw(const EditorContext& context) {
    auto& document = document_;
    document.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if(!ImGui::IsAnyItemActive()) {
        document.EndEdit();
    }
    // ファイル切替は未保存draftを自動的に捨てない。
    if(document.pendingOpen || pendingNew_ >= 0 || document.exitRequested) {
        if(document.dirty) {
            ImGui::OpenPopup("未保存のアニメーション");
        } else {
            document.StopPreview(context.session);
            if(pendingNew_ >= 0) {
                document.New(pendingNew_);
                pendingNew_ = -1;
            } else if(document.pendingOpen) {
                document.Report(document.Open(*document.pendingOpen));
                document.pendingOpen.reset();
            }
        }
    }
    if(ImGui::BeginPopupModal("未保存のアニメーション", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("編集中のアセットを保存しますか？");
        if(ImGui::Button("保存して続行")) {
            auto result = document.Save();
            document.Report(result);
            if(result) {
                document.StopPreview(context.session);
                if(document.exitRequested) {
                    document.exitRequested = false;
                    document.exitReady = true;
                } else if(pendingNew_ >= 0) {
                    document.New(pendingNew_);
                    pendingNew_ = -1;
                } else if(document.pendingOpen) {
                    document.Report(document.Open(*document.pendingOpen));
                }
                document.pendingOpen.reset();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("破棄して続行")) {
            document.StopPreview(context.session);
            if(document.exitRequested) {
                document.dirty = false;
                document.exitRequested = false;
                document.exitReady = true;
            } else if(pendingNew_ >= 0) {
                document.New(pendingNew_);
                pendingNew_ = -1;
            } else if(document.pendingOpen) {
                document.Report(document.Open(*document.pendingOpen));
            }
            document.pendingOpen.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("キャンセル")) {
            document.pendingOpen.reset();
            pendingNew_ = -1;
            document.exitRequested = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if(ImGui::Button("新規")) {
        ImGui::OpenPopup("NewAnimationAsset");
    }
    if(ImGui::BeginPopup("NewAnimationAsset")) {
        const char* names[] = {"Animation Clip", "Animator Controller", "Timeline"};
        for(int kind = 0; kind < 3; ++kind) {
            if(ImGui::Selectable(names[kind])) {
                pendingNew_ = kind;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("保存")) {
        document.Report(document.Save());
    }
    ImGui::SameLine();
    if(ImGui::Button("再読込")) {
        auto path = ResolveAnimationPath(document.root, document.reference);
        if(path && std::filesystem::is_regular_file(*path)) {
            document.pendingOpen = *path;
        }
    }
    ImGui::SameLine();
    if(ImGui::Button("Undo")) {
        document.Undo();
    }
    ImGui::SameLine();
    if(ImGui::Button("Redo")) {
        document.Redo();
    }
    ImGui::SameLine();
    if(ImGui::Button("選択Entityへ設定")) {
        document.Report(document.Attach(context.session));
    }
    ImGui::SameLine();
    if(ImGui::Button("モデルから取り込み")) {
        ImGui::OpenPopup("ImportModelAnimations");
    }
    if(ImGui::BeginPopup("ImportModelAnimations")) {
        for(const auto* extension : {".gltf", ".glb"}) {
            for(const auto& asset : document.Assets(extension)) {
                if(ImGui::Selectable(asset.c_str())) {
                    auto path = ResolveAnimationPath(document.root, asset);
                    if(path) {
                        document.Report(document.ImportModel(*path));
                    }
                }
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SetNextItemWidth(-1);
    if(TextField("##AssetPath", document.reference)) {
        document.dirty = true;
        ++document.revision;
    }
    ImGui::TextUnformatted(document.dirty ? "未保存の変更あり" : "保存済み / Assetsからの相対パス");
    if(ImGui::Checkbox("プレビュー", &document.preview)) {
        if(!document.preview) {
            document.StopPreview(context.session);
        }
    }
    ImGui::SameLine();
    if(ImGui::Button(document.playing ? "一時停止" : "再生")) {
        document.preview = true;
        document.playing = !document.playing;
    }
    ImGui::SameLine();
    if(ImGui::Button("停止・シーンへ戻る")) {
        document.time = 0;
        document.StopPreview(context.session);
    }
    if(document.value.index() != 1) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::DragFloat("時刻（秒）", &document.time, .01f, 0, 100000, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    }
    const auto before = document.value;
    bool changed = false;
    if(auto* clip = std::get_if<AnimationClipDocument>(&document.value)) {
        DrawClip(context, *clip, changed);
    } else if(auto* controller = std::get_if<AnimatorDocument>(&document.value)) {
        DrawController(context, *controller, changed);
    } else {
        DrawTimeline(context, std::get<TimelineDocument>(document.value), changed);
    }
    if(changed) {
        document.Changed(before, ImGui::IsAnyItemActive());
    }
    if(document.focused && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput) {
        if(ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            document.Report(document.Save());
        }
        if(ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            document.Undo();
        }
        if(ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            document.Redo();
        }
    }
    if(!document.status.empty()) {
        ImGui::TextWrapped("%s", document.status.c_str());
    }
}
void AnimationStudioPanel::DrawClip(const EditorContext& context, AnimationClipDocument& clip, bool& changed) {
    ImGui::SeparatorText("Animation Clip");
    float minimumDuration = .001f;
    for(const auto& track : clip.tracks) {
        if(!track.keys.empty()) {
            minimumDuration = (std::max)(minimumDuration, track.keys.back().time);
        }
    }
    for(const auto& event : clip.events) {
        minimumDuration = (std::max)(minimumDuration, event.time);
    }
    changed |= ImGui::DragFloat("長さ（秒）", &clip.duration, .01f, minimumDuration, 100000, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::Checkbox("ループ", &clip.loop);
    if(!clip.source.empty()) {
        ImGui::TextWrapped("Source: %s / %s", clip.source.c_str(), clip.clip.c_str());
        if(ImGui::Button("編集可能なキーへ展開")) {
            document_.Report(document_.MakeEditable());
        }
    }
    if(ImGui::Button("Property Trackを追加")) {
        ImGui::OpenPopup("AddAnimatedProperty");
    }
    if(ImGui::BeginPopup("AddAnimatedProperty")) {
        auto entity = context.session.GetSelectedEntity();
        for(const auto& [id, type] : context.types.Types()) {
            if(!entity.IsAlive() || !type.contains(entity)) {
                continue;
            }
            for(const auto& property : type.properties) {
                const auto value = property.get(entity);
                const auto numbers = value ? PropertyNumbers(*value) : std::vector<float>{};
                if(numbers.empty()) {
                    continue;
                }
                const auto label = id + "." + property.id;
                if(ImGui::Selectable(label.c_str())) {
                    AnimationTrack track;
                    track.component = id;
                    track.property = property.id;
                    if(property.kind == PropertyKind::Bool || property.kind == PropertyKind::Int) {
                        track.interpolation = AnimationInterpolation::Step;
                    }
                    track.keys.push_back({0, numbers, {}, {}});
                    clip.tracks.push_back(std::move(track));
                    document_.selected = static_cast<int>(clip.tracks.size()) - 1;
                    changed = true;
                }
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("イベント追加")) {
        clip.events.push_back({(std::min)(document_.time, clip.duration), "Event", ""});
        changed = true;
    }
    DrawKeys(clip, changed);
    if(!clip.tracks.empty()) {
        document_.selected = std::clamp(document_.selected, 0, static_cast<int>(clip.tracks.size())-1);
        auto& track = clip.tracks[document_.selected];
        ImGui::SeparatorText("Track / Key");
        if(track.component == "$node") {
            ImGui::TextWrapped("ボーン: %s", TrackLabel(track).c_str());
        } else {
            changed |= TextField("対象（空=自身、子は名前/子の名前）", track.target);
            ImGui::Text("%s.%s", track.component.c_str(), track.property.c_str());
        }
        const char* modes[] = {"Step", "Linear", "CubicSpline"};
        int mode = static_cast<int>(track.interpolation);
        if(ImGui::Combo("補間", &mode, modes, 3)) {
            track.interpolation = static_cast<AnimationInterpolation>(mode);
            changed = true;
        }
        if(ImGui::Button("現在時刻へキー追加")) {
            InsertKey(track, std::clamp(document_.time, 0.0f, clip.duration), SampleAnimationTrack(track, document_.time));
            changed = true;
        }
        ImGui::SameLine();
        if(ImGui::Button("選択Entityの現在値を記録")) {
            const auto* property = FindProperty(context.types, track);
            auto entity = context.session.GetSelectedEntity();
            if(entity.IsAlive() && !track.target.empty()) {
                entity = context.session.GetLevel()->Animations().Target(entity, track.target);
            }
            if(property && entity.IsAlive()) {
                auto current = property->get(entity);
                if(current) {
                    InsertKey(track, std::clamp(document_.time, 0.0f, clip.duration), PropertyNumbers(*current));
                    changed = true;
                }
            }
        }
        if(!track.keys.empty()) {
            document_.key = std::clamp(document_.key, 0, static_cast<int>(track.keys.size())-1);
            auto& key = track.keys[document_.key];
            ImGui::Text("Key %d / %zu", document_.key + 1, track.keys.size());
            const float minimum = document_.key == 0 ? 0 : track.keys[document_.key-1].time + .0001f;
            const float maximum = document_.key+1 == static_cast<int>(track.keys.size()) ? clip.duration : track.keys[document_.key+1].time - .0001f;
            changed |= ImGui::DragFloat("キー時刻", &key.time, .01f, minimum, maximum, "%.4f", ImGuiSliderFlags_AlwaysClamp);
            for(size_t dimension = 0; dimension < key.value.size(); ++dimension) {
                ImGui::PushID(static_cast<int>(dimension));
                const char* names[] = {"X / R", "Y / G", "Z / B", "W / A"};
                changed |= ImGui::DragFloat(names[dimension], &key.value[dimension], .01f);
                if(track.interpolation == AnimationInterpolation::CubicSpline) {
                    key.inTangent.resize(key.value.size());
                    key.outTangent.resize(key.value.size());
                    ImGui::SameLine();
                    changed |= ImGui::DragFloat("In", &key.inTangent[dimension], .01f);
                    ImGui::SameLine();
                    changed |= ImGui::DragFloat("Out", &key.outTangent[dimension], .01f);
                }
                ImGui::PopID();
            }
            if(track.keys.size() > 1 && ImGui::Button("キー削除")) {
                track.keys.erase(track.keys.begin() + document_.key);
                changed = true;
            }
        }
        DrawCurve(track, clip.duration);
        if(ImGui::Button("Track削除")) {
            clip.tracks.erase(clip.tracks.begin() + document_.selected);
            changed = true;
        }
    }
    for(size_t index = 0; index < clip.events.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        auto& event = clip.events[index];
        changed |= ImGui::DragFloat("Event time", &event.time, .01f, 0, clip.duration, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        changed |= TextField("イベント名", event.name);
        changed |= TextField("Payload", event.payload);
        if(ImGui::SmallButton("イベント削除")) {
            clip.events.erase(clip.events.begin() + index);
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
}
void AnimationStudioPanel::DrawKeys(AnimationClipDocument& clip, bool& changed) {
    const float labelWidth = 240, row = 27;
    const float width = (std::max)(300.0f, ImGui::GetContentRegionAvail().x - labelWidth - 20);
    ImGui::BeginChild("KeyTimeline", {0, (std::min)(260.0f, row * (static_cast<float>(clip.tracks.size()) + 2))}, ImGuiChildFlags_Borders);
    auto origin = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const float duration = (std::max)(clip.duration, .001f);
    ImGui::SetCursorScreenPos({origin.x + labelWidth, origin.y});
    ImGui::InvisibleButton("ScrubRuler", {width, row});
    if(ImGui::IsItemActive()) {
        document_.time = std::clamp((ImGui::GetIO().MousePos.x - origin.x - labelWidth) / width * duration, 0.0f, duration);
        document_.preview = true;
    }
    for(int tick = 0; tick <= 10; ++tick) {
        const float x = origin.x + labelWidth + width * static_cast<float>(tick) / 10;
        draw->AddLine({x, origin.y}, {x, origin.y + row * (static_cast<float>(clip.tracks.size())+1)}, IM_COL32(70,75,85,255));
        char text[32];
        std::snprintf(text, sizeof(text), "%.2f", duration * static_cast<float>(tick) / 10);
        draw->AddText({x+2, origin.y}, IM_COL32(160,170,185,255), text);
    }
    for(size_t index = 0; index < clip.tracks.size(); ++index) {
        auto& track = clip.tracks[index];
        const float y = origin.y + row * (static_cast<float>(index)+1);
        ImGui::PushID(static_cast<int>(index));
        ImGui::SetCursorScreenPos({origin.x, y});
        const auto label = TrackLabel(track);
        if(ImGui::Selectable(label.c_str(), document_.selected == static_cast<int>(index), 0, {labelWidth-8, row})) {
            document_.selected = static_cast<int>(index);
        }
        for(size_t keyIndex = 0; keyIndex < track.keys.size(); ++keyIndex) {
            auto& key = track.keys[keyIndex];
            const float x = origin.x + labelWidth + key.time / duration * width;
            const float cy = y + row / 2;
            const bool selected = document_.selected == static_cast<int>(index) && document_.key == static_cast<int>(keyIndex);
            const ImU32 color = selected ? IM_COL32(255,199,80,255) : IM_COL32(120,190,240,255);
            draw->AddQuadFilled({x,cy-5}, {x+5,cy}, {x,cy+5}, {x-5,cy}, color);
            ImGui::SetCursorScreenPos({x-6, y+2});
            ImGui::PushID(static_cast<int>(keyIndex));
            ImGui::InvisibleButton("Key", {12, row-4});
            if(ImGui::IsItemActivated()) {
                document_.selected = static_cast<int>(index);
                document_.key = static_cast<int>(keyIndex);
                document_.time = key.time;
            }
            if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                const float minimum = keyIndex == 0 ? 0 : track.keys[keyIndex-1].time + .0001f;
                const float maximum = keyIndex+1 == track.keys.size() ? duration : track.keys[keyIndex+1].time - .0001f;
                key.time = std::clamp(key.time + ImGui::GetIO().MouseDelta.x / width * duration, minimum, maximum);
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    const float cursor = origin.x + labelWidth + document_.time / duration * width;
    draw->AddLine({cursor, origin.y}, {cursor, origin.y + row * (static_cast<float>(clip.tracks.size())+1)}, IM_COL32(255,110,80,255), 2);
    ImGui::SetCursorScreenPos({origin.x, origin.y + row * (static_cast<float>(clip.tracks.size())+1)});
    ImGui::Dummy({width+labelWidth, 1});
    ImGui::EndChild();
}
void AnimationStudioPanel::DrawCurve(const AnimationTrack& track, float duration) {
    if(track.keys.empty()) {
        return;
    }
    const ImVec2 size{(std::max)(100.0f, ImGui::GetContentRegionAvail().x), 150};
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("Curve", size);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x+size.x, origin.y+size.y}, IM_COL32(20,24,31,255));
    float low = 0, high = 1;
    for(const auto& key : track.keys) {
        for(float value : key.value) {
            low = (std::min)(low, value);
            high = (std::max)(high, value);
        }
    }
    const ImU32 colors[] = {IM_COL32(245,105,100,255), IM_COL32(100,220,140,255), IM_COL32(110,170,255,255), IM_COL32(220,210,140,255)};
    for(size_t dimension = 0; dimension < track.keys.front().value.size(); ++dimension) {
        ImVec2 previous;
        for(int sample = 0; sample <= 160; ++sample) {
            const float amount = static_cast<float>(sample) / 160;
            const auto value = SampleAnimationTrack(track, amount * duration);
            const ImVec2 point{origin.x + amount * size.x, origin.y + size.y - 8 - (value[dimension]-low)/(high-low)*(size.y-16)};
            if(sample > 0) {
                draw->AddLine(previous, point, colors[dimension], 1.5f);
            }
            previous = point;
        }
    }
}
void AnimationStudioPanel::DrawController(const EditorContext& context, AnimatorDocument& controller, bool& changed) {
    ImGui::SeparatorText("Animator Controller");
    if(context.session.HasAnimationPreview()) {
        auto actor = context.session.GetPreviewLevel()->FindEntity(context.session.GetSelection());
        if(const auto* animator = actor.GetComponent<AnimatorComponent>()) {
            ImGui::Text("再生State: %s   %.2f s   %s", animator->state.c_str(), animator->stateTime, animator->nextState.c_str());
        }
    }
    if(ImGui::Button("State追加")) {
        AnimatorState state;
        state.name = "State" + std::to_string(controller.states.size());
        state.position = {80 + static_cast<float>(controller.states.size()%4)*200, 70 + static_cast<float>(controller.states.size()/4)*100};
        controller.states.push_back(state);
        document_.selected = static_cast<int>(controller.states.size())-1;
        changed = true;
    }
    ImGui::SameLine();
    if(ImGui::Button("Parameter追加")) {
        controller.parameters.push_back({"Parameter" + std::to_string(controller.parameters.size()), "Float", 0});
        changed = true;
    }
    ImGui::SameLine();
    if(ImGui::Button("Transition追加") && !controller.states.empty()) {
        AnimatorTransition transition;
        transition.from = controller.states.front().name;
        transition.to = controller.states.back().name;
        controller.transitions.push_back(transition);
        document_.selectedTransition = static_cast<int>(controller.transitions.size())-1;
        changed = true;
    }
    DrawControllerGraph(context, controller, changed);
    for(size_t index = 0; index < controller.parameters.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        auto& parameter = controller.parameters[index];
        changed |= TextField("Parameter", parameter.name);
        changed |= Choice("型", parameter.type, {"Float", "Int", "Bool", "Trigger"});
        changed |= ImGui::DragFloat("値 / Preview", &parameter.value, .01f);
        if(ImGui::SmallButton("Parameter削除")) {
            controller.parameters.erase(controller.parameters.begin()+index);
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if(!controller.states.empty()) {
        document_.selected = std::clamp(document_.selected, 0, static_cast<int>(controller.states.size())-1);
        auto& state = controller.states[document_.selected];
        ImGui::SeparatorText("Selected State");
        const auto oldName = state.name;
        if(TextField("State名", state.name)) {
            if(controller.entry == oldName) {
                controller.entry = state.name;
            }
            for(auto& transition : controller.transitions) {
                if(transition.from == oldName) {
                    transition.from = state.name;
                }
                if(transition.to == oldName) {
                    transition.to = state.name;
                }
            }
            changed = true;
        }
        if(ImGui::Button("Entryに設定")) {
            controller.entry = state.name;
            changed = true;
        }
        changed |= Choice("Motion", state.blend, {"Clip", "1D", "2D"});
        changed |= ImGui::DragFloat("再生速度", &state.speed, .01f, 0, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::Checkbox("State loop", &state.loop);
        if(state.blend == "Clip") {
            changed |= AssetField("Clip", state.asset, ".nxanim");
        } else {
            changed |= TextField("Parameter X", state.parameterX);
            if(state.blend == "2D") {
                changed |= TextField("Parameter Y", state.parameterY);
            }
            if(ImGui::Button("Motion追加")) {
                state.motions.push_back({});
                changed = true;
            }
            for(size_t index = 0; index < state.motions.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                auto& motion = state.motions[index];
                changed |= AssetField("Motion Clip", motion.asset, ".nxanim");
                changed |= ImGui::DragFloat("Threshold / X", &motion.x, .01f);
                if(state.blend == "2D") {
                    changed |= ImGui::DragFloat("Y", &motion.y, .01f);
                }
                changed |= ImGui::DragFloat("Speed", &motion.speed, .01f, 0, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::SmallButton("Motion削除")) {
                    state.motions.erase(state.motions.begin()+index);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
        }
        if(controller.states.size() > 1 && ImGui::Button("State削除")) {
            const auto removed = state.name;
            controller.states.erase(controller.states.begin()+document_.selected);
            std::erase_if(controller.transitions, [&](const auto& transition) { return transition.from == removed || transition.to == removed; });
            if(controller.entry == removed) {
                controller.entry = controller.states.front().name;
            }
            changed = true;
        }
    }
    ImGui::SeparatorText("Transitions");
    for(size_t index = 0; index < controller.transitions.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        auto& transition = controller.transitions[index];
        const auto name = transition.from + " -> " + transition.to;
        if(ImGui::TreeNode(name.c_str())) {
            document_.selectedTransition = static_cast<int>(index);
            changed |= TextField("From（Any State可）", transition.from);
            changed |= TextField("To", transition.to);
            changed |= ImGui::DragFloat("遷移時間（秒）", &transition.duration, .01f, 0, 100, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::Checkbox("Has Exit Time", &transition.hasExitTime);
            if(transition.hasExitTime) {
                changed |= ImGui::DragFloat("Exit Time（クリップ長の割合）", &transition.exitTime, .01f, 0, 100, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            }
            changed |= ImGui::DragFloat("切替先の開始（秒）", &transition.offset, .01f, 0, 100000, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::Button("条件追加")) {
                transition.conditions.push_back({});
                changed = true;
            }
            for(size_t conditionIndex = 0; conditionIndex < transition.conditions.size(); ++conditionIndex) {
                ImGui::PushID(static_cast<int>(conditionIndex));
                auto& condition = transition.conditions[conditionIndex];
                changed |= TextField("条件Parameter", condition.parameter);
                changed |= Choice("比較", condition.operation, {">", "<", "==", "!=", "True", "False"});
                changed |= ImGui::DragFloat("条件値", &condition.value, .01f);
                if(ImGui::SmallButton("条件削除")) {
                    transition.conditions.erase(transition.conditions.begin()+conditionIndex);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if(ImGui::Button("Transition削除")) {
                controller.transitions.erase(controller.transitions.begin()+index);
                changed = true;
                ImGui::TreePop();
                ImGui::PopID();
                break;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}
void AnimationStudioPanel::DrawControllerGraph(const EditorContext& context, AnimatorDocument& controller, bool& changed) {
    // ID空間はCanvasごとに独立する。1/2はEntry/Any State、10以降は実StateとTransition。
    // 保存対象はDocumentの論理座標だけで、倍率とpanは表示状態として分離する。
    UI::NodeEditorSession session(nodeEditor_);
    if(graphReference_ != document_.reference) {
        graphReference_ = document_.reference;
        nodeEditor_.ClearSelection();
        nodeEditor_.SetPanning({180,0});
        nodeZoom_ = 1;
    }
    std::vector<std::string> names;
    for(const auto& state : controller.states) {
        names.push_back(state.name);
    }
    if(names != graphStateNames_) {
        nodeEditor_.ClearSelection();
        graphStateNames_ = std::move(names);
    }
    bool frameAll = ImGui::Button("全ノードを表示");
    ImGui::SameLine();
    const bool resetZoom = ImGui::Button("100%");
    ImGui::SameLine();
    ImGui::Text("%.0f%%", nodeZoom_*100);
    ImGui::TextUnformatted("ピンからピンへドラッグで遷移を作成。中ボタンで移動、ホイールで拡縮、Deleteで削除。");
    std::string currentState;
    if(context.session.HasAnimationPreview()) {
        auto actor = context.session.GetPreviewLevel()->FindEntity(context.session.GetSelection());
        if(const auto* animator = actor.GetComponent<AnimatorComponent>()) {
            currentState = animator->state;
        }
    }
    ImGui::BeginChild("AnimatorCanvas", {0, 300}, ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const float drawZoom = nodeZoom_;
    std::array<float,2> origin{}, size{};
    bool hovered = false;
    std::vector<UI::NodeEditor::Layout> layouts{{1,{-160,20}},{2,{-160,140}}};
    {
        UI::NodeEditorCanvas canvas(nodeEditor_, drawZoom);
        const auto cursor = ImGui::GetCursorScreenPos();
        const auto available = ImGui::GetWindowSize();
        origin = {cursor.x,cursor.y};
        size = {available.x,available.y};
        for(int special : {1,2}) {
            const auto position = special == 1 ? std::array<float,2>{-160,20} : std::array<float,2>{-160,140};
            nodeEditor_.SetNodePosition(special,{position[0]*drawZoom,position[1]*drawZoom});
            nodeEditor_.PushTitleColor(special == 1 ? IM_COL32(48,105,65,255) : IM_COL32(37,99,110,255));
            nodeEditor_.BeginNode(special);
            nodeEditor_.SetDraggable(special,false);
            nodeEditor_.BeginTitle();
            ImGui::TextUnformatted(special == 1 ? "Entry" : "Any State");
            nodeEditor_.EndTitle();
            nodeEditor_.BeginOutput(special*3+1);
            ImGui::TextUnformatted(special == 1 ? "Start" : "Transition");
            nodeEditor_.EndOutput();
            nodeEditor_.EndNode();
            nodeEditor_.PopTitleColor();
        }
        for(size_t index = 0; index < controller.states.size(); ++index) {
            const auto& state = controller.states[index];
            const int id = static_cast<int>(index)+10;
            layouts.push_back({id,state.position});
            nodeEditor_.SetNodePosition(id,{state.position[0]*drawZoom,state.position[1]*drawZoom});
            const uint32_t color = state.name == currentState ? IM_COL32(40,115,70,255) :
                state.name == controller.entry ? IM_COL32(105,70,35,255) : IM_COL32(35,72,100,255);
            nodeEditor_.PushTitleColor(color);
            nodeEditor_.BeginNode(id);
            ImGui::PushID(id);
            nodeEditor_.BeginTitle();
            ImGui::TextUnformatted(state.name.c_str());
            nodeEditor_.EndTitle();
            nodeEditor_.BeginInput(id*3);
            ImGui::TextUnformatted("State");
            nodeEditor_.EndInput();
            nodeEditor_.BeginStatic(id*3+2);
            ImGui::Text("Motion: %s",state.blend.c_str());
            ImGui::Dummy({150*drawZoom,0});
            nodeEditor_.EndStatic();
            nodeEditor_.BeginOutput(id*3+1);
            ImGui::TextUnformatted("Transition");
            nodeEditor_.EndOutput();
            ImGui::PopID();
            nodeEditor_.EndNode();
            nodeEditor_.PopTitleColor();
        }
        const auto stateId = [&](const std::string& name) {
            const auto found = std::ranges::find(controller.states,name,&AnimatorState::name);
            return found == controller.states.end() ? -1 : static_cast<int>(found-controller.states.begin())+10;
        };
        const int entry = stateId(controller.entry);
        if(entry >= 10) {
            nodeEditor_.Link(1,4,entry*3);
        }
        for(size_t index = 0; index < controller.transitions.size(); ++index) {
            const auto& transition = controller.transitions[index];
            const int from = transition.from == "Any State" ? 2 : stateId(transition.from);
            const int to = stateId(transition.to);
            if(from >= 0 && to >= 10) {
                nodeEditor_.Link(static_cast<int>(index)+10,from*3+1,to*3);
            }
        }
        nodeEditor_.MiniMap();
        hovered = ImGui::IsWindowHovered() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
        canvas.Close();
    }
    int start = 0, end = 0, destroyed = 0;
    if(nodeEditor_.LinkCreated(start,end)) {
        if(start%3 == 0) {
            std::swap(start,end);
        }
        const int from = start/3, to = end/3;
        if(start%3 == 1 && end%3 == 0 && to >= 10 && to-10 < static_cast<int>(controller.states.size()) && from != to) {
            if(from == 1) {
                controller.entry = controller.states[to-10].name;
                changed = true;
            } else if(from == 2 || (from >= 10 && from-10 < static_cast<int>(controller.states.size()))) {
                AnimatorTransition transition;
                transition.from = from == 2 ? "Any State" : controller.states[from-10].name;
                transition.to = controller.states[to-10].name;
                controller.transitions.push_back(std::move(transition));
                document_.selectedTransition = static_cast<int>(controller.transitions.size())-1;
                changed = true;
            }
        }
    }
    if(nodeEditor_.LinkDestroyed(destroyed) && destroyed >= 10 && destroyed-10 < static_cast<int>(controller.transitions.size())) {
        controller.transitions.erase(controller.transitions.begin()+destroyed-10);
        nodeEditor_.ClearSelection();
        changed = true;
    }
    const auto nodes = nodeEditor_.SelectedNodes(), links = nodeEditor_.SelectedLinks();
    if(nodes.size() == 1 && nodes[0] >= 10 && nodes[0]-10 < static_cast<int>(controller.states.size())) {
        document_.selected = nodes[0]-10;
        document_.selectedTransition = -1;
    }
    if(links.size() == 1 && links[0] >= 10 && links[0]-10 < static_cast<int>(controller.transitions.size())) {
        document_.selectedTransition = links[0]-10;
    }
    // ドラッグ結果を論理座標へ戻す。pan/zoom操作そのものではアセットをdirtyにしない。
    for(size_t index = 0; index < controller.states.size(); ++index) {
        const auto position = nodeEditor_.NodePosition(static_cast<int>(index)+10);
        const std::array<float,2> logical{position[0]/drawZoom,position[1]/drawZoom};
        auto& state = controller.states[index];
        if(std::abs(logical[0]-state.position[0]) > .01f || std::abs(logical[1]-state.position[1]) > .01f) {
            state.position = logical;
            changed = true;
        }
    }
    if(hovered && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete,false)) {
        std::set<std::string> removed;
        for(int id : nodes) {
            if(id >= 10 && id-10 < static_cast<int>(controller.states.size())) {
                removed.insert(controller.states[id-10].name);
            }
        }
        // 最後のStateは残す。空Controllerは保存時に拒否されるため、編集操作でも壊さない。
        if(removed.size() < controller.states.size()) {
            std::erase_if(controller.states,[&](const auto& state) { return removed.contains(state.name); });
            std::erase_if(controller.transitions,[&](const auto& transition) { return removed.contains(transition.from) || removed.contains(transition.to); });
            if(removed.contains(controller.entry) && !controller.states.empty()) {
                controller.entry = controller.states.front().name;
            }
            changed |= !removed.empty();
        }
        if(removed.empty()) {
            auto ordered = links;
            std::ranges::sort(ordered,std::greater<>());
            for(int id : ordered) {
                if(id >= 10 && id-10 < static_cast<int>(controller.transitions.size())) {
                    controller.transitions.erase(controller.transitions.begin()+id-10);
                    changed = true;
                }
            }
        }
        nodeEditor_.ClearSelection();
    }
    if(frameAll) {
        nodeZoom_ = nodeEditor_.FrameAll(layouts,size,drawZoom);
    } else if(resetZoom) {
        nodeZoom_ = nodeEditor_.ZoomAt(drawZoom,1,{size[0]*.5f,size[1]*.5f});
    } else if(hovered && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1) &&
        !ImGui::IsMouseDown(2) && ImGui::GetIO().MouseWheel != 0) {
        const auto mouse = ImGui::GetMousePos();
        nodeZoom_ = nodeEditor_.ZoomAt(drawZoom,drawZoom*std::pow(1.15f,std::clamp(ImGui::GetIO().MouseWheel,-20.0f,20.0f)),
            {mouse.x-origin[0],mouse.y-origin[1]});
    }
    ImGui::EndChild();
}
void AnimationStudioPanel::DrawTimeline(const EditorContext&, TimelineDocument& timeline, bool& changed) {
    ImGui::SeparatorText("Timeline");
    changed |= ImGui::DragFloat("Timeline長さ（秒）", &timeline.duration, .1f, .1f, 100000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::Checkbox("Timeline loop", &timeline.loop);
    if(ImGui::Button("Animation Track追加")) {
        timeline.tracks.push_back({});
        document_.selected = static_cast<int>(timeline.tracks.size())-1;
        changed = true;
    }
    ImGui::SameLine();
    if(ImGui::Button("Activation Track追加")) {
        TimelineTrack track;
        track.type = "Activation";
        track.name = "Activation";
        timeline.tracks.push_back(track);
        changed = true;
    }
    ImGui::SliderFloat("表示倍率", &zoom_, 1, 5, "%.1f");
    ImGui::TextUnformatted("上の目盛りでスクラブ。区間をドラッグで移動、両端をドラッグで長さを変更。");
    ImGui::BeginChild("TimelineCanvas", {0, 240}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    const auto origin = ImGui::GetCursorScreenPos();
    const float labelWidth = 170;
    const float width = (std::max)(200.0f, ImGui::GetContentRegionAvail().x-labelWidth-20) * zoom_;
    const float scale = width / (std::max)(timeline.duration, .1f);
    auto* draw = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos({origin.x+labelWidth, origin.y});
    ImGui::InvisibleButton("TimelineRuler", {width, 28});
    if(ImGui::IsItemActive()) {
        document_.time = std::clamp((ImGui::GetIO().MousePos.x-origin.x-labelWidth)/scale, 0.0f, timeline.duration);
        document_.preview = true;
    }
    for(int tick = 0; tick <= 20; ++tick) {
        const float amount = static_cast<float>(tick)/20;
        const float x = origin.x+labelWidth+amount*width;
        draw->AddLine({x,origin.y}, {x,origin.y+28+static_cast<float>(timeline.tracks.size())*42}, IM_COL32(70,75,85,255));
        char label[32];
        std::snprintf(label,sizeof(label),"%.1f",amount*timeline.duration);
        draw->AddText({x+2,origin.y},IM_COL32(170,180,195,255),label);
    }
    for(size_t index = 0; index < timeline.tracks.size(); ++index) {
        auto& track = timeline.tracks[index];
        const float y = origin.y+28+static_cast<float>(index)*42;
        ImGui::PushID(static_cast<int>(index));
        ImGui::SetCursorScreenPos({origin.x,y});
        if(ImGui::Selectable(track.name.c_str(), document_.selected == static_cast<int>(index), 0, {labelWidth-8,36})) {
            document_.selected = static_cast<int>(index);
        }
        for(size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            auto& clip = track.clips[clipIndex];
            const ImVec2 start{origin.x+labelWidth+clip.start*scale,y+2};
            const float clipWidth = (std::max)(14.0f,clip.duration*scale);
            draw->AddRectFilled(start, {start.x+clipWidth,y+36}, document_.selected == static_cast<int>(index) && document_.key == static_cast<int>(clipIndex) ? IM_COL32(65,133,175,255) : IM_COL32(50,87,112,255), 4);
            draw->AddText({start.x+6,start.y+6},IM_COL32(235,240,245,255),clip.asset.empty() ? "Active" : clip.asset.c_str());
            ImGui::SetCursorScreenPos(start);
            ImGui::PushID(static_cast<int>(clipIndex));
            ImGui::InvisibleButton("TimelineClip",{clipWidth,34});
            if(ImGui::IsItemActivated()) {
                document_.selected = static_cast<int>(index);
                document_.key = static_cast<int>(clipIndex);
            }
            if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                const float delta = ImGui::GetIO().MouseDelta.x/scale;
                const float click = ImGui::GetIO().MouseClickedPos[0].x-start.x;
                if(click < 8) {
                    const float end = clip.start+clip.duration;
                    clip.start = std::clamp(clip.start+delta,0.0f,end-.01f);
                    clip.duration = end-clip.start;
                } else if(click > clipWidth-8) {
                    clip.duration = std::clamp(clip.duration+delta,.01f,(std::max)(.01f,timeline.duration-clip.start));
                } else {
                    clip.start = std::clamp(clip.start+delta,0.0f,(std::max)(0.0f,timeline.duration-clip.duration));
                }
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    const float cursor = origin.x+labelWidth+document_.time*scale;
    draw->AddLine({cursor,origin.y},{cursor,origin.y+28+static_cast<float>(timeline.tracks.size())*42},IM_COL32(255,120,80,255),2);
    ImGui::SetCursorScreenPos({origin.x,origin.y+30+static_cast<float>(timeline.tracks.size())*42});
    ImGui::Dummy({width+labelWidth,1});
    ImGui::EndChild();
    if(timeline.tracks.empty()) {
        return;
    }
    document_.selected = std::clamp(document_.selected,0,static_cast<int>(timeline.tracks.size())-1);
    auto& track = timeline.tracks[document_.selected];
    changed |= TextField("Track名",track.name);
    changed |= TextField("対象（空=自身、子の名前/子の名前）",track.target);
    changed |= ImGui::Checkbox("ミュート",&track.muted);
    if(ImGui::Button("区間追加")) {
        TimelineClip clip;
        clip.start = std::clamp(document_.time,0.0f,(std::max)(0.0f,timeline.duration-1));
        clip.duration = (std::min)(1.0f,timeline.duration);
        track.clips.push_back(clip);
        document_.key = static_cast<int>(track.clips.size())-1;
        changed = true;
    }
    if(!track.clips.empty()) {
        document_.key = std::clamp(document_.key,0,static_cast<int>(track.clips.size())-1);
        auto& clip = track.clips[document_.key];
        if(track.type == "Animation") {
            changed |= AssetField("アニメーション",clip.asset,".nxanim");
        }
        changed |= ImGui::DragFloat("開始（秒）",&clip.start,.01f,0,timeline.duration,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat("区間長（秒）",&clip.duration,.01f,.01f,timeline.duration,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat("Clip In（秒）",&clip.clipIn,.01f,0,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat("区間の速度",&clip.speed,.01f,0,100,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat("Ease In（秒）",&clip.easeIn,.01f,0,clip.duration,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::DragFloat("Ease Out（秒）",&clip.easeOut,.01f,0,clip.duration,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        changed |= ImGui::Checkbox("区間内ループ",&clip.loop);
        if(ImGui::Button("区間削除")) {
            track.clips.erase(track.clips.begin()+document_.key);
            changed = true;
        }
    }
    if(ImGui::Button("Trackを削除")) {
        timeline.tracks.erase(timeline.tracks.begin()+document_.selected);
        changed = true;
    }
}
}
