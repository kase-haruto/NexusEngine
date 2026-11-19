#pragma once
#include "ThirdParty/cgltf/cgltf.h"
namespace NexusEngine {
/** \brief cgltfのpathをUTF-8として解釈し、日本語と長いWindows pathを共通処理する。 */
cgltf_options GltfFileOptions();
}
