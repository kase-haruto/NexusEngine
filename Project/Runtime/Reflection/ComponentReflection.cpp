#include "ComponentReflection.h"

namespace NexusEngine {
ReflectionValue ComponentReflection::Capture(Scene& scene) const {
    std::set<std::type_index> registered;
    for(const auto& [id, type] : types_) registered.insert(type.nativeType);
    scene.ForEachStorageInfo([&](std::type_index type, size_t count) {
        if(count && !registered.contains(type))
            throw std::runtime_error("Unregistered component cannot be reinstanced: " + std::string(type.name()));
    });
    ReflectionValue::Object components;
    for(const auto& [id, type] : types_) components.emplace(id, type.capture(scene));
    const auto layout = scene.CaptureEntityLayout();
    return ReflectionValue(ReflectionValue::Object{
        {"components", ReflectionValue(std::move(components))},
        {"generations", CaptureReflection(layout.generations)}, {"alive", CaptureReflection(layout.alive)},
        {"freeIndices", CaptureReflection(layout.freeIndices)}});
}
void ComponentReflection::Restore(Scene& scene, const ReflectionValue& snapshot) const {
    const auto& components = std::get<ReflectionValue::Object>(snapshot.At("components").data);
    Scene::EntityLayout layout;
    RestoreReflection(snapshot.At("generations"), layout.generations); RestoreReflection(snapshot.At("alive"), layout.alive);
    RestoreReflection(snapshot.At("freeIndices"), layout.freeIndices);
    scene.RestoreEntityLayout(layout);
    for(const auto& [id, values] : components) {
        auto it = types_.find(id);
        if(it == types_.end()) {
            if(!std::get<ReflectionValue::Array>(values.data).empty())
                throw std::runtime_error("Reflected component type was removed: " + id);
            continue;
        }
        it->second.restore(scene, values);
    }
}
}
