#include "EditorSession.h"

#include "Runtime/Scene/Components/TransformComponent.h"


NexusEngine::Result<void> NexusEngine::EditorSession::Create() {
	auto document = std::make_unique<LevelDocument>();
	auto result = document->Create("EditorLevel");
	if(!result) return result;
	Close();
	levelDocument_ = std::move(document);
	return {};
}

void NexusEngine::EditorSession::Close() noexcept {
	selected_ = {};
	dirty_	  = false;
	levelDocument_.reset();
}

void NexusEngine::EditorSession::Select(PersistentId id) {
	auto* level = GetLevel();
	selected_ = level && level->FindEntity(id).IsAlive() ? id : PersistentId{};
}

NexusEngine::Entity NexusEngine::EditorSession::GetSelectedEntity() {
	auto* level = GetLevel();
	return level ? level->FindEntity(selected_) : Entity{};
}

bool NexusEngine::EditorSession::SetTransform(PersistentId id, Vector3 translation, Quaternion rotation, Vector3 scale) {
	const auto finite = [](Vector3 value) {
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	};
	const float lengthSquared = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
	if(!finite(translation) || !finite(scale) || !std::isfinite(lengthSquared) || lengthSquared < 1.0e-12f) return false;
	auto* level = GetLevel();
	if(level == nullptr) return false;
	auto  entity	= level->FindEntity(id);
	auto* transform = entity.GetComponent<TransformComponent>();
	if(transform == nullptr) return false;
	const float inverseLength = 1.0f / std::sqrt(lengthSquared);
	rotation.x *= inverseLength;
	rotation.y *= inverseLength;
	rotation.z *= inverseLength;
	rotation.w *= inverseLength;
	transform->translation = translation;
	transform->rotation	   = rotation;
	transform->scale	   = scale;
	dirty_				   = true;
	return true;
}
