#pragma once
#include "Platform/IPC/NamedPipeChannel.h"
#include "CurveSerializer.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include <future>

namespace NexusEngine {
class Level;
enum class CurvePreviewAction { Observe, Play, Pause, Stop, Seek };
struct RemoteCurveState {
    bool connected = false, found = false, playing = false;
    double input = 0;
    std::string diagnostic;
};
/** \brief Editor側。未保存Curveのsnapshotをchunk送信し、Runtimeから現在Xを受け取る。 */
class CurvePreviewSender final {
public:
    explicit CurvePreviewSender(std::string endpoint);
    void SetDraft(std::string reference, const FloatCurve* curve);
    void Select(PersistentId entity);
    bool Command(CurvePreviewAction action, double input = 0);
    void Poll();
    const RemoteCurveState& State() const noexcept;
private:
    IPC::NamedPipeChannel channel_;
    std::string reference_, text_;
    PersistentId entity_;
    uint64_t revision_ = 1, generation_ = 0;
    size_t offset_ = 0;
    int stage_ = 0;
    RemoteCurveState state_;
};
/** \brief Runtime側。受信/parseとWorldへの反映を分離し、完了snapshotだけをmain threadで交換する。 */
class CurvePreviewReceiver final {
public:
    explicit CurvePreviewReceiver(std::string endpoint);
    void Poll(Level& level);
private:
    IPC::NamedPipeChannel channel_;
    std::string reference_, stagingReference_, text_, appliedReference_, error_;
    PersistentId entity_;
    uint64_t generation_ = 0, revision_ = 0, pendingRevision_ = 0;
    size_t expected_ = 0;
    std::future<Result<FloatCurve>> parsed_;
    std::string pendingReference_;
    void Playback(Level& level, CurvePreviewAction action, double input);
    RemoteCurveState ReadState(Level& level);
};
}
