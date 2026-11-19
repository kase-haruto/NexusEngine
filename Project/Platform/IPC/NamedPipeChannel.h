#pragma once
#include "Foundation/IPC/RuntimeProtocol.h"
#include <memory>

namespace NexusEngine::IPC {
/** \brief ローカル同一ユーザーの非同期message pipe。切断時のqueueを破棄し、再接続を待つ。 */
class NamedPipeChannel final : public IControlChannel {
public:
    NamedPipeChannel(std::string endpoint, bool server);
    ~NamedPipeChannel() override;
    NamedPipeChannel(const NamedPipeChannel&) = delete;
    NamedPipeChannel& operator=(const NamedPipeChannel&) = delete;
    bool Send(const Message& message) override;
    std::optional<Message> TryReceive() override;
    bool IsOpen() const noexcept override;
    void Close() noexcept override;
    uint64_t Generation() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
