#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace NexusEngine {
// 交換対象の C++ レイアウトから独立した値ツリー。ポインタやクロージャを保持しない。
struct ReflectionValue {
    using Array = std::vector<ReflectionValue>;
    using Object = std::map<std::string, ReflectionValue, std::less<>>;
    std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string, Array, Object> data;

    ReflectionValue() = default;
    template<class T> explicit ReflectionValue(T value) : data(std::move(value)) {}
    const ReflectionValue& At(std::string_view key) const {
        const auto& object = std::get<Object>(data);
        auto it = object.find(key);
        if(it == object.end()) throw std::runtime_error("Missing reflection field: " + std::string(key));
        return it->second;
    }
};

template<class T> struct ReflectionTraits;
template<class T> concept ReflectedType = requires { ReflectionTraits<T>::Id; };

template<class T, class = void> struct ReflectionCodec;
template<class T> ReflectionValue CaptureReflection(const T& value) { return ReflectionCodec<T>::Save(value); }
template<class T> void RestoreReflection(const ReflectionValue& value, T& target) { ReflectionCodec<T>::Load(value, target); }

template<class T> struct ReflectionCodec<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
    static ReflectionValue Save(T value) {
        if constexpr(std::is_same_v<T, bool>) return ReflectionValue(value);
        else if constexpr(std::is_floating_point_v<T>) {
            if(!std::isfinite(value)) throw std::runtime_error("Non-finite reflection value");
            return ReflectionValue(static_cast<double>(value));
        } else if constexpr(std::is_signed_v<T>) return ReflectionValue(static_cast<int64_t>(value));
        else return ReflectionValue(static_cast<uint64_t>(value));
    }
    static void Load(const ReflectionValue& source, T& value) {
        // 暗黙の型変換による破損を避け、数値の種類と範囲を照合する。
        if constexpr(std::is_same_v<T, bool>) value = std::get<bool>(source.data);
        else if constexpr(std::is_floating_point_v<T>) {
            const auto raw = std::get<double>(source.data);
            if(!std::isfinite(raw) || raw < -std::numeric_limits<T>::max() || raw > std::numeric_limits<T>::max())
                throw std::runtime_error("Floating reflection value out of range");
            value = static_cast<T>(raw);
        } else if constexpr(std::is_signed_v<T>) {
            const auto raw = std::get<int64_t>(source.data);
            if(raw < std::numeric_limits<T>::min() || raw > std::numeric_limits<T>::max())
                throw std::runtime_error("Signed reflection value out of range");
            value = static_cast<T>(raw);
        } else {
            const auto raw = std::get<uint64_t>(source.data);
            if(raw > std::numeric_limits<T>::max()) throw std::runtime_error("Unsigned reflection value out of range");
            value = static_cast<T>(raw);
        }
    }
};
template<class T> struct ReflectionCodec<T, std::enable_if_t<std::is_enum_v<T>>> {
    static ReflectionValue Save(T value) { return CaptureReflection(static_cast<std::underlying_type_t<T>>(value)); }
    static void Load(const ReflectionValue& source, T& value) {
        std::underlying_type_t<T> raw{}; RestoreReflection(source, raw); value = static_cast<T>(raw);
    }
};
template<> struct ReflectionCodec<std::string> {
    static ReflectionValue Save(const std::string& value) { return ReflectionValue(value); }
    static void Load(const ReflectionValue& source, std::string& value) { value = std::get<std::string>(source.data); }
};
template<class T, size_t N> struct ReflectionCodec<std::array<T, N>> {
    static ReflectionValue Save(const std::array<T, N>& value) {
        ReflectionValue::Array result; result.reserve(N);
        for(const auto& element : value) result.push_back(CaptureReflection(element));
        return ReflectionValue(std::move(result));
    }
    static void Load(const ReflectionValue& source, std::array<T, N>& value) {
        const auto& array = std::get<ReflectionValue::Array>(source.data);
        if(array.size() != N) throw std::runtime_error("Reflection array size changed");
        for(size_t i = 0; i < N; ++i) RestoreReflection(array[i], value[i]);
    }
};
template<class T> struct ReflectionCodec<std::vector<T>> {
    static ReflectionValue Save(const std::vector<T>& value) {
        ReflectionValue::Array result; result.reserve(value.size());
        for(const T& element : value) result.push_back(CaptureReflection(element));
        return ReflectionValue(std::move(result));
    }
    static void Load(const ReflectionValue& source, std::vector<T>& value) {
        const auto& array = std::get<ReflectionValue::Array>(source.data);
        std::vector<T> next(array.size());
        for(size_t i = 0; i < array.size(); ++i) {
            T element{}; RestoreReflection(array[i], element); next[i] = std::move(element);
        }
        value = std::move(next);
    }
};
template<class T> struct ReflectionCodec<std::optional<T>> {
    static ReflectionValue Save(const std::optional<T>& value) { return value ? CaptureReflection(*value) : ReflectionValue(); }
    static void Load(const ReflectionValue& source, std::optional<T>& value) {
        if(std::holds_alternative<std::monostate>(source.data)) value.reset();
        else { T next{}; RestoreReflection(source, next); value = std::move(next); }
    }
};
template<ReflectedType T> struct ReflectionCodec<T> {
    static ReflectionValue Save(const T& value) {
        ReflectionValue::Object fields;
        ReflectionTraits<T>::Visit([&](const char* id, auto member) { fields.emplace(id, CaptureReflection(value.*member)); });
        return ReflectionValue(ReflectionValue::Object{{"type", ReflectionValue(std::string(ReflectionTraits<T>::Id))},
            {"fields", ReflectionValue(std::move(fields))}});
    }
    static void Load(const ReflectionValue& source, T& value) {
        if(std::get<std::string>(source.At("type").data) != ReflectionTraits<T>::Id)
            throw std::runtime_error("Reflected type ID changed");
        const auto& fields = std::get<ReflectionValue::Object>(source.At("fields").data);
        // 追加メンバは新コンストラクタの既定値を保ち、削除メンバは読み飛ばす。
        ReflectionTraits<T>::Visit([&](const char* id, auto member) {
            if(auto it = fields.find(id); it != fields.end()) RestoreReflection(it->second, value.*member);
        });
    }
};
}
