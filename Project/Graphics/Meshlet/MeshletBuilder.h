#pragma once
#include "MeshletData.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Foundation/Error/Result.h"
namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * MeshletBuilder
 * - 通常MeshからMeshletデータを構築するクラス
 * - Meshlet分割、インデックス再構成、Bounds生成を担当する
 * - GPU Resourceの生成や描画処理は担当しない
 *---------------------------------------------------------------------------------------*/
class MeshletBuilder final {
public:
    /**
     * \brief ModelVertex / triangle index を Submesh 単位で Meshlet に変換する
     * \param mesh 静的 Mesh の CPU データ。UInt16 / UInt32 index に対応
     * \return Meshlet と Bounds、または不正な入力のエラー
     */
    [[nodiscard]] Result<MeshletData> Build(const MeshAssetData& mesh) const;
};
}
