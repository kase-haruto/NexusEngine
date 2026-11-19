#include "ContentMetadata.h"
#include "AssetCatalog.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "ThirdParty/nlohmann/json.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <regex>
#include <set>
#include <cstring>
#include <array>
#include <sstream>
#include <algorithm>
#include <cctype>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace NexusEngine {
namespace {
using Json = nlohmann::json;
Error Invalid(std::string text) {
    return Error(ErrorCategory::Resource, 295, std::move(text));
}
void Texture(ContentMetadata &metadata) {
    const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Apartment {
        bool owns = false;
        ~Apartment() {
            if (owns) {
                CoUninitialize();
            }
        }
    } apartmentScope{SUCCEEDED(apartment)};
    // COM objectをすべて解放した後で、worker自身のapartmentを閉じる。
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(metadata.file.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame))) {
        UINT width = 0, height = 0, frames = 0;
        frame->GetSize(&width, &height);
        decoder->GetFrameCount(&frames);
        WICPixelFormatGUID pixel{};
        frame->GetPixelFormat(&pixel);
        Microsoft::WRL::ComPtr<IWICComponentInfo> component;
        Microsoft::WRL::ComPtr<IWICPixelFormatInfo> info;
        UINT bits = 0;
        if (SUCCEEDED(factory->CreateComponentInfo(pixel, &component)) && SUCCEEDED(component.As(&info))) {
            info->GetBitsPerPixel(&bits);
        }
        metadata.fields["Resolution"] = std::to_string(width) + " x " + std::to_string(height);
        metadata.fields["Source pixel bits"] = std::to_string(bits);
        metadata.fields["Frames"] = std::to_string(frames);
        metadata.fields["Mip count"] = "1 (source image)";
    } else if (metadata.file.extension() == ".dds") {
        std::ifstream input(metadata.file, std::ios::binary);
        std::array<uint32_t, 32> header{};
        input.read(reinterpret_cast<char *>(header.data()), sizeof(header));
        if (input && header[0] == 0x20534444 && header[1] == 124) {
            metadata.fields["Resolution"] = std::to_string(header[4]) + " x " + std::to_string(header[3]);
            metadata.fields["Mip count"] = std::to_string(std::max(1u, header[7]));
            const auto code = header[21];
            std::string format(4, ' ');
            std::memcpy(format.data(), &code, 4);
            if (format == "DX10") {
                uint32_t dxgi = 0;
                input.read(reinterpret_cast<char *>(&dxgi), 4);
                format += " / DXGI " + std::to_string(dxgi);
            }
            metadata.fields["Source format"] = format;
        } else {
            metadata.error = "Invalid DDS header.";
        }
    } else {
        metadata.error = "Texture metadata could not be decoded.";
    }
}
void JsonStrings(const Json &json, std::vector<std::string> &values, size_t depth = 0) {
    if (depth > 128 || values.size() >= 8192) {
        return;
    }
    if (json.is_string()) {
        values.push_back(json.get<std::string>());
    } else if (json.is_array() || json.is_object()) {
        for (const auto &child : json) {
            JsonStrings(child, values, depth + 1);
        }
    }
}
std::string SourceCode(std::string text, bool hideStrings) {
    // 宣言情報をコメントや文字列の例から誤検出しない。行番号とinclude literalは必要時だけ保持する。
    enum class State { Code, Line, Block, Quoted };
    State state = State::Code;
    char quote = 0;
    for (size_t index = 0; index < text.size(); ++index) {
        const char current = text[index], next = index + 1 < text.size() ? text[index + 1] : 0;
        if (state == State::Line) {
            if (current == '\n') {
                state = State::Code;
            } else {
                text[index] = ' ';
            }
        } else if (state == State::Block) {
            if (current == '*' && next == '/') {
                text[index] = text[index + 1] = ' ';
                ++index;
                state = State::Code;
            } else if (current != '\n') {
                text[index] = ' ';
            }
        } else if (state == State::Quoted) {
            if (hideStrings && current != '\n') {
                text[index] = ' ';
            }
            if (current == '\\' && index + 1 < text.size()) {
                if (hideStrings) {
                    text[index + 1] = ' ';
                }
                ++index;
            } else if (current == quote) {
                state = State::Code;
            }
        } else if (current == '/' && next == '/') {
            text[index] = text[index + 1] = ' ';
            ++index;
            state = State::Line;
        } else if (current == '/' && next == '*') {
            text[index] = text[index + 1] = ' ';
            ++index;
            state = State::Block;
        } else if (current == '"' || current == 39) {
            quote = current;
            state = State::Quoted;
            if (hideStrings) {
                text[index] = ' ';
            }
        }
    }
    return text;
}
bool ReferenceCandidate(const std::string &text) {
    if (IsAssetIdentity(text)) {
        return true;
    }
    if (text.empty() || text.size() > 32768 || text.find_first_of("\r\n") != std::string::npos ||
        text.starts_with("data:")) {
        return false;
    }
    auto extension = std::filesystem::path(ConvertString(text)).extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    static const std::set<std::string> extensions{
        ".png",        ".jpg",        ".jpeg",    ".tga",      ".bmp",   ".dds",    ".hdr",
        ".gltf",       ".glb",        ".obj",     ".mtl",      ".bin",   ".mat",    ".nxmat",
        ".level",      ".json",       ".prefab",  ".nxprefab", ".curve", ".nxanim", ".nxcontroller",
        ".nxtimeline", ".nxretarget", ".nxlayer", ".wav",      ".ogg",   ".mp3",    ".hlsl",
        ".hlsli",      ".h",          ".hpp",     ".cpp",      ".cs"};
    return extensions.contains(extension);
}
} // namespace
Result<std::string> ContentMetadataProvider::Text(const std::filesystem::path &file, size_t limit) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || static_cast<uintmax_t>(input.tellg()) > limit) {
        return std::unexpected(Invalid("Unreadable file or metadata size budget exceeded."));
    }
    std::string text(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!input && !text.empty()) {
        return std::unexpected(Invalid("Could not read file metadata."));
    }
    return text;
}
Result<ContentMetadata> ContentMetadataProvider::Read(const std::filesystem::path &file,
                                                      const std::filesystem::path &root, bool textureDetails,
                                                      std::stop_token stop) {
    ContentMetadata metadata;
    metadata.file = file;
    std::error_code error;
    if (std::filesystem::is_directory(file, error)) {
        metadata.fields["Type"] = "Folder";
        return metadata;
    }
    error.clear();
    metadata.bytes = std::filesystem::file_size(file, error);
    if (error) {
        return std::unexpected(Invalid(error.message()));
    }
    const ContentItemRegistry registry;
    const std::string typeId = registry.Classify(file, false).id;
    metadata.fields["Type"] = typeId;
    metadata.fields["File size"] = std::to_string(metadata.bytes) + " bytes";
    const auto sidecar = std::filesystem::path(file.wstring() + L".nxmeta");
    if (std::filesystem::exists(sidecar, error)) {
        const auto contents = Text(sidecar, 65536);
        if (contents) {
            try {
                const auto json = Json::parse(*contents);
                metadata.guid = json.value("id", json.value("uuid", std::string{}));
            } catch (...) {
            }
        }
    }
    if (typeId == "Texture") {
        if (textureDetails) {
            Texture(metadata);
        }
        return metadata;
    }
    auto extension = file.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    static const std::set<std::string> textExtensions{
        ".glb",      ".gltf",  ".obj",      ".mtl",          ".json",       ".level",      ".prefab",
        ".nxprefab", ".curve", ".nxanim",   ".nxcontroller", ".nxtimeline", ".nxretarget", ".nxlayer",
        ".hlsl",     ".hlsli", ".cpp",      ".cc",           ".cxx",        ".h",          ".hpp",
        ".hxx",      ".cs",    ".settings", ".mat",          ".nxmat"};
    if (!textExtensions.contains(extension)) {
        return metadata;
    }
    if (stop.stop_requested()) {
        return std::unexpected(Invalid("Metadata request cancelled."));
    }
    std::string text;
    if (extension == ".glb") {
        std::ifstream input(file, std::ios::binary);
        std::array<uint32_t, 5> header{};
        input.read(reinterpret_cast<char *>(header.data()), sizeof(header));
        if (!input || header[0] != 0x46546C67 || header[1] != 2 || header[4] != 0x4E4F534A ||
            header[3] > 16 * 1024 * 1024) {
            return std::unexpected(Invalid("Invalid or oversized GLB JSON chunk."));
        }
        text.resize(header[3]);
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
    } else {
        const auto loaded = Text(file);
        if (!loaded) {
            metadata.error = loaded.error().GetMessageText();
            return metadata;
        }
        text = *loaded;
    }
    std::vector<std::string> references;
    if (typeId == "Cpp" || typeId == "Header" || typeId == "CSharp") {
        metadata.language = typeId == "CSharp" ? "C#" : "C++";
        const auto declarations = SourceCode(text, true);
        const auto includes = SourceCode(text, false);
        std::smatch match;
        if (std::regex_search(declarations, match, std::regex(R"(\bnamespace\s+([A-Za-z_][A-Za-z_0-9.:]*))"))) {
            metadata.nameSpace = match[1].str();
        }
        if (std::regex_search(declarations, match, std::regex(R"(\b(?:class|struct)\s+([A-Za-z_][A-Za-z_0-9]*))"))) {
            metadata.className = match[1].str();
        }
        const std::regex include(R"re(#\s*include\s*"([^"\r\n]+)")re");
        for (std::sregex_iterator it(includes.begin(), includes.end(), include), end; it != end; ++it) {
            references.push_back((*it)[1].str());
        }
    } else if (extension == ".obj" || extension == ".mtl" || typeId == "Shader") {
        std::istringstream lines(text);
        std::string line;
        uint64_t vertices = 0, triangles = 0, meshes = 0;
        std::set<std::string> materials;
        while (std::getline(lines, line) && !stop.stop_requested()) {
            std::istringstream row(line);
            std::string key, value;
            row >> key;
            if (key == "v") {
                ++vertices;
            }
            if (key == "o" || key == "g") {
                ++meshes;
            }
            if (key == "usemtl") {
                row >> value;
                materials.insert(value);
            }
            if (key == "f") {
                size_t count = 0;
                while (row >> value) {
                    ++count;
                }
                if (count >= 3) {
                    triangles += count - 2;
                }
            }
            if (key == "mtllib" || key.starts_with("map_") || key == "bump") {
                std::getline(row, value);
                const auto first = value.find_first_not_of(" \t");
                if (first != std::string::npos) {
                    references.push_back(value.substr(first));
                }
            }
        }
        if (extension == ".obj") {
            metadata.triangles = triangles;
            metadata.fields["Vertices"] = std::to_string(vertices);
            metadata.fields["Meshes"] = std::to_string(meshes);
            metadata.fields["Materials"] = std::to_string(materials.size());
        }
    }
    try {
        if (extension == ".gltf" || extension == ".glb" ||
            (!text.empty() && (text.front() == '{' || text.front() == '['))) {
            const auto json = Json::parse(text, [](int depth, Json::parse_event_t, const Json &) {
                if (depth > 128) {
                    throw std::runtime_error("Metadata nesting budget exceeded.");
                }
                return true;
            });
            JsonStrings(json, references);
            if (typeId == "Model" && json.contains("meshes")) {
                uint64_t vertices = 0, triangles = 0;
                const auto accessors = json.value("accessors", Json::array());
                for (const auto &mesh : json.at("meshes")) {
                    for (const auto &primitive : mesh.value("primitives", Json::array())) {
                        const auto position = primitive.value("attributes", Json::object()).value("POSITION", -1);
                        const auto vertexCount = position >= 0 && static_cast<size_t>(position) < accessors.size()
                                                     ? accessors[position].value("count", uint64_t{})
                                                     : 0;
                        vertices += vertexCount;
                        const auto index = primitive.value("indices", -1);
                        const auto count = index >= 0 && static_cast<size_t>(index) < accessors.size()
                                               ? accessors[index].value("count", uint64_t{})
                                               : vertexCount;
                        const auto mode = primitive.value("mode", 4);
                        triangles += mode == 4 ? count / 3 : (mode == 5 || mode == 6) && count >= 3 ? count - 2 : 0;
                    }
                }
                metadata.triangles = triangles;
                metadata.fields["Vertices"] = std::to_string(vertices);
                metadata.fields["Meshes"] = std::to_string(json.at("meshes").size());
                metadata.fields["Materials"] = std::to_string(json.value("materials", Json::array()).size());
            }
        } else {
            const std::regex quoted(R"re("([^"\r\n]+)")re");
            for (std::sregex_iterator it(text.begin(), text.end(), quoted), end; it != end; ++it) {
                references.push_back((*it)[1].str());
            }
        }
    } catch (const std::exception &failure) {
        metadata.error = failure.what();
    }
    std::set<std::string> unique;
    for (const auto &reference : references) {
        if (!ReferenceCandidate(reference) || !unique.insert(reference).second) {
            continue;
        }
        auto resolved =
            IsAssetIdentity(reference)
                ? ResolveAssetReference(root, reference)
                : ResolveFileWithinRoot(
                      root,
                      AssetCatalog::Utf8((file.parent_path() / ConvertString(reference)).lexically_relative(root)));
        if (resolved && (!std::filesystem::exists(*resolved, error) || error)) {
            resolved = std::unexpected(Invalid("Relative dependency unavailable."));
            error.clear();
        }
        if (!resolved && !IsAssetIdentity(reference)) {
            resolved = ResolveAssetReference(root, reference);
        }
        bool missing = !resolved;
        if (resolved) {
            missing = !std::filesystem::exists(*resolved, error) || error;
            error.clear();
        }
        metadata.dependencies.push_back({reference, resolved ? *resolved : std::filesystem::path{}, missing});
    }
    if (metadata.triangles) {
        metadata.fields["Triangles"] = std::to_string(*metadata.triangles);
    }
    return metadata;
}
ContentMetadataRequest::~ContentMetadataRequest() {
    cancellation_.request_stop();
    if (task_.valid()) {
        task_.wait();
    }
}
void ContentMetadataRequest::Request(std::filesystem::path file, std::filesystem::path root) {
    if (requested_ == file && root_ == root) {
        return;
    }
    ++generation_;
    requested_ = std::move(file);
    root_ = std::move(root);
    cancellation_.request_stop();
    value_.reset();
    Poll();
}
void ContentMetadataRequest::Invalidate() {
    ++generation_;
    cancellation_.request_stop();
    value_.reset();
}
void ContentMetadataRequest::Poll() {
    if (task_.valid()) {
        if (task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        try {
            auto value = task_.get();
            if (running_ == requested_ && runningGeneration_ == generation_) {
                if (value) {
                    value_ = std::move(*value);
                } else {
                    ContentMetadata error;
                    error.file = running_;
                    error.error = value.error().GetMessageText();
                    value_ = std::move(error);
                }
            }
        } catch (const std::exception &error) {
            ContentMetadata value;
            value.error = error.what();
            value_ = std::move(value);
        }
    }
    if (!requested_.empty() && !value_ && !task_.valid()) {
        running_ = requested_;
        runningGeneration_ = generation_;
        cancellation_ = std::stop_source{};
        task_ = std::async(std::launch::async, [file = requested_, root = root_, stop = cancellation_.get_token()] {
            return ContentMetadataProvider::Read(file, root, true, stop);
        });
    }
}
bool ContentMetadataRequest::Busy() const {
    return task_.valid();
}
const std::optional<ContentMetadata> &ContentMetadataRequest::Value() const {
    return value_;
}
} // namespace NexusEngine
