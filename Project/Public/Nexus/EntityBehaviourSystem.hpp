#pragma once
#include "GameComponents.hpp"
#include "NativeGameSystems.h"
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace NexusGame {
/*-----------------------------------------------------------------------------------------
 * EntityBehaviourContext
 * - DLL所有BehaviourへEntityと公開値APIだけを渡す。ECS内部やComponent pointerは公開しない
 * - 関連付け世代を毎回検証し、同じEntityへのComponent再追加後に旧instanceが書き込むのを防ぐ
 * - owner thread専用。Behaviourが借用し、その破棄後にSystemが解放する
 *---------------------------------------------------------------------------------------*/
class EntityBehaviourContext final {
public:
    /** \brief 同じDLL内のSystemが検証済みWorld・関連付け値から借用Contextを構築する */
    EntityBehaviourContext(const NexusGameApiV1& game, const NexusGameServicesContextV1& services,
        NexusEntityHandle entity, std::string type, uint64_t attachment)
        : game_(game), components_(services), entity_(entity), type_(std::move(type)), attachment_(attachment) {}
    EntityBehaviourContext(const EntityBehaviourContext&) = delete;
    EntityBehaviourContext& operator=(const EntityBehaviourContext&) = delete;
    /** \brief pointerではなくWorld / Entity世代付き識別子を返す */
    NexusEntityHandle Entity() const noexcept { return entity_; }
    /** \brief 自身の公開Propertyを値コピーで取得する */
    NexusGameStatus Read(NexusGameString property, NexusComponentValueV1& value, char* text = nullptr, uint32_t capacity = 0) const noexcept {
        return components_.Read(entity_, Type(), property, value, text, capacity, attachment_);
    }
    /** \brief 旧attachmentまたは終了中のinstanceからの書込みを拒否する */
    NexusGameStatus Write(NexusGameString property, NexusComponentValueV1 value, NexusGameString text = {}) const noexcept {
        return closing_ ? NEXUS_GAME_INVALID_HANDLE : components_.Write(entity_, Type(), property, value, text, attachment_);
    }
    /** \brief 現在も同じ関連付けであることを確認してLocal Transformをコピーする */
    NexusGameStatus GetTransform(NexusLocalTransform& value) const noexcept {
        value = {}; const auto status = Validate();
        return status == NEXUS_GAME_OK ? game_.getLocalTransform(entity_, &value) : status;
    }
    /** \brief Worldの変更は通常実行中だけ許可する。終了callbackは所有資源の解放に限定する */
    NexusGameStatus SetTransform(const NexusLocalTransform& value) const noexcept {
        if(closing_) return NEXUS_GAME_INVALID_HANDLE;
        const auto status = Validate(); return status == NEXUS_GAME_OK ? game_.setLocalTransform(entity_, &value) : status;
    }
    /** \brief System内部の照合用。0や再利用されたtokenを同一instanceと扱わない */
    uint64_t Attachment() const noexcept { return attachment_; }
    /** \brief OnDisable / OnDestroyより先に終了時書込みを停止する */
    void BeginDestroy() noexcept { closing_ = true; }
private:
    NexusGameString Type() const noexcept { return {type_.data(), static_cast<uint32_t>(type_.size())}; }
    NexusGameStatus Validate() const noexcept {
        uint64_t current = 0; const auto status = components_.Attachment(entity_, Type(), current);
        return status == NEXUS_GAME_OK && current != attachment_ ? NEXUS_GAME_INVALID_HANDLE : status;
    }
    NexusGameApiV1 game_{}; //< Engine常駐関数のみ。DLL関数やECS objectを保持しない
    Components components_; //< Loader所有clientを借用し、停止後はEngine側で無効化される
    NexusEntityHandle entity_{}; //< WorldとEntity世代を含む値
    std::string type_; //< DLL内で確保・解放する正規Type ID
    uint64_t attachment_ = 0; //< Componentの削除・再追加で変わる実行token
    bool closing_ = false; //< 終了中のWorld変更を防ぐ
};

/*-----------------------------------------------------------------------------------------
 * EntityBehaviourSystem<T>
 * - 値Componentに対応するDLL所有TのLifecycleを、既存Native SystemのUpdate内で実行する
 * - System所有・順序・thread制御は既存Scheduler。別ECSやSystem Managerは作らない
 * - TはContext&を受け取り、任意のOnCreate/Enable/Update/Disable/Destroyを実装できる
 * - callbackはvoidまたはNexusGameStatusを返す。失敗時はSystem停止を要求し、終了失敗ではDLLをpinする
 *---------------------------------------------------------------------------------------*/
template<class T> class EntityBehaviourSystem final {
    static_assert(std::is_nothrow_destructible_v<T>, "Behaviour destructor must not throw.");
    using Key = std::pair<uint32_t, uint32_t>;
    struct Instance {
        std::unique_ptr<EntityBehaviourContext> context; //< Tより長生きする借用先
        std::unique_ptr<T> value; //< 確保元DLL内でのみ破棄する。contextより先に破棄される
        bool enabled = false; //< OnEnable成功後だけtrue
        bool disabled = false, destroyed = false; //< 終了callbackを再試行しない。失敗時はDLL pinと再起動が必要
        NexusGameStatus cleanup = NEXUS_GAME_OK; //< 一度失敗した終了を後の成功で隠さない
    };
public:
    /** \brief 既存Scheduler向けDescriptor。Component metadataは別exportが宣言する */
    static NexusNativeSystemDescriptorV1 Descriptor(NexusGameString systemId) noexcept {
        static constexpr NexusGameString writes[]{{"GameComponents", 14}, {"Transform", 9}};
        NexusNativeSystemDescriptorV1 result{}; result.structSize = sizeof(result); result.version = 1; result.id = systemId;
        result.writes = writes; result.writeCount = 2;
        result.create = [](const NexusNativeSystemContextV1* context, void** output) noexcept -> NexusGameStatus {
            if(!output) return NEXUS_GAME_INVALID_ARGUMENT; *output = nullptr;
            if(!context || context->abiVersion != 1 || context->structSize != sizeof(*context) ||
                context->game.abiVersion != 1 || context->game.structSize != sizeof(context->game) ||
                !context->world || !context->game.getLocalTransform || !context->game.setLocalTransform) return NEXUS_GAME_INCOMPATIBLE_ABI;
            try { *output = new EntityBehaviourSystem(*context); return NEXUS_GAME_OK; } catch(...) { return NEXUS_GAME_FAILED; }
        };
        result.update = [](void* state, float deltaTime) noexcept -> NexusGameStatus {
            if(!state) return NEXUS_GAME_INVALID_ARGUMENT;
            auto& self = *static_cast<EntityBehaviourSystem*>(state);
            if(self.failed_) return NEXUS_GAME_FAILED;
            NexusGameStatus status = NEXUS_GAME_FAILED;
            try { status = self.Update(deltaTime); } catch(...) {}
            self.failed_ = status != NEXUS_GAME_OK; return status;
        };
        result.destroy = [](void* state) noexcept -> NexusGameStatus {
            if(!state) return NEXUS_GAME_OK;
            auto* self = static_cast<EntityBehaviourSystem*>(state);
            const auto status = self->Stop();
            // 失敗した所有物を無条件deleteしない。既存ScopeがstateとDLLを保持して終了失敗を通知する。
            if(status == NEXUS_GAME_OK) delete self;
            return status;
        };
        return result;
    }
    /** \brief services接続時の限定access callbackから呼ぶ。System pointerをcallback外へ保存しない */
    static NexusGameStatus Connect(void* state, const NexusGameServicesContextV1& services, NexusGameString type) noexcept {
        if(!state || !type.data || !type.length || type.length > 512) return NEXUS_GAME_INVALID_ARGUMENT;
        auto& self = *static_cast<EntityBehaviourSystem*>(state);
        if(services.abiVersion != 1 || services.structSize != sizeof(services) || services.world != self.world_ ||
            services.api.abiVersion != 1 || services.api.structSize != sizeof(services.api) || !services.api.invoke || !services.client) return NEXUS_GAME_INCOMPATIBLE_ABI;
        if(self.components_ || !self.instances_.empty()) return NEXUS_GAME_INVALID_ARGUMENT;
        try { self.type_.assign(type.data, type.length); self.components_ = std::make_unique<Components>(services); self.services_ = services; return NEXUS_GAME_OK; }
        catch(...) { return NEXUS_GAME_FAILED; }
    }
private:
    explicit EntityBehaviourSystem(const NexusNativeSystemContextV1& context) : game_(context.game), world_(context.world) {}
    ~EntityBehaviourSystem() = default;
    /** \brief 同じDLL内で例外を消費し、C ABIにはstatusだけを返す */
    template<class Function> static NexusGameStatus Invoke(Function&& function) noexcept {
        try { if constexpr(std::is_void_v<decltype(function())>) { function(); return NEXUS_GAME_OK; } else return function(); }
        catch(...) { return NEXUS_GAME_FAILED; }
    }
    static NexusGameStatus Create(T& value) noexcept { return Invoke([&]() -> decltype(auto) { if constexpr(requires { value.OnCreate(); }) return value.OnCreate(); }); }
    static NexusGameStatus Enable(T& value) noexcept { return Invoke([&]() -> decltype(auto) { if constexpr(requires { value.OnEnable(); }) return value.OnEnable(); }); }
    static NexusGameStatus Disable(T& value) noexcept { return Invoke([&]() -> decltype(auto) { if constexpr(requires { value.OnDisable(); }) return value.OnDisable(); }); }
    static NexusGameStatus Destroy(T& value) noexcept { return Invoke([&]() -> decltype(auto) { if constexpr(requires { value.OnDestroy(); }) return value.OnDestroy(); }); }
    static NexusGameStatus Tick(T& value, float dt) noexcept { return Invoke([&]() -> decltype(auto) { if constexpr(requires { value.OnUpdate(dt); }) return value.OnUpdate(dt); }); }
    /** \brief 全終了callbackを一度だけ試みる。解放済み資源への再試行を避け、失敗を保持する */
    static NexusGameStatus Retire(Instance& instance) noexcept {
        instance.context->BeginDestroy();
        const auto disabled = !instance.enabled || instance.disabled ? NEXUS_GAME_OK : Disable(*instance.value);
        instance.disabled = true;
        const auto destroyed = instance.destroyed ? NEXUS_GAME_OK : Destroy(*instance.value);
        instance.destroyed = true;
        if(disabled != NEXUS_GAME_OK) instance.cleanup = disabled;
        if(destroyed != NEXUS_GAME_OK) instance.cleanup = destroyed;
        return instance.cleanup;
    }
    NexusGameStatus Stop() noexcept {
        NexusGameStatus result = NEXUS_GAME_OK;
        for(auto it = instances_.begin(); it != instances_.end();) {
            const auto status = Retire(*it->second);
            if(status == NEXUS_GAME_OK) it = instances_.erase(it); else { result = status; ++it; }
        }
        return result;
    }
    /** \brief Query完了後だけcallbackを呼ぶ。callback内の構造変更でScene反復を壊さない */
    NexusGameStatus Update(float deltaTime) {
        if(!components_) return NEXUS_GAME_INVALID_HANDLE;
        const NexusGameString type{type_.data(), static_cast<uint32_t>(type_.size())};
        std::map<Key, NexusEntityHandle> current; uint32_t cursor = 0;
        do {
            NexusEntityHandle entities[128]{}; NexusComponentQueryV1 page{};
            auto status = components_->Query(type, cursor, entities, 128, page); if(status != NEXUS_GAME_OK) return status;
            for(uint32_t i = 0; i < page.count; ++i) current.emplace(Key{entities[i].index, entities[i].generation}, entities[i]);
            if(page.nextOffset != UINT32_MAX && page.nextOffset <= cursor) return NEXUS_GAME_FAILED;
            cursor = page.nextOffset;
        } while(cursor != UINT32_MAX);
        // Entity破棄とComponent除去は次の安全なUpdate境界でLifecycleへ反映する。
        for(auto it = instances_.begin(); it != instances_.end();) {
            if(current.contains(it->first)) { ++it; continue; }
            const auto status = Retire(*it->second); if(status != NEXUS_GAME_OK) return status;
            it = instances_.erase(it);
        }
        for(const auto& [key, entity] : current) {
            uint64_t token = 0; auto status = components_->Attachment(entity, type, token);
            if(status == NEXUS_GAME_INVALID_HANDLE) continue;
            if(status != NEXUS_GAME_OK) return status;
            auto found = instances_.find(key);
            if(found != instances_.end() && found->second->context->Attachment() != token) {
                status = Retire(*found->second); if(status != NEXUS_GAME_OK) return status;
                instances_.erase(found); found = instances_.end();
            }
            if(found == instances_.end()) {
                auto instance = std::make_unique<Instance>();
                instance->context = std::make_unique<EntityBehaviourContext>(game_, services_, entity, type_, token);
                instance->value = std::make_unique<T>(*instance->context);
                // OnCreate失敗後もStopでOnDestroyを呼べるよう、callbackより先に所有を確定する。
                found = instances_.emplace(key, std::move(instance)).first;
                status = Create(*found->second->value); if(status != NEXUS_GAME_OK) return status;
            }
            auto& instance = *found->second;
            NexusComponentValueV1 enabled{}; status = instance.context->Read({"enabled", 7}, enabled);
            if(status == NEXUS_GAME_INVALID_HANDLE) {
                uint64_t currentToken = 0; const auto currentStatus = components_->Attachment(entity, type, currentToken);
                if(currentStatus == NEXUS_GAME_INVALID_HANDLE || (currentStatus == NEXUS_GAME_OK && currentToken != token)) continue;
                // Entityが生存しているのに値を読めない場合は未来Version等の非互換。黙って停止しない。
                return currentStatus == NEXUS_GAME_OK ? NEXUS_GAME_INCOMPATIBLE_ABI : currentStatus;
            }
            if(status != NEXUS_GAME_OK || enabled.kind != NEXUS_COMPONENT_BOOL) return status == NEXUS_GAME_OK ? NEXUS_GAME_INCOMPATIBLE_ABI : status;
            if((enabled.integer != 0) != instance.enabled) {
                // OnEnableの途中失敗も終了時OnDisableの対象にする。
                if(enabled.integer) { instance.enabled = true; instance.disabled = false; }
                else instance.disabled = true; // 部分解放後に失敗しても、終了時にOnDisableを再試行しない。
                status = enabled.integer ? Enable(*instance.value) : Disable(*instance.value);
                if(status != NEXUS_GAME_OK) { if(!enabled.integer) instance.cleanup = status; return status; }
                instance.enabled = enabled.integer != 0;
            }
            // OnEnableが自身を除去する場合も、旧関連付けのOnUpdateを実行しない。
            uint64_t after = 0; status = components_->Attachment(entity, type, after);
            if(status == NEXUS_GAME_INVALID_HANDLE || (status == NEXUS_GAME_OK && after != token)) continue;
            if(status != NEXUS_GAME_OK) return status;
            if(instance.enabled) { status = Tick(*instance.value, deltaTime); if(status != NEXUS_GAME_OK) return status; }
        }
        return NEXUS_GAME_OK;
    }
    NexusGameApiV1 game_{}; //< Engine常駐関数表のコピー
    uint64_t world_ = 0; //< 起動時World。別Worldへの接続を拒否する
    NexusGameServicesContextV1 services_{}; //< Loader所有clientの借用。停止中は呼出しが拒否される
    std::unique_ptr<Components> components_; //< 接続後だけ存在する値API facade
    std::string type_; //< 一つのSystemが実行する正規Component ID
    std::map<Key, std::unique_ptr<Instance>> instances_; //< DLL内実行状態。Level保存はGameComponentDataが担当する
    bool failed_ = false; //< 更新失敗後に部分初期化instanceを再実行しない
};
}
