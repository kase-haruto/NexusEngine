#include "Graphics/Model/ModelLoader.h"
#include "Graphics/Model/ModelAssetLibrary.h"
#include "Graphics/Model/ModelInstance.h"
#include "Graphics/Renderer/RenderScene.h"
#include "ThirdParty/nlohmann/json.hpp"
#include "Runtime/Animation/AnimationDocument.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace NexusEngine::Tests {
namespace {
void ExpectAnimation(bool condition, const char* message) {
    if(!condition) {
        throw std::runtime_error(message);
    }
}
template<class T> void RequireAnimation(const Result<T>& result) {
    if(!result) {
        throw std::runtime_error(result.error().GetMessageText());
    }
}
}
void RunAnimationAssetTests(const char* externalModel) {
    const auto folder = std::filesystem::path("Generated/AnimationAssetTests");
    std::filesystem::create_directories(folder);
    const auto original = std::filesystem::path("Project/Resources/Assets/Models/SimpleSkin");
    for(const auto& file : std::filesystem::directory_iterator(original)) {
        if(file.path().extension() == ".bin") {
            std::filesystem::copy_file(file.path(), folder / file.path().filename(), std::filesystem::copy_options::overwrite_existing);
        }
    }
    std::ifstream input(original / "SimpleSkin.gltf");
    auto json = nlohmann::json::parse(input);
    for(std::size_t index = 0; index < json["nodes"].size(); ++index) {
        json["nodes"][index]["name"] = "Node" + std::to_string(index);
    }
    json["animations"][0]["name"] = "Idle";
    auto second = json["animations"][0];
    second["name"] = "Walk";
    json["animations"].push_back(second);
    // 有効なモーフtrackを追加し、全モデルが拒否されていたケースを再現する。
    json["meshes"][0]["primitives"][0]["targets"] = {{{"POSITION", 1}}};
    json["meshes"][0]["weights"] = {0};
    nlohmann::json morph = {
        {"name", "Expression"},
        {"channels", {{{"sampler", 0}, {"target", {{"node", 0}, {"path", "weights"}}}}}},
        {"samplers", {{{"input", 5}, {"output", 5}}}}
    };
    json["animations"].push_back(morph);
    {
        std::ofstream output(folder / "multi.gltf");
        output << json.dump(2);
    }
    auto loaded = ModelLoader{}.LoadGltf(folder / "multi.gltf");
    RequireAnimation(loaded);
    ExpectAnimation(loaded->animations.size() == 3, "Clip order/count changed.");
    ExpectAnimation(loaded->animations[2].hasMorphAnimation && loaded->animations[2].duration == 5.5f,
        "Morph-only clip lost metadata/duration.");
    {
        std::ofstream output(folder / "walk.nxanim");
        output << R"({"version":1,"source":"multi.gltf","clip":"Walk"})";
    }
    ModelAssetLibrary library(folder, ModelAssetData{});
    auto animation = library.Animation("walk.nxanim");
    RequireAnimation(animation);
    auto same = library.Animation("walk.nxanim");
    RequireAnimation(same);
    ExpectAnimation(*same == *animation && (*animation)->clip.name == "Walk", "Animation asset cache failed.");
    ModelAssetLibrary::Request request{PrimitiveShape::Asset, "multi.gltf", "", "walk.nxanim", "multi.gltf#animation/Idle"};
    library.Retain(std::span(&request, 1));
    auto range = library.Resolve(request);
    RequireAnimation(range);
    ExpectAnimation(library.MatchesRequests(std::span(&request, 1)), "Prepared animation request did not match.");
    PrimitiveRenderItem item;
    item.animation = PrimitiveRenderItem::AnimationSample{};
    item.animation->asset = request.animationAsset;
    item.animation->blendAsset = request.blendAnimationAsset;
    item.animation->blendWeight = .5f;
    range->ResolveAnimation(item);
    ExpectAnimation(item.animation.has_value(), "Asset binding failed.");
    ModelInstance bound;
    RequireAnimation(bound.Initialize(library.Snapshot()));
    RequireAnimation(bound.SampleBlend(item.animation->index, 1, true, item.animation->blendIndex, 2, false, .5f));
    ModelInstance a, b, mixed;
    RequireAnimation(a.Initialize(*loaded));
    RequireAnimation(b.Initialize(*loaded));
    RequireAnimation(mixed.Initialize(*loaded));
    RequireAnimation(a.Sample(0, 0, false));
    RequireAnimation(b.Sample(1, 2, false));
    for(float weight : {0.0f, .5f, 1.0f}) {
        RequireAnimation(mixed.SampleBlend(0, 0, false, 1, 2, false, weight));
        const auto& actual = mixed.GetNodeWorldTransforms();
        const auto& expected = weight == 0 ? a.GetNodeWorldTransforms() : b.GetNodeWorldTransforms();
        for(std::size_t node = 0; node < actual.size(); ++node) {
            for(std::size_t row = 0; row < 4; ++row) {
                for(std::size_t col = 0; col < 4; ++col) {
                    ExpectAnimation(std::isfinite(actual[node].At(row, col)), "Blend produced nonfinite pose.");
                    if(weight != .5f) {
                        ExpectAnimation(std::abs(actual[node].At(row, col) - expected[node].At(row, col)) < .0001f, "Blend endpoint mismatch.");
                    }
                }
            }
        }
    }
    // 片側だけにtrackがある場合もbind poseから補間し、前フレームの値を残さない。
    ModelAssetData numeric;
    ModelNodeAssetData node;
    node.name = "Root";
    node.localTransform.translation = {2, 0, 0};
    numeric.nodes.push_back(node);
    AnimationClip left, right;
    NodeAnimationChannel translation;
    translation.nodeIndex = 0;
    translation.translations.push_back({0, {10, 0, 0}, {}, {}});
    left.channels.push_back(translation);
    NodeAnimationChannel rotation;
    rotation.nodeIndex = 0;
    rotation.rotations.push_back({0, {0, 0, 1, 0}, {}, {}});
    right.channels.push_back(rotation);
    numeric.animations = {left, right};
    ModelInstance numericPose;
    RequireAnimation(numericPose.Initialize(numeric));
    RequireAnimation(numericPose.SampleBlend(0, 0, true, 1, 0, true, .5f));
    ExpectAnimation(std::abs(numericPose.GetNodeWorldTransforms()[0].At(3,0) - 6) < .0001f, "Missing-track fallback failed.");
    ExpectAnimation(std::abs(numericPose.GetNodeWorldTransforms()[0].At(0,0)) < .0001f, "Quaternion midpoint is not a 90-degree rotation.");
    const auto before = mixed.GetNodeWorldTransforms();
    ExpectAnimation(!mixed.SampleBlend(0, 0, true, 99, 0, true, .5f), "Invalid blend accepted.");
    ExpectAnimation(mixed.GetNodeWorldTransforms()[0].At(0,0) == before[0].At(0,0), "Failed sample mutated pose.");
    AnimationClipDocument editable;
    editable.duration = 1;
    AnimationTrack bone;
    bone.component = "$node";
    bone.property = "translation";
    bone.target = (*animation)->nodePaths.at((*animation)->clip.channels.front().nodeIndex);
    bone.keys = {{0,{9,0,0},{},{}},{1,{9,0,0},{},{}}};
    editable.tracks.push_back(bone);
    RequireAnimation(SaveAnimationDocument(folder/"editable.nxanim",editable));
    auto authored = library.Animation("editable.nxanim");
    RequireAnimation(authored);
    ExpectAnimation((*authored)->clip.channels[0].translations[0].value.x == 9,"Authored bone keys were not imported.");
    auto layeredRequest = request;
    layeredRequest.layers = {"editable.nxanim", "walk.nxanim"};
    auto layeredRange = library.Resolve(layeredRequest);
    RequireAnimation(layeredRange);
    PrimitiveRenderItem layered;
    layered.animation = PrimitiveRenderItem::AnimationSample{};
    layered.animation->layers = {{"editable.nxanim",0,.5f,.5f,false},{"walk.nxanim",0,.5f,.5f,false}};
    layeredRange->ResolveAnimation(layered);
    ExpectAnimation(layered.animation.has_value(),"Layer references were not resolved.");
    ModelInstance layeredPose;
    RequireAnimation(layeredPose.Initialize(library.Snapshot()));
    RequireAnimation(layeredPose.SampleLayers(layered.animation->layers));
    // 別モデルに属するアセットを、対応する階層を持つモデルへ再バインドする。
    json["animations"] = nlohmann::json::array();
    {
        std::ofstream output(folder / "target.gltf");
        output << json.dump(2);
    }
    auto target = library.Resolve(ModelAssetLibrary::Request{PrimitiveShape::Asset, "target.gltf", "", "walk.nxanim", ""});
    RequireAnimation(target);
    json["nodes"][2]["name"] = "Incompatible";
    {
        std::ofstream output(folder / "wrong.gltf");
        output << json.dump(2);
    }
    ExpectAnimation(!library.Resolve(ModelAssetLibrary::Request{PrimitiveShape::Asset, "wrong.gltf", "", "walk.nxanim", ""}), "Incompatible skeleton accepted.");
    // 描画Meshを取り除いた、アニメーション専用glTFをアセットの依存先として使う。
    json["nodes"][2]["name"] = "Node2";
    json["animations"] = nlohmann::json::array({second});
    for(auto& sourceNode : json["nodes"]) {
        sourceNode.erase("mesh");
        sourceNode.erase("skin");
    }
    json.erase("meshes");
    json.erase("skins");
    {
        std::ofstream output(folder / "animation-only.gltf");
        output << json.dump(2);
    }
    auto standalone = library.Animation("animation-only.gltf#animation/Walk");
    RequireAnimation(standalone);
    ExpectAnimation(!(*standalone)->clip.channels.empty(), "Animation-only glTF lost its channels.");
    if(externalModel) {
        auto external = ModelLoader{}.LoadGltf(std::filesystem::path(externalModel));
        RequireAnimation(external);
        ModelInstance pose;
        RequireAnimation(pose.Initialize(*external));
        for(uint32_t index = 0; index < external->animations.size(); ++index) {
            RequireAnimation(pose.Sample(index, .25f));
        }
        std::cout << "PASS: actual model loaded and sampled " << external->animations.size() << " clips\n";
    }
    std::cout << "PASS: multi-clip/morph import, animation assets, skeleton binding and blending\n";
}
}
