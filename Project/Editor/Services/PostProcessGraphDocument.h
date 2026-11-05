#pragma once
#include <optional>
#include "Graphics/PostProcess/PostProcessGraph.h"
namespace NexusEngine {
    // Owns the draft graph. GPU application is queued until PrepareRender.
    class PostProcessGraphDocument final {
    public:
        void Initialize(const std::filesystem::path& projectRoot, const std::filesystem::path& configRoot = {});
        PostProcessGraph& Graph() noexcept { return graph_; }
        const PostProcessGraph& Graph() const noexcept { return graph_; }
        void Changed(bool rendering = true) { dirty_ = true; if(rendering && autoApply) pending_ = true; }
        void RequestApply() { pending_ = true; }
        std::optional<PostProcessGraph> TakePending();
        void Applied(const Result<void>& result);
        Result<void> Save();
        Result<void> Load();
        void Reset() { graph_ = PostProcessGraph::Default(); Changed(); }
        const std::string& Status() const noexcept { return status_; }
        bool HasError() const noexcept { return error_; }
        bool IsDirty() const noexcept { return dirty_; }
        bool autoApply = true;
        bool reloadRequested = false;
        bool capturesKeyboard = false; // Panel focus prevents scene shortcuts from acting on graph edits.
        std::string Serialize() const;
        Result<void> Deserialize(std::string_view json);
    private:
        PostProcessGraph graph_ = PostProcessGraph::Default();
        std::filesystem::path file_, root_;
        bool dirty_ = false, pending_ = false, error_ = false;
        std::string status_;
    };
}
