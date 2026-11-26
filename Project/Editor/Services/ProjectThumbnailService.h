#pragma once
#include "Runtime/Application/ApplicationPaths.h"
#include <atomic>
#include <chrono>
#include <cstdint>

namespace NexusEngine {
class GraphicsSystem;

/*-----------------------------------------------------------------------------------------
 * ProjectThumbnailService
 * - Editorの保存・終了と初期Template撮影を、SDR取得とPNG保存に結び付ける
 *---------------------------------------------------------------------------------------*/
class ProjectThumbnailService final {
public:
    /** \brief Projectと、初回だけ取得可能なTemplate画像の保存先を解決する */
    void Initialize(const ApplicationPaths& paths, const std::filesystem::path& initialLevel);
    /** \brief このEditorにProject画像の有効な保存先があるか */
    bool Enabled() const noexcept;
    /** \brief 保存Sceneのコピーを予約済み、または待機期限に達したか */
    bool ReadyToSwitchScene() noexcept;
    /** \brief 文書保存の世代を確認し、連続した要求を集約する */
    void ObserveSave(uint64_t revision) noexcept;
    /** \brief 終了要求を受け付け、撮影と保存の待機時間を制限する */
    bool ReadyToExit(bool requested) noexcept;
    /** \brief 描画可能な編集Sceneで次のSDRフレームを予約する */
    bool PrepareCapture(GraphicsSystem& graphics, bool sceneReady);
    /** \brief 初期状態の撮影を予約するまでEditor入力を保留する */
    bool WaitingForInitialFrame() const noexcept;
private:
    using Clock = std::chrono::steady_clock;
    enum class Capture { None, Project, Default };
    enum class WriteState { Running, Succeeded, Failed };
    /** \brief 完了したReadbackをCPU専用のWorkerへ引き渡す */
    void StartWrite(struct DecodedImage image);
    Clock::time_point saveDeadline_{}; //< 保存を伴うScene切替の待機期限
    std::filesystem::path projectFile_, defaultFile_; //< 出力の用途ごとの独立したパス
    std::shared_ptr<std::atomic<WriteState>> writeDone_; //< Workerの完了通知。Editorオブジェクトは共有しない
    Clock::time_point initialDeadline_{}, exitDeadline_{}; //< 描画不能・I/O失敗でも操作を回復する期限
    uint64_t saveRevision_ = 0; //< 最後に確認した保存世代
    Capture inFlight_ = Capture::None; //< Readback結果の出力種別
    bool projectPending_ = false, defaultPending_ = false; //< 未記録の要求
};
}
