#include "StartupAssets.h"
#include "Graphics/Model/ModelLoader.h"

namespace NexusEngine {
Result<ModelAssetData> LoadSampleStartupAsset(const std::filesystem::path& directory, OpticalSphereMaterialSlots* slots) {
    auto asset = ModelLoader{}.LoadGltf(directory / L"Models/Sponza/Sponza.gltf");
    if(!asset) return std::unexpected(std::move(asset.error()));
    auto spheres = AppendSponzaOpticalSpheres(*asset);
    if(!spheres) return std::unexpected(std::move(spheres.error()));
    if(slots) *slots = *spheres;
    auto testScene = AppendPBRTestScene(*asset);
    if(!testScene) return std::unexpected(std::move(testScene.error()));
    return asset;
}
}
