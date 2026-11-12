#pragma once
#include "Public/Nexus/GameComponents.h"

namespace NexusEngine {
class Level;
class TypeRegistry;
/**
 * \brief 所有Thread検証済みのWorldで共通Reflectionへの値操作を実行する
 * \param level Runtimeが所有する既存Level。処理中に破棄・交換しない
 * \param types ホストが登録した共通型情報。別の型Registryを構築しない
 * \param world 応答Handleに設定する寿命token
 * \param target 操作対象。Queryだけは空Handleを要求する
 * \return 不正引数・不在・容量不足をstatusで返し、C++例外を境界へ出さない
 */
NexusGameStatus InvokeGameComponent(Level& level, const TypeRegistry& types, uint64_t world, NexusEntityHandle target,
    NexusGameBytes request, uint8_t* response, uint32_t capacity, uint32_t* length) noexcept;
/** \brief 最大64件のHandle / Transformを検証し、無効要素では部分更新せず失敗する */
NexusGameStatus InvokeGameTransformBatch(Level& level, uint64_t world, NexusEntityHandle target,
    NexusGameBytes request, uint8_t* response, uint32_t capacity, uint32_t* length) noexcept;
}
