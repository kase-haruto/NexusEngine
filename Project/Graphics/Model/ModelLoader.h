#pragma once

// c++
#include <filesystem>

// engine
#include "Foundation/Error/Result.h"
#include "ModelAssetData.h"

namespace NexusEngine {
	/*-----------------------------------------------------------------------------------------
	 * ModelLoader
	 * - 外部モデルファイルをBackend非依存のModelAssetDataへ変換する
	 * - GPU Resource生成と描画は担当しない
	 *---------------------------------------------------------------------------------------*/
	class ModelLoader final {
	public:
		/**
		 * \brief Wavefront OBJを読み込み、object/group単位のMeshを生成する
		 * \param filePath 読み込むOBJファイル
		 * \return GPU upload前のModelデータ。面はTriangle Listへ三角形分割される
		 * \note 現段階では頂点位置と面だけを対象とし、material/normal/UVは後続拡張で扱う
		 */
		[[nodiscard]] Result<ModelAssetData> LoadObj(const std::filesystem::path& filePath) const;

		/**
		 * \brief glTF 2.0（.gltf/.glb）からMesh、Node、Skin、Animationを読み込む
		 * \param filePath 読み込むglTFまたはGLBファイル
		 * \return Graphics API非依存のModelデータ
		 */
		[[nodiscard]] Result<ModelAssetData> LoadGltf(const std::filesystem::path& filePath) const;
	};
} // namespace NexusEngine
