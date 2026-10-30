#include "ShadowSystem.h"

#include <cmath>
#include "Graphics/Backend/DirectX12/Shadow/ShadowBackend.h"
#include "Graphics/Renderer/GraphicsContext.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"

namespace NexusEngine {
	class ShadowSystem::Impl final {
	public:
		ShadowBackend backend;
	};
	ShadowSystem::ShadowSystem() noexcept = default;
	ShadowSystem::~ShadowSystem() noexcept { Shutdown(); }
	void ShadowSystem::RetainGeometry(std::span<const uint64_t> generations) { if(impl_) impl_->backend.RetainGeometry(generations); }
	void ShadowSystem::Swap(ShadowSystem& other) noexcept {
		if(impl_) impl_->backend.ResetGeometry();
		if(other.impl_) other.impl_->backend.ResetGeometry();
		impl_.swap(other.impl_);
		std::swap(settings_, other.settings_);
	}
	Result<void> ShadowSystem::SetSettings(const ShadowSettings& settings) {
		if(static_cast<uint8_t>(settings.mode) > static_cast<uint8_t>(ShadowMode::ShadowMap) ||
			settings.sampleCount == 0 || settings.sampleCount > 64 ||
			settings.shadowMapResolution < 128 || settings.shadowMapResolution > 8192 ||
			!std::isfinite(settings.rayOriginBias) || settings.rayOriginBias < 0 ||
			!std::isfinite(settings.rayTMin) || settings.rayTMin < 0 ||
			!std::isfinite(settings.rayTMax) || settings.rayTMax <= settings.rayTMin ||
			!std::isfinite(settings.shadowMapBias) || settings.shadowMapBias < 0 ||
			!std::isfinite(settings.shadowMapExtent) || settings.shadowMapExtent <= 0) {
			return std::unexpected(Error(ErrorCategory::Graphics, 50, "Invalid shadow settings (range or finite-value check failed)."));
		}
		settings_ = settings;
		return {};
	}
	Result<void> ShadowSystem::Initialize(GraphicsResourceFactory& factory, const std::filesystem::path& shaders) {
		if(impl_) return std::unexpected(Error(ErrorCategory::Graphics, 50, "ShadowSystem already initialized."));
		auto implementation = std::make_unique<Impl>();
		auto result = implementation->backend.Initialize(factory.nativeDevice_,
			factory.commandQueue_, factory.descriptors_, factory.bindlessDescriptors_, &factory,
			factory.framesInFlight_, factory.capabilities_, shaders);
		if(!result) { implementation->backend.Shutdown(); return result; }
		impl_ = std::move(implementation);
		return {};
	}
	Result<ShaderResourceRef> ShadowSystem::Render(GraphicsContext& context, const Matrix4x4& viewProjection,
		const DirectionalLightData& light, const std::span<const ShadowDrawItem> draws) {
		if(!impl_) return std::unexpected(Error(ErrorCategory::Graphics, 50, "ShadowSystem is not initialized."));
		auto connection = context.RegisterRecording(&impl_->backend, &ShadowBackend::CompleteRecording);
		if(!connection) return std::unexpected(std::move(connection.error()));
		auto result = impl_->backend.Render(context, context.nativeCommandList_,
			viewProjection, light, draws, settings_);
		// Restore even after an optional technique fails, so Lighting/ImGui cannot accidentally
		// target the Shadow Map or inherit its resolution as the viewport.
		context.RestoreMainRenderTargets();
		return result;
	}
	GraphicsCapabilities ShadowSystem::GetCapabilities() const noexcept { return impl_ ? impl_->backend.capabilities : GraphicsCapabilities {}; }
	ShadowStatistics ShadowSystem::GetStatistics() const noexcept { return impl_ ? impl_->backend.GetStatistics() : ShadowStatistics {}; }
	void ShadowSystem::Shutdown() noexcept {
		if(impl_) impl_->backend.Shutdown();
		impl_.reset();
	}
}
