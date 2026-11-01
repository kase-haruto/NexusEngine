#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * Process
 * - Unicode引数と作業Directoryを指定して子プロセスを所有する
 * - UI、Project、Engine選択、ビルド方針は知らない
 *---------------------------------------------------------------------------------------*/
class Process final {
public:
    Process() = default;
    ~Process();
    Process(Process&& other) noexcept;
    Process& operator=(Process&& other) noexcept;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    /** \brief shellを介さず起動する。引数は個別の値として渡す */
    [[nodiscard]] static Result<Process> Start(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
                                             const std::filesystem::path& directory, bool hidden = false,
                                             const std::filesystem::path& log = {}, bool ownTree = false);
    /** \brief 終了済みなら終了コードを返す。実行中ならnullopt */
    [[nodiscard]] Result<std::optional<uint32_t>> Poll() const;
    /** \brief このオブジェクトが起動したプロセスだけを中止する */
    [[nodiscard]] Result<void> Terminate(uint32_t exitCode = 1);
    /** \brief 所有するGUI子へ通常の終了要求を送る。強制終了はしない */
    [[nodiscard]] Result<void> RequestClose();
    uint32_t Id() const noexcept { return id_; }
    /** \brief Windowsのargv規則で一つの引数を引用する */
    static std::wstring QuoteArgument(const std::wstring& argument);
private:
    void* handle_ = nullptr; //< 子プロセスhandleのみを所有。破棄では子を終了しない
    uint32_t id_ = 0; //< 起動時のPID
    void* job_ = nullptr; //< ビルド時だけ子孫の寿命も所有するJob
};
}
