#include "GuiContext.h"

namespace NexusEngine::UI {
	namespace {
		constexpr int32_t kAlreadyInitialized = 1;
	}

	Result<void> Context::Initialize(const ContextDesc& desc) {
		if(initialized_) {
			return std::unexpected(Error(
				ErrorCategory::Framework, kAlreadyInitialized, "GUI context is already initialized."));
		}
		textureProvider_ = desc.textureProvider;
		theme_ = desc.theme;
		frameIndex_ = 0;
		frameActive_ = false;
		initialized_ = true;
		return {};
	}

	void Context::Shutdown() noexcept {
		textureProvider_ = nullptr;
		frameIndex_ = 0;
		frameActive_ = false;
		initialized_ = false;
	}

	void Context::BeginFrame() noexcept {
		if(!initialized_ || frameActive_) return;
		++frameIndex_;
		frameActive_ = true;
	}

	void Context::EndFrame() noexcept {
		if(!initialized_) return;
		frameActive_ = false;
	}

	bool Context::IsInitialized() const noexcept { return initialized_; }
	bool Context::IsFrameActive() const noexcept { return frameActive_; }
	uint64_t Context::GetFrameIndex() const noexcept { return frameIndex_; }
	const Theme& Context::GetTheme() const noexcept { return theme_; }
	ITextureProvider* Context::GetTextureProvider() const noexcept { return textureProvider_; }
	void Context::SetTheme(const Theme& theme) noexcept { theme_ = theme; }
	void Context::SetTextureProvider(ITextureProvider* const provider) noexcept { textureProvider_ = provider; }
} // namespace NexusEngine::UI
