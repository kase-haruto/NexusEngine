#include "ReflectionTextArchive.h"
#include <iomanip>
#include <cctype>
#include <cstdio>
#include <istream>
#include <limits>
#include <ostream>

namespace NexusEngine {
namespace {
constexpr uint64_t kMaxBudget = 16 * 1024 * 1024;
constexpr unsigned kMaxDepth = 64;

// quotedの無制限な読み取りを避け、文字列も同じ読込予算へ含める。
std::string ReadString(std::istream& input, uint64_t& budget) {
    input >> std::ws;
    if(input.get() != '"') throw std::runtime_error("Expected quoted reflection string");
    std::string result;
    char character;
    while(input.get(character)) {
        if(character == '"') return result;
        if(character == '\\' && !input.get(character)) break;
        if(budget == 0) throw std::runtime_error("Reflection text string limit exceeded");
        --budget; result += character;
    }
    throw std::runtime_error("Truncated reflection string");
}
template<class T> T ReadNumber(std::istream& input) {
    T value{};
    if(!(input >> value)) throw std::runtime_error("Invalid reflection number");
    return value;
}
uint64_t ReadUnsigned(std::istream& input) {
    input >> std::ws;
    if(input.peek() == '-') throw std::runtime_error("Negative unsigned reflection number");
    return ReadNumber<uint64_t>(input);
}
void WriteValue(std::ostream& output, const ReflectionValue& value, unsigned depth, uint64_t& budget) {
    if(depth > kMaxDepth || budget == 0) throw std::runtime_error("Reflection text limit exceeded");
    --budget;
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr(std::is_same_v<T, std::monostate>) output << "null";
        else if constexpr(std::is_same_v<T, bool>) output << "bool " << (data ? 1 : 0);
        else if constexpr(std::is_same_v<T, int64_t>) output << "int " << data;
        else if constexpr(std::is_same_v<T, uint64_t>) output << "uint " << data;
        else if constexpr(std::is_same_v<T, double>) {
            if(!std::isfinite(data)) throw std::runtime_error("Non-finite reflection number");
            output << "float " << std::setprecision(std::numeric_limits<double>::max_digits10) << data;
        } else if constexpr(std::is_same_v<T, std::string>) {
            if(data.size() > budget) throw std::runtime_error("Reflection text string limit exceeded");
            budget -= data.size(); output << "string " << std::quoted(data);
        } else {
            if(data.size() > budget) throw std::runtime_error("Reflection text collection limit exceeded");
            if constexpr(std::is_same_v<T, ReflectionValue::Object>) output << "object ";
            else output << "array ";
            output << data.size();
            for(const auto& entry : data) {
                output << ' ';
                if constexpr(std::is_same_v<T, ReflectionValue::Object>) {
                    if(entry.first.size() > budget) throw std::runtime_error("Reflection text key limit exceeded");
                    budget -= entry.first.size();
                    output << std::quoted(entry.first) << ' '; WriteValue(output, entry.second, depth + 1, budget);
                } else WriteValue(output, entry, depth + 1, budget);
            }
        }
    }, value.data);
    if(!output) throw std::runtime_error("Cannot write reflection text");
}
ReflectionValue ReadValue(std::istream& input, unsigned depth, uint64_t& budget) {
    if(depth > kMaxDepth || budget == 0) throw std::runtime_error("Reflection text limit exceeded");
    --budget;
    // 型名自体も読込量を制限する。
    std::string kind;
    input >> std::ws;
    while(input && input.peek() != EOF && !std::isspace(static_cast<unsigned char>(input.peek()))) {
        if(kind.size() == 8) throw std::runtime_error("Invalid reflection text kind");
        kind += static_cast<char>(input.get());
    }
    if(kind == "null") return {};
    if(kind == "bool") {
        const auto number = ReadNumber<int>(input);
        if(number != 0 && number != 1) throw std::runtime_error("Invalid reflection boolean");
        return ReflectionValue(number != 0);
    }
    if(kind == "int") return ReflectionValue(ReadNumber<int64_t>(input));
    if(kind == "uint") return ReflectionValue(ReadUnsigned(input));
    if(kind == "float") {
        const auto number = ReadNumber<double>(input);
        if(!std::isfinite(number)) throw std::runtime_error("Non-finite reflection number");
        return ReflectionValue(number);
    }
    if(kind == "string") return ReflectionValue(ReadString(input, budget));
    if(kind != "array" && kind != "object") throw std::runtime_error("Invalid reflection text kind");
    const auto count = ReadUnsigned(input);
    if(count > budget) throw std::runtime_error("Reflection text collection limit exceeded");
    if(kind == "array") {
        ReflectionValue::Array values;
        for(uint64_t i = 0; i < count; ++i) values.push_back(ReadValue(input, depth + 1, budget));
        return ReflectionValue(std::move(values));
    }
    ReflectionValue::Object values;
    for(uint64_t i = 0; i < count; ++i) {
        auto key = ReadString(input, budget);
        auto value = ReadValue(input, depth + 1, budget);
        if(!values.emplace(std::move(key), std::move(value)).second) throw std::runtime_error("Duplicate reflection field");
    }
    return ReflectionValue(std::move(values));
}
}
void WriteReflectionTextArchive(std::ostream& output, const ReflectionValue& value) {
    uint64_t budget = kMaxBudget;
    const auto precision = output.precision();
    WriteValue(output, value, 0, budget);
    output.precision(precision);
}
ReflectionValue ReadReflectionTextArchive(std::istream& input) {
    uint64_t budget = kMaxBudget;
    return ReadValue(input, 0, budget);
}
}
