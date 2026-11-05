#pragma once
#include "PostProcessGraph.h"
namespace NexusEngine {
    std::string SerializePostProcessGraph(const PostProcessGraph& graph);
    Result<PostProcessGraph> DeserializePostProcessGraph(std::string_view text);
    Result<PostProcessGraph> LoadPostProcessGraph(const std::filesystem::path& file);
    // Custom shaders belong to the project; built-in nodes retain engine-relative paths.
    inline void ResolvePostProcessGraphShaders(PostProcessGraph& graph, const std::filesystem::path& root) {
        for(auto& node : graph.nodes) if(node.kind==PostProcessNodeKind::Shader && !node.effect.shaderPath.empty() && node.effect.shaderPath.is_relative())
            node.effect.shaderPath=root/node.effect.shaderPath;
    }
}
