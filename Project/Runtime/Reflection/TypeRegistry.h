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
    bool animatable = false; //< 新keyframe bindingでの明示許可。安定IDと型検査も別途必須
};
/* 表示用の値ヒントだけを保持する。Runtimeは翻訳・画像解決・ImGuiを参照しない。 */
struct ComponentEditorMetadata {
    std::string displayName; //< 空なら既存labelKeyを使う。翻訳キーまたは表示文字列
    std::string category; //< 拡張可能な安定カテゴリID。未指定はOther
    std::string description; //< 翻訳キーまたは説明文
    std::string icon; //< Editorの既存IconRegistryで解決する抽象ID
    std::string origin; //< Engine / Game / Plugin / Customなどの表示用分類
    std::vector<std::string> keywords; //< 型名以外の検索語。保存IDへ影響させない
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
    ComponentEditorMetadata editor; //< 登録時に提供するUI非依存の値Metadata
    std::function<std::string(Entity)> addUnavailableReason; //< World全体の追加制約。空なら追加候補
};
class TypeRegistry final {
public:
    /** \brief 型を登録する。既存のIDは上書きしない */
    bool Register(TypeDescriptor type);
    /** 生成情報を補完する。専用UIのgetter/setterと追加制約を維持する。 */
    void MergeGenerated(TypeDescriptor type);
    /** 登録・解除・補完の成功時だけ変わる。Registryのcopy/交換にも対応する。 */
    uint64_t Generation() const noexcept;
    /** \brief 安定IDで型情報を取得する */
    const TypeDescriptor* Find(std::string_view id) const {
        auto it = types_.find(id); return it == types_.end() ? nullptr : &it->second;
    }
    const auto& Types() const { return types_; }
    /** \brief 所有Catalogの差替え時だけ削除する。呼出側がframe境界で編集履歴とDescriptor参照を先に解除する */
    bool Unregister(std::string_view id);
private:
    uint64_t generation_ = 0; //< 値snapshotの変更世代。Editorキャッシュの失効に使う
    std::map<std::string, TypeDescriptor, std::less<>> types_; //< 初期登録後は読み取り専用
};
/** \brief 既存Componentを型記述へ接続する。Editor UIは具体型を知らない */
void RegisterSceneTypes(TypeRegistry& registry);
}
