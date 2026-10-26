#include "ReflectionArchive.h"
#include <istream>
#include <ostream>

namespace NexusEngine {
namespace {
constexpr uint64_t kMaxElements = 16 * 1024 * 1024;
template<class T> void Write(std::ostream& stream, const T& value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    if(!stream) throw std::runtime_error("Cannot write reflection archive");
}
template<class T> T Read(std::istream& stream) {
    T value{}; stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    if(!stream) throw std::runtime_error("Truncated reflection archive");
    return value;
}
void WriteString(std::ostream& stream, const std::string& text) {
    Write(stream, static_cast<uint64_t>(text.size()));
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if(!stream) throw std::runtime_error("Cannot write reflection string");
}
std::string ReadString(std::istream& stream, uint64_t& budget) {
    const auto size = Read<uint64_t>(stream);
    if(size > budget) throw std::runtime_error("Reflection string limit exceeded");
    budget -= size;
    std::string text(static_cast<size_t>(size), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(size));
    if(!stream) throw std::runtime_error("Truncated reflection string");
    return text;
}
void WriteValue(std::ostream& stream, const ReflectionValue& value, unsigned depth) {
    if(depth > 64) throw std::runtime_error("Reflection nesting limit exceeded");
    Write(stream, static_cast<uint8_t>(value.data.index()));
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr(std::is_same_v<T, std::monostate>) {}
        else if constexpr(std::is_same_v<T, bool>) Write(stream, static_cast<uint8_t>(data));
        else if constexpr(std::is_arithmetic_v<T>) Write(stream, data);
        else if constexpr(std::is_same_v<T, std::string>) WriteString(stream, data);
        else {
            Write(stream, static_cast<uint64_t>(data.size()));
            for(const auto& entry : data) {
                if constexpr(std::is_same_v<T, ReflectionValue::Object>) {
                    WriteString(stream, entry.first); WriteValue(stream, entry.second, depth + 1);
                } else WriteValue(stream, entry, depth + 1);
            }
        }
    }, value.data);
}
ReflectionValue ReadValue(std::istream& stream, unsigned depth, uint64_t& budget) {
    if(depth > 64 || budget == 0) throw std::runtime_error("Reflection archive limit exceeded");
    --budget;
    const auto kind = Read<uint8_t>(stream);
    switch(kind) {
    case 0: return {};
    case 1: { auto value = Read<uint8_t>(stream); if(value > 1) throw std::runtime_error("Invalid reflection boolean"); return ReflectionValue(value != 0); }
    case 2: return ReflectionValue(Read<int64_t>(stream));
    case 3: return ReflectionValue(Read<uint64_t>(stream));
    case 4: return ReflectionValue(Read<double>(stream));
    case 5: return ReflectionValue(ReadString(stream, budget));
    case 6: {
        const auto count = Read<uint64_t>(stream);
        if(count > budget) throw std::runtime_error("Reflection array limit exceeded");
        ReflectionValue::Array result;
        for(uint64_t i = 0; i < count; ++i) result.push_back(ReadValue(stream, depth + 1, budget));
        return ReflectionValue(std::move(result));
    }
    case 7: {
        const auto count = Read<uint64_t>(stream);
        if(count > budget) throw std::runtime_error("Reflection object limit exceeded");
        ReflectionValue::Object result;
        for(uint64_t i = 0; i < count; ++i) {
            auto key = ReadString(stream, budget); auto value = ReadValue(stream, depth + 1, budget);
            if(!result.emplace(std::move(key), std::move(value)).second) throw std::runtime_error("Duplicate reflection key");
        }
        return ReflectionValue(std::move(result));
    }
    default: throw std::runtime_error("Invalid reflection value kind");
    }
}
}
void WriteReflectionArchive(std::ostream& output, const ReflectionValue& value) {
    Write(output, uint32_t{0x3152584e}); WriteValue(output, value, 0);
}
ReflectionValue ReadReflectionArchive(std::istream& input) {
    if(Read<uint32_t>(input) != 0x3152584e) throw std::runtime_error("Unsupported reflection archive version");
    uint64_t budget = kMaxElements;
    return ReadValue(input, 0, budget);
}
}
