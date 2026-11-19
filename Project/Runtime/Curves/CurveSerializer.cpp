#include "CurveSerializer.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace NexusEngine {
Result<FloatCurve> ParseFloatCurve(std::string_view text) {
    try {
        if(text.size() > 32 * 1024 * 1024) {
            return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: file exceeds 32 MiB"));
        }
        const auto json = nlohmann::json::parse(text);
        if(!json.is_object() || !json.at("version").is_number_integer() ||
            (json.at("version").get<int>() != 1 && json.at("version").get<int>() != 2) || !json.at("keys").is_array()) {
            return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: unsupported format"));
        }
        std::vector<CurveKey> keys;
        if(json.at("keys").size() > 100000) {
            return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: too many keys"));
        }
        for(const auto& value : json.at("keys")) {
            for(const auto* field : {"interpolation", "tangentMode"}) {
                if(value.contains(field) && !value.at(field).is_number_integer()) {
                    return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: mode must be an integer"));
                }
            }
            CurveKey key;
            key.x = value.at("x").get<double>();
            key.y = value.at("y").get<double>();
            key.inTangent = value.value("inTangent", 0.0);
            key.outTangent = value.value("outTangent", 0.0);
            key.interpolation = static_cast<CurveInterpolation>(value.value("interpolation", 1));
            key.tangentMode = static_cast<CurveTangentMode>(value.value("tangentMode", 2));
            if(json.at("version").get<int>() >= 2) {
                key.inWeight = value.value("inWeight", 1.0 / 3);
                key.outWeight = value.value("outWeight", 1.0 / 3);
            } else if(key.interpolation == CurveInterpolation::Bezier) {
                return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: Bezier requires version 2"));
            }
            keys.push_back(key);
        }
        FloatCurve curve;
        auto result = curve.SetKeys(std::move(keys));
        if(!result) {
            return std::unexpected(result.error());
        }
        return curve;
    } catch(const std::exception& error) {
        return std::unexpected(Error(ErrorCategory::Resource, 392, std::string("Curve: ") + error.what()));
    }
}
Result<FloatCurve> LoadFloatCurve(const std::filesystem::path& file) {
    try {
        if(std::filesystem::file_size(file) > 32 * 1024 * 1024) {
            return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: file exceeds 32 MiB"));
        }
        std::ifstream input(NativeFilePath(file), std::ios::binary);
        if(!input) {
            return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: cannot read file"));
        }
        std::ostringstream text;
        text << input.rdbuf();
        if(input.bad()) {
            return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: incomplete file"));
        }
        return ParseFloatCurve(text.str());
    } catch(const std::exception& error) {
        return std::unexpected(Error(ErrorCategory::Resource, 392, std::string("Curve: ") + error.what()));
    }
}
std::string SerializeFloatCurve(const FloatCurve& curve) {
    nlohmann::json json{{"version", 1}, {"keys", nlohmann::json::array()}};
    const bool weighted = std::ranges::any_of(curve.Keys(), [](const auto& key) {
        return key.interpolation == CurveInterpolation::Bezier || key.inWeight != 1.0 / 3 || key.outWeight != 1.0 / 3;
    });
    if(weighted) {
        json["version"] = 2;
    }
    for(const auto& key : curve.Keys()) {
        json["keys"].push_back({{"x", key.x}, {"y", key.y},
            {"inTangent", key.inTangent}, {"outTangent", key.outTangent},
            {"interpolation", static_cast<int>(key.interpolation)},
            {"tangentMode", static_cast<int>(key.tangentMode)}});
        if(weighted) {
            json["keys"].back()["inWeight"] = key.inWeight;
            json["keys"].back()["outWeight"] = key.outWeight;
        }
    }
    return json.dump(2) + "\n";
}
Result<void> SaveFloatCurve(const std::filesystem::path& file, const FloatCurve& curve) {
    const auto text = SerializeFloatCurve(curve);
    if(text.size() > 32 * 1024 * 1024) {
        return std::unexpected(Error(ErrorCategory::Resource, 392, "Curve: serialized file exceeds 32 MiB"));
    }
    return WriteFileAtomically(file, text);
}
}
