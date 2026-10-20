#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace NexusEngine::IPC {
inline constexpr uint16_t ProtocolVersion = 1;
inline constexpr uint32_t MaximumPayload = 1024 * 1024;
enum class MessageType : uint16_t {
    Hello = 1, Ready, Shutdown, Goodbye, Resize, Input, Selection, PropertyUpdate,
    Play, Pause, Stop, LoadLevel, Error, Log, Snapshot, Reply,
    FramePool, FrameReady, FrameReleased, RetirePool, PoolRetired, Heartbeat
};
enum class RuntimeState : uint8_t { Disconnected, Starting, Connected, Running, Paused, Stopping, Error };
struct Message {
    MessageType type = MessageType::Hello; //< 安定wire値
    uint64_t session = 0; //< 再接続で変更するnonce
    uint64_t request = 0; //< 応答照合、0は通知
    uint64_t revision = 0; //< Level更新番号
    std::vector<std::byte> payload; //< 型別に明示的にencodeする
};
/*-----------------------------------------------------------------------------------------
 * WireWriter / WireReader
 * - little-endianの固定幅整数と長さ付きUTF-8を符号化する
 * - C++構造体、pointer、Graphics API型をwireへコピーしない
 *---------------------------------------------------------------------------------------*/
class WireWriter final {
public:
    void U16(uint16_t value);
    void U32(uint32_t value);
    void U64(uint64_t value);
    bool String(const std::string& value);
    std::vector<std::byte> data; //< 完成後にmessageへmoveする
};
class WireReader final {
public:
    explicit WireReader(std::span<const std::byte> bytes) : bytes_(bytes) {}
    bool U16(uint16_t& value);
    bool U32(uint32_t& value);
    bool U64(uint64_t& value);
    bool String(std::string& value, size_t limit = 65536);
    bool Finished() const { return cursor_ == bytes_.size(); }
private:
    std::span<const std::byte> bytes_; //< 受信bufferの範囲
    size_t cursor_ = 0; //< 検証済み読取位置
};
/** \brief 44 byte header + payloadを生成する。上限超過は拒否する */
std::optional<std::vector<std::byte>> Encode(const Message& message);
/** \brief 一件分の完全なpacketを検証する。部分受信の組立はChannelの責務 */
std::optional<Message> Decode(std::span<const std::byte> packet);
/*-----------------------------------------------------------------------------------------
 * IControlChannel / IRuntimeConnection
 * - IPC実装と上位の接続状態を分離する
 * - Send/TryReceiveはUIを待機させず、queue上限超過をエラーとして返す
 *---------------------------------------------------------------------------------------*/
class IControlChannel {
public:
    virtual ~IControlChannel() = default;
    virtual bool Send(const Message& message) = 0;
    virtual std::optional<Message> TryReceive() = 0;
    virtual bool IsOpen() const noexcept = 0;
    virtual void Close() noexcept = 0;
};
class IRuntimeConnection {
public:
    virtual ~IRuntimeConnection() = default;
    virtual RuntimeState State() const noexcept = 0;
    /** \brief 現接続のsession/requestを付与して送信する。UIをblockしない */
    virtual bool Submit(Message message) = 0;
    virtual void Poll() = 0;
    virtual void RequestStop() = 0;
    virtual std::string LastError() const = 0;
};
struct ResizeMessage { uint64_t viewport = 1, generation = 0; uint32_t width = 0, height = 0; };
struct FrameMessage {
    uint64_t viewport = 1, generation = 0, sequence = 0; //< pool世代と単調増加frame番号
    uint32_t slot = 0; //< ring index
    uint64_t readyValue = 0, releasedValue = 0; //< producer/consumerの別Fence値
};
/** \brief 制御payloadの固定幅encode。サイズ0は非表示として扱う */
std::vector<std::byte> EncodeResize(const ResizeMessage& message);
std::optional<ResizeMessage> DecodeResize(std::span<const std::byte> payload);
std::vector<std::byte> EncodeFrame(const FrameMessage& message);
std::optional<FrameMessage> DecodeFrame(std::span<const std::byte> payload);
}
