#pragma once

// engine
#include "Foundation/Math/Matrix4x4.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * TransformComponent
	 * - EntityのLocal Transformを保持する
	 * - Matrix計算と親子階層の評価はTransformSystemが担当する
	 *---------------------------------------------------------------------------------------*/
	struct TransformComponent {
		Vector3 translation;                         //< Local座標
		Quaternion rotation;                         //< Local回転。常にQuaternionを正本とする
		Vector3 scale { 1.0f, 1.0f, 1.0f };          //< Local拡大率
		Matrix4x4 localMatrix;                       //< Local値から毎Update再計算するMatrix
		Matrix4x4 worldMatrix;                       //< 親のWorld Matrixを反映したMatrix
	};

} // namespace NexusEngine
