#include "CurvePreviewBridge.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/KeyframeAnimationComponent.h"
#include "Runtime/Scene/Components/SkeletalAnimationComponent.h"
#include "Runtime/Scene/Components/CurveBindingComponent.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>

namespace NexusEngine {
namespace {
constexpr size_t MaximumCurve = 32 * 1024 * 1024;
constexpr size_t ChunkSize = 512 * 1024;
IPC::Message Packet(IPC::MessageType type, uint64_t revision, IPC::WireWriter writer = {}) {
    IPC::Message message;
    message.type = type;
    message.revision = revision;
    message.payload = std::move(writer.data);
    return message;
}
template<class Callback>
bool WithPlayer(Level& level, PersistentId id, const std::string& reference, Callback callback) {
    auto entity = level.FindEntity(id);
    const auto& root = level.Animations().Root();
    if(auto* player = entity.GetComponent<KeyframeAnimationComponent>(); player && SameAssetReference(root, player->speedCurveAsset, reference)) {
        const auto clip = level.Animations().Clip(player->clipAsset);
        callback(*player, player->speedCurveDuration > 0 ? player->speedCurveDuration : clip ? (*clip)->duration : 1);
        return true;
    }
    if(auto* player = entity.GetComponent<SkeletalAnimationComponent>(); player && SameAssetReference(root, player->speedCurveAsset, reference)) {
        const auto clip = level.Animations().Clip(player->clipAsset);
        callback(*player, player->speedCurveDuration > 0 ? player->speedCurveDuration : clip ? (*clip)->duration : 1);
        return true;
    }
    if(auto* player = entity.GetComponent<CurveBindingComponent>(); player && SameAssetReference(root, player->curveAsset, reference)) {
        callback(*player, player->duration);
        return true;
    }
    return false;
}
}
CurvePreviewSender::CurvePreviewSender(std::string endpoint) : channel_(std::move(endpoint), true) {
}
void CurvePreviewSender::SetDraft(std::string reference, const FloatCurve* curve) {
    reference_ = std::move(reference);
    text_ = curve ? SerializeFloatCurve(*curve) : std::string{};
    state_.diagnostic.clear();
    if(text_.size() > MaximumCurve) {
        text_.clear();
        state_.diagnostic = "Curve preview exceeds the 32 MiB transfer limit";
    }
    ++revision_;
    offset_ = 0;
    stage_ = 0;
}
void CurvePreviewSender::Select(PersistentId entity) {
    if(entity_ != entity) {
        entity_ = entity;
        Command(CurvePreviewAction::Observe);
    }
}
bool CurvePreviewSender::Command(CurvePreviewAction action, double input) {
    if(!std::isfinite(input) || std::abs(input) > 1e12 || reference_.empty()) {
        return false;
    }
    IPC::WireWriter writer;
    writer.U64(entity_.high);
    writer.U64(entity_.low);
    writer.U16(static_cast<uint16_t>(action));
    writer.String(reference_);
    writer.String(std::to_string(input));
    return channel_.Send(Packet(IPC::MessageType::CurvePlayback, revision_, std::move(writer)));
}
void CurvePreviewSender::Poll() {
    state_.connected = channel_.IsOpen();
    if(!state_.connected) {
        state_.found = false;
        return;
    }
    if(generation_ != channel_.Generation()) {
        generation_ = channel_.Generation();
        offset_ = 0;
        stage_ = 0;
    }
    // 一frameのqueue投入を制限する。大きなcurveでも描画threadで通信完了を待たない。
    for(int budget = 0; budget < 8 && stage_ < 3; ++budget) {
        IPC::WireWriter writer;
        IPC::MessageType type;
        if(text_.empty()) {
            type = IPC::MessageType::CurveClear;
        } else if(stage_ == 0) {
            type = IPC::MessageType::CurveBegin;
            writer.String(reference_);
            writer.U32(static_cast<uint32_t>(text_.size()));
        } else if(offset_ < text_.size()) {
            type = IPC::MessageType::CurveChunk;
            writer.U32(static_cast<uint32_t>(offset_));
            writer.String(text_.substr(offset_, ChunkSize));
        } else {
            type = IPC::MessageType::CurveCommit;
        }
        if(!channel_.Send(Packet(type, revision_, std::move(writer)))) {
            break;
        }
        if(type == IPC::MessageType::CurveClear || type == IPC::MessageType::CurveCommit) {
            stage_ = 3;
            Command(CurvePreviewAction::Observe);
        } else if(type == IPC::MessageType::CurveBegin) {
            stage_ = 1;
        } else {
            offset_ = (std::min)(text_.size(), offset_ + ChunkSize);
        }
    }
    while(auto message = channel_.TryReceive()) {
        if(message->type != IPC::MessageType::CurveState || message->revision != revision_) {
            continue;
        }
        IPC::WireReader reader(message->payload);
        std::string json;
        if(reader.String(json) && reader.Finished()) {
            const auto value = nlohmann::json::parse(json, nullptr, false);
            if(value.is_object() && value.value("input", nlohmann::json{}).is_number()) {
                state_.found = value.value("found", nlohmann::json{}) == true;
                state_.playing = value.value("playing", nlohmann::json{}) == true;
                state_.input = value["input"].get<double>();
                state_.diagnostic = value.value("diagnostic", std::string{});
            }
        }
    }
}
const RemoteCurveState& CurvePreviewSender::State() const noexcept {
    return state_;
}
CurvePreviewReceiver::CurvePreviewReceiver(std::string endpoint) : channel_(std::move(endpoint), false) {
}
void CurvePreviewReceiver::Playback(Level& level, CurvePreviewAction action, double input) {
    WithPlayer(level, entity_, reference_, [&](auto& player, double duration) {
        switch(action) {
        case CurvePreviewAction::Play:
            player.Play();
            break;
        case CurvePreviewAction::Pause:
            player.Pause();
            break;
        case CurvePreviewAction::Stop:
            player.Stop();
            break;
        case CurvePreviewAction::Seek:
            if constexpr(std::is_same_v<std::decay_t<decltype(player)>, CurveBindingComponent>) {
                if(player.manual) {
                    player.input = static_cast<float>(input);
                } else {
                    player.Seek(static_cast<float>(input * duration));
                }
            } else {
                player.Seek(static_cast<float>(input * duration));
            }
            break;
        default:
            break;
        }
    });
}
RemoteCurveState CurvePreviewReceiver::ReadState(Level& level) {
    RemoteCurveState state;
    state.connected = channel_.IsOpen();
    state.diagnostic = error_;
    state.found = WithPlayer(level, entity_, reference_, [&](const auto& player, double duration) {
        state.input = duration > 0 ? player.elapsed / duration : 0;
        state.playing = player.playing;
        if constexpr(std::is_same_v<std::decay_t<decltype(player)>, CurveBindingComponent>) {
            if(player.manual) {
                state.input = player.input;
            } else if(player.loop) {
                state.input -= std::floor(state.input);
            }
        }
    });
    return state;
}
void CurvePreviewReceiver::Poll(Level& level) {
    if(generation_ != channel_.Generation() || !channel_.IsOpen()) {
        // 切断したEditorの未保存値をRuntimeへ残さない。保存済みassetへ戻す。
        if(!appliedReference_.empty()) {
            level.Animations().Curves().RemoveOverride(appliedReference_, true);
            appliedReference_.clear();
        }
        generation_ = channel_.Generation();
        revision_ = 0;
        expected_ = 0;
        text_.clear();
    }
    if(parsed_.valid() && parsed_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto curve = parsed_.get();
        if(curve && pendingRevision_ == revision_ && channel_.IsOpen()) {
            const auto applied = level.Animations().Curves().Override(pendingReference_, *curve);
            if(applied) {
                if(appliedReference_ != pendingReference_ && !appliedReference_.empty()) {
                    level.Animations().Curves().RemoveOverride(appliedReference_, true);
                }
                appliedReference_ = pendingReference_;
                error_.clear();
            } else {
                error_ = applied.error().GetMessageText();
            }
        } else if(!curve) {
            error_ = curve.error().GetMessageText();
        }
    }
    for(int budget = 0; budget < 128; ++budget) {
        // parse中はcommitをqueueに保持する。futureを上書きしてmain threadを待機させない。
        if(parsed_.valid()) {
            break;
        }
        auto message = channel_.TryReceive();
        if(!message) {
            break;
        }
        IPC::WireReader reader(message->payload);
        if(message->type == IPC::MessageType::CurveBegin) {
            uint32_t size = 0;
            std::string reference;
            if(message->revision >= revision_ && reader.String(reference) && reader.U32(size) && reader.Finished() && size <= MaximumCurve) {
                revision_ = message->revision;
                stagingReference_ = std::move(reference);
                expected_ = size;
                text_.clear();
            }
        } else if(message->type == IPC::MessageType::CurveChunk && message->revision == revision_) {
            uint32_t offset = 0;
            std::string chunk;
            if(reader.U32(offset) && reader.String(chunk, ChunkSize) && reader.Finished() && offset == text_.size() && chunk.size() <= expected_ - text_.size()) {
                text_ += chunk;
            } else {
                expected_ = 0;
                text_.clear();
                error_ = "Incomplete Curve transfer";
            }
        } else if(message->type == IPC::MessageType::CurveCommit && message->revision == revision_ && expected_ && expected_ == text_.size() && reader.Finished()) {
            pendingRevision_ = revision_;
            pendingReference_ = stagingReference_;
            parsed_ = std::async(std::launch::async, [text = std::move(text_)] {
                return ParseFloatCurve(text);
            });
            text_.clear();
            expected_ = 0;
        } else if(message->type == IPC::MessageType::CurveClear && message->revision >= revision_ && reader.Finished()) {
            revision_ = message->revision;
            if(!appliedReference_.empty()) {
                level.Animations().Curves().RemoveOverride(appliedReference_, true);
                appliedReference_.clear();
            }
            expected_ = 0;
            text_.clear();
        } else if(message->type == IPC::MessageType::CurvePlayback) {
            uint16_t action = 0;
            std::string reference, input;
            PersistentId entity;
            if(reader.U64(entity.high) && reader.U64(entity.low) && reader.U16(action) && reader.String(reference) && reader.String(input, 64) && reader.Finished() && action <= static_cast<uint16_t>(CurvePreviewAction::Seek)) {
                const auto parsed = nlohmann::json::parse(input, nullptr, false);
                if(parsed.is_number()) {
                    const double value = parsed.get<double>();
                    if(std::isfinite(value) && std::abs(value) <= 1e12) {
                        entity_ = entity;
                        reference_ = std::move(reference);
                        Playback(level, static_cast<CurvePreviewAction>(action), value);
                    }
                }
            }
        }
    }
    if(channel_.IsOpen()) {
        const auto state = ReadState(level);
        IPC::WireWriter writer;
        writer.String(nlohmann::json{{"found", state.found}, {"playing", state.playing}, {"input", state.input}, {"diagnostic", state.diagnostic}}.dump());
        channel_.Send(Packet(IPC::MessageType::CurveState, revision_, std::move(writer)));
    }
}
}
