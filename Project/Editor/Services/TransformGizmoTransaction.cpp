#include "TransformGizmoTransaction.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cstring>

namespace NexusEngine {
namespace {
bool Decompose(const Matrix4x4& matrix, std::array<float,3>& translation, std::array<float,4>& rotation, std::array<float,3>& scale) {
    DirectX::XMFLOAT4X4 native;
    std::memcpy(&native, matrix.elements.data(), sizeof(native));
    DirectX::XMVECTOR s, r, t;
    if(!DirectX::XMMatrixDecompose(&s, &r, &t, DirectX::XMLoadFloat4x4(&native))) return false;
    DirectX::XMFLOAT3 position, scaling; DirectX::XMFLOAT4 quaternion;
    DirectX::XMStoreFloat3(&position,t); DirectX::XMStoreFloat3(&scaling,s); DirectX::XMStoreFloat4(&quaternion,r);
    translation={position.x,position.y,position.z}; rotation={quaternion.x,quaternion.y,quaternion.z,quaternion.w}; scale={scaling.x,scaling.y,scaling.z};
    const auto recomposed=MakeAffineMatrix({scale[0],scale[1],scale[2]}, {rotation[0],rotation[1],rotation[2],rotation[3]}, {translation[0],translation[1],translation[2]});
    // 分解APIが近似したshearも拒否する。親に非一様scaleがある回転で値を壊さない。
    for(size_t i=0;i<16;++i) if(!std::isfinite(matrix.elements[i]) || !std::isfinite(recomposed.elements[i]) ||
        std::abs(recomposed.elements[i]-matrix.elements[i])>1e-4f*std::max(1.0f,std::abs(matrix.elements[i]))) return false;
    return std::ranges::all_of(scale,[](float v) { return std::isfinite(v) && std::abs(v)>=1e-5f; });
}
}
bool TransformGizmoTransaction::IsCurrent() const {
    const auto current=session_.Selection().All();
    return generation_==session_.Generation() && !session_.IsPlaying() && current.size()==selected_.size() && std::equal(current.begin(),current.end(),selected_.begin());
}
bool TransformGizmoTransaction::Begin(const Matrix4x4& pivot, bool translationOnly) {
    Commit();
    auto inverse=TryInverse(pivot);
    auto* level=session_.GetLevel();
    if(!inverse || !level || session_.IsPlaying() || session_.Selection().All().empty()) return false;
    inversePivot_=*inverse; translationOnly_=translationOnly; generation_=session_.Generation();
    selected_.assign(session_.Selection().All().begin(),session_.Selection().All().end());
    for(auto id:selected_) {
        auto entity=level->FindEntity(id);
        auto* transform=entity.GetComponent<TransformComponent>();
        if(!transform) { targets_.clear(); handles_.clear(); originals_.clear(); return false; }
        Target target; target.world=transform->worldMatrix;
        auto* hierarchy=entity.GetComponent<HierarchyComponent>();
        auto parent=hierarchy ? hierarchy->parent : EntityId{};
        if(auto* value=entity.GetScene()->GetComponent<TransformComponent>(parent)) target.parent=value->worldMatrix;
        // 未選択の中間親も、選択祖先が動けば同じWorld差分を継承する。
        size_t remaining=entity.GetScene()->GetEntityCount();
        while(parent.IsValid() && remaining--) {
            auto ancestor=entity.GetScene()->GetEntity(parent);
            if(auto* persistent=ancestor.GetComponent<PersistentIdComponent>())
                target.parentAffected |= std::ranges::find(selected_,persistent->value)!=selected_.end();
            auto* link=ancestor.GetComponent<HierarchyComponent>(); parent=link ? link->parent : EntityId{};
        }
        targets_.push_back(target);
        for(const char* property:{"translation","rotation","scale"}) {
            PropertyHandle handle{id,"Transform",property}; auto value=properties_.Read(handle);
            if(!value) { targets_.clear(); handles_.clear(); originals_.clear(); return false; }
            handles_.push_back(std::move(handle)); originals_.push_back(*value);
        }
    }
    properties_.Begin(handles_); return true;
}
bool TransformGizmoTransaction::Preview(const Matrix4x4& pivot) {
    if(!IsActive() || !IsCurrent()) { Cancel(); return false; }
    const auto delta=Multiply(inversePivot_,pivot);
    auto values=originals_;
    for(size_t i=0;i<targets_.size();++i) {
        const auto& target=targets_[i];
        auto parentInverse=TryInverse(target.parentAffected ? Multiply(target.parent,delta) : target.parent);
        if(!parentInverse) return false;
        const auto local=Multiply(Multiply(target.world,delta),*parentInverse);
        std::array<float,3> position, scale; std::array<float,4> rotation;
        if(!Decompose(local,position,rotation,scale)) return false;
        values[i*3]=position;
        if(!translationOnly_) { values[i*3+1]=rotation; values[i*3+2]=scale; }
    }
    return properties_.PreviewValues(handles_,values);
}
void TransformGizmoTransaction::Commit() {
    if(IsActive() && IsCurrent()) properties_.Commit();
    targets_.clear(); handles_.clear(); originals_.clear(); selected_.clear();
}
void TransformGizmoTransaction::Cancel() {
    if(IsActive() && generation_==session_.Generation()) properties_.Cancel();
    targets_.clear(); handles_.clear(); originals_.clear(); selected_.clear();
}
}
