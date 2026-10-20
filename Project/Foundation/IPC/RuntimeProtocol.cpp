#include "RuntimeProtocol.h"

namespace NexusEngine::IPC {
namespace {
constexpr uint32_t Magic = 0x5358454e; // NEXS
constexpr size_t HeaderSize = 44;
bool ValidType(MessageType type) { return type >= MessageType::Hello && type <= MessageType::Heartbeat; }
}
void WireWriter::U16(uint16_t value) { for(int i = 0; i < 2; ++i) data.push_back(static_cast<std::byte>(value >> (8 * i))); }
void WireWriter::U32(uint32_t value) { for(int i = 0; i < 4; ++i) data.push_back(static_cast<std::byte>(value >> (8 * i))); }
void WireWriter::U64(uint64_t value) { for(int i = 0; i < 8; ++i) data.push_back(static_cast<std::byte>(value >> (8 * i))); }
bool WireWriter::String(const std::string& value) {
    if(value.size() > MaximumPayload || data.size() + 4 + value.size() > MaximumPayload) return false;
    U32(static_cast<uint32_t>(value.size()));
    for(unsigned char c : value) data.push_back(static_cast<std::byte>(c)); return true;
}
bool WireReader::U16(uint16_t& value) {
    if(bytes_.size() - cursor_ < 2) return false; value = 0;
    for(int i = 0; i < 2; ++i) value = static_cast<uint16_t>(value | (uint16_t(std::to_integer<uint8_t>(bytes_[cursor_++])) << (i * 8)));
    return true;
}
bool WireReader::U32(uint32_t& value) {
    if(bytes_.size() - cursor_ < 4) return false; value = 0;
    for(int i = 0; i < 4; ++i) value |= uint32_t(std::to_integer<uint8_t>(bytes_[cursor_++])) << (i * 8); return true;
}
bool WireReader::U64(uint64_t& value) {
    if(bytes_.size() - cursor_ < 8) return false; value = 0;
    for(int i = 0; i < 8; ++i) value |= uint64_t(std::to_integer<uint8_t>(bytes_[cursor_++])) << (i * 8); return true;
}
bool WireReader::String(std::string& value, size_t limit) {
    uint32_t size = 0;
    if(!U32(size) || size > limit || size > bytes_.size() - cursor_) return false;
    value.clear(); value.reserve(size);
    for(uint32_t i = 0; i < size; ++i) value.push_back(static_cast<char>(std::to_integer<uint8_t>(bytes_[cursor_++])));
    return value.find('\0') == value.npos;
}
std::optional<std::vector<std::byte>> Encode(const Message& m) {
    if(m.payload.size() > MaximumPayload || !ValidType(m.type)) return {};
    WireWriter w;
    w.U32(Magic); w.U16(ProtocolVersion); w.U16(static_cast<uint16_t>(m.type));
    w.U32(static_cast<uint32_t>(m.payload.size())); w.U64(m.session); w.U64(m.request); w.U64(m.revision);
    w.U64(0); // reserved。v1では0のみ受理。
    w.data.insert(w.data.end(), m.payload.begin(), m.payload.end()); return std::move(w.data);
}
std::optional<Message> Decode(std::span<const std::byte> packet) {
    if(packet.size() < HeaderSize || packet.size() > HeaderSize + MaximumPayload) return {};
    WireReader r(packet.first(HeaderSize));
    uint32_t magic = 0, size = 0; uint16_t version = 0, type = 0; uint64_t reserved = 0; Message m;
    if(!r.U32(magic) || !r.U16(version) || !r.U16(type) || !r.U32(size) || !r.U64(m.session) ||
        !r.U64(m.request) || !r.U64(m.revision) || !r.U64(reserved)) return {};
    m.type = static_cast<MessageType>(type);
    if(magic != Magic || version != ProtocolVersion || reserved != 0 || !ValidType(m.type) ||
        size > MaximumPayload || packet.size() != HeaderSize + size) return {};
    m.payload.assign(packet.begin() + HeaderSize, packet.end()); return m;
}
std::vector<std::byte> EncodeResize(const ResizeMessage& m) {
    WireWriter w; w.U64(m.viewport); w.U64(m.generation); w.U32(m.width); w.U32(m.height); return std::move(w.data);
}
std::optional<ResizeMessage> DecodeResize(std::span<const std::byte> p) {
    WireReader r(p); ResizeMessage m;
    if(!r.U64(m.viewport) || !r.U64(m.generation) || !r.U32(m.width) || !r.U32(m.height) || !r.Finished() ||
        !m.viewport || m.width > 16384 || m.height > 16384 || ((m.width == 0) != (m.height == 0))) return {};
    return m;
}
std::vector<std::byte> EncodeFrame(const FrameMessage& m) {
    WireWriter w; w.U64(m.viewport); w.U64(m.generation); w.U64(m.sequence); w.U32(m.slot);
    w.U64(m.readyValue); w.U64(m.releasedValue); return std::move(w.data);
}
std::optional<FrameMessage> DecodeFrame(std::span<const std::byte> p) {
    WireReader r(p); FrameMessage m;
    if(!r.U64(m.viewport) || !r.U64(m.generation) || !r.U64(m.sequence) || !r.U32(m.slot) ||
        !r.U64(m.readyValue) || !r.U64(m.releasedValue) || !r.Finished() || !m.viewport || !m.generation ||
        !m.sequence || m.slot >= 3 || !m.readyValue || !m.releasedValue) return {};
    return m;
}
}
