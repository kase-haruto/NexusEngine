#include "ViewportSession.h"
#include <bit>
#include <cmath>

namespace NexusEngine {
bool ViewportSession::Attach(IPC::IRuntimeConnection& connection, IViewportFrameTransport& frames, UI::ITextureProvider& textures) {
    if(connection_ || frame_ || localTextures_) return false;
    connection_ = &connection; frames_ = &frames; textures_ = &textures; return true;
}
bool ViewportSession::AttachLocalPreview(UI::ITextureProvider& textures, UI::TextureHandle texture,
    std::function<bool(uint32_t, uint32_t)> resize) {
    if(connection_ || frame_ || localTextures_ || !texture.IsValid() || !resize) return false;
    localTextures_ = &textures; localTexture_ = texture; localResize_ = std::move(resize); return true;
}
void ViewportSession::BeginFrame() {
    if(!connection_) return;
    connection_->Poll();
    if(State() == IPC::RuntimeState::Disconnected || State() == IPC::RuntimeState::Error) return;
    if(!frame_) frame_ = frames_->Acquire();
}
bool ViewportSession::CompleteSubmission() {
    if(!frame_) return true;
    if(!frames_->Release(*frame_)) return false;
    frame_.reset(); return true;
}
void ViewportSession::Detach() noexcept {
    if(frames_) frames_->Disconnect();
    frame_.reset(); connection_ = nullptr; frames_ = nullptr; textures_ = nullptr;
    width_ = height_ = 0; generation_ = 0;
    localTextures_ = nullptr; localTexture_ = {}; localResize_ = {};
    cameraPreviewTexture_ = {}; cameraPreviewVisible_ = false;
}
bool ViewportSession::Resize(uint32_t width, uint32_t height) {
    if(localTextures_) {
        if(width > 16384 || height > 16384 || ((width == 0) != (height == 0))) return false;
        if(width == width_ && height == height_) return true;
        if(!localResize_(width, height)) return false;
        width_ = width; height_ = height; return true;
    }
    if(!connection_ || !frames_ || State() == IPC::RuntimeState::Disconnected || State() == IPC::RuntimeState::Error ||
        width > 16384 || height > 16384 || ((width == 0) != (height == 0))) return false;
    if(width == width_ && height == height_) return true;
    IPC::Message m; m.type = IPC::MessageType::Resize;
    m.payload = IPC::EncodeResize({1, generation_ + 1, width, height});
    if(!connection_->Submit(std::move(m))) return false;
    // Texture再生成は通知を受けたGraphics hostが担当する。PanelはGPUへ触れない。
    width_ = width; height_ = height; ++generation_; return true;
}
bool ViewportSession::Input(uint32_t kind, uint32_t code, float x, float y) {
    if(!connection_ || !std::isfinite(x) || !std::isfinite(y)) return false;
    IPC::WireWriter writer; writer.U64(1); writer.U32(kind); writer.U32(code);
    writer.U32(std::bit_cast<uint32_t>(x)); writer.U32(std::bit_cast<uint32_t>(y));
    IPC::Message m; m.type = IPC::MessageType::Input; m.payload = std::move(writer.data);
    return connection_->Submit(std::move(m));
}
bool ViewportSession::Select(PersistentId id) {
    if(!connection_) return false;
    IPC::WireWriter writer; writer.U32(id.IsValid() ? 1 : 0);
    if(id.IsValid()) { writer.U64(id.high); writer.U64(id.low); }
    IPC::Message m; m.type = IPC::MessageType::Selection; m.payload = std::move(writer.data);
    return connection_->Submit(std::move(m));
}
std::optional<UI::ResolvedTexture> ViewportSession::Image() const {
    if(localTextures_) return localTextures_->Resolve(localTexture_);
    if(!frame_ || !textures_) return {};
    return textures_->Resolve({frame_->texture});
}
}
