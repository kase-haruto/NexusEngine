#include "ViewportTools.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace NexusEngine {
namespace {
Vector3 Point(Vector3 p, const Matrix4x4& m, float w = 1) {
    return {p.x*m.At(0,0)+p.y*m.At(1,0)+p.z*m.At(2,0)+w*m.At(3,0),
        p.x*m.At(0,1)+p.y*m.At(1,1)+p.z*m.At(2,1)+w*m.At(3,1),
        p.x*m.At(0,2)+p.y*m.At(1,2)+p.z*m.At(2,2)+w*m.At(3,2)};
}
std::optional<Vector3> Unproject(Vector3 point, const Matrix4x4& matrix) {
    auto result = Point(point, matrix);
    const float w = point.x*matrix.At(0,3)+point.y*matrix.At(1,3)+point.z*matrix.At(2,3)+matrix.At(3,3);
    if(!std::isfinite(w) || std::abs(w) < 1e-8f) return {};
    result = result / w;
    if(!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) return {};
    return result;
}
bool HitBox(Vector3 origin, Vector3 direction, Vector3 low, Vector3 high, float maximum) {
    float begin = 0, end = maximum;
    const float o[]{origin.x,origin.y,origin.z}, d[]{direction.x,direction.y,direction.z};
    const float lo[]{low.x,low.y,low.z}, hi[]{high.x,high.y,high.z};
    for(size_t axis = 0; axis < 3; ++axis) {
        if(std::abs(d[axis]) < 1e-9f) { if(o[axis] < lo[axis] || o[axis] > hi[axis]) return false; continue; }
        float a = (lo[axis]-o[axis])/d[axis], b = (hi[axis]-o[axis])/d[axis];
        if(a > b) std::swap(a,b);
        begin = std::max(begin,a); end = std::min(end,b); if(end < begin) return false;
    }
    return true;
}
Vector3 Vertex(const MeshAssetData& mesh, uint32_t index) {
    float xyz[3]; std::memcpy(xyz, mesh.vertexData.data() + static_cast<size_t>(index)*mesh.vertexStride, sizeof(xyz));
    return {xyz[0],xyz[1],xyz[2]};
}
uint32_t Index(const MeshAssetData& mesh, size_t index) {
    if(mesh.indexFormat == IndexFormat::UInt16) { uint16_t value; std::memcpy(&value, mesh.indexData.data()+index*2,2); return value; }
    uint32_t value; std::memcpy(&value, mesh.indexData.data()+index*4,4); return value;
}
std::optional<float> HitTriangle(Vector3 origin, Vector3 direction, Vector3 a, Vector3 b, Vector3 c) {
    const auto edge1 = b-a, edge2 = c-a, p = Vector3::Cross(direction,edge2);
    const float determinant = Vector3::Dot(edge1,p);
    if(std::abs(determinant) < 1e-9f) return {};
    const auto t = origin-a; const float u = Vector3::Dot(t,p)/determinant;
    if(u < 0 || u > 1) return {};
    const auto q = Vector3::Cross(t,edge1); const float v = Vector3::Dot(direction,q)/determinant;
    if(v < 0 || u+v > 1) return {};
    const float distance = Vector3::Dot(edge2,q)/determinant;
    return distance > 0 ? std::optional<float>(distance) : std::nullopt;
}
}
void ViewportTools::SetModel(const ModelAssetData& model, const ModelInstance& pose) {
    model_ = &model; pose_ = &pose; bounds_.assign(model.meshes.size(), {});
    for(size_t i = 0; i < model.meshes.size(); ++i) {
        const auto& mesh = model.meshes[i];
        if(mesh.vertexStride < sizeof(float)*3 || mesh.vertexData.size() < mesh.vertexStride) continue;
        auto& bound = bounds_[i]; bound.low = bound.high = Vertex(mesh,0); bound.valid = true;
        for(size_t vertex = 1; vertex < mesh.vertexData.size()/mesh.vertexStride; ++vertex) {
            const auto p = Vertex(mesh,static_cast<uint32_t>(vertex));
            bound.low = {std::min(bound.low.x,p.x), std::min(bound.low.y,p.y), std::min(bound.low.z,p.z)};
            bound.high = {std::max(bound.high.x,p.x), std::max(bound.high.y,p.y), std::max(bound.high.z,p.z)};
        }
    }
}
void ViewportTools::Prepare(RenderScene& frame, float aspect) {
    frame_ = &frame;
    if(session_.IsPlaying()) return;
    if(generation_ != session_.Generation()) {
        generation_ = session_.Generation(); position_ = {0,2,-10}; yaw_ = pitch_ = 0;
        if(frame.camera) if(auto world = TryInverse(frame.camera->viewMatrix)) {
            position_ = Point({},*world); const auto forward = Point({0,0,1},*world,0).Normalized();
            yaw_ = std::atan2(forward.x,forward.z); pitch_ = -std::asin(std::clamp(forward.y,-1.0f,1.0f));
        }
    }
    const auto world = MakeAffineMatrix(Vector3::One(), Quaternion::FromEulerRadians({pitch_,yaw_,0}), position_);
    const auto view = TryInverse(world);
    if(!view || aspect <= 0) return;
    const auto projection = MakePerspectiveFovMatrix(1.0471975512f,aspect,0.05f,100000.0f);
    frame.camera = RenderCamera{{}, *view, projection, Multiply(*view,projection)};
}
bool ViewportTools::Pick(float x, float y, bool toggle) {
    if(session_.IsPlaying() || !frame_ || !frame_->camera || !model_ || !pose_ ||
        !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1) return false;
    auto inverse = TryInverse(frame_->camera->viewProjectionMatrix);
    if(!inverse) return false;
    auto near = Unproject({2*x-1,1-2*y,0},*inverse), far = Unproject({2*x-1,1-2*y,1},*inverse);
    if(!near || !far) return false;
    const auto direction = (*far-*near).Normalized();
    float closest = std::numeric_limits<float>::max(); EntityId picked;
    const auto& transforms = pose_->GetNodeWorldTransforms();
    for(const auto& object : frame_->primitives) for(size_t node = 0; node < model_->nodes.size() && node < transforms.size(); ++node) {
        if(object.nodeIndex != PrimitiveRenderItem::kAllNodes && object.nodeIndex != node) continue;
        for(auto meshIndex : model_->nodes[node].meshIndices) {
            if(meshIndex >= bounds_.size() || !bounds_[meshIndex].valid) continue;
            const auto& mesh = model_->meshes[meshIndex];
            // 静的geometryを厳密に三角形判定する。Skin変形のCPU展開は別の拡張対象。
            if(mesh.skinIndex != MeshAssetData::kNoSkin) continue;
            auto local = TryInverse(Multiply(transforms[node],object.worldMatrix));
            if(!local) continue;
            const auto origin = Point(*near,*local), ray = Point(direction,*local,0);
            const auto& bound = bounds_[meshIndex];
            if(!HitBox(origin,ray,bound.low,bound.high,closest)) continue;
            const size_t count = mesh.indexData.size()/(mesh.indexFormat == IndexFormat::UInt16 ? 2 : 4);
            const size_t vertices = mesh.vertexData.size()/mesh.vertexStride;
            for(size_t triangle = 0; triangle+2 < count; triangle += 3) {
                auto a = Index(mesh,triangle), b = Index(mesh,triangle+1), c = Index(mesh,triangle+2);
                if(a >= vertices || b >= vertices || c >= vertices) continue;
                auto hit = HitTriangle(origin,ray,Vertex(mesh,a),Vertex(mesh,b),Vertex(mesh,c));
                if(hit && *hit < closest) {
                    closest = *hit; picked = {static_cast<uint32_t>(object.sourceObject.value), static_cast<uint32_t>(object.sourceObject.value >> 32)};
                }
            }
        }
    }
    properties_.Commit();
    auto* level = session_.GetLevel(); auto entity = level && level->GetScene() ? level->GetScene()->GetEntity(picked) : Entity{};
    if(auto* persistent = entity.GetComponent<PersistentIdComponent>()) { session_.Selection().Select(persistent->value,toggle); return true; }
    if(!toggle) session_.Selection().Clear(); return false;
}
void ViewportTools::DrawToolbar(const LocalizationService& text) {
    static constexpr const char* keys[]{"viewport.translate","viewport.rotate","viewport.scale"};
    for(int i = 0; i < 3; ++i) {
        if(i) ImGui::SameLine();
        if(ImGui::RadioButton(text.Label(keys[i],keys[i]).c_str(),mode_ == i)) { properties_.Commit(); mode_ = i; }
    }
    ImGui::TextDisabled("%s",text.Text("viewport.controls").c_str());
}
void ViewportTools::FocusSelection() {
    if(session_.IsPlaying()) return;
    auto entity = session_.GetSelectedEntity();
    auto* transform = entity.GetComponent<TransformComponent>();
    if(!transform) return;
    Vector3 pivot = Point({},transform->worldMatrix); float radius = 1;
    if(auto* primitive = entity.GetComponent<PrimitiveRenderComponent>(); primitive && model_ && pose_) {
        const auto& nodes = pose_->GetNodeWorldTransforms();
        const size_t node = primitive->nodeIndex;
        if(node < model_->nodes.size() && node < nodes.size() && !model_->nodes[node].meshIndices.empty()) {
            const auto mesh = model_->nodes[node].meshIndices.front();
            if(mesh < bounds_.size() && bounds_[mesh].valid) {
                const auto world = Multiply(nodes[node],transform->worldMatrix);
                pivot = Point((bounds_[mesh].low+bounds_[mesh].high)*0.5f,world);
                radius = (Point(bounds_[mesh].high,world)-Point(bounds_[mesh].low,world)).Length()*0.5f;
            }
        }
    }
    const auto camera = MakeAffineMatrix(Vector3::One(),Quaternion::FromEulerRadians({pitch_,yaw_,0}),{});
    position_ = pivot-Point({0,0,1},camera,0)*std::max(radius*2.5f,2.0f);
}
void ViewportTools::DrawOverlay(float x, float y, float width, float height) {
    if(session_.IsPlaying() || !frame_ || !frame_->camera) return;
    const auto& io = ImGui::GetIO(); const bool hovered = ImGui::IsItemHovered();
    if(hovered && ImGui::IsMouseClicked(1)) { navigating_ = true; ImGui::SetWindowFocus(); }
    if(!ImGui::IsMouseDown(1) || !ImGui::IsWindowFocused() || io.AppFocusLost) navigating_ = false;
    if(navigating_ && !io.WantTextInput) {
        yaw_ += io.MouseDelta.x*0.004f; pitch_ = std::clamp(pitch_+io.MouseDelta.y*0.004f,-1.55f,1.55f);
        const auto camera = MakeAffineMatrix(Vector3::One(),Quaternion::FromEulerRadians({pitch_,yaw_,0}),{});
        const auto right = Point({1,0,0},camera,0), forward = Point({0,0,1},camera,0);
        const float distance = std::min(io.DeltaTime,0.1f)*(io.KeyShift ? 50.0f : 10.0f);
        if(ImGui::IsKeyDown(ImGuiKey_W)) position_ = position_+forward*distance;
        if(ImGui::IsKeyDown(ImGuiKey_S)) position_ = position_-forward*distance;
        if(ImGui::IsKeyDown(ImGuiKey_D)) position_ = position_+right*distance;
        if(ImGui::IsKeyDown(ImGuiKey_A)) position_ = position_-right*distance;
        if(ImGui::IsKeyDown(ImGuiKey_E)) position_.y += distance;
        if(ImGui::IsKeyDown(ImGuiKey_Q)) position_.y -= distance;
    }
    if(hovered && io.MouseWheel != 0) {
        const auto camera = MakeAffineMatrix(Vector3::One(),Quaternion::FromEulerRadians({pitch_,yaw_,0}),{});
        position_ = position_+Point({0,0,1},camera,0)*io.MouseWheel*2;
    }
    if(ImGui::IsWindowFocused() && !io.WantTextInput && !navigating_ && !ImGui::IsAnyItemActive()) {
        if(ImGui::IsKeyPressed(ImGuiKey_W,false)) mode_ = 0;
        if(ImGui::IsKeyPressed(ImGuiKey_E,false)) mode_ = 1;
        if(ImGui::IsKeyPressed(ImGuiKey_R,false)) mode_ = 2;
    }
    bool gizmoHovered = false;
    const auto cursor = ImGui::GetCursorScreenPos();
    auto entity = session_.GetSelectedEntity();
    auto* transform = entity.GetComponent<TransformComponent>();
    if(transform) {
        auto pivot = Point({},transform->worldMatrix);
        Matrix4x4 parentWorld;
        if(auto* hierarchy = entity.GetComponent<HierarchyComponent>())
            if(auto* parent = entity.GetScene()->GetComponent<TransformComponent>(hierarchy->parent)) parentWorld = parent->worldMatrix;
        const float length = std::max((pivot-position_).Length()*0.12f,0.1f);
        const auto project = [&](Vector3 p) -> std::optional<ImVec2> {
            const auto& matrix = frame_->camera->viewProjectionMatrix;
            const float w = p.x*matrix.At(0,3)+p.y*matrix.At(1,3)+p.z*matrix.At(2,3)+matrix.At(3,3);
            if(w <= 0) return {};
            auto ndc = Unproject(p,matrix);
            if(!ndc || ndc->z < 0 || ndc->z > 1) return {};
            return ImVec2{x+(ndc->x+1)*0.5f*width,y+(1-ndc->y)*0.5f*height};
        };
        if(auto center = project(pivot)) {
            const Vector3 axes[]{{1,0,0},{0,1,0},{0,0,1}};
            const ImU32 colors[]{IM_COL32(240,90,90,255),IM_COL32(90,220,130,255),IM_COL32(90,150,250,255)};
            for(int axis = 0; axis < 3; ++axis) {
                auto end = project(pivot+Point(axes[axis],parentWorld,0)*length);
                if(!end || end->x < x+10 || end->y < y+10 || end->x > x+width-10 || end->y > y+height-10) continue;
                ImGui::GetWindowDrawList()->AddLine(*center,*end,colors[axis],3);
                ImGui::GetWindowDrawList()->AddCircleFilled(*end,6,colors[axis]);
                const auto selectedId = session_.GetSelection();
                const auto stableId = std::to_string(selectedId.high) + ':' + std::to_string(selectedId.low);
                ImGui::PushID(stableId.c_str()); ImGui::PushID(axis); ImGui::SetCursorScreenPos({end->x-9,end->y-9});
                ImGui::InvisibleButton("axis",{18,18}); gizmoHovered |= ImGui::IsItemHovered();
                if(ImGui::IsItemActivated()) {
                    dragHandles_.clear(); dragValues_.clear();
                    for(auto id : session_.Selection().All()) {
                        PropertyHandle handle{id,"Transform",mode_ == 0 ? "translation" : mode_ == 1 ? "rotation" : "scale"};
                        if(auto value = properties_.Read(handle)) { dragHandles_.push_back(handle); dragValues_.push_back(*value); }
                    }
                    properties_.Begin(dragHandles_);
                }
                if(ImGui::IsItemActive() && !dragValues_.empty()) {
                    if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {
                        properties_.Cancel(); dragValues_.clear(); dragHandles_.clear(); ImGui::PopID(); ImGui::PopID(); continue;
                    }
                    const auto delta = ImGui::GetMouseDragDelta(0);
                    const float sx = end->x-center->x, sy = end->y-center->y, squared = sx*sx+sy*sy;
                    const float amount = squared > 4 ? (delta.x*sx+delta.y*sy)/squared : delta.x*0.01f;
                    auto values = dragValues_;
                    for(auto& value : values) {
                        if(mode_ == 1) {
                            const auto& v = std::get<std::array<float,4>>(value);
                            const float rotation = io.KeyCtrl ? std::round(amount/0.2617993878f)*0.2617993878f : amount;
                            const auto q = Quaternion::Multiply(Quaternion{v[0],v[1],v[2],v[3]},Quaternion::FromEulerRadians(axes[axis]*rotation));
                            value = std::array<float,4>{q.x,q.y,q.z,q.w};
                        } else {
                            auto v = std::get<std::array<float,3>>(value);
                            v[axis] += amount*(mode_ == 0 ? length : 1.0f);
                            if(io.KeyCtrl) { const float step = mode_ == 0 ? 0.5f : 0.1f; v[axis] = std::round(v[axis]/step)*step; }
                            value = v;
                        }
                    }
                    properties_.PreviewValues(dragHandles_,values);
                }
                if(ImGui::IsItemDeactivated()) { properties_.Commit(); dragValues_.clear(); dragHandles_.clear(); }
                ImGui::PopID(); ImGui::PopID();
            }
        }
    }
    // Overlay後に内容範囲を延長せず通常layoutへ戻す。SetCursorだけではImGuiの検証が失敗する。
    ImGui::SetCursorScreenPos({cursor.x,cursor.y-ImGui::GetStyle().ItemSpacing.y});
    ImGui::Dummy({0,0});
    if(hovered && !gizmoHovered && !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0))
        Pick((io.MousePos.x-x)/width,(io.MousePos.y-y)/height,io.KeyCtrl);
}
}
