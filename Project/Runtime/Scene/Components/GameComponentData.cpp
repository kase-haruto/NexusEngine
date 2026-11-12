#include "GameComponentData.h"
#include "Runtime/Reflection/GamePropertyMetadata.h"
#include <atomic>

namespace NexusEngine {
uint64_t NextGameComponentAttachment() noexcept {
    static std::atomic<uint64_t> next{1};
    auto value = next.load();
    // overflow後は0に固定する。古いinstanceのtokenを再利用して復活させない。
    while(value && !next.compare_exchange_weak(value, value + 1)) {}
    return value;
}
bool GameComponentData::IsValid() const noexcept {
    try {
        static_cast<void>(GamePropertyMetadata::State(propertyState));
        if(attachments.size() > 256) return false;
        for(const auto& [id, token] : attachments) if(!token || GamePropertyMetadata::Id(id) != id) return false;
        for(size_t index = 0; index < attachments.size(); ++index)
            for(size_t previous = 0; previous < index; ++previous)
                if(attachments[previous].first == attachments[index].first) return false;
        return true;
    } catch(...) { return false; }
}
}
