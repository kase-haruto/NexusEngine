#include "RenderSceneExtractor.h"

// c++
#include <cmath>
#include <optional>
#include <utility>

// engine
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
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
		if(!std::isfinite(aspectRatio) || aspectRatio <= 0.0f) {
			return std::unexpected(Error(ErrorCategory::Scene, kInvalidViewport, "Render viewport aspect ratio must be positive and finite."));
		}

		RenderScene result;
		scene.ForEach<TransformComponent, PrimitiveRenderComponent>(
			[&](Entity entity, TransformComponent& transform, PrimitiveRenderComponent& primitive) {
				if(primitive.enabled) {
					result.primitives.push_back({ MakeRenderObjectId(entity.GetId()), transform.worldMatrix, primitive.tint });
				}
			});

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
		return result;
	}

} // namespace NexusEngine
