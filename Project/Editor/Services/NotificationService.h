#pragma once
#include <chrono>
#include <deque>
#include <string>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * NotificationService
 * - UI非依存の期限付き通知を上限付きで保持する
 *---------------------------------------------------------------------------------------*/
class NotificationService final {
public:
    struct Item { std::string text; bool error; std::chrono::steady_clock::time_point expires; };
    /** \brief 操作結果を最大4件、6秒間表示する */
    void Push(std::string text, bool error = false) {
        if(items_.size() == 4) items_.pop_front();
        items_.push_back({std::move(text), error, std::chrono::steady_clock::now() + std::chrono::seconds(6)});
    }
    const std::deque<Item>& Items() {
        const auto now = std::chrono::steady_clock::now();
        while(!items_.empty() && items_.front().expires <= now) items_.pop_front();
        return items_;
    }
private:
    std::deque<Item> items_; //< UI thread所有
};
}
