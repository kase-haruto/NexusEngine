#include "LevelSerializer.h"

// c++
#include <fstream>
#include <cmath>
#include <locale>
#include <set>
#include <iomanip>
#include <map>
#include <string>
#include <utility>
#include <vector>

// engine
#include "Level.h"
#include "Runtime/Scene/TransformSystem.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/Components/NameComponent.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/TransformComponent.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kOpenFailed = 40;
		constexpr int32_t kInvalidFormat = 41;
		constexpr int32_t kDuplicatePersistentId = 42;
		constexpr uint32_t kFormatVersion = 8;

		using PersistentKey = std::pair<uint64_t, uint64_t>;

		[[nodiscard]] PersistentKey MakeKey(const PersistentId id) noexcept {
			return { id.high, id.low };
		}

		bool IsFinite(const TransformComponent& transform) {
			const float values[] = { transform.translation.x, transform.translation.y, transform.translation.z,
				transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w,
				transform.scale.x, transform.scale.y, transform.scale.z };
			for(float value : values) if(!std::isfinite(value)) return false;
			return std::isfinite(transform.rotation.LengthSquared()) && transform.rotation.LengthSquared() > 0.0f;
		}

		bool IsValid(const CameraComponent& camera) {
			return std::isfinite(camera.verticalFieldOfViewRadians) && std::isfinite(camera.nearClip) && std::isfinite(camera.farClip) &&
				camera.verticalFieldOfViewRadians > 0.0f && camera.verticalFieldOfViewRadians < 3.1415926536f &&
				camera.nearClip > 0.0f && camera.farClip > camera.nearClip;
		}

		bool IsValid(const PrimitiveRenderComponent& primitive) {
			for(float value : primitive.tint) if(!std::isfinite(value)) return false;
            const auto valid = [](const std::string& path) {
                return path.size() <= 4096 && path.find('\0') == path.npos && path.find('\n') == path.npos && path.find('\r') == path.npos;
            };
            return valid(primitive.meshAsset) && valid(primitive.baseColorTexture) && primitive.material.IsValid();
		}

        // v1〜7にはInstance材質がないため、既定のdisabledを保つ。
        bool ReadMaterial(std::istream& input, MaterialOverride& material) {
            input >> material.enabled >> material.slot;
            for(float& value : material.baseColor) input >> value;
            input >> material.metallic >> material.roughness >> material.normalScale;
            for(float& value : material.emissive) input >> value;
            input >> material.occlusionStrength;
            return static_cast<bool>(input) && material.IsValid();
        }

		struct PendingParent {
			EntityId child;       //< 新World内で生成された子Entity
			PersistentKey parent; //< 保存データ内の親PersistentId
		};
	}

	Result<void> LevelSerializer::Save(const std::filesystem::path& path, Level& level) {
		if(level.Lighting() && !level.Lighting()->IsValid())
			return std::unexpected(Error(ErrorCategory::Scene,kInvalidFormat,"Level lighting is invalid."));
		Scene* scene = level.GetScene();
		if(scene == nullptr) {
			return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Cannot serialize an unloaded level."));
		}

		// 非対応Componentを黙って落とすとEditorの保存でデータを失うため、I/O前に拒否する。
		const std::set<std::type_index> supported { typeid(NameComponent), typeid(TransformComponent),
			typeid(PersistentIdComponent), typeid(HierarchyComponent), typeid(CameraComponent), typeid(PrimitiveRenderComponent), typeid(DirectionalLightComponent) };
		bool supportedStorages = true;
		scene->ForEachStorageInfo([&](std::type_index type, size_t size) {
			supportedStorages = supportedStorages && (size == 0 || supported.contains(type));
		});
		if(!supportedStorages) {
			return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "No serializer registered for a populated component storage."));
		}
		std::set<PersistentKey> ids;
		size_t count = 0;
		bool valid = true;
		scene->ForEach<PersistentIdComponent, NameComponent, TransformComponent>(
			[&](Entity entity, PersistentIdComponent& persistent, NameComponent&, TransformComponent& transform) {
				++count;
				valid = valid && persistent.value.IsValid() && ids.insert(MakeKey(persistent.value)).second && IsFinite(transform);
				if(const auto* camera = entity.GetComponent<CameraComponent>()) valid = valid && IsValid(*camera);
				if(const auto* light = entity.GetComponent<DirectionalLightComponent>()) valid = valid && light->IsValid();
				if(const auto* primitive = entity.GetComponent<PrimitiveRenderComponent>()) valid = valid && IsValid(*primitive);
				if(const auto* hierarchy = entity.GetComponent<HierarchyComponent>(); hierarchy && hierarchy->parent.IsValid()) {
					valid = valid && scene->GetComponent<PersistentIdComponent>(hierarchy->parent) != nullptr;
				}
			});
		if(!valid || count != scene->GetEntityCount()) {
			return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Level contains missing, duplicate or invalid persistent data."));
		}
		TransformSystem transformValidation;
		auto hierarchyResult = transformValidation.Update(*scene);
		if(!hierarchyResult) return hierarchyResult;
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.imbue(std::locale::classic());
		if(!output) {
			return std::unexpected(Error(ErrorCategory::FileSystem, kOpenFailed, "Failed to open level file for writing."));
		}

		output << "NEXUS_LEVEL " << kFormatVersion << '\n';
		output << "LEVEL " << std::quoted(level.GetName()) << '\n';
		output << std::setprecision(9);
		output << "LIGHTING " << level.Lighting().has_value();
		if(const auto& lighting=level.Lighting()) {
			for(float value:lighting->skyIrradiance) output << ' ' << value;
			output << ' ' << lighting->probes.intensity;
			for(float value:lighting->probes.origin) output << ' ' << value;
			for(float value:lighting->probes.spacing) output << ' ' << value;
			for(uint32_t value:lighting->probes.counts) output << ' ' << value;
			output << ' ' << lighting->probes.raysPerProbe;
		}
		output << '\n';
		const auto environment=level.Lighting()?level.Lighting()->environment:EnvironmentLightingData{};
		const auto utf8=environment.imagePath.u8string();
		output << "ENVIRONMENT " << std::quoted(std::string(reinterpret_cast<const char*>(utf8.data()),utf8.size()))
			<< ' ' << environment.intensity << ' ' << environment.rotation << '\n';

		// EntityIdは実行ごとに変わるため一切書かず、PersistentIdを参照の唯一の鍵にする。
		scene->ForEach<PersistentIdComponent, NameComponent, TransformComponent>(
			[&](Entity entity, PersistentIdComponent& persistent, NameComponent& name, TransformComponent& transform) {
				output << "ENTITY " << persistent.value.high << ' ' << persistent.value.low << ' '
				       << std::quoted(name.name) << '\n';
				output << "TRANSFORM "
				       << transform.translation.x << ' ' << transform.translation.y << ' ' << transform.translation.z << ' '
				       << transform.rotation.x << ' ' << transform.rotation.y << ' ' << transform.rotation.z << ' ' << transform.rotation.w << ' '
				       << transform.scale.x << ' ' << transform.scale.y << ' ' << transform.scale.z << '\n';

				PersistentId parentId {};
				if(const auto* hierarchy = entity.GetComponent<HierarchyComponent>();
				   hierarchy != nullptr && hierarchy->parent.IsValid()) {
					if(const auto* parent = scene->GetComponent<PersistentIdComponent>(hierarchy->parent)) {
						parentId = parent->value;
					}
				}
				output << "PARENT " << parentId.high << ' ' << parentId.low << '\n';

				if(const auto* camera = entity.GetComponent<CameraComponent>()) {
					output << "CAMERA 1 " << camera->verticalFieldOfViewRadians << ' ' << camera->nearClip << ' '
					       << camera->farClip << ' ' << camera->primary << ' ' << camera->enabled << '\n';
				} else {
					output << "CAMERA 0\n";
				}
				if(const auto* primitive = entity.GetComponent<PrimitiveRenderComponent>()) {
					output << "PRIMITIVE 1 " << primitive->tint[0] << ' ' << primitive->tint[1] << ' '
					       << primitive->tint[2] << ' ' << primitive->tint[3] << ' ' << primitive->enabled << ' ' << primitive->nodeIndex << ' ' << primitive->staticLighting
                           << ' ' << std::quoted(primitive->meshAsset) << ' ' << std::quoted(primitive->baseColorTexture);
                    const auto& material = primitive->material;
                    output << ' ' << material.enabled << ' ' << material.slot;
                    for(float value : material.baseColor) output << ' ' << value;
                    output << ' ' << material.metallic << ' ' << material.roughness << ' ' << material.normalScale;
                    for(float value : material.emissive) output << ' ' << value;
                    output << ' ' << material.occlusionStrength << '\n';
				} else {
					output << "PRIMITIVE 0\n";
				}
				if(const auto* component = entity.GetComponent<DirectionalLightComponent>()) {
					const auto& l = component->light;
					output << "DIRECTIONAL_LIGHT 1 " << component->enabled << ' ' << l.direction[0] << ' ' << l.direction[1] << ' ' << l.direction[2]
						<< ' ' << l.color[0] << ' ' << l.color[1] << ' ' << l.color[2] << ' ' << l.intensity << ' ' << l.angularRadius << '\n';
				} else output << "DIRECTIONAL_LIGHT 0\n";
				output << "END_ENTITY\n";
			});
		output << "END_LEVEL\n";

		if(!output) {
			return std::unexpected(Error(ErrorCategory::FileSystem, kOpenFailed, "Failed while writing level file."));
		}
		return {};
	}

	Result<void> LevelSerializer::Load(const std::filesystem::path& path, Level& level) {
		std::ifstream input(path, std::ios::binary);
		input.imbue(std::locale::classic());
		if(!input) {
			return std::unexpected(Error(ErrorCategory::FileSystem, kOpenFailed, "Failed to open level file for reading."));
		}

		std::string token;
		uint32_t version = 0;
		std::string loadedName;
		if(!(input >> token >> version) || token != "NEXUS_LEVEL" || version < 1 || version > kFormatVersion ||
		   !(input >> token >> std::quoted(loadedName)) || token != "LEVEL") {
			return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Level header or version is invalid."));
		}

		std::optional<LevelLightingSettings> lighting;
		if(version>=5) {
			bool present=false;
			if(!(input >> token >> present) || token!="LIGHTING")
				return std::unexpected(Error(ErrorCategory::Scene,kInvalidFormat,"Level lighting record is missing."));
			if(present) {
				lighting.emplace();
				for(float& value:lighting->skyIrradiance) input >> value;
				input >> lighting->probes.intensity;
				for(float& value:lighting->probes.origin) input >> value;
				for(float& value:lighting->probes.spacing) input >> value;
				for(uint32_t& value:lighting->probes.counts) input >> value;
				input >> lighting->probes.raysPerProbe;
				if(!input || !lighting->IsValid()) return std::unexpected(Error(ErrorCategory::Scene,kInvalidFormat,"Level lighting values are invalid."));
			}
		}
		if(version>=6) {
			std::string imagePath;float intensity=1,rotation=0;
			if(!(input >> token >> std::quoted(imagePath) >> intensity >> rotation) || token!="ENVIRONMENT" ||
				!std::isfinite(intensity) || intensity<0 || !std::isfinite(rotation))
				return std::unexpected(Error(ErrorCategory::Scene,kInvalidFormat,"Invalid Level environment."));
			if(!imagePath.empty()) {
				if(!lighting) lighting.emplace();
				lighting->environment.imagePath=std::filesystem::path{std::u8string(reinterpret_cast<const char8_t*>(imagePath.data()),imagePath.size())};
				lighting->environment.intensity=intensity;lighting->environment.rotation=rotation;
			}
		}
		auto loadedScene = std::make_unique<Scene>();
		std::map<PersistentKey, EntityId> loadedEntities;
		std::vector<PendingParent> pendingParents;

		while(input >> token && token != "END_LEVEL") {
			if(token != "ENTITY") {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Expected ENTITY record."));
			}

			PersistentId persistent;
			std::string entityName;
			if(!(input >> persistent.high >> persistent.low >> std::quoted(entityName)) || !persistent.IsValid()) {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Entity persistent ID is invalid."));
			}
			if(loadedEntities.contains(MakeKey(persistent))) {
				return std::unexpected(Error(ErrorCategory::Scene, kDuplicatePersistentId, "Level contains duplicate persistent IDs."));
			}

			Entity entity = loadedScene->CreateEntity(std::move(entityName));
			if(entity.AddComponent<PersistentIdComponent>(PersistentIdComponent { persistent }) == nullptr) {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Failed to restore persistent ID component."));
			}
			loadedEntities.emplace(MakeKey(persistent), entity.GetId());

			auto* transform = entity.GetComponent<TransformComponent>();
			if(!(input >> token) || token != "TRANSFORM" || transform == nullptr ||
			   !(input >> transform->translation.x >> transform->translation.y >> transform->translation.z
			           >> transform->rotation.x >> transform->rotation.y >> transform->rotation.z >> transform->rotation.w
			           >> transform->scale.x >> transform->scale.y >> transform->scale.z)) {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Transform record is invalid."));
			}
			if(!IsFinite(*transform)) {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Transform contains non-finite values or a zero quaternion."));
			}

			PersistentId parent;
			if(!(input >> token >> parent.high >> parent.low) || token != "PARENT") {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Parent record is invalid."));
			}
			if(parent.IsValid()) {
				pendingParents.push_back({ entity.GetId(), MakeKey(parent) });
			}

			bool hasCamera = false;
			if(!(input >> token >> hasCamera) || token != "CAMERA") {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Camera record is invalid."));
			}
			if(hasCamera) {
				CameraComponent camera;
				if(!(input >> camera.verticalFieldOfViewRadians >> camera.nearClip >> camera.farClip >> camera.primary >> camera.enabled) ||
				   !IsValid(camera) ||
				   entity.AddComponent<CameraComponent>(camera) == nullptr) {
					return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Camera component is invalid."));
				}
			}

			bool hasPrimitive = false;
			if(!(input >> token >> hasPrimitive) || token != "PRIMITIVE") {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Primitive record is invalid."));
			}
			if(hasPrimitive) {
				PrimitiveRenderComponent primitive;
				if(!(input >> primitive.tint[0] >> primitive.tint[1] >> primitive.tint[2] >> primitive.tint[3] >> primitive.enabled) ||
				   (version >= 2 && !(input >> primitive.nodeIndex)) ||
				   (version >= 4 && !(input >> primitive.staticLighting)) ||
                   (version >= 7 && !(input >> std::quoted(primitive.meshAsset) >> std::quoted(primitive.baseColorTexture))) ||
                   (version >= 8 && !ReadMaterial(input, primitive.material)) ||
                   primitive.meshAsset.size() > 4096 || primitive.baseColorTexture.size() > 4096 ||
				   !IsValid(primitive) ||
				   entity.AddComponent<PrimitiveRenderComponent>(primitive) == nullptr) {
					return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Primitive component is invalid."));
				}
			}
			if(version >= 3) {
				bool hasLight = false;
				if(!(input >> token >> hasLight) || token != "DIRECTIONAL_LIGHT")
					return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Directional light record is invalid."));
				if(hasLight) {
					DirectionalLightComponent component;
					auto& l = component.light;
					if(!(input >> component.enabled >> l.direction[0] >> l.direction[1] >> l.direction[2] >> l.color[0] >> l.color[1] >> l.color[2]
						>> l.intensity >> l.angularRadius) || !component.IsValid() || !entity.AddComponent<DirectionalLightComponent>(component))
						return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Directional light component is invalid."));
				}
			}
			if(!(input >> token) || token != "END_ENTITY") {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Entity record is not terminated."));
			}
		}

		if(!input && token != "END_LEVEL") {
			return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Level file ended unexpectedly."));
		}
		if(input >> token) {
			return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Unexpected data after END_LEVEL."));
		}

		// 全Entity生成後に永続IDを新しいRuntime EntityIdへ変換し、保存順への依存をなくす。
		for(const PendingParent& pending : pendingParents) {
			const auto parent = loadedEntities.find(pending.parent);
			if(parent == loadedEntities.end()) {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Parent persistent ID was not found."));
			}
			Entity child = loadedScene->GetEntity(pending.child);
			if(child.AddComponent<HierarchyComponent>(HierarchyComponent { parent->second }) == nullptr) {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidFormat, "Failed to restore hierarchy component."));
			}
		}

		// 参照先だけでなく循環も検証し、計算済みMatrixを復元してから公開する。
		TransformSystem transforms;
		auto transformResult = transforms.Update(*loadedScene);
		if(!transformResult) return transformResult;

		// 完全に検証できたWorldだけを公開するため、失敗時は既存Levelを変更しない。
		level.scene_ = std::move(loadedScene);
		level.name_ = std::move(loadedName);
		level.lighting_ = std::move(lighting);
		level.state_ = LevelState::Loaded;
		return {};
	}

} // namespace NexusEngine
