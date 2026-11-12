#pragma once
#include "Foundation/Reflection/ReflectionValue.h"
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * GameComponentData
 * - DLLが宣言した値Componentを既存ECS内へ保存する。DLL内C++ objectやcallbackは保持しない
 * - 保存値と実行用attachmentを分け、Level復元時には実行用tokenを生成し直す
 * - Componentの別ECSではなく、既存ComponentStorageが所有する一つの値Componentである
 *---------------------------------------------------------------------------------------*/
struct GameComponentData {
    std::string propertyState; //< 共通Property state形式。未知の型・Propertyも保持する
    // MSVCのmapはmove構築でもsentinelを確保するため、既存ECSのnoexcept move制約を満たせない。
    // 最大256件の値配列にして、Storage再確保はbufferの所有権移動だけで完了させる。
    std::vector<std::pair<std::string, uint64_t>> attachments; //< 削除・再追加を区別する実行token。保存しない
    /** \brief 借用したIDからtoken値だけを取得する。見つからない場合は0。内部pointerは返さない */
    uint64_t FindAttachment(std::string_view id) const noexcept {
        for(const auto& item : attachments) if(item.first == id) return item.second;
        return 0;
    }
    /** \brief 一意IDのtokenを更新・追加する。新規確保の例外は既存呼出元の失敗処理へ返す */
    bool SetAttachment(std::string id, uint64_t token) {
        if(id.empty() || !token) return false;
        for(auto& item : attachments) if(item.first == id) { item.second = token; return true; }
        if(attachments.size() >= 256) return false;
        attachments.emplace_back(std::move(id), token); return true;
    }
    /** \brief 指定IDを解除する。旧tokenを残して再追加後の旧instanceを有効化しない */
    void RemoveAttachment(std::string_view id) {
        std::erase_if(attachments, [id](const auto& item) { return item.first == id; });
    }
    /** \brief 既存Level serializationに渡す前に状態サイズ・構造とtokenを検証する */
    bool IsValid() const noexcept;
};
// ECSの制約を弱めず、格納値側にも契約を明記して原因型をコンパイル診断へ出す。
static_assert(std::is_nothrow_move_constructible_v<GameComponentData>, "GameComponentData must support noexcept move construction for ECS storage.");
static_assert(std::is_nothrow_move_assignable_v<GameComponentData>, "GameComponentData must support noexcept move assignment for ECS storage.");
/** \brief Process内で再利用しないtokenを発行する。0は枯渇・無効を表す */
uint64_t NextGameComponentAttachment() noexcept;
// 値だけを既存Reflectionへ登録する。実行tokenやDLLのメモリ配置をLevelへ保存しない。
template<> struct ReflectionTraits<GameComponentData> {
    static constexpr const char* Id = "GameComponents";
    static constexpr const char* Label = "";
    template<class Visitor> static void Visit(Visitor&& visitor) { visitor("propertyState", &GameComponentData::propertyState); }
};
}
