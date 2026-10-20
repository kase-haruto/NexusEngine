#include "TypeRegistry.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
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
        [](auto& c, const auto& v) { if(!Finite(v)) return false; c.scale = {v[0], v[1], v[2]}; return true; }));
    // QuaternionはVector4として公開しない。正規化と専用Drawerの追加時に登録する。
    registry.Register(std::move(transform));
    auto primitive = Type<PrimitiveRenderComponent>("PrimitiveRender", "component.primitive");
    primitive.properties.push_back(Property<PrimitiveRenderComponent, bool>("enabled", "property.visible", PropertyKind::Bool,
        [](const auto& c) { return c.enabled; }, [](auto& c, bool v) { c.enabled = v; return true; }));
    primitive.properties.push_back(Property<PrimitiveRenderComponent, std::array<float, 4>>("tint", "property.tint", PropertyKind::Vector4,
        [](const auto& c) { return c.tint; }, [](auto& c, const auto& v) {
            if(!std::ranges::all_of(v, [](float f) { return std::isfinite(f); })) return false;
            c.tint = v; return true;
        }));
    registry.Register(std::move(primitive));
    auto camera = Type<CameraComponent>("Camera", "component.camera");
    camera.properties.push_back(Property<CameraComponent, float>("fov", "property.fov", PropertyKind::Float,
        [](const auto& c) { return c.verticalFieldOfViewRadians; }, [](auto& c, float v) {
            if(!std::isfinite(v) || v <= 0 || v >= 3.14159265f) return false;
            c.verticalFieldOfViewRadians = v; return true;
        }));
    registry.Register(std::move(camera));
}
}
