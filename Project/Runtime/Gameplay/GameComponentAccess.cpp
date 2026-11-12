#include "GameComponentAccess.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Reflection/TypeRegistry.h"
#include "Runtime/Scene/Components/GameComponentData.h"
#include <Windows.h>
#include <cmath>
#include <cstring>
#include <vector>
#include <array>
#include <string>
#include <string_view>
#include <type_traits>

namespace NexusEngine {
namespace {
/** \brief 境界の値をそのままPropertyへ渡さず、UTF-8と制御文字を検査する */
bool ValidText(std::string_view text, bool value = false) {
    for(unsigned char c : text) if(c < 32 && !(value && (c == '\n' || c == '\r' || c == '\t'))) return false;
    return text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0;
}
/** \brief 未使用fieldも検査し、別kindを同じbyte配置で解釈する曖昧な入力を拒否する */
bool ValidValue(const NexusComponentValueV1& value, std::string_view text) {
    if(value.reserved || value.stringLength != text.size()) return false;
    uint32_t numbers = 0;
    switch(value.kind) {
        case NEXUS_COMPONENT_BOOL: if(value.integer != 0 && value.integer != 1) return false; break;
        case NEXUS_COMPONENT_INT: case NEXUS_COMPONENT_ENUM: break;
        case NEXUS_COMPONENT_FLOAT: numbers = 1; break;
        case NEXUS_COMPONENT_VECTOR2: numbers = 2; break;
        case NEXUS_COMPONENT_VECTOR3: numbers = 3; break;
        case NEXUS_COMPONENT_VECTOR4: numbers = 4; break;
        case NEXUS_COMPONENT_STRING: if(!ValidText(text, true)) return false; break;
        default: return false;
    }
    if(value.kind != NEXUS_COMPONENT_STRING && !text.empty()) return false;
    if(value.kind != NEXUS_COMPONENT_BOOL && value.kind != NEXUS_COMPONENT_INT && value.kind != NEXUS_COMPONENT_ENUM && value.integer) return false;
    for(uint32_t index = 0; index < 4; ++index)
        if(!std::isfinite(value.number[index]) || (index >= numbers && value.number[index] != 0)) return false;
    return true;
}
/** \brief 共通Propertyの意味を維持し、Enum / Color / Asset pathも既存setterで検証する */
uint32_t Kind(PropertyKind kind) {
    switch(kind) {
        case PropertyKind::Bool: return NEXUS_COMPONENT_BOOL;
        case PropertyKind::Int: case PropertyKind::MeshNode: return NEXUS_COMPONENT_INT;
        case PropertyKind::Float: return NEXUS_COMPONENT_FLOAT;
        case PropertyKind::String: case PropertyKind::MeshAsset: case PropertyKind::TextureAsset: return NEXUS_COMPONENT_STRING;
        case PropertyKind::Vector2: return NEXUS_COMPONENT_VECTOR2;
        case PropertyKind::Vector3: return NEXUS_COMPONENT_VECTOR3;
        case PropertyKind::Vector4: case PropertyKind::Rotation: case PropertyKind::Color: return NEXUS_COMPONENT_VECTOR4;
        case PropertyKind::Enum: return NEXUS_COMPONENT_ENUM;
    }
    return 0;
}
PropertyValue Decode(const NexusComponentValueV1& value, std::string_view text) {
    switch(value.kind) {
        case NEXUS_COMPONENT_BOOL: return value.integer != 0;
        case NEXUS_COMPONENT_INT: return static_cast<int>(value.integer);
        case NEXUS_COMPONENT_ENUM: return EnumValue{value.integer};
        case NEXUS_COMPONENT_FLOAT: return value.number[0];
        case NEXUS_COMPONENT_STRING: return std::string(text);
        case NEXUS_COMPONENT_VECTOR2: return std::array<float, 2>{value.number[0], value.number[1]};
        case NEXUS_COMPONENT_VECTOR3: return std::array<float, 3>{value.number[0], value.number[1], value.number[2]};
        default: return std::array<float, 4>{value.number[0], value.number[1], value.number[2], value.number[3]};
    }
}
/** \brief getterの値も境界表現へコピーし、内部PropertyValueやstringの所有権を公開しない */
bool Encode(const PropertyValue& value, uint32_t kind, NexusComponentValueV1& output, std::string& text) {
    output = {}; output.kind = kind;
    return std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr(std::is_same_v<T, bool>) { if(kind != NEXUS_COMPONENT_BOOL) return false; output.integer = item ? 1 : 0; }
        else if constexpr(std::is_same_v<T, int>) { if(kind != NEXUS_COMPONENT_INT) return false; output.integer = item; }
        else if constexpr(std::is_same_v<T, EnumValue>) { if(kind != NEXUS_COMPONENT_ENUM) return false; output.integer = item.value; }
        else if constexpr(std::is_same_v<T, float>) { if(kind != NEXUS_COMPONENT_FLOAT) return false; output.number[0] = item; }
        else if constexpr(std::is_same_v<T, std::string>) {
            if(kind != NEXUS_COMPONENT_STRING || item.size() > 3072) return false;
            text = item; output.stringLength = static_cast<uint32_t>(text.size());
        } else {
            constexpr auto count = std::tuple_size_v<T>;
            if(kind != NEXUS_COMPONENT_VECTOR2 + count - 2) return false;
            for(size_t index = 0; index < count; ++index) output.number[index] = item[index];
        }
        return ValidValue(output, text);
    }, value);
}
NexusGameStatus Copy(const void* data, uint32_t size, uint8_t* output, uint32_t capacity, uint32_t* length) {
    *length = size;
    if(capacity < size) return NEXUS_GAME_BUFFER_TOO_SMALL;
    if(size) std::memcpy(output, data, size);
    return NEXUS_GAME_OK;
}
}
NexusGameStatus InvokeGameTransformBatch(Level& level, uint64_t world, NexusEntityHandle target,
    NexusGameBytes request, uint8_t* response, uint32_t capacity, uint32_t* length) noexcept {
    if(!length) return NEXUS_GAME_INVALID_ARGUMENT;
    *length = 0;
    try {
        if(target.world || target.index || target.generation || !request.data || request.length < sizeof(NexusTransformBatchV1) ||
            request.length > 4096 || capacity > 4096 || (capacity && !response)) return NEXUS_GAME_INVALID_ARGUMENT;
        NexusTransformBatchV1 header{}; std::memcpy(&header, request.data, sizeof(header));
        if(header.count > 64 || (header.operation != 1 && header.operation != 2)) return NEXUS_GAME_INVALID_ARGUMENT;
        const size_t elementSize = header.operation == 1 ? sizeof(NexusEntityHandle) : sizeof(NexusTransformUpdateV1);
        if(sizeof(header) + header.count * elementSize != request.length) return NEXUS_GAME_INVALID_ARGUMENT;
        std::array<NexusLocalTransform, 64> values{};
        std::array<TransformComponent*, 64> components{};
        for(uint32_t index = 0; index < header.count; ++index) {
            NexusTransformUpdateV1 update{};
            // 呼出側のalignmentへ依存せず、各入力をNative所有の一時DTOへコピーする。
            std::memcpy(&update, request.data + sizeof(header) + index * elementSize, elementSize);
            if(update.entity.world != world) return NEXUS_GAME_INVALID_HANDLE;
            auto entity = level.GetScene()->GetEntity({update.entity.index, update.entity.generation});
            auto* component = entity.GetComponent<TransformComponent>();
            if(!component) return NEXUS_GAME_INVALID_HANDLE;
            components[index] = component;
            if(header.operation == 1) {
                values[index] = {{component->translation.x, component->translation.y, component->translation.z},
                    {component->rotation.x, component->rotation.y, component->rotation.z, component->rotation.w},
                    {component->scale.x, component->scale.y, component->scale.z}};
            } else {
                // 全要素のfinite / Quaternion検査が終わるまでWorldを変更しない。
                for(float item : update.transform.translation) if(!std::isfinite(item)) return NEXUS_GAME_INVALID_ARGUMENT;
                for(float item : update.transform.scale) if(!std::isfinite(item)) return NEXUS_GAME_INVALID_ARGUMENT;
                float squared = 0;
                for(float item : update.transform.rotation) { if(!std::isfinite(item)) return NEXUS_GAME_INVALID_ARGUMENT; squared += item * item; }
                if(!std::isfinite(squared) || squared < 1.0e-12f) return NEXUS_GAME_INVALID_ARGUMENT;
                const auto inverse = 1.0f / std::sqrt(squared);
                for(auto& item : update.transform.rotation) item *= inverse;
                values[index] = update.transform;
            }
        }
        if(header.operation == 1) return Copy(values.data(), static_cast<uint32_t>(header.count * sizeof(NexusLocalTransform)), response, capacity, length);
        // このscope内では構造変更や外部callbackを呼ばない。検証済みの既存Componentだけを更新する。
        for(uint32_t index = 0; index < header.count; ++index) {
            const auto& value = values[index]; auto& component = *components[index];
            component.translation = {value.translation[0], value.translation[1], value.translation[2]};
            component.rotation = {value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3]};
            component.scale = {value.scale[0], value.scale[1], value.scale[2]};
        }
        return NEXUS_GAME_OK;
    } catch(...) { *length = 0; return NEXUS_GAME_FAILED; }
}
NexusGameStatus InvokeGameComponent(Level& level, const TypeRegistry& types, uint64_t world, NexusEntityHandle target,
    NexusGameBytes request, uint8_t* response, uint32_t capacity, uint32_t* length) noexcept {
    if(!length) return NEXUS_GAME_INVALID_ARGUMENT;
    *length = 0;
    try {
        // requestは非整列のManaged byte[]でもよい。直接reinterpretせず固定headerをコピーする。
        if(request.length < sizeof(NexusComponentRequestV1) || request.length > 4096 || !request.data || capacity > 4096 || (capacity && !response)) return NEXUS_GAME_INVALID_ARGUMENT;
        NexusComponentRequestV1 header{}; std::memcpy(&header, request.data, sizeof(header));
        if(header.reserved || !header.typeLength || header.typeLength > 512 || header.propertyLength > 512 || header.value.stringLength > 3072 ||
            static_cast<uint64_t>(sizeof(header)) + header.typeLength + header.propertyLength + header.value.stringLength != request.length)
            return NEXUS_GAME_INVALID_ARGUMENT;
        const auto* bytes = reinterpret_cast<const char*>(request.data + sizeof(header));
        const std::string_view typeId(bytes, header.typeLength), propertyId(bytes + header.typeLength, header.propertyLength);
        const std::string_view text(bytes + header.typeLength + header.propertyLength, header.value.stringLength);
        if(!ValidText(typeId) || !ValidText(propertyId)) return NEXUS_GAME_INVALID_ARGUMENT;
        // Scriptの存在照会は安全だが、instance / attachmentの編集は既存専用APIだけが管理する。
        if((typeId == "Script" && header.operation != NEXUS_COMPONENT_HAS && header.operation != NEXUS_COMPONENT_QUERY) ||
            typeId.starts_with("ManagedScript:")) return NEXUS_GAME_INVALID_ARGUMENT;
        const auto* type = types.Find(typeId);
        if(!type || !type->contains) return NEXUS_GAME_INVALID_HANDLE;
        const bool attached = header.operation == NEXUS_COMPONENT_READ_ATTACHED || header.operation == NEXUS_COMPONENT_WRITE_ATTACHED;
        const bool write = header.operation == NEXUS_COMPONENT_WRITE || header.operation == NEXUS_COMPONENT_WRITE_ATTACHED;
        const bool property = attached || header.operation == NEXUS_COMPONENT_READ || header.operation == NEXUS_COMPONENT_WRITE;
        if(property != !propertyId.empty()) return NEXUS_GAME_INVALID_ARGUMENT;
        if(!write) {
            const NexusComponentValueV1 empty{};
            if(std::memcmp(&header.value, &empty, sizeof(empty))) return NEXUS_GAME_INVALID_ARGUMENT;
        } else if(!ValidValue(header.value, text)) return NEXUS_GAME_INVALID_ARGUMENT;
        if(header.operation == NEXUS_COMPONENT_QUERY) {
            if(target.world || target.index || target.generation || !header.limit || header.limit > 255 || header.offset == UINT32_MAX) return NEXUS_GAME_INVALID_ARGUMENT;
            std::vector<NexusEntityHandle> entities; entities.reserve(header.limit);
            // 既存Sceneの生存Entityを反復し、別Query用ECSや永続索引を持たない。
            const auto next = level.GetScene()->ForEachEntityFrom(header.offset, [&](Entity entity) {
                if(!type->contains(entity)) return true;
                const auto id = entity.GetId(); entities.push_back({world, id.index, id.generation});
                return entities.size() < header.limit;
            });
            NexusComponentQueryV1 page{static_cast<uint32_t>(entities.size()), next};
            *length = static_cast<uint32_t>(sizeof(page) + entities.size() * sizeof(NexusEntityHandle));
            if(capacity < *length) return NEXUS_GAME_BUFFER_TOO_SMALL;
            std::memcpy(response, &page, sizeof(page));
            if(!entities.empty()) std::memcpy(response + sizeof(page), entities.data(), entities.size() * sizeof(NexusEntityHandle));
            return NEXUS_GAME_OK;
        }
        if((!attached && (header.offset || header.limit)) || target.world != world) return NEXUS_GAME_INVALID_ARGUMENT;
        auto entity = level.GetScene()->GetEntity({target.index, target.generation});
        if(!entity.IsAlive()) return NEXUS_GAME_INVALID_HANDLE;
        if(header.operation == NEXUS_COMPONENT_HAS) {
            const uint32_t present = type->contains(entity) ? 1u : 0u;
            return Copy(&present, sizeof(present), response, capacity, length);
        }
        if(header.operation == NEXUS_COMPONENT_ATTACHMENT || attached) {
            if(!type->valueBacked || !type->contains(entity)) return NEXUS_GAME_INVALID_HANDLE;
            auto* data = entity.GetComponent<GameComponentData>(); if(!data) return NEXUS_GAME_INVALID_HANDLE;
            if(attached) {
                const auto expected = static_cast<uint64_t>(header.offset) | (static_cast<uint64_t>(header.limit) << 32);
                if(!expected || data->FindAttachment(typeId) != expected) return NEXUS_GAME_INVALID_HANDLE;
            } else {
                *length = sizeof(uint64_t); if(capacity < *length) return NEXUS_GAME_BUFFER_TOO_SMALL;
                auto token = data->FindAttachment(typeId);
                if(!token) {
                    token = NextGameComponentAttachment(); if(!token || !data->SetAttachment(std::string(typeId), token)) return NEXUS_GAME_FAILED;
                }
                return Copy(&token, sizeof(token), response, capacity, length);
            }
        }
        if(header.operation == NEXUS_COMPONENT_ADD || header.operation == NEXUS_COMPONENT_REMOVE) {
            // 必須Componentと階層の削除はEntity・永続ID・Transformの整合性を壊すため公開しない。
            if(typeId == "Name" || typeId == "Transform" || typeId == "Hierarchy" || typeId == "PersistentId") return NEXUS_GAME_INVALID_ARGUMENT;
            if(header.operation == NEXUS_COMPONENT_ADD) {
                if(type->contains(entity)) return NEXUS_GAME_OK;
                return type->add && type->add(entity) ? NEXUS_GAME_OK : NEXUS_GAME_FAILED;
            }
            if(!type->contains(entity)) return NEXUS_GAME_INVALID_HANDLE;
            return type->remove && type->remove(entity) ? NEXUS_GAME_OK : NEXUS_GAME_FAILED;
        }
        if(!property) return NEXUS_GAME_INVALID_ARGUMENT;
        if(!type->contains(entity)) return NEXUS_GAME_INVALID_HANDLE;
        for(const auto& descriptor : type->properties) if(descriptor.id == propertyId) {
            const auto kind = Kind(descriptor.kind);
            if(write) {
                if(!kind || kind != header.value.kind || !descriptor.set) return NEXUS_GAME_INVALID_ARGUMENT;
                return descriptor.set(entity, Decode(header.value, text)) ? NEXUS_GAME_OK : NEXUS_GAME_INVALID_ARGUMENT;
            }
            const auto value = descriptor.get ? descriptor.get(entity) : std::nullopt;
            if(!value) return NEXUS_GAME_INVALID_HANDLE;
            NexusComponentValueV1 output{}; std::string content;
            if(!Encode(*value, kind, output, content)) return NEXUS_GAME_FAILED;
            *length = static_cast<uint32_t>(sizeof(output) + content.size());
            if(capacity < *length) return NEXUS_GAME_BUFFER_TOO_SMALL;
            std::memcpy(response, &output, sizeof(output));
            if(!content.empty()) std::memcpy(response + sizeof(output), content.data(), content.size());
            return NEXUS_GAME_OK;
        }
        return NEXUS_GAME_INVALID_HANDLE;
    } catch(...) { *length = 0; return NEXUS_GAME_FAILED; }
}
}
