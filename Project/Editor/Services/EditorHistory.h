#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * EditorHistory
 * - プロパティと構造編集を同じ時系列で管理する
 * - 保存位置は一意な状態番号で識別し、Undo分岐後も誤ってcleanにしない
 *---------------------------------------------------------------------------------------*/
class EditorHistory final {
public:
    struct Operation {
        std::string label; //< 操作名の翻訳キー
        std::function<bool()> undo, redo; //< 成功した場合だけ履歴位置を進める
        uint64_t before = 0, after = 0; //< 分岐を区別する状態番号
    };
    EditorHistory() { operations_.reserve(256); }
    /** \brief Document破棄前に借用参照を持つ操作を解放する */
    void Reset() { operations_.clear(); cursor_ = 0; current_ = saved_ = 0; ++revision_; }
    /** \brief 適用済みの操作を追加し、Redo枝を破棄する */
    void Record(Operation operation) {
        operation.before = current_; operation.after = next_++;
        operations_.erase(operations_.begin() + static_cast<std::ptrdiff_t>(cursor_), operations_.end());
        if(operations_.size() == 256) { operations_.erase(operations_.begin()); --cursor_; }
        current_ = operation.after;
        operations_.push_back(std::move(operation)); cursor_ = operations_.size(); ++revision_;
    }
    /** \brief 直前の操作を取り消す。失敗時は位置を変えない */
    bool Undo() {
        if(!CanUndo() || !operations_[cursor_ - 1].undo()) return false;
        current_ = operations_[--cursor_].before; ++revision_; return true;
    }
    /** \brief 取り消した操作を再適用する */
    bool Redo() {
        if(!CanRedo() || !operations_[cursor_].redo()) return false;
        current_ = operations_[cursor_++].after; ++revision_; return true;
    }
    void MarkSaved() noexcept { saved_ = current_; }
    bool IsDirty() const noexcept { return current_ != saved_; }
    bool CanUndo() const noexcept { return cursor_ > 0; }
    bool CanRedo() const noexcept { return cursor_ < operations_.size(); }
    size_t Revision() const noexcept { return revision_; }
private:
    std::vector<Operation> operations_; //< 最大256件。closureは値snapshotを所有する
    size_t cursor_ = 0, revision_ = 0; //< 履歴位置とcache無効化番号
    uint64_t current_ = 0, saved_ = 0, next_ = 1; //< nextはResetでも再利用しない
};
}
