#pragma once
#include <array>
#include <cmath>

namespace NexusEngine {
	/** API非依存の鏡面/透過特性。absorptionはWorld単位あたりのBeer-Lambert係数。 */
	struct OpticalMaterial {
		float reflectivity = 0; //< 鏡面の反射率、0〜1
		float transmission = 0; //< ガラス透過率、0〜1
		float indexOfRefraction = 1.5f; //< 空気を1とする屈折率
		std::array<float, 3> absorption {0, 0, 0}; //< 内部移動距離に応じたRGB吸収
		[[nodiscard]] bool IsOptical() const noexcept { return reflectivity > 0 || transmission > 0; }
		[[nodiscard]] bool IsValid() const noexcept {
			if(!std::isfinite(reflectivity) || reflectivity < 0 || reflectivity > 1 ||
				!std::isfinite(transmission) || transmission < 0 || transmission > 1 ||
				!std::isfinite(indexOfRefraction) || indexOfRefraction < 1 || indexOfRefraction > 3) return false;
			for(float value : absorption) if(!std::isfinite(value) || value < 0) return false;
			return true;
		}
	};
}
