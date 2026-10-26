#include "ViewportTools.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace NexusEngine {
ReflectionValue ViewportTools::CaptureForReinstancing() {
    gizmo_.Commit();
    ImGuizmo::Enable(false); ImGuizmo::Enable(true);
    return ReflectionValue(ReflectionValue::Object{
        {"camera", CaptureReflection(std::array<float, 5>{position_.x, position_.y, position_.z, yaw_, pitch_})},
        {"viewSettings", CaptureReflection(std::array<float, 2>{cameraSpeed_, fieldOfView_})},
        {"snapSteps", CaptureReflection(snapSteps_)},
        {"localSpace", CaptureReflection(localSpace_)}, {"snapping", CaptureReflection(snapping_)},
        {"guides", CaptureReflection(guidesVisible_)}, {"diagnostics", CaptureReflection(showDiagnostics_)},
        {"previewSize", CaptureReflection(previewSize_)},
        {"mode", CaptureReflection(mode_)}, {"gizmo", CaptureReflection(gizmoVisible_)}});
}
void ViewportTools::RestoreAfterReinstancing(const ReflectionValue& snapshot) {
    std::array<float, 5> camera{}; RestoreReflection(snapshot.At("camera"), camera);
    position_ = {camera[0], camera[1], camera[2]}; yaw_ = camera[3]; pitch_ = camera[4];
    RestoreReflection(snapshot.At("mode"), mode_); RestoreReflection(snapshot.At("gizmo"), gizmoVisible_);
    // 古いコードで取得したスナップショットからも復元できるよう、新規項目は存在を確認する。
    const auto& fields = std::get<ReflectionValue::Object>(snapshot.data);
    if(fields.contains("viewSettings")) {
        std::array<float, 2> settings{}; RestoreReflection(snapshot.At("viewSettings"), settings);
        cameraSpeed_ = settings[0]; fieldOfView_ = settings[1];
        RestoreReflection(snapshot.At("snapSteps"), snapSteps_);
        RestoreReflection(snapshot.At("localSpace"), localSpace_); RestoreReflection(snapshot.At("snapping"), snapping_);
        RestoreReflection(snapshot.At("guides"), guidesVisible_); RestoreReflection(snapshot.At("diagnostics"), showDiagnostics_);
        RestoreReflection(snapshot.At("previewSize"), previewSize_);
    }
    generation_ = session_.Generation();
}
namespace {
constexpr float kIconRadius = 14;
struct ClipPoint { float x, y, z, w; };
ClipPoint ToClip(Vector3 p, const Matrix4x4& m) {
    return {p.x*m.At(0,0)+p.y*m.At(1,0)+p.z*m.At(2,0)+m.At(3,0),
        p.x*m.At(0,1)+p.y*m.At(1,1)+p.z*m.At(2,1)+m.At(3,1),
        p.x*m.At(0,2)+p.y*m.At(1,2)+p.z*m.At(2,2)+m.At(3,2),
        p.x*m.At(0,3)+p.y*m.At(1,3)+p.z*m.At(2,3)+m.At(3,3)};
}
bool Finite(ClipPoint p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.w); }
ImVec2 Screen(ClipPoint p, float x, float y, float width, float height) {
    return {x+(p.x/p.w+1)*width*.5f, y+(1-p.y/p.w)*height*.5f};
}
// Clip in homogeneous D3D space, including segments crossing the eye/near plane.
void GuideLine(ImDrawList& draw, Vector3 a, Vector3 b, const Matrix4x4& vp,
    float x, float y, float width, float height, ImU32 color) {
    auto ca = ToClip(a,vp), cb = ToClip(b,vp);
    if(!Finite(ca) || !Finite(cb)) return;
    const float da[]{ca.x+ca.w,ca.w-ca.x,ca.y+ca.w,ca.w-ca.y,ca.z,ca.w-ca.z,ca.w-1e-5f};
    const float db[]{cb.x+cb.w,cb.w-cb.x,cb.y+cb.w,cb.w-cb.y,cb.z,cb.w-cb.z,cb.w-1e-5f};
    float low = 0, high = 1;
    for(size_t i=0;i<7;++i) {
        if(da[i]<0 && db[i]<0) return;
        if(da[i]<0) low=std::max(low,da[i]/(da[i]-db[i]));
        if(db[i]<0) high=std::min(high,da[i]/(da[i]-db[i]));
    }
    if(low>high) return;
    const auto at=[&](float t) { return ClipPoint{ca.x+(cb.x-ca.x)*t,ca.y+(cb.y-ca.y)*t,
        ca.z+(cb.z-ca.z)*t,ca.w+(cb.w-ca.w)*t}; };
    draw.AddLine(Screen(at(low),x,y,width,height),Screen(at(high),x,y,width,height),color,1.5f);
}
struct SceneIcon { Entity entity; ClipPoint clip; bool camera, enabled; };
std::vector<SceneIcon> SceneIcons(Scene& scene, const Matrix4x4& vp) {
    std::vector<SceneIcon> icons;
    scene.ForEach<TransformComponent>([&](Entity entity, TransformComponent& transform) {
        const auto* camera=entity.GetComponent<CameraComponent>();
        const auto* light=entity.GetComponent<DirectionalLightComponent>();
        if(!camera && !light) return;
        const auto clip=ToClip({transform.worldMatrix.At(3,0),transform.worldMatrix.At(3,1),transform.worldMatrix.At(3,2)},vp);
        if(!Finite(clip) || clip.w<=1e-5f || clip.z<0 || clip.z>clip.w ||
            std::abs(clip.x)>clip.w || std::abs(clip.y)>clip.w) return;
        icons.push_back({entity,clip,camera!=nullptr,camera ? camera->enabled : light->enabled});
    });
    std::stable_sort(icons.begin(),icons.end(),[](const auto& a,const auto& b) { return a.clip.z/a.clip.w>b.clip.z/b.clip.w; });
    return icons;
}
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
    PrepareCameraGuides(aspect);
    if(generation_ != session_.Generation()) {
        generation_ = session_.Generation(); position_ = {0,2,-10}; yaw_ = pitch_ = 0;
        // 編集開始時の視点は既存の主カメラへ合わせる。複数配置でも編集は継続できる。
        if(!frame.camera) if(auto* level = session_.GetLevel(); level && level->GetScene()) {
            bool found = false;
            level->GetScene()->ForEach<TransformComponent, CameraComponent>([&](Entity, TransformComponent& transform, CameraComponent& camera) {
                if(found || !camera.enabled || !camera.primary) return;
                const auto forward = Point({0,0,1}, transform.worldMatrix, 0).Normalized();
                if(!TryInverse(transform.worldMatrix) || !std::isfinite(forward.y)) return;
                position_ = Point({}, transform.worldMatrix);
                yaw_ = std::atan2(forward.x,forward.z); pitch_ = -std::asin(std::clamp(forward.y,-1.0f,1.0f));
                found = true;
            });
        }
        if(frame.camera) if(auto world = TryInverse(frame.camera->viewMatrix)) {
            position_ = Point({},*world); const auto forward = Point({0,0,1},*world,0).Normalized();
            yaw_ = std::atan2(forward.x,forward.z); pitch_ = -std::asin(std::clamp(forward.y,-1.0f,1.0f));
        }
    }
    const auto world = MakeAffineMatrix(Vector3::One(), Quaternion::FromEulerRadians({pitch_,yaw_,0}), position_);
    const auto view = TryInverse(world);
    if(!view || aspect <= 0) return;
    const auto projection = MakePerspectiveFovMatrix(fieldOfView_*0.01745329252f,aspect,0.05f,100000.0f);
    frame.camera = RenderCamera{{}, *view, projection, Multiply(*view,projection)};
}
bool ViewportTools::Pick(float x, float y, bool toggle) {
    if(session_.IsPlaying() || IsManipulating() || gizmoOwnsMouse_ || blockedUntilRelease_ || !frame_ || !frame_->camera ||
        !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1) return false;
    auto* level = session_.GetLevel();
    if(guidesVisible_ && level && level->GetScene() && viewportWidth_>0 && viewportHeight_>0) {
        auto icons=SceneIcons(*level->GetScene(),frame_->camera->viewProjectionMatrix);
        // Icons are overlays; the frontmost visible icon wins before mesh picking.
        for(auto it=icons.rbegin();it!=icons.rend();++it) {
            const auto p=Screen(it->clip,0,0,viewportWidth_,viewportHeight_);
            const float dx=x*viewportWidth_-p.x, dy=y*viewportHeight_-p.y;
            if(dx*dx+dy*dy>kIconRadius*kIconRadius) continue;
            if(auto* id=it->entity.GetComponent<PersistentIdComponent>()) {
                const auto value=id->value;
                properties_.Commit(); session_.Selection().Select(value,toggle); return true;
            }
        }
    }
    if(!model_ || !pose_) {
        properties_.Commit(); if(!toggle) session_.Selection().Clear(); return false;
    }
    auto inverse = TryInverse(frame_->camera->viewProjectionMatrix);
    if(!inverse) return false;
    auto near = Unproject({2*x-1,1-2*y,0},*inverse), far = Unproject({2*x-1,1-2*y,1},*inverse);
    if(!near || !far) return false;
    const auto direction = (*far-*near).Normalized();
    float closest = std::numeric_limits<float>::max(); EntityId picked;
    const auto& transforms = pose_->GetNodeWorldTransforms();
    std::vector<std::vector<Vector3>> deformed(model_->meshes.size());
    std::vector<Bounds> deformedBounds(model_->meshes.size());
    for(const auto& object : frame_->primitives) for(size_t node = 0; node < model_->nodes.size() && node < transforms.size(); ++node) {
        if(object.nodeIndex != PrimitiveRenderItem::kAllNodes && object.nodeIndex != node) continue;
        if(node < object.nodeBegin || node >= object.nodeEnd) continue;
        for(auto meshIndex : model_->nodes[node].meshIndices) {
            if(meshIndex >= bounds_.size() || !bounds_[meshIndex].valid) continue;
            const auto& mesh = model_->meshes[meshIndex];
            const bool skinned = mesh.skinIndex != MeshAssetData::kNoSkin;
            if(skinned && deformed[meshIndex].empty()) {
                const auto* palette = pose_->GetSkinPalette(mesh.skinIndex);
                if(!palette || mesh.vertexStride != sizeof(ModelVertex)) continue;
                auto& positions = deformed[meshIndex];
                auto& bound = deformedBounds[meshIndex];
                for(size_t vertex = 0; vertex < mesh.vertexData.size()/mesh.vertexStride; ++vertex) {
                    ModelVertex input;
                    std::memcpy(&input, mesh.vertexData.data()+vertex*mesh.vertexStride, sizeof(input));
                    Vector3 position{};
                    for(size_t joint = 0; joint < 4; ++joint) {
                        const float index = input.jointIndices[joint];
                        if(!std::isfinite(index) || index < 0 || index >= static_cast<float>(palette->size())) continue;
                        position = position + Point({input.position[0],input.position[1],input.position[2]},
                            (*palette)[static_cast<size_t>(index)]) * input.jointWeights[joint];
                    }
                    positions.push_back(position);
                    if(!bound.valid) { bound.low = bound.high = position; bound.valid = true; }
                    else {
                        bound.low = {std::min(bound.low.x,position.x),std::min(bound.low.y,position.y),std::min(bound.low.z,position.z)};
                        bound.high = {std::max(bound.high.x,position.x),std::max(bound.high.y,position.y),std::max(bound.high.z,position.z)};
                    }
                }
            }
            auto local = TryInverse(skinned ? object.worldMatrix : Multiply(transforms[node],object.worldMatrix));
            if(!local) continue;
            const auto origin = Point(*near,*local), ray = Point(direction,*local,0);
            const auto& bound = skinned ? deformedBounds[meshIndex] : bounds_[meshIndex];
            if(!HitBox(origin,ray,bound.low,bound.high,closest)) continue;
            const size_t count = mesh.indexData.size()/(mesh.indexFormat == IndexFormat::UInt16 ? 2 : 4);
            const size_t vertices = mesh.vertexData.size()/mesh.vertexStride;
            const auto vertex = [&](uint32_t index) { return skinned ? deformed[meshIndex][index] : Vertex(mesh,index); };
            for(const auto& sub : mesh.submeshes) {
                if(sub.firstIndex > count || sub.indexCount > count-sub.firstIndex) continue;
                const size_t end = static_cast<size_t>(sub.firstIndex)+sub.indexCount;
                for(size_t triangle = sub.firstIndex; triangle+2 < end; triangle += 3) {
                    const int64_t a = static_cast<int64_t>(Index(mesh,triangle))+sub.vertexOffset;
                    const int64_t b = static_cast<int64_t>(Index(mesh,triangle+1))+sub.vertexOffset;
                    const int64_t c = static_cast<int64_t>(Index(mesh,triangle+2))+sub.vertexOffset;
                    if(a < 0 || b < 0 || c < 0 || static_cast<uint64_t>(a) >= vertices ||
                        static_cast<uint64_t>(b) >= vertices || static_cast<uint64_t>(c) >= vertices) continue;
                    auto hit = HitTriangle(origin,ray,vertex(static_cast<uint32_t>(a)),vertex(static_cast<uint32_t>(b)),vertex(static_cast<uint32_t>(c)));
                    if(hit && *hit < closest) {
                        closest = *hit; picked = {static_cast<uint32_t>(object.sourceObject.value), static_cast<uint32_t>(object.sourceObject.value >> 32)};
                    }
                }
            }
        }
    }
    properties_.Commit();
    auto entity = level && level->GetScene() ? level->GetScene()->GetEntity(picked) : Entity{};
    if(auto* persistent = entity.GetComponent<PersistentIdComponent>()) { session_.Selection().Select(persistent->value,toggle); return true; }
    if(!toggle) session_.Selection().Clear(); return false;
}
void ViewportTools::DrawToolbar(const LocalizationService& text) {
    ImGui::BeginDisabled(IsManipulating());
    const float toolbarRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    static constexpr const char* keys[]{"viewport.translate","viewport.rotate","viewport.scale"};
    static constexpr const char* labels[]{"W", "E", "R"};
    for(int i = 0; i < 3; ++i) {
        if(i) ImGui::SameLine();
        const bool active = mode_ == i;
        if(active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        if(ImGui::Button(labels[i], {ImGui::GetFrameHeight()*1.25f, 0})) { properties_.Commit(); mode_ = i; }
        if(active) ImGui::PopStyleColor();
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text.Text(keys[i]).c_str());
    }
    const auto next = [&](const std::string& label) {
        const float spacing = ImGui::GetStyle().ItemSpacing.x*2;
        const float buttonWidth = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x*2;
        if(ImGui::GetItemRectMax().x + spacing + buttonWidth <= toolbarRight) ImGui::SameLine(0, spacing);
    };
    next(text.Text(localSpace_ ? "viewport.local" : "viewport.world"));
    if(ImGui::Button(text.Text(localSpace_ ? "viewport.local" : "viewport.world").c_str())) localSpace_ = !localSpace_;
    next(text.Text("viewport.snap"));
    if(ImGui::Button(text.Text("viewport.snap").c_str())) ImGui::OpenPopup("ViewportSnap");
    if(ImGui::BeginPopup("ViewportSnap")) {
        ImGui::Checkbox(text.Text("viewport.snapEnabled").c_str(), &snapping_);
        for(int i=0;i<3;++i) {
            ImGui::SetNextItemWidth(ImGui::GetFontSize()*8);
            ImGui::DragFloat(text.Text(keys[i]).c_str(), &snapSteps_[i], i==1 ? 1.0f : .05f, .001f, i==1 ? 180.0f : 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            snapSteps_[i] = std::clamp(snapSteps_[i], .001f, i==1 ? 180.0f : 1000.0f);
        }
        ImGui::EndPopup();
    }
    next(text.Text("viewport.cameraTools"));
    if(ImGui::Button(text.Text("viewport.cameraTools").c_str())) ImGui::OpenPopup("ViewportCamera");
    if(ImGui::BeginPopup("ViewportCamera")) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize()*12);
        ImGui::SliderFloat(text.Text("viewport.cameraSpeed").c_str(), &cameraSpeed_, .1f, 100, "%.1f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(ImGui::GetFontSize()*12);
        ImGui::SliderFloat(text.Text("viewport.fov").c_str(), &fieldOfView_, 10, 150, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        if(ImGui::MenuItem(text.Text("viewport.focus").c_str(), "F")) FocusSelection();
        ImGui::EndPopup();
    }
    next(text.Text("viewport.show"));
    if(ImGui::Button(text.Text("viewport.show").c_str())) ImGui::OpenPopup("ViewportShow");
    if(ImGui::BeginPopup("ViewportShow")) {
        ImGui::Checkbox(text.Text("settings.gizmo").c_str(), &gizmoVisible_);
        ImGui::Checkbox(text.Text("viewport.guides").c_str(), &guidesVisible_);
        ImGui::Checkbox(text.Text("viewport.diagnostics").c_str(), &showDiagnostics_);
        ImGui::SeparatorText(text.Text("viewport.previewSize").c_str());
        static constexpr const char* sizeKeys[]{"viewport.small", "viewport.medium", "viewport.large"};
        for(int i=0;i<3;++i) if(ImGui::RadioButton(text.Text(sizeKeys[i]).c_str(), previewSize_ == i)) previewSize_ = i;
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    if(gizmoRejected_) session_.Diagnostics().Set("gizmo", text.Text("viewport.gizmoRejected"));
    else session_.Diagnostics().Resolve("gizmo");
    ImGui::Separator();
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
void ViewportTools::PrepareCameraGuides(float aspect) {
    cameraGuides_.clear();
    auto* level=session_.GetLevel();
    if(!level || !level->GetScene() || !std::isfinite(aspect) || aspect<=0) return;
    // Capture the scene camera volume before installing the editor camera. These world
    // vertices must never be rebuilt from the observer's matrices or GUI viewport size.
    level->GetScene()->ForEach<TransformComponent,CameraComponent>([&](Entity entity,TransformComponent& transform,CameraComponent& camera) {
        if(!std::isfinite(camera.verticalFieldOfViewRadians) || camera.verticalFieldOfViewRadians<=0 ||
            camera.verticalFieldOfViewRadians>=3.14159265f || !std::isfinite(camera.nearClip) ||
            !std::isfinite(camera.farClip) || camera.nearClip<=0 || camera.farClip<=camera.nearClip) return;
        CameraGuide guide{entity.GetId(),transform.worldMatrix,{}};
        guide.symbolScale=camera.smallFrustum ? 1.0f/7.0f : 1.0f;
        const float tangent=std::tan(camera.verticalFieldOfViewRadians*.5f);
        // Keep the frustum as a compact editor symbol, preserving FOV and clip ratios.
        // This changes only the guide geometry, never the runtime camera's clipping.
        constexpr float kMaximumGuideDepth=3.0f;
        const float guideScale=std::min(1.0f,kMaximumGuideDepth/camera.farClip)*guide.symbolScale;
        for(int plane=0;plane<2;++plane) {
            const float depth=(plane ? camera.farClip : camera.nearClip)*guideScale;
            const float h=tangent*depth, w=h*aspect;
            for(int corner=0;corner<4;++corner)
                guide.corners[plane*4+corner]=Point({corner==0 || corner==3 ? -w : w,corner<2 ? -h : h,depth},guide.world);
        }
        cameraGuides_.push_back(guide);
    });
}
void ViewportTools::DrawSceneGuides(float x, float y, float width, float height) {
    auto* level=session_.GetLevel();
    if(!level || !level->GetScene()) return;
    auto& scene=*level->GetScene();
    auto& draw=*ImGui::GetWindowDrawList();
    const auto& vp=frame_->camera->viewProjectionMatrix;
    draw.PushClipRect({x,y},{x+width,y+height},true);
    const auto selected=[&](Entity entity) {
        const auto* id=entity.GetComponent<PersistentIdComponent>();
        return id && session_.Selection().Contains(id->value);
    };
    const auto colorFor=[&](Entity entity,bool camera,bool enabled) {
        return selected(entity) ? IM_COL32(255,170,55,255) : !enabled ? IM_COL32(150,150,150,180) :
            camera ? IM_COL32(100,205,255,220) : IM_COL32(255,225,110,220);
    };
    const auto line=[&](Vector3 a,Vector3 b,ImU32 color) { GuideLine(draw,a,b,vp,x,y,width,height,color); };
    for(const auto& guide : cameraGuides_) {
        auto entity=scene.GetEntity(guide.entity);
        const auto* camera=entity.GetComponent<CameraComponent>();
        if(!camera) continue;
        const auto color=colorFor(entity,true,camera->enabled);
        const auto& corners=guide.corners;
        for(int i=0;i<4;++i) {
            line(corners[i],corners[(i+1)%4],color);
            line(corners[i+4],corners[(i+1)%4+4],color);
            line(corners[i],corners[i+4],color);
        }
        // A short camera body/direction marker stays readable with distant far planes.
        const float symbolScale=guide.symbolScale;
        const auto localLine=[&](Vector3 a,Vector3 b) { line(Point(a*symbolScale,guide.world),Point(b*symbolScale,guide.world),color); };
        localLine({-.3f,.2f,0},{.3f,.2f,0}); localLine({.3f,.2f,0},{0,.5f,0}); localLine({0,.5f,0},{-.3f,.2f,0});
        localLine({}, {0,0,1});
    }
    scene.ForEach<TransformComponent,DirectionalLightComponent>([&](Entity entity,TransformComponent& transform,DirectionalLightComponent& light) {
        if(!light.IsValid()) return;
        // The component stores surface-to-light; arrows show photons travelling in the opposite direction.
        const auto direction=Point({light.light.direction[0],light.light.direction[1],light.light.direction[2]},transform.worldMatrix,0).Normalized()*-1;
        if(direction.LengthSquared()<1e-8f) return;
        const auto right=Vector3::Cross(std::abs(direction.y)>.99f ? Vector3::Right() : Vector3::Up(),direction).Normalized();
        const auto up=Vector3::Cross(direction,right);
        const auto origin=Point({},transform.worldMatrix);
        const auto color=colorFor(entity,false,light.enabled);
        for(int i=0;i<5;++i) {
            const auto start=origin+(i==1 ? right*.5f : i==2 ? right*-.5f : i==3 ? up*.5f : i==4 ? up*-.5f : Vector3{});
            const auto end=start+direction*3;
            line(start,end,color); line(end,end-direction*.35f+right*.18f,color); line(end,end-direction*.35f-right*.18f,color);
        }
    });
    auto icons=SceneIcons(scene,vp);
    for(const auto& icon : icons) {
        const auto p=Screen(icon.clip,x,y,width,height);
        const auto color=colorFor(icon.entity,icon.camera,icon.enabled);
        draw.AddCircleFilled(p,kIconRadius,IM_COL32(22,27,34,230),24);
        draw.AddCircle(p,kIconRadius,color,24,selected(icon.entity) ? 2.5f : 1.0f);
        if(icon.camera) {
            draw.AddRect({p.x-8,p.y-5},{p.x+3,p.y+5},color,1.0f,2.0f);
            draw.AddQuad({p.x+3,p.y-2},{p.x+9,p.y-6},{p.x+9,p.y+6},{p.x+3,p.y+2},color,2);
        } else {
            draw.AddCircle(p,4,color,16,2);
            for(int i=0;i<8;++i) {
                const float a=static_cast<float>(i)*.7853981634f;
                draw.AddLine({p.x+std::cos(a)*7,p.y+std::sin(a)*7},{p.x+std::cos(a)*10,p.y+std::sin(a)*10},color,2);
            }
        }
    }
    draw.PopClipRect();
    if(ImGui::IsItemHovered() && !navigating_ && !IsManipulating()) {
        const auto mouse=ImGui::GetIO().MousePos;
        for(auto it=icons.rbegin();it!=icons.rend();++it) {
            const auto p=Screen(it->clip,x,y,width,height);
            if((mouse.x-p.x)*(mouse.x-p.x)+(mouse.y-p.y)*(mouse.y-p.y)>kIconRadius*kIconRadius) continue;
            const auto* name=it->entity.GetComponent<NameComponent>();
            ImGui::SetTooltip("%s (%s)",name ? name->name.c_str() : "",it->camera ? "Camera" : "Directional Light");
            break;
        }
    }
}
void ViewportTools::DrawOverlay(float x, float y, float width, float height, bool inputBlocked) {
    viewportWidth_=width; viewportHeight_=height;
    if(session_.IsPlaying() || !frame_ || !frame_->camera || !std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0) return;
    inputBlocked |= ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const auto& io = ImGui::GetIO(); const bool hovered = !inputBlocked && ImGui::IsItemHovered();
    if(guidesVisible_) DrawSceneGuides(x,y,width,height);
    if(hovered && !IsManipulating() && ImGui::IsMouseClicked(1)) { navigating_ = true; ImGui::SetWindowFocus(); }
    if(!ImGui::IsMouseDown(1) || !ImGui::IsWindowFocused() || io.AppFocusLost || inputBlocked) navigating_ = false;
    if(navigating_ && !io.WantTextInput) {
        yaw_ += io.MouseDelta.x*0.004f; pitch_ = std::clamp(pitch_+io.MouseDelta.y*0.004f,-1.55f,1.55f);
        const auto camera = MakeAffineMatrix(Vector3::One(),Quaternion::FromEulerRadians({pitch_,yaw_,0}),{});
        const auto right = Point({1,0,0},camera,0), forward = Point({0,0,1},camera,0);
        const float distance = std::min(io.DeltaTime,0.1f)*cameraSpeed_*(io.KeyShift ? 5.0f : 1.0f);
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
    if(!inputBlocked && ImGui::IsWindowFocused() && !io.WantTextInput && !navigating_ && !IsManipulating() && !ImGui::IsAnyItemActive()) {
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
    if(auto pivot = GetSelectionPivot(); gizmoVisible_ && pivot) {
        ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(x,y,width,height);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::PushID("NexusViewport");
        ImGuizmo::Enable(!inputBlocked && !navigating_ && !blockedUntilRelease_ && !io.AppFocusLost);
        auto edited = *pivot;
        const auto before = edited;
        const ImGuizmo::OPERATION operations[]{ImGuizmo::TRANSLATE,ImGuizmo::ROTATE,ImGuizmo::SCALE};
        const float step = snapSteps_[mode_];
        const float snap[]{step,step,step};
        // Engineはrow-vector/row-major。ImGuizmoのfloat[16]と同じ配置なので転置しない。
        const bool changed = ImGuizmo::Manipulate(frame_->camera->viewMatrix.elements.data(),
            frame_->camera->projectionMatrix.elements.data(),operations[mode_],localSpace_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
            edited.elements.data(),nullptr,(snapping_ || io.KeyCtrl) ? snap : nullptr);
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
