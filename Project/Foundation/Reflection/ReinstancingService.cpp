#include "ReinstancingService.h"
#include "ReflectionArchive.h"
#include <algorithm>
#include <fstream>

namespace NexusEngine {
ReinstancingService& ReinstancingService::Get() { static ReinstancingService service; return service; }
void ReinstancingService::Register(Participant participant) {
    if(prepared_ || !participant.context || !participant.capture || !participant.release || !participant.restore)
        throw std::logic_error("Invalid reinstancing participant");
    for(const auto& current : participants_) {
        if(current.id == participant.id || current.context == participant.context)
            throw std::logic_error("Duplicate reinstancing participant");
    }
    participants_.push_back(std::move(participant));
}
void ReinstancingService::Unregister(void* context) noexcept {
    std::erase_if(participants_, [context](const auto& value) { return value.context == context; });
}
void ReinstancingService::ProtectFile(std::filesystem::path path) {
    std::ifstream input(path, std::ios::binary);
    if(!input) throw std::runtime_error("Cannot read protected live coding header");
    protectedFiles_[std::move(path)] = std::string(std::istreambuf_iterator<char>(input), {});
}
bool ReinstancingService::Prepare() noexcept {
    if(prepared_) return false;
    error_.clear();
    try {
        for(const auto& [path, original] : protectedFiles_) {
            std::ifstream input(path, std::ios::binary);
            if(!input || std::string(std::istreambuf_iterator<char>(input), {}) != original)
                throw std::runtime_error("Stable host header changed; save and rebuild/restart: " + path.string());
        }
        ReflectionValue::Object snapshots;
        for(const auto& participant : participants_) snapshots.emplace(participant.id, participant.capture(participant.context));
        snapshot_ = ReflectionValue(std::move(snapshots));
        if(!recoveryPath_.empty() && !participants_.empty()) {
            std::filesystem::create_directories(recoveryPath_.parent_path());
            const auto pending = std::filesystem::path(recoveryPath_.wstring() + L".pending");
            {
                std::ofstream output(pending, std::ios::binary | std::ios::trunc);
                WriteReflectionArchive(output, snapshot_); output.flush();
                if(!output) throw std::runtime_error("Cannot flush reinstancing recovery archive");
            }
            // 前回の回復ファイルは、新しい保存が完了するまで消さない。
            std::error_code ignored;
            std::filesystem::remove(recoveryPath_, ignored);
            std::filesystem::rename(pending, recoveryPath_);
        }
        prepared_ = true;
        return true;
    } catch(const std::exception& exception) { error_ = exception.what(); }
    catch(...) { error_ = "Reinstancing capture failed"; }
    snapshot_ = {};
    return false;
}
bool ReinstancingService::BeforePatch() noexcept {
    if(!prepared_ || released_) { error_ = "Unexpected reinstancing pre-patch hook"; return false; }
    for(auto it = participants_.rbegin(); it != participants_.rend(); ++it) it->release(it->context);
    released_ = true;
    return true;
}
void ReinstancingService::AfterPatch() noexcept {
    if(!released_ || !error_.empty()) return;
    try {
        for(const auto& participant : participants_) participant.restore(participant.context, snapshot_.At(participant.id));
        released_ = false;
        ++generation_;
    } catch(const std::exception& exception) { error_ = exception.what(); }
    catch(...) { error_ = "Reinstancing restore failed"; }
}
void ReinstancingService::FinishAttempt() {
    // コンパイル失敗でフックが呼ばれなかった場合も、旧オブジェクトをそのまま利用する。
    prepared_ = false;
    if(released_ || !error_.empty()) throw std::runtime_error("Reinstancing failed; recovery archive retained: " + error_);
    snapshot_ = {};
}
}
