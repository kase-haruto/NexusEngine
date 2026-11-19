// 自動生成ファイル。型定義側の NEXUS_REFLECT 宣言から再生成する。
#pragma once
#include "Foundation/Reflection/ReflectionValue.h"
#include "Foundation/Math/Quaternion.h"
#include "Foundation/Math/Vector3.h"
#include "Graphics/Material/MaterialOverride.h"
#include "Graphics/Shadow/ShadowTypes.h"
#include "Runtime/Scene/Components/AnimationPlayerComponent.h"
#include "Runtime/Scene/Components/AnimatorComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/CameraEffectComponent.h"
#include "Runtime/Scene/Components/ColliderComponent.h"
#include "Runtime/Scene/Components/CurveBindingComponent.h"
#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include "Runtime/Scene/Components/ExponentialHeightFogComponent.h"
#include "Runtime/Scene/Components/FogComponent.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/Components/KeyframeAnimationComponent.h"
#include "Runtime/Scene/Components/NameComponent.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/RigidBodyComponent.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include "Runtime/Scene/Components/SkeletalAnimationComponent.h"
#include "Runtime/Scene/Components/SkyAtmosphereComponent.h"
#include "Runtime/Scene/Components/SkyLightComponent.h"
#include "Runtime/Scene/Components/SkyRendererComponent.h"
#include "Runtime/Scene/Components/TimelinePlayerComponent.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Runtime/Scene/EntityId.h"
namespace NexusEngine {
template<> struct ReflectionTraits<AnimationPlayerComponent> {
    static constexpr const char* Id = "AnimationPlayer";
    static constexpr const char* Label = "component.animationPlayer";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("animationIndex", &AnimationPlayerComponent::animationIndex);
        visitor("playbackSpeed", &AnimationPlayerComponent::playbackSpeed);
        visitor("loop", &AnimationPlayerComponent::loop);
        visitor("playing", &AnimationPlayerComponent::playing);
        visitor("time", &AnimationPlayerComponent::time);
        visitor("animationAsset", &AnimationPlayerComponent::animationAsset);
        visitor("blendAnimationAsset", &AnimationPlayerComponent::blendAnimationAsset);
        visitor("blendAnimationIndex", &AnimationPlayerComponent::blendAnimationIndex);
        visitor("blendTime", &AnimationPlayerComponent::blendTime);
        visitor("blendLoop", &AnimationPlayerComponent::blendLoop);
        visitor("blendWeight", &AnimationPlayerComponent::blendWeight);
        visitor("fadeDuration", &AnimationPlayerComponent::fadeDuration);
        visitor("fadeElapsed", &AnimationPlayerComponent::fadeElapsed);
    }
};
template<> struct ReflectionTraits<AnimatorComponent> {
    static constexpr const char* Id = "Animator";
    static constexpr const char* Label = "component.animator";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("controllerAsset", &AnimatorComponent::controllerAsset);
        visitor("playing", &AnimatorComponent::playing);
        visitor("parameters", &AnimatorComponent::parameters);
    }
};
template<> struct ReflectionTraits<CameraComponent> {
    static constexpr const char* Id = "Camera";
    static constexpr const char* Label = "component.camera";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("fov", &CameraComponent::verticalFieldOfViewRadians);
        visitor("near", &CameraComponent::nearClip);
        visitor("far", &CameraComponent::farClip);
        visitor("primary", &CameraComponent::primary);
        visitor("enabled", &CameraComponent::enabled);
        visitor("frustumSize", &CameraComponent::smallFrustum);
    }
};
template<> struct ReflectionTraits<CameraEffectComponent> {
    static constexpr const char* Id = "CameraEffect";
    static constexpr const char* Label = "component.cameraEffect";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("shader", &CameraEffectComponent::shader);
        visitor("enabled", &CameraEffectComponent::enabled);
        visitor("parameters0", &CameraEffectComponent::parameters0);
        visitor("parameters1", &CameraEffectComponent::parameters1);
        visitor("parameters2", &CameraEffectComponent::parameters2);
        visitor("parameters3", &CameraEffectComponent::parameters3);
    }
};
template<> struct ReflectionTraits<ColliderComponent> {
    static constexpr const char* Id = "Collider";
    static constexpr const char* Label = "component.collider";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("enabled", &ColliderComponent::enabled);
        visitor("meshAsset", &ColliderComponent::meshAsset);
        visitor("shape", &ColliderComponent::shape);
        visitor("halfExtent", &ColliderComponent::halfExtent);
        visitor("radius", &ColliderComponent::radius);
        visitor("halfHeight", &ColliderComponent::halfHeight);
        visitor("friction", &ColliderComponent::friction);
        visitor("restitution", &ColliderComponent::restitution);
        visitor("sensor", &ColliderComponent::sensor);
        visitor("collisionLayer", &ColliderComponent::collisionLayer);
        visitor("collisionMask", &ColliderComponent::collisionMask);
    }
};
template<> struct ReflectionTraits<CurveBindingComponent> {
    static constexpr const char* Id = "CurveBinding";
    static constexpr const char* Label = "component.curveBinding";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("curveAsset", &CurveBindingComponent::curveAsset);
        visitor("target", &CurveBindingComponent::target);
        visitor("targetComponent", &CurveBindingComponent::targetComponent);
        visitor("targetProperty", &CurveBindingComponent::targetProperty);
        visitor("channel", &CurveBindingComponent::channel);
        visitor("duration", &CurveBindingComponent::duration);
        visitor("elapsed", &CurveBindingComponent::elapsed);
        visitor("playbackSpeed", &CurveBindingComponent::playbackSpeed);
        visitor("loop", &CurveBindingComponent::loop);
        visitor("playing", &CurveBindingComponent::playing);
        visitor("manual", &CurveBindingComponent::manual);
        visitor("input", &CurveBindingComponent::input);
        visitor("outputScale", &CurveBindingComponent::outputScale);
        visitor("outputBias", &CurveBindingComponent::outputBias);
    }
};
template<> struct ReflectionTraits<DirectionalLightComponent> {
    static constexpr const char* Id = "DirectionalLight";
    static constexpr const char* Label = "component.light";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("light", &DirectionalLightComponent::light);
        visitor("enabled", &DirectionalLightComponent::enabled);
    }
};
template<> struct ReflectionTraits<DirectionalLightData> {
    static constexpr const char* Id = "DirectionalLightData";
    static constexpr const char* Label = "";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("direction", &DirectionalLightData::direction);
        visitor("angularRadius", &DirectionalLightData::angularRadius);
        visitor("color", &DirectionalLightData::color);
        visitor("intensity", &DirectionalLightData::intensity);
    }
};
template<> struct ReflectionTraits<EntityId> {
    static constexpr const char* Id = "EntityId";
    static constexpr const char* Label = "";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("index", &EntityId::index);
        visitor("generation", &EntityId::generation);
    }
};
template<> struct ReflectionTraits<ExponentialHeightFogComponent> {
    static constexpr const char* Id = "ExponentialHeightFog";
    static constexpr const char* Label = "component.heightFog";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("enabled", &ExponentialHeightFogComponent::enabled);
        visitor("density", &ExponentialHeightFogComponent::density);
        visitor("heightFalloff", &ExponentialHeightFogComponent::heightFalloff);
        visitor("heightOffset", &ExponentialHeightFogComponent::heightOffset);
        visitor("startDistance", &ExponentialHeightFogComponent::startDistance);
        visitor("maxOpacity", &ExponentialHeightFogComponent::maxOpacity);
        visitor("cutoffDistance", &ExponentialHeightFogComponent::cutoffDistance);
        visitor("color", &ExponentialHeightFogComponent::color);
        visitor("atmosphereContribution", &ExponentialHeightFogComponent::atmosphereContribution);
    }
};
template<> struct ReflectionTraits<FogComponent> {
    static constexpr const char* Id = "Fog";
    static constexpr const char* Label = "component.fog";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("enabled", &FogComponent::enabled);
        visitor("startDistance", &FogComponent::startDistance);
        visitor("endDistance", &FogComponent::endDistance);
        visitor("color", &FogComponent::color);
    }
};
template<> struct ReflectionTraits<HierarchyComponent> {
    static constexpr const char* Id = "Hierarchy";
    static constexpr const char* Label = "";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("parent", &HierarchyComponent::parent);
    }
};
template<> struct ReflectionTraits<KeyframeAnimationComponent> {
    static constexpr const char* Id = "KeyframeAnimation";
    static constexpr const char* Label = "component.keyframeAnimation";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("clipAsset", &KeyframeAnimationComponent::clipAsset);
        visitor("speedCurveAsset", &KeyframeAnimationComponent::speedCurveAsset);
        visitor("playbackSpeed", &KeyframeAnimationComponent::playbackSpeed);
        visitor("loop", &KeyframeAnimationComponent::loop);
        visitor("playing", &KeyframeAnimationComponent::playing);
        visitor("elapsed", &KeyframeAnimationComponent::elapsed);
        visitor("speedCurveDuration", &KeyframeAnimationComponent::speedCurveDuration);
        visitor("controllerAsset", &KeyframeAnimationComponent::controllerAsset);
        visitor("controllerParameters", &KeyframeAnimationComponent::controllerParameters);
        visitor("reverseEvents", &KeyframeAnimationComponent::reverseEvents);
    }
};
template<> struct ReflectionTraits<MaterialOverride> {
    static constexpr const char* Id = "MaterialOverride";
    static constexpr const char* Label = "";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("enabled", &MaterialOverride::enabled);
        visitor("slot", &MaterialOverride::slot);
        visitor("baseColor", &MaterialOverride::baseColor);
        visitor("metallic", &MaterialOverride::metallic);
        visitor("roughness", &MaterialOverride::roughness);
        visitor("normalScale", &MaterialOverride::normalScale);
        visitor("emissive", &MaterialOverride::emissive);
        visitor("occlusionStrength", &MaterialOverride::occlusionStrength);
    }
};
template<> struct ReflectionTraits<NameComponent> {
    static constexpr const char* Id = "Name";
    static constexpr const char* Label = "component.name";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("name", &NameComponent::name);
    }
};
template<> struct ReflectionTraits<PersistentId> {
    static constexpr const char* Id = "PersistentId";
    static constexpr const char* Label = "";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("high", &PersistentId::high);
        visitor("low", &PersistentId::low);
    }
};
template<> struct ReflectionTraits<PersistentIdComponent> {
    static constexpr const char* Id = "PersistentIdComponent";
    static constexpr const char* Label = "";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("value", &PersistentIdComponent::value);
    }
};
template<> struct ReflectionTraits<PrimitiveRenderComponent> {
    static constexpr const char* Id = "PrimitiveRender";
    static constexpr const char* Label = "component.primitive";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("tint", &PrimitiveRenderComponent::tint);
        visitor("enabled", &PrimitiveRenderComponent::enabled);
        visitor("nodeIndex", &PrimitiveRenderComponent::nodeIndex);
        visitor("staticLighting", &PrimitiveRenderComponent::staticLighting);
        visitor("meshAsset", &PrimitiveRenderComponent::meshAsset);
        visitor("baseColorTexture", &PrimitiveRenderComponent::baseColorTexture);
        visitor("material", &PrimitiveRenderComponent::material);
        visitor("shape", &PrimitiveRenderComponent::shape);
        visitor("uvScale", &PrimitiveRenderComponent::uvScale);
        visitor("uvOffset", &PrimitiveRenderComponent::uvOffset);
    }
};
template<> struct ReflectionTraits<Quaternion> {
    static constexpr const char* Id = "Quaternion";
    static constexpr const char* Label = "";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("x", &Quaternion::x);
        visitor("y", &Quaternion::y);
        visitor("z", &Quaternion::z);
        visitor("w", &Quaternion::w);
    }
};
template<> struct ReflectionTraits<RigidBodyComponent> {
    static constexpr const char* Id = "RigidBody";
    static constexpr const char* Label = "component.rigidBody";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("enabled", &RigidBodyComponent::enabled);
        visitor("motion", &RigidBodyComponent::motion);
        visitor("shape", &RigidBodyComponent::shape);
        visitor("halfExtent", &RigidBodyComponent::halfExtent);
        visitor("radius", &RigidBodyComponent::radius);
        visitor("halfHeight", &RigidBodyComponent::halfHeight);
        visitor("mass", &RigidBodyComponent::mass);
        visitor("friction", &RigidBodyComponent::friction);
        visitor("restitution", &RigidBodyComponent::restitution);
        visitor("gravityScale", &RigidBodyComponent::gravityScale);
        visitor("velocity", &RigidBodyComponent::velocity);
        visitor("angularVelocity", &RigidBodyComponent::angularVelocity);
        visitor("sensor", &RigidBodyComponent::sensor);
        visitor("continuous", &RigidBodyComponent::continuous);
        visitor("collisionLayer", &RigidBodyComponent::collisionLayer);
        visitor("collisionMask", &RigidBodyComponent::collisionMask);
    }
};
template<> struct ReflectionTraits<ScriptComponent> {
    static constexpr const char* Id = "Script";
    static constexpr const char* Label = "component.script";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("scriptType", &ScriptComponent::scriptType);
        visitor("enabled", &ScriptComponent::enabled);
        visitor("propertyState", &ScriptComponent::propertyState);
        visitor("additional", &ScriptComponent::additional);
        visitor("slotId", &ScriptComponent::slotId);
    }
};
template<> struct ReflectionTraits<SkeletalAnimationComponent> {
    static constexpr const char* Id = "SkeletalAnimation";
    static constexpr const char* Label = "component.skeletalAnimation";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("clipAsset", &SkeletalAnimationComponent::clipAsset);
        visitor("speedCurveAsset", &SkeletalAnimationComponent::speedCurveAsset);
        visitor("playbackSpeed", &SkeletalAnimationComponent::playbackSpeed);
        visitor("loop", &SkeletalAnimationComponent::loop);
        visitor("playing", &SkeletalAnimationComponent::playing);
        visitor("elapsed", &SkeletalAnimationComponent::elapsed);
        visitor("speedCurveDuration", &SkeletalAnimationComponent::speedCurveDuration);
        visitor("blendClipAsset", &SkeletalAnimationComponent::blendClipAsset);
        visitor("blendSpeed", &SkeletalAnimationComponent::blendSpeed);
        visitor("blendLoop", &SkeletalAnimationComponent::blendLoop);
        visitor("blendWeight", &SkeletalAnimationComponent::blendWeight);
        visitor("crossfadeDuration", &SkeletalAnimationComponent::crossfadeDuration);
        visitor("layerAsset", &SkeletalAnimationComponent::layerAsset);
        visitor("controllerAsset", &SkeletalAnimationComponent::controllerAsset);
        visitor("controllerParameters", &SkeletalAnimationComponent::controllerParameters);
        visitor("rootMotion", &SkeletalAnimationComponent::rootMotion);
        visitor("rootMotionJoint", &SkeletalAnimationComponent::rootMotionJoint);
        visitor("rootMotionAxes", &SkeletalAnimationComponent::rootMotionAxes);
        visitor("rootMotionRotation", &SkeletalAnimationComponent::rootMotionRotation);
        visitor("applyRootMotion", &SkeletalAnimationComponent::applyRootMotion);
        visitor("reverseEvents", &SkeletalAnimationComponent::reverseEvents);
    }
};
template<> struct ReflectionTraits<SkyAtmosphereComponent> {
    static constexpr const char* Id = "SkyAtmosphere";
    static constexpr const char* Label = "component.skyAtmosphere";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("enabled", &SkyAtmosphereComponent::enabled);
        visitor("timeOfDay", &SkyAtmosphereComponent::timeOfDay);
        visitor("sunAzimuth", &SkyAtmosphereComponent::sunAzimuth);
        visitor("sunIntensity", &SkyAtmosphereComponent::sunIntensity);
        visitor("skyIntensity", &SkyAtmosphereComponent::skyIntensity);
        visitor("rayleighScale", &SkyAtmosphereComponent::rayleighScale);
        visitor("mieScale", &SkyAtmosphereComponent::mieScale);
        visitor("mieAnisotropy", &SkyAtmosphereComponent::mieAnisotropy);
        visitor("nightIntensity", &SkyAtmosphereComponent::nightIntensity);
        visitor("driveSun", &SkyAtmosphereComponent::driveSun);
    }
};
template<> struct ReflectionTraits<SkyLightComponent> {
    static constexpr const char* Id = "SkyLight";
    static constexpr const char* Label = "component.skyLight";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("enabled", &SkyLightComponent::enabled);
        visitor("useAtmosphere", &SkyLightComponent::useAtmosphere);
        visitor("intensity", &SkyLightComponent::intensity);
    }
};
template<> struct ReflectionTraits<SkyRendererComponent> {
    static constexpr const char* Id = "SkyRenderer";
    static constexpr const char* Label = "component.sky";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("enabled", &SkyRendererComponent::enabled);
        visitor("intensity", &SkyRendererComponent::intensity);
    }
};
template<> struct ReflectionTraits<TimelinePlayerComponent> {
    static constexpr const char* Id = "TimelinePlayer";
    static constexpr const char* Label = "component.timeline";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("timelineAsset", &TimelinePlayerComponent::timelineAsset);
        visitor("time", &TimelinePlayerComponent::time);
        visitor("playing", &TimelinePlayerComponent::playing);
        visitor("loop", &TimelinePlayerComponent::loop);
        visitor("speed", &TimelinePlayerComponent::speed);
    }
};
template<> struct ReflectionTraits<TransformComponent> {
    static constexpr const char* Id = "Transform";
    static constexpr const char* Label = "component.transform";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("translation", &TransformComponent::translation);
        visitor("rotation", &TransformComponent::rotation);
        visitor("scale", &TransformComponent::scale);
    }
};
template<> struct ReflectionTraits<Vector3> {
    static constexpr const char* Id = "Vector3";
    static constexpr const char* Label = "";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("x", &Vector3::x);
        visitor("y", &Vector3::y);
        visitor("z", &Vector3::z);
    }
};
}
namespace NexusEngine { class ComponentReflection; class TypeRegistry;
void RegisterGeneratedComponents(ComponentReflection& registry);
void RegisterGeneratedInspectorTypes(TypeRegistry& registry);
}
