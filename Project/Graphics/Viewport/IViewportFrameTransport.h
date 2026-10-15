#pragma once
#include "Foundation/IPC/RuntimeProtocol.h"

namespace NexusEngine {
struct ViewportFrame {
    uint64_t texture = 0; //< Graphics側のopaque texture token、API型は公開しない
    uint32_t width = 0, height = 0; //< 画像サイズ
    IPC::FrameMessage synchronization; //< resource世代と同期値
};
/*-----------------------------------------------------------------------------------------
 * IViewportFrameTransport
 * - GPU Frame転送の取得と解放をGraphics実装へ委譲する
 * - AcquireはGPU完了をCPUで待たず、描画queueへの依存を登録する
 *---------------------------------------------------------------------------------------*/
class IViewportFrameTransport {
public:
    virtual ~IViewportFrameTransport() = default;
    /** \brief 完成したframeのleaseを取得する。未着時は空 */
    virtual std::optional<ViewportFrame> Acquire() = 0;
    /** \brief Editorのsampling command提出後にconsumer Fenceをsignalする */
    virtual bool Release(const ViewportFrame& frame) = 0;
    /** \brief 新世代を予約する。旧世代はconsumer完了まで保持する */
    virtual bool Resize(uint32_t width, uint32_t height) = 0;
    /** \brief 接続を切る。GPU参照中の資源を即時解放してはいけない */
    virtual void Disconnect() noexcept = 0;
};
}
