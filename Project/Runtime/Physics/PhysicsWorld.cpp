#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include "PhysicsWorld.h"
#include "PhysicsComponentAccess.h"
#include "PhysicsMeshCooking.h"
#include "PhysicsContactCollector.h"
#include "Runtime/Scene/Scene.h"
#include "Runtime/Scene/Components/RigidBodyComponent.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include <map>
#include <set>
#include <thread>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace NexusEngine {
namespace {
Error Failure(const char* message) { return Error(ErrorCategory::Scene, 260, message); }
JPH::Vec3 Vector(const std::array<float,3>& value) { return {value[0],value[1],value[2]}; }
std::array<float,3> Array(JPH::Vec3Arg value) { return {value.GetX(),value.GetY(),value.GetZ()}; }
uint64_t Key(EntityId id) { return (static_cast<uint64_t>(id.generation)<<32)|id.index; }
/*-----------------------------------------------------------------------------------------
 * JoltRegistration
 * - Engineが利用するProcess共通の型・allocator登録をRAIIで保持する
 * - 各Worldの生成より先に確立し、終了時はWorld破棄後に登録を解除する
 *---------------------------------------------------------------------------------------*/
struct JoltRegistration {
    JoltRegistration() {
        // 他の所有者のFactoryを上書きするとallocatorと型情報の寿命が崩れるため拒否する。
        if(JPH::Factory::sInstance) throw std::logic_error("Jolt Factory is already owned outside Nexus Physics.");
        JPH::RegisterDefaultAllocator();
        auto factory = std::make_unique<JPH::Factory>();
        JPH::Factory::sInstance = factory.get();
        try { JPH::RegisterTypes(); }
        catch(...) { JPH::Factory::sInstance = nullptr; throw; }
        static_cast<void>(factory.release());
    }
    ~JoltRegistration() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};
}
/*-----------------------------------------------------------------------------------------
 * PhysicsWorld::Impl
 * - Jolt solver、衝突filter、作業領域とEntityIdからBodyへの派生索引を所有する
 * - Entityの保存値を持つECSではなく、Sceneを交換したら再構築する計算用状態に限定する
 *---------------------------------------------------------------------------------------*/
struct PhysicsWorld::Impl {
    /** 直前の同期値。外部からのpose/速度変更とsolver自身の結果を区別するために保持する。 */
    struct Body {
        JPH::BodyID id; //< solver所有bodyへの識別子。外部には公開しない
        RigidBodyComponent settings; //< 変更検出用のコピー。ゲーム状態の正本はScene
        std::array<float,3> scale; //< 形状再生成の判定に使う前回scale
        JPH::RVec3 position; //< solverから最後に書き戻した位置
        JPH::Quat rotation; //< solverから最後に書き戻した回転
        std::string meshAsset; //< 形状変更の検出用の参照名。CPU model pointerは保持しない
    };
    std::thread::id thread=std::this_thread::get_id(); //< solver/APIを同じ所有threadに限定する
    Scene* scene=nullptr; //< 所有しない。交換時は参照を比較するだけで旧objectをdereferenceしない
    JPH::BroadPhaseLayerInterfaceTable layers{2,2}; //< Staticと移動bodyを分ける派生layer
    JPH::ObjectLayerPairFilterTable pairs{2}; //< Static同士のsimulation接触は作らない
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> broad; //< solverより長く保持するfilter
    JPH::TempAllocatorImpl allocator{16*1024*1024}; //< World専用のstep作業領域
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs}; //< 既存の逐次実行契約を維持するbackend
    PhysicsContactCollector contacts; //< solverより長く生存する値収集listener。ゲームcallbackを呼ばない
    JPH::PhysicsSystem physics; //< JoltのBody/Shapeと接触cacheを所有する
    std::map<uint64_t,Body> bodies; //< solver実体へのIDだけ。別ECSのゲーム状態Storageにはしない
    ModelSource models; //< Hostが所有する既存ModelAssetLibraryへの同期借用
    using MeshKey=std::tuple<std::string,int,std::array<float,3>>;
    std::map<MeshKey,CookedPhysicsMesh> meshShapes; //< 同じ参照・形状・scaleのcookingをWorld内で共有する
    uint32_t meshTriangles=0; //< cache全体の上限。未参照shapeは追加時に回収する
    /** \brief filterをsolverより先に確定し、CPU solverを初期化する */
    explicit Impl(ModelSource source):models(std::move(source)) {
        layers.MapObjectToBroadPhaseLayer(0,JPH::BroadPhaseLayer(0)); layers.MapObjectToBroadPhaseLayer(1,JPH::BroadPhaseLayer(1));
        pairs.EnableCollision(0,1); pairs.EnableCollision(1,1);
        broad=std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(layers,2,pairs,2);
        physics.Init(16384,0,65536,16384,layers,*broad,pairs);
        physics.SetContactListener(&contacts);
        physics.SetGravity(JPH::Vec3(0,-9.81f,0));
    }
    /** \brief Bodyを解除してからsolverとfilterを逆順破棄する */
    ~Impl() { Clear(); }
    /** \brief 旧Sceneを参照せずに全Bodyを解除・破棄する */
    void Clear() {
        auto& api=physics.GetBodyInterface(); for(auto& [key,body]:bodies) { static_cast<void>(key); api.RemoveBody(body.id); api.DestroyBody(body.id); } bodies.clear();
        meshShapes.clear(); meshTriangles=0;
        contacts.Clear();
    }
    /** \brief CPU modelの既存cacheを借用し、World所有のJolt形状へ一度だけcookingする */
    Result<JPH::ShapeRefC> Mesh(std::string_view reference,int shape,const std::array<float,3>& scale) {
        const MeshKey key{std::string(reference),shape,scale};
        if(auto found=meshShapes.find(key);found!=meshShapes.end()) return found->second.shape;
        if(!models) return std::unexpected(Failure("Mesh Collider requires the Host model source."));
        for(auto it=meshShapes.begin();it!=meshShapes.end();) {
            if(it->second.shape->GetRefCount()==1) { meshTriangles-=it->second.triangles; it=meshShapes.erase(it); }
            else ++it;
        }
        if(meshShapes.size()>=128) return std::unexpected(Failure("Physics mesh shape cache capacity exceeded."));
        auto source=models(reference); if(!source) return std::unexpected(source.error());
        if(!*source) return std::unexpected(Failure("Physics model source returned an empty asset."));
        auto cooked=CookPhysicsMesh(**source,shape,scale); if(!cooked) return std::unexpected(cooked.error());
        if(uint64_t(meshTriangles)+cooked->triangles>1000000) return std::unexpected(Failure("Physics mesh triangle cache capacity exceeded."));
        meshTriangles+=cooked->triangles;
        return meshShapes.emplace(key,std::move(*cooked)).first->second.shape;
    }
    /** \brief shape/material変更時だけ再生成する。速度と力は既存bodyの値として更新する */
    static bool Same(const Body& body,const RigidBodyComponent& value,const std::array<float,3>& scale) {
        const auto& old=body.settings;
        return old.motion==value.motion && old.shape==value.shape && old.halfExtent==value.halfExtent && old.radius==value.radius && old.halfHeight==value.halfHeight &&
            old.mass==value.mass && old.friction==value.friction && old.restitution==value.restitution && old.gravityScale==value.gravityScale && old.sensor==value.sensor && old.continuous==value.continuous && body.scale==scale;
    }
    /** \brief 全入力を検証後、同じScene内の世代付きEntityIdをBodyへ同期する */
    Result<void> Sync(Scene& value) {
        if(thread!=std::this_thread::get_id()) return std::unexpected(Failure("Physics requires its owner thread."));
        // 全入力を先に検証する。不正Entityの検出前に一部bodyだけ更新しない。
        bool valid=true; size_t count=0; std::set<uint64_t> keep;
        value.ForEachEntity([&](Entity entity) {
            RigidBodyComponent component;
            if(!ResolvePhysicsBody(entity,component)) return;
            keep.insert(Key(entity.GetId()));
            ++count;
            const auto* transform=entity.GetComponent<TransformComponent>();
            const auto* rigid=entity.GetComponent<RigidBodyComponent>();
            const auto* collider=entity.GetComponent<ColliderComponent>();
            if((rigid && !rigid->IsValid()) || (collider && !collider->IsValid()) || !transform || (component.shape==3 && component.motion==1)) { valid=false; return; }
            if(const auto* hierarchy=entity.GetComponent<HierarchyComponent>(); hierarchy && hierarchy->parent.IsValid()) { valid=false; return; }
            const float pose[]{transform->translation.x,transform->translation.y,transform->translation.z,
                transform->rotation.x,transform->rotation.y,transform->rotation.z,transform->rotation.w};
            for(float item:pose) if(!std::isfinite(item)) valid=false;
            const float scale[]{transform->scale.x,transform->scale.y,transform->scale.z};
            for(float item:scale) if(!std::isfinite(item) || item<0.0001f || item>10000) valid=false;
            // 極端に小さい形状や積算overflowをsolverへ渡さない。形状寸法はscale適用後も検査する。
            const auto supported=[](float dimension) { return std::isfinite(dimension) && dimension>=0.0001f && dimension<=100000; };
            if(component.shape==0) {
                for(size_t i=0;i<3;++i) if(!supported(component.halfExtent[i]*scale[i])) valid=false;
            } else if(component.shape<3) {
                const float radius=component.radius*(component.shape==1 ? (std::max)({scale[0],scale[1],scale[2]}) : (std::max)(scale[0],scale[2]));
                if(!supported(radius)) valid=false;
                if(component.shape==2 && component.halfHeight!=0 && !supported(component.halfHeight*scale[1])) valid=false;
            }
            const JPH::Quat rotation(transform->rotation.x,transform->rotation.y,transform->rotation.z,transform->rotation.w);
            if(!rotation.IsNormalized()) valid=false;
        });
        if(!valid || count>16384) return std::unexpected(Failure("Invalid RigidBody/Collider or pose. Bodies require root Entities; Triangle Mesh requires Static/Kinematic motion."));
        if(scene!=&value) { Clear(); scene=&value; }
        auto& api=physics.GetBodyInterface(); std::optional<Error> cookingError; bool wakeDynamics=false;
        // 削除済みEntityを生成より先に解放する。上限数のWorldでもslotを再利用できるようにする。
        for(auto it=bodies.begin();it!=bodies.end();) {
            if(keep.contains(it->first)) { ++it; continue; }
            wakeDynamics=true;
            api.RemoveBody(it->second.id); api.DestroyBody(it->second.id); it=bodies.erase(it);
        }
        value.ForEachEntity([&](Entity entity) {
            RigidBodyComponent component;
            if(!ResolvePhysicsBody(entity,component)) return;
            auto& transform=*entity.GetComponent<TransformComponent>();
            const auto* collider=entity.GetComponent<ColliderComponent>();
            const std::string_view mesh=collider ? collider->meshAsset : std::string_view{};
            // 検証済みroot Entityのlocal poseをWorld poseとして同期する。
            const std::array<float,3> scale{transform.scale.x,transform.scale.y,transform.scale.z};
            JPH::RVec3 position(transform.translation.x,transform.translation.y,transform.translation.z);
            JPH::Quat rotation(transform.rotation.x,transform.rotation.y,transform.rotation.z,transform.rotation.w);
            const auto key=Key(entity.GetId()); keep.insert(key); auto found=bodies.find(key);
            if(found!=bodies.end() && (!Same(found->second,component,scale) || (component.shape>=3 && found->second.meshAsset!=mesh))) { wakeDynamics=true; api.RemoveBody(found->second.id); api.DestroyBody(found->second.id); bodies.erase(found); found=bodies.end(); }
            if(found==bodies.end()) {
                wakeDynamics |= component.motion==0;
                JPH::ShapeRefC shape;
                if(component.shape>=3) {
                    auto cooked=Mesh(mesh,component.shape,scale); if(!cooked) { valid=false; cookingError=cooked.error(); return; }
                    shape=std::move(*cooked);
                } else if(component.shape==0) shape=new JPH::BoxShape(JPH::Vec3(component.halfExtent[0]*scale[0],component.halfExtent[1]*scale[1],component.halfExtent[2]*scale[2]));
                else if(component.shape==1) shape=new JPH::SphereShape(component.radius*(std::max)({scale[0],scale[1],scale[2]}));
                else if(component.halfHeight==0) shape=new JPH::SphereShape(component.radius*(std::max)(scale[0],scale[2]));
                else shape=new JPH::CapsuleShape(component.halfHeight*scale[1],component.radius*(std::max)(scale[0],scale[2]));
                const auto motion=component.motion==0?JPH::EMotionType::Static:component.motion==1?JPH::EMotionType::Dynamic:JPH::EMotionType::Kinematic;
                JPH::BodyCreationSettings settings(shape,position,rotation,motion,static_cast<JPH::ObjectLayer>(component.motion==0?0:1));
                settings.mFriction=component.friction; settings.mRestitution=component.restitution; settings.mGravityFactor=component.gravityScale; settings.mIsSensor=component.sensor;
                settings.mMotionQuality=component.continuous && component.motion==1 ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
                settings.mCollideKinematicVsNonDynamic=component.motion==2;
                settings.mLinearVelocity=Vector(component.velocity); settings.mAngularVelocity=Vector(component.angularVelocity); settings.mUserData=key;
                if(component.motion!=0) { settings.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia; settings.mMassPropertiesOverride.mMass=component.mass; }
                auto id=api.CreateAndAddBody(settings,JPH::EActivation::Activate); if(id.IsInvalid()) { valid=false; return; }
                found=bodies.emplace(key,Body{id,component,scale,position,rotation,std::string(mesh)}).first;
            } else {
                if(position!=found->second.position || rotation!=found->second.rotation) {
                    wakeDynamics |= component.motion==0;
                    api.SetPositionAndRotation(found->second.id,position,rotation,JPH::EActivation::Activate);
                }
                // solver結果を毎frame再設定すると静止bodyまで起床するため、外部変更だけ適用する。
                if(component.motion!=0 && component.velocity!=found->second.settings.velocity) api.SetLinearVelocity(found->second.id,Vector(component.velocity));
                if(component.motion!=0 && component.angularVelocity!=found->second.settings.angularVelocity) api.SetAngularVelocity(found->second.id,Vector(component.angularVelocity));
                found->second.settings=component;
                found->second.position=position; found->second.rotation=rotation;
            }
        });
        for(auto it=bodies.begin();it!=bodies.end();) { if(keep.contains(it->first)) { ++it; continue; } api.RemoveBody(it->second.id); api.DestroyBody(it->second.id); it=bodies.erase(it); }
        if(cookingError) return std::unexpected(*cookingError);
        if(!valid) return std::unexpected(Failure("Invalid RigidBody, pose, parent hierarchy, or physics capacity."));
        // Staticの移動/撤去でも、その上でsleep中のDynamicを宙に残さない。空間最適化は計測後に検討する。
        if(wakeDynamics) for(const auto& [key,body]:bodies) { static_cast<void>(key); if(body.settings.motion==1) api.ActivateBody(body.id); }
        return {};
    }
};
PhysicsWorld::PhysicsWorld(ModelSource models) {
    // 登録はProcess共通だがWorld/solverは個別所有。DLL/Managed callbackをJoltへ登録しない。
    static JoltRegistration registration;
    impl_=std::make_unique<Impl>(std::move(models));
}
PhysicsWorld::~PhysicsWorld()=default;
Result<void> PhysicsWorld::Step(Scene& scene,float deltaTime) {
    if(!std::isfinite(deltaTime)||deltaTime<0||deltaTime>1) return std::unexpected(Failure("Physics delta time must be between zero and one second."));
    if(deltaTime>0) impl_->contacts.BeginFrame();
    auto synced=impl_->Sync(scene); if(!synced) return synced;
    if(deltaTime==0) return {};
    auto& api=impl_->physics.GetBodyInterface();
    scene.ForEach<RigidBodyComponent>([&](Entity entity,RigidBodyComponent& value) {
        const auto found=impl_->bodies.find(Key(entity.GetId())); if(found==impl_->bodies.end()||value.motion!=1) return;
        if(value.force!=std::array<float,3>{}) api.AddForce(found->second.id,Vector(value.force));
        if(value.impulse!=std::array<float,3>{}) api.AddImpulse(found->second.id,Vector(value.impulse));
        value.force={}; value.impulse={};
    });
    const int steps=(std::max)(1,static_cast<int>(std::ceil(deltaTime*60)));
    if(impl_->physics.Update(deltaTime,steps,&impl_->allocator,&impl_->jobs)!=JPH::EPhysicsUpdateError::None) return std::unexpected(Failure("Jolt simulation capacity exceeded."));
    impl_->contacts.FinishFrame(impl_->physics.GetBodyLockInterface());
    for(auto& [key,body]:impl_->bodies) {
        auto entity=scene.GetEntity({static_cast<uint32_t>(key),static_cast<uint32_t>(key>>32)});
        auto* value=entity.GetComponent<RigidBodyComponent>(); auto* transform=entity.GetComponent<TransformComponent>(); if(!value||!transform) continue;
        body.position=api.GetPosition(body.id); body.rotation=api.GetRotation(body.id);
        transform->translation={body.position.GetX(),body.position.GetY(),body.position.GetZ()}; transform->rotation={body.rotation.GetX(),body.rotation.GetY(),body.rotation.GetZ(),body.rotation.GetW()};
        if(value->motion!=0) { value->velocity=Array(api.GetLinearVelocity(body.id)); value->angularVelocity=Array(api.GetAngularVelocity(body.id)); }
        body.settings.velocity=value->velocity; body.settings.angularVelocity=value->angularVelocity;
    }
    return {};
}
Result<PhysicsWorld::Hit> PhysicsWorld::Raycast(Scene& scene,const std::array<float,3>& origin,const std::array<float,3>& direction,float distance) {
    for(const auto* vector:{&origin,&direction}) for(float value:*vector) if(!std::isfinite(value)) return std::unexpected(Failure("Ray contains nonfinite values."));
    const auto vector=Vector(direction); const float length=vector.Length(); if(!std::isfinite(length)||length<1e-6f||!std::isfinite(distance)||distance<=0||distance>1000000) return std::unexpected(Failure("Invalid ray direction or distance."));
    auto synced=impl_->Sync(scene); if(!synced) return std::unexpected(synced.error());
    JPH::RRayCast ray(JPH::RVec3(origin[0],origin[1],origin[2]),vector*(distance/length)); JPH::RayCastResult result;
    if(!impl_->physics.GetNarrowPhaseQuery().CastRay(ray,result)) return Hit{};
    const auto key=impl_->physics.GetBodyInterface().GetUserData(result.mBodyID); Hit hit; hit.entity={static_cast<uint32_t>(key),static_cast<uint32_t>(key>>32)};
    const auto point=ray.GetPointOnRay(result.mFraction); hit.point={point.GetX(),point.GetY(),point.GetZ()}; hit.distance=distance*result.mFraction;
    JPH::BodyLockRead lock(impl_->physics.GetBodyLockInterface(),result.mBodyID); if(lock.Succeeded()) hit.normal=Array(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2,point));
    return hit;
}
std::span<const PhysicsWorld::Contact> PhysicsWorld::Contacts() const noexcept { return impl_->contacts.Events(); }
uint32_t PhysicsWorld::DroppedContacts() const noexcept { return impl_->contacts.Dropped(); }
}
