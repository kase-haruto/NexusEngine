#pragma once

#include <cstdint>

#include "Foundation/Error/Result.h"
#include "GuiTheme.h"
#include "IGuiTextureProvider.h"

namespace NexusEngine::UI {
	struct ContextDesc {
		ITextureProvider* textureProvider = nullptr;
		Theme theme;
	};

	/* GUIサービスとFrame境界を保持する。ImGui ContextやGPU Resourceは所有しない。 */
	class Context final {
	public:
		[[nodiscard]] Result<void> Initialize(const ContextDesc& desc = {});
		void Shutdown() noexcept;
		void BeginFrame() noexcept;
		void EndFrame() noexcept;

		[[nodiscard]] bool IsInitialized() const noexcept;
		[[nodiscard]] bool IsFrameActive() const noexcept;
		[[nodiscard]] uint64_t GetFrameIndex() const noexcept;
		[[nodiscard]] const Theme& GetTheme() const noexcept;
		[[nodiscard]] ITextureProvider* GetTextureProvider() const noexcept;

		void SetTheme(const Theme& theme) noexcept;
		void SetTextureProvider(ITextureProvider* provider) noexcept;

	private:
		ITextureProvider* textureProvider_ = nullptr;
		Theme theme_;
		uint64_t frameIndex_ = 0;
		bool initialized_ = false;
		bool frameActive_ = false;
	};
} // namespace NexusEngine::UI
