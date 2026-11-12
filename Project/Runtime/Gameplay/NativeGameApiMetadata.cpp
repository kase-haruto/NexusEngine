#include "NativeGameApiMetadata.h"
#include <set>
#include <stdexcept>

namespace NexusEngine {
std::string NativeGameApiMetadata::Identifier(NexusGameString value) {
    if(!value.data || !value.length || value.length > 128) throw std::invalid_argument("Invalid Native System identifier.");
    for(uint32_t index = 0; index < value.length; ++index) {
        const char ch = value.data[index];
        if(!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-' || ch == ':' || ch == '/'))
            throw std::invalid_argument("Native System identifier must be ASCII.");
    }
    return {value.data, value.length};
}
namespace {
/** \brief Public APIの限定値種別を宣言し、void引数や暗黙pointer型を拒否する */
NativeGameApiMetadata::ValueType CopyType(const NexusGameApiTypeV1& type, bool result) {
    if(type.reserved || type.kind > NEXUS_VALUE_FLOAT32_ARRAY || (!result && type.kind == NEXUS_VALUE_VOID)) throw std::invalid_argument("Unsupported function value type.");
    std::string id;
    if(type.kind == NEXUS_VALUE_ENUM32) id = NativeGameApiMetadata::Identifier(type.typeId);
    else if(type.typeId.length) throw std::invalid_argument("Only enum contracts carry a type ID.");
    return {type.kind, std::move(id)};
}
}
NexusGameStatus NativeGameApiMetadata::Declare(uint64_t generation, uint64_t system, const std::string& systemId, const NexusNativeFunctionDescriptorV1& descriptor) {
    if(!generation || !system || descriptor.structSize != sizeof(descriptor) || !descriptor.version || descriptor.ownerThreadOnly != 1 ||
       descriptor.parameterCount > 16 || (descriptor.parameterCount && !descriptor.parameters)) return NEXUS_GAME_INVALID_ARGUMENT;
    try {
        const auto id = Identifier(descriptor.id);
        if(Find(system, id, descriptor.version) || functions_.size() >= 256) return NEXUS_GAME_FAILED;
        Function function{generation, system, systemId, id, descriptor.version, CopyType(descriptor.result, true), {}};
        std::set<std::string> parameters;
        for(uint32_t index = 0; index < descriptor.parameterCount; ++index) {
            const auto& source = descriptor.parameters[index]; auto name = Identifier(source.id);
            if(!parameters.insert(name).second) return NEXUS_GAME_INVALID_ARGUMENT;
            function.parameters.push_back({std::move(name), CopyType(source.type, false)});
        }
        // 全検証が完了してから公開し、失敗した宣言を半分だけ残さない。
        functions_.push_back(std::move(function)); return NEXUS_GAME_OK;
    } catch(const std::invalid_argument&) { return NEXUS_GAME_INVALID_ARGUMENT; }
    catch(...) { return NEXUS_GAME_FAILED; }
}
const NativeGameApiMetadata::Function* NativeGameApiMetadata::Find(uint64_t system, std::string_view id, uint32_t version) const noexcept {
    for(const auto& function : functions_) if(function.system == system && function.id == id && function.version == version) return &function;
    return nullptr;
}
}
