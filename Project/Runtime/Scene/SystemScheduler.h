#pragma once

// c++
#include <memory>
#include <span>
#include <stdexcept>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

// engine
#include "Foundation/Error/Result.h"

namespace NexusEngine {
	class Scene;

	/** ゲームロジック、物理の値書戻し、派生Matrix確定の順。公開DLLのphase値とは別のEngine内部enum。 */
	enum class SystemPhase : uint8_t { Simulation, Physics, Transform };

	/*-----------------------------------------------------------------------------------------
	 * ComponentAccess
	 * - Systemが読む/書くComponent型をSchedulerへ宣言する
	 * - 現段階では診断metadataであり、Componentアクセスの実体は保持しない
	 *---------------------------------------------------------------------------------------*/
	struct ComponentAccess {
		std::vector<std::type_index> reads;  //< 値を変更せず参照するComponent型
		std::vector<std::type_index> writes; //< 値を変更するComponent型
	};

	/*-----------------------------------------------------------------------------------------
	 * ISceneSystem
	 * - 1つのScene更新処理とComponentアクセス宣言を表すRuntime境界
	 * - System間参照、描画実行、Editor UIは管理しない
	 *---------------------------------------------------------------------------------------*/
	class ISceneSystem {
	public:
		virtual ~ISceneSystem() = default;
		[[nodiscard]] virtual SystemPhase GetPhase() const noexcept { return SystemPhase::Simulation; }
		[[nodiscard]] virtual std::span<const std::type_index> GetReadComponents() const noexcept = 0;
		[[nodiscard]] virtual std::span<const std::type_index> GetWriteComponents() const noexcept = 0;
        /** \brief 公開World APIで構造変更も可能なSystemはtrue。アクセス一覧だけで並列安全と判断しない */
        [[nodiscard]] virtual bool HasUnspecifiedComponentAccess() const noexcept { return false; }
		[[nodiscard]] virtual Result<void> Update(Scene& scene, float deltaTime) = 0;
        /** 固定間隔で物理stepの直前に呼ぶ。可変更新と重複実行しない。 */
        virtual Result<void> FixedUpdate(Scene& scene, float fixedDeltaTime);
	};

	/*-----------------------------------------------------------------------------------------
	 * SystemScheduler
	 * - Level内Systemの所有と決定的な逐次実行順を管理する
	 * - Job SystemやThreadの所有はせず、アクセス宣言を将来の依存解析へ公開する
	 *---------------------------------------------------------------------------------------*/
	class SystemScheduler final {
	public:
		/** \brief Systemを末尾へ登録する \return 登録したSystemへの安定参照 */
		template<typename System, typename... Arguments>
		System& AddSystem(Arguments&&... arguments) {
			if(executing_) throw std::logic_error("Cannot register a system during scheduler execution.");
			static_assert(std::is_base_of_v<ISceneSystem, System>);
			auto system = std::make_unique<System>(std::forward<Arguments>(arguments)...);
			System& reference = *system;
            AddOwnedSystem(std::move(system));
			return reference;
		}
        /**
         * \brief Runtime adapterの所有権を既存Schedulerへ移す
         * \param system Engine側で生成済みのSystem。DLL objectそのものは渡さない
         * \return phase順・同phase登録順で格納したSystemへの参照
         */
        ISceneSystem& AddOwnedSystem(std::unique_ptr<ISceneSystem> system);
        /**
         * \brief Module停止時に指定Systemを解除・破棄する
         * \param system このSchedulerが所有するSystem
         * \return 実行中または未登録ならfalse。callback終了後のframe境界で呼ぶ
         */
        bool RemoveSystem(ISceneSystem& system) noexcept;
        [[nodiscard]] bool IsExecuting() const noexcept { return executing_; }

		/** \brief 登録順にSystemを実行する \note frame中の一時allocationは行わない */
		[[nodiscard]] Result<void> Update(Scene& scene, float deltaTime);
        [[nodiscard]] Result<void> FixedStep(Scene& scene, float deltaTime);
        [[nodiscard]] Result<void> VariableStep(Scene& scene, float deltaTime);
    private:
        [[nodiscard]] Result<void> Run(Scene& scene, float deltaTime, int mode);
    public:

		[[nodiscard]] std::span<const std::unique_ptr<ISceneSystem>> GetSystems() const noexcept {
			return systems_;
		}

	private:
		std::vector<std::unique_ptr<ISceneSystem>> systems_; //< Levelと同じ寿命を持つSystem群
		bool executing_ = false; //< 再入と実行中のvector変更を拒否する
	};

} // namespace NexusEngine
