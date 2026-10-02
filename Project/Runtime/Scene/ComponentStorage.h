#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace NexusEngine {
	/* Component実データの所有とdense走査だけを担当する型消去境界。 */
	class IComponentStorage {
	public:
		virtual ~IComponentStorage() = default;
		virtual void Remove(uint32_t entityIndex) = 0;
		[[nodiscard]] virtual size_t GetSize() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetEntityIndexAt(size_t denseIndex) const noexcept = 0;
		[[nodiscard]] virtual std::span<const uint32_t> GetEntityIndices() const noexcept = 0;
	};

	/*-----------------------------------------------------------------------------------------
	 * ComponentStorage
	 * - 型別Sparse Setと連続Component配列を所有する
	 * - Entity世代、Level、Renderer、Editor UIは管理しない
	 *---------------------------------------------------------------------------------------*/
	template<typename Component>
	class ComponentStorage final : public IComponentStorage {
	public:
		static_assert(std::is_nothrow_move_constructible_v<Component>);
		static_assert(std::is_nothrow_move_assignable_v<Component>);

		/** \brief 重複を拒否してComponentを構築する。再確保により既存参照は無効になる */
		template<typename... Arguments>
		[[nodiscard]] Component* Add(const uint32_t entityIndex, Arguments&&... arguments) {
			if(Contains(entityIndex)) {
				return nullptr;
			}
			if(entityIndex >= sparseIndices_.size()) {
				sparseIndices_.resize(static_cast<size_t>(entityIndex) + 1, kInvalidDenseIndex);
			}

			// vector既定の幾何増加をreserve(size+1)で潰すと追加N件がO(N^2)になる。
			// 容量不足時だけ約2倍へ拡張し、Component構築後のEntity index追加はnoexceptに保つ。
			const size_t requiredSize = denseComponents_.size() + 1;
			if(denseEntities_.capacity() < requiredSize) {
				const size_t newCapacity = (std::max)(requiredSize, denseEntities_.capacity() * 2);
				denseEntities_.reserve(newCapacity);
			}
			if(denseComponents_.capacity() < requiredSize) {
				const size_t newCapacity = (std::max)(requiredSize, denseComponents_.capacity() * 2);
				denseComponents_.reserve(newCapacity);
			}
			denseComponents_.emplace_back(std::forward<Arguments>(arguments)...);
			denseEntities_.push_back(entityIndex);
			const size_t denseIndex = denseComponents_.size() - 1;
			sparseIndices_[entityIndex] = denseIndex;
			return &denseComponents_.back();
		}

		[[nodiscard]] Component* Get(const uint32_t entityIndex) noexcept {
			return Contains(entityIndex) ? &denseComponents_[sparseIndices_[entityIndex]] : nullptr;
		}

		[[nodiscard]] const Component* Get(const uint32_t entityIndex) const noexcept {
			return Contains(entityIndex) ? &denseComponents_[sparseIndices_[entityIndex]] : nullptr;
		}

		void Remove(const uint32_t entityIndex) override {
			if(!Contains(entityIndex)) {
				return;
			}
			const size_t removedDenseIndex = sparseIndices_[entityIndex];
			const size_t lastDenseIndex = denseComponents_.size() - 1;
			if(removedDenseIndex != lastDenseIndex) {
				// 穴へ末尾を移し、移動元Entityの逆引きを更新してdense/sparseを一致させる。
				denseComponents_[removedDenseIndex] = std::move(denseComponents_[lastDenseIndex]);
				const uint32_t movedEntityIndex = denseEntities_[lastDenseIndex];
				denseEntities_[removedDenseIndex] = movedEntityIndex;
				sparseIndices_[movedEntityIndex] = removedDenseIndex;
			}
			denseComponents_.pop_back();
			denseEntities_.pop_back();
			sparseIndices_[entityIndex] = kInvalidDenseIndex;
		}

		[[nodiscard]] size_t GetSize() const noexcept override { return denseComponents_.size(); }
		[[nodiscard]] std::span<const uint32_t> GetEntityIndices() const noexcept override { return denseEntities_; }
		[[nodiscard]] uint32_t GetEntityIndexAt(const size_t denseIndex) const noexcept override {
			return denseEntities_[denseIndex];
		}

	private:
		static constexpr size_t kInvalidDenseIndex = (std::numeric_limits<size_t>::max)();

		[[nodiscard]] bool Contains(const uint32_t entityIndex) const noexcept {
			return entityIndex < sparseIndices_.size() && sparseIndices_[entityIndex] != kInvalidDenseIndex;
		}

		std::vector<size_t> sparseIndices_; //< Entity indexからdense indexへの対応
		std::vector<uint32_t> denseEntities_; //< denseComponents_と同じ順序のEntity index
		std::vector<Component> denseComponents_; //< 反復対象を連続配置するComponent所有領域
	};

} // namespace NexusEngine
