#include "AnimationCurvePreview.h"
#include "CurveEditDocument.h"
#include "AnimationStudioDocument.h"
#include "Editor/Core/EditorSession.h"
#include "Runtime/Scene/Components/KeyframeAnimationComponent.h"
#include "Runtime/Scene/Components/SkeletalAnimationComponent.h"
#include "Runtime/Scene/Components/CurveBindingComponent.h"
#include <cmath>
#include <algorithm>
#include "Platform/FileSystem/AssetIdentity.h"
#include "Runtime/Curves/CurvePreviewBridge.h"

namespace NexusEngine {
namespace {
struct PreviewTarget {
    Entity entity; //< callbackの呼出中だけ有効なhandle
    bool generic = false;
    bool skeletal = false; //< 用途側でcomponentを解決する
    double duration = 1, elapsed = 0; //< normalized Xと独立経過秒の変換
};
std::optional<PreviewTarget> Target(CurveEditDocument& curve, EditorSession& session) {
    if(!session.HasAnimationPreview() && !session.IsPlaying()) {
        return {};
    }
    auto* level = session.GetPreviewLevel();
    if(!level) {
        return {};
    }
    PreviewTarget result;
    result.entity = level->FindEntity(session.GetSelection());
    std::string asset;
    if(const auto* component = result.entity.GetComponent<KeyframeAnimationComponent>(); component && SameAssetReference(level->Animations().Root(), component->speedCurveAsset, curve.AssetReference())) {
        result.duration = component->speedCurveDuration;
        result.elapsed = component->elapsed;
        asset = component->clipAsset;
    } else if(const auto* skeletal = result.entity.GetComponent<SkeletalAnimationComponent>(); skeletal && SameAssetReference(level->Animations().Root(), skeletal->speedCurveAsset, curve.AssetReference())) {
        result.skeletal = true;
        result.duration = skeletal->speedCurveDuration;
        result.elapsed = skeletal->elapsed;
        asset = skeletal->clipAsset;
    } else if(const auto* binding = result.entity.GetComponent<CurveBindingComponent>(); binding && SameAssetReference(level->Animations().Root(), binding->curveAsset, curve.AssetReference())) {
        result.generic = true;
        result.duration = binding->duration;
        result.elapsed = binding->manual ? binding->input * binding->duration : binding->elapsed;
        if(!binding->manual && binding->loop && result.duration > 0) {
            result.elapsed = std::fmod(result.elapsed, result.duration);
        }
    } else {
        return {};
    }
    if(result.duration <= 0) {
        const auto clip = level->Animations().Clip(asset);
        result.duration = clip ? (*clip)->duration : 1;
    }
    if(result.duration <= 0) {
        result.duration = 1;
    }
    return result;
}
}
void ConnectAnimationCurvePreview(CurveEditDocument& curve, AnimationStudioDocument& studio, EditorSession& session) {
    curve.preview.label = "Normalized Time (0..1) / Curve Output";
    curve.preview.input = [&curve, &session]() -> std::optional<double> {
        auto target = Target(curve, session);
        if(!target) {
            return {};
        }
        return target->generic ? target->elapsed / target->duration : (std::min)(1.0, target->elapsed / target->duration);
    };
    curve.preview.play = [&curve, &studio, &session] {
        auto target = Target(curve, session);
        if(!target) {
            return;
        }
        if(target->generic) {
            target->entity.GetComponent<CurveBindingComponent>()->Play();
        } else if(studio.preview) {
            studio.playing = true;
        } else if(target->skeletal) {
            target->entity.GetComponent<SkeletalAnimationComponent>()->Play();
        } else {
            target->entity.GetComponent<KeyframeAnimationComponent>()->Play();
        }
    };
    curve.preview.pause = [&curve, &studio, &session] {
        auto target = Target(curve, session);
        if(!target) {
            return;
        }
        if(target->generic) {
            target->entity.GetComponent<CurveBindingComponent>()->Pause();
        } else if(studio.preview) {
            studio.playing = false;
        } else if(target->skeletal) {
            target->entity.GetComponent<SkeletalAnimationComponent>()->Pause();
        } else {
            target->entity.GetComponent<KeyframeAnimationComponent>()->Pause();
        }
    };
    curve.preview.seek = [&curve, &studio, &session](double x) {
        auto target = Target(curve, session);
        if(!target || !std::isfinite(x) || std::abs(x) > 1e12) {
            return;
        }
        const auto elapsed = static_cast<float>(x * target->duration);
        if(target->generic) {
            auto* binding = target->entity.GetComponent<CurveBindingComponent>();
            if(binding->manual) {
                binding->input = static_cast<float>(x);
            } else {
                binding->Seek(elapsed);
            }
        } else if(studio.preview) {
            studio.time = (std::max)(0.0f, elapsed);
        } else if(target->skeletal) {
            target->entity.GetComponent<SkeletalAnimationComponent>()->Seek(elapsed);
        } else {
            target->entity.GetComponent<KeyframeAnimationComponent>()->Seek(elapsed);
        }
    };
    curve.preview.stop = [&curve] {
        if(curve.preview.pause) {
            curve.preview.pause();
        }
        if(curve.preview.seek) {
            curve.preview.seek(0);
        }
    };
}
void ConnectRemoteCurvePreview(CurveEditDocument& curve, std::function<std::shared_ptr<CurvePreviewSender>()> sender) {
    // standaloneが接続している間だけ送信する。通常のWorld previewのcallbackは保持する。
    auto local = curve.preview;
    curve.preview.input = [sender, input = local.input]() -> std::optional<double> {
        const auto remote = sender();
        if(remote && remote->State().connected) {
            return remote->State().found ? std::optional(remote->State().input) : std::nullopt;
        }
        return input ? input() : std::nullopt;
    };
    curve.preview.play = [sender, play = local.play] {
        const auto remote = sender();
        if(remote && remote->State().connected) {
            remote->Command(CurvePreviewAction::Play);
        } else if(play) {
            play();
        }
    };
    curve.preview.pause = [sender, pause = local.pause] {
        const auto remote = sender();
        if(remote && remote->State().connected) {
            remote->Command(CurvePreviewAction::Pause);
        } else if(pause) {
            pause();
        }
    };
    curve.preview.stop = [sender, stop = local.stop] {
        const auto remote = sender();
        if(remote && remote->State().connected) {
            remote->Command(CurvePreviewAction::Stop);
        } else if(stop) {
            stop();
        }
    };
    curve.preview.seek = [sender, seek = local.seek](double input) {
        const auto remote = sender();
        if(remote && remote->State().connected) {
            remote->Command(CurvePreviewAction::Seek, input);
        } else if(seek) {
            seek(input);
        }
    };
}
}
