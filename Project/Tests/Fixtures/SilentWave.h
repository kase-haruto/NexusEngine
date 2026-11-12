#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
namespace NexusTests {
/** \brief 実XAudio2/Managed再生を確認するため、1秒の無音PCM WAVをfixtureとして生成する */
inline bool WriteSilentWave(const std::filesystem::path& file) {
    std::error_code error; std::filesystem::create_directories(file.parent_path(),error); if(error) return false;
    std::ofstream wave(file,std::ios::binary);
    const auto u16=[&](uint16_t value) { wave.write(reinterpret_cast<const char*>(&value),2); };
    const auto u32=[&](uint32_t value) { wave.write(reinterpret_cast<const char*>(&value),4); };
    wave.write("RIFF",4); u32(48000+36); wave.write("WAVEfmt ",8); u32(16);
    u16(1); u16(1); u32(48000); u32(48000); u16(1); u16(8);
    wave.write("data",4); u32(48000);
    const std::string silence(48000,static_cast<char>(128)); wave.write(silence.data(),static_cast<std::streamsize>(silence.size()));
    return static_cast<bool>(wave);
}
}
