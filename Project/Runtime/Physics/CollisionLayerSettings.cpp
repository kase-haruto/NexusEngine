#include "CollisionLayerSettings.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <Windows.h>

namespace NexusEngine {
namespace {
Error InvalidLayers(const char* message) {
    return Error(ErrorCategory::Resource, 274, message);
}
}
CollisionLayerSettings::CollisionLayerSettings() : matrix(DefaultCollisionMatrix()) {
    names[0] = "Default";
    for(uint32_t index = 0; index < redirects.size(); ++index) {
        redirects[index] = index;
    }
}
uint32_t CollisionLayerSettings::Resolve(uint32_t layer) const noexcept {
    for(size_t depth = 0; layer < 32 && depth < 32; ++depth) {
        if(redirects[layer] == layer) {
            return layer;
        }
        layer = redirects[layer];
    }
    return 32;
}
std::string CollisionLayerSettings::Label(uint32_t layer) const {
    layer = Resolve(layer);
    if(layer >= 32) {
        return "Invalid layer";
    }
    return names[layer].empty() ? "Layer " + std::to_string(layer) : names[layer];
}
bool CollisionLayerSettings::IsValid() const noexcept {
    if(names[0] != "Default" || redirects[0] != 0) {
        return false;
    }
    for(uint32_t row = 0; row < 32; ++row) {
        const auto resolved = Resolve(row);
        if(resolved >= 32 || names[row].size() > 96 || (resolved != row && (!names[row].empty() || names[resolved].empty()))) {
            return false;
        }
        for(unsigned char character : names[row]) {
            if(character < 32 || character == 127) {
                return false;
            }
        }
        if(!names[row].empty() && (names[row].front() == ' ' || names[row].back() == ' ')) {
            return false;
        }
        for(uint32_t column = 0; column < 32; ++column) {
            if(((matrix[row] >> column) & 1u) != ((matrix[column] >> row) & 1u) ||
                (row != column && !names[row].empty() && names[row] == names[column])) {
                return false;
            }
        }
    }
    return true;
}
CollisionMatrix CollisionLayerSettings::EffectiveMatrix() const noexcept {
    CollisionMatrix effective{};
    for(uint32_t row = 0; row < 32; ++row) {
        for(uint32_t column = 0; column < 32; ++column) {
            const auto first = Resolve(row);
            const auto second = Resolve(column);
            if(first < 32 && second < 32 && (matrix[first] & (uint32_t{1} << second)) != 0) {
                effective[row] |= uint32_t{1} << column;
            }
        }
    }
    return effective;
}
Result<CollisionLayerSettings> CollisionLayerSettings::Load(const std::filesystem::path& path) {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if(error) {
        return std::unexpected(InvalidLayers("Cannot inspect PhysicsLayers.settings."));
    }
    if(!exists) {
        return CollisionLayerSettings{};
    }
    std::ifstream input(path, std::ios::binary);
    std::string token;
    unsigned version = 0;
    CollisionLayerSettings settings;
    if(!(input >> token >> version) || token != "NEXUS_COLLISION_LAYERS" || version != 1) {
        return std::unexpected(InvalidLayers("Invalid PhysicsLayers.settings header."));
    }
    for(uint32_t index = 0; index < 32; ++index) {
        uint64_t mask = 0;
        if(!(input >> std::quoted(settings.names[index]) >> settings.redirects[index] >> mask) || mask > UINT32_MAX) {
            return std::unexpected(InvalidLayers("Invalid PhysicsLayers.settings row."));
        }
        settings.matrix[index] = static_cast<uint32_t>(mask);
    }
    if((input >> token) || !input.eof() || !settings.IsValid()) {
        return std::unexpected(InvalidLayers("Invalid collision names, redirects or asymmetric matrix."));
    }
    return settings;
}
Result<void> CollisionLayerSettings::Save(const std::filesystem::path& path) const {
    if(!IsValid() || path.empty()) {
        return std::unexpected(InvalidLayers("Invalid collision settings or save path."));
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if(error) {
        return std::unexpected(InvalidLayers("Cannot create collision settings directory."));
    }
    // 同一volumeの一時ファイルを置換する。失敗時は現在のProject設定を保持する。
    auto temporary = path;
    temporary += ".tmp." + std::to_string(GetCurrentProcessId());
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << "NEXUS_COLLISION_LAYERS 1\n";
    for(uint32_t index = 0; index < 32; ++index) {
        output << std::quoted(names[index]) << ' ' << redirects[index] << ' ' << matrix[index] << '\n';
    }
    output.close();
    if(!output || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        return std::unexpected(InvalidLayers("Cannot save PhysicsLayers.settings; previous settings were kept."));
    }
    return {};
}
}
