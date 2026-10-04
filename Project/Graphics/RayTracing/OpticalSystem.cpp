#include "OpticalSystem.h"
#include <cmath>
#include "Graphics/Backend/DirectX12/RayTracing/OpticalBackend.h"
#include "Graphics/Renderer/GraphicsContext.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"

namespace NexusEngine {
	class OpticalSystem::Impl { public: OpticalBackend backend; };
	OpticalSystem::OpticalSystem() noexcept = default;
	OpticalSystem::~OpticalSystem() noexcept { Shutdown(); }
	Result<void> OpticalSystem::Initialize(GraphicsResourceFactory& factory, const std::filesystem::path& shaders) {
		if(impl_) return std::unexpected(Error(ErrorCategory::Graphics, 70, "OpticalSystem already initialized."));
		auto next = std::make_unique<Impl>();
		auto result = next->backend.Initialize(factory.nativeDevice_, factory.commandQueue_, factory.bindlessDescriptors_,
			&factory, factory.framesInFlight_, factory.capabilities_.SupportsRayTracing(), shaders);
		if(!result) { next->backend.Shutdown(); return result; }
		impl_ = std::move(next); return {};
	}
	Result<void> OpticalSystem::SetSettings(const OpticalSettings& settings) {
		if(settings.maxBounces < 1 || settings.maxBounces > 12 || !std::isfinite(settings.rayBias) ||
			settings.rayBias <= 0 || !std::isfinite(settings.rayDistance) || settings.rayDistance <= settings.rayBias)
			return std::unexpected(Error(ErrorCategory::Graphics, 70, "Invalid optical settings."));
		settings_ = settings; return {};
	}
	Result<ShaderResourceRef> OpticalSystem::Render(GraphicsContext& context, const Matrix4x4& vp,
		const DirectionalLightData& light, std::span<const OpticalDrawItem> draws, std::span<const OpticalSurfaceMaterial> materials) {
		if(!impl_) return std::unexpected(Error(ErrorCategory::Graphics, 70, "OpticalSystem is not initialized."));
		auto connected = context.RegisterRecording(&impl_->backend, &OpticalBackend::CompleteRecording);
		if(!connected) return std::unexpected(std::move(connected.error()));
		return impl_->backend.Render(context, context.nativeCommandList_, vp, light, draws, materials, settings_);
	}
	void OpticalSystem::Shutdown() noexcept { if(impl_) impl_->backend.Shutdown(); impl_.reset(); }
}
