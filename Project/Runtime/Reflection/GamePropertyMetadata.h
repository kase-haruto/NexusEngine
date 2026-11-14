#pragma once
#include "TypeRegistry.h"
#include "Runtime/Scene/EntityReference.h"
#include "Runtime/Project/ProjectJson.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

// C# / Native Componentの値Metadataで同じ保存形式・型・互換性検査を利用する。
namespace NexusEngine::GamePropertyMetadata {
using ProjectJson::Json;
/*-----------------------------------------------------------------------------------------
 * PropertyMetadata / TypeMetadata
 * - CLR参照を含まないimmutableな永続契約。accessorは共有所有してCatalog破棄後も安全にする
 *---------------------------------------------------------------------------------------*/
struct PropertyMetadata {
    std::string id, label, kind; //< 永続ID・表示名・保存kind。言語のメンバ配置へ依存しない
    std::vector<std::string> names; //< 正規IDを先頭とする互換名の探索順序
    PropertyKind valueKind; //< 共通Drawerが扱う値型
    PropertyValue initial; //< Exporterが取得した既定値のコピー
};
struct TypeMetadata {
    std::string id, label; //< Script IDとFullName表示
    int version; //< 保存契約の識別番号。自動migrationは実行しない
    std::vector<std::string> names; //< 正規IDと旧ID・FullName
    std::vector<PropertyMetadata> properties; //< Catalog内だけで所有する公開契約
};
/** \brief 言語境界のID上限を共通化する。JSONのキーとしてのみ使用しパスへ転用しない */
inline std::string Id(std::string value) {
    if(value.empty() || value.size() > 512) throw std::runtime_error("Invalid Script metadata ID.");
    for(unsigned char ch : value) if(ch < 32 || ch == 127) throw std::runtime_error("Control character in metadata ID.");
    return value;
}
/** \brief 曖昧なAliasを拒否し、正規ID優先の探索順序を作る */
inline std::vector<std::string> Names(const Json& aliases, const std::string& id) {
    if(!aliases.is_array() || aliases.size() > 256) throw std::runtime_error("Invalid aliases.");
    std::vector<std::string> result{id};
    for(const auto& alias : aliases) {
        auto name = Id(alias.get<std::string>());
        if(std::ranges::find(result, name) != result.end()) throw std::runtime_error("Duplicate alias.");
        result.push_back(std::move(name));
    }
    return result;
}
/** \brief 不明な型・Property・kindは保持し、既知の構造だけ検証する */
inline Json State(std::string_view text) {
    if(text.empty()) return Json{{"schemaVersion", 1}, {"types", Json::object()}};
    if(text.size() > 262144) throw std::runtime_error("Script state exceeds 256 KiB.");
    std::vector<std::set<std::string>> keys;
    auto validate = [&](int depth, Json::parse_event_t event, Json& value) {
        if(depth > 16) throw std::runtime_error("Script state nesting limit.");
        if(event == Json::parse_event_t::object_start) keys.emplace_back();
        if(event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second) throw std::runtime_error("Duplicate state key.");
        if(event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    };
    auto root = Json::parse(text, validate);
    ProjectJson::Keys(root, {"schemaVersion", "types"});
    if(!root.at("schemaVersion").is_number_integer() || root.at("schemaVersion") != 1 || !root.at("types").is_object()) throw std::runtime_error("Invalid Script state schema.");
    for(const auto& [name, scope] : root.at("types").items()) {
        Id(name); ProjectJson::Keys(scope, {"version", "values"});
        if(!scope.at("version").is_number_integer() || scope.at("version") <= 0 || scope.at("version") > INT32_MAX || !scope.at("values").is_object()) throw std::runtime_error("Invalid type state.");
        for(const auto& [property, kinds] : scope.at("values").items()) {
            Id(property); if(!kinds.is_object()) throw std::runtime_error("Invalid Property state.");
            for(const auto& [kind, value] : kinds.items()) { Id(kind); static_cast<void>(value); }
        }
    }
    return root;
}
/** \brief 既知ID・Aliasの新しい保存Versionを古いMetadataで上書きしない。未知の型は保持する */
inline void CompatibleState(const Json& root, const TypeMetadata& type) {
    const auto& types = root.at("types");
    for(const auto& name : type.names) if(types.contains(name) && types.at(name).at("version").get<int>() > type.version)
        throw std::runtime_error("Saved Script version is newer than the loaded metadata: " + type.id + ". Load compatible Game code and metadata.");
}
/** \brief 浮動小数点への変換後にも有限性を確認する */
inline float Number(const Json& value) {
    if(!value.is_number()) throw std::runtime_error("Expected number.");
    auto result = value.get<float>(); if(!std::isfinite(result)) throw std::runtime_error("Nonfinite property."); return result;
}
/** \brief 次元数と全要素を検証して共通Vector値へ変換する */
template<size_t Size> PropertyValue Vector(const Json& value) {
    if(!value.is_array() || value.size() != Size) throw std::runtime_error("Invalid Vector property.");
    std::array<float, Size> result{};
    for(size_t i = 0; i < Size; ++i) result[i] = Number(value[i]);
    return result;
}
/** \brief 保存kindとJSON値の組を、型変換による情報損失を避けて検証する */
inline PropertyValue Value(const std::string& kind, const Json& value) {
    if((kind == "EntityReference" || kind == "ComponentReference") && value.is_string()) {
        auto text = value.get<std::string>();
        if(!ValidateReferenceValue(text, kind == "ComponentReference")) {
            throw std::runtime_error("Invalid persistent reference.");
        }
        return text;
    }
    if(kind == "Bool" && value.is_boolean()) return value.get<bool>();
    if(kind == "Int" && value.is_number_integer() && value >= INT32_MIN && value <= INT32_MAX) return value.get<int>();
    if(kind == "Float") return Number(value);
    if(kind == "String" && value.is_string()) {
        auto result = value.get<std::string>();
        if(result.size() > 16384 || result.find('\0') != result.npos) throw std::runtime_error("Invalid String property.");
        return result;
    }
    if(kind == "Vector2") return Vector<2>(value);
    if(kind == "Vector3") return Vector<3>(value);
    if(kind == "Vector4") return Vector<4>(value);
    throw std::runtime_error("Unsupported Property kind / value.");
}
/** \brief 対応する共通PropertyKindだけを公開する */
inline PropertyKind Kind(const std::string& kind) {
    if(kind == "EntityReference") {
        return PropertyKind::EntityReference;
    }
    if(kind == "ComponentReference") {
        return PropertyKind::ComponentReference;
    }
    if(kind == "Bool") return PropertyKind::Bool; if(kind == "Int") return PropertyKind::Int;
    if(kind == "Float") return PropertyKind::Float; if(kind == "String") return PropertyKind::String;
    if(kind == "Vector2") return PropertyKind::Vector2; if(kind == "Vector3") return PropertyKind::Vector3;
    if(kind == "Vector4") return PropertyKind::Vector4; throw std::runtime_error("Unsupported Property kind.");
}

}
