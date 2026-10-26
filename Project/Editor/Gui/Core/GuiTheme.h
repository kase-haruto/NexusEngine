#pragma once

#include "GuiTypes.h"

namespace NexusEngine::UI {
	/* Widget実装が共有する意味ベースのTheme。ImGuiCol等のBackend定数は公開しない。 */
	struct Theme {
		Color text { 0.90f, 0.90f, 0.90f, 1.00f };
		Color textDisabled { 0.55f, 0.55f, 0.55f, 1.00f };
		Color surface { 0.205f, 0.205f, 0.205f, 1.00f };
		Color surfaceHovered { 0.26f, 0.26f, 0.26f, 1.00f };
		Color surfaceActive { 0.31f, 0.31f, 0.31f, 1.00f };
		Color accent { 0.95f, 0.49f, 0.18f, 1.00f };
		Color error { 0.90f, 0.20f, 0.20f, 1.00f };
		// Transform/vector fields share the same axis identity throughout the editor.
		Color axisX { 0.88f, 0.32f, 0.36f, 1.00f };
		Color axisY { 0.48f, 0.72f, 0.28f, 1.00f };
		Color axisZ { 0.30f, 0.56f, 0.90f, 1.00f };
		Color axisW { 0.68f, 0.48f, 0.82f, 1.00f };
		Vector2 itemSpacing { 8.0f, 4.0f };
		Vector2 framePadding { 6.0f, 3.0f };
		float controlRounding = 4.0f;
		float disabledOpacity = 0.45f;
	};
} // namespace NexusEngine::UI
