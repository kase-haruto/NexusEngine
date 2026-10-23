#include "TypeRegistry.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include <cmath>

namespace NexusEngine {
namespace {
template<class Component, class Value, class Getter, class Setter>
PropertyDescriptor Property(std::string id, std::string key, PropertyKind kind, Getter getter, Setter setter) {
    return {std::move(id), std::move(key), kind,
        [getter](Entity entity) -> std::optional<PropertyValue> {
            if(auto* component = entity.GetComponent<Component>()) return PropertyValue(getter(*component));
            return std::nullopt;
        },
        [setter](Entity entity, const PropertyValue& value) {
            auto* component = entity.GetComponent<Component>();
            const auto* typed = std::get_if<Value>(&value);
            return component && typed && setter(*component, *typed);
        }, {}};
}
template<class C> TypeDescriptor Type(const char* id, const char* label) {
    return {id, label, [](Entity entity) { return entity.HasComponent<C>(); }, {}};
}
bool Finite(const std::array<float, 3>& value) { return std::ranges::all_of(value, [](float f) { return std::isfinite(f); }); }
// 元のInstance値を検証前に変更せず、Detailsと専門ページで同じsetterを共有する。
template<class Value>
PropertyDescriptor MaterialProperty(const char* id, const char* label, PropertyKind kind, Value MaterialOverride::*member) {
    return Property<PrimitiveRenderComponent, Value>(id, label, kind,
        [member](const auto& c) { return c.material.*member; },
        [member](auto& c, const Value& value) {
            auto next = c.material; next.*member = value;
            if(!next.IsValid()) return false;
            c.material = next; return true;
        });
}
}
void RegisterSceneTypes(TypeRegistry& registry) {
    auto name = Type<NameComponent>("Name", "component.name");
    name.properties.push_back(Property<NameComponent, std::string>("name", "property.name", PropertyKind::String,
        [](const auto& c) { return c.name; }, [](auto& c, const std::string& v) {
            if(v.empty() || v.size() > 1024 || v.find('\0') != v.npos) return false;
            c.name = v; return true;
        }));
    registry.Register(std::move(name));
    auto transform = Type<TransformComponent>("Transform", "component.transform");
    transform.properties.push_back(Property<TransformComponent, std::array<float, 3>>("translation", "property.translation", PropertyKind::Vector3,
        [](const auto& c) { return std::array<float, 3>{c.translation.x, c.translation.y, c.translation.z}; },
        [](auto& c, const auto& v) { if(!Finite(v)) return false; c.translation = {v[0], v[1], v[2]}; return true; }));
    transform.properties.push_back(Property<TransformComponent, std::array<float, 3>>("scale", "property.scale", PropertyKind::Vector3,
        [](const auto& c) { return std::array<float, 3>{c.scale.x, c.scale.y, c.scale.z}; },
        [](auto& c, const auto& v) {
            if(!Finite(v) || std::ranges::any_of(v, [](float f) { return std::abs(f) < 1e-5f; })) return false;
            c.scale = {v[0], v[1], v[2]}; return true;
        }));
    transform.properties.push_back(Property<TransformComponent, std::array<float, 4>>("rotation", "property.rotation", PropertyKind::Rotation,
        [](const auto& c) { return std::array<float, 4>{c.rotation.x, c.rotation.y, c.rotation.z, c.rotation.w}; },
        [](auto& c, const auto& v) {
            float squared = 0;
            for(float f : v) { if(!std::isfinite(f)) return false; squared += f * f; }
            if(!std::isfinite(squared) || squared < 1e-12f) return false;
            const float factor = std::abs(squared - 1) < 1e-6f ? 1.0f : 1.0f / std::sqrt(squared);
            c.rotation = {v[0] * factor, v[1] * factor, v[2] * factor, v[3] * factor}; return true;
        }));
    registry.Register(std::move(transform));
    auto primitive = Type<PrimitiveRenderComponent>("PrimitiveRender", "component.primitive");
    primitive.add = [](Entity entity) { return entity.AddComponent<PrimitiveRenderComponent>() != nullptr; };
    auto shape = Property<PrimitiveRenderComponent, EnumValue>("shape", "property.primitiveShape", PropertyKind::Enum,
        [](const auto& c) { return EnumValue{static_cast<int>(c.shape)}; },
        [](auto& c, EnumValue v) {
            auto next = static_cast<PrimitiveShape>(v.value);
            if(!IsValidPrimitiveShape(next)) return false;
            c.shape = next; return true;
        });
    shape.enumValues = {{0,"primitive.asset"},{1,"primitive.cube"},{2,"primitive.sphere"},
        {3,"primitive.plane"},{4,"primitive.cylinder"},{5,"primitive.cone"}};
    primitive.properties.push_back(std::move(shape));
    const auto validPath = [](const std::string& v) {
        return v.size() <= 4096 && v.find('\0') == v.npos && v.find('\n') == v.npos && v.find('\r') == v.npos;
    };
    primitive.properties.push_back(Property<PrimitiveRenderComponent, std::string>("meshAsset", "property.mesh", PropertyKind::MeshAsset,
        [](const auto& c) { return c.meshAsset; }, [validPath](auto& c, const auto& v) { if(!validPath(v)) return false; c.meshAsset = v; return true; }));
    primitive.properties.push_back(Property<PrimitiveRenderComponent, int>("nodeIndex", "property.meshNode", PropertyKind::MeshNode,
        [](const auto& c) { return c.nodeIndex == PrimitiveRenderComponent::kAllNodes ? -1 : static_cast<int>(c.nodeIndex); },
        [](auto& c, int v) { if(v < -1) return false; c.nodeIndex = v == -1 ? PrimitiveRenderComponent::kAllNodes : static_cast<uint32_t>(v); return true; }));
    primitive.properties.push_back(Property<PrimitiveRenderComponent, std::string>("baseColorTexture", "property.baseColorTexture", PropertyKind::TextureAsset,
        [](const auto& c) { return c.baseColorTexture; }, [validPath](auto& c, const auto& v) { if(!validPath(v)) return false; c.baseColorTexture = v; return true; }));
    primitive.properties.push_back(Property<PrimitiveRenderComponent, bool>("enabled", "property.visible", PropertyKind::Bool,
        [](const auto& c) { return c.enabled; }, [](auto& c, bool v) { c.enabled = v; return true; }));
    primitive.properties.push_back(Property<PrimitiveRenderComponent, bool>("staticLighting", "property.staticLighting", PropertyKind::Bool,
        [](const auto& c) { return c.staticLighting; }, [](auto& c, bool v) { c.staticLighting = v; return true; }));
    primitive.properties.push_back(Property<PrimitiveRenderComponent, std::array<float, 4>>("tint", "property.tint", PropertyKind::Color,
        [](const auto& c) { return c.tint; }, [](auto& c, const auto& v) {
            if(!std::ranges::all_of(v, [](float f) { return std::isfinite(f); })) return false;
            c.tint = v; return true;
        }));
    primitive.properties.push_back(MaterialProperty("materialEnabled", "material.override", PropertyKind::Bool, &MaterialOverride::enabled));
    primitive.properties.push_back(MaterialProperty("materialSlot", "material.slot", PropertyKind::Int, &MaterialOverride::slot));
    primitive.properties.push_back(MaterialProperty("materialBaseColor", "material.baseColor", PropertyKind::Color, &MaterialOverride::baseColor));
    primitive.properties.push_back(MaterialProperty("materialMetallic", "material.metallic", PropertyKind::Float, &MaterialOverride::metallic));
    primitive.properties.push_back(MaterialProperty("materialRoughness", "material.roughness", PropertyKind::Float, &MaterialOverride::roughness));
    primitive.properties.push_back(MaterialProperty("materialNormalScale", "material.normalScale", PropertyKind::Float, &MaterialOverride::normalScale));
    primitive.properties.push_back(MaterialProperty("materialEmissive", "material.emissive", PropertyKind::Vector3, &MaterialOverride::emissive));
    primitive.properties.push_back(MaterialProperty("materialOcclusion", "material.occlusion", PropertyKind::Float, &MaterialOverride::occlusionStrength));
    registry.Register(std::move(primitive));
    auto camera = Type<CameraComponent>("Camera", "component.camera");
    camera.add = [](Entity entity) {
        auto* value = entity.AddComponent<CameraComponent>();
        if(!value) return false;
        value->primary = false;
        return true;
    };
    camera.properties.push_back(Property<CameraComponent, float>("fov", "property.fov", PropertyKind::Float,
        [](const auto& c) { return c.verticalFieldOfViewRadians; }, [](auto& c, float v) {
            if(!std::isfinite(v) || v <= 0 || v >= 3.14159265f) return false;
            c.verticalFieldOfViewRadians = v; return true;
        }));
    camera.properties.push_back(Property<CameraComponent, float>("near", "property.near", PropertyKind::Float,
        [](const auto& c) { return c.nearClip; }, [](auto& c, float v) {
            if(!std::isfinite(v) || v <= 0 || v >= c.farClip) return false;
            c.nearClip = v; return true;
        }));
    camera.properties.push_back(Property<CameraComponent, float>("far", "property.far", PropertyKind::Float,
        [](const auto& c) { return c.farClip; }, [](auto& c, float v) {
            if(!std::isfinite(v) || v <= c.nearClip) return false;
            c.farClip = v; return true;
        }));
    auto frustumSize = Property<CameraComponent, EnumValue>("frustumSize", "property.frustumSize", PropertyKind::Enum,
        [](const auto& c) { return EnumValue{c.smallFrustum ? 1 : 0}; },
        [](auto& c, EnumValue v) {
            if(v.value!=0 && v.value!=1) return false;
            c.smallFrustum=v.value==1; return true;
        });
    frustumSize.enumValues={{0,"camera.frustumNormal"},{1,"camera.frustumSmall"}};
    camera.properties.push_back(std::move(frustumSize));
    for(bool primary : {false, true}) {
        PropertyDescriptor flag{primary ? "primary" : "enabled", primary ? "property.primary" : "property.enabled", PropertyKind::Bool,
            [primary](Entity entity) -> std::optional<PropertyValue> {
                if(auto* c = entity.GetComponent<CameraComponent>()) return primary ? c->primary : c->enabled;
                return std::nullopt;
            }, [primary](Entity entity, const PropertyValue& value) {
                auto* c = entity.GetComponent<CameraComponent>(); const auto* v = std::get_if<bool>(&value);
                if(!c || !v) return false;
                bool conflict = false;
                if(*v && (primary ? c->enabled : c->primary)) entity.GetScene()->ForEach<CameraComponent>([&](Entity other, const auto& cameraValue) {
                    if(other.GetId() != entity.GetId() && cameraValue.enabled && cameraValue.primary) conflict = true;
                });
                if(conflict) return false;
                if(primary) c->primary = *v; else c->enabled = *v; return true;
            }, {}};
        camera.properties.push_back(std::move(flag));
    }
    registry.Register(std::move(camera));
    auto light = Type<DirectionalLightComponent>("DirectionalLight", "component.light");
    light.add = [](Entity entity) {
        auto* value = entity.AddComponent<DirectionalLightComponent>();
        if(!value) return false;
        entity.GetScene()->ForEach<DirectionalLightComponent>([&](Entity other, const auto& existing) {
            if(other.GetId() != entity.GetId() && existing.enabled) value->enabled = false;
        });
        return true;
    };
    light.properties.push_back(Property<DirectionalLightComponent, float>("intensity", "property.intensity", PropertyKind::Float,
        [](const auto& c) { return c.light.intensity; }, [](auto& c, float v) {
            if(!std::isfinite(v) || v < 0) return false; c.light.intensity = v; return true;
        }));
    light.properties.push_back(Property<DirectionalLightComponent, std::array<float, 3>>("color", "property.color", PropertyKind::Vector3,
        [](const auto& c) { return c.light.color; }, [](auto& c, const auto& v) {
            if(!Finite(v) || std::ranges::any_of(v, [](float f) { return f < 0; })) return false;
            c.light.color = v; return true;
        }));
    light.properties.push_back(Property<DirectionalLightComponent, std::array<float, 3>>("direction", "property.direction", PropertyKind::Vector3,
        [](const auto& c) { return c.light.direction; }, [](auto& c, const auto& v) {
            if(!Finite(v)) return false;
            const auto squared = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
            if(!std::isfinite(squared) || squared < 1e-12f) return false;
            c.light.direction = v; return true;
        }));
    light.properties.push_back({"enabled", "property.enabled", PropertyKind::Bool,
        [](Entity entity) -> std::optional<PropertyValue> {
            if(auto* c = entity.GetComponent<DirectionalLightComponent>()) return c->enabled;
            return std::nullopt;
        }, [](Entity entity, const PropertyValue& value) {
            auto* c = entity.GetComponent<DirectionalLightComponent>(); const auto* v = std::get_if<bool>(&value);
            if(!c || !v) return false;
            bool conflict = false;
            if(*v) entity.GetScene()->ForEach<DirectionalLightComponent>([&](Entity other, const auto& otherLight) {
                if(other.GetId() != entity.GetId() && otherLight.enabled) conflict = true;
            });
            if(conflict) return false; c->enabled = *v; return true;
        }, {}});
    registry.Register(std::move(light));
}
}
