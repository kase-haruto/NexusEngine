#include "GameApplication.h"
#include <exception>

namespace NexusEngine {
Result<void> GameApplication::Load(const std::filesystem::path& path, const std::filesystem::path& root,
                                   ModelAssetLibrary::ReloadStartup loadStartup) {
    if(runtime_.IsRunning() || library_)
        return std::unexpected(Error(ErrorCategory::Framework, 46, "Game application is already loaded."));
    auto level = std::make_unique<Level>();
    auto result = level->Load(path);
    if(!result) return result;
    result = runtime_.Start(std::move(level));
    if(!result) return result;
    // 以下の失敗でもSystemとWorldを破棄し、再試行可能な状態へ戻す。
    auto prepare = [&]() -> Result<void> {
        auto& active = *runtime_.GetLevel();
        const auto lighting = active.Lighting().value_or(LevelLightingSettings{});
        renderScene_.ambientIrradiance = lighting.skyIrradiance;
        renderScene_.environment = lighting.environment;
        renderScene_.globalIllumination.enabled = false;
        renderScene_.bakedLighting.enabled = false;
        renderScene_.ddgi.enabled = false;
        auto extracted = extractor_.ExtractInto(*active.GetScene(), 16.0f / 9.0f, renderScene_);
        if(!extracted) return extracted;
        if(!renderScene_.camera)
            return std::unexpected(Error(ErrorCategory::Scene, 47, "Game level requires an enabled primary camera."));
        auto startup = std::make_shared<ModelAssetData>();
        for(const auto& item : renderScene_.primitives) {
            if(item.shape != PrimitiveShape::Asset || item.meshAsset != kStartupModelAssetReference) continue;
            if(!loadStartup)
                return std::unexpected(Error(ErrorCategory::Resource, 49, "Level uses :startup without a startup asset loader."));
            auto loaded = loadStartup();
            if(!loaded) return std::unexpected(std::move(loaded.error()));
            *startup = std::move(*loaded);
            break;
        }
        library_ = std::make_unique<ModelAssetLibrary>(std::move(startup), root);
        for(const auto& item : renderScene_.primitives) {
            if(item.shape == PrimitiveShape::Asset && item.meshAsset.empty()) continue;
            auto range = library_->Resolve(item.shape, item.meshAsset, item.baseColorTexture);
            if(!range) return std::unexpected(std::move(range.error()));
        }
        asset_ = library_->SharedSnapshot();
        auto initialized = pose_.Initialize(*asset_);
        if(!initialized) return initialized;
        if(!asset_->animations.empty()) {
            auto play = pose_.Play(0, true);
            if(!play) return play;
        }
        return ResolveDrawRanges();
    };
    try {
        result = prepare();
    } catch(const std::exception& exception) {
        result = std::unexpected(Error(ErrorCategory::Resource, 50, std::string("Game asset preparation failed: ") + exception.what()));
    } catch(...) {
        result = std::unexpected(Error(ErrorCategory::Resource, 50, "Game asset preparation failed with an unknown exception."));
    }
    if(!result) {
        runtime_.Stop(); pose_.Shutdown(); asset_.reset(); library_.reset(); renderScene_ = RenderScene{};
    }
    return result;
}

Result<void> GameApplication::Update(float deltaTime) {
    auto result = runtime_.Update(deltaTime);
    if(result && runtime_.IsRunning() && !runtime_.IsPaused()) pose_.Update(deltaTime);
    return result;
}

Result<void> GameApplication::PrepareRender(uint32_t width, uint32_t height) {
    auto* level = runtime_.GetLevel();
    if(!level || !level->GetScene()) {
        renderScene_.camera.reset(); renderScene_.primitives.clear();
        return {};
    }
    if(width == 0 || height == 0) return {};
    auto result = extractor_.ExtractInto(*level->GetScene(), static_cast<float>(width) / static_cast<float>(height), renderScene_);
    if(!result) return result;
    return ResolveDrawRanges();
}

Result<void> GameApplication::ResolveDrawRanges() {
    for(auto& item : renderScene_.primitives) {
        if(item.shape == PrimitiveShape::Asset && item.meshAsset.empty()) { item.nodeEnd = 0; continue; }
        const auto range = library_->FindRange(item.shape, item.meshAsset, item.baseColorTexture);
        if(!range)
            return std::unexpected(Error(ErrorCategory::Resource, 48,
                "Game requested an asset not prepared at startup: " + item.meshAsset + " / " + item.baseColorTexture));
        item.nodeBegin = range->begin; item.nodeEnd = range->end; item.materialBegin = range->materialBegin;
        if(item.shape != PrimitiveShape::Asset) item.nodeIndex = PrimitiveRenderItem::kAllNodes;
        if(item.nodeIndex != PrimitiveRenderItem::kAllNodes) {
            if(item.nodeIndex >= range->end - range->begin) item.nodeEnd = 0;
            else item.nodeIndex += range->begin;
        }
    }
    return {};
}
}
