#include "EditorSession.h"



NexusEngine::Result<void> NexusEngine::EditorSession::Create() {
	Close();
	return level_.LoadEmpty();
}

void NexusEngine::EditorSession::Close() noexcept {
	selected_ = {};
	dirty_	  = false;
	level_.Unload();
}

void NexusEngine::EditorSession::Select(PersistentId id) {
	selected_ = level_.FindEntity(id).IsAlive() ? id : PersistentId{};
}

bool NexusEngine::EditorSession::SetTransform(PersistentId id, Vector3 translation, Quaternion rotation, Vector3 scale) {
	const auto finite = [](Vector3 value) {
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	};
	const float lengthSquared = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
	if(!finite(translation) || !finite(scale) || !std::isfinite(lengthSquared) || lengthSquared < 1.0e-12f) return false;
	auto  entity	= level_.FindEntity(id);
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