#pragma once
#include <map>
#include <string>

namespace NexusEngine {
// 診断はキーで更新・解除し、同じ問題を毎フレーム追加しない。
// 過去のログはBufferedLogBufferで保持する。
class ViewportDiagnostics final {
public:
    struct Item { std::string text; bool error = false; };
    void Set(std::string key, std::string text, bool error = false) {
        if(!items_.contains(key) && items_.size() >= 64) return;
        if(text.size() > 8192) text.resize(8192);
        items_[std::move(key)] = {std::move(text), error};
    }
    void Resolve(const std::string& key) { items_.erase(key); }
    void Clear() { items_.clear(); }
    const std::map<std::string, Item>& Items() const noexcept { return items_; }
private:
    std::map<std::string, Item> items_;
};
}
