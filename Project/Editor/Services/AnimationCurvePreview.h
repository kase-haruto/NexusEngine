#pragma once
#include <functional>
#include <memory>
namespace NexusEngine {
class CurvePreviewSender;
class CurveEditDocument;
class AnimationStudioDocument;
class EditorSession;
/** \brief 汎用preview callbackへAnimation側の単位と操作を接続する。 */
void ConnectAnimationCurvePreview(CurveEditDocument& curve, AnimationStudioDocument& studio, EditorSession& session);
void ConnectRemoteCurvePreview(CurveEditDocument& curve, std::function<std::shared_ptr<CurvePreviewSender>()> sender);
}
