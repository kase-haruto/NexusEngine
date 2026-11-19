#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <string>

namespace NexusEngine {
/** \brief Mesh/Texture/GPUを構築せず、glTF/GLBの実際のsampler時刻からclip長を取得する。 */
Result<float> GltfAnimationDuration(const std::filesystem::path& file, const std::string& clip);
}
