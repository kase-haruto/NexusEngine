#include "NativeGameSystemScope.h"
#include "GameApiBridge.h"
#include "Runtime/Reflection/NativeComponentMetadata.h"
#include "Runtime/Scene/Components/GameComponentData.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Reflection/ComponentReflection.h"
#include "Foundation/Logging/Logger.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <cstring>
#include <limits>
#include <Windows.h>

namespace NexusEngine {
namespace {
/*-----------------------------------------------------------------------------------------
 * NativeGameSystemAdapter
 * - DLLのSystem stateを既存ISceneSystemの逐次実行契約へ接続するEngine所有adapter
 * - DLL stateはDLLのdestroyで解放し、Scene・ECS Storageや別Schedulerを所有しない
 *---------------------------------------------------------------------------------------*/
class NativeGameSystemAdapter final : public ISceneSystem {
public:
    Level& level; //< Schedulerを所有するLevel。Module scopeより長く生存する
    uint64_t world, handle; std::string id; uint32_t version; //< pointerを公開しない識別情報
    ComponentAccess access; //< 既存ComponentReflectionで解決した診断用metadata
    std::vector<std::string> after; //< 先に登録済みの同Module依存。逆順停止で依存先を最後まで維持する
    std::thread::id thread = std::this_thread::get_id(); //< create / Update / access / destroyの所有thread
    std::shared_ptr<bool> clean; //< Scopeよりadapterが残っても失敗情報だけを保持する
    NexusNativeSystemDescriptorV1 callbacks{}; //< Engine / DLL境界の関数表。文字列pointerはコピー後に落とす
    void* state = nullptr; //< DLLが確保し、DLLだけが解放する
    bool busy = false, detached = false; //< 再入と無効化後のcallbackを拒否する
    bool patchPaused = false; //< パッチ境界でSystem.UpdateとNative借用も停止する
    bool stateTransfer = false; //< Engineが明示Restoreのstack内だけ設定する。通常Bindingは再開しない

    NativeGameSystemAdapter(Level& owner, uint64_t token, uint64_t generation, std::string name, uint32_t apiVersion, std::shared_ptr<bool> health)
        : level(owner), world(token), handle(generation), id(std::move(name)), version(apiVersion), clean(std::move(health)) {}
    ~NativeGameSystemAdapter() override { static_cast<void>(Stop()); }
    /** \brief SystemをDLL側で作る。部分生成のstateも同じdestroy経路へ渡す */
    NexusGameStatus Initialize() noexcept {
        if(busy || detached) return NEXUS_GAME_FAILED;
        busy = true; NexusGameStatus status = NEXUS_GAME_FAILED;
        const NexusNativeSystemContextV1 context{1, sizeof(NexusNativeSystemContextV1), GameApiBridge::Api(), world, handle};
        try { status = callbacks.create(&context, &state); } catch(...) { status = NEXUS_GAME_FAILED; }
        busy = false; return status == NEXUS_GAME_OK && !state ? NEXUS_GAME_FAILED : status;
    }
    std::span<const std::type_index> GetReadComponents() const noexcept override { return access.reads; }
    std::span<const std::type_index> GetWriteComponents() const noexcept override { return access.writes; }
    // Game Public APIはEntity生成・削除も許す。型宣言だけで将来並列実行できるとは扱わない。
    bool HasUnspecifiedComponentAccess() const noexcept override { return true; }
    /** \brief Schedulerが渡した現在Sceneを検査し、DLLのUpdateへだけ転送する */
    Result<void> Update(Scene& scene, float deltaTime) override {
        if(detached) return {};
        if(patchPaused) return std::unexpected(Error(ErrorCategory::Scene, 237, "Native System is suspended for patching: " + id));
        if(busy || thread != std::this_thread::get_id() || level.GetScene() != &scene || !GameApiBridge::MatchesWorld(level, world) || !std::isfinite(deltaTime) || deltaTime < 0)
            return std::unexpected(Error(ErrorCategory::Scene, 237, "Native System update requires its current World and owner thread: " + id));
        busy = true; NexusGameStatus status = NEXUS_GAME_FAILED;
        try { status = callbacks.update(state, deltaTime); } catch(...) { status = NEXUS_GAME_FAILED; }
        busy = false;
        if(status == NEXUS_GAME_OK) return {};
        return std::unexpected(Error(ErrorCategory::Scene, 237, "Native System update failed: " + id + " (" + std::to_string(status) + ")."));
    }
    /** \brief Native callback中だけDLL stateを借用する。取得と処理を別APIにして無期限のpointer返却を避ける */
    NexusGameStatus Visit(NexusNativeSystemVisitorFn visitor, void* user) noexcept {
        if(detached || !state) return NEXUS_GAME_INVALID_HANDLE;
        if(patchPaused && !stateTransfer) return NEXUS_GAME_BINDING_DISABLED;
        if(busy) return NEXUS_GAME_FAILED;
        busy = true; NexusGameStatus result = NEXUS_GAME_FAILED;
        try { result = visitor(state, user); } catch(...) { result = NEXUS_GAME_FAILED; }
        busy = false; return result;
    }
    /** \brief 安全停止できない場合はcallbackを切り離し、呼出元にDLL pinを要求する */
    void Detach() noexcept { detached = true; callbacks = {}; state = nullptr; *clean = false; }
    /** \brief 登録が無効になった後にDLL内でstateを破棄する。失敗は自動rollbackできない */
    bool Stop() noexcept {
        if(detached) return *clean;
        if(busy || thread != std::this_thread::get_id()) { Detach(); return false; }
        auto destroy = callbacks.destroy; auto* instance = state;
        detached = true; state = nullptr; callbacks = {};
        if(instance) {
            try { if(!destroy || destroy(instance) != NEXUS_GAME_OK) *clean = false; }
            catch(...) { *clean = false; }
        }
        return *clean;
    }
};
}
/*-----------------------------------------------------------------------------------------
 * NativeGameSystemScope::Impl
 * - Module scopeの数値routingとSchedulerへの登録・解除を担当する
 * - systemsは非所有追跡だけ。公開契約の値は独立したMetadata containerへ委譲する
 *---------------------------------------------------------------------------------------*/
struct NativeGameSystemScope::Impl {
    Level& level; uint64_t world, token = 0; //< Worldは既存GameApiBridgeの寿命token
    std::thread::id thread = std::this_thread::get_id(); //< 登録と参照の所有thread
    std::vector<NativeGameSystemAdapter*> systems; //< 所有者はlevelのScheduler。Module停止時だけ解除する
    std::map<std::string, std::type_index, std::less<>> componentTypes; //< 既存ReflectionのIDをNative型へ解決した値コピー
    NativeGameApiMetadata publications; //< Systemを所有しない関数契約だけを保持する
    std::string moduleId; //< Loaderが照合済みのProject Module ID。型名やDLL pathに依存させない
    struct Binding { uint64_t declaration, generation; NexusNativeFunctionFn callback; }; //< System stateは所有しない
    std::vector<Binding> bindings; //< DLL callbackはscope失効前だけ借用し、Managedへ渡さない
    std::vector<Binding> staged; //< パッチ後の再登録候補。全検証後だけ公開し、部分更新を見せない
    bool patching = false; //< Compile失敗では既存Handleを保持する専用の停止状態
    bool patchApplied = false; //< 適用後に非適用だったことにして旧表へ戻す操作を拒否する
    bool enabled = true; //< 停止・再登録中の呼出しを拒否する
    std::shared_ptr<bool> clean = std::make_shared<bool>(true); //< Native破棄失敗のQuarantine判定
    bool sealed = false, closed = false, mutating = false; //< 初期登録後とcallback中の変更を拒否する
    /* 数値routing以外のSystem Registryは増やさない。weak参照はModule所有権を延ばさない。 */
    struct Routes {
        std::mutex mutex;
        std::map<uint64_t, std::weak_ptr<Impl>> scopes;
        uint64_t next = 1; //< scope / System / 関数世代をProcess内で再利用しない
    };
    static Routes& Routing() { static Routes routes; return routes; }
    static uint64_t Generation() {
        auto& routes = Routing(); std::lock_guard lock(routes.mutex);
        if(!routes.next) throw std::overflow_error("Native System handle space exhausted.");
        return routes.next++;
    }
    Impl(Level& owner, uint64_t tokenValue, std::string_view module) : level(owner), world(tokenValue), moduleId(module) {
        static_cast<void>(NativeGameApiMetadata::Identifier({moduleId.data(), static_cast<uint32_t>(moduleId.size())}));
        if(!GameApiBridge::MatchesWorld(level, world)) throw std::invalid_argument("Native System scope requires the Level's current World.");
        ComponentReflection reflection;
        const auto registration = level.GetReflectionRegistration(); registration.RegisterComponents(reflection);
        for(const auto& [id, type] : reflection.Types()) componentTypes.emplace(id, type.nativeType);
        // 値Componentは共通Storageを使う。宣言は既存Schedulerへ同じStorage依存として渡す。
        if(registration.nativeComponents) for(const auto& type : registration.nativeComponents->Types()) componentTypes.emplace(type.id, typeid(GameComponentData));
    }
    /** \brief owner threadのscopeを一時保持する。Native callbackをrouting lock中に呼ばない */
    template<class Function> static NexusGameStatus Dispatch(uint64_t tokenValue, Function&& function) noexcept {
        try {
            std::shared_ptr<Impl> scope;
            { auto& routes = Routing(); std::lock_guard lock(routes.mutex); auto found = routes.scopes.find(tokenValue); if(found == routes.scopes.end()) return NEXUS_GAME_INVALID_HANDLE; scope = found->second.lock(); }
            if(!scope) return NEXUS_GAME_INVALID_HANDLE;
            if(scope->thread != std::this_thread::get_id()) return NEXUS_GAME_WRONG_THREAD;
            if(scope->closed) return NEXUS_GAME_INVALID_HANDLE;
            if(!GameApiBridge::MatchesWorld(scope->level, scope->world)) return NEXUS_GAME_INVALID_HANDLE;
            return function(*scope);
        } catch(const std::invalid_argument&) { return NEXUS_GAME_INVALID_ARGUMENT; }
        catch(...) { return NEXUS_GAME_FAILED; }
    }
    NativeGameSystemAdapter* Find(uint64_t system) const noexcept {
        for(auto* candidate : systems) if(candidate->handle == system && !candidate->detached) return candidate;
        return nullptr;
    }
    NativeGameSystemAdapter* Find(std::string_view id) const noexcept {
        for(auto* candidate : systems) if(candidate->id == id && !candidate->detached) return candidate;
        return nullptr;
    }
    /** \brief Component名だけを既存Reflectionから解決する。独自TypeRegistryを作らない */
    std::vector<std::type_index> AccessTypes(const NexusGameString* ids, uint32_t count) const {
        if(count > 32 || (count && !ids)) throw std::invalid_argument("Invalid Component access declaration.");
        std::vector<std::type_index> result; std::set<std::string> names;
        for(uint32_t index = 0; index < count; ++index) {
            const auto name = NativeGameApiMetadata::Identifier(ids[index]); auto found = componentTypes.find(name);
            if(found == componentTypes.end() || !names.insert(name).second) throw std::invalid_argument("Unknown or duplicate Component Type ID.");
            result.push_back(found->second);
        }
        return result;
    }
    static NexusGameStatus NEXUS_GAME_CALL Add(uint64_t scope, const NexusNativeSystemDescriptorV1* descriptor, uint64_t* output) noexcept {
        if(!output) return NEXUS_GAME_INVALID_ARGUMENT; *output = 0;
        if(!descriptor || descriptor->structSize != sizeof(*descriptor) || !descriptor->version || descriptor->reserved || descriptor->reservedReads || descriptor->reservedWrites ||
            !descriptor->create || !descriptor->update || !descriptor->destroy || descriptor->afterCount > 64 || (descriptor->afterCount && !descriptor->after)) return NEXUS_GAME_INVALID_ARGUMENT;
        return Dispatch(scope, [&](Impl& self) {
            auto& scheduler = self.level.GetSystemScheduler();
            if(self.sealed || self.mutating || scheduler.IsExecuting() || self.systems.size() >= 64) return NEXUS_GAME_FAILED;
            const auto id = NativeGameApiMetadata::Identifier(descriptor->id); if(self.Find(id)) return NEXUS_GAME_FAILED;
            self.mutating = true; struct Reset { bool& flag; ~Reset() { flag = false; } } reset{self.mutating};
            auto adapter = std::make_unique<NativeGameSystemAdapter>(self.level, self.world, Generation(), id, descriptor->version, self.clean);
            std::set<std::string> dependencies;
            for(uint32_t index = 0; index < descriptor->afterCount; ++index) {
                auto dependency = NativeGameApiMetadata::Identifier(descriptor->after[index]);
                if(!self.Find(dependency) || !dependencies.insert(dependency).second) return NEXUS_GAME_INVALID_ARGUMENT;
                adapter->after.push_back(std::move(dependency));
            }
            adapter->access.reads = self.AccessTypes(descriptor->reads, descriptor->readCount);
            adapter->access.writes = self.AccessTypes(descriptor->writes, descriptor->writeCount);
            adapter->callbacks.create = descriptor->create; adapter->callbacks.update = descriptor->update; adapter->callbacks.destroy = descriptor->destroy;
            self.systems.reserve(self.systems.size() + 1);
            const auto initialized = adapter->Initialize();
            if(initialized != NEXUS_GAME_OK) return initialized;
            if(self.closed) return NEXUS_GAME_INVALID_HANDLE;
            auto* pointer = adapter.get(); scheduler.AddOwnedSystem(std::move(adapter));
            self.systems.push_back(pointer); *output = pointer->handle; return NEXUS_GAME_OK;
        });
    }
    static NexusGameStatus NEXUS_GAME_CALL FindSystem(uint64_t scope, NexusGameString id, uint32_t version, uint64_t* output) noexcept {
        if(!output) return NEXUS_GAME_INVALID_ARGUMENT; *output = 0;
        if(!version) return NEXUS_GAME_INVALID_ARGUMENT;
        return Dispatch(scope, [&](Impl& self) {
            auto* system = self.Find(NativeGameApiMetadata::Identifier(id)); if(!system) return NEXUS_GAME_INVALID_HANDLE;
            if(system->version != version) return NEXUS_GAME_INCOMPATIBLE_ABI;
            *output = system->handle; return NEXUS_GAME_OK;
        });
    }
    static NexusGameStatus NEXUS_GAME_CALL Access(uint64_t scope, uint64_t handle, NexusNativeSystemVisitorFn visitor, void* user) noexcept {
        if(!visitor) return NEXUS_GAME_INVALID_ARGUMENT;
        return Dispatch(scope, [&](Impl& self) { auto* system = self.Find(handle); return system ? system->Visit(visitor, user) : NEXUS_GAME_INVALID_HANDLE; });
    }
    static NexusGameStatus NEXUS_GAME_CALL Declare(uint64_t scope, uint64_t handle, const NexusNativeFunctionDescriptorV1* descriptor) noexcept {
        if(!descriptor) return NEXUS_GAME_INVALID_ARGUMENT;
        return Dispatch(scope, [&](Impl& self) {
            if(self.sealed || self.mutating || self.level.GetSystemScheduler().IsExecuting()) return NEXUS_GAME_FAILED;
            auto* system = self.Find(handle); if(!system) return NEXUS_GAME_INVALID_HANDLE;
            return self.publications.Declare(Generation(), handle, system->id, *descriptor);
        });
    }
    static NexusGameStatus NEXUS_GAME_CALL FindFunction(uint64_t scope, uint64_t handle, NexusGameString id, uint32_t version, NexusNativeFunctionInfoV1* output) noexcept {
        if(!output) return NEXUS_GAME_INVALID_ARGUMENT; *output = {};
        if(!version) return NEXUS_GAME_INVALID_ARGUMENT;
        return Dispatch(scope, [&](Impl& self) {
            if(!self.Find(handle)) return NEXUS_GAME_INVALID_HANDLE;
            const auto name = NativeGameApiMetadata::Identifier(id); const auto* function = self.publications.Find(handle, name, version);
            if(!function) {
                for(const auto& current : self.publications.Functions()) if(current.system == handle && current.id == name) return NEXUS_GAME_INCOMPATIBLE_ABI;
                return NEXUS_GAME_INVALID_HANDLE;
            }
            *output = {function->generation, handle, function->version, static_cast<uint32_t>(function->parameters.size()), function->result.kind, 1};
            return NEXUS_GAME_OK;
        });
    }
    /** \brief Metadataとは別に呼出先を結び付ける。更新時は停止中だけ再登録し世代を変える */
    static NexusGameStatus NEXUS_GAME_CALL Bind(uint64_t scope, uint64_t handle, NexusGameString id, uint32_t version, NexusNativeFunctionFn callback) noexcept {
        if(!callback || !version) return NEXUS_GAME_INVALID_ARGUMENT;
        return Dispatch(scope, [&](Impl& self) {
            if(self.mutating || self.level.GetSystemScheduler().IsExecuting() || (self.sealed && self.enabled) ||
                std::ranges::any_of(self.systems, [](const auto* system) { return system->busy; })) return NEXUS_GAME_FAILED;
            if(!self.Find(handle)) return NEXUS_GAME_SYSTEM_NOT_REGISTERED;
            const auto name = NativeGameApiMetadata::Identifier(id);
            const auto* function = self.publications.Find(handle, name, version);
            if(!function) {
                for(const auto& declaration : self.publications.Functions()) if(declaration.system == handle && declaration.id == name) return NEXUS_GAME_INCOMPATIBLE_ABI;
                return NEXUS_GAME_FUNCTION_NOT_REGISTERED;
            }
            if(self.patching && !std::ranges::any_of(self.bindings, [&](const auto& entry) { return entry.declaration == function->generation; })) return NEXUS_GAME_INCOMPATIBLE_ABI;
            auto& target = self.patching ? self.staged : self.bindings;
            for(auto& binding : target) if(binding.declaration == function->generation) {
                if(self.patching) return NEXUS_GAME_FAILED;
                if(self.enabled) return NEXUS_GAME_FAILED;
                binding.generation = Generation(); binding.callback = callback; return NEXUS_GAME_OK;
            }
            target.push_back({function->generation, Generation(), callback}); return NEXUS_GAME_OK;
        });
    }
    /** \brief 古いHandleを一時停止の後にも復活させない。callback中の停止は拒否する */
    static NexusGameStatus NEXUS_GAME_CALL EnableBindings(uint64_t scope, uint32_t enabledValue) noexcept {
        if(enabledValue > 1) return NEXUS_GAME_INVALID_ARGUMENT;
        return Dispatch(scope, [&](Impl& self) {
            if(self.patching || self.mutating || self.level.GetSystemScheduler().IsExecuting() ||
                std::ranges::any_of(self.systems, [](const auto* system) { return system->busy; })) return NEXUS_GAME_FAILED;
            if(!enabledValue) {
                self.enabled = false;
                for(auto& binding : self.bindings) binding.generation = Generation();
            } else self.enabled = true;
            return NEXUS_GAME_OK;
        });
    }
    /** \brief IDだけでなく引数順・値型・Enum IDまで照合して型付きFacadeの誤接続を防ぐ */
    static bool Compatible(const NativeGameApiMetadata::Function& actual, const NativeGameApiMetadata::Function& expected) noexcept {
        const auto same = [](const auto& left, const auto& right) { return left.kind == right.kind && left.typeId == right.typeId; };
        if(!same(actual.result, expected.result) || actual.parameters.size() != expected.parameters.size()) return false;
        for(size_t index = 0; index < actual.parameters.size(); ++index) if(!same(actual.parameters[index].type, expected.parameters[index].type)) return false;
        return true;
    }
    static NexusGameStatus NEXUS_GAME_CALL ResolveBinding(uint64_t world, NexusGameString module, NexusGameString systemId, uint32_t systemVersion,
        const NexusNativeFunctionDescriptorV1* signature, NexusNativeBindingHandleV1* output) noexcept {
        if(!output) return NEXUS_GAME_INVALID_ARGUMENT; *output = {};
        if(!signature || !systemVersion) return NEXUS_GAME_INVALID_ARGUMENT;
        try {
            const auto moduleName = NativeGameApiMetadata::Identifier(module), systemName = NativeGameApiMetadata::Identifier(systemId);
            uint64_t selected = 0;
            // 既存scope routingを再利用する。Moduleの曖昧な重複を任意に選ばない。
            { auto& routes = Routing(); std::lock_guard lock(routes.mutex);
                for(const auto& [tokenValue, weak] : routes.scopes) if(auto scope = weak.lock(); scope && scope->world == world && scope->moduleId == moduleName) {
                    if(selected) return NEXUS_GAME_FAILED; selected = tokenValue;
                }
            }
            if(!selected) return NEXUS_GAME_SYSTEM_NOT_REGISTERED;
            return Dispatch(selected, [&](Impl& self) {
                if(!self.enabled) return NEXUS_GAME_BINDING_DISABLED;
                auto* system = self.Find(systemName); if(!system) return NEXUS_GAME_SYSTEM_NOT_REGISTERED;
                if(system->version != systemVersion) return NEXUS_GAME_INCOMPATIBLE_ABI;
                NativeGameApiMetadata expected;
                const auto copied = expected.Declare(1, 1, systemName, *signature); if(copied != NEXUS_GAME_OK) return copied;
                const auto name = NativeGameApiMetadata::Identifier(signature->id);
                const auto* function = self.publications.Find(system->handle, name, signature->version);
                if(!function) {
                    for(const auto& entry : self.publications.Functions()) if(entry.system == system->handle && entry.id == name) return NEXUS_GAME_INCOMPATIBLE_ABI;
                    return NEXUS_GAME_FUNCTION_NOT_REGISTERED;
                }
                if(!Compatible(*function, expected.Functions().front())) return NEXUS_GAME_INCOMPATIBLE_ABI;
                for(const auto& binding : self.bindings) if(binding.declaration == function->generation) {
                    *output = {self.token, system->handle, binding.generation}; return NEXUS_GAME_OK;
                }
                return NEXUS_GAME_FUNCTION_NOT_REGISTERED;
            });
        } catch(const std::invalid_argument&) { return NEXUS_GAME_INVALID_ARGUMENT; } catch(...) { return NEXUS_GAME_FAILED; }
    }
    /** \brief 固定DTOの値域・未使用field・借用範囲・Entity世代を呼出前後に検証する */
    NexusGameStatus ValidateValue(const NexusNativeValueV1& value, const NativeGameApiMetadata::ValueType& type) const noexcept {
        if(value.kind != type.kind) return NEXUS_GAME_INVALID_ARGUMENT;
        const bool buffer = type.kind == NEXUS_VALUE_UTF8 || type.kind == NEXUS_VALUE_FLOAT32_ARRAY;
        if(!buffer && (value.count || value.data)) return NEXUS_GAME_INVALID_ARGUMENT;
        if(type.kind != NEXUS_VALUE_ENTITY && (value.entity.world || value.entity.index || value.entity.generation)) return NEXUS_GAME_INVALID_ARGUMENT;
        if(type.kind != NEXUS_VALUE_BOOL && type.kind != NEXUS_VALUE_INT32 && type.kind != NEXUS_VALUE_UINT32 && type.kind != NEXUS_VALUE_ENUM32 && value.integer) return NEXUS_GAME_INVALID_ARGUMENT;
        uint32_t dimensions = 0;
        if(type.kind == NEXUS_VALUE_FLOAT32) dimensions = 1;
        if(type.kind >= NEXUS_VALUE_VECTOR2 && type.kind <= NEXUS_VALUE_VECTOR4) dimensions = type.kind - NEXUS_VALUE_VECTOR2 + 2;
        for(uint32_t index = 0; index < 4; ++index) if(index < dimensions ? !std::isfinite(value.number[index]) : value.number[index] != 0) return NEXUS_GAME_INVALID_ARGUMENT;
        if(type.kind == NEXUS_VALUE_BOOL && value.integer != 0 && value.integer != 1) return NEXUS_GAME_INVALID_ARGUMENT;
        if((type.kind == NEXUS_VALUE_INT32 || type.kind == NEXUS_VALUE_ENUM32) && (value.integer < INT32_MIN || value.integer > INT32_MAX)) return NEXUS_GAME_INVALID_ARGUMENT;
        if(type.kind == NEXUS_VALUE_UINT32 && (value.integer < 0 || value.integer > UINT32_MAX)) return NEXUS_GAME_INVALID_ARGUMENT;
        if(type.kind == NEXUS_VALUE_ENTITY) {
            if(value.entity.world != world) return NEXUS_GAME_INVALID_HANDLE;
            return GameApiBridge::Api().isEntityAlive(value.entity);
        }
        if(buffer) {
            const auto limit = type.kind == NEXUS_VALUE_UTF8 ? 4096u : 1024u;
            if(value.count > limit || (value.count ? !value.data : value.data != nullptr)) return NEXUS_GAME_INVALID_ARGUMENT;
            if(type.kind == NEXUS_VALUE_UTF8 && value.count && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, static_cast<const char*>(value.data), static_cast<int>(value.count), nullptr, 0) <= 0) return NEXUS_GAME_INVALID_ARGUMENT;
            if(type.kind == NEXUS_VALUE_FLOAT32_ARRAY) for(uint32_t index = 0; index < value.count; ++index) {
                float number = 0; std::memcpy(&number, static_cast<const uint8_t*>(value.data) + index * sizeof(float), sizeof(float));
                if(!std::isfinite(number)) return NEXUS_GAME_INVALID_ARGUMENT;
            }
        }
        return NEXUS_GAME_OK;
    }
    static NexusGameStatus NEXUS_GAME_CALL InvokeBinding(uint64_t world, NexusNativeBindingHandleV1 handle, const NexusNativeValueV1* arguments, uint32_t count,
        NexusNativeValueV1* output, uint8_t* buffer, uint32_t capacity) noexcept {
        if(!output) return NEXUS_GAME_INVALID_ARGUMENT; *output = {};
        if(count > 16 || (count && !arguments) || capacity > 4096 || (capacity && !buffer)) return NEXUS_GAME_INVALID_ARGUMENT;
        return Dispatch(handle.scope, [&](Impl& self) {
            if(world != self.world) return NEXUS_GAME_INVALID_HANDLE;
            if(!self.enabled) return NEXUS_GAME_BINDING_DISABLED;
            auto* system = self.Find(handle.system); if(!system) return NEXUS_GAME_SYSTEM_NOT_REGISTERED;
            const NativeGameApiMetadata::Function* function = nullptr; NexusNativeFunctionFn callback = nullptr;
            for(const auto& binding : self.bindings) if(binding.generation == handle.function)
                for(const auto& declaration : self.publications.Functions()) if(declaration.generation == binding.declaration && declaration.system == handle.system) { function = &declaration; callback = binding.callback; }
            if(!function || !callback) return NEXUS_GAME_STALE_BINDING;
            if(count != function->parameters.size()) return NEXUS_GAME_INVALID_ARGUMENT;
            for(uint32_t index = 0; index < count; ++index) { const auto status = self.ValidateValue(arguments[index], function->parameters[index].type); if(status != NEXUS_GAME_OK) return status; }
            // callbackでscopeが失効しても署名pointerを再参照しない。結果型だけを値で保持する。
            const auto resultType = function->result;
            const auto entered = GameApiBridge::BeginNativeCall(world); if(entered != NEXUS_GAME_OK) return entered;
            struct Leave { uint64_t world; ~Leave() { GameApiBridge::EndNativeCall(world); } } leave{world};
            struct Call {
                NexusNativeFunctionFn callback; const NexusNativeValueV1* arguments; uint32_t count; NexusNativeValueV1 result{}; uint8_t* buffer; uint32_t capacity;
                static NexusGameStatus NEXUS_GAME_CALL Run(void* system, void* context) {
                    auto& call = *static_cast<Call*>(context); return call.callback(system, call.arguments, call.count, &call.result, call.buffer, call.capacity);
                }
            } call{callback, arguments, count, {}, buffer, capacity};
            const auto called = system->Visit(Call::Run, &call);
            // DLLが予約statusを返しても再送可能と誤認しない。callback後の失敗は自動再送しない。
            if(called != NEXUS_GAME_OK) return called == NEXUS_GAME_STALE_BINDING ? NEXUS_GAME_FAILED : called;
            if(self.closed || !self.enabled) return NEXUS_GAME_INVALID_HANDLE;
            // DLLが返したpointerを任意にdereferenceしない。可変長結果は呼出元buffer内だけに限定する。
            if(call.result.kind == NEXUS_VALUE_UTF8 || call.result.kind == NEXUS_VALUE_FLOAT32_ARRAY) {
                const uint64_t bytes = static_cast<uint64_t>(call.result.count) * (call.result.kind == NEXUS_VALUE_UTF8 ? 1 : sizeof(float));
                if(bytes > capacity || (bytes && call.result.data != buffer)) return NEXUS_GAME_INVALID_ARGUMENT;
            }
            const auto valid = self.ValidateValue(call.result, resultType); if(valid != NEXUS_GAME_OK) return valid;
            *output = call.result; return NEXUS_GAME_OK;
        });
    }
    bool Close() noexcept {
        // 誤threadからSchedulerやbusy状態を書き換えない。Loader側がDLLをpinし、owner threadでの終了を要求する。
        if(thread != std::this_thread::get_id()) return false;
        if(closed) return *clean;
        closed = true; enabled = false; patching = false; patchApplied = false; staged.clear(); bindings.clear(); publications.Clear();
        try { auto& routes = Routing(); std::lock_guard lock(routes.mutex); routes.scopes.erase(token); } catch(...) { *clean = false; }
        auto& scheduler = level.GetSystemScheduler();
        if(scheduler.IsExecuting() || mutating || std::ranges::any_of(systems, [](const auto* system) { return system->busy; })) {
            // 更新中のEngine adapterを削除しない。後続callbackを切り、DLL stateはpinしたコードと共に残す。
            for(auto* system : systems) system->Detach();
            *clean = false; return false;
        }
        for(auto position = systems.rbegin(); position != systems.rend(); ++position) {
            static_cast<void>((*position)->Stop());
            if(!scheduler.RemoveSystem(**position)) *clean = false;
        }
        systems.clear(); return *clean;
    }
};
NativeGameSystemScope::NativeGameSystemScope(Level& level, uint64_t world, std::string_view moduleId) : impl_(std::make_shared<Impl>(level, world, moduleId)) {
    impl_->token = Impl::Generation(); auto& routes = Impl::Routing(); std::lock_guard lock(routes.mutex); routes.scopes.emplace(impl_->token, impl_);
}
NativeGameSystemScope::~NativeGameSystemScope() {
    static_cast<void>(Close());
    // 例外的な誤thread破棄でも、数値routingだけは失効する。DLL stateの解放はLoaderがpinして防ぐ。
    try { auto& routes = Impl::Routing(); std::lock_guard lock(routes.mutex); routes.scopes.erase(impl_->token); } catch(...) {}
}
NexusNativeSystemsRegistrationV1 NativeGameSystemScope::Context() const noexcept {
    return {1, sizeof(NexusNativeSystemsRegistrationV1), impl_->token,
        {1, sizeof(NexusNativeSystemsApiV1), Impl::Add, Impl::FindSystem, Impl::Access},
        {1, sizeof(NexusNativeSystemPublicApiV1), Impl::Declare, Impl::FindFunction}};
}
void NativeGameSystemScope::Seal() noexcept { if(impl_->thread == std::this_thread::get_id()) impl_->sealed = true; }
NexusNativeSystemsRegistrationV2 NativeGameSystemScope::BindingContext() const noexcept {
    auto context = Context(); context.abiVersion = 2; context.structSize = sizeof(NexusNativeSystemsRegistrationV2);
    return {context, {1, sizeof(NexusNativeBindingRegistrationV1), Impl::Bind, Impl::EnableBindings}};
}
NexusNativeBindingApiV1 NativeGameSystemScope::BindingApi() noexcept {
    return {1, sizeof(NexusNativeBindingApiV1), Impl::ResolveBinding, Impl::InvokeBinding};
}
bool NativeGameSystemScope::PreparePatch() noexcept {
    auto& self = *impl_;
    if(self.thread != std::this_thread::get_id() || self.closed || !self.sealed || !self.enabled || self.patching || self.mutating ||
        self.level.GetSystemScheduler().IsExecuting() || !GameApiBridge::MatchesWorld(self.level, self.world) ||
        std::ranges::any_of(self.systems, [](const auto* system) { return system->busy; })) return false;
    self.staged.clear(); self.patching = true; self.patchApplied = false; self.enabled = false;
    for(auto* system : self.systems) system->patchPaused = true;
    return true;
}
bool NativeGameSystemScope::RestoreState(NexusRestoreNativeGameStateFn restore,void* state,uint64_t schema,std::span<const uint8_t> data) noexcept {
    auto& self=*impl_;
    if(!restore || !state || !schema || data.size()>NEXUS_NATIVE_STATE_MAX_BYTES || self.thread!=std::this_thread::get_id() || self.closed ||
        !self.patching || self.enabled || self.mutating || self.level.GetSystemScheduler().IsExecuting() || !GameApiBridge::MatchesWorld(self.level,self.world) ||
        std::ranges::any_of(self.systems,[](const auto* system) { return system->busy; })) return false;
    // ModuleのRestoreは既存Systemへ保存値を返せるが、登録変更・Update・公開Bindingは停止したままにする。
    self.mutating=true;
    for(auto* system:self.systems) system->stateTransfer=true;
    struct Guard {
        Impl& self;
        ~Guard() { for(auto* system:self.systems) system->stateTransfer=false; self.mutating=false; }
    } guard{self};
    try { return restore(state,1,schema,data.data(),static_cast<uint32_t>(data.size()))==NEXUS_GAME_OK; }
    catch(...) { return false; }
}
bool NativeGameSystemScope::FinishPatch(bool applied) noexcept {
    auto& self = *impl_;
    if(self.thread != std::this_thread::get_id() || self.closed || !self.patching || self.level.GetSystemScheduler().IsExecuting() || !GameApiBridge::MatchesWorld(self.level, self.world) ||
        std::ranges::any_of(self.systems, [](const auto* system) { return system->busy; })) return false;
    if(!applied && self.patchApplied) return false;
    if(applied) {
        self.patchApplied = true;
        if(self.staged.size() != self.bindings.size()) return false;
        for(const auto& original : self.bindings)
            if(!std::ranges::any_of(self.staged, [&](const auto& candidate) { return candidate.declaration == original.declaration; })) return false;
        self.bindings.swap(self.staged);
    }
    self.staged.clear(); self.patching = false; self.enabled = true;
    for(auto* system : self.systems) system->patchPaused = false;
    return true;
}
void NativeGameSystemScope::Abandon() noexcept {
    if(impl_->thread != std::this_thread::get_id()) return;
    // レイアウト不一致時は新しいdestroyを旧stateへ適用しない。Engine adapterだけを安全に解除する。
    for(auto* system : impl_->systems) system->Detach();
    *impl_->clean = false; static_cast<void>(Close());
}
bool NativeGameSystemScope::Close() noexcept {
    const auto stopped = impl_->Close();
    if(!stopped) NEXUS_LOG_ERROR("GameModule", "Native System shutdown failed; pin the owning DLL and restart the host.");
    return stopped;
}
const NativeGameApiMetadata& NativeGameSystemScope::Publications() const noexcept { return impl_->publications; }
}
