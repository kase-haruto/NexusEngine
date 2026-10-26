#include "ModelAssetLibrary.h"
#include "ModelLoader.h"
#include "PrimitiveMeshBuilder.h"
#include "Graphics/Resource/WicImageDecoder.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <set>
#include <type_traits>

namespace NexusEngine {
namespace {
Error Invalid(const char* message) { return Error(ErrorCategory::Resource, 130, message); }
std::string Utf8(const std::filesystem::path& path) {
    auto text = path.generic_u8string(); return {reinterpret_cast<const char*>(text.data()), text.size()};
}
std::filesystem::path Path(const std::string& text) { return std::filesystem::path(reinterpret_cast<const char8_t*>(text.c_str())); }
std::string MeshKey(PrimitiveShape shape, const std::string& mesh) {
    return shape == PrimitiveShape::Asset ? mesh : ":primitive/" + std::to_string(static_cast<uint32_t>(shape));
}
}
ModelAssetLibrary::ModelAssetLibrary(std::filesystem::path root, const ModelAssetData& initial)
    : ModelAssetLibrary(std::make_shared<ModelAssetData>(initial), std::move(root)) {}
ModelAssetLibrary::ModelAssetLibrary(std::shared_ptr<ModelAssetData> initial, std::filesystem::path root, ReloadStartup reloadStartup)
    : root_(std::filesystem::weakly_canonical(root)), aggregate_(std::move(initial)), reloadStartup_(std::move(reloadStartup)) {
    models_.emplace(kStartupModelAssetReference, aggregate_);
    ranges_[{kStartupModelAssetReference, ""}] = {0, static_cast<uint32_t>(aggregate_->nodes.size())};
}
void ModelAssetLibrary::Retain(std::span<const Request> requests) {
    std::set<std::pair<std::string, std::string>> active;
    std::set<std::string> meshes, textures;
    for(const auto& request : requests) {
        if(!IsValidPrimitiveShape(request.shape)) continue;
        auto mesh = MeshKey(request.shape, request.mesh);
        if(mesh.empty()) continue;
        active.emplace(mesh, request.texture);
        meshes.insert(std::move(mesh));
        if(!request.texture.empty()) textures.insert(request.texture);
    }
    if(std::ranges::any_of(ranges_, [&](const auto& entry) { return !active.contains(entry.first); })) {
        // Rebuild ranges before the caller resolves them; offsets cannot be compacted mid-frame.
        aggregate_ = std::make_shared<ModelAssetData>();
        ranges_.clear();
        ++revision_;
    }
    // Synthetic sources without a reload callback must remain available for legacy references.
    std::erase_if(models_, [&](const auto& entry) {
        return (entry.first != kStartupModelAssetReference || reloadStartup_) && !meshes.contains(entry.first);
    });
    std::erase_if(textures_, [&](const auto& entry) { return !textures.contains(entry.first); });
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
    if(reference.empty()) return std::unexpected(Invalid("No mesh is assigned."));
    if(auto found = models_.find(reference); found != models_.end()) return found->second.get();
    if(reference == kStartupModelAssetReference && reloadStartup_) {
        auto loaded = reloadStartup_();
        if(!loaded) return std::unexpected(loaded.error());
        return models_.emplace(reference, std::make_shared<ModelAssetData>(std::move(*loaded))).first->second.get();
    }
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
    return models_.emplace(reference, std::make_shared<ModelAssetData>(std::move(*loaded))).first->second.get();
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
    // An unassigned renderer has no geometry, even if the library contains models.
    if(mesh.empty()) return Range{};
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
    const auto nodeOffset = static_cast<uint32_t>(aggregate_->nodes.size());
    const auto meshOffset = static_cast<uint32_t>(aggregate_->meshes.size());
    const auto skinOffset = static_cast<uint32_t>(aggregate_->skins.size());
    const auto materialOffset = static_cast<uint32_t>(aggregate_->materials.size());
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
    // Only published snapshots require a copy. Reserve every destination before committing
    // so an allocation failure cannot expose a partly appended range.
    if(aggregate_.use_count() != 1) aggregate_ = std::make_shared<ModelAssetData>(*aggregate_);
    auto& next = *aggregate_;
    next.nodes.reserve(next.nodes.size() + addition.nodes.size());
    next.meshes.reserve(next.meshes.size() + addition.meshes.size());
    next.skins.reserve(next.skins.size() + addition.skins.size());
    next.materials.reserve(next.materials.size() + addition.materials.size());
    next.animations.reserve(next.animations.size() + addition.animations.size());
    Range range{nodeOffset, nodeOffset + static_cast<uint32_t>(addition.nodes.size()), materialOffset};
    ranges_.emplace(key, range);
    const auto append = [](auto& target, auto& values) {
        static_assert(std::is_nothrow_move_constructible_v<typename std::decay_t<decltype(target)>::value_type>);
        target.insert(target.end(), std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
    };
    append(next.nodes, addition.nodes); append(next.meshes, addition.meshes); append(next.skins, addition.skins);
    append(next.materials, addition.materials); append(next.animations, addition.animations);
    ++revision_;
    return range;
}
Result<ModelAssetLibrary::Range> ModelAssetLibrary::Resolve(PrimitiveShape shape, const std::string& mesh, const std::string& texture) {
    if(!IsValidPrimitiveShape(shape)) return std::unexpected(Invalid("Invalid primitive shape."));
    if(shape == PrimitiveShape::Asset) return Resolve(mesh, texture);
    const auto key = MeshKey(shape, mesh);
    if(!models_.contains(key)) {
        auto generated = PrimitiveMeshBuilder::Build(shape);
        if(!generated) return std::unexpected(generated.error());
        models_.emplace(key, std::make_shared<ModelAssetData>(std::move(*generated)));
    }
    return Resolve(key, texture);
}
}
