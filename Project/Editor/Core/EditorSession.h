#pragma once

#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include <cmath>

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * EditorSession
	 * - 編集用Level、永続IDによる選択、編集状態を所有する
	 * - ImGui、GPU、通信には依存しない。同一Process内の編集を担当する
	 *---------------------------------------------------------------------------------------*/
	class EditorSession final {
	public:
		/** \brief 既存選択を解除して編集用Worldを生成する */
		[[nodiscard]] Result<void> Create() {
			Close();
			return level_.LoadEmpty();
		}
		/** \brief World参照を破棄する前に選択状態を解除する */
		void Close() noexcept {
			selected_ = {};
			dirty_ = false;
			level_.Unload();
		}
		/** \brief 現在のWorldで解決できる永続IDだけを選択する */
		void Select(PersistentId id) {
			selected_ = level_.FindEntity(id).IsAlive() ? id : PersistentId{};
		}
		[[nodiscard]] PersistentId GetSelection() const noexcept { return selected_; }
		[[nodiscard]] Entity GetSelectedEntity() { return level_.FindEntity(selected_); }
		[[nodiscard]] Level& GetLevel() noexcept { return level_; }
		[[nodiscard]] bool IsDirty() const noexcept { return dirty_; }

		/** \brief IDとLocal値による編集を適用する。派生MatrixはLevel更新で再計算する */
		[[nodiscard]] bool SetTransform(PersistentId id, Vector3 translation, Quaternion rotation, Vector3 scale) {
			const auto finite = [](Vector3 value) {
				return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
			};
			const float lengthSquared = rotation.x * rotation.x + rotation.y * rotation.y
				+ rotation.z * rotation.z + rotation.w * rotation.w;
			if(!finite(translation) || !finite(scale) || !std::isfinite(lengthSquared) || lengthSquared < 1.0e-12f) return false;
			auto entity = level_.FindEntity(id);
			auto* transform = entity.GetComponent<TransformComponent>();
			if(transform == nullptr) return false;
			const float inverseLength = 1.0f / std::sqrt(lengthSquared);
			rotation.x *= inverseLength;
			rotation.y *= inverseLength;
			rotation.z *= inverseLength;
			rotation.w *= inverseLength;
			transform->translation = translation;
			transform->rotation = rotation;
			transform->scale = scale;
			dirty_ = true;
			return true;
		}

	private:
		Level level_{"EditorLevel"}; //< 編集用World。再生用Worldとは共有しない
		PersistentId selected_; //< EntityやComponentのアドレスを保持しない
		bool dirty_ = false; //< 編集操作が適用されたか
	};
}
