#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include "Foundation/Error/Result.h"
#include <array>
namespace NexusEngine {
struct ModelAssetData;
/** Engine内だけのcooking結果。Jolt shapeの所有参照をWorld cacheへ移す。 */
struct CookedPhysicsMesh { JPH::ShapeRefC shape; uint32_t triangles=0; };
/**
 * \brief 既存CPU modelのbind pose・submesh範囲を検証してTriangle Mesh/Convex Hullを生成する
 * \param shape 3=Triangle Mesh、4=Convex Hull。GPUや独自Asset loaderを利用しない
 * \return Native/Managed/Game DLLへJolt型を公開せず、Engine内でのみ利用する所有結果
 */
Result<CookedPhysicsMesh> CookPhysicsMesh(const ModelAssetData& asset,int shape,const std::array<float,3>& scale);
}
