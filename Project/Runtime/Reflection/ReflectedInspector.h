#pragma once
#include "TypeRegistry.h"
#include "Foundation/Reflection/ReflectionValue.h"

namespace NexusEngine {
// 専用 Inspector の検証・表示を優先し、生成情報は未登録の型とプロパティを補う。
template<class T> void RegisterReflectedInspector(TypeRegistry& registry, std::vector<std::string> sourceFiles = {}) {
    if(std::string_view(ReflectionTraits<T>::Label).empty()) return;
    TypeDescriptor descriptor{ReflectionTraits<T>::Id, ReflectionTraits<T>::Label,
        [](Entity entity) { return entity.HasComponent<T>(); }, {},
        [](Entity entity) { return entity.AddComponent<T>() != nullptr; }};
    descriptor.sourceFiles = std::move(sourceFiles);
    // 破棄関数をEngine側へコピーせず、同じ既存Storageの型付き削除だけを登録する。
    descriptor.remove = [](Entity entity) { return entity.RemoveComponent<T>(); };
    ReflectionTraits<T>::Visit([&]<class Member, class Owner>([[maybe_unused]] const char* id, [[maybe_unused]] Member Owner::* member) {
        static_assert(std::is_base_of_v<Owner, T> || std::is_same_v<Owner, T>);
        // 保存に必要だが直接編集させない内部値は、型側の明示的な公開方針に従う。
        if constexpr(requires { T::IsPropertyEditable(std::string_view{}); }) if(!T::IsPropertyEditable(id)) return;
        constexpr bool supported = std::is_same_v<Member, bool> || std::is_same_v<Member, int> ||
            std::is_same_v<Member, uint32_t> || std::is_same_v<Member, float> || std::is_same_v<Member, std::string> ||
            std::is_same_v<Member, std::array<float, 2>> || std::is_same_v<Member, std::array<float, 3>> ||
            std::is_same_v<Member, std::array<float, 4>>;
        if constexpr(supported) {
            using UiValue = std::conditional_t<std::is_same_v<Member, uint32_t>, int, Member>;
            constexpr auto kind = [] {
                if constexpr(std::is_same_v<UiValue, bool>) return PropertyKind::Bool;
                else if constexpr(std::is_same_v<UiValue, int>) return PropertyKind::Int;
                else if constexpr(std::is_same_v<UiValue, float>) return PropertyKind::Float;
                else if constexpr(std::is_same_v<UiValue, std::string>) return PropertyKind::String;
                else if constexpr(std::is_same_v<UiValue, std::array<float, 2>>) return PropertyKind::Vector2;
                else if constexpr(std::is_same_v<UiValue, std::array<float, 3>>) return PropertyKind::Vector3;
                else return PropertyKind::Vector4;
            }();
            descriptor.properties.push_back({id, id, kind,
                [member](Entity entity) -> std::optional<PropertyValue> {
                    const auto* value = entity.GetComponent<T>();
                    if(!value) return {};
                    if constexpr(std::is_same_v<Member, uint32_t>) {
                        if(value->*member > static_cast<uint32_t>(INT32_MAX)) return {};
                    }
                    return PropertyValue(static_cast<UiValue>(value->*member));
                },
                [member](Entity entity, const PropertyValue& input) {
                    auto* value = entity.GetComponent<T>(); const auto* converted = std::get_if<UiValue>(&input);
                    if(!value || !converted) return false;
                    if constexpr(std::is_same_v<Member, uint32_t>) { if(*converted < 0) return false; }
                    if constexpr(std::is_same_v<Member, float>) { if(!std::isfinite(*converted)) return false; }
                    auto previous = value->*member;
                    value->*member = static_cast<Member>(*converted);
                    if constexpr(requires { value->IsValid(); }) {
                        if(!value->IsValid()) { value->*member = std::move(previous); return false; }
                    }
                    return true;
                }, {}});
        }
    });
    registry.MergeGenerated(std::move(descriptor));
}
void RegisterGeneratedInspectorTypes(TypeRegistry& registry);
}
