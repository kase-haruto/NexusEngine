#pragma once

#include <memory>

namespace NexusEngine {
    // SDK とその ABI を公開ヘッダから隠蔽する。呼び出しはメインスレッドに限定する。
    class LiveCodingAgent final {
    public:
        explicit LiveCodingAgent(bool enabled);
        ~LiveCodingAgent();
        LiveCodingAgent(const LiveCodingAgent&) = delete;
        LiveCodingAgent& operator=(const LiveCodingAgent&) = delete;

        [[nodiscard]] bool WantsReload();
        void ScheduleReload();
        void Reload();

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
