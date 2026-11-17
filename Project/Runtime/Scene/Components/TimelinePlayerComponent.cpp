#include "TimelinePlayerComponent.h"
#include <cmath>
namespace NexusEngine {
bool TimelinePlayerComponent::IsValid() const noexcept {
    return std::isfinite(time) && time >= 0 && std::isfinite(speed) && speed >= 0;
}
}
