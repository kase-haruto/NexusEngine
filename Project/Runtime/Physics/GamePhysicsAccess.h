#pragma once
#include "Public/Nexus/GamePhysics.h"
namespace NexusEngine {
class Level;
/**
 * \brief 所有threadとWorld leaseを検証済みの既存LevelへPhysicsの値操作を適用する
 * \param world 戻り値HandleとtargetのWorldを検査する寿命token
 * \return 無効Handle・不正値・容量不足・solver失敗をstatusへ変換する
 */
NexusGameStatus InvokeGamePhysics(Level& level, uint64_t world, NexusEntityHandle target,
    NexusGameBytes request, uint8_t* response, uint32_t capacity, uint32_t* length) noexcept;
/** \brief 同じ接触値をsnapshot APIと既存Event payloadへ変換する。生のEntityを保持しない */
NexusGameStatus CopyPhysicsContacts(Level& level,uint64_t world,uint32_t offset,uint8_t* response,uint32_t capacity,uint32_t* length) noexcept;
}
