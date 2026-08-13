#pragma once

#include "GuiTypes.h"

namespace NexusEngine::UI {
	/* Widget実装が共有する意味ベースのTheme。ImGuiCol等のBackend定数は公開しない。 */
	struct Theme {
		Color text { 0.90f, 0.90f, 0.90f, 1.00f };
		Color textDisabled { 0.55f, 0.55f, 0.55f, 1.00f };
		Color surface { 0.09f, 0.09f, 0.09f, 1.00f };
		Color surfaceHovered { 0.18f, 0.18f, 0.18f, 1.00f };
		Color surfaceActive { 0.245f, 0.245f, 0.245f, 1.00f };
		Color accent { 1.00f, 0.35f, 0.10f, 1.00f };
		Color error { 0.90f, 0.20f, 0.20f, 1.00f };
		Vector2 itemSpacing { 8.0f, 4.0f };
		Vector2 framePadding { 5.0f, 5.0f };
		float controlRounding = 3.0f;
		float disabledOpacity = 0.45f;
	};
} // namespace NexusEngine::UI
