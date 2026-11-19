#pragma once
#include "Runtime/Animation/AnimationDocument.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include <variant>
#include <optional>

namespace NexusEngine {
class EditorSession;
/** ファイルとUndoだけを所有する。プレビューはEditorSessionの複製Worldへ適用する。 */
class AnimationStudioDocument final {
public:
    using Value = std::variant<AnimationClipDocument, AnimatorDocument, TimelineDocument>;
    void Initialize(const std::filesystem::path& root);
    void New(int kind);
    Result<void> Open(const std::filesystem::path& file);
    Result<void> Save();
    void RefreshLocation();
    void Changed(const Value& before, bool continuous);
    void EndEdit();
    void Undo();
    void Redo();
    Result<void> ImportModel(const std::filesystem::path& model);
    Result<void> MakeEditable();
    Result<void> Attach(EditorSession& session);
    Result<void> Preview(EditorSession& session, float deltaTime);
    void StopPreview(EditorSession& session);
    std::vector<std::string> Assets(const std::string& extension) const;
    void Report(const Result<void>& result);
    Value value = AnimationClipDocument{};
    std::string reference = "Animations/NewAnimation.nxanim", status;
    std::filesystem::path root;
    bool dirty = false, error = false, focused = false, preview = false, playing = false, recording = false;
    float time = 0;
    int selected = 0, key = 0, selectedTransition = -1;
    size_t revision = 1, savedRevision = 0;
    std::optional<std::filesystem::path> pendingOpen;
    bool exitRequested = false, exitReady = false;
private:
    std::vector<Value> undo_, redo_;
    bool editing_ = false;
    size_t previewRevision_ = 0;
    PersistentId previewEntity_;
    std::string previewReference_;
    size_t previewKind_ = 0;
    std::string identity_, identityPath_;
};
}
