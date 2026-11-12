#include "EditorSession.h"
#include "Foundation/Logging/Logger.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"

/** \brief CLR非依存のMetadataを検証してからPlay設定へ接続する。失敗時は旧Catalogを維持する */
bool NexusEngine::EditorSession::SetCSharpRuntime(CSharpRuntimeSettings settings) {
    std::shared_ptr<const ManagedScriptMetadata> metadata;
    if(!settings.gameAssembly.empty()) {
        auto file = settings.gameAssembly; file.replace_extension(".metadata.json");
        auto loaded = ManagedScriptMetadata::Load(file);
        if(!loaded) {
            NEXUS_LOG_ERROR("Editor", "Managed metadata: " + loaded.error().GetMessageText() + " Rebuild GameScripts before opening the Editor.");
            return false;
        }
        metadata = std::move(*loaded);
    }
    // Runtimeが設定を受け入れた場合だけCatalogを置換し、失敗時は現在の編集状態を保つ。
    if(!playRuntime_.SetCSharpRuntime(std::move(settings))) return false;
    managedMetadata_ = std::move(metadata); return true;
}

bool NexusEngine::EditorSession::SetCameraPreview(PersistentId id) {
	auto* level = GetLevel();
	auto entity = level ? level->FindEntity(id) : Entity{};
	if(!entity.GetComponent<CameraComponent>() || !entity.GetComponent<TransformComponent>()) return false;
	previewCamera_ = id;
	return true;
}

NexusEngine::Result<NexusEngine::RenderCamera> NexusEngine::EditorSession::ResolveCameraPreview(float aspect) {
	auto* level = GetPreviewLevel();
	auto entity = level ? level->FindEntity(previewCamera_) : Entity{};
	const auto* camera = entity.GetComponent<CameraComponent>();
	const auto* transform = entity.GetComponent<TransformComponent>();
	if(!camera || !transform) {
		// 対象が削除された場合は解除し、別のカメラへ自動的に切り替えない。
		ClearCameraPreview();
		return std::unexpected(Error(ErrorCategory::Scene, 20, "Preview camera is no longer available."));
	}
	if(!std::isfinite(aspect) || aspect <= 0 || !std::isfinite(camera->verticalFieldOfViewRadians) ||
		camera->verticalFieldOfViewRadians <= 0 || camera->verticalFieldOfViewRadians >= 3.14159265f ||
		!std::isfinite(camera->nearClip) || !std::isfinite(camera->farClip) ||
		camera->nearClip <= 0 || camera->farClip <= camera->nearClip)
		return std::unexpected(Error(ErrorCategory::Scene, 21, "Preview camera projection settings are invalid."));
	const auto view = TryInverse(transform->worldMatrix);
	if(!view) return std::unexpected(Error(ErrorCategory::Scene, 22, "Preview camera transform is not invertible."));
	const auto projection = MakePerspectiveFovMatrix(camera->verticalFieldOfViewRadians, aspect, camera->nearClip, camera->farClip);
	const auto local = entity.GetId();
	return RenderCamera{{(static_cast<uint64_t>(local.generation) << 32) | local.index}, *view, projection, Multiply(*view, projection)};
}

bool NexusEngine::EditorSession::SetStaticLighting(PersistentId id, bool enabled) {
	auto* level=GetLevel();
	if(!level) return false;
	auto entity=level->FindEntity(id);
	auto* primitive=entity.GetComponent<PrimitiveRenderComponent>();
	if(!primitive) return false;
	primitive->staticLighting=enabled;
	dirty_=true;
	return true;
}


NexusEngine::Result<void> NexusEngine::EditorSession::Create() {
	auto document = std::make_unique<LevelDocument>(reflection_);
	auto result = document->Create("EditorLevel");
	if(!result) return result;
    if(!document->GetLevel()->SetPhysicsModelSource(physicsModels_)) return std::unexpected(Error(ErrorCategory::Resource,260,"Cannot configure document collision assets."));
    if(!resourceRoot_.empty()) { auto resources=document->GetLevel()->SetResourceRoot(resourceRoot_); if(!resources) return resources; }
	Close();
	levelDocument_ = std::move(document);
	return {};
}

void NexusEngine::EditorSession::Close() noexcept {
	StopPlay();
	selection_.Clear();
	ClearCameraPreview(); diagnostics_.Clear();
	++generation_;
	dirty_	  = false;
	levelDocument_.reset();
}

bool NexusEngine::EditorSession::StartPlay() {
	playError_.reset();
	if(IsPlaying() || !GetLevel() || !GetLevel()->GetScene()) return false;
    ++playEpoch_;
	auto scene = GetLevel()->GetScene()->Clone();
	if(!scene) return false;
	auto level = std::make_unique<Level>(GetLevel()->GetName(), reflection_);
    if(!level->SetPhysicsModelSource(GetLevel()->PhysicsModels())) return false;
    if(!GetLevel()->ResourceRoot().empty()) {
        auto resources=level->SetResourceRoot(GetLevel()->ResourceRoot());
        if(!resources) { playError_=resources.error(); return false; }
    }
	level->Lighting() = GetLevel()->Lighting();
	level->ExchangeScene(std::move(scene));
	auto result = playRuntime_.Start(std::move(level));
	if(!result) { playError_ = std::move(result.error()); return false; }
	return true;
}
NexusEngine::Result<void> NexusEngine::EditorSession::ReloadCSharp(const std::filesystem::path& assembly) {
    auto file = assembly; file.replace_extension(".metadata.json");
    auto metadata = ManagedScriptMetadata::Load(file); if(!metadata) return std::unexpected(metadata.error());
    auto result = playRuntime_.ReloadCSharp(assembly);
    if(result) managedMetadata_ = std::move(*metadata);
    return result;
}

NexusEngine::Result<void> NexusEngine::EditorSession::UpdatePreview(float deltaTime) {
	if(IsPlaying()) return playRuntime_.Update(deltaTime);
	if(!std::isfinite(deltaTime) || deltaTime < 0)
		return std::unexpected(Error(ErrorCategory::Framework, 43, "Preview delta time must be finite and non-negative."));
	return GetLevel() ? GetLevel()->Update(0.0f) : Result<void>{};
}

NexusEngine::Result<void> NexusEngine::EditorSession::Open(const std::filesystem::path& path) {
	auto document = std::make_unique<LevelDocument>(reflection_);
	auto result = document->Open(path);
	if(!result) return result;
    if(!document->GetLevel()->SetPhysicsModelSource(physicsModels_)) return std::unexpected(Error(ErrorCategory::Resource,260,"Cannot configure document collision assets."));
    if(!resourceRoot_.empty()) { auto resources=document->GetLevel()->SetResourceRoot(resourceRoot_); if(!resources) return resources; }
	Close();
	levelDocument_ = std::move(document);
	return {};
}

void NexusEngine::EditorSession::Select(PersistentId id) {
	auto* level = GetLevel();
	selection_.Select(level && level->FindEntity(id).IsAlive() ? id : PersistentId{});
}

NexusEngine::Entity NexusEngine::EditorSession::GetSelectedEntity() {
	auto* level = GetLevel();
	return level ? level->FindEntity(selection_.Primary()) : Entity{};
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
