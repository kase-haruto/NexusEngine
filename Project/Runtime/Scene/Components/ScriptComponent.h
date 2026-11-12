#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include "Foundation/Reflection/ReflectionValue.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "Runtime/Scene/PersistentIdGenerator.h"

NEXUS_REFLECT_COMPONENT(ScriptComponent, "Script", "component.script", scriptType, enabled, propertyState, additional, slotId)

namespace NexusEngine {
/** \brief 既存の永続ID発行元を再利用し、保存/Undoでruntime tokenと異なる関連付けIDを保つ */
inline PersistentId NewScriptSlotId() { static PersistentIdGenerator generator; return generator.Generate(); }
/*-----------------------------------------------------------------------------------------
 * ScriptInstanceHandle
 * - Managed session内のScriptBehaviourを識別する数値ID。ポインタやGCHandleではない
 * - World / sessionを越えて転用せず、保存・Clone後にはScriptSystemが再構築する
 *---------------------------------------------------------------------------------------*/
struct ScriptInstanceHandle { uint64_t value = 0; //< 0は未生成。Managed session内で再利用しない
};
/*-----------------------------------------------------------------------------------------
 * ScriptAttachment
 * - 一つのManaged behaviourの保存値と実行token。Managed objectやECS Storageは所有しない
 * - 主Scriptと追加Scriptで同じ値契約を使い、実行tokenは保存しない
 *---------------------------------------------------------------------------------------*/
struct ScriptAttachment {
    std::string scriptType; //< 安定Script Type ID。FullName / Aliasも互換名として受け入れる
    bool enabled = true; //< Pauseとは独立したbehaviour単位の有効状態
    std::string propertyState; //< 型ID・Property ID・kind別のJSON値。未知値を残してrename / 型変更に備える
    ScriptInstanceHandle instance; //< ScriptSystemだけが確定する実行用Handle。Reflection対象外
    uint64_t attachment = 0; //< 削除・再追加を区別するWorld内token。Reflection対象外
    PersistentId slotId=NewScriptSlotId(); //< 保存するinstance識別子。旧Levelのfield省略時は既定初期化で新規発行する
    /** \brief 編集途中の空型名は許容し、関連付けごとの保存上限を検証する */
    bool IsValid() const noexcept {
        if(!slotId.IsValid() || scriptType.size() > 512 || propertyState.size() > 262144 || propertyState.find('\0') != propertyState.npos) return false;
        for(unsigned char ch : scriptType) if(ch < 32) return false;
        return true;
    }
};
/*-----------------------------------------------------------------------------------------
 * ScriptComponent
 * - 既存ECSが所有する主Scriptと追加Script。主Scriptの旧保存fieldを維持する
 * - 同一Typeの複数instanceもslotIdで区別し、共通Property Handleから一つだけ解決する
 *---------------------------------------------------------------------------------------*/
struct ScriptComponent : ScriptAttachment {
    std::vector<ScriptAttachment> additional; //< 最大31件。順序も保存する。実行objectはManaged session所有
    /** \brief owner threadで主Script、追加Scriptの順に訪問する。訪問中の配列構造変更は禁止 */
    template<class Function> void ForEachScript(Function&& function) { function(static_cast<ScriptAttachment&>(*this)); for(auto& slot : additional) function(slot); }
    template<class Function> void ForEachScript(Function&& function) const { function(static_cast<const ScriptAttachment&>(*this)); for(const auto& slot : additional) function(slot); }
    /** \brief 正規IDまたは指定された互換名に対応する現在の関連付けを解決する */
    ScriptAttachment* FindScript(std::string_view type) noexcept {
        if(scriptType == type) return this;
        for(auto& slot : additional) if(slot.scriptType == type) return &slot;
        return nullptr;
    }
    /** \brief 永続instance IDだけで解決する。無効IDを先頭Scriptへ読み替えない */
    ScriptAttachment* FindSlot(PersistentId id) noexcept {
        if(!id.IsValid()) return nullptr;
        if(slotId==id) return this;
        for(auto& slot:additional) if(slot.slotId==id) return &slot;
        return nullptr;
    }
    /** \brief runtime tokenだけを解決する。0や解除済みのtokenは再利用しない */
    ScriptAttachment* FindAttachment(uint64_t token) noexcept {
        if(!token) return nullptr;
        if(attachment == token) return this;
        for(auto& slot : additional) if(slot.attachment == token) return &slot;
        return nullptr;
    }
    /** \brief 一つだけ解除する。主Scriptを外す時は先頭の追加Scriptを昇格し、その寿命を維持する */
    bool RemoveAttachment(ScriptAttachment* slot) {
        if(slot == static_cast<ScriptAttachment*>(this)) {
            if(additional.empty()) return false; // 最後の一つは呼出側が既存ECS Componentごと削除する。
            static_cast<ScriptAttachment&>(*this) = std::move(additional.front()); additional.erase(additional.begin()); return true;
        }
        for(auto it = additional.begin(); it != additional.end(); ++it) if(&*it == slot) { additional.erase(it); return true; }
        return false;
    }
    /** \brief 保存上限とinstance IDの一意性を検査する。Type IDの重複は独立instanceとして許可する */
    bool IsValid() const noexcept {
        if(!ScriptAttachment::IsValid() || additional.size() > 31 || (scriptType.empty() && !additional.empty())) return false;
        size_t size = propertyState.size();
        for(size_t i = 0; i < additional.size(); ++i) {
            const auto& slot = additional[i]; if(!slot.IsValid() || slot.scriptType.empty() || slot.slotId==slotId) return false;
            size += slot.propertyState.size(); if(size > 1024 * 1024) return false;
            for(size_t j = 0; j < i; ++j) if(additional[j].slotId==slot.slotId) return false;
        }
        return true;
    }
    /** \brief 追加リストや保存payloadを直接編集させず、型付き共通Descriptorを使う */
    static bool IsPropertyEditable(std::string_view id) noexcept { return id != "propertyState" && id != "additional" && id != "slotId"; }
};
// 追加Scriptは値として既存Reflectionへ登録する。Component自体を増やす別ECSは作らない。
template<> struct ReflectionTraits<ScriptAttachment> {
    static constexpr const char* Id = "ScriptAttachment";
    template<class Visitor> static void Visit(Visitor&& visitor) {
        visitor("scriptType", &ScriptAttachment::scriptType); visitor("enabled", &ScriptAttachment::enabled); visitor("propertyState", &ScriptAttachment::propertyState);
        visitor("slotId", &ScriptAttachment::slotId);
    }
};
}
