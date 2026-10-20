#include "HdrImage.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
namespace NexusEngine {
    Result<HdrImage> LoadHdrImage(const std::filesystem::path& path) {
        auto invalid = [&]() -> Result<HdrImage> {
            return std::unexpected(Error(ErrorCategory::Resource, 90, "Invalid/unsupported Radiance HDR image: " + path.string()));
        };
        std::ifstream file(path, std::ios::binary);
        std::string line;
        if(!std::getline(file, line) || (line != "#?RADIANCE" && line != "#?RGBE" && line != "#?RADIANCE\r" && line != "#?RGBE\r")) return invalid();
        bool format = false;
        while(std::getline(file, line)) {
            if(!line.empty() && line.back() == '\r') line.pop_back();
            if(line.empty()) break;
            if(line == "FORMAT=32-bit_rle_rgbe") format = true;
        }
        if(!format || !std::getline(file, line)) return invalid();
        HdrImage result; std::string y, x; std::istringstream resolution(line);
        if(!(resolution >> y >> result.height >> x >> result.width) || y != "-Y" || x != "+X" ||
           !result.width || !result.height || result.width > 16384 || result.height > 8192 ||
           uint64_t(result.width) * result.height > 33554432) return invalid();
        result.pixels.resize(size_t(result.width) * result.height);
        std::vector<std::array<unsigned char, 4>> scanline(result.width);
        for(uint32_t row = 0; row < result.height; ++row) {
            std::array<unsigned char, 4> header {};
            if(!file.read(reinterpret_cast<char*>(header.data()), 4)) return invalid();
            if(result.width >= 8 && result.width <= 32767 && header[0] == 2 && header[1] == 2 && !(header[2] & 128)) {
                if((uint32_t(header[2]) * 256 + header[3]) != result.width) return invalid();
                for(uint32_t channel = 0; channel < 4; ++channel) {
                    uint32_t column = 0;
                    while(column < result.width) {
                        unsigned char count = 0, value = 0;
                        if(!file.read(reinterpret_cast<char*>(&count), 1) || !count) return invalid();
                        const uint32_t length = count > 128 ? count - 128 : count;
                        if(length > result.width - column) return invalid();
                        if(count > 128) {
                            if(!file.read(reinterpret_cast<char*>(&value), 1)) return invalid();
                            for(uint32_t i = 0; i < length; ++i) scanline[column++][channel] = value;
                        } else {
                            for(uint32_t i = 0; i < length; ++i) {
                                if(!file.read(reinterpret_cast<char*>(&value), 1)) return invalid();
                                scanline[column++][channel] = value;
                            }
                        }
                    }
                }
            } else {
                scanline[0] = header;
                if(result.width > 1 && !file.read(reinterpret_cast<char*>(scanline.data() + 1), (result.width - 1) * 4)) return invalid();
                // Old RGBE run markers need a different decoder; reject rather than silently corrupt.
                for(const auto& pixel : scanline) if(pixel[0] == 1 && pixel[1] == 1 && pixel[2] == 1) return invalid();
            }
            for(uint32_t column = 0; column < result.width; ++column) {
                const auto& rgbe = scanline[column];
                float scale = rgbe[3] ? std::ldexp(1.0f, int(rgbe[3]) - 136) : 0;
                auto& pixel = result.pixels[size_t(row) * result.width + column];
                for(uint32_t c = 0; c < 3; ++c) pixel[c] = std::min(float(rgbe[c]) * scale, 65504.0f);
                pixel[3] = 1;
            }
        }
        return result;
    }
}
