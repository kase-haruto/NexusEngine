#pragma once
#include "Core/Framework/FrameworkUpdateClient.h"
#include <Windows.h>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * BoundedFrameClient
 * - 既存Applicationへ更新を委譲し、指定frameの描画準備後に通常終了を要求する
 * - ApplicationやGraphicsを所有せず、frame数0では終了を制限しない
 *---------------------------------------------------------------------------------------*/
class BoundedFrameClient final : public IFrameworkUpdateClient {
public:
    /** \brief 対象ApplicationはこのadapterとFrameworkより長く保持する */
    BoundedFrameClient(IFrameworkUpdateClient& client, uint32_t limit) : client_(client), limit_(limit) {}
    void SetFrameInput(const InputFrame& input) override { client_.SetFrameInput(input); }
    Result<void> AttachGraphics(GraphicsSystem& graphics,IGraphicsRenderer* renderer) override { return client_.AttachGraphics(graphics,renderer); }
    void DetachGraphics() noexcept override { client_.DetachGraphics(); }
    [[nodiscard]] Result<void> Update(float deltaTime) override { return client_.Update(deltaTime); }
    [[nodiscard]] Result<void> PrepareRender(uint32_t width, uint32_t height) override {
        auto result = client_.PrepareRender(width,height);
        if(result && limit_ && ++frames_ >= limit_) PostQuitMessage(0);
        return result;
    }
    [[nodiscard]] uint32_t Frames() const noexcept { return frames_; }
private:
    IFrameworkUpdateClient& client_; //< 非所有。通常の更新・描画抽出をそのまま利用する
    uint32_t limit_ = 0; //< 0は通常の無制限実行
    uint32_t frames_ = 0; //< 成功した描画準備の回数。描画失敗はFrameworkの終了codeで判別する
};
}
