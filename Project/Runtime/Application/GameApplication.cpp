#include "GameApplication.h"
#include <exception>

namespace NexusEngine {
Result<void> GameApplication::Load(const std::filesystem::path& path, const std::filesystem::path& root,
                                   ModelAssetLibrary::ReloadStartup loadStartup) {
    if(runtime_.IsRunning() || library_)
        return std::unexpected(Error(ErrorCategory::Framework, 46, "Game application is already loaded."));
    auto level = std::make_unique<Level>("Level", reflection_);
    auto resourceRoot=level->SetResourceRoot(paths_.projectRoot.empty() ? std::filesystem::absolute(root) : paths_.projectRoot);
    if(!resourceRoot) return resourceRoot;
    auto result = level->Load(path);
    if(!result) return result;
    // Script OnCreateのMesh queryより先に同じCPU asset cacheを用意し、描画側も再利用する。
    try {
        library_=std::make_unique<ModelAssetLibrary>(std::make_shared<ModelAssetData>(),root,loadStartup);
        library_->Retain({});
    } catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::Resource,50,exception.what())); }
    if(!level->SetPhysicsModelSource([this](std::string_view reference) { return library_->Model(std::string(reference)); })) {
        library_.reset(); return std::unexpected(Error(ErrorCategory::Resource,260,"Cannot configure collision model assets."));
    }
    result = runtime_.Start(std::move(level));
    if(!result) { library_.reset(); return result; }
    // 以下の失敗でもSystemとWorldを破棄し、再試行可能な状態へ戻す。
    auto prepare = [&]() -> Result<void> {
        auto& active = *runtime_.GetLevel();
        const auto lighting = active.Lighting().value_or(LevelLightingSettings{});
        renderScene_.ambientIrradiance = lighting.skyIrradiance;
        renderScene_.environment = lighting.environment;
        if(!paths_.projectRoot.empty() && !renderScene_.environment.imagePath.empty()) {
            auto image = ResolveProjectPath(paths_.projectRoot, renderScene_.environment.imagePath);
            if(!image) return std::unexpected(image.error());
            renderScene_.environment.imagePath = *image;
        }
        renderScene_.globalIllumination.enabled = false;
        renderScene_.bakedLighting.enabled = false;
        renderScene_.ddgi.enabled = false;
        auto extracted = extractor_.ExtractInto(*active.GetScene(), 16.0f / 9.0f, renderScene_);
        if(!extracted) return extracted;
        if(!renderScene_.camera)
            return std::unexpected(Error(ErrorCategory::Scene, 47, "Game level requires an enabled primary camera."));
        for(const auto& item : renderScene_.primitives) {
            if(item.shape != PrimitiveShape::Asset || item.meshAsset != kStartupModelAssetReference) continue;
            if(!loadStartup)
                return std::unexpected(Error(ErrorCategory::Resource, 49, "Level uses :startup without a startup asset loader."));
            break;
        }
        for(const auto& item : renderScene_.primitives) {
            if(item.shape == PrimitiveShape::Asset && item.meshAsset.empty()) continue;
            auto range = library_->Resolve(item.shape, item.meshAsset, item.baseColorTexture);
            if(!range) return std::unexpected(std::move(range.error()));
        }
        asset_ = library_->SharedSnapshot();
        auto initialized = pose_.Initialize(*asset_);
        if(!initialized) return initialized;
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
    return result;
}

Result<void> GameApplication::PrepareRender(uint32_t width, uint32_t height) {
    auto* level = runtime_.GetLevel();
    if(!level || !level->GetScene()) {
        renderScene_.camera.reset(); renderScene_.primitives.clear();
        return {};
    }
    if(width == 0 || height == 0) return {};
    const auto lighting=level->Lighting().value_or(LevelLightingSettings{});
    renderScene_.ambientIrradiance=lighting.skyIrradiance; renderScene_.environment=lighting.environment;
    if(!renderScene_.environment.imagePath.empty() && !paths_.projectRoot.empty()) {
        auto image=ResolveProjectPath(paths_.projectRoot,renderScene_.environment.imagePath); if(!image) return std::unexpected(image.error());
        renderScene_.environment.imagePath=*image;
    }
    auto result = extractor_.ExtractInto(*level->GetScene(), static_cast<float>(width) / static_cast<float>(height), renderScene_);
    renderScene_.screenEffects = level->ScreenEffects();
    for(auto& effect : renderScene_.screenEffects)
        if(effect.shaderPath.is_relative() && !paths_.projectRoot.empty()) effect.shaderPath = paths_.projectRoot / effect.shaderPath;
    if(!result) return result;
    result=PrepareRequestedAssets(); if(!result) return result;
    return ResolveDrawRanges();
}

Result<void> GameApplication::AttachGraphics(GraphicsSystem& graphics,IGraphicsRenderer* renderer) {
    if(!renderer) return std::unexpected(Error(ErrorCategory::Resource,48,"GameApplication requires a Renderer."));
    graphics_=&graphics; renderer_=renderer; return {};
}
Result<void> GameApplication::PrepareRequestedAssets() {
    std::vector<ModelAssetLibrary::Request> requests;
    for(const auto& item:renderScene_.primitives) {
        if(item.shape==PrimitiveShape::Asset && item.meshAsset.empty()) continue;
        requests.push_back({item.shape,item.shape==PrimitiveShape::Asset ? item.meshAsset : std::string{},item.baseColorTexture});
    }
    if(library_->MatchesRequests(requests)) return {};
    // 既存libraryの共有source cacheとcopy-on-writeを利用し、現行snapshotを変更しない。
    auto candidate=std::make_unique<ModelAssetLibrary>(*library_); candidate->Retain(requests);
    for(const auto& request:requests) { auto range=candidate->Resolve(request.shape,request.mesh,request.texture); if(!range) return std::unexpected(range.error()); }
    const auto next=candidate->SharedSnapshot();
    if(next==asset_) { library_=std::move(candidate); return {}; }
    ModelInstance pose; auto initialized=pose.Initialize(*next); if(!initialized) return initialized;
    const auto previous=asset_;
    bool committed=false;
    const auto commit=[&] { if(committed) return; pose_=std::move(pose); asset_=next; library_=std::move(candidate); committed=true; };
    if(graphics_ && renderer_) {
        if(!assetPublisher_) return std::unexpected(Error(ErrorCategory::Resource,48,"Host did not bind dynamic asset publication."));
        auto published=assetPublisher_(*graphics_,*renderer_,next,pose,commit); if(!published) return published;
        if(!committed) return std::unexpected(Error(ErrorCategory::Resource,48,"Host did not commit prepared assets."));
    } else commit();
    ++renderScene_.ddgi.revision;
    return {};
}

Result<void> GameApplication::ResolveDrawRanges() {
    for(auto& item : renderScene_.primitives) {
        if(item.shape == PrimitiveShape::Asset && item.meshAsset.empty()) { item.nodeEnd = 0; continue; }
        const auto range = library_->FindRange(item.shape, item.meshAsset, item.baseColorTexture);
        if(!range)
            return std::unexpected(Error(ErrorCategory::Resource, 48,
                "Game requested an asset not prepared at startup: " + item.meshAsset + " / " + item.baseColorTexture));
        item.nodeBegin = range->begin; item.nodeEnd = range->end; item.materialBegin = range->materialBegin;
        range->ResolveAnimation(item);
        if(item.shape != PrimitiveShape::Asset) item.nodeIndex = PrimitiveRenderItem::kAllNodes;
        if(item.nodeIndex != PrimitiveRenderItem::kAllNodes) {
            if(item.nodeIndex >= range->end - range->begin) item.nodeEnd = 0;
            else item.nodeIndex += range->begin;
        }
    }
    return {};
}
}
