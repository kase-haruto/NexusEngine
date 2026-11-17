#include "AnimatorComponent.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <map>
#include <cmath>
namespace NexusEngine {
namespace {
using Json = nlohmann::json;
std::map<std::string, float> Parameters(const std::string& text) {
    std::map<std::string, float> result;
    try {
        const auto json = Json::parse(text);
        for(const auto& [name, value] : json.items()) {
            if(value.is_boolean()) {
                result[name] = value.get<bool>() ? 1.0f : 0.0f;
            } else if(value.is_number()) {
                const float number = value.get<float>();
                if(std::isfinite(number)) {
                    result[name] = number;
                }
            }
        }
    } catch(const std::exception&) {
        // 不正な外部入力はパラメーターを実行しない。アセット定義の既定値を使う。
    }
    return result;
}
}
void AnimatorComponent::SetFloat(const std::string& name, float value) {
    if(!std::isfinite(value)) {
        return;
    }
    auto values = Parameters(parameters);
    values[name] = value;
    parameters = Json(values).dump();
}
void AnimatorComponent::SetInt(const std::string& name, int value) {
    SetFloat(name, static_cast<float>(value));
}
void AnimatorComponent::SetBool(const std::string& name, bool value) {
    SetFloat(name, value ? 1.0f : 0.0f);
}
void AnimatorComponent::SetTrigger(const std::string& name) {
    SetBool(name, true);
}
void AnimatorComponent::ResetTrigger(const std::string& name) {
    SetBool(name, false);
}
float AnimatorComponent::GetFloat(const std::string& name) const {
    const auto values = Parameters(parameters);
    const auto found = values.find(name);
    return found == values.end() ? 0.0f : found->second;
}
}
