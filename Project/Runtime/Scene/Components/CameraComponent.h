#pragma once

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * CameraComponent
	 * - Entityを透視投影Cameraとして描画抽出対象にする設定を保持する
	 * - View Matrix計算、Viewportサイズ、GPU Resourceは保持しない
	 *---------------------------------------------------------------------------------------*/
	struct CameraComponent {
		float verticalFieldOfViewRadians = 0.7853981634f; //< 垂直画角（radian）
		float nearClip = 0.1f;                           //< Near clipping distance
		float farClip = 1000.0f;                         //< Far clipping distance
		bool primary = true;                             //< Runtime描画に使用するCamera候補か
		bool enabled = true;                             //< 描画抽出の対象か
	};

} // namespace NexusEngine
