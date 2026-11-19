#include "CameraEffectComponent.h"
#include <cmath>
#include <algorithm>

namespace NexusEngine {
bool CameraEffectComponent::IsValid() const {
    if(shader.empty() || shader.size() > 4096 || shader.find('\0') != std::string::npos) {
        return false;
    }
    for(const auto* parameters : {&parameters0, &parameters1, &parameters2, &parameters3}) {
        if(!std::ranges::all_of(*parameters, [](float value) { return std::isfinite(value); })) {
            return false;
        }
    }
    return true;
}
bool CameraEffectComponent::IsPropertyAnimatable(std::string_view id) {
    return id == "parameters0" || id == "parameters1" || id == "parameters2" || id == "parameters3";
}
}
