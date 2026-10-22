#pragma once
#include "ModelAssetData.h"
#include "Graphics/Resource/WicImageDecoder.h"
namespace NexusEngine {
/** \brief Bind姿勢の実Geometryを深度付きで縮小描画する。GPUやEditor状態を使用しない */
Result<DecodedImage> CreateModelThumbnail(const ModelAssetData& asset, int node = -1);
/** \brief Aspectを維持し正方形のchecker背景へ縮小する */
DecodedImage CreateImageThumbnail(const DecodedImage& image);
}
