#pragma once
#include "Runtime/Scene/Scene.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <variant>

namespace NexusEngine {
struct EnumValue { int value = 0; friend bool operator==(EnumValue, EnumValue) = default; };
using PropertyValue = std::variant<bool, int, float, std::string, std::array<float, 2>, std::array<float, 3>, std::array<float, 4>, EnumValue>;
enum class PropertyKind { Bool, Int, Float, String, Vector2, Vector3, Vector4, Enum, Rotation, Color, MeshAsset, TextureAsset, MeshNode, ColliderShape, CollisionLayer, LayerMask, EntityReference, ComponentReference };
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
    std::function<std::string(Entity)> unavailableReason; //< 取得失敗時の値Metadata由来の理由。UIやManaged objectは所有しない
    std::function<std::optional<PropertyValue>(Entity,PersistentId)> getInstance; //< 同型複数instanceを永続IDで解決する任意accessor
    std::function<bool(Entity,PersistentId,const PropertyValue&)> setInstance; //< 対象instanceだけを検証付きで更新する
};
struct TypeDescriptor {
    std::string id, labelKey; //< 安定型IDと表示metadata
    std::function<bool(Entity)> contains; //< Component存在確認
    std::vector<PropertyDescriptor> properties; //< 順序付き公開プロパティ
    std::function<bool(Entity)> add; //< 未追加Entityへ既定値で追加する。必須Componentは公開しない
    std::vector<std::string> sourceFiles; //< 編集用UTF-8パス。型IDやLevel保存へ使わず、Editorで現在Project内か再検証する
    std::function<bool(Entity)> remove; //< 明示的な構造変更窓口。必須型とScriptは各専用APIが制約を検証する
    bool valueBacked = false; //< DLL由来の値Component。attachment検証付き操作はこの格納方式だけに公開する
    std::function<std::vector<PersistentId>(Entity)> instances; //< 共通Detailsへ渡す順序付きinstance ID。実行objectは公開しない
    std::function<bool(Entity)> addInstance; //< 同型の既存instanceがあっても独立した値を追加する明示操作
    std::function<bool(Entity,PersistentId)> removeInstance; //< 永続IDの一つだけを解除する構造変更
};
class TypeRegistry final {
public:
    /** \brief 型を登録する。既存のIDは上書きしない */
    bool Register(TypeDescriptor type) { auto id = type.id; return types_.emplace(std::move(id), std::move(type)).second; }
    /** 生成情報を補完する。専用 UI の同一 ID の getter/setter は置換しない。 */
    void MergeGenerated(TypeDescriptor type) {
        auto found = types_.find(type.id);
        if(found == types_.end()) { Register(std::move(type)); return; }
        // 専用Inspectorのaccessorを維持し、生成器だけが知るソース対応を補完する。
        if(!found->second.remove) found->second.remove = std::move(type.remove);
        for(auto& source : type.sourceFiles)
            if(std::ranges::find(found->second.sourceFiles, source) == found->second.sourceFiles.end())
                found->second.sourceFiles.push_back(std::move(source));
        for(auto& property : type.properties) {
            const bool exists = std::ranges::any_of(found->second.properties, [&](const auto& current) { return current.id == property.id; });
            if(!exists) found->second.properties.push_back(std::move(property));
        }
    }
    /** \brief 安定IDで型情報を取得する */
    const TypeDescriptor* Find(std::string_view id) const {
        auto it = types_.find(id); return it == types_.end() ? nullptr : &it->second;
    }
    const auto& Types() const { return types_; }
    /** \brief 所有Catalogの差替え時だけ削除する。呼出側がframe境界で編集履歴とDescriptor参照を先に解除する */
    bool Unregister(std::string_view id) { auto found = types_.find(id); if(found == types_.end()) return false; types_.erase(found); return true; }
private:
    std::map<std::string, TypeDescriptor, std::less<>> types_; //< 初期登録後は読み取り専用
};
/** \brief 既存Componentを型記述へ接続する。Editor UIは具体型を知らない */
void RegisterSceneTypes(TypeRegistry& registry);
}
