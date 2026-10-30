#pragma once

#include "Foundation/Reflection/ReflectionValue.h"
#include "Runtime/Scene/Scene.h"
#include <set>

namespace NexusEngine {
// Component の生成・破棄を行う関数は、各パッチの生成コードから再登録する。
class ComponentReflection final {
public:
    struct Type {
        std::string id;
        std::type_index nativeType;
        ReflectionValue (*capture)(Scene&);
        void (*restore)(Scene&, const ReflectionValue&);
        bool (*contains)(Entity);
        ReflectionValue (*captureEntity)(Entity);
        void (*restoreEntity)(Entity, const ReflectionValue&);
    };
    template<class T> void Register() {
        const std::string id(ReflectionTraits<T>::Id);
        if(types_.contains(id)) throw std::runtime_error("Duplicate reflected component ID: " + id);
        types_.emplace(id, Type{id, typeid(T),
            [](Scene& scene) {
                ReflectionValue::Array values;
                scene.ForEach<T>([&](Entity entity, const T& component) {
                    values.emplace_back(ReflectionValue::Object{
                        {"index", CaptureReflection(entity.GetId().index)},
                        {"generation", CaptureReflection(entity.GetId().generation)},
                        {"value", CaptureReflection(component)}});
                });
                return ReflectionValue(std::move(values));
            },
            [](Scene& scene, const ReflectionValue& snapshot) {
                for(const auto& entry : std::get<ReflectionValue::Array>(snapshot.data)) {
                    EntityId id; RestoreReflection(entry.At("index"), id.index); RestoreReflection(entry.At("generation"), id.generation);
                    if(!scene.IsAlive(id) || scene.HasComponent<T>(id)) throw std::runtime_error("Invalid reinstanced entity");
                    T next{}; RestoreReflection(entry.At("value"), next);
                    if constexpr(requires { next.IsValid(); }) {
                        if(!next.IsValid()) throw std::runtime_error("Reinstanced component validation failed");
                    }
                    if(!scene.AddComponent<T>(id, std::move(next))) throw std::runtime_error("Component reinstancing failed");
                }
            },
            [](Entity entity) { return entity.HasComponent<T>(); },
            [](Entity entity) {
                const auto* value = entity.GetComponent<T>();
                if(!value) throw std::runtime_error("Missing component during level save");
                if constexpr(requires { value->IsValid(); }) {
                    if(!value->IsValid()) throw std::runtime_error("Component validation failed during level save");
                }
                return CaptureReflection(*value);
            },
            [](Entity entity, const ReflectionValue& snapshot) {
                if(entity.HasComponent<T>()) throw std::runtime_error("Duplicate level component");
                T value{}; RestoreReflection(snapshot, value);
                if constexpr(requires { value.IsValid(); }) {
                    if(!value.IsValid()) throw std::runtime_error("Component validation failed during level load");
                }
                // ReflectionCodecでも有限値・値域を検証する。追加フィールドは既定値を保つ。
                if(!entity.AddComponent<T>(std::move(value))) throw std::runtime_error("Cannot restore level component");
            }});
    }
    [[nodiscard]] ReflectionValue Capture(Scene& scene) const;
    void Restore(Scene& scene, const ReflectionValue& snapshot) const;
    const auto& Types() const noexcept { return types_; }
private:
    std::map<std::string, Type, std::less<>> types_;
};
void RegisterGeneratedComponents(ComponentReflection& registry);
}
