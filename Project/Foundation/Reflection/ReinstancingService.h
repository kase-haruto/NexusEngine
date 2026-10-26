#pragma once

#include "ReflectionValue.h"
#include <filesystem>

namespace NexusEngine {
// パッチ適用中も残るホスト。登録先の context とこの型のレイアウトはセッション中固定する。
class ReinstancingService final {
public:
    struct Participant {
        std::string id;
        void* context = nullptr;
        ReflectionValue (*capture)(void*) = nullptr;
        void (*release)(void*) noexcept = nullptr;
        void (*restore)(void*, const ReflectionValue&) = nullptr;
    };
    static ReinstancingService& Get();
    void Register(Participant participant);
    void Unregister(void* context) noexcept;
    void SetRecoveryPath(std::filesystem::path path) { recoveryPath_ = std::move(path); }
    void ProtectFile(std::filesystem::path path);
    // SDK の Reload より前に保存を完了し、保存失敗時は旧インスタンスを維持する。
    bool Prepare() noexcept;
    bool BeforePatch() noexcept;
    void AfterPatch() noexcept;
    void FinishAttempt();
    const std::string& Error() const noexcept { return error_; }
    size_t Generation() const noexcept { return generation_; }
private:
    std::vector<Participant> participants_;
    std::map<std::filesystem::path, std::string> protectedFiles_;
    ReflectionValue snapshot_;
    std::filesystem::path recoveryPath_;
    std::string error_;
    size_t generation_ = 0;
    bool prepared_ = false;
    bool released_ = false;
};
}
