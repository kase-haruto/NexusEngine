#include "CurveEditorPanel.h"
#include "Editor/Services/AssetCatalog.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace NexusEngine {
CurveEditorPanel::CurveEditorPanel(CurveEditDocument& document, std::filesystem::path root)
    : document_(document), root_(std::move(root)) {
    std::snprintf(path_.data(), path_.size(), "%s", document_.Reference().c_str());
}
std::string_view CurveEditorPanel::Id() const noexcept {
    return "CurveEditor";
}
std::string_view CurveEditorPanel::TitleKey() const noexcept {
    return "panel.curveEditor";
}
std::array<float, 2> CurveEditorPanel::InitialSize() const noexcept {
    return {880, 620};
}
bool CurveEditorPanel::IsScrollable() const noexcept {
    return false;
}
void CurveEditorPanel::RequestOpen(const std::filesystem::path& file) {
    pending_ = file;
}
void CurveEditorPanel::Apply(const Result<void>& result) {
    if(!result) {
        status_ = result.error().GetMessageText();
    } else {
        status_.clear();
    }
}
void CurveEditorPanel::CopySelected() {
    const std::vector<size_t> selection(selected_.begin(), selected_.end());
    auto text = document_.CopyKeys(selection);
    if(text && !selection.empty()) {
        ImGui::SetClipboardText(text->c_str());
        status_.clear();
    } else if(!text) {
        status_ = text.error().GetMessageText();
    }
}
void CurveEditorPanel::PasteSelected(double x) {
    const char* text = ImGui::GetClipboardText();
    auto result = document_.PasteKeys(text ? text : "", x);
    if(!result) {
        status_ = result.error().GetMessageText();
        return;
    }
    selected_ = std::set<size_t>(result->begin(), result->end());
    status_.clear();
}
void CurveEditorPanel::DrawEditingTools() {
    if(ImGui::SmallButton("コピー")) {
        CopySelected();
    }
    ImGui::SameLine();
    if(ImGui::SmallButton("貼付")) {
        PasteSelected(pasteX_);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputDouble("貼付X", &pasteX_, 0, 0, "%.4g");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::Combo("移動軸", &moveAxis_, "XY\0X\0Y\0");
    ImGui::Checkbox("スナップ", &snap_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputDouble("刻みX", &snapX_, 0, 0, "%.4g");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputDouble("刻みY", &snapY_, 0, 0, "%.4g");
    if(!std::isfinite(snapX_) || snapX_ <= 1e-12) {
        snapX_ = .1;
    }
    if(!std::isfinite(snapY_) || snapY_ <= 1e-12) {
        snapY_ = .1;
    }
    ImGui::SetNextItemWidth(150);
    if(ImGui::BeginCombo("プリセット", "選択 (Undo可能)")) {
        const char* names[] = {"Constant 1", "Linear", "Smooth", "Ease In", "Ease Out", "Pulse"};
        for(int index = 0; index < 6; ++index) {
            if(ImGui::Selectable(names[index])) {
                Apply(document_.SetPreset(index));
                selected_.clear();
                FrameAll();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if(ImGui::BeginCombo("比較Curve", "重ね合わせ (読取専用)")) {
        if(ImGui::Selectable("比較をクリア")) {
            overlays_.clear();
        }
        std::error_code error;
        for(std::filesystem::recursive_directory_iterator it(root_, std::filesystem::directory_options::skip_permission_denied, error), end;
            it != end && !error; it.increment(error)) {
            if(it->is_regular_file() && it->path().extension() == ".curve") {
                const auto reference = AssetCatalog::Utf8(it->path().lexically_relative(root_));
                if(ImGui::Selectable(reference.c_str())) {
                    auto overlay = LoadFloatCurve(it->path());
                    if(overlay) {
                        std::erase_if(overlays_, [&](const auto& item) {
                            return item.first == reference;
                        });
                        if(overlays_.size() >= 8) {
                            overlays_.erase(overlays_.begin());
                        }
                        overlays_.emplace_back(reference, std::move(*overlay));
                    } else {
                        status_ = overlay.error().GetMessageText();
                    }
                }
            }
        }
        ImGui::EndCombo();
    }
    // 比較の色と名前を常に示す。再選択でdisk snapshotを再取得する。
    for(size_t index = 0; index < overlays_.size(); ++index) {
        ImGui::TextColored(ImVec4(.5f + .06f * static_cast<float>(index), .65f, .8f, 1), "%s (比較)", overlays_[index].first.c_str());
    }
}
void CurveEditorPanel::FrameAll() {
    minimum_ = {0, 0};
    maximum_ = {1, 1};
    const auto& keys = document_.Value().Keys();
    if(!keys.empty()) {
        minimum_ = {keys.front().x, keys.front().y};
        maximum_ = minimum_;
        for(const auto& key : keys) {
            minimum_[1] = (std::min)(minimum_[1], key.y);
            maximum_[0] = (std::max)(maximum_[0], key.x);
            maximum_[1] = (std::max)(maximum_[1], key.y);
        }
        // 接線のovershootも全体表示へ含める。
        for(int i = 0; i <= 256; ++i) {
            const double x = minimum_[0] + (maximum_[0] - minimum_[0]) * i / 256;
            const double y = document_.Value().Evaluate(x);
            minimum_[1] = (std::min)(minimum_[1], y);
            maximum_[1] = (std::max)(maximum_[1], y);
        }
    }
    for(size_t axis = 0; axis < 2; ++axis) {
        const double padding = (std::max)(.1, (maximum_[axis] - minimum_[axis]) * .12);
        minimum_[axis] -= padding;
        maximum_[axis] += padding;
    }
    fitted_ = true;
}
void CurveEditorPanel::DeleteSelected() {
    auto keys = document_.Value().Keys();
    for(auto it = selected_.rbegin(); it != selected_.rend(); ++it) {
        if(*it < keys.size()) {
            keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(*it));
        }
    }
    Apply(document_.SetKeys(std::move(keys)));
    document_.EndEdit();
    selected_.clear();
}
void CurveEditorPanel::AddKey(double x, double y) {
    if(snap_) {
        x = std::round(x / snapX_) * snapX_;
        y = std::round(y / snapY_) * snapY_;
    }
    auto curve = document_.Value();
    auto inserted = curve.InsertKey(x, y);
    if(!inserted) {
        Apply(inserted);
        return;
    }
    const auto& keys = curve.Keys();
    const auto found = std::lower_bound(keys.begin(), keys.end(), x, [](const auto& key, double value) {
        return key.x < value;
    });
    const auto index = static_cast<size_t>(found - keys.begin());
    Apply(document_.SetKeys(curve.Keys()));
    document_.EndEdit();
    selected_ = {index};
}
void CurveEditorPanel::DrawSelection() {
    const auto& current = document_.Value().Keys();
    if(selected_.empty() || *selected_.begin() >= current.size()) {
        ImGui::TextDisabled("キーを選択すると数値と補間を編集できます。");
        return;
    }
    auto keys = current;
    auto& key = keys[*selected_.begin()];
    // 自動接線の表示と手動モードへの切替は、保存値ではなく現在の評価勾配を使う。
    key.inTangent = document_.Value().Tangent(*selected_.begin(), true);
    key.outTangent = document_.Value().Tangent(*selected_.begin(), false);
    bool edited = false;
    ImGui::Text("%zu キー選択 / 数値編集は先頭キー", selected_.size());
    ImGui::SetNextItemWidth(130);
    edited |= ImGui::InputDouble("X", &key.x, 0, 0, "%.6f");
    const bool xActive = ImGui::IsItemActive();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    edited |= ImGui::InputDouble("Y", &key.y, 0, 0, "%.6f");
    bool active = xActive || ImGui::IsItemActive();
    int interpolation = static_cast<int>(key.interpolation);
    ImGui::SetNextItemWidth(160);
    edited |= ImGui::Combo("補間", &interpolation, "Constant\0Linear\0Cubic Hermite\0Weighted Bezier\0");
    key.interpolation = static_cast<CurveInterpolation>(interpolation);
    ImGui::SameLine();
    int mode = static_cast<int>(key.tangentMode);
    ImGui::SetNextItemWidth(160);
    edited |= ImGui::Combo("接線", &mode, "Free\0Aligned\0Broken\0Auto\0Clamped Auto\0");
    key.tangentMode = static_cast<CurveTangentMode>(mode);
    const bool automatic = key.tangentMode == CurveTangentMode::Auto || key.tangentMode == CurveTangentMode::ClampedAuto;
    ImGui::BeginDisabled(automatic);
    ImGui::SetNextItemWidth(130);
    const bool inEdited = ImGui::InputDouble("In dy/dx", &key.inTangent, 0, 0, "%.6f");
    edited |= inEdited;
    if(inEdited && key.tangentMode == CurveTangentMode::Aligned) {
        key.outTangent = key.inTangent;
    }
    active |= ImGui::IsItemActive();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    const bool outEdited = ImGui::InputDouble("Out dy/dx", &key.outTangent, 0, 0, "%.6f");
    edited |= outEdited;
    if(outEdited && key.tangentMode == CurveTangentMode::Aligned) {
        key.inTangent = key.outTangent;
    }
    active |= ImGui::IsItemActive();
    ImGui::EndDisabled();
    const auto selectedIndex = *selected_.begin();
    const bool weighted = key.interpolation == CurveInterpolation::Bezier ||
        (selectedIndex > 0 && keys[selectedIndex - 1].interpolation == CurveInterpolation::Bezier);
    ImGui::BeginDisabled(!weighted);
    ImGui::SetNextItemWidth(130);
    edited |= ImGui::InputDouble("In weight [0..1]", &key.inWeight, 0, 0, "%.4f");
    active |= ImGui::IsItemActive();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    edited |= ImGui::InputDouble("Out weight [0..1]", &key.outWeight, 0, 0, "%.4f");
    active |= ImGui::IsItemActive();
    ImGui::EndDisabled();
    if(edited) {
        Apply(document_.SetKeys(std::move(keys)));
    }
    if(!active && dragKey_ < 0 && !ImGui::IsAnyItemActive()) {
        document_.EndEdit();
    }
}
void CurveEditorPanel::DrawGraph() {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const auto available = ImGui::GetContentRegionAvail();
    const ImVec2 size{(std::max)(160.0f, available.x), (std::max)(120.0f, available.y - ImGui::GetScrollY() - 170)};
    ImGui::InvisibleButton("CurveGraph", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    const auto& io = ImGui::GetIO();
    const auto screen = [&](double x, double y) {
        // 極端な接線の画面外座標をfloatへ直接変換しない。clip前の無限大を防ぐ。
        return ImVec2{origin.x + static_cast<float>(std::clamp((x - minimum_[0]) / (maximum_[0] - minimum_[0]), -1e4, 1e4)) * size.x,
            origin.y + size.y - static_cast<float>(std::clamp((y - minimum_[1]) / (maximum_[1] - minimum_[1]), -1e4, 1e4)) * size.y};
    };
    const auto point = [&](ImVec2 position) {
        return std::array<double, 2>{minimum_[0] + (position.x - origin.x) / size.x * (maximum_[0] - minimum_[0]),
            maximum_[1] - (position.y - origin.y) / size.y * (maximum_[1] - minimum_[1])};
    };
    const auto mouse = point(io.MousePos);
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(23, 26, 32, 255));
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    for(int axis = 0; axis < 2; ++axis) {
        const double range = maximum_[axis] - minimum_[axis];
        const double rawStep = range / 7;
        double step = std::pow(10.0, std::floor(std::log10(rawStep)));
        const double ratio = rawStep / step;
        step *= ratio > 5 ? 10 : ratio > 2 ? 5 : ratio > 1 ? 2 : 1;
        for(double value = std::ceil(minimum_[axis] / step) * step; value <= maximum_[axis]; value += step) {
            const auto at = axis == 0 ? screen(value, maximum_[1]) : screen(minimum_[0], value);
            const auto end = axis == 0 ? ImVec2{at.x, origin.y + size.y} : ImVec2{origin.x + size.x, at.y};
            draw->AddLine(at, end, std::abs(value) < step * .001 ? IM_COL32(110, 118, 132, 255) : IM_COL32(44, 49, 59, 255));
            char label[48];
            std::snprintf(label, sizeof(label), "%.4g", value);
            draw->AddText({at.x + 3, at.y + 2}, IM_COL32(140, 146, 160, 255), label);
        }
    }
    const auto& curve = document_.Value();
    for(size_t index = 0; index < overlays_.size(); ++index) {
        const auto& overlay = overlays_[index].second;
        auto previousOverlay = screen(minimum_[0], overlay.Evaluate(minimum_[0]));
        for(int sample = 1; sample <= 256; ++sample) {
            const double x = minimum_[0] + (maximum_[0] - minimum_[0]) * sample / 256;
            const auto next = screen(x, overlay.Evaluate(x));
            draw->AddLine(previousOverlay, next, ImGui::ColorConvertFloat4ToU32(ImVec4(.5f + .06f * static_cast<float>(index), .65f, .8f, .6f)));
            previousOverlay = next;
        }
    }
    ImVec2 previous = screen(minimum_[0], curve.Evaluate(minimum_[0]));
    for(int sample = 1; sample <= 512; ++sample) {
        const double x = minimum_[0] + (maximum_[0] - minimum_[0]) * sample / 512;
        const auto next = screen(x, curve.Evaluate(x));
        draw->AddLine(previous, next, IM_COL32(94, 202, 242, 255), 2);
        previous = next;
    }
    int hitKey = -1, hitTangent = 0;
    const auto hit = [&](ImVec2 position) {
        const float dx = position.x - io.MousePos.x;
        const float dy = position.y - io.MousePos.y;
        return dx * dx + dy * dy < 81;
    };
    for(size_t index = 0; index < curve.Keys().size(); ++index) {
        const auto& key = curve.Keys()[index];
        const auto at = screen(key.x, key.y);
        if(selected_.contains(index)) {
            for(int side : {-1, 1}) {
                double dx = (maximum_[0] - minimum_[0]) * .08 * side;
                if(side < 0 && index > 0 && curve.Keys()[index - 1].interpolation == CurveInterpolation::Bezier) {
                    dx = -(key.x - curve.Keys()[index - 1].x) * key.inWeight;
                } else if(side > 0 && index + 1 < curve.Keys().size() && key.interpolation == CurveInterpolation::Bezier) {
                    dx = (curve.Keys()[index + 1].x - key.x) * key.outWeight;
                }
                const auto handle = screen(key.x + dx, key.y + dx * curve.Tangent(index, side < 0));
                draw->AddLine(at, handle, IM_COL32(229, 178, 89, 255));
                draw->AddCircleFilled(handle, 4, IM_COL32(229, 178, 89, 255));
                if(hit(handle)) {
                    hitKey = static_cast<int>(index);
                    hitTangent = side;
                }
            }
        }
        draw->AddCircleFilled(at, selected_.contains(index) ? 6.0f : 4.5f,
            selected_.contains(index) ? IM_COL32(255, 206, 102, 255) : IM_COL32(208, 218, 231, 255));
        if(hit(at)) {
            hitKey = static_cast<int>(index);
            hitTangent = 0;
        }
    }
    draw->PopClipRect();
    if(document_.preview.input) {
        const auto x = document_.preview.input();
        if(x && *x >= minimum_[0] && *x <= maximum_[0]) {
            const auto at = screen(*x, 0);
            draw->AddLine({at.x, origin.y}, {at.x, origin.y + size.y}, IM_COL32(255, 120, 85, 255), 2);
        }
    }
    if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if(hitKey >= 0) {
            const size_t index = static_cast<size_t>(hitKey);
            if(io.KeyCtrl || io.KeyShift) {
                if(selected_.contains(index)) {
                    selected_.erase(index);
                } else {
                    selected_.insert(index);
                }
            } else if(!selected_.contains(index)) {
                selected_ = {index};
            }
            if(selected_.contains(index)) {
                dragKey_ = hitKey;
                dragTangent_ = hitTangent;
                dragAxis_ = moveAxis_;
                dragMouse_ = mouse;
                dragKeys_ = curve.Keys();
                if(hitTangent != 0) {
                    dragKeys_[index].inTangent = curve.Tangent(index, true);
                    dragKeys_[index].outTangent = curve.Tangent(index, false);
                }
                document_.BeginEdit();
            }
        } else {
            marquee_ = true;
            marqueeStart_ = mouse;
            marqueeBase_ = io.KeyCtrl || io.KeyShift ? selected_ : std::set<size_t>{};
            selected_ = marqueeBase_;
        }
    }
    if(marquee_) {
        selected_ = marqueeBase_;
        for(size_t index = 0; index < curve.Keys().size(); ++index) {
            const auto& key = curve.Keys()[index];
            if(key.x >= (std::min)(mouse[0], marqueeStart_[0]) && key.x <= (std::max)(mouse[0], marqueeStart_[0]) &&
                key.y >= (std::min)(mouse[1], marqueeStart_[1]) && key.y <= (std::max)(mouse[1], marqueeStart_[1])) {
                selected_.insert(index);
            }
        }
        const auto first = screen(marqueeStart_[0], marqueeStart_[1]);
        // pointerがgraph外へ出ても、隣の設定UIへ矩形を描かない。
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        draw->AddRectFilled({(std::min)(first.x, io.MousePos.x), (std::min)(first.y, io.MousePos.y)},
            {(std::max)(first.x, io.MousePos.x), (std::max)(first.y, io.MousePos.y)}, IM_COL32(85, 165, 235, 40));
        draw->PopClipRect();
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            marquee_ = false;
        }
    }
    const auto startPosition = screen(dragMouse_[0], dragMouse_[1]);
    const float dragDx = io.MousePos.x - startPosition.x;
    const float dragDy = io.MousePos.y - startPosition.y;
    if(dragKey_ >= 0 && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1) || ImGui::IsMouseReleased(ImGuiMouseButton_Left)) &&
        dragDx * dragDx + dragDy * dragDy >= 1) {
        // 高速dragが1frameでreleaseまで届いても、最終座標を適用してからUndoを閉じる。
        auto keys = dragKeys_;
        if(dragTangent_ != 0) {
            auto& key = keys[static_cast<size_t>(dragKey_)];
            const double dx = mouse[0] - key.x;
            if(std::abs(dx) > 1e-9) {
                const double slope = (mouse[1] - key.y) / dx;
                const bool aligned = key.tangentMode == CurveTangentMode::Aligned;
                key.tangentMode = aligned ? CurveTangentMode::Aligned : CurveTangentMode::Broken;
                if(dragTangent_ < 0 || aligned) {
                    key.inTangent = slope;
                }
                if(dragTangent_ > 0 || aligned) {
                    key.outTangent = slope;
                }
                const size_t index = static_cast<size_t>(dragKey_);
                if(dragTangent_ < 0 && index > 0 && keys[index - 1].interpolation == CurveInterpolation::Bezier) {
                    key.inWeight = std::clamp(-dx / (key.x - keys[index - 1].x), 0.0, 1.0);
                } else if(dragTangent_ > 0 && index + 1 < keys.size() && key.interpolation == CurveInterpolation::Bezier) {
                    key.outWeight = std::clamp(dx / (keys[index + 1].x - key.x), 0.0, 1.0);
                }
            }
        } else {
            double dx = mouse[0] - dragMouse_[0];
            double dy = mouse[1] - dragMouse_[1];
            if(dragAxis_ == 0 && io.KeyShift) {
                dragAxis_ = std::abs(dragDx) >= std::abs(dragDy) ? 1 : 2;
            }
            if(dragAxis_ == 1) {
                dy = 0;
            } else if(dragAxis_ == 2) {
                dx = 0;
            }
            if(snap_) {
                const auto& anchor = keys[static_cast<size_t>(dragKey_)];
                if(dragAxis_ != 2) {
                    dx = std::round((anchor.x + dx) / snapX_) * snapX_ - anchor.x;
                }
                if(dragAxis_ != 1) {
                    dy = std::round((anchor.y + dy) / snapY_) * snapY_ - anchor.y;
                }
            }
            // 選択group内の間隔を維持し、未選択keyを越えない。重複Xを生成しない。
            for(size_t index : selected_) {
                if(index > 0 && !selected_.contains(index - 1)) {
                    dx = (std::max)(dx, keys[index - 1].x + 1e-6 - keys[index].x);
                }
                if(index + 1 < keys.size() && !selected_.contains(index + 1)) {
                    dx = (std::min)(dx, keys[index + 1].x - 1e-6 - keys[index].x);
                }
            }
            for(size_t index : selected_) {
                keys[index].x += dx;
                keys[index].y += dy;
            }
        }
        Apply(document_.SetKeys(std::move(keys)));
    }
    if(dragKey_ >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        dragKey_ = -1;
        document_.EndEdit();
    }
    if(hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hitKey < 0) {
        AddKey(mouse[0], curve.Evaluate(mouse[0]));
    }
    if(hovered && io.MouseWheel != 0) {
        const double factor = std::pow(1.15, -io.MouseWheel);
        for(size_t axis = 0; axis < 2; ++axis) {
            const double span = std::clamp((maximum_[axis] - minimum_[axis]) * factor, 1e-5, 1e12);
            const double fraction = (mouse[axis] - minimum_[axis]) / (maximum_[axis] - minimum_[axis]);
            minimum_[axis] = mouse[axis] - fraction * span;
            maximum_[axis] = minimum_[axis] + span;
        }
    }
    if(hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const double dx = -io.MouseDelta.x / size.x * (maximum_[0] - minimum_[0]);
        const double dy = io.MouseDelta.y / size.y * (maximum_[1] - minimum_[1]);
        minimum_[0] += dx;
        maximum_[0] += dx;
        minimum_[1] += dy;
        maximum_[1] += dy;
    }
    if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if(hitKey >= 0 && !selected_.contains(static_cast<size_t>(hitKey))) {
            selected_ = {static_cast<size_t>(hitKey)};
        } else if(hitKey < 0) {
            selected_.clear();
        }
        dragMouse_ = mouse;
        ImGui::OpenPopup("CurveContext");
    }
    if(ImGui::BeginPopup("CurveContext")) {
        if(ImGui::MenuItem("ここにキーを追加")) {
            AddKey(dragMouse_[0], dragMouse_[1]);
        }
        if(ImGui::MenuItem("カーブ上へキーを追加")) {
            AddKey(dragMouse_[0], curve.Evaluate(dragMouse_[0]));
        }
        if(ImGui::MenuItem("選択キーを削除", "Delete", false, !selected_.empty())) {
            DeleteSelected();
        }
        if(ImGui::MenuItem("コピー", "Ctrl+C", false, !selected_.empty())) {
            CopySelected();
        }
        if(ImGui::MenuItem("ここに貼付", "Ctrl+V")) {
            PasteSelected(dragMouse_[0]);
        }
        if(ImGui::BeginMenu("補間", !selected_.empty())) {
            const char* labels[] = {"Constant", "Linear", "Cubic Hermite", "Weighted Bezier"};
            for(int mode = 0; mode < 4; ++mode) {
                if(ImGui::MenuItem(labels[mode])) {
                    auto keys = curve.Keys();
                    for(size_t index : selected_) {
                        keys[index].interpolation = static_cast<CurveInterpolation>(mode);
                    }
                    Apply(document_.SetKeys(std::move(keys)));
                    document_.EndEdit();
                }
            }
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("接線モード", !selected_.empty())) {
            const char* labels[] = {"Free", "Aligned", "Broken", "Auto", "Clamped Auto"};
            for(int mode = 0; mode < 5; ++mode) {
                if(ImGui::MenuItem(labels[mode])) {
                    auto keys = curve.Keys();
                    for(size_t index : selected_) {
                        keys[index].inTangent = curve.Tangent(index, true);
                        keys[index].outTangent = curve.Tangent(index, false);
                        keys[index].tangentMode = static_cast<CurveTangentMode>(mode);
                    }
                    Apply(document_.SetKeys(std::move(keys)));
                    document_.EndEdit();
                }
            }
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("X / Y 数値入力", !selected_.empty())) {
            auto keys = curve.Keys();
            auto& key = keys[*selected_.begin()];
            bool edited = ImGui::InputDouble("X", &key.x, 0, 0, "%.6f");
            edited |= ImGui::InputDouble("Y", &key.y, 0, 0, "%.6f");
            if(edited) {
                Apply(document_.SetKeys(std::move(keys)));
            }
            if(!ImGui::IsAnyItemActive()) {
                document_.EndEdit();
            }
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("全体表示", "F")) {
            FrameAll();
        }
        if(ImGui::MenuItem("表示をリセット")) {
            minimum_ = {-.1, -.1};
            maximum_ = {1.1, 2.1};
        }
        ImGui::EndPopup();
    }
    if(hovered && !io.WantTextInput) {
        if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
            CopySelected();
        }
        if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
            PasteSelected(mouse[0]);
        }
        if(ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            DeleteSelected();
        }
        if(ImGui::IsKeyPressed(ImGuiKey_F)) {
            FrameAll();
        }
    }
    ImGui::TextDisabled("X %.5g   Y %.5g   |  空白ドラッグ: 範囲選択   Shiftドラッグ: 軸固定   Ctrl+C/V: コピー/貼付", mouse[0], mouse[1]);
}
void CurveEditorPanel::Draw(const EditorContext&) {
    document_.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    std::erase_if(selected_, [&](size_t index) { return index >= document_.Value().Keys().size(); });
    if(dragKey_ >= 0 && ((!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) ||
        static_cast<size_t>(dragKey_) >= document_.Value().Keys().size())) {
        document_.EndEdit();
        dragKey_ = -1;
    }
    if(displayedReference_ != document_.Reference()) {
        selected_.clear();
        marquee_ = false;
        dragKey_ = -1;
        fitted_ = false;
        displayedReference_ = document_.Reference();
        std::snprintf(path_.data(), path_.size(), "%s", displayedReference_.c_str());
    }
    if(!fitted_) {
        FrameAll();
    }
    // 小さいdockでも数値欄へ到達できるよう、内容だけを縦scroll可能にする。
    // wheelはgraphのzoomへ渡し、scrollbarのdragで欄を移動する。
    if(!ImGui::BeginChild("CurveEditorContent", {0, 0}, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    ImGui::Text("%s%s", document_.Reference().c_str(), document_.IsDirty() ? " * 未保存" : "");
    if(document_.preview.input) {
        const auto input = document_.preview.input();
        if(input) {
            ImGui::Text("%s  X=%.4g  Y=%.4g", document_.preview.label.c_str(), *input, document_.Value().Evaluate(*input));
            if(ImGui::SmallButton("Preview Play") && document_.preview.play) {
                document_.preview.play();
            }
            ImGui::SameLine();
            if(ImGui::SmallButton("Pause") && document_.preview.pause) {
                document_.preview.pause();
            }
            ImGui::SameLine();
            if(ImGui::SmallButton("Stop") && document_.preview.stop) {
                document_.preview.stop();
            }
            ImGui::SameLine();
            double position = *input;
            ImGui::SetNextItemWidth(140);
            if(ImGui::InputDouble("Preview X", &position, 0, 0, "%.4f") && document_.preview.seek) {
                document_.preview.seek(position);
            }
        } else {
            ImGui::TextDisabled("使用先のプレビュー未接続 / Curve単体編集");
        }
    }
    if(ImGui::Button("新規")) {
        pendingNew_ = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth((std::min)(220.0f, (std::max)(100.0f, ImGui::GetContentRegionAvail().x - 90)));
    if(ImGui::BeginCombo("開く", "Curve Asset")) {
        std::error_code error;
        for(std::filesystem::recursive_directory_iterator it(root_, std::filesystem::directory_options::skip_permission_denied, error), end; it != end && !error; it.increment(error)) {
            if(it->path().extension() == ".curve") {
                const auto label = AssetCatalog::Utf8(it->path().lexically_relative(root_));
                if(ImGui::Selectable(label.c_str())) {
                    RequestOpen(it->path());
                }
            }
        }
        ImGui::EndCombo();
    }
    if(ImGui::Button("Undo")) {
        document_.Undo();
        selected_.clear();
    }
    ImGui::SameLine();
    if(ImGui::Button("Redo")) {
        document_.Redo();
        selected_.clear();
    }
    ImGui::SameLine();
    if(ImGui::Button("全体表示")) {
        FrameAll();
    }
    ImGui::SameLine();
    if(ImGui::Button("選択表示") && !selected_.empty()) {
        const auto& keys = document_.Value().Keys();
        const auto& first = keys[*selected_.begin()];
        minimum_ = {first.x, first.y};
        maximum_ = minimum_;
        for(size_t index : selected_) {
            minimum_[0] = (std::min)(minimum_[0], keys[index].x);
            minimum_[1] = (std::min)(minimum_[1], keys[index].y);
            maximum_[0] = (std::max)(maximum_[0], keys[index].x);
            maximum_[1] = (std::max)(maximum_[1], keys[index].y);
        }
        for(size_t axis = 0; axis < 2; ++axis) {
            const double padding = (std::max)(.1, (maximum_[axis] - minimum_[axis]) * .2);
            minimum_[axis] -= padding;
            maximum_[axis] += padding;
        }
    }
    ImGui::SameLine();
    if(ImGui::Button("再読込")) {
        auto path = ResolveFileWithinRoot(root_, document_.Reference());
        if(path) {
            RequestOpen(*path);
        }
    }
    ImGui::SetNextItemWidth((std::max)(150.0f, ImGui::GetContentRegionAvail().x - 170));
    ImGui::InputText("##CurvePath", path_.data(), path_.size());
    ImGui::SameLine();
    if(ImGui::Button("保存 / 別名保存")) {
        auto path = ResolveFileWithinRoot(root_, path_.data());
        if(path && std::filesystem::exists(*path) && (!document_.HasSavedFile() || document_.Reference() != path_.data())) {
            pendingSave_ = path_.data();
            ImGui::OpenPopup("保存先のCurveを上書き");
        } else {
            Apply(document_.Save(path_.data()));
        }
    }
    if(ImGui::BeginPopupModal("保存先のCurveを上書き", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s\n既存のCurveを上書きします。", pendingSave_.c_str());
        if(ImGui::Button("上書き保存")) {
            Apply(document_.Save(pendingSave_));
            pendingSave_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("戻る")) {
            pendingSave_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if(pendingNew_ || !pending_.empty()) {
        if(document_.IsDirty()) {
            ImGui::OpenPopup("未保存Curveの変更");
        } else {
            if(pendingNew_) {
                document_.New();
            } else {
                Apply(document_.Open(pending_));
            }
            pending_.clear();
            pendingNew_ = false;
            selected_.clear();
            fitted_ = false;
            std::snprintf(path_.data(), path_.size(), "%s", document_.Reference().c_str());
        }
    }
    if(ImGui::BeginPopupModal("未保存Curveの変更", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("編集中のCurveに未保存の変更があります。");
        bool proceed = false;
        if(ImGui::Button("保存して続行")) {
            const auto result = document_.Save(path_.data());
            Apply(result);
            proceed = result.has_value();
        }
        ImGui::SameLine();
        if(ImGui::Button("破棄して続行")) {
            proceed = true;
        }
        ImGui::SameLine();
        if(ImGui::Button("キャンセル")) {
            pending_.clear();
            pendingNew_ = false;
            ImGui::CloseCurrentPopup();
        }
        if(proceed) {
            if(pendingNew_) {
                document_.New();
            } else {
                Apply(document_.Open(pending_));
            }
            pending_.clear();
            pendingNew_ = false;
            selected_.clear();
            fitted_ = false;
            std::snprintf(path_.data(), path_.size(), "%s", document_.Reference().c_str());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if(document_.focused && !ImGui::GetIO().WantTextInput) {
        if(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            document_.Undo();
            selected_.clear();
        }
        if(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            document_.Redo();
            selected_.clear();
        }
        if(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            auto path = ResolveFileWithinRoot(root_, document_.Reference());
            if(path && !document_.HasSavedFile() && std::filesystem::exists(*path)) {
                pendingSave_ = document_.Reference();
                ImGui::OpenPopup("保存先のCurveを上書き");
            } else {
                Apply(document_.Save(document_.Reference()));
            }
        }
    }
    if(!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
    DrawEditingTools();
    DrawGraph();
    DrawSelection();
    ImGui::EndChild();
}
}

