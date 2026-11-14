#include "GameApiBridge.h"
#include "GameComponentAccess.h"
#include "Runtime/Physics/GamePhysicsAccess.h"
#include "Public/Nexus/ScriptAttachments.h"
#include "Public/Nexus/GameInput.h"
#include "Public/Nexus/GameAudio.h"
#include "Public/Nexus/GameWorldControl.h"
#include "Public/Nexus/GameDebug.h"
#include "Public/Nexus/GameTime.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Application/ApplicationPaths.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include "Runtime/Reflection/ManagedScriptMetadata.h"
#include "Runtime/Reflection/TypeRegistry.h"
#include "Runtime/Reflection/ComponentReflection.h"
#include "Runtime/Reflection/NativeComponentMetadata.h"
#include "Foundation/Logging/Logger.h"
#include <Windows.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <deque>
#include <vector>
#include <array>

namespace NexusEngine {
namespace {
/* World tokenの解決だけを行う非所有ルーター。ECSを二重に所有しない。
 * mutexは誤ったthreadからの呼出しにも安全にエラーを返すために必要で、
 * World操作自体は登録threadに限定する。登録解除とアクセスを同じlockで直列化する。 */
/*-----------------------------------------------------------------------------------------
 * WorldEntry
 * - 一つの登録Worldの非所有参照とアクセスthreadを記録する
 * - Scene交換後の旧Entity Handleを別Sceneへ転送しない
 *---------------------------------------------------------------------------------------*/
struct WorldEntry {
    Level* level; //< GameRuntime所有。登録解除がLevel破棄に先行する
    Scene* scene; //< 登録時のScene。同一Level内のScene交換を検出する
    std::thread::id thread; //< Game APIを実行できる所有thread
    bool managedServicesFrozen = false; //< Hybrid ReloadのcandidateからNative DLL stateを変更させない
    uint32_t serviceDepth = 0; //< callback中のclient失効と無制限の再帰を拒否する
    uint64_t sequence = 1; //< World内のEvent連番。0は使わない
    TypeRegistry types; //< 同じホストReflectionから得たaccessor。Component実体やDLL内objectを所有しない
    uint64_t token = 0; //< Queryの空targetでも応答Handleへ付ける、Bridgeと同じ寿命token
    bool physicsOverflow=false; //< 接触Eventのqueue超過をConsoleへ一度だけ通知する
};
/*-----------------------------------------------------------------------------------------
 * ServiceEvent / ServiceClient / ServiceCommand
 * - World寿命の値通信とNative登録の非所有参照だけを保持する。ECS / Levelを複製しない
 * - Managed objectは含めず、client失効でqueueとcallbackの借用を同時に終了する
 *---------------------------------------------------------------------------------------*/
struct ServiceEvent {
    uint64_t sequence; NexusEntityHandle source; uint32_t version; //< コピー可能な通知header
    std::string type; std::vector<uint8_t> payload; //< Engine所有の値コピー。外部bufferは保持しない
};
struct ServiceClient {
    uint64_t world; bool native; //< 同じWorldのthread制約とNative登録権限
    std::deque<ServiceEvent> events; //< 最大256件。保存・別世代への引継ぎは行わない
    bool physicsEvents=false; //< 未購読clientのqueueをPhysics通知で埋めない
};
struct ServiceCommand {
    uint64_t world, owner; std::string id; uint32_t version; //< 登録scopeと永続protocol ID
    NexusGameCommandFn callback; void* state; //< DLLが所有する借用値。client失効前だけ使用する
};
/*-----------------------------------------------------------------------------------------
 * WorldRegistry
 * - Process内のWorld tokenを既存Levelへ解決する内部ルーティング表
 * - ECS本体やLevelを所有せず、通常ホストのRuntime終了後に破棄される
 *---------------------------------------------------------------------------------------*/
struct WorldRegistry {
    std::mutex mutex; //< 誤threadの照会と登録解除の競合も防ぐ
    std::map<uint64_t, WorldEntry> worlds; //< Bridgeの生存期間だけ有効な借用参照
    uint64_t next = 1; //< 再利用しないtoken。overflow後は新規登録を拒否する
    bool legacyPatch = false; //< 同期パッチ中は別Threadも含め新規Interop Worldの開始を拒否する
    bool patchBlocked = false; //< 参照解除失敗後に残留callbackへ新コードを適用しない
    std::thread::id patchThread; //< 境界を取得したThreadだけが解除する
    uint64_t nextAttachment = 1; //< Scriptの削除・再追加を、Entity世代とは別に区別する
    uint64_t nextClient = 1; //< Process内で再利用しない通信capability
    std::map<uint64_t, ServiceClient> clients; //< Worldへの副索引と有界queueだけを所有する
    std::vector<ServiceCommand> commands; //< Native DLLのcallbackはclient失効前だけ借用する
};
WorldRegistry& Worlds() { static WorldRegistry registry; return registry; }

/** \brief tokenを解決して既存Levelへ処理を適用する。全例外をABI内のstatusへ閉じ込める */
template<class Function>
NexusGameStatus Access(uint64_t token, Function&& function) noexcept {
    try {
        auto& registry = Worlds();
        std::lock_guard lock(registry.mutex);
        const auto found = registry.worlds.find(token);
        if(found == registry.worlds.end()) return NEXUS_GAME_INVALID_HANDLE;
        if(found->second.thread != std::this_thread::get_id()) return NEXUS_GAME_WRONG_THREAD;
        if(!found->second.scene || found->second.level->GetScene() != found->second.scene) return NEXUS_GAME_INVALID_HANDLE;
        return function(*found->second.level);
    } catch(...) { return NEXUS_GAME_FAILED; }
}

/** \brief 呼出側所有bufferの範囲とUTF-8を検証し、不正な名前を既存Levelへ混入させない */
bool Text(NexusGameString value, uint32_t limit, bool multiline = false) noexcept {
    if(value.length > limit || (value.length && !value.data)) return false;
    if(!value.length) return true;
    for(uint32_t index = 0; index < value.length; ++index) {
        const auto ch = static_cast<unsigned char>(value.data[index]);
        if(ch < 32 && !(multiline && (ch == '\n' || ch == '\t' || ch == '\r'))) return false;
    }
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data, static_cast<int>(value.length), nullptr, 0) > 0;
}

/** \brief Worldを解決した後に既存Entity世代を検証する。Entityポインタをcacheしない */
Entity Resolve(Level& level, NexusEntityHandle handle) noexcept {
    return level.GetScene()->GetEntity({handle.index, handle.generation});
}

NexusGameStatus NEXUS_GAME_CALL Create(uint64_t world, NexusGameString name, NexusEntityHandle* output) noexcept {
    if(!output) return NEXUS_GAME_INVALID_ARGUMENT;
    *output = {};
    if(!Text(name, 256)) return NEXUS_GAME_INVALID_ARGUMENT;
    return Access(world, [&](Level& level) {
        auto entity = level.CreateEntity(name.length ? std::string(name.data, name.length) : "Entity");
        if(!entity.IsAlive()) return NEXUS_GAME_FAILED;
        const auto id = entity.GetId();
        *output = {world, id.index, id.generation};
        return NEXUS_GAME_OK;
    });
}
NexusGameStatus NEXUS_GAME_CALL Destroy(NexusEntityHandle handle) noexcept {
    return Access(handle.world, [&](Level& level) {
        auto entity = Resolve(level, handle);
        if(!entity.IsAlive()) return NEXUS_GAME_INVALID_HANDLE;
        return level.DestroyEntity(entity) ? NEXUS_GAME_OK : NEXUS_GAME_FAILED;
    });
}
NexusGameStatus NEXUS_GAME_CALL Alive(NexusEntityHandle handle) noexcept {
    return Access(handle.world, [&](Level& level) { return Resolve(level, handle).IsAlive() ? NEXUS_GAME_OK : NEXUS_GAME_INVALID_HANDLE; });
}
NexusGameStatus NEXUS_GAME_CALL Entities(uint64_t world, NexusEntityHandle* output, uint32_t capacity, uint32_t* count) noexcept {
    if(!count || (capacity && !output)) return NEXUS_GAME_INVALID_ARGUMENT;
    *count = 0;
    return Access(world, [&](Level& level) {
        const auto size = level.GetScene()->GetEntityCount();
        if(size > (std::numeric_limits<uint32_t>::max)()) return NEXUS_GAME_FAILED;
        *count = static_cast<uint32_t>(size);
        if(capacity < *count) return NEXUS_GAME_BUFFER_TOO_SMALL;
        uint32_t index = 0;
        // Componentを外したEntityも含め、World構造を変更せずHandleだけを一括コピーする。
        level.GetScene()->ForEachEntity([&](Entity entity) {
            const auto id = entity.GetId(); output[index++] = {world, id.index, id.generation};
        });
        *count = index;
        return NEXUS_GAME_OK;
    });
}
NexusGameStatus NEXUS_GAME_CALL GetTransform(NexusEntityHandle handle, NexusLocalTransform* output) noexcept {
    if(!output) return NEXUS_GAME_INVALID_ARGUMENT;
    *output = {};
    return Access(handle.world, [&](Level& level) {
        const auto* transform = Resolve(level, handle).GetComponent<TransformComponent>();
        if(!transform) return NEXUS_GAME_INVALID_HANDLE;
        *output = {{transform->translation.x, transform->translation.y, transform->translation.z},
            {transform->rotation.x, transform->rotation.y, transform->rotation.z, transform->rotation.w},
            {transform->scale.x, transform->scale.y, transform->scale.z}};
        return NEXUS_GAME_OK;
    });
}
NexusGameStatus NEXUS_GAME_CALL SetTransform(NexusEntityHandle handle, const NexusLocalTransform* value) noexcept {
    if(!value) return NEXUS_GAME_INVALID_ARGUMENT;
    for(float item : value->translation) if(!std::isfinite(item)) return NEXUS_GAME_INVALID_ARGUMENT;
    for(float item : value->scale) if(!std::isfinite(item)) return NEXUS_GAME_INVALID_ARGUMENT;
    float length = 0;
    for(float item : value->rotation) { if(!std::isfinite(item)) return NEXUS_GAME_INVALID_ARGUMENT; length += item * item; }
    if(!std::isfinite(length) || length < 1.0e-12f) return NEXUS_GAME_INVALID_ARGUMENT;
    const float inverse = 1.0f / std::sqrt(length);
    return Access(handle.world, [&](Level& level) {
        auto* transform = Resolve(level, handle).GetComponent<TransformComponent>();
        if(!transform) return NEXUS_GAME_INVALID_HANDLE;
        transform->translation = {value->translation[0], value->translation[1], value->translation[2]};
        transform->rotation = {value->rotation[0] * inverse, value->rotation[1] * inverse, value->rotation[2] * inverse, value->rotation[3] * inverse};
        transform->scale = {value->scale[0], value->scale[1], value->scale[2]};
        return NEXUS_GAME_OK;
    });
}
NexusGameStatus NEXUS_GAME_CALL Log(uint64_t world, NexusGameString message) noexcept {
    if(!Text(message, 16384, true)) return NEXUS_GAME_INVALID_ARGUMENT;
    const auto status = Access(world, [](Level&) { return NEXUS_GAME_OK; });
    if(status != NEXUS_GAME_OK) return status;
    // 外部SinkのcallbackをWorld lock内で呼ばず、再入時のdeadlockを避ける。
    Logger::Get().Log(LogLevel::Info, "Game", message.length ? std::string_view(message.data, message.length) : std::string_view{});
    return NEXUS_GAME_OK;
}
/** \brief Accessのlock内だけで関連付けtokenを発行する。再利用で旧callbackを復活させない */
uint64_t Attachment() noexcept {
    auto& counter = Worlds().nextAttachment;
    if(!counter) return 0;
    return counter++;
}
NexusGameStatus NEXUS_GAME_CALL SetScript(NexusEntityHandle handle, NexusGameString type, uint32_t enabled) noexcept {
    if(!type.length || !Text(type, 512) || enabled > 1) return NEXUS_GAME_INVALID_ARGUMENT;
    return Access(handle.world, [&](Level& level) {
        auto entity = Resolve(level, handle);
        if(!entity.IsAlive()) return NEXUS_GAME_INVALID_HANDLE;
        const auto token = Attachment(); if(!token) return NEXUS_GAME_FAILED;
        std::string name(type.data, type.length);
        auto* component = entity.GetComponent<ScriptComponent>();
        // 旧SetScriptの置換契約は維持する。同型追加は明示operation 6だけで行う。
        if(component) { const auto* duplicate=component->FindScript(name); if(duplicate && duplicate!=static_cast<ScriptAttachment*>(component)) return NEXUS_GAME_INVALID_ARGUMENT; }
        if(!component) component = entity.AddComponent<ScriptComponent>();
        if(!component) return NEXUS_GAME_FAILED;
        component->scriptType = std::move(name); component->enabled = enabled != 0;
        component->instance = {}; component->attachment = token;
        component->slotId=NewScriptSlotId();
        return NEXUS_GAME_OK;
    });
}
NexusGameStatus NEXUS_GAME_CALL RemoveScript(NexusEntityHandle handle) noexcept {
    return Access(handle.world, [&](Level& level) { return Resolve(level, handle).RemoveComponent<ScriptComponent>() ? NEXUS_GAME_OK : NEXUS_GAME_INVALID_HANDLE; });
}
NexusGameStatus NEXUS_GAME_CALL ScriptState(NexusEntityHandle handle, NexusScriptStateV1* output) noexcept {
    if(!output) return NEXUS_GAME_INVALID_ARGUMENT;
    *output = {};
    return Access(handle.world, [&](Level& level) {
        auto* component = Resolve(level, handle).GetComponent<ScriptComponent>();
        if(!component) return NEXUS_GAME_INVALID_HANDLE;
        bool valid = component->IsValid();
        component->ForEachScript([&](auto& slot) { if(!slot.attachment) slot.attachment = Attachment(); valid &= slot.attachment != 0; });
        if(!valid) return NEXUS_GAME_FAILED;
        *output = {component->attachment, component->enabled ? 1u : 0u, 0};
        return NEXUS_GAME_OK;
    });
}
NexusGameStatus NEXUS_GAME_CALL ScriptEnabled(NexusEntityHandle handle, uint32_t enabled) noexcept {
    if(enabled > 1) return NEXUS_GAME_INVALID_ARGUMENT;
    return Access(handle.world, [&](Level& level) {
        auto* component = Resolve(level, handle).GetComponent<ScriptComponent>();
        if(!component) return NEXUS_GAME_INVALID_HANDLE;
        component->enabled = enabled != 0; return NEXUS_GAME_OK;
    });
}
/** \brief 関連付けtoken検証後に値をコピーする。Componentのアドレスを境界外へ渡さない */
NexusGameStatus NEXUS_GAME_CALL ReadProperties(NexusEntityHandle handle, uint64_t attachment, char* output, uint32_t capacity, uint32_t* length) noexcept {
    if(!length || (capacity && !output) || !attachment) return NEXUS_GAME_INVALID_ARGUMENT;
    *length = 0;
    return Access(handle.world, [&](Level& level) {
        auto* owner = Resolve(level, handle).GetComponent<ScriptComponent>();
        auto* component = owner ? owner->FindAttachment(attachment) : nullptr;
        if(!component) return NEXUS_GAME_INVALID_HANDLE;
        if(component->propertyState.size() > 262144) return NEXUS_GAME_FAILED;
        *length = static_cast<uint32_t>(component->propertyState.size());
        if(capacity < *length) return NEXUS_GAME_BUFFER_TOO_SMALL;
        if(*length) std::memcpy(output, component->propertyState.data(), *length);
        return NEXUS_GAME_OK;
    });
}
NexusGameStatus NEXUS_GAME_CALL WriteProperties(NexusEntityHandle handle, uint64_t attachment, NexusGameString state) noexcept {
    if(!attachment || !Text(state, 262144, true)) return NEXUS_GAME_INVALID_ARGUMENT;
    // parseはlock外で完了する。書込直前にもWorld・Entity世代・attachmentを再検証する。
    const auto text = state.length ? std::string_view(state.data, state.length) : std::string_view{};
    if(!ValidateScriptPropertyState(text)) return NEXUS_GAME_INVALID_ARGUMENT;
    return Access(handle.world, [&](Level& level) {
        auto* owner = Resolve(level, handle).GetComponent<ScriptComponent>();
        auto* component = owner ? owner->FindAttachment(attachment) : nullptr;
        if(!component) return NEXUS_GAME_INVALID_HANDLE;
        size_t total = text.size(); owner->ForEachScript([&](const auto& slot) { if(&slot != component) total += slot.propertyState.size(); });
        if(total > 1024 * 1024) return NEXUS_GAME_INVALID_ARGUMENT;
        component->propertyState.assign(text); return NEXUS_GAME_OK;
    });
}
/** \brief 同じWorld lockで関連付けを解決する。Managed objectやComponent pointerは返さない */
NexusGameStatus NEXUS_GAME_CALL ScriptAttachments(void*, NexusEntityHandle handle, NexusGameBytes request,
    uint8_t* response, uint32_t capacity, uint32_t* length) noexcept {
    if(!length) return NEXUS_GAME_INVALID_ARGUMENT; *length = 0;
    if(!request.data || request.length < sizeof(NexusScriptAttachmentRequestV1) || (capacity && !response)) return NEXUS_GAME_INVALID_ARGUMENT;
    NexusScriptAttachmentRequestV1 header{}; std::memcpy(&header, request.data, sizeof(header));
    const bool adding=header.operation==1 || header.operation==6;
    if(header.operation < 1 || header.operation > 6 || header.enabled > 1 || header.typeLength > 512 || sizeof(header) + header.typeLength != request.length ||
        (!adding && header.typeLength) || (header.operation != 5 && header.index) ||
        ((adding || header.operation == 5) && header.attachment) ||
        ((header.operation == 2 || header.operation == 3 || header.operation == 4) && !header.attachment) ||
        ((header.operation == 2 || header.operation == 4 || header.operation == 5) && header.enabled) || header.index > 32) return NEXUS_GAME_INVALID_ARGUMENT;
    const NexusGameString type{reinterpret_cast<const char*>(request.data + sizeof(header)), header.typeLength};
    if(adding && (!type.length || !Text(type, 512))) return NEXUS_GAME_INVALID_ARGUMENT;
    return Access(handle.world, [&](Level& level) {
        auto entity = Resolve(level, handle); if(!entity.IsAlive()) return NEXUS_GAME_INVALID_HANDLE;
        auto* owner = entity.GetComponent<ScriptComponent>();
        if(owner && !owner->IsValid()) return NEXUS_GAME_FAILED;
        const bool output = adding || header.operation == 2 || header.operation == 5;
        if(output) { *length = sizeof(NexusScriptStateV1); if(capacity < *length) return NEXUS_GAME_BUFFER_TOO_SMALL; }
        ScriptAttachment* slot = owner ? owner->FindAttachment(header.attachment) : nullptr;
        if(adding) {
            const std::string name(type.data, type.length);
            slot = owner && header.operation==1 ? owner->FindScript(name) : nullptr;
            if(!slot) {
                if(owner && !owner->scriptType.empty() && owner->additional.size() >= 31) return NEXUS_GAME_INVALID_ARGUMENT;
                ScriptAttachment next; next.scriptType = name; next.enabled = header.enabled != 0; next.attachment = Attachment();
                if(!next.attachment) return NEXUS_GAME_FAILED;
                if(!owner) { owner = entity.AddComponent<ScriptComponent>(); if(!owner) return NEXUS_GAME_FAILED; }
                if(owner->scriptType.empty()) { static_cast<ScriptAttachment&>(*owner) = std::move(next); slot = owner; }
                else { owner->additional.push_back(std::move(next)); slot = &owner->additional.back(); }
            }
        } else if(header.operation == 5) {
            if(owner) slot = header.index == 0 ? static_cast<ScriptAttachment*>(owner) : header.index <= owner->additional.size() ? &owner->additional[header.index - 1] : nullptr;
            if(slot && slot->scriptType.empty()) slot = nullptr;
            if(slot) { *length += static_cast<uint32_t>(slot->scriptType.size()); if(capacity < *length) return NEXUS_GAME_BUFFER_TOO_SMALL; }
        } else if(header.operation != 2 && !slot) return NEXUS_GAME_INVALID_HANDLE;
        if(header.operation == 3) { slot->enabled = header.enabled != 0; return NEXUS_GAME_OK; }
        if(header.operation == 4) {
            if(owner->additional.empty()) return entity.RemoveComponent<ScriptComponent>() ? NEXUS_GAME_OK : NEXUS_GAME_FAILED;
            return owner->RemoveAttachment(slot) ? NEXUS_GAME_OK : NEXUS_GAME_FAILED;
        }
        NexusScriptStateV1 state{};
        if(slot) {
            if(!slot->attachment) slot->attachment = Attachment(); if(!slot->attachment) return NEXUS_GAME_FAILED;
            state = {slot->attachment, slot->enabled ? 1u : 0u, 0};
        }
        std::memcpy(response, &state, sizeof(state));
        if(header.operation == 5 && slot) std::memcpy(response + sizeof(state), slot->scriptType.data(), slot->scriptType.size());
        return NEXUS_GAME_OK;
    });
}
/** \brief ASCIIの永続IDだけを受け入れ、CLR型名やNativeアドレスをprotocolにしない */
bool ServiceId(NexusGameString text) noexcept {
    if(!text.data || !text.length || text.length > 128) return false;
    for(uint32_t i = 0; i < text.length; ++i) {
        const char c = text.data[i];
        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '/' || c == ':')) return false;
    }
    return true;
}
/** \brief Worldの既存thread / Scene検証をclient経由でも同じ条件で適用する */
template<class Function> NexusGameStatus ClientAccess(uint64_t client, Function&& function) noexcept {
    try {
        auto& registry = Worlds(); std::lock_guard lock(registry.mutex);
        auto found = registry.clients.find(client); if(found == registry.clients.end()) return NEXUS_GAME_INVALID_HANDLE;
        auto world = registry.worlds.find(found->second.world); if(world == registry.worlds.end()) return NEXUS_GAME_INVALID_HANDLE;
        if(world->second.thread != std::this_thread::get_id()) return NEXUS_GAME_WRONG_THREAD;
        if(world->second.level->GetScene() != world->second.scene) return NEXUS_GAME_INVALID_HANDLE;
        return function(registry, world->second, found->second);
    } catch(...) { return NEXUS_GAME_FAILED; }
}
/** \brief Worldへ有界clientを作る。Native権限は公開openでは付与しない */
NexusGameStatus OpenServices(uint64_t world, uint64_t* output, bool native) noexcept {
    if(!output) return NEXUS_GAME_INVALID_ARGUMENT; *output = 0;
    return Access(world, [&](Level&) {
        auto& registry = Worlds(); size_t count = 0;
        for(const auto& [id, client] : registry.clients) { static_cast<void>(id); if(client.world == world) ++count; }
        if(count >= 32 || !registry.nextClient) return NEXUS_GAME_FAILED;
        const auto id = registry.nextClient++;
        registry.clients.emplace(id, ServiceClient{world, native, {}}); *output = id; return NEXUS_GAME_OK;
    });
}
NexusGameStatus NEXUS_GAME_CALL OpenManagedServices(uint64_t world, uint64_t* output) noexcept { return OpenServices(world, output, false); }
/** \brief callback中のstate解放を防ぎ、登録と値queueを一緒に失効させる */
NexusGameStatus NEXUS_GAME_CALL CloseServices(uint64_t client) noexcept {
    return ClientAccess(client, [&](WorldRegistry& registry, WorldEntry& world, ServiceClient&) {
        if(world.serviceDepth) return NEXUS_GAME_FAILED;
        world.level->StopAudioOwner(client);
        world.scene->ClearDebugLines(client);
        std::erase_if(registry.commands, [=](const auto& item) { return item.owner == client; });
        registry.clients.erase(client); return NEXUS_GAME_OK;
    });
}
/** \brief Native loaderのscopeへ明示登録する。同名・同Versionを黙って置換しない */
NexusGameStatus NEXUS_GAME_CALL RegisterCommand(uint64_t client, NexusGameString id, uint32_t version, NexusGameCommandFn callback, void* state) noexcept {
    if(!ServiceId(id) || !version || !callback) return NEXUS_GAME_INVALID_ARGUMENT;
    // Engineの固定protocolをGame DLLから差し替えると、型と所有権の検証を迂回できてしまう。
    if(std::string_view(id.data, id.length).starts_with("nexus.engine.")) return NEXUS_GAME_INVALID_ARGUMENT;
    return ClientAccess(client, [&](WorldRegistry& registry, WorldEntry& world, ServiceClient& owner) {
        if(!owner.native || world.serviceDepth) return NEXUS_GAME_FAILED;
        size_t count = 0;
        for(const auto& item : registry.commands) if(item.world == owner.world) {
            ++count; if(item.id == std::string_view(id.data, id.length) && item.version == version) return NEXUS_GAME_FAILED;
        }
        if(count >= 128) return NEXUS_GAME_FAILED;
        registry.commands.push_back({owner.world, client, std::string(id.data, id.length), version, callback, state}); return NEXUS_GAME_OK;
    });
}
/** \brief 無指定Handleまたは同じWorldの生存Entityだけを送信・呼出対象にする */
bool ServiceEntity(const WorldEntry& world, uint64_t token, NexusEntityHandle entity) {
    if(!entity.world) return !entity.index && !entity.generation;
    return entity.world == token && Resolve(*world.level, entity).IsAlive();
}
/** \brief World lease中だけ借用する。Reflection callbackはWorld mutex外で実行する */
NexusGameStatus NEXUS_GAME_CALL Components(void* state, NexusEntityHandle entity, NexusGameBytes request,
    uint8_t* response, uint32_t capacity, uint32_t* length) noexcept {
    auto& world = *static_cast<WorldEntry*>(state);
    return InvokeGameComponent(*world.level, world.types, world.token, entity, request, response, capacity, length);
}
/** \brief 同じWorld leaseと所有Thread検証で一括Transformを処理する */
NexusGameStatus NEXUS_GAME_CALL TransformBatch(void* state, NexusEntityHandle entity, NexusGameBytes request,
    uint8_t* response, uint32_t capacity, uint32_t* length) noexcept {
    auto& world = *static_cast<WorldEntry*>(state);
    return InvokeGameTransformBatch(*world.level, world.token, entity, request, response, capacity, length);
}
/** Physics呼出し中だけのclient借用値。購読はこの接続の寿命へ帰属する。 */
struct PhysicsInvocation { WorldEntry* world=nullptr; uint64_t client=0; };
/** \brief 常駐EngineのPhysics入口。DLL関数pointerをManagedへ渡さない */
NexusGameStatus NEXUS_GAME_CALL Physics(void* state, NexusEntityHandle entity, NexusGameBytes request,
    uint8_t* response, uint32_t capacity, uint32_t* length) noexcept {
    auto& call=*static_cast<PhysicsInvocation*>(state);
    if(request.length==sizeof(NexusPhysicsRequestV1)) {
        NexusPhysicsRequestV1 input{}; std::memcpy(&input,request.data,sizeof(input));
        if(input.operation==5) {
            if(entity.world || entity.index || entity.generation || input.reserved>1 || input.reserved2 || input.distance!=0) return NEXUS_GAME_INVALID_ARGUMENT;
            for(float item:input.vector) if(item!=0) return NEXUS_GAME_INVALID_ARGUMENT;
            for(float item:input.direction) if(item!=0) return NEXUS_GAME_INVALID_ARGUMENT;
            *length=0;
            return ClientAccess(call.client,[&](WorldRegistry&,WorldEntry&,ServiceClient& client) { client.physicsEvents=input.reserved!=0; return NEXUS_GAME_OK; });
        }
    }
    return InvokeGamePhysics(*call.world->level,call.world->token,entity,request,response,capacity,length);
}
/** \brief 消費せず同じframeの入力をコピーし、Script間でedgeを取り合わない */
NexusGameStatus NEXUS_GAME_CALL Input(void* state, NexusEntityHandle entity, NexusGameBytes request,
    uint8_t* response, uint32_t capacity, uint32_t* length) noexcept {
    if(entity.world || entity.index || entity.generation) return NEXUS_GAME_INVALID_ARGUMENT;
    if(request.length) {
        if(request.length!=sizeof(NexusGamepadRequestV1) || !request.data) return NEXUS_GAME_INVALID_ARGUMENT;
        NexusGamepadRequestV1 query{}; std::memcpy(&query,request.data,sizeof(query));
        if(query.operation!=1) return NEXUS_GAME_INVALID_ARGUMENT;
        *length=sizeof(NexusGamepadsFrameV1);
        if(capacity<*length) return NEXUS_GAME_BUFFER_TOO_SMALL;
        const auto& frame=static_cast<WorldEntry*>(state)->level->Input();
        NexusGamepadsFrameV1 result{}; result.count=NEXUS_GAMEPAD_COUNT; result.focused=frame.focused ? 1u : 0u;
        for(size_t slot=0;slot<NEXUS_GAMEPAD_COUNT;++slot) {
            const auto& pad=frame.gamepads[slot]; auto& copy=result.pads[slot];
            copy.connected=pad.connected ? 1u : 0u; copy.connectedThisFrame=pad.connectedThisFrame ? 1u : 0u;
            copy.disconnectedThisFrame=pad.disconnectedThisFrame ? 1u : 0u;
            copy.down=pad.down; copy.pressed=pad.pressed; copy.released=pad.released;
            for(size_t axis=0;axis<2;++axis) { copy.leftStick[axis]=pad.leftStick[axis]; copy.rightStick[axis]=pad.rightStick[axis]; }
            copy.leftTrigger=pad.leftTrigger; copy.rightTrigger=pad.rightTrigger;
        }
        std::memcpy(response,&result,sizeof(result)); return NEXUS_GAME_OK;
    }
    *length=sizeof(NexusInputFrameV1);
    if(capacity<*length) return NEXUS_GAME_BUFFER_TOO_SMALL;
    const auto& frame=static_cast<WorldEntry*>(state)->level->Input();
    NexusInputFrameV1 result{};
    for(size_t i=0;i<256;++i) { result.down[i]=frame.down[i]; result.pressed[i]=frame.pressed[i]; result.released[i]=frame.released[i]; }
    result.mouseX=frame.mouseX; result.mouseY=frame.mouseY; result.deltaX=frame.deltaX; result.deltaY=frame.deltaY; result.wheel=frame.wheel;
    result.focused=frame.focused ? 1u : 0u;
    std::memcpy(response,&result,sizeof(result));
    return NEXUS_GAME_OK;
}
/** Audio呼出し中だけ保持する値。clientがvoice所有者でありWorld全体へ権限を拡大しない。 */
struct AudioInvocation { Level* level=nullptr; uint64_t owner=0; };
/** \brief XAudio2の副作用はScene snapshotで戻せないため、Reloadのfreeze中は入口で拒否する */
NexusGameStatus NEXUS_GAME_CALL Audio(void* state,NexusEntityHandle target,NexusGameBytes request,
    uint8_t* response,uint32_t capacity,uint32_t* length) noexcept {
    if(target.world || target.index || target.generation || request.length<sizeof(NexusAudioRequestV1)) return NEXUS_GAME_INVALID_ARGUMENT;
    NexusAudioRequestV1 input{}; std::memcpy(&input,request.data,sizeof(input));
    if(input.operation<1 || input.operation>4 || input.pathLength>2048 || request.length!=sizeof(input)+input.pathLength || input.loop>1 ||
        !std::isfinite(input.volume) || input.volume<0 || input.volume>4) return NEXUS_GAME_INVALID_ARGUMENT;
    const bool output=input.operation==1 || input.operation==3;
    *length=output ? sizeof(uint64_t) : 0;
    if(capacity<*length) return NEXUS_GAME_BUFFER_TOO_SMALL;
    if((input.operation==1 && (!input.pathLength || input.voice)) ||
        (input.operation!=1 && (input.pathLength || input.loop || !input.voice))) return NEXUS_GAME_INVALID_ARGUMENT;
    auto& context=*static_cast<AudioInvocation*>(state);
    try {
        auto& audio=context.level->Audio(); uint64_t value=0;
        if(input.operation==1) {
            const NexusGameString text{reinterpret_cast<const char*>(request.data+sizeof(input)),input.pathLength};
            if(!Text(text,2048)) return NEXUS_GAME_INVALID_ARGUMENT;
            const std::string utf8(text.data,text.length);
            const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()),utf8.size()));
            auto played=audio.Play(context.owner,path,input.volume,input.loop!=0);
            if(!played) { Logger::Get().Log(LogLevel::Error,"Audio",played.error().GetMessageText()); *length=0; return NEXUS_GAME_FAILED; }
            value=*played;
        } else if(input.operation==2) {
            if(!audio.Stop(context.owner,input.voice)) return NEXUS_GAME_INVALID_HANDLE;
        } else if(input.operation==3) {
            auto playing=audio.IsPlaying(context.owner,input.voice);
            if(!playing) return NEXUS_GAME_INVALID_HANDLE;
            value=*playing ? 1u : 0u;
        } else if(!audio.SetVolume(context.owner,input.voice,input.volume)) return NEXUS_GAME_INVALID_HANDLE;
        if(output) std::memcpy(response,&value,sizeof(value));
        return NEXUS_GAME_OK;
    } catch(...) { *length=0; return NEXUS_GAME_FAILED; }
}
/** \brief Project共通の参照を解決する。Level遷移は要求だけ蓄積し、このstackでは破棄しない */
NexusGameStatus NEXUS_GAME_CALL WorldControl(void* state,NexusEntityHandle target,NexusGameBytes request,
    uint8_t* response,uint32_t capacity,uint32_t* length) noexcept {
    if(target.world || target.index || target.generation || request.length<sizeof(NexusWorldRequestV1)) return NEXUS_GAME_INVALID_ARGUMENT;
    NexusWorldRequestV1 input{}; std::memcpy(&input,request.data,sizeof(input));
    if(input.operation<1 || input.operation>2 || !input.pathLength || input.pathLength>2048 || sizeof(input)+input.pathLength!=request.length) return NEXUS_GAME_INVALID_ARGUMENT;
    const NexusGameString text{reinterpret_cast<const char*>(request.data+sizeof(input)),input.pathLength};
    if(!Text(text,2048)) return NEXUS_GAME_INVALID_ARGUMENT;
    auto& world=*static_cast<WorldEntry*>(state);
    if(input.operation==1 && world.managedServicesFrozen) return NEXUS_GAME_FAILED;
    try {
        const std::u8string utf8(reinterpret_cast<const char8_t*>(text.data),text.length);
        const std::filesystem::path reference(utf8);
        if(input.operation==1) return world.level->RequestLevel(reference) ? NEXUS_GAME_OK : NEXUS_GAME_INVALID_ARGUMENT;
        *length=sizeof(uint32_t); if(capacity<*length) return NEXUS_GAME_BUFFER_TOO_SMALL;
        auto path=ResolveProjectPath(world.level->ResourceRoot(),reference); if(!path) return NEXUS_GAME_INVALID_ARGUMENT;
        std::error_code error; const uint32_t exists=std::filesystem::is_regular_file(*path,error) && !error ? 1u : 0u;
        std::memcpy(response,&exists,sizeof(exists)); return NEXUS_GAME_OK;
    } catch(...) { *length=0; return NEXUS_GAME_FAILED; }
}
struct DebugInvocation {
    WorldEntry* world = nullptr;
    uint64_t client = 0;
};
/** \brief Worldの所有threadで診断値だけを処理する。外部Loggerはrouting lockの外で呼ぶ */
NexusGameStatus NEXUS_GAME_CALL DebugCommand(void* state, NexusEntityHandle target, NexusGameBytes request,
    uint8_t* response, uint32_t capacity, uint32_t* length) noexcept {
    if(request.length < sizeof(NexusDebugRequestV1)) {
        return NEXUS_GAME_INVALID_ARGUMENT;
    }
    NexusDebugRequestV1 input{};
    std::memcpy(&input, request.data, sizeof(input));
    if(input.reserved || !std::isfinite(input.duration) || input.duration < 0) {
        return NEXUS_GAME_INVALID_ARGUMENT;
    }
    auto& call = *static_cast<DebugInvocation*>(state);
    auto& scene = *call.world->scene;
    try {
        if(input.operation == 4) {
            if(!target.world || input.count || input.duration || request.length != sizeof(input)) {
                return NEXUS_GAME_INVALID_ARGUMENT;
            }
            // Update途中のLocal値も反映する。cacheやScene全体のTransformSystemを変更せず親を辿る。
            Matrix4x4 matrix;
            auto entity = Resolve(*call.world->level, target);
            std::vector<EntityId> visited;
            while(entity.IsAlive()) {
                const auto id = entity.GetId();
                if(visited.size() >= 4096 || std::ranges::find(visited, id) != visited.end()) {
                    return NEXUS_GAME_FAILED;
                }
                visited.push_back(id);
                const auto* transform = entity.GetComponent<TransformComponent>();
                if(!transform) {
                    return NEXUS_GAME_INVALID_HANDLE;
                }
                matrix = Multiply(matrix, MakeAffineMatrix(transform->scale, transform->rotation, transform->translation));
                const auto* hierarchy = entity.GetComponent<HierarchyComponent>();
                if(!hierarchy || !scene.IsAlive(hierarchy->parent)) {
                    break;
                }
                entity = scene.GetEntity(hierarchy->parent);
            }
            for(float value : matrix.elements) {
                if(!std::isfinite(value)) {
                    return NEXUS_GAME_INVALID_ARGUMENT;
                }
            }
            *length = sizeof(matrix);
            if(capacity < *length) {
                return NEXUS_GAME_BUFFER_TOO_SMALL;
            }
            std::memcpy(response, matrix.elements.data(), sizeof(matrix));
            return NEXUS_GAME_OK;
        }
        if(target.world || target.index || target.generation) {
            return NEXUS_GAME_INVALID_ARGUMENT;
        }
        if(input.operation == 1) {
            const NexusGameString text{reinterpret_cast<const char*>(request.data + sizeof(input)), request.length - sizeof(input)};
            if(input.count > 2 || input.duration || !Text(text, 4080, true)) {
                return NEXUS_GAME_INVALID_ARGUMENT;
            }
            const auto severity = input.count == 0 ? LogLevel::Info : input.count == 1 ? LogLevel::Warning : LogLevel::Error;
            Logger::Get().Log(severity, "Game", std::string_view(text.data, text.length));
            return NEXUS_GAME_OK;
        }
        if(input.operation == 3) {
            if(input.count || input.duration || request.length != sizeof(input)) {
                return NEXUS_GAME_INVALID_ARGUMENT;
            }
            scene.ClearDebugLines(call.client);
            return NEXUS_GAME_OK;
        }
        if(input.operation != 2 || input.count > 64 || request.length != sizeof(input) + input.count * sizeof(NexusDebugLineV1)) {
            return NEXUS_GAME_INVALID_ARGUMENT;
        }
        std::vector<DebugDrawLine> lines;
        lines.reserve(input.count);
        for(uint32_t index = 0; index < input.count; ++index) {
            NexusDebugLineV1 line{};
            std::memcpy(&line, request.data + sizeof(input) + index * sizeof(line), sizeof(line));
            if(line.depthTest > 1) {
                return NEXUS_GAME_INVALID_ARGUMENT;
            }
            DebugDrawLine value;
            for(size_t axis = 0; axis < 3; ++axis) {
                if(!std::isfinite(line.start[axis]) || !std::isfinite(line.end[axis])) {
                    return NEXUS_GAME_INVALID_ARGUMENT;
                }
                value.start[axis] = line.start[axis];
                value.end[axis] = line.end[axis];
            }
            for(size_t channel = 0; channel < 4; ++channel) {
                if(!std::isfinite(line.color[channel]) || line.color[channel] < 0 || line.color[channel] > 1) {
                    return NEXUS_GAME_INVALID_ARGUMENT;
                }
                value.color[channel] = line.color[channel];
            }
            value.depthTest = line.depthTest != 0;
            lines.push_back(value);
        }
        return scene.AddDebugLines(call.client, lines, input.duration) ? NEXUS_GAME_OK : NEXUS_GAME_FAILED;
    } catch(...) {
        *length = 0;
        return NEXUS_GAME_FAILED;
    }
}
/** \brief 時計の値と永続IDだけをコピーする。Worldを跨ぐpointerや保存Handleを返さない。 */
NexusGameStatus NEXUS_GAME_CALL TimeCommand(void* state, NexusEntityHandle target, NexusGameBytes request,
    uint8_t* response, uint32_t capacity, uint32_t* length) {
    *length = 0;
    if(target.world || request.length != sizeof(NexusTimeRequestV1)) {
        return NEXUS_GAME_INVALID_ARGUMENT;
    }
    NexusTimeRequestV1 input{};
    std::memcpy(&input, request.data, sizeof(input));
    if(input.reserved || input.reserved2 || input.operation < 1 || input.operation > 3 || !std::isfinite(input.value)) {
        return NEXUS_GAME_INVALID_ARGUMENT;
    }
    auto& clock = static_cast<WorldEntry*>(state)->level->GetScene()->Time();
    if(input.operation != 1) {
        if(input.operation == 2 && input.value >= 0 && input.value <= 100) {
            clock.timeScale = input.value;
        } else if(input.operation == 3 && input.value >= .001f && input.value <= 1) {
            clock.fixedDeltaTime = input.value;
        } else {
            return NEXUS_GAME_INVALID_ARGUMENT;
        }
        return NEXUS_GAME_OK;
    }
    *length = sizeof(NexusGameTimeV1);
    if(capacity < *length) {
        return NEXUS_GAME_BUFFER_TOO_SMALL;
    }
    const NexusGameTimeV1 output{clock.elapsed, clock.unscaledElapsed, clock.fixedElapsed,
        clock.inFixedStep ? clock.activeFixedDelta : clock.deltaTime, clock.unscaledDeltaTime,
        clock.inFixedStep ? clock.activeFixedDelta : clock.fixedDeltaTime, clock.timeScale, clock.frame, clock.fixedTick, clock.inFixedStep ? 1u : 0u, 0, clock.droppedFixedTime};
    std::memcpy(response, &output, sizeof(output));
    return NEXUS_GAME_OK;
}
NexusGameStatus NEXUS_GAME_CALL ReferenceCommand(void* state, NexusEntityHandle target, NexusGameBytes request,
    uint8_t* response, uint32_t capacity, uint32_t* length) {
    *length = 0;
    if(request.length != sizeof(NexusEntityReferenceRequestV1)) {
        return NEXUS_GAME_INVALID_ARGUMENT;
    }
    NexusEntityReferenceRequestV1 input{};
    std::memcpy(&input, request.data, sizeof(input));
    auto& level = *static_cast<WorldEntry*>(state)->level;
    if(input.reserved || input.operation < 1 || input.operation > 2) {
        return NEXUS_GAME_INVALID_ARGUMENT;
    }
    if(input.operation == 1) {
        auto entity = Resolve(level, target);
        if(!entity.IsAlive() || !entity.HasComponent<PersistentIdComponent>() || input.high || input.low) {
            return NEXUS_GAME_INVALID_HANDLE;
        }
        *length = sizeof(NexusEntityReferenceV1);
        if(capacity < *length) {
            return NEXUS_GAME_BUFFER_TOO_SMALL;
        }
        const auto id = entity.GetComponent<PersistentIdComponent>()->value;
        const NexusEntityReferenceV1 value{id.high, id.low};
        std::memcpy(response, &value, sizeof(value));
    } else {
        if(target.world) {
            return NEXUS_GAME_INVALID_ARGUMENT;
        }
        *length = sizeof(NexusEntityHandle);
        if(capacity < *length) {
            return NEXUS_GAME_BUFFER_TOO_SMALL;
        }
        const auto entity = level.FindEntity({input.high, input.low});
        NexusEntityHandle value{};
        if(entity.IsAlive()) {
            const auto id = entity.GetId();
            value = {static_cast<WorldEntry*>(state)->token, id.index, id.generation};
        }
        std::memcpy(response, &value, sizeof(value));
    }
    return NEXUS_GAME_OK;
}


/** \brief 例外と再帰をABI内へ閉じ込め、登録済みNative処理を一度だけ呼ぶ */
NexusGameStatus NEXUS_GAME_CALL InvokeCommand(uint64_t client, NexusGameString id, uint32_t version, NexusEntityHandle target,
    NexusGameBytes request, uint8_t* response, uint32_t capacity, uint32_t* length) noexcept {
    if(!length) return NEXUS_GAME_INVALID_ARGUMENT; *length = 0;
    if(!ServiceId(id) || !version || request.length > 4096 || (request.length && !request.data) || capacity > 4096 || (capacity && !response)) return NEXUS_GAME_INVALID_ARGUMENT;
    NexusGameCommandFn callback = nullptr; void* state = nullptr; uint64_t token = 0;
    AudioInvocation audioInvocation;
    PhysicsInvocation physicsInvocation;
    DebugInvocation debugInvocation;
    const bool engineComponent = std::string_view(id.data, id.length) == NEXUS_COMPONENT_COMMAND;
    const bool engineTransforms = std::string_view(id.data, id.length) == NEXUS_TRANSFORM_BATCH_COMMAND;
    const bool engineScripts = std::string_view(id.data, id.length) == NEXUS_SCRIPT_ATTACHMENTS_COMMAND;
    const bool enginePhysics = std::string_view(id.data, id.length) == NEXUS_PHYSICS_COMMAND;
    const bool engineInput = std::string_view(id.data, id.length) == NEXUS_INPUT_COMMAND;
    const bool engineAudio = std::string_view(id.data, id.length) == NEXUS_AUDIO_COMMAND;
    const bool engineControl = std::string_view(id.data, id.length) == NEXUS_WORLD_CONTROL_COMMAND;
    const bool engineDebug = std::string_view(id.data, id.length) == NEXUS_DEBUG_COMMAND;
    const bool engineTime = std::string_view(id.data, id.length) == NEXUS_TIME_COMMAND;
    const bool engineReference = std::string_view(id.data, id.length) == NEXUS_REFERENCE_COMMAND;
    const bool engineValue = engineTime || engineReference || engineComponent || engineTransforms || engineScripts || enginePhysics || engineInput || engineAudio || engineControl || engineDebug;
    const auto access = ClientAccess(client, [&](WorldRegistry& registry, WorldEntry& world, ServiceClient& caller) {
        // DLL固有stateの変更だけをfreezeする。共通Sceneへの値操作はReloadのWorld snapshotで
        // 復元できるため、新世代OnCreateでも利用する。旧世代の書込禁止はGameWorldが検査する。
        if((!caller.native && world.managedServicesFrozen && !engineValue) || world.serviceDepth >= 8) return NEXUS_GAME_FAILED;
        if(engineAudio && world.managedServicesFrozen) return NEXUS_GAME_FAILED;
        if(!ServiceEntity(world, caller.world, target)) return NEXUS_GAME_INVALID_HANDLE;
        if(engineValue) {
            if(version != (enginePhysics ? NEXUS_PHYSICS_PROTOCOL_VERSION : engineScripts ? NEXUS_SCRIPT_ATTACHMENTS_PROTOCOL_VERSION : NEXUS_COMPONENT_PROTOCOL_VERSION)) return NEXUS_GAME_INCOMPATIBLE_ABI;
            callback = engineControl ? WorldControl : engineAudio ? Audio : engineInput ? Input : enginePhysics ? Physics : engineScripts ? ScriptAttachments : engineComponent ? Components : TransformBatch;
            state = &world;
            if(engineTime) {
                callback = TimeCommand;
            }
            if(engineReference) {
                callback = ReferenceCommand;
            }
            if(enginePhysics) { physicsInvocation={&world,client}; state=&physicsInvocation; }
            if(engineAudio) { audioInvocation={world.level,client}; state=&audioInvocation; }
            if(engineDebug) {
                debugInvocation = {&world, client};
                state = &debugInvocation;
                callback = DebugCommand;
            }
            token = caller.world; ++world.serviceDepth; return NEXUS_GAME_OK;
        }
        for(const auto& item : registry.commands) if(item.world == caller.world && item.id == std::string_view(id.data, id.length) && item.version == version) {
            callback = item.callback; state = item.state; token = caller.world; ++world.serviceDepth; return NEXUS_GAME_OK;
        }
        return NEXUS_GAME_INVALID_HANDLE;
    });
    if(access != NEXUS_GAME_OK) return access;
    NexusGameStatus result = NEXUS_GAME_FAILED;
    // World lock外でNative callbackを実行し、共通Entity APIへの再入をdeadlockさせない。
    try { result = callback(state, target, request, response, capacity, length); } catch(...) { result = NEXUS_GAME_FAILED; }
    if(*length > 4096 || (*length > capacity && !(engineValue && result == NEXUS_GAME_BUFFER_TOO_SMALL))) { *length = 0; result = NEXUS_GAME_FAILED; }
    try { auto& registry = Worlds(); std::lock_guard lock(registry.mutex); auto found = registry.worlds.find(token); if(found != registry.worlds.end()) --found->second.serviceDepth; }
    catch(...) { return NEXUS_GAME_FAILED; }
    return result;
}
/** \brief 同じWorldの他clientへ値コピーを配送する。容量不足・確保失敗では部分配送しない */
NexusGameStatus NEXUS_GAME_CALL PublishEvent(uint64_t client, NexusGameString type, uint32_t version, NexusEntityHandle source, NexusGameBytes payload) noexcept {
    if(!ServiceId(type) || !version || payload.length > 4096 || (payload.length && !payload.data)) return NEXUS_GAME_INVALID_ARGUMENT;
    if(std::string_view(type.data,type.length)==NEXUS_PHYSICS_CONTACT_EVENT) return NEXUS_GAME_INVALID_ARGUMENT;
    return ClientAccess(client, [&](WorldRegistry& registry, WorldEntry& world, ServiceClient& sender) {
        if(!sender.native && world.managedServicesFrozen) return NEXUS_GAME_FAILED;
        if(!ServiceEntity(world, sender.world, source)) return NEXUS_GAME_INVALID_HANDLE;
        if(!world.sequence) return NEXUS_GAME_FAILED;
        // 全受信先の容量を先に検査し、満杯時の部分配送を避ける。
        std::vector<uint64_t> delivered;
        for(const auto& [id, receiver] : registry.clients) if(id != client && receiver.world == sender.world && receiver.events.size() >= 256) return NEXUS_GAME_FAILED;
        ServiceEvent event{world.sequence, source, version, std::string(type.data, type.length), {}};
        if(payload.length) event.payload.assign(payload.data, payload.data + payload.length);
        delivered.reserve(registry.clients.size());
        try {
            for(auto& [id, receiver] : registry.clients) if(id != client && receiver.world == sender.world) { receiver.events.push_back(event); delivered.push_back(id); }
        } catch(...) { for(auto id : delivered) registry.clients.at(id).events.pop_back(); throw; }
        ++world.sequence; return NEXUS_GAME_OK;
    });
}
/** \brief Eventを呼出側bufferへコピー後に消費する。Managed callbackをNativeへ登録させない */
NexusGameStatus NEXUS_GAME_CALL PollEvent(uint64_t client, NexusGameEventV1* header, uint8_t* buffer, uint32_t capacity, uint32_t* length) noexcept {
    if(!header || !length || (capacity && !buffer)) return NEXUS_GAME_INVALID_ARGUMENT; *header = {}; *length = 0;
    return ClientAccess(client, [&](WorldRegistry&, WorldEntry& world, ServiceClient& receiver) {
        if(!receiver.native && world.managedServicesFrozen) return NEXUS_GAME_FAILED;
        if(receiver.events.empty()) return NEXUS_GAME_OK;
        const auto& event = receiver.events.front();
        const auto typeLength = static_cast<uint32_t>(event.type.size()), payloadLength = static_cast<uint32_t>(event.payload.size());
        *header = {event.sequence, event.source, event.version, typeLength, payloadLength, 0}; *length = typeLength + payloadLength;
        if(capacity < *length) return NEXUS_GAME_BUFFER_TOO_SMALL;
        std::memcpy(buffer, event.type.data(), typeLength);
        if(payloadLength) std::memcpy(buffer + typeLength, event.payload.data(), payloadLength);
        receiver.events.pop_front(); return NEXUS_GAME_OK;
    });
}
}

GameApiBridge::GameApiBridge(Level& level) {
    if(!level.GetScene()) throw std::invalid_argument("Game API requires a loaded Level.");
    // Editorと同じ専用accessorを先に登録し、生成Reflectionで追加型と削除窓口を補完する。
    // 登録処理自体はWorld lock外で終え、ホストcallbackからの再入を妨げない。
    TypeRegistry types; RegisterSceneTypes(types);
    const auto reflection = level.GetReflectionRegistration();
    reflection.RegisterInspector(types);
    // Details専用の疑似型をComponentとして公開しない。Levelへ保存可能な同じ型集合に限定する。
    ComponentReflection components;
    reflection.RegisterComponents(components);
    std::vector<std::string> excluded;
    for(const auto& [id, type] : types.Types()) {
        static_cast<void>(type);
        if(!components.Types().contains(id) && !(reflection.nativeComponents && reflection.nativeComponents->Contains(id))) excluded.push_back(id);
    }
    for(const auto& id : excluded) types.Unregister(id);
    auto& registry = Worlds();
    {
        std::lock_guard lock(registry.mutex);
        if(registry.legacyPatch) throw std::runtime_error("Cannot create an Interop World during synchronized Native patching.");
        if(registry.next == 0) throw std::overflow_error("Game World token space exhausted.");
        world_ = registry.next++;
        WorldEntry entry{&level, level.GetScene(), std::this_thread::get_id()};
        entry.token = world_;
        entry.types = std::move(types);
        registry.worlds.emplace(world_, std::move(entry));
    }
}
void GameApiBridge::PublishPhysicsContacts() noexcept {
    bool warn=false;
    try {
        {
            auto& registry=Worlds(); std::lock_guard lock(registry.mutex);
            const auto found=registry.worlds.find(world_); if(found==registry.worlds.end()) return;
            auto& world=found->second;
            if(world.thread!=std::this_thread::get_id() || world.serviceDepth || world.scene!=world.level->GetScene()) return;
            if(world.level->PhysicsContacts().empty() && !world.level->DroppedPhysicsContacts()) return;
            std::vector<ServiceClient*> subscribed;
            for(auto& [id,client]:registry.clients) { static_cast<void>(id); if(client.world==world_ && client.physicsEvents) subscribed.push_back(&client); }
            if(subscribed.empty()) return;
            bool overflow=false; uint32_t offset=0;
            for(;;) {
                std::array<uint8_t,4096> payload{}; uint32_t length=0;
                if(CopyPhysicsContacts(*world.level,world_,offset,payload.data(),static_cast<uint32_t>(payload.size()),&length)!=NEXUS_GAME_OK || !world.sequence) { overflow=true; break; }
                NexusPhysicsContactsHeaderV1 header{}; std::memcpy(&header,payload.data(),sizeof(header));
                ServiceEvent event{world.sequence++,{},NEXUS_PHYSICS_PROTOCOL_VERSION,NEXUS_PHYSICS_CONTACT_EVENT,std::vector<uint8_t>(payload.begin(),payload.begin()+length)};
                // Physicsは内部producerなので満杯でゲーム全体を止めない。未購読clientや他のEventを追い出さない。
                for(auto* client:subscribed) {
                    if(client->events.size()>=256) { overflow=true; continue; }
                    client->events.push_back(event);
                }
                if(header.next==UINT32_MAX) break;
                if(header.next<=offset) { overflow=true; break; }
                offset=header.next;
            }
            warn=overflow && !world.physicsOverflow; world.physicsOverflow=overflow;
        }
        // Logger callbackがGame APIへ再入してもdeadlockしないよう、routing lockの外で通知する。
        if(warn) NEXUS_LOG_WARNING("Physics","Contact Event queue is full. Poll subscribed events every frame or use the contact snapshot API.");
    } catch(...) { /* 通知bufferの失敗で物理世界を破壊せず、snapshotの照会経路を保つ。 */ }
}
GameApiBridge::~GameApiBridge() {
    auto& registry = Worlds();
    std::lock_guard lock(registry.mutex);
    std::erase_if(registry.commands, [&](const auto& item) { return item.world == world_; });
    const auto world=registry.worlds.find(world_);
    if(world!=registry.worlds.end()) {
        for(const auto& [client,entry]:registry.clients) if(entry.world==world_) world->second.level->StopAudioOwner(client);
    }
    std::erase_if(registry.clients, [&](const auto& item) { return item.second.world == world_; });
    registry.worlds.erase(world_);
}
bool GameApiBridge::BeginLegacyPatch() noexcept {
    try {
        auto& registry = Worlds(); std::lock_guard lock(registry.mutex);
        if(registry.legacyPatch || registry.patchBlocked || !registry.worlds.empty()) return false;
        registry.legacyPatch = true; registry.patchThread = std::this_thread::get_id(); return true;
    } catch(...) { return false; }
}
bool GameApiBridge::BeginGameModulePatch(Level& level) noexcept {
    try {
        auto& registry = Worlds(); std::lock_guard lock(registry.mutex);
        if(registry.legacyPatch || registry.patchBlocked || registry.worlds.size() != 1) return false;
        const auto& world = registry.worlds.begin()->second;
        if(world.level != &level || world.thread != std::this_thread::get_id() || world.serviceDepth || world.scene != level.GetScene()) return false;
        registry.legacyPatch = true; registry.patchThread = std::this_thread::get_id(); return true;
    } catch(...) { return false; }
}
void GameApiBridge::EndLegacyPatch() noexcept {
    try { auto& registry = Worlds(); std::lock_guard lock(registry.mutex); if(registry.patchThread == std::this_thread::get_id()) registry.legacyPatch = false; } catch(...) {}
}
void GameApiBridge::BlockLegacyPatches() noexcept {
    try { auto& registry = Worlds(); std::lock_guard lock(registry.mutex); registry.patchBlocked = true; } catch(...) {}
}
NexusGameApiV1 GameApiBridge::Api() noexcept {
    return {NEXUS_GAME_ABI_VERSION, sizeof(NexusGameApiV1), Create, Destroy, Alive, Entities, GetTransform, SetTransform, Log};
}
NexusScriptApiV1 GameApiBridge::Scripts() noexcept { return {1, sizeof(NexusScriptApiV1), SetScript, RemoveScript, ScriptState, ScriptEnabled}; }
NexusScriptPropertyApiV1 GameApiBridge::Properties() noexcept { return {1, sizeof(NexusScriptPropertyApiV1), ReadProperties, WriteProperties}; }
NexusGameServicesApiV1 GameApiBridge::Services() noexcept { return {1, sizeof(NexusGameServicesApiV1), OpenManagedServices, CloseServices, RegisterCommand, InvokeCommand, PublishEvent, PollEvent}; }
NexusGameStatus GameApiBridge::OpenNativeServices(uint64_t world, uint64_t* client) noexcept { return OpenServices(world, client, true); }
bool GameApiBridge::FreezeManagedServices(bool frozen) noexcept {
    return Access(world_, [&](Level&) { auto& entry = Worlds().worlds.at(world_); if(entry.serviceDepth) return NEXUS_GAME_FAILED; entry.managedServicesFrozen = frozen; return NEXUS_GAME_OK; }) == NEXUS_GAME_OK;
}
bool GameApiBridge::MatchesWorld(Level& level, uint64_t world) noexcept {
    return Access(world, [&](Level& actual) { return &actual == &level ? NEXUS_GAME_OK : NEXUS_GAME_INVALID_HANDLE; }) == NEXUS_GAME_OK;
}
NexusGameStatus GameApiBridge::BeginNativeCall(uint64_t world) noexcept {
    return Access(world, [&](Level&) {
        auto& entry = Worlds().worlds.at(world);
        if(entry.managedServicesFrozen || entry.serviceDepth >= 8) return NEXUS_GAME_FAILED;
        ++entry.serviceDepth; return NEXUS_GAME_OK;
    });
}
void GameApiBridge::EndNativeCall(uint64_t world) noexcept {
    static_cast<void>(Access(world, [&](Level&) {
        auto& entry = Worlds().worlds.at(world); if(entry.serviceDepth) --entry.serviceDepth;
        return NEXUS_GAME_OK;
    }));
}
bool GameApiBridge::RebindScene() noexcept {
    try {
        auto& registry = Worlds(); std::lock_guard lock(registry.mutex);
        auto found = registry.worlds.find(world_);
        if(found == registry.worlds.end() || found->second.thread != std::this_thread::get_id() || !found->second.level->GetScene()) return false;
        found->second.scene = found->second.level->GetScene(); return true;
    } catch(...) { return false; }
}
}
