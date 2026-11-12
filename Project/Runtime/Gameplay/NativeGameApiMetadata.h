#pragma once
#include "Public/Nexus/NativeGameSystemApi.h"
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * NativeGameApiMetadata
 * - Moduleが明示宣言したSystem関数の値型・互換性・thread契約をコピーして保持する
 * - Systemの生成・所有・実行、関数のBinding / 呼出しは行わない
 * - Property編集用TypeRegistryにSystemをComponentとして偽装しない
 *---------------------------------------------------------------------------------------*/
class NativeGameApiMetadata final {
public:
    struct ValueType { uint32_t kind; std::string typeId; }; //< Public APIの限定値種別。Propertyと重なる型の意味を揃え、型pointerは保持しない
    struct Parameter { std::string id; ValueType type; }; //< 宣言順を維持した引数の値Metadata
    struct Function {
        uint64_t generation, system; //< Module scopeと組で検証する再利用しない登録ID
        std::string systemId, id; //< C++型名に依存しない識別子
        uint32_t version; //< 互換性を明示する版番号
        ValueType result; std::vector<Parameter> parameters; //< DLL bufferからコピーした契約だけを所有する
    };
    /** \brief 文字列IDを検証・コピーする \param value 呼出し中だけ借用するASCII ID \return 不正時は例外。ABI adapterがstatusへ変換する */
    static std::string Identifier(NexusGameString value);
    /** \brief 所有者が検証済みのSystemへ公開契約だけを宣言する \return 重複・型・上限不正なら失敗 */
    NexusGameStatus Declare(uint64_t generation, uint64_t system, const std::string& systemId, const NexusNativeFunctionDescriptorV1& descriptor);
    /** \brief System / 関数 / Versionが一致する現在の契約を検索する。C ABIへ内部pointerを返さない */
    [[nodiscard]] const Function* Find(uint64_t system, std::string_view id, uint32_t version) const noexcept;
    /** \brief Module失効時に公開Metadataだけを解除する。System本体はSchedulerが破棄する */
    void Clear() noexcept { functions_.clear(); }
    [[nodiscard]] std::span<const Function> Functions() const noexcept { return functions_; }
private:
    std::vector<Function> functions_; //< Module scope寿命の値Metadata。Native / Managed callbackを所有しない
};
}
