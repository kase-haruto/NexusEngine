#include "RenderSceneExtractor.h"

// c++
#include <cmath>
#include <optional>
#include <utility>

// engine
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/AnimationPlayerComponent.h"
#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include "Runtime/Scene/Components/SkyRendererComponent.h"
#include "Runtime/Scene/Components/FogComponent.h"
#include "Runtime/Scene/Components/SkyAtmosphereComponent.h"
#include "Runtime/Scene/Components/SkyLightComponent.h"
#include "Runtime/Scene/Components/ExponentialHeightFogComponent.h"

#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Runtime/Scene/Scene.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidViewport = 10;
		constexpr int32_t kInvalidCameraProjection = 11;
		constexpr int32_t kMultiplePrimaryCameras = 12;
		constexpr int32_t kNonInvertibleCameraTransform = 13;
		constexpr float kPi = 3.14159265358979323846f;

		[[nodiscard]] constexpr RenderObjectId MakeRenderObjectId(const EntityId entity) noexcept {
			return { (static_cast<uint64_t>(entity.generation) << 32) | entity.index };
		}
	}

	Result<RenderScene> RenderSceneExtractor::Extract(Scene& scene, const float aspectRatio) const {
		RenderScene result;
		auto status = ExtractInto(scene, aspectRatio, result);
		if(!status) return std::unexpected(std::move(status.error()));
		return result;
	}

	Result<void> RenderSceneExtractor::ExtractInto(Scene& scene, const float aspectRatio, RenderScene& result, bool includePrimaryCamera) const {
		if(!std::isfinite(aspectRatio) || aspectRatio <= 0.0f) {
			return std::unexpected(Error(ErrorCategory::Scene, kInvalidViewport, "Render viewport aspect ratio must be positive and finite."));
		}

		// clearはcapacityを保持するので、安定したEntity数なら毎frameの再確保は発生しない。
		result.primitives.clear();
        scene.CopyDebugLines(result.debugLines);
		result.camera.reset();
        result.atmosphere = {};
        result.skyLight.reset();
        result.fog = {};
        bool invalidFog = false;
        scene.ForEach<FogComponent>([&](Entity, const FogComponent& fog) {
            if(!fog.enabled) return;
            if(!fog.IsValid() || result.fog.enabled) { invalidFog = true; return; }
            result.fog = fog.GetSettings();
        });
        if(invalidFog) return std::unexpected(Error(ErrorCategory::Scene, 17,
            "Expected at most one enabled, valid distance fog component."));
        result.skyIntensity.reset();
        bool invalidSky = false;
        scene.ForEach<SkyRendererComponent>([&](Entity, const SkyRendererComponent& sky) {
            if(!sky.enabled) return;
            if(!sky.IsValid() || result.skyIntensity.has_value()) { invalidSky = true; return; }
            result.skyIntensity = sky.intensity;
        });
        if(invalidSky) return std::unexpected(Error(ErrorCategory::Scene, 16,
            "Expected at most one enabled, valid sky renderer."));
		result.directionalLight = {};
		result.directionalLight.intensity = 0;
		bool lightFound = false;
		std::optional<Error> lightError;
		scene.ForEach<TransformComponent, DirectionalLightComponent>(
			[&](Entity, TransformComponent& transform, DirectionalLightComponent& component) {
				if(lightError || !component.enabled) return;
				if(lightFound || !component.IsValid()) {
					lightError = Error(ErrorCategory::Scene, 14, "Expected at most one enabled, valid directional light."); return;
				}
				lightFound = true;
				result.directionalLight = component.light;
				float lengthSquared = 0;
				for(size_t c = 0; c < 3; ++c) {
					float value = 0;
					for(size_t r = 0; r < 3; ++r) value += component.light.direction[r] * transform.worldMatrix.At(r, c);
					result.directionalLight.direction[c] = value; lengthSquared += value * value;
				}
				if(!std::isfinite(lengthSquared) || lengthSquared < 1e-12f) {
					lightError = Error(ErrorCategory::Scene, 15, "Directional light world direction is invalid."); return;
				}
				for(float& value : result.directionalLight.direction) value /= std::sqrt(lengthSquared);
			});
		if(lightError) return std::unexpected(std::move(*lightError));
        bool invalidEnvironment = false;
        scene.ForEach<SkyAtmosphereComponent>([&](Entity, const auto& sky) {
            if(!sky.enabled) return;
            if(!sky.IsValid() || result.atmosphere.enabled) { invalidEnvironment = true; return; }
            result.atmosphere = sky;
        });
        // Sky Atmosphere takes precedence over the legacy background without changing saved entities.
        if(result.atmosphere.enabled) {
            result.skyIntensity = 1.0f;
            if(!result.atmosphere.driveSun && lightFound) result.atmosphere.sunDirectionOverride = result.directionalLight.direction;
            if(result.atmosphere.driveSun) {
                result.directionalLight.direction = result.atmosphere.SunDirection();
                result.directionalLight.color = result.atmosphere.SunTransmittance();
                result.directionalLight.intensity = result.atmosphere.sunIntensity;
            }
        }
        scene.ForEach<SkyLightComponent>([&](Entity, const auto& sky) {
            if(!sky.IsValid() || result.skyLight.has_value()) { invalidEnvironment = true; return; }
            result.skyLight = sky;
        });
        bool heightFogFound = false;
        scene.ForEach<TransformComponent, ExponentialHeightFogComponent>([&](Entity, const auto& transform, const auto& fog) {
            if(!fog.enabled) return;
            if(!fog.IsValid() || heightFogFound) { invalidEnvironment = true; return; }
            heightFogFound = true;
            // Height fog replaces legacy distance fog when both exist in an old level.
            result.fog = fog.GetSettings(transform.worldMatrix.At(3, 1));
            if(!result.fog.IsValid()) { invalidEnvironment = true; return; }
            if(result.atmosphere.enabled && fog.atmosphereContribution > 0) {
                auto sun = result.atmosphere.SunTransmittance();
                const float elevation = result.atmosphere.SunDirection()[1];
                const float daylight = std::clamp((elevation + .12f) / .3f, 0.0f, 1.0f);
                for(size_t i = 0; i < 3; ++i) result.fog.color[i] *=
                    fog.atmosphereContribution * (daylight * (.15f + sun[i]) + result.atmosphere.nightIntensity);
            }
        });
        if(invalidEnvironment) return std::unexpected(Error(ErrorCategory::Scene, 18,
            "Expected valid, unique Sky Atmosphere, Sky Light and Exponential Height Fog components."));

		scene.ForEach<TransformComponent, PrimitiveRenderComponent>(
			[&](Entity entity, TransformComponent& transform, PrimitiveRenderComponent& primitive) {
				if(primitive.enabled) {
					result.primitives.push_back({ MakeRenderObjectId(entity.GetId()), transform.worldMatrix, primitive.tint, primitive.nodeIndex });
					auto& item = result.primitives.back();
					item.staticLighting = primitive.staticLighting;
                    if(const auto* player = entity.GetComponent<AnimationPlayerComponent>(); player && player->IsValid()) {
                        item.animation = PrimitiveRenderItem::AnimationSample{player->animationIndex, player->time, player->loop};
                        item.staticLighting = false;
                    }
                    item.meshAsset = primitive.meshAsset;
                    item.shape = primitive.shape;
                    if(primitive.shape == PrimitiveShape::Asset && primitive.meshAsset.empty()) item.nodeEnd = 0;
                    item.baseColorTexture = primitive.baseColorTexture;
                    item.material = primitive.material;
					if(const auto* id = entity.GetComponent<PersistentIdComponent>()) {
						// ECSの世代番号に依存せず、Levelの永続IDから照合キーを作る。
						item.lightingId = id->value.high ^ (id->value.low * 1099511628211ull);
					}
				}
			});

		if(!includePrimaryCamera) return {};
		std::optional<Error> cameraError;
		scene.ForEach<TransformComponent, CameraComponent>(
			[&](Entity entity, TransformComponent& transform, CameraComponent& camera) {
				if(cameraError.has_value() || !camera.enabled || !camera.primary) {
					return;
				}
				if(result.camera.has_value()) {
					cameraError = Error(ErrorCategory::Scene, kMultiplePrimaryCameras, "Scene contains multiple enabled primary cameras.");
					return;
				}
				if(!std::isfinite(camera.verticalFieldOfViewRadians) ||
				   !std::isfinite(camera.nearClip) || !std::isfinite(camera.farClip) ||
				   camera.verticalFieldOfViewRadians <= 0.0f || camera.verticalFieldOfViewRadians >= kPi ||
				   camera.nearClip <= 0.0f || camera.farClip <= camera.nearClip) {
					cameraError = Error(ErrorCategory::Scene, kInvalidCameraProjection, "Primary camera projection settings are invalid.");
					return;
				}

				const auto viewMatrix = TryInverse(transform.worldMatrix);
				if(!viewMatrix.has_value()) {
					cameraError = Error(ErrorCategory::Scene, kNonInvertibleCameraTransform, "Primary camera world transform is not invertible.");
					return;
				}
				const Matrix4x4 projection = MakePerspectiveFovMatrix(
					camera.verticalFieldOfViewRadians, aspectRatio, camera.nearClip, camera.farClip);
				result.camera = RenderCamera {
					MakeRenderObjectId(entity.GetId()), *viewMatrix, projection, Multiply(*viewMatrix, projection)
				};
			});

		if(cameraError.has_value()) {
			return std::unexpected(std::move(*cameraError));
		}
		return {};
	}

} // namespace NexusEngine
