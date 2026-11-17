#include "ModelAssetLibrary.h"
#include "ModelLoader.h"
#include "Graphics/Renderer/RenderScene.h"
#include "PrimitiveMeshBuilder.h"
#include "Graphics/Resource/WicImageDecoder.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <set>
#include <type_traits>
#include "ThirdParty/nlohmann/json.hpp"
#include "Runtime/Animation/AnimationDocument.h"

namespace NexusEngine {
namespace {
std::vector<std::string> AnimationNodePaths(const ModelAssetData& model) {
    std::vector<std::string> paths;
    paths.reserve(model.nodes.size());
    for(const auto& node : model.nodes) {
        std::string path = node.parentIndex == ModelNodeAssetData::kNoParent ? "" : paths.at(node.parentIndex);
        path += std::to_string(node.name.size()) + ":" + node.name + "/";
        paths.push_back(std::move(path));
    }
    return paths;
}
Error Invalid(const char* message);
std::filesystem::path Path(const std::string& text);
Result<std::shared_ptr<const AnimationAsset>> AuthoredAnimation(const std::string& reference, const AnimationClipDocument& definition) {
            auto asset = std::make_shared<AnimationAsset>();
            asset->reference = reference;
            asset->clip.name = Path(reference).stem().string();
            asset->clip.duration = definition.duration;
            // $node trackのtargetはインポート時の階層パス。汎用Property trackはRuntimeが処理する。
            for(const auto& track : definition.tracks) {
                if(track.component != "$node") {
                    continue;
                }
                auto found = std::find(asset->nodePaths.begin(), asset->nodePaths.end(), track.target);
                uint32_t node = static_cast<uint32_t>(found - asset->nodePaths.begin());
                if(found == asset->nodePaths.end()) {
                    asset->nodePaths.push_back(track.target);
                    asset->clip.channels.push_back({node});
                }
                auto& channel = asset->clip.channels[node];
                for(const auto& key : track.keys) {
                    const auto vector = [](const std::vector<float>& value) {
                        return value.size() >= 3 ? Vector3{value[0], value[1], value[2]} : Vector3{};
                    };
                    if(track.property == "rotation" && key.value.size() == 4) {
                        QuaternionKeyframe frame;
                        frame.time = key.time;
                        frame.value = Quaternion{key.value[0], key.value[1], key.value[2], key.value[3]}.Normalized();
                        if(key.inTangent.size() == 4) {
                            frame.inTangent = {key.inTangent[0], key.inTangent[1], key.inTangent[2], key.inTangent[3]};
                        }
                        if(key.outTangent.size() == 4) {
                            frame.outTangent = {key.outTangent[0], key.outTangent[1], key.outTangent[2], key.outTangent[3]};
                        }
                        channel.rotationInterpolation = track.interpolation;
                        channel.rotations.push_back(frame);
                    } else if(track.property == "translation" && key.value.size() == 3) {
                        channel.translationInterpolation = track.interpolation;
                        channel.translations.push_back({key.time, vector(key.value), vector(key.inTangent), vector(key.outTangent)});
                    } else if(track.property == "scale" && key.value.size() == 3) {
                        channel.scaleInterpolation = track.interpolation;
                        channel.scales.push_back({key.time, vector(key.value), vector(key.inTangent), vector(key.outTangent)});
                    } else {
                        return std::unexpected(Invalid("Unsupported node animation property or dimension."));
                    }
                }
            }
            std::shared_ptr<const AnimationAsset> immutable = std::move(asset);
            return immutable;
}
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
    if(!aggregate_->nodes.empty() || !reloadStartup_) {
        std::promise<ModelResult> initialModel;
        initialModel.set_value(aggregate_);
        sources_->models.emplace(kStartupModelAssetReference, initialModel.get_future().share());
    }
    ranges_[{kStartupModelAssetReference, ""}] = {0, static_cast<uint32_t>(aggregate_->nodes.size()), 0, 0, static_cast<uint32_t>(aggregate_->animations.size())};
}
ModelAssetLibrary::Request ModelAssetLibrary::Request::FromItem(const PrimitiveRenderItem& item) {
    Request request{item.shape, item.meshAsset, item.baseColorTexture};
    if(item.animation && item.shape == PrimitiveShape::Asset) {
        if(item.animation->layers.empty()) {
            request.animationAsset = item.animation->asset;
            request.blendAnimationAsset = item.animation->blendAsset;
        }
        for(const auto& layer : item.animation->layers) {
            if(!layer.asset.empty()) {
                request.layers.push_back(layer.asset);
            }
        }
        std::ranges::sort(request.layers);
        request.layers.erase(std::unique(request.layers.begin(), request.layers.end()), request.layers.end());
    }
    return request;
}
void ModelAssetLibrary::Range::ResolveAnimation(PrimitiveRenderItem& item) const {
    if(!item.animation) {
        return;
    }
    if(item.shape != PrimitiveShape::Asset) {
        item.animation.reset();
        return;
    }
    auto& sample = *item.animation;
    // Asset参照を実行時indexへ変換する。毎フレームの描画処理ではI/Oを行わない。
    const auto resolve = [&](const std::string& asset, uint32_t& index) {
        if(!asset.empty()) {
            const auto found = animationAssets.find(asset);
            if(found == animationAssets.end()) {
                return false;
            }
            index = found->second;
            return true;
        }
        if(index >= animationCount) {
            return false;
        }
        index += animationBegin;
        return true;
    };
    if(!sample.layers.empty()) {
        for(auto& layer : sample.layers) {
            if(!resolve(layer.asset, layer.index)) {
                item.animation.reset();
                return;
            }
        }
        return;
    }
    if(item.shape != PrimitiveShape::Asset || !resolve(sample.asset, sample.index) ||
        (sample.blendWeight > 0 && !resolve(sample.blendAsset, sample.blendIndex))) {
        item.animation.reset();
    }
}
void ModelAssetLibrary::Retain(std::span<const Request> requests) {
    std::set<std::pair<std::string, std::string>> active;
    std::set<std::string> textures;
    for(const auto& request : requests) {
        if(!IsValidPrimitiveShape(request.shape)) continue;
        auto mesh = MeshKey(request.shape, request.mesh);
        if(mesh.empty()) continue;
        active.emplace(mesh, request.texture);
        if(!request.texture.empty()) textures.insert(request.texture);
    }
    if(std::ranges::any_of(ranges_, [&](const auto& entry) { return !active.contains(entry.first); })) {
        // Rebuild ranges before the caller resolves them; offsets cannot be compacted mid-frame.
        aggregate_ = std::make_shared<ModelAssetData>();
        ranges_.clear();
        ++revision_;
    }
    // Only upload variants are compacted. Immutable sources survive detach/reattach.
    std::erase_if(textures_, [&](const auto& entry) { return !textures.contains(entry.first); });
    std::erase_if(textureIds_, [&](const auto& entry) { return !textures.contains(entry.first); });
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
bool ModelAssetLibrary::MatchesRequests(std::span<const Request> requests) const {
    std::set<std::pair<std::string, std::string>> keys;
    for(const auto& request : requests) {
        if(!IsValidPrimitiveShape(request.shape)) return false;
        const auto mesh = MeshKey(request.shape, request.mesh);
        if(!mesh.empty()) keys.emplace(mesh, request.texture);
    }
    for(const auto& request : requests) {
        const auto found = ranges_.find({MeshKey(request.shape, request.mesh), request.texture});
        if(found == ranges_.end()) {
            continue;
        }
        for(const auto& layer : request.layers) {
            if(!found->second.animationAssets.contains(layer)) {
                return false;
            }
        }
        if((!request.animationAsset.empty() && !found->second.animationAssets.contains(request.animationAsset)) ||
            (!request.blendAnimationAsset.empty() && !found->second.animationAssets.contains(request.blendAnimationAsset))) {
            return false;
        }
    }
    if(keys.size() != ranges_.size()) return false;
    for(const auto& key : keys) if(!ranges_.contains(key)) return false;
    return true;
}
Result<const ModelAssetData*> ModelAssetLibrary::Model(const std::string& reference) {
    if(reference.empty()) return std::unexpected(Invalid("No mesh is assigned."));
    return CachedModel(reference, [&]() -> Result<ModelAssetData> {
        if(reference == kStartupModelAssetReference && reloadStartup_) return reloadStartup_();
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
        return std::move(*loaded);
    });
}
void ModelAssetLibrary::InvalidateAnimations() {
    animations_.clear();
    animationOverrides_.clear();
    aggregate_ = std::make_shared<ModelAssetData>();
    ranges_.clear();
    ++revision_;
}
Result<void> ModelAssetLibrary::PreviewAnimation(const std::string& reference, const AnimationClipDocument& definition) {
    Result<std::shared_ptr<const AnimationAsset>> asset;
    if(definition.source.empty()) {
        asset = AuthoredAnimation(reference, definition);
    } else {
        asset = Animation(definition.source + "#animation/" + definition.clip);
    }
    if(!asset) {
        return std::unexpected(asset.error());
    }
    animationOverrides_[reference] = *asset;
    aggregate_ = std::make_shared<ModelAssetData>();
    ranges_.clear();
    ++revision_;
    return {};
}
Result<std::shared_ptr<const AnimationAsset>> ModelAssetLibrary::Animation(const std::string& reference) {
    if(const auto preview = animationOverrides_.find(reference); preview != animationOverrides_.end()) {
        return preview->second;
    }
    if(const auto found = animations_.find(reference); found != animations_.end()) {
        return found->second;
    }
    std::string source;
    std::string clipName;
    const auto separator = reference.find("#animation/");
    if(separator != std::string::npos) {
        source = reference.substr(0, separator);
        clipName = reference.substr(separator + 11);
    } else {
        auto path = Reference(Path(reference));
        if(!path) {
            return std::unexpected(path.error());
        }
        if(Path(*path).extension() != ".nxanim") {
            return std::unexpected(Invalid("Animation assets require .nxanim or model#animation/clip references."));
        }
        auto definition = LoadAnimationClipDocument(root_ / Path(*path));
        if(!definition) {
            return std::unexpected(definition.error());
        }
        if(definition->source.empty()) {
            auto built = AuthoredAnimation(reference, *definition);
            if(built) {
                animations_.emplace(reference, *built);
            }
            return built;
        }
        source = definition->source;
        clipName = definition->clip;
    }
    auto model = Model(source);
    if(!model) {
        // Meshを持たないアニメーション専用glTFも、描画モデルとは別に取り込む。
        model = CachedModel(":animation/" + source, [&]() -> Result<ModelAssetData> {
            auto canonical = Reference(Path(source));
            if(!canonical) {
                return std::unexpected(canonical.error());
            }
            return ModelLoader{}.LoadGltf(root_ / Path(*canonical), false);
        });
        if(!model) {
            return std::unexpected(model.error());
        }
    }
    const AnimationClip* selected = nullptr;
    for(const auto& clip : (*model)->animations) {
        if(clip.name == clipName) {
            if(selected) {
                return std::unexpected(Invalid("Animation clip name is ambiguous; give source clips unique names."));
            }
            selected = &clip;
        }
    }
    if(!selected) {
        return std::unexpected(Invalid("Animation asset clip was not found in its source."));
    }
    auto asset = std::make_shared<AnimationAsset>();
    asset->reference = reference;
    asset->source = source;
    asset->clip = *selected;
    asset->nodePaths = AnimationNodePaths(**model);
    std::shared_ptr<const AnimationAsset> immutable = std::move(asset);
    animations_.emplace(reference, immutable);
    return immutable;
}
Result<ModelAssetLibrary::Range> ModelAssetLibrary::Resolve(const Request& request) {
    auto range = Resolve(request.shape, request.mesh, request.texture);
    if(!range) {
        return range;
    }
    if(request.animationAsset.empty() && request.blendAnimationAsset.empty() && request.layers.empty()) {
        return range;
    }
    if(request.shape != PrimitiveShape::Asset || request.mesh.empty()) {
        return std::unexpected(Invalid("Animation assets require a model."));
    }
    auto target = Model(request.mesh);
    if(!target) {
        return std::unexpected(target.error());
    }
    const auto paths = AnimationNodePaths(**target);
    auto references = request.layers;
    references.push_back(request.animationAsset);
    references.push_back(request.blendAnimationAsset);
    for(const auto& reference : references) {
        if(reference.empty() || range->animationAssets.contains(reference)) {
            continue;
        }
        auto asset = Animation(reference);
        if(!asset) {
            return std::unexpected(asset.error());
        }
        AnimationClip bound = (*asset)->clip;
        // アセットのnode indexは元モデル固有なので、階層パスで描画対象へ再バインドする。
        // 曖昧な同名階層や互換性のない骨格はエラーとし、無関係なnodeを動かさない。
        for(auto& channel : bound.channels) {
            if(channel.nodeIndex >= (*asset)->nodePaths.size()) {
                return std::unexpected(Invalid("Animation asset node is invalid."));
            }
            const auto& path = (*asset)->nodePaths[channel.nodeIndex];
            const auto found = std::find(paths.begin(), paths.end(), path);
            if(found == paths.end() || std::find(found + 1, paths.end(), path) != paths.end() ||
                std::count((*asset)->nodePaths.begin(), (*asset)->nodePaths.end(), path) != 1) {
                return std::unexpected(Invalid("Animation asset skeleton does not match the target model."));
            }
            channel.nodeIndex = range->begin + static_cast<uint32_t>(found - paths.begin());
        }
        if(aggregate_.use_count() != 1) {
            aggregate_ = std::make_shared<ModelAssetData>(*aggregate_);
        }
        const auto index = static_cast<uint32_t>(aggregate_->animations.size());
        aggregate_->animations.push_back(std::move(bound));
        range->animationAssets.emplace(reference, index);
        ranges_.at({request.mesh, request.texture}) = *range;
        ++revision_;
    }
    return range;
}
Result<const ModelAssetData*> ModelAssetLibrary::CachedModel(const std::string& key, const ReloadStartup& load) {
    std::promise<ModelResult> promise;
    std::shared_future<ModelResult> future;
    bool producer = false;
    {
        std::lock_guard lock(sources_->mutex);
        auto [entry, inserted] = sources_->models.try_emplace(key);
        if(inserted) entry->second = promise.get_future().share();
        future = entry->second;
        producer = inserted;
    }
    if(producer) {
        // No cache lock during file I/O, parsing or a wait on another worker.
        ModelResult result;
        try {
            auto loaded = load();
            if(loaded) result = std::make_shared<ModelAssetData>(std::move(*loaded));
            else result = std::unexpected(std::move(loaded.error()));
        } catch(const std::exception& exception) {
            result = std::unexpected(Error(ErrorCategory::Resource, 131, exception.what()));
        } catch(...) {
            result = std::unexpected(Invalid("Model loading failed with an unknown exception."));
        }
        const bool success = result.has_value();
        promise.set_value(std::move(result));
        if(!success) {
            // Existing waiters see the failure; a later request can retry.
            std::lock_guard lock(sources_->mutex);
            sources_->models.erase(key);
        }
    }
    const auto& result = future.get();
    if(!result) return std::unexpected(result.error());
    return result->get();
}
const ModelAssetData* ModelAssetLibrary::FindModel(const std::string& reference) const {
    std::shared_future<ModelResult> future;
    {
        std::lock_guard lock(sources_->mutex);
        auto found = sources_->models.find(reference);
        if(found == sources_->models.end()) return nullptr;
        future = found->second;
    }
    if(future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    const auto& result = future.get();
    return result ? result->get() : nullptr;
}
std::optional<ModelAssetLibrary::Range> ModelAssetLibrary::FindRange(PrimitiveShape shape, const std::string& mesh, const std::string& texture) const {
    if(!IsValidPrimitiveShape(shape)) return std::nullopt;
    const auto key = MeshKey(shape, mesh);
    if(key.empty()) return Range{};
    auto found = ranges_.find({key, texture});
    return found == ranges_.end() ? std::nullopt : std::optional{found->second};
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
    textureIds_.emplace(reference, NextModelResourceIdentity());
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
        for(auto& material : addition.materials) {
            material.baseColorImageData = **image;
            material.baseColorImageId = textureIds_.at(texture);
        }
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
    Range range{nodeOffset, nodeOffset + static_cast<uint32_t>(addition.nodes.size()), materialOffset,
        static_cast<uint32_t>(next.animations.size()), static_cast<uint32_t>(addition.animations.size())};
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
    auto generated = CachedModel(key, [shape]() { return PrimitiveMeshBuilder::Build(shape); });
    if(!generated) return std::unexpected(generated.error());
    return Resolve(key, texture);
}
}
