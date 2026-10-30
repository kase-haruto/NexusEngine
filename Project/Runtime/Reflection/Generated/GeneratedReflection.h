// 自動生成ファイル。型定義側の NEXUS_REFLECT 宣言から再生成する。
#pragma once
#include "Foundation/Reflection/ReflectionValue.h"
#include "Foundation/Math/Quaternion.h"
#include "Foundation/Math/Vector3.h"
#include "Game/Components/MoveComponent.h"
#include "Graphics/Material/MaterialOverride.h"
#include "Graphics/Shadow/ShadowTypes.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/Components/NameComponent.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Runtime/Scene/EntityId.h"
namespace NexusEngine {
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
template<> struct ReflectionTraits<HierarchyComponent> {
    static constexpr const char* Id = "Hierarchy";
    static constexpr const char* Label = "";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("parent", &HierarchyComponent::parent);
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
template<> struct ReflectionTraits<NexusGame::MoveComponent> {
    static constexpr const char* Id = "Game.Move";
    static constexpr const char* Label = "component.gameMove";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("speed", &NexusGame::MoveComponent::speed);
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
