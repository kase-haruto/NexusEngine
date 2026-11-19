#include "AnimationRigPanel.h"
#include "Editor/Services/AssetCatalog.h"
#include "Graphics/Model/ModelLoader.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace NexusEngine {
namespace {
bool RigText(const char* label, std::string& value) {
    std::array<char, 2048> buffer{};
    std::memcpy(buffer.data(), value.data(), (std::min)(value.size(), buffer.size() - 1));
    if(ImGui::InputText(label, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}
void RigAsset(const char* label, std::string& value, const std::filesystem::path& root, const char* extension) {
    ImGui::PushID(label);
    std::string display = value;
    if(IsAssetIdentity(value)) {
        const auto path = ResolveAssetReference(root, value);
        if(path) {
            display = AssetCatalog::Utf8(path->lexically_relative(root));
        }
    }
    if(ImGui::BeginCombo(label, value.empty() ? "未設定" : display.c_str())) {
        if(ImGui::Selectable("解除")) {
            value.clear();
        }
        std::error_code error;
        for(std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
            it != end && !error; it.increment(error)) {
            const auto suffix = it->path().extension();
            const bool model = std::string_view(extension) == "model" && (suffix == ".gltf" || suffix == ".glb");
            const bool rig = std::string_view(extension) == "rig" && (suffix == ".nxretarget" || suffix == ".nxlayers");
            if(it->is_regular_file() && (model || rig || suffix == extension)) {
                const auto path = AssetCatalog::Utf8(it->path().lexically_relative(root));
                if(ImGui::Selectable(path.c_str(), path == value)) {
                    const auto identity = EnsureAssetIdentity(root, it->path());
                    value = identity ? *identity : path;
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
}
void RigJoint(const char* label, std::string& value, const ModelAssetData& model) {
    const auto paths = SkeletonNodePaths(model);
    const auto found = std::ranges::find(paths, value);
    const auto index = static_cast<size_t>(found - paths.begin());
    const char* selected = index < model.nodes.size() ? model.nodes[index].name.c_str() : "jointを選択";
    if(ImGui::BeginCombo(label, selected)) {
        for(size_t node = 0; node < paths.size(); ++node) {
            ImGui::PushID(static_cast<int>(node));
            const auto text = DisplaySkeletonPath(paths[node]);
            if(ImGui::Selectable(text.c_str(), paths[node] == value)) {
                value = paths[node];
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}
}
AnimationRigPanel::AnimationRigPanel(AnimationRigDocument& document, std::filesystem::path root)
    : document_(document), root_(std::move(root)) {
}
std::string_view AnimationRigPanel::Id() const noexcept {
    return "AnimationRig";
}
std::string_view AnimationRigPanel::TitleKey() const noexcept {
    return "panel.animationRig";
}
std::array<float, 2> AnimationRigPanel::InitialSize() const noexcept {
    return {850, 650};
}
void AnimationRigPanel::Report(const Result<void>& result) {
    status_ = result ? "" : result.error().GetMessageText();
}
void AnimationRigPanel::LoadModels(const std::string& source, const std::string& target) {
    if(loading_.valid()) {
        return;
    }
    loadedSource_ = source;
    loadedTarget_ = target;
    const auto root = root_;
    // workerは値だけをcaptureする。閉じたPanel/Scene/ImGuiへ触れない。
    loading_ = std::async(std::launch::async, [root, source, target]() -> Result<Models> {
        Models models;
        for(const auto& [reference, destination] : {std::pair{source, &models.first}, std::pair{target, &models.second}}) {
            if(reference.empty()) {
                continue;
            }
            auto path = ResolveAssetReference(root, reference);
            if(!path) {
                return std::unexpected(path.error());
            }
            auto loaded = ModelLoader{}.LoadGltf(*path, false, false);
            if(!loaded) {
                return std::unexpected(loaded.error());
            }
            *destination = std::make_shared<const ModelAssetData>(std::move(*loaded));
        }
        return models;
    });
}
void AnimationRigPanel::DrawRetarget(RetargetProfile& profile) {
    RigAsset("Source Model", profile.sourceModel, root_, "model");
    RigAsset("Target Model", profile.targetModel, root_, "model");
    if(ImGui::Button("骨格を読み込む")) {
        LoadModels(profile.sourceModel, profile.targetModel);
    }
    const bool ready = models_.first && models_.second && loadedSource_ == profile.sourceModel && loadedTarget_ == profile.targetModel;
    if(!ready) {
        ImGui::TextDisabled("両モデルを読み込み、対応候補を生成してください。");
        return;
    }
    if(ImGui::Button("一意な同名jointから対応候補を生成")) {
        const auto source = profile.sourceModel, target = profile.targetModel;
        profile = SuggestRetargetProfile(*models_.first, *models_.second);
        profile.sourceModel = source;
        profile.targetModel = target;
    }
    ImGui::SameLine();
    if(ImGui::Button("現在の骨格署名を採用")) {
        profile.sourceSignature = SkeletonSignature(*models_.first);
        profile.targetSignature = SkeletonSignature(*models_.second);
    }
    ImGui::TextWrapped("source: %s / target: %s", profile.sourceSignature.c_str(), profile.targetSignature.c_str());
    ImGui::TextWrapped("自動候補は確認してから保存してください。rest相対TRS・基底回転・移動倍率で補正します。");
    if(ImGui::Button("対応を追加")) {
        profile.joints.push_back({});
    }
    for(size_t index = 0; index < profile.joints.size(); ++index) {
        auto& joint = profile.joints[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::Separator();
        RigJoint("Source Joint", joint.source, *models_.first);
        RigJoint("Target Joint", joint.target, *models_.second);
        ImGui::DragFloat3("移動倍率 XYZ", &joint.translationScale.x, .01f, -1000, 1000);
        if(ImGui::DragFloat4("基底回転 Quaternion", &joint.axis.x, .005f, -1, 1)) {
            joint.axis = joint.axis.Normalized();
        }
        if(ImGui::SmallButton("対応を削除")) {
            profile.joints.erase(profile.joints.begin() + static_cast<std::ptrdiff_t>(index));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    const auto valid = ValidateRetargetProfile(profile);
    if(!valid) {
        ImGui::TextWrapped("%s", valid.error().GetMessageText().c_str());
    }
}
void AnimationRigPanel::DrawLayers(AnimationLayerDocument& document) {
    RigAsset("Mask用Target Model (編集用)", maskModel_, root_, "model");
    if(ImGui::Button("Mask用骨格を読み込む")) {
        LoadModels("", maskModel_);
    }
    ImGui::TextWrapped("上から順に適用。Overrideは現在姿勢への混合、Additiveはrestとの差分です。mask空欄は全身です。");
    if(document.layers.size() < 32 && ImGui::Button("Layer追加")) {
        document.layers.push_back({});
    }
    for(size_t index = 0; index < document.layers.size(); ++index) {
        auto& layer = document.layers[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::SeparatorText(layer.name.c_str());
        RigText("名前", layer.name);
        ImGui::Checkbox("有効", &layer.enabled);
        RigAsset("Clip", layer.clip, root_, ".nxanim");
        int mode = layer.mode == PoseBlendMode::Additive ? 1 : 0;
        if(ImGui::Combo("合成", &mode, "Override\0Additive\0")) {
            layer.mode = mode == 0 ? PoseBlendMode::Override : PoseBlendMode::Additive;
        }
        ImGui::SliderFloat("重み", &layer.weight, 0, 1);
        ImGui::DragFloat("速度", &layer.speed, .01f, -100, 100);
        ImGui::Checkbox("Loop", &layer.loop);
        RigAsset("Speed Curve", layer.speedCurve, root_, ".curve");
        RigAsset("Weight Curve", layer.weightCurve, root_, ".curve");
        ImGui::DragFloat("Curve区間 秒 (0=clip長)", &layer.curveDuration, .01f, 0, 100000);
        if(models_.second && loadedTarget_ == maskModel_ && ImGui::BeginCombo("Mask joints", layer.mask.empty() ? "全身" : "選択jointと子孫")) {
            const auto paths = SkeletonNodePaths(*models_.second);
            if(ImGui::Selectable("全身に戻す")) {
                layer.mask.clear();
            }
            for(size_t node = 0; node < paths.size(); ++node) {
                const bool selected = std::ranges::find(layer.mask, paths[node]) != layer.mask.end();
                ImGui::PushID(static_cast<int>(node));
                if(ImGui::Selectable(models_.second->nodes[node].name.c_str(), selected, ImGuiSelectableFlags_DontClosePopups)) {
                    if(selected) {
                        std::erase(layer.mask, paths[node]);
                    } else {
                        layer.mask.push_back(paths[node]);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::Text("Mask: %zu joint roots", layer.mask.size());
        if(ImGui::SmallButton("削除")) {
            document.layers.erase(document.layers.begin() + static_cast<std::ptrdiff_t>(index));
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        if(index > 0 && ImGui::SmallButton("上へ")) {
            std::swap(document.layers[index], document.layers[index - 1]);
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        if(index + 1 < document.layers.size() && ImGui::SmallButton("下へ")) {
            std::swap(document.layers[index], document.layers[index + 1]);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
}
void AnimationRigPanel::Draw(const EditorContext&) {
    document_.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if(loading_.valid() && loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto loaded = loading_.get();
        if(loaded) {
            models_ = std::move(*loaded);
            status_.clear();
        } else {
            status_ = loaded.error().GetMessageText();
            models_ = {};
        }
    }
    if(shownReference_ != document_.Reference()) {
        shownReference_ = document_.Reference();
        std::snprintf(savePath_.data(), savePath_.size(), "%s", shownReference_.c_str());
    }
    ImGui::Text("%s%s", document_.Reference().c_str(), document_.IsDirty() ? " * 未保存" : "");
    if(ImGui::Button("新規Rig")) {
        pendingNew_ = 0;
    }
    ImGui::SameLine();
    if(ImGui::Button("新規Layers")) {
        pendingNew_ = 1;
    }
    ImGui::SameLine();
    std::string open;
    RigAsset("開く", open, root_, "rig");
    if(!open.empty()) {
        const auto path = ResolveAssetReference(root_, open);
        if(path) {
            pendingOpen_ = *path;
        }
    }
    if(ImGui::Button("Undo")) {
        document_.Undo();
    }
    ImGui::SameLine();
    if(ImGui::Button("Redo")) {
        document_.Redo();
    }
    ImGui::SetNextItemWidth((std::max)(160.0f, ImGui::GetContentRegionAvail().x - 100));
    ImGui::InputText("##RigSavePath", savePath_.data(), savePath_.size());
    ImGui::SameLine();
    if(ImGui::Button("保存")) {
        auto path = ResolveFileWithinRoot(root_, savePath_.data());
        if(path && std::filesystem::exists(*path) && document_.Reference() != savePath_.data()) {
            pendingSave_ = savePath_.data();
            ImGui::OpenPopup("Rig保存先の上書き");
        } else {
            Report(document_.Save(savePath_.data()));
        }
    }
    if(ImGui::BeginPopupModal("Rig保存先の上書き", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(pendingSave_.c_str());
        if(ImGui::Button("上書き保存")) {
            Report(document_.Save(pendingSave_));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("キャンセル")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if(pendingNew_ >= 0 || !pendingOpen_.empty()) {
        if(!document_.IsDirty()) {
            if(pendingNew_ >= 0) {
                document_.New(pendingNew_ == 1);
            } else {
                Report(document_.Open(pendingOpen_));
            }
            pendingNew_ = -1;
            pendingOpen_.clear();
        } else {
            ImGui::OpenPopup("未保存Rig/Layerの変更");
        }
    }
    if(ImGui::BeginPopupModal("未保存Rig/Layerの変更", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("編集中のassetに未保存の変更があります。");
        bool proceed = false;
        if(ImGui::Button("保存して続行")) {
            const auto saved = document_.Save(savePath_.data());
            Report(saved);
            proceed = saved.has_value();
        }
        ImGui::SameLine();
        if(ImGui::Button("破棄して続行")) {
            proceed = true;
        }
        ImGui::SameLine();
        if(ImGui::Button("キャンセル")) {
            pendingNew_ = -1;
            pendingOpen_.clear();
            ImGui::CloseCurrentPopup();
        }
        if(proceed) {
            if(pendingNew_ >= 0) {
                document_.New(pendingNew_ == 1);
            } else {
                Report(document_.Open(pendingOpen_));
            }
            pendingNew_ = -1;
            pendingOpen_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if(loading_.valid()) {
        ImGui::TextDisabled("モデルを読み込んでいます...");
    }
    if(!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
    document_.BeginEdit();
    if(auto* profile = std::get_if<RetargetProfile>(&document_.Draft())) {
        DrawRetarget(*profile);
    } else {
        DrawLayers(std::get<AnimationLayerDocument>(document_.Draft()));
    }
    if(!ImGui::IsAnyItemActive()) {
        document_.EndEdit();
    }
    if(document_.focused && !ImGui::GetIO().WantTextInput) {
        if(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            document_.Undo();
        }
        if(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            document_.Redo();
        }
        if(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            Report(document_.Save(document_.Reference()));
        }
    }
}
}
