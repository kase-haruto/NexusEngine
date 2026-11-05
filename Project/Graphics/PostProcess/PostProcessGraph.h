#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include "Foundation/Error/Result.h"
#include "PostProcessSettings.h"
#include "ScreenEffect.h"

namespace NexusEngine {
    enum class PostProcessNodeKind { Scene, Bloom, ToneMapping, Vignette, Shader, Fxaa, Output };
    struct PostProcessNode {
        int id = 0;
        PostProcessNodeKind kind = PostProcessNodeKind::Shader;
        bool enabled = true;
        ScreenEffect effect;
        std::array<float, 2> position {};
    };
    struct PostProcessLink { int from = 0, to = 0; };
    struct CompiledPostProcessGraph {
        PostProcessSettings settings;
        std::vector<ScreenEffect> effects;
        std::vector<int> order;
    };
    // Single colour stream. Disconnected draft nodes are allowed; cycles and ambiguous
    // inputs are not. HDR -> display conversion is explicit and cannot be bypassed.
    struct PostProcessGraph {
        std::vector<PostProcessNode> nodes;
        std::vector<PostProcessLink> links;
        PostProcessSettings settings;
        static PostProcessGraph Default() {
            PostProcessGraph graph;
            const PostProcessNodeKind kinds[] = {PostProcessNodeKind::Scene, PostProcessNodeKind::Bloom,
                PostProcessNodeKind::ToneMapping, PostProcessNodeKind::Fxaa, PostProcessNodeKind::Output};
            for(int i = 0; i < 5; ++i) {
                PostProcessNode node; node.id = i + 1; node.kind = kinds[i]; node.position = {float(i * 260), 60};
                graph.nodes.push_back(node);
                if(i) graph.links.push_back({i, i + 1});
            }
            return graph;
        }
        const PostProcessNode* Find(int id) const {
            for(const auto& node : nodes) if(node.id == id) return &node;
            return nullptr;
        }
        static bool OutputsHdr(PostProcessNodeKind kind) { return kind == PostProcessNodeKind::Scene || kind == PostProcessNodeKind::Bloom; }
        static bool InputsHdr(PostProcessNodeKind kind) { return kind == PostProcessNodeKind::Bloom || kind == PostProcessNodeKind::ToneMapping; }
        static Result<void> Invalid(const char* message) { return std::unexpected(Error(ErrorCategory::Graphics, 110, message)); }
        Result<void> ValidateLinks() const {
            if(nodes.size() > 1024 || links.size() > 2048) return Invalid("Graph exceeds the node/link limit.");
            std::set<int> ids, inputs;
            std::map<int, std::vector<int>> edges;
            for(const auto& node : nodes) {
                if(node.id <= 0 || node.id > 1000000 || !ids.insert(node.id).second ||
                   node.kind < PostProcessNodeKind::Scene || node.kind > PostProcessNodeKind::Output ||
                   !std::isfinite(node.position[0]) || !std::isfinite(node.position[1])) return Invalid("Invalid or duplicate graph node.");
            }
            for(const auto& link : links) {
                const auto* from = Find(link.from); const auto* to = Find(link.to);
                if(!from || !to || from == to || from->kind == PostProcessNodeKind::Output || to->kind == PostProcessNodeKind::Scene)
                    return Invalid("Invalid graph link endpoints.");
                if(OutputsHdr(from->kind) != InputsHdr(to->kind)) return Invalid("Cannot connect HDR and display-colour pins. Use Tone Mapping.");
                if(!inputs.insert(link.to).second) return Invalid("An input can have only one source.");
                edges[link.from].push_back(link.to);
            }
            std::map<int, int> state;
            std::function<bool(int)> visit = [&](int id) {
                if(state[id] == 1) return false;
                if(state[id] == 2) return true;
                state[id] = 1;
                for(int next : edges[id]) if(!visit(next)) return false;
                state[id] = 2; return true;
            };
            for(int id : ids) if(!visit(id)) return Invalid("Graph contains a cycle.");
            return {};
        }
        Result<void> Connect(int from, int to) {
            auto previous = links;
            std::erase_if(links, [&](const auto& link) { return link.to == to; });
            links.push_back({from, to});
            auto result = ValidateLinks();
            if(!result) links = std::move(previous);
            return result;
        }
        Result<CompiledPostProcessGraph> Compile() const {
            auto validation = ValidateLinks();
            if(!validation) return std::unexpected(std::move(validation.error()));
            if(!settings.IsValid()) return std::unexpected(Error(ErrorCategory::Graphics, 111, "Invalid graph post-process settings."));
            const PostProcessNode* output = nullptr;
            int scenes = 0;
            for(const auto& node : nodes) {
                if(node.kind == PostProcessNodeKind::Scene) ++scenes;
                if(node.kind == PostProcessNodeKind::Output) {
                    if(output) return std::unexpected(Error(ErrorCategory::Graphics, 112, "Graph needs exactly one Output."));
                    output = &node;
                }
            }
            if(!output || scenes != 1) return std::unexpected(Error(ErrorCategory::Graphics, 112, "Graph needs one Scene and one Output."));
            std::vector<const PostProcessNode*> path;
            for(auto* node = output; node;) {
                path.push_back(node);
                if(node->kind == PostProcessNodeKind::Scene) break;
                auto link = std::find_if(links.begin(), links.end(), [&](const auto& candidate) { return candidate.to == node->id; });
                if(link == links.end()) return std::unexpected(Error(ErrorCategory::Graphics, 113, "Connect Scene through Tone Mapping to Output."));
                node = Find(link->from);
            }
            std::reverse(path.begin(), path.end());
            CompiledPostProcessGraph compiled; compiled.settings = settings;
            compiled.settings.bloomEnabled = false; compiled.settings.antiAliasingEnabled = false;
            int toneMappers = 0, blooms = 0;
            for(const auto* node : path) {
                compiled.order.push_back(node->id);
                switch(node->kind) {
                case PostProcessNodeKind::Bloom:
                    if(++blooms > 1) return std::unexpected(Error(ErrorCategory::Graphics, 114, "Only one Bloom pass is supported."));
                    compiled.settings.bloomEnabled = node->enabled; break;
                case PostProcessNodeKind::ToneMapping: ++toneMappers; break;
                case PostProcessNodeKind::Vignette:
                case PostProcessNodeKind::Shader:
                case PostProcessNodeKind::Fxaa: {
                    auto effect = node->effect; effect.enabled = effect.enabled && node->enabled;
                    if(node->kind == PostProcessNodeKind::Vignette) { effect.shaderPath = L"PostProcess/Vignette.hlsl"; effect.entryPoint = L"PSMain"; }
                    if(node->kind == PostProcessNodeKind::Fxaa) { effect.shaderPath = L"PostProcess/Fxaa.hlsl"; effect.entryPoint = L"PSMain"; }
                    if(!effect.IsValid()) return std::unexpected(Error(ErrorCategory::Graphics, 115, "Shader node needs a valid HLSL path, entry point and finite parameters."));
                    compiled.effects.push_back(std::move(effect)); break;
                }
                default: break;
                }
            }
            if(toneMappers != 1) return std::unexpected(Error(ErrorCategory::Graphics, 116, "Output requires exactly one Tone Mapping pass."));
            return compiled;
        }
    };
}
