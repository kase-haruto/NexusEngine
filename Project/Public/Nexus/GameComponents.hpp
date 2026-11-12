#pragma once
#include "GameComponents.h"
#include <array>
#include <cstring>

namespace NexusGame {
/*-----------------------------------------------------------------------------------------
 * Components
 * - Game DLL向けの値API Facade。既存services clientを借用し、開閉やWorldの所有は行わない
 * - 内部の配列は呼出し中だけ有効であり、STLやComponent pointerをC ABIへ渡さない
 *---------------------------------------------------------------------------------------*/
class Components final {
public:
    /** \brief NexusConnectGameServicesが受け取った関数表とclientを借用する */
    explicit Components(const NexusGameServicesContextV1& context) noexcept : api_(context.api),
        client_(context.abiVersion == 1 && context.structSize == sizeof(context) ? context.client : 0) {}
    /** \brief 現在のEntityが指定Componentを持つかを返す */
    NexusGameStatus Has(NexusEntityHandle entity, NexusGameString type, bool& present) const noexcept {
        present = false; std::array<uint8_t, 4096> output{}; uint32_t length = 0;
        const auto status = Call({NEXUS_COMPONENT_HAS}, entity, type, {}, {}, output, length);
        if(status != NEXUS_GAME_OK) return status;
        uint32_t value = 0;
        if(length != sizeof(value)) return NEXUS_GAME_FAILED;
        std::memcpy(&value, output.data(), sizeof(value)); if(value > 1) return NEXUS_GAME_FAILED;
        present = value != 0; return NEXUS_GAME_OK;
    }
    /** \brief 既存Reflectionの既定値で追加する。すでに存在する場合も成功する */
    NexusGameStatus Add(NexusEntityHandle entity, NexusGameString type) const noexcept { return Change(NEXUS_COMPONENT_ADD, entity, type); }
    /** \brief 必須型以外を削除する。Scriptは既存Script APIの責務を維持する */
    NexusGameStatus Remove(NexusEntityHandle entity, NexusGameString type) const noexcept { return Change(NEXUS_COMPONENT_REMOVE, entity, type); }
    /** \brief Propertyのコピーを取得する。Stringは呼出側bufferへコピーし、Native pointerを返さない */
    NexusGameStatus Read(NexusEntityHandle entity, NexusGameString type, NexusGameString property,
        NexusComponentValueV1& value, char* text = nullptr, uint32_t capacity = 0, uint64_t attachment = 0) const noexcept {
        value = {}; if(capacity && !text) return NEXUS_GAME_INVALID_ARGUMENT;
        std::array<uint8_t, 4096> output{}; uint32_t length = 0;
        NexusComponentRequestV1 request{}; request.operation = attachment ? NEXUS_COMPONENT_READ_ATTACHED : NEXUS_COMPONENT_READ;
        request.offset = static_cast<uint32_t>(attachment); request.limit = static_cast<uint32_t>(attachment >> 32);
        const auto status = Call(request, entity, type, property, {}, output, length);
        if(status != NEXUS_GAME_OK) return status;
        if(length < sizeof(value)) return NEXUS_GAME_FAILED;
        std::memcpy(&value, output.data(), sizeof(value));
        if(value.reserved || value.kind < 1 || value.kind > 8 || value.stringLength > 3072 || sizeof(value) + value.stringLength != length) { value = {}; return NEXUS_GAME_FAILED; }
        if(capacity < value.stringLength) return NEXUS_GAME_BUFFER_TOO_SMALL;
        if(value.stringLength) std::memcpy(text, output.data() + sizeof(value), value.stringLength);
        return NEXUS_GAME_OK;
    }
    /** \brief Detailsと同じ検証付きsetterで更新する。文字列はUTF-8の値コピー */
    NexusGameStatus Write(NexusEntityHandle entity, NexusGameString type, NexusGameString property,
        NexusComponentValueV1 value, NexusGameString text = {}, uint64_t attachment = 0) const noexcept {
        value.stringLength = text.length;
        NexusComponentRequestV1 request{}; request.operation = attachment ? NEXUS_COMPONENT_WRITE_ATTACHED : NEXUS_COMPONENT_WRITE; request.value = value;
        request.offset = static_cast<uint32_t>(attachment); request.limit = static_cast<uint32_t>(attachment >> 32);
        std::array<uint8_t, 4096> output{}; uint32_t length = 0;
        const auto status = Call(request, entity, type, property, text, output, length);
        return status == NEXUS_GAME_OK && length ? NEXUS_GAME_FAILED : status;
    }
    /** \brief 値Componentの関連付け世代を取得する。削除・再追加後の旧Behaviourを識別する */
    NexusGameStatus Attachment(NexusEntityHandle entity, NexusGameString type, uint64_t& token) const noexcept {
        token = 0; std::array<uint8_t, 4096> output{}; uint32_t length = 0;
        const auto status = Call({NEXUS_COMPONENT_ATTACHMENT}, entity, type, {}, {}, output, length);
        if(status != NEXUS_GAME_OK) return status;
        if(length != sizeof(token)) return NEXUS_GAME_FAILED;
        std::memcpy(&token, output.data(), sizeof(token)); return token ? NEXUS_GAME_OK : NEXUS_GAME_FAILED;
    }
    /** \brief 一つのComponentを条件にHandleをページ取得する。構造変更前に全ページを取得する */
    NexusGameStatus Query(NexusGameString type, uint32_t offset, NexusEntityHandle* entities, uint32_t capacity,
        NexusComponentQueryV1& page) const noexcept {
        page = {}; if(!entities || !capacity || capacity > 255) return NEXUS_GAME_INVALID_ARGUMENT;
        NexusComponentRequestV1 request{}; request.operation = NEXUS_COMPONENT_QUERY; request.offset = offset; request.limit = capacity;
        std::array<uint8_t, 4096> output{}; uint32_t length = 0;
        const auto status = Call(request, {}, type, {}, {}, output, length); if(status != NEXUS_GAME_OK) return status;
        if(length < sizeof(page)) return NEXUS_GAME_FAILED;
        std::memcpy(&page, output.data(), sizeof(page));
        if(page.count > capacity || sizeof(page) + page.count * sizeof(NexusEntityHandle) != length) { page = {}; return NEXUS_GAME_FAILED; }
        if(page.count) std::memcpy(entities, output.data() + sizeof(page), page.count * sizeof(NexusEntityHandle));
        return NEXUS_GAME_OK;
    }
private:
    /** \brief 固定protocolへ整列を要求しないbyte列をコピーし、一回だけ同期呼出しする */
    NexusGameStatus Call(NexusComponentRequestV1 header, NexusEntityHandle entity, NexusGameString type, NexusGameString property,
        NexusGameString text, std::array<uint8_t, 4096>& output, uint32_t& length) const noexcept {
        length = 0;
        if(api_.abiVersion != 1 || api_.structSize != sizeof(api_) || !api_.invoke || !client_ ||
            !type.length || type.length > 512 || !type.data || property.length > 512 || (property.length && !property.data) || text.length > 3072 || (text.length && !text.data)) return NEXUS_GAME_INVALID_ARGUMENT;
        const uint32_t size = static_cast<uint32_t>(sizeof(header)) + type.length + property.length + text.length;
        if(size > 4096) return NEXUS_GAME_INVALID_ARGUMENT;
        header.typeLength = type.length; header.propertyLength = property.length;
        std::array<uint8_t, 4096> request{};
        std::memcpy(request.data(), &header, sizeof(header)); std::memcpy(request.data() + sizeof(header), type.data, type.length);
        if(property.length) std::memcpy(request.data() + sizeof(header) + type.length, property.data, property.length);
        if(text.length) std::memcpy(request.data() + sizeof(header) + type.length + property.length, text.data, text.length);
        return api_.invoke(client_, {NEXUS_COMPONENT_COMMAND, sizeof(NEXUS_COMPONENT_COMMAND) - 1}, 1, entity,
            {request.data(), size}, output.data(), static_cast<uint32_t>(output.size()), &length);
    }
    /** \brief 応答を持たない構造変更を一度だけ実行し、未知の応答形式は成功扱いにしない */
    NexusGameStatus Change(uint32_t operation, NexusEntityHandle entity, NexusGameString type) const noexcept {
        NexusComponentRequestV1 request{}; request.operation = operation;
        std::array<uint8_t, 4096> output{}; uint32_t length = 0;
        const auto status = Call(request, entity, type, {}, {}, output, length);
        return status == NEXUS_GAME_OK && length ? NEXUS_GAME_FAILED : status;
    }
    NexusGameServicesApiV1 api_{}; //< Engine常駐の固定関数表。Game DLL内部関数は保持しない
    uint64_t client_ = 0; //< Loaderが所有する借用client。Module停止後はEngineが拒否する
};
}
