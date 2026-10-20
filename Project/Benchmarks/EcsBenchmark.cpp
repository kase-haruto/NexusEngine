// c++
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <iostream>
#include <vector>

// engine
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Runtime/Scene/Scene.h"

namespace NexusEngine::Benchmarks {
	namespace {
		using Clock = std::chrono::steady_clock;

		struct VelocityComponent {
			Vector3 linear;
		};

		struct BenchmarkResult {
			size_t entityCount = 0;
			double createMilliseconds = 0.0;
			double addMilliseconds = 0.0;
			double singleQueryMilliseconds = 0.0;
			double multiQueryMilliseconds = 0.0;
			double removeMilliseconds = 0.0;
			double destroyMilliseconds = 0.0;
			double checksum = 0.0; //< 更新値を外へ出し、最適化で処理が消えることを防ぐ
		};

		template<typename Function>
		double MeasureMilliseconds(Function&& function) {
			const auto begin = Clock::now();
			function();
			return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
		}

		BenchmarkResult RunCase(const size_t entityCount) {
			Scene scene;
			std::vector<Entity> entities;
			entities.reserve(entityCount);

			BenchmarkResult result;
			result.entityCount = entityCount;
			result.createMilliseconds = MeasureMilliseconds([&] {
				for(size_t index = 0; index < entityCount; ++index) {
					entities.push_back(scene.CreateEntity());
				}
			});
			result.addMilliseconds = MeasureMilliseconds([&] {
				for(Entity entity : entities) {
					static_cast<void>(entity.AddComponent<VelocityComponent>(VelocityComponent { { 1.0f, 2.0f, 3.0f } }));
				}
			});
			result.singleQueryMilliseconds = MeasureMilliseconds([&] {
				scene.ForEach<TransformComponent>([](Entity, TransformComponent& transform) {
					transform.translation.x += 1.0f;
				});
			});
			result.multiQueryMilliseconds = MeasureMilliseconds([&] {
				scene.ForEach<TransformComponent, VelocityComponent>(
					[](Entity, TransformComponent& transform, VelocityComponent& velocity) {
						transform.translation = transform.translation + velocity.linear;
					});
			});
			result.removeMilliseconds = MeasureMilliseconds([&] {
				for(Entity entity : entities) {
					static_cast<void>(entity.RemoveComponent<VelocityComponent>());
				}
			});
			scene.ForEach<TransformComponent>([&](Entity, TransformComponent& value) { result.checksum += value.translation.x; });
			result.destroyMilliseconds = MeasureMilliseconds([&] {
				for(Entity entity : entities) {
					static_cast<void>(scene.DestroyEntity(entity));
				}
			});
			return result;
		}
	}

	/**
	 * \brief ECS micro benchmarkを実行してCSVを出力する
	 * \note 最適化Build、同一Hardware、複数回測定の中央値で比較すること
	 */
	void RunEcsBenchmark(std::ostream& output) {
		output << "entities,create_ms,add_ms,single_query_ms,multi_query_ms,remove_ms,destroy_ms,checksum\n";
		for(const size_t count : std::array<size_t, 3> { 10'000, 100'000, 500'000 }) {
			const BenchmarkResult result = RunCase(count);
			output << result.entityCount << ',' << result.createMilliseconds << ',' << result.addMilliseconds << ','
			       << result.singleQueryMilliseconds << ',' << result.multiQueryMilliseconds << ','
			       << result.removeMilliseconds << ',' << result.destroyMilliseconds << ',' << result.checksum << '\n';
		}
	}

} // namespace NexusEngine::Benchmarks

#if defined(NEXUS_ECS_BENCHMARK_MAIN)
int main() {
	NexusEngine::Benchmarks::RunEcsBenchmark(std::cout);
	return 0;
}
#endif
