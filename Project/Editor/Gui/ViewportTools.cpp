#include "ViewportTools.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
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
    if(session_.IsPlaying() || IsManipulating() || gizmoOwnsMouse_ || blockedUntilRelease_ || !frame_ || !frame_->camera || !model_ || !pose_ ||
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
        if(node < object.nodeBegin || node >= object.nodeEnd) continue;
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
    ImGui::BeginDisabled(IsManipulating());
    static constexpr const char* keys[]{"viewport.translate","viewport.rotate","viewport.scale"};
    for(int i = 0; i < 3; ++i) {
        if(i) ImGui::SameLine();
        if(ImGui::RadioButton(text.Label(keys[i],keys[i]).c_str(),mode_ == i)) { properties_.Commit(); mode_ = i; }
    }
    ImGui::TextDisabled("%s",text.Text("viewport.controls").c_str());
    ImGui::EndDisabled();
    if(gizmoRejected_) ImGui::TextWrapped("%s", text.Text("viewport.gizmoRejected").c_str());
}
std::optional<ViewportTools::Bounds> ViewportTools::GetEntityLocalBounds(Entity entity) const {
    if(frame_ && model_ && pose_ && entity.IsAlive()) {
        const auto& nodes = pose_->GetNodeWorldTransforms();
        const auto local = entity.GetId();
        const uint64_t packed = (static_cast<uint64_t>(local.generation)<<32)|local.index;
        Vector3 low{1e30f,1e30f,1e30f}, high{-1e30f,-1e30f,-1e30f}; bool found = false;
        for(const auto& item : frame_->primitives) if(item.sourceObject.value == packed) {
            for(size_t node = item.nodeBegin; node < std::min(model_->nodes.size(), static_cast<size_t>(item.nodeEnd)) && node < nodes.size(); ++node) {
                if(item.nodeIndex != PrimitiveRenderItem::kAllNodes && item.nodeIndex != node) continue;
                for(auto mesh : model_->nodes[node].meshIndices) if(mesh < bounds_.size() && bounds_[mesh].valid) {
                    // 通常描画と同じNode変換。Skin MeshはEntity空間の頂点を使用する。
                    const auto nodeTransform = model_->meshes[mesh].skinIndex == MeshAssetData::kNoSkin ? nodes[node] : Matrix4x4::Identity();
                    const auto& bound = bounds_[mesh];
                    for(int corner = 0; corner < 8; ++corner) {
                        auto point = Point({corner&1 ? bound.high.x : bound.low.x, corner&2 ? bound.high.y : bound.low.y, corner&4 ? bound.high.z : bound.low.z}, nodeTransform);
                        low={std::min(low.x,point.x),std::min(low.y,point.y),std::min(low.z,point.z)};
                        high={std::max(high.x,point.x),std::max(high.y,point.y),std::max(high.z,point.z)}; found=true;
                    }
                }
            }
        }
        if(found) return Bounds{low,high,true};
    }
    return {};
}
std::optional<Matrix4x4> ViewportTools::GetSelectionPivot() const {
    auto entity = session_.GetSelectedEntity();
    const auto* transform = entity.GetComponent<TransformComponent>();
    if(!transform) return {};
    auto pivot = transform->worldMatrix;
    if(auto bounds = GetEntityLocalBounds(entity)) {
        // T(centerLocal) * world。回転/拡縮時も同じ形状中心を保持し、原点周りの公転を防ぐ。
        const auto center = Point((bounds->low+bounds->high)*.5f,transform->worldMatrix);
        pivot.At(3,0)=center.x; pivot.At(3,1)=center.y; pivot.At(3,2)=center.z;
    }
    return pivot;
}
void ViewportTools::FocusSelection() {
    if(session_.IsPlaying()) return;
    auto pivotMatrix = GetSelectionPivot();
    if(!pivotMatrix) return;
    const auto pivot = Point({},*pivotMatrix);
    float radius = 1;
    auto entity = session_.GetSelectedEntity();
    if(auto bounds = GetEntityLocalBounds(entity)) {
        const auto& world = entity.GetComponent<TransformComponent>()->worldMatrix;
        radius = 0;
        for(int corner=0;corner<8;++corner) {
            auto point = Point({corner&1 ? bounds->high.x : bounds->low.x, corner&2 ? bounds->high.y : bounds->low.y, corner&4 ? bounds->high.z : bounds->low.z},world);
            radius = std::max(radius,(point-pivot).Length());
        }
    }
    const auto camera = MakeAffineMatrix(Vector3::One(),Quaternion::FromEulerRadians({pitch_,yaw_,0}),{});
    position_ = pivot-Point({0,0,1},camera,0)*std::max(radius*2.5f,2.0f);
}
void ViewportTools::DrawOverlay(float x, float y, float width, float height) {
    if(session_.IsPlaying() || !frame_ || !frame_->camera) return;
    const auto& io = ImGui::GetIO(); const bool hovered = ImGui::IsItemHovered();
    if(hovered && !IsManipulating() && ImGui::IsMouseClicked(1)) { navigating_ = true; ImGui::SetWindowFocus(); }
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
    if(hovered && !IsManipulating() && io.MouseWheel != 0) {
        const auto camera = MakeAffineMatrix(Vector3::One(),Quaternion::FromEulerRadians({pitch_,yaw_,0}),{});
        position_ = position_+Point({0,0,1},camera,0)*io.MouseWheel*2;
    }
    if(ImGui::IsWindowFocused() && !io.WantTextInput && !navigating_ && !IsManipulating() && !ImGui::IsAnyItemActive()) {
        if(ImGui::IsKeyPressed(ImGuiKey_W,false)) mode_ = 0;
        if(ImGui::IsKeyPressed(ImGuiKey_E,false)) mode_ = 1;
        if(ImGui::IsKeyPressed(ImGuiKey_R,false)) mode_ = 2;
    }
    if(IsManipulating() && (!gizmo_.IsCurrent() || ImGui::IsKeyPressed(ImGuiKey_Escape,false))) {
        gizmo_.Cancel(); blockedUntilRelease_ = ImGui::IsMouseDown(0);
        ImGuizmo::PushID("NexusViewport"); ImGuizmo::Enable(false); ImGuizmo::PopID();
    }
    if(!ImGui::IsMouseDown(0)) blockedUntilRelease_ = false;
    bool gizmoHovered = false;
    if(auto pivot = GetSelectionPivot()) {
        ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(x,y,width,height);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::PushID("NexusViewport");
        ImGuizmo::Enable(!navigating_ && !blockedUntilRelease_ && !io.AppFocusLost);
        auto edited = *pivot;
        const auto before = edited;
        const ImGuizmo::OPERATION operations[]{ImGuizmo::TRANSLATE,ImGuizmo::ROTATE,ImGuizmo::SCALE};
        const float step = mode_ == 0 ? .5f : mode_ == 1 ? 15.0f : .1f;
        const float snap[]{step,step,step};
        // Engineはrow-vector/row-major。ImGuizmoのfloat[16]と同じ配置なので転置しない。
        const bool changed = ImGuizmo::Manipulate(frame_->camera->viewMatrix.elements.data(),
            frame_->camera->projectionMatrix.elements.data(),operations[mode_],ImGuizmo::LOCAL,
            edited.elements.data(),nullptr,io.KeyCtrl ? snap : nullptr);
        const bool usingGizmo = ImGuizmo::IsUsing();
        // IsOver()は前Frameのhoverを返す。今Frameのハンドルを使い、瞬間的なclickも消費する。
        gizmoHovered = ImGuizmo::GetHoveredHandleType() != ImGuizmo::MT_NONE || usingGizmo;
        if(usingGizmo || (gizmoHovered && ImGui::IsMouseClicked(0))) gizmoOwnsMouse_ = true;
        if(usingGizmo && !IsManipulating()) gizmo_.Begin(before,mode_ == 0);
        if(changed && IsManipulating()) gizmoRejected_ = !gizmo_.Preview(edited);
        if(IsManipulating() && !usingGizmo) gizmo_.Commit();
        ImGuizmo::PopID();
    }
    const bool gizmoOwnedThisFrame = gizmoOwnsMouse_;
    CompleteInteraction();
    if(hovered && !gizmoHovered && !gizmoOwnedThisFrame && !IsManipulating() && !blockedUntilRelease_ && !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0))
        Pick((io.MousePos.x-x)/width,(io.MousePos.y-y)/height,io.KeyCtrl);
}
void ViewportTools::CompleteInteraction() {
    if(!ImGui::IsMouseDown(0)) gizmoOwnsMouse_ = false;
    if(!IsManipulating()) return;
    const bool canceled = !gizmo_.IsCurrent() || ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape,false);
    if(canceled) { gizmo_.Cancel(); blockedUntilRelease_ = ImGui::IsMouseDown(0); }
    else if(!ImGui::IsMouseDown(0)) gizmo_.Commit();
    else return;
    ImGuizmo::PushID("NexusViewport"); ImGuizmo::Enable(false); ImGuizmo::PopID();
}
}
