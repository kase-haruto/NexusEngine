#pragma once
#include "Graphics/Model/ModelAssetData.h"
#include <algorithm>

namespace NexusEngine {
// Source-local slots used by the same nodes that the renderer draws.
inline std::vector<int> MaterialSlots(const ModelAssetData& source, int selectedNode) {
    std::vector<int> slots;
    for(size_t node = 0; node < source.nodes.size(); ++node) {
        if(selectedNode >= 0 && node != static_cast<size_t>(selectedNode)) continue;
        for(auto mesh : source.nodes[node].meshIndices) if(mesh < source.meshes.size())
            for(const auto& sub : source.meshes[mesh].submeshes)
                if(sub.indexCount && sub.materialSlot < source.materials.size()) slots.push_back(static_cast<int>(sub.materialSlot));
    }
    std::ranges::sort(slots);
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    return slots;
}
}
