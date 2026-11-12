#pragma once
#include "GameComponents.h"
#include <array>
#include <cstring>

namespace NexusGame {
/*-----------------------------------------------------------------------------------------
 * Transforms
 * - Native Systemが最大64 Entityを値で一括取得・更新するためのFacade
 * - 所有Thread上の同期呼出しだけを行い、ComponentやSceneの内部アドレスを返さない
 *---------------------------------------------------------------------------------------*/
class Transforms final {
public:
    /** \brief Module接続時のservices clientを借用する。clientの終了はLoaderが管理する */
    explicit Transforms(const NexusGameServicesContextV1& context) noexcept : api_(context.api),
        client_(context.abiVersion == 1 && context.structSize == sizeof(context) ? context.client : 0) {}
    /** \brief 全Entityの妥当性を検査後、Local Transformを同じ順序でコピーする */
    NexusGameStatus Read(const NexusEntityHandle* entities, uint32_t count, NexusLocalTransform* output) const noexcept {
        if(count > 64 || (count && (!entities || !output))) return NEXUS_GAME_INVALID_ARGUMENT;
        std::array<uint8_t, 4096> input{}, response{};
        const NexusTransformBatchV1 header{1, count}; std::memcpy(input.data(), &header, sizeof(header));
        if(count) std::memcpy(input.data() + sizeof(header), entities, count * sizeof(NexusEntityHandle));
        uint32_t length = 0;
        const auto status = Invoke(input, static_cast<uint32_t>(sizeof(header) + count * sizeof(NexusEntityHandle)), response, length);
        if(status != NEXUS_GAME_OK) return status;
        if(length != count * sizeof(NexusLocalTransform)) return NEXUS_GAME_FAILED;
        if(length) std::memcpy(output, response.data(), length);
        return NEXUS_GAME_OK;
    }
    /** \brief 全要素を検証してから適用する。不正要素があればWorldは変更しない */
    NexusGameStatus Write(const NexusTransformUpdateV1* updates, uint32_t count) const noexcept {
        if(count > 64 || (count && !updates)) return NEXUS_GAME_INVALID_ARGUMENT;
        std::array<uint8_t, 4096> input{}, response{};
        const NexusTransformBatchV1 header{2, count}; std::memcpy(input.data(), &header, sizeof(header));
        if(count) std::memcpy(input.data() + sizeof(header), updates, count * sizeof(NexusTransformUpdateV1));
        uint32_t length = 0;
        const auto status = Invoke(input, static_cast<uint32_t>(sizeof(header) + count * sizeof(NexusTransformUpdateV1)), response, length);
        return status == NEXUS_GAME_OK && length ? NEXUS_GAME_FAILED : status;
    }
private:
    /** \brief 既存servicesの予約Engine IDへ値だけを送り、callbackを保持しない */
    NexusGameStatus Invoke(const std::array<uint8_t, 4096>& input, uint32_t count, std::array<uint8_t, 4096>& response, uint32_t& length) const noexcept {
        if(api_.abiVersion != 1 || api_.structSize != sizeof(api_) || !api_.invoke || !client_) return NEXUS_GAME_INVALID_ARGUMENT;
        return api_.invoke(client_, {NEXUS_TRANSFORM_BATCH_COMMAND, sizeof(NEXUS_TRANSFORM_BATCH_COMMAND) - 1}, 1, {},
            {input.data(), count}, response.data(), static_cast<uint32_t>(response.size()), &length);
    }
    NexusGameServicesApiV1 api_{}; //< Engine常駐表のコピー。Native Systemのコードpointerは含まない
    uint64_t client_ = 0; //< Module寿命の借用capability。古いWorldではEngineが拒否する
};
}
