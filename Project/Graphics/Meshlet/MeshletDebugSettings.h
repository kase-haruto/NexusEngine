#pragma once
#include <cstdint>
#include "MeshletConfig.h"
namespace NexusEngine {
enum class MeshletVisualization : uint32_t {
    Normal = NEXUS_MESHLET_DEBUG_NORMAL,
    MeshletColor = NEXUS_MESHLET_DEBUG_COLOR,
    CullingResult = NEXUS_MESHLET_DEBUG_CULLING_RESULT
};
/*-----------------------------------------------------------------------------------------
 * MeshletDebugSettings
 * - Editor が書き換える API 非依存の表示設定
 * - Runtime は ImGui を参照せず、Renderer は値だけを利用する
 *---------------------------------------------------------------------------------------*/
struct MeshletDebugSettings {
    bool enabled = false; //< 従来経路を既定とする opt-in
    bool enableCulling = true; //< AS Frustum Culling
    MeshletVisualization visualization = MeshletVisualization::Normal; //< 決定的 Debug 表示
};
}
