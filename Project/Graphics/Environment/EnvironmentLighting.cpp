#include "EnvironmentLighting.h"
#include <cmath>
#include <utility>
#include "Graphics/Backend/DirectX12/Environment/EnvironmentMapProcessor.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"
#include "Graphics/Resource/HdrImage.h"
namespace NexusEngine {
    class EnvironmentLighting::Impl {
    public:
        EnvironmentMapProcessor processor;
        std::filesystem::path attemptedPath;
        uint64_t attemptedRevision = 0;
        bool attempted = false;
    };
    EnvironmentLighting::EnvironmentLighting() noexcept = default;
    EnvironmentLighting::~EnvironmentLighting() noexcept { Shutdown(); }
    Result<void> EnvironmentLighting::Initialize(GraphicsResourceFactory& f, const std::filesystem::path& shaders) {
        if(impl_) return std::unexpected(Error(ErrorCategory::Graphics, 90, "EnvironmentLighting already initialized."));
        impl_ = std::make_unique<Impl>();
        impl_->processor.Initialize(f.nativeDevice_, f.commandQueue_, f.bindlessDescriptors_, shaders);
        return {};
    }
    Result<void> EnvironmentLighting::Prepare(const EnvironmentLightingData& settings) {
        if(!impl_) return std::unexpected(Error(ErrorCategory::Graphics, 90, "EnvironmentLighting not initialized."));
        if(!settings.enabled || settings.imagePath.empty()) return {};
        if(impl_->attempted && impl_->attemptedPath == settings.imagePath && impl_->attemptedRevision == settings.revision) return {};
        // Failed requests are cached too: invalid files must not stall/log every frame.
        impl_->attempted = true; impl_->attemptedPath = settings.imagePath; impl_->attemptedRevision = settings.revision;
        auto image = LoadHdrImage(settings.imagePath);
        if(!image) return std::unexpected(std::move(image.error()));
        return impl_->processor.Bake(*image);
    }
    EnvironmentDrawData EnvironmentLighting::GetDrawData(const EnvironmentLightingData& settings) const noexcept {
        auto result = impl_ ? impl_->processor.GetDrawData() : EnvironmentDrawData{};
        const bool valid = std::isfinite(settings.intensity) && settings.intensity >= 0 && std::isfinite(settings.rotation);
        result.parameters[0] = valid ? settings.intensity : 0;
        result.parameters[1] = valid ? std::sin(settings.rotation) : 0;
        result.parameters[2] = valid ? std::cos(settings.rotation) : 1;
        result.options[1] = result.options[1] && valid && settings.enabled && !settings.imagePath.empty();
        result.options[2] = static_cast<uint32_t>(settings.debugMode);
        result.options[3] = settings.legacyHalfLambert ? 1 : 0;
        return result;
    }
    void EnvironmentLighting::Shutdown() noexcept { if(impl_) impl_->processor.Shutdown(); impl_.reset(); }
}
