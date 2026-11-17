#include "AnimationDocument.h"
#include "ThirdParty/nlohmann/json.hpp"
#include "Platform/FileSystem/AtomicFile.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace NexusEngine {
namespace {
using Json = nlohmann::json;
Error InvalidAnimation(const std::string& message) {
    return Error(ErrorCategory::Resource, 390, "Animation: " + message);
}
bool Positive(float value, bool zero = true) {
    return std::isfinite(value) && (zero ? value >= 0 : value > 0);
}
Result<Json> ReadAnimation(const std::filesystem::path& file) {
    try {
        // 編集画面からも同じ上限を適用し、壊れたファイルでUIを占有しない。
        if(std::filesystem::file_size(file) > 32 * 1024 * 1024) {
            return std::unexpected(InvalidAnimation("file exceeds 32 MiB"));
        }
        std::ifstream input(file);
        auto json = Json::parse(input);
        if(!json.is_object()) {
            return std::unexpected(InvalidAnimation("expected an object"));
        }
        return json;
    } catch(const std::exception& error) {
        return std::unexpected(InvalidAnimation(error.what()));
    }
}
Json TrackJson(const AnimationTrack& track) {
    Json result{{"target", track.target}, {"component", track.component}, {"property", track.property},
        {"interpolation", static_cast<int>(track.interpolation)}, {"keys", Json::array()}};
    for(const auto& key : track.keys) {
        result["keys"].push_back({{"time", key.time}, {"value", key.value},
            {"in", key.inTangent}, {"out", key.outTangent}});
    }
    return result;
}
}
float AnimationClipTime(float time, float duration, bool loop) {
    if(duration <= 0) {
        return 0;
    }
    return loop ? std::fmod((std::max)(time, 0.0f), duration) : std::clamp(time, 0.0f, duration);
}
Result<std::filesystem::path> ResolveAnimationPath(const std::filesystem::path& root, const std::string& reference) {
    if(reference.empty() || root.empty()) {
        return std::unexpected(InvalidAnimation("asset root or reference is empty"));
    }
    std::error_code error;
    const auto base = std::filesystem::weakly_canonical(root, error);
    const auto path = std::filesystem::weakly_canonical(base / std::filesystem::path(reinterpret_cast<const char8_t*>(reference.c_str())), error);
    const auto relative = path.lexically_relative(base);
    if(error || relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
        return std::unexpected(InvalidAnimation("asset must be inside Assets"));
    }
    return path;
}
Result<void> ValidateAnimationDocument(const AnimationClipDocument& document) {
    if(!Positive(document.duration) || document.tracks.size() > 4096 || document.events.size() > 10000) {
        return std::unexpected(InvalidAnimation("invalid duration or too many tracks/events"));
    }
    std::set<std::tuple<std::string, std::string, std::string>> bindings;
    for(const auto& track : document.tracks) {
        if(track.component.empty() || track.property.empty() || track.keys.empty() || track.keys.size() > 100000 ||
            !bindings.emplace(track.target, track.component, track.property).second) {
            return std::unexpected(InvalidAnimation("empty or duplicate track"));
        }
        const auto dimensions = track.keys.front().value.size();
        if(dimensions < 1 || dimensions > 4 || static_cast<int>(track.interpolation) < 0 || static_cast<int>(track.interpolation) > 2) {
            return std::unexpected(InvalidAnimation("invalid track dimension/interpolation"));
        }
        float previous = -1;
        for(const auto& key : track.keys) {
            if(!Positive(key.time) || key.time <= previous || key.time > document.duration || key.value.size() != dimensions ||
                (!key.inTangent.empty() && key.inTangent.size() != dimensions) ||
                (!key.outTangent.empty() && key.outTangent.size() != dimensions)) {
                return std::unexpected(InvalidAnimation("keys require unique increasing times within duration"));
            }
            for(const auto* values : {&key.value, &key.inTangent, &key.outTangent}) {
                for(float value : *values) {
                    if(!std::isfinite(value)) {
                        return std::unexpected(InvalidAnimation("key contains nonfinite values"));
                    }
                }
            }
            previous = key.time;
        }
    }
    for(const auto& event : document.events) {
        if(!Positive(event.time) || event.time > document.duration || event.name.empty()) {
            return std::unexpected(InvalidAnimation("invalid event"));
        }
    }
    return {};
}
Result<void> ValidateAnimationDocument(const AnimatorDocument& document) {
    if(document.states.empty() || document.states.size() > 1024 || document.transitions.size() > 4096) {
        return std::unexpected(InvalidAnimation("controller requires states"));
    }
    std::set<std::string> states, parameters;
    for(const auto& parameter : document.parameters) {
        if(parameter.name.empty() || !parameters.insert(parameter.name).second || !std::isfinite(parameter.value) ||
            (parameter.type != "Float" && parameter.type != "Int" && parameter.type != "Bool" && parameter.type != "Trigger")) {
            return std::unexpected(InvalidAnimation("invalid or duplicate parameter"));
        }
    }
    for(const auto& state : document.states) {
        if(state.name.empty() || state.name == "Any State" || !states.insert(state.name).second || !Positive(state.speed) ||
            !std::isfinite(state.position[0]) || !std::isfinite(state.position[1])) {
            return std::unexpected(InvalidAnimation("invalid or duplicate state"));
        }
        if(state.blend == "Clip") {
            if(state.asset.empty()) {
                return std::unexpected(InvalidAnimation("state has no clip"));
            }
        } else {
            if((state.blend != "1D" && state.blend != "2D") || state.motions.empty() ||
                !parameters.contains(state.parameterX) || (state.blend == "2D" && !parameters.contains(state.parameterY))) {
                return std::unexpected(InvalidAnimation("invalid Blend Tree"));
            }
            std::set<std::pair<float, float>> positions;
            for(const auto& motion : state.motions) {
                if(motion.asset.empty() || !std::isfinite(motion.x) || !std::isfinite(motion.y) || !Positive(motion.speed) ||
                    !positions.emplace(motion.x, state.blend == "1D" ? 0 : motion.y).second) {
                    return std::unexpected(InvalidAnimation("invalid or duplicate Blend Tree point"));
                }
            }
        }
    }
    if(!states.contains(document.entry)) {
        return std::unexpected(InvalidAnimation("entry state does not exist"));
    }
    for(const auto& transition : document.transitions) {
        if((transition.from != "Any State" && !states.contains(transition.from)) || !states.contains(transition.to) ||
            !Positive(transition.duration) || !Positive(transition.exitTime) || !Positive(transition.offset)) {
            return std::unexpected(InvalidAnimation("invalid transition"));
        }
        for(const auto& condition : transition.conditions) {
            if(!parameters.contains(condition.parameter) || !std::isfinite(condition.value) ||
                (condition.operation != ">" && condition.operation != "<" && condition.operation != "==" && condition.operation != "!=" &&
                 condition.operation != "True" && condition.operation != "False")) {
                return std::unexpected(InvalidAnimation("invalid transition condition"));
            }
        }
    }
    return {};
}
Result<void> ValidateAnimationDocument(const TimelineDocument& document) {
    if(!Positive(document.duration, false) || document.tracks.size() > 1024) {
        return std::unexpected(InvalidAnimation("invalid Timeline duration"));
    }
    for(const auto& track : document.tracks) {
        if(track.type != "Animation" && track.type != "Activation") {
            return std::unexpected(InvalidAnimation("unsupported Timeline track type"));
        }
        for(const auto& clip : track.clips) {
            if(!Positive(clip.start) || !Positive(clip.duration, false) || !Positive(clip.clipIn) || !Positive(clip.speed) ||
                !Positive(clip.easeIn) || !Positive(clip.easeOut) || clip.easeIn > clip.duration || clip.easeOut > clip.duration ||
                clip.start + clip.duration > document.duration || (track.type == "Animation" && clip.asset.empty())) {
                return std::unexpected(InvalidAnimation("invalid Timeline clip range"));
            }
        }
    }
    return {};
}
Result<AnimationClipDocument> LoadAnimationClipDocument(const std::filesystem::path& file) {
    auto read = ReadAnimation(file);
    if(!read) {
        return std::unexpected(read.error());
    }
    try {
        const auto& json = *read;
        const int version = json.at("version");
        if(version != 1 && version != 2) {
            return std::unexpected(InvalidAnimation("unsupported clip version"));
        }
        AnimationClipDocument document;
        document.source = json.value("source", "");
        document.clip = json.value("clip", "");
        document.duration = json.value("duration", 1.0f);
        document.loop = json.value("loop", true);
        for(const auto& trackJson : json.value("tracks", Json::array())) {
            AnimationTrack track;
            track.target = trackJson.value("target", "");
            track.component = trackJson.at("component");
            track.property = trackJson.at("property");
            track.interpolation = static_cast<AnimationInterpolation>(trackJson.value("interpolation", 1));
            for(const auto& key : trackJson.at("keys")) {
                track.keys.push_back({key.at("time"), key.at("value").get<std::vector<float>>(),
                    key.value("in", std::vector<float>{}), key.value("out", std::vector<float>{})});
            }
            document.tracks.push_back(std::move(track));
        }
        for(const auto& event : json.value("events", Json::array())) {
            document.events.push_back({event.at("time"), event.at("name"), event.value("payload", "")});
        }
        auto valid = ValidateAnimationDocument(document);
        if(!valid) {
            return std::unexpected(valid.error());
        }
        return document;
    } catch(const std::exception& error) {
        return std::unexpected(InvalidAnimation(error.what()));
    }
}
Result<AnimatorDocument> LoadAnimatorDocument(const std::filesystem::path& file) {
    auto read = ReadAnimation(file);
    if(!read) {
        return std::unexpected(read.error());
    }
    try {
        const auto& json = *read;
        if(json.at("version") != 1) {
            return std::unexpected(InvalidAnimation("unsupported controller version"));
        }
        AnimatorDocument document;
        document.entry = json.at("entry");
        for(const auto& parameter : json.value("parameters", Json::array())) {
            document.parameters.push_back({parameter.at("name"), parameter.at("type"), parameter.value("value", 0.0f)});
        }
        for(const auto& stateJson : json.at("states")) {
            AnimatorState state;
            state.name = stateJson.at("name");
            state.asset = stateJson.value("asset", "");
            state.speed = stateJson.value("speed", 1.0f);
            state.loop = stateJson.value("loop", true);
            state.position = stateJson.value("position", std::array<float, 2>{80, 80});
            state.blend = stateJson.value("blend", "Clip");
            state.parameterX = stateJson.value("parameterX", "Speed");
            state.parameterY = stateJson.value("parameterY", "Direction");
            for(const auto& motion : stateJson.value("motions", Json::array())) {
                state.motions.push_back({motion.at("asset"), motion.value("x", 0.0f), motion.value("y", 0.0f), motion.value("speed", 1.0f)});
            }
            document.states.push_back(std::move(state));
        }
        for(const auto& item : json.value("transitions", Json::array())) {
            AnimatorTransition transition;
            transition.from = item.at("from");
            transition.to = item.at("to");
            transition.duration = item.value("duration", .2f);
            transition.exitTime = item.value("exitTime", 1.0f);
            transition.hasExitTime = item.value("hasExitTime", false);
            transition.offset = item.value("offset", 0.0f);
            for(const auto& condition : item.value("conditions", Json::array())) {
                transition.conditions.push_back({condition.at("parameter"), condition.at("operation"), condition.value("value", 0.0f)});
            }
            document.transitions.push_back(std::move(transition));
        }
        auto valid = ValidateAnimationDocument(document);
        if(!valid) {
            return std::unexpected(valid.error());
        }
        return document;
    } catch(const std::exception& error) {
        return std::unexpected(InvalidAnimation(error.what()));
    }
}
Result<TimelineDocument> LoadTimelineDocument(const std::filesystem::path& file) {
    auto read = ReadAnimation(file);
    if(!read) {
        return std::unexpected(read.error());
    }
    try {
        const auto& json = *read;
        if(json.at("version") != 1) {
            return std::unexpected(InvalidAnimation("unsupported Timeline version"));
        }
        TimelineDocument document;
        document.duration = json.value("duration", 5.0f);
        document.loop = json.value("loop", false);
        for(const auto& item : json.value("tracks", Json::array())) {
            TimelineTrack track;
            track.name = item.value("name", "Animation");
            track.target = item.value("target", "");
            track.type = item.value("type", "Animation");
            track.muted = item.value("muted", false);
            for(const auto& clip : item.value("clips", Json::array())) {
                track.clips.push_back({clip.value("asset", ""), clip.value("start", 0.0f), clip.value("duration", 1.0f),
                    clip.value("clipIn", 0.0f), clip.value("speed", 1.0f), clip.value("easeIn", 0.0f), clip.value("easeOut", 0.0f), clip.value("loop", false)});
            }
            document.tracks.push_back(std::move(track));
        }
        auto valid = ValidateAnimationDocument(document);
        if(!valid) {
            return std::unexpected(valid.error());
        }
        return document;
    } catch(const std::exception& error) {
        return std::unexpected(InvalidAnimation(error.what()));
    }
}
Result<void> SaveAnimationDocument(const std::filesystem::path& file, const AnimationClipDocument& document) {
    auto valid = ValidateAnimationDocument(document);
    if(!valid) {
        return valid;
    }
    Json json{{"version", 2}, {"source", document.source}, {"clip", document.clip}, {"duration", document.duration},
        {"loop", document.loop}, {"tracks", Json::array()}, {"events", Json::array()}};
    for(const auto& track : document.tracks) {
        json["tracks"].push_back(TrackJson(track));
    }
    for(const auto& event : document.events) {
        json["events"].push_back({{"time", event.time}, {"name", event.name}, {"payload", event.payload}});
    }
    return WriteFileAtomically(file, json.dump(2) + "\n");
}
Result<void> SaveAnimationDocument(const std::filesystem::path& file, const AnimatorDocument& document) {
    auto valid = ValidateAnimationDocument(document);
    if(!valid) {
        return valid;
    }
    Json json{{"version", 1}, {"entry", document.entry}, {"parameters", Json::array()}, {"states", Json::array()}, {"transitions", Json::array()}};
    for(const auto& parameter : document.parameters) {
        json["parameters"].push_back({{"name", parameter.name}, {"type", parameter.type}, {"value", parameter.value}});
    }
    for(const auto& state : document.states) {
        Json item{{"name", state.name}, {"asset", state.asset}, {"speed", state.speed}, {"loop", state.loop}, {"position", state.position},
            {"blend", state.blend}, {"parameterX", state.parameterX}, {"parameterY", state.parameterY}, {"motions", Json::array()}};
        for(const auto& motion : state.motions) {
            item["motions"].push_back({{"asset", motion.asset}, {"x", motion.x}, {"y", motion.y}, {"speed", motion.speed}});
        }
        json["states"].push_back(item);
    }
    for(const auto& transition : document.transitions) {
        Json item{{"from", transition.from}, {"to", transition.to}, {"duration", transition.duration}, {"exitTime", transition.exitTime},
            {"hasExitTime", transition.hasExitTime}, {"offset", transition.offset}, {"conditions", Json::array()}};
        for(const auto& condition : transition.conditions) {
            item["conditions"].push_back({{"parameter", condition.parameter}, {"operation", condition.operation}, {"value", condition.value}});
        }
        json["transitions"].push_back(item);
    }
    return WriteFileAtomically(file, json.dump(2) + "\n");
}
Result<void> SaveAnimationDocument(const std::filesystem::path& file, const TimelineDocument& document) {
    auto valid = ValidateAnimationDocument(document);
    if(!valid) {
        return valid;
    }
    Json json{{"version", 1}, {"duration", document.duration}, {"loop", document.loop}, {"tracks", Json::array()}};
    for(const auto& track : document.tracks) {
        Json item{{"name", track.name}, {"target", track.target}, {"type", track.type}, {"muted", track.muted}, {"clips", Json::array()}};
        for(const auto& clip : track.clips) {
            item["clips"].push_back({{"asset", clip.asset}, {"start", clip.start}, {"duration", clip.duration}, {"clipIn", clip.clipIn},
                {"speed", clip.speed}, {"easeIn", clip.easeIn}, {"easeOut", clip.easeOut}, {"loop", clip.loop}});
        }
        json["tracks"].push_back(item);
    }
    return WriteFileAtomically(file, json.dump(2) + "\n");
}
std::vector<float> SampleAnimationTrack(const AnimationTrack& track, float time) {
    if(track.keys.empty()) {
        return {};
    }
    if(time <= track.keys.front().time) {
        return track.keys.front().value;
    }
    if(time >= track.keys.back().time) {
        return track.keys.back().value;
    }
    const auto upper = std::upper_bound(track.keys.begin(), track.keys.end(), time,
        [](float sample, const AnimationKey& key) { return sample < key.time; });
    const auto& left = *(upper - 1);
    if(track.interpolation == AnimationInterpolation::Step) {
        return left.value;
    }
    const auto& right = *upper;
    const float interval = right.time - left.time;
    const float amount = (time - left.time) / interval;
    auto result = left.value;
    if(track.property == "rotation" && result.size() == 4 && track.interpolation == AnimationInterpolation::Linear) {
        float dot = 0;
        for(size_t index = 0; index < 4; ++index) {
            dot += left.value[index]*right.value[index];
        }
        const float sign = dot < 0 ? -1.0f : 1.0f;
        dot = std::clamp(std::abs(dot),0.0f,1.0f);
        const float angle = std::acos(dot);
        const float sine = std::sin(angle);
        const float first = dot > .9995f ? 1-amount : std::sin((1-amount)*angle)/sine;
        const float second = dot > .9995f ? amount : std::sin(amount*angle)/sine;
        float norm = 0;
        for(size_t index = 0; index < 4; ++index) {
            result[index] = first*left.value[index] + second*sign*right.value[index];
            norm += result[index]*result[index];
        }
        if(norm > 1e-12f) {
            for(float& value : result) {
                value /= std::sqrt(norm);
            }
        }
        return result;
    }
    for(size_t index = 0; index < result.size(); ++index) {
        if(track.interpolation == AnimationInterpolation::CubicSpline) {
            const float a2 = amount * amount, a3 = a2 * amount;
            const float out = left.outTangent.empty() ? 0 : left.outTangent[index];
            const float in = right.inTangent.empty() ? 0 : right.inTangent[index];
            result[index] = (2*a3 - 3*a2 + 1)*left.value[index] + (a3 - 2*a2 + amount)*interval*out +
                (-2*a3 + 3*a2)*right.value[index] + (a3 - a2)*interval*in;
        } else {
            result[index] += (right.value[index] - left.value[index]) * amount;
        }
    }
    return result;
}
}
