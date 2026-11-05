#include "PostProcessGraphAsset.h"
#include "ThirdParty/nlohmann/json.hpp"
#include "Foundation/Utility/String/ConvertString.h"
#include <fstream>
#include <sstream>
namespace NexusEngine {
    using Json=nlohmann::json;
    std::string SerializePostProcessGraph(const PostProcessGraph& graph) {
        const auto& s = graph.settings;
        Json j {{"version",1}, {"settings", {{"toneMapper",int(s.toneMapper)}, {"exposureEV",s.exposureEV},
            {"bloomIntensity",s.bloomIntensity},{"bloomThreshold",s.bloomThreshold},{"bloomSoftKnee",s.bloomSoftKnee},
            {"saturation",s.saturation},{"contrast",s.contrast},{"gamma",s.gamma},{"gain",s.gain},{"offset",s.offset},
            {"whiteBalance",s.whiteBalance},{"filmicEnabled",s.filmicEnabled}}}, {"nodes",Json::array()}, {"links",Json::array()}};
        for(const auto& node : graph.nodes)
            j["nodes"].push_back({{"id",node.id},{"kind",int(node.kind)},{"enabled",node.enabled},{"position",node.position},
                {"shader",ConvertString(node.effect.shaderPath.wstring())},{"entry",ConvertString(node.effect.entryPoint)},
                {"parameters",node.effect.parameters},{"effectEnabled",node.effect.enabled}});
        for(const auto& link : graph.links) j["links"].push_back({{"from",link.from},{"to",link.to}});
        return j.dump(2);
    }
    Result<PostProcessGraph> DeserializePostProcessGraph(std::string_view text) {
        try {
            if(text.size() > 1024 * 1024) throw std::runtime_error("Graph document is too large.");
            const auto j = Json::parse(text);
            if(j.at("version").get<int>() != 1) throw std::runtime_error("Unsupported render graph version.");
            PostProcessGraph replacement;
            auto& s = replacement.settings; const auto& v = j.at("settings");
            s.toneMapper = static_cast<ToneMapper>(v.at("toneMapper").get<int>());
            s.exposureEV=v.at("exposureEV"); s.bloomIntensity=v.at("bloomIntensity"); s.bloomThreshold=v.at("bloomThreshold");
            s.bloomSoftKnee=v.at("bloomSoftKnee"); s.saturation=v.at("saturation"); s.contrast=v.at("contrast"); s.gamma=v.at("gamma");
            s.gain=v.at("gain").get<std::array<float,3>>(); s.offset=v.at("offset").get<std::array<float,3>>();
            s.whiteBalance=v.at("whiteBalance").get<std::array<float,3>>(); s.filmicEnabled=v.at("filmicEnabled");
            if(!s.IsValid() || j.at("nodes").size()>1024 || j.at("links").size()>2048) throw std::runtime_error("Invalid render graph data.");
            for(const auto& n : j.at("nodes")) {
                PostProcessNode node; node.id=n.at("id"); node.kind=static_cast<PostProcessNodeKind>(n.at("kind").get<int>());
                node.enabled=n.at("enabled"); node.position=n.at("position").get<std::array<float,2>>();
                node.effect.shaderPath=ConvertString(n.at("shader").get<std::string>());
                node.effect.entryPoint=ConvertString(n.at("entry").get<std::string>());
                node.effect.parameters=n.at("parameters").get<std::array<float,16>>(); node.effect.enabled=n.at("effectEnabled");
                replacement.nodes.push_back(std::move(node));
            }
            for(const auto& l : j.at("links")) replacement.links.push_back({l.at("from"),l.at("to")});
            auto valid = replacement.ValidateLinks(); if(!valid) return std::unexpected(std::move(valid.error()));
            for(const auto& node : replacement.nodes)
                for(float parameter : node.effect.parameters) if(!std::isfinite(parameter)) throw std::runtime_error("Nonfinite shader parameter.");
            return replacement;
        } catch(const std::exception& e) { return std::unexpected(Error(ErrorCategory::FileSystem, 117, e.what())); }
    }
    Result<PostProcessGraph> LoadPostProcessGraph(const std::filesystem::path& path) {
        std::error_code ec;
        if(std::filesystem::file_size(path,ec)>1024*1024 || ec)
            return std::unexpected(Error(ErrorCategory::FileSystem,117,"Cannot read render graph document."));
        std::ifstream file(path,std::ios::binary); std::ostringstream bytes; bytes << file.rdbuf();
        if(!file) return std::unexpected(Error(ErrorCategory::FileSystem,117,"Cannot read render graph document."));
        return DeserializePostProcessGraph(bytes.str());
    }
}