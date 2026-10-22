#include "ModelAssetLibrary.h"
#include "ModelLoader.h"
#include "Graphics/Resource/WicImageDecoder.h"
#include <fstream>
#include <algorithm>
#include <cctype>

namespace NexusEngine {
namespace {
Error Invalid(const char* message) { return Error(ErrorCategory::Resource, 130, message); }
std::string Utf8(const std::filesystem::path& path) {
    auto text = path.generic_u8string(); return {reinterpret_cast<const char*>(text.data()), text.size()};
}
std::filesystem::path Path(const std::string& text) { return std::filesystem::path(reinterpret_cast<const char8_t*>(text.c_str())); }
}
ModelAssetLibrary::ModelAssetLibrary(std::filesystem::path root, const ModelAssetData& initial)
    : root_(std::filesystem::weakly_canonical(root)), aggregate_(initial) {
    models_.emplace("", initial);
    ranges_[{"", ""}] = {0, static_cast<uint32_t>(initial.nodes.size())};
}
Result<std::string> ModelAssetLibrary::Reference(const std::filesystem::path& path) const {
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(path.is_absolute() ? path : root_ / path, error);
    if(error) return std::unexpected(Invalid("Asset path cannot be resolved."));
    auto relative = resolved.lexically_relative(root_);
    if(relative.empty() || relative.is_absolute() || *relative.begin() == ".." || !std::filesystem::is_regular_file(resolved, error) || error)
        return std::unexpected(Invalid("Choose a file inside the project Asset folder."));
    return Utf8(relative);
}
Result<const ModelAssetData*> ModelAssetLibrary::Model(const std::string& reference) {
    if(auto found = models_.find(reference); found != models_.end()) return &found->second;
    auto canonical = Reference(Path(reference));
    if(!canonical) return std::unexpected(canonical.error());
    auto extension = Utf8(Path(*canonical).extension());
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if(extension != ".gltf" && extension != ".glb" && extension != ".obj") return std::unexpected(Invalid("Supported meshes: glTF, GLB, OBJ."));
    auto loaded = extension == ".obj" ? ModelLoader{}.LoadObj(root_ / Path(*canonical)) : ModelLoader{}.LoadGltf(root_ / Path(*canonical));
    if(!loaded) return std::unexpected(loaded.error());
    if(loaded->nodes.empty() || loaded->meshes.empty()) return std::unexpected(Invalid("Model contains no meshes."));
    // OBJのように材質を持たないSourceにも、Variantを適用できる既定slotを用意する。
    if(loaded->materials.empty()) loaded->materials.emplace_back();
    return &models_.emplace(reference, std::move(*loaded)).first->second;
}
Result<const std::vector<uint8_t>*> ModelAssetLibrary::Texture(const std::string& reference) {
    if(auto found = textures_.find(reference); found != textures_.end()) return &found->second;
    auto canonical = Reference(Path(reference));
    if(!canonical) return std::unexpected(canonical.error());
    std::ifstream input(root_ / Path(*canonical), std::ios::binary | std::ios::ate);
    auto length = input.tellg();
    if(!input || length <= 0 || length > 128 * 1024 * 1024) return std::unexpected(Invalid("Image is empty or exceeds 128 MiB."));
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if(!input) return std::unexpected(Invalid("Cannot read image."));
    auto decoded = WicImageDecoder{}.Decode(bytes);
    if(!decoded) return std::unexpected(decoded.error());
    return &textures_.emplace(reference, std::move(bytes)).first->second;
}
Result<ModelAssetLibrary::Range> ModelAssetLibrary::Resolve(const std::string& mesh, const std::string& texture) {
    const auto key = std::make_pair(mesh, texture);
    if(auto found = ranges_.find(key); found != ranges_.end()) return found->second;
    auto source = Model(mesh);
    if(!source) return std::unexpected(source.error());
    ModelAssetData addition = **source;
    if(!texture.empty()) {
        auto image = Texture(texture);
        if(!image) return std::unexpected(image.error());
        for(auto& material : addition.materials) material.baseColorImageData = **image;
    }
    const auto nodeOffset = static_cast<uint32_t>(aggregate_.nodes.size());
    const auto meshOffset = static_cast<uint32_t>(aggregate_.meshes.size());
    const auto skinOffset = static_cast<uint32_t>(aggregate_.skins.size());
    const auto materialOffset = static_cast<uint32_t>(aggregate_.materials.size());
    for(auto& node : addition.nodes) {
        if(node.parentIndex != ModelNodeAssetData::kNoParent) node.parentIndex += nodeOffset;
        for(auto& index : node.meshIndices) index += meshOffset;
    }
    for(auto& data : addition.meshes) {
        if(data.skinIndex != MeshAssetData::kNoSkin) data.skinIndex += skinOffset;
        for(auto& part : data.submeshes) part.materialSlot += materialOffset;
    }
    for(auto& skin : addition.skins) for(auto& joint : skin.joints) joint += nodeOffset;
    for(auto& clip : addition.animations) for(auto& channel : clip.channels) channel.nodeIndex += nodeOffset;
    // 追加を全て検証してからsnapshotを交換し、失敗時に半端な範囲を公開しない。
    ModelAssetData next = aggregate_;
    const auto append = [](auto& target, auto& values) {
        target.insert(target.end(), std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
    };
    append(next.nodes, addition.nodes); append(next.meshes, addition.meshes); append(next.skins, addition.skins);
    append(next.materials, addition.materials); append(next.animations, addition.animations);
    aggregate_ = std::move(next);
    Range range{nodeOffset, static_cast<uint32_t>(aggregate_.nodes.size()), materialOffset};
    ranges_.emplace(key, range); return range;
}
}
