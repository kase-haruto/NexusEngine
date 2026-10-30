#pragma once
#include "OpticalDemoScene.h"
#include <filesystem>

namespace NexusEngine {
/** \brief 同梱デモLevel用の旧:startupモデルをEditor/単体起動で共通に構築する */
[[nodiscard]] Result<ModelAssetData> LoadSampleStartupAsset(const std::filesystem::path& directory,
                                                           OpticalSphereMaterialSlots* slots = nullptr);
}
