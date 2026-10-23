#pragma once
#include "ILogSink.h"
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <optional>

namespace NexusEngine {
struct OwnedLogRecord {
    LogLevel level; //< ログ重要度
    std::chrono::system_clock::time_point timestamp; //< 発生時刻
    std::string category, message, source; //< Loggerの一時string_viewからコピーする
};
/*-----------------------------------------------------------------------------------------
 * BufferedLogBuffer / BufferedLogSink
 * - スレッド間でログ値を受け渡す上限付きバッファ
 * - UIやEditorを参照せずLoggerの既存Sink契約を実装する
 *---------------------------------------------------------------------------------------*/
class BufferedLogBuffer final {
public:
    /** \brief 件数と各文字列長を制限して追加する */
    void Append(const LogRecord& record) {
        OwnedLogRecord owned {record.level, record.timestamp, std::string(record.category.substr(0, 256)),
            std::string(record.message.substr(0, 8192)), std::string(record.location.file_name()).substr(0, 512)};
        owned.source += ":" + std::to_string(record.location.line());
        std::lock_guard lock(mutex_);
        if(records_.size() == 2048) records_.pop_front();
        records_.push_back(std::move(owned));
        ++revision_;
    }
    /** \brief mutexを解放した値snapshotをUIへ渡す */
    std::vector<OwnedLogRecord> Snapshot() const { std::lock_guard lock(mutex_); return {records_.begin(), records_.end()}; }
    /** \brief 無更新時の文字列コピーを省く。番号はsnapshotと同じlockで取得する */
    std::optional<std::vector<OwnedLogRecord>> SnapshotIfChanged(uint64_t& revision) const {
        std::lock_guard lock(mutex_);
        if(revision == revision_) return {};
        auto copy = std::vector<OwnedLogRecord>(records_.begin(), records_.end());
        revision = revision_; return copy;
    }
    void Clear() { std::lock_guard lock(mutex_); records_.clear(); ++revision_; }
private:
    mutable std::mutex mutex_; //< 生産者とUI取得の排他
    std::deque<OwnedLogRecord> records_; //< 最大2048件、約18MiB以内の文字列
    uint64_t revision_ = 1; //< 追加とClearで増やす更新番号
};
class BufferedLogSink final : public ILogSink {
public:
    explicit BufferedLogSink(std::shared_ptr<BufferedLogBuffer> buffer) : buffer_(std::move(buffer)) {}
    void Write(const LogRecord& record) noexcept override { try { buffer_->Append(record); } catch(...) {} }
private:
    std::shared_ptr<BufferedLogBuffer> buffer_; //< Loggerと画面で共有。画面終了後もdanglingしない
};
}
