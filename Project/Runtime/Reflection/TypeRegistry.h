#pragma once
#include "Runtime/Scene/Scene.h"
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <variant>

namespace NexusEngine {
struct EnumValue { int value = 0; friend bool operator==(EnumValue, EnumValue) = default; };
using PropertyValue = std::variant<bool, int, float, std::string, std::array<float, 2>, std::array<float, 3>, std::array<float, 4>, EnumValue>;
enum class PropertyKind { Bool, Int, Float, String, Vector2, Vector3, Vector4, Enum, Rotation, Color };
/*-----------------------------------------------------------------------------------------
 * PropertyDescriptor / TypeRegistry
 * - Runtime Componentの型と値アクセスを登録するUI非依存Reflection
 * - 生のComponentポインタを保持せず、アクセスのたびにEntityから解決する
 *---------------------------------------------------------------------------------------*/
struct PropertyDescriptor {
    std::string id; //< 通信やUndoにも使える安定プロパティID
    std::string labelKey; //< 表示metadata。Runtimeは翻訳を行わない
    PropertyKind kind; //< PropertyValueの型と一致する
    std::function<std::optional<PropertyValue>(Entity)> get; //< 現在値のコピー
    std::function<bool(Entity, const PropertyValue&)> set; //< 検証付き更新
    std::vector<std::pair<int, std::string>> enumValues; //< 値と表示キー
};
struct TypeDescriptor {
    std::string id, labelKey; //< 安定型IDと表示metadata
    std::function<bool(Entity)> contains; //< Component存在確認
    std::vector<PropertyDescriptor> properties; //< 順序付き公開プロパティ
};
class TypeRegistry final {
public:
    /** \brief 型を登録する。既存のIDは上書きしない */
    bool Register(TypeDescriptor type) { auto id = type.id; return types_.emplace(std::move(id), std::move(type)).second; }
    /** \brief 安定IDで型情報を取得する */
    const TypeDescriptor* Find(std::string_view id) const {
        auto it = types_.find(id); return it == types_.end() ? nullptr : &it->second;
    }
    const auto& Types() const { return types_; }
private:
    std::map<std::string, TypeDescriptor, std::less<>> types_; //< 初期登録後は読み取り専用
};
/** \brief 既存Componentを型記述へ接続する。Editor UIは具体型を知らない */
void RegisterSceneTypes(TypeRegistry& registry);
}
