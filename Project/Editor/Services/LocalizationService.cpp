#include "LocalizationService.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace NexusEngine {
bool LocalizationService::Load(const std::filesystem::path& directory) {
    const auto read = [](const auto& path, auto& dictionary) {
        std::ifstream file(path, std::ios::binary);
        if(!file) return false;
        dictionary.clear();
        std::string line, key, value;
        while(std::getline(file, line)) {
            std::istringstream stream(line);
            if(stream >> key >> std::quoted(value)) dictionary[key] = value;
        }
        return !file.bad();
    };
    const bool en = read(directory / "en.lang", english_);
    const bool ja = read(directory / "ja.lang", japanese_);
    return en && ja;
}
std::string LocalizationService::Text(std::string_view key) const {
    // キーを所有stringに変換して返し、フレーム途中の言語変更でも寿命を保証する。
    if(language_ == "ja") if(auto it = japanese_.find(key); it != japanese_.end()) return it->second;
    if(auto it = english_.find(key); it != english_.end()) return it->second;
    return std::string(key);
}
std::string LocalizationService::Label(std::string_view key, std::string_view id) const {
    return Text(key) + "###" + std::string(id);
}
}
