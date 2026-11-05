#include "PostProcessGraphDocument.h"
#include "Graphics/PostProcess/PostProcessGraphAsset.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <fstream>
#include <sstream>

namespace NexusEngine {

    void PostProcessGraphDocument::Initialize(const std::filesystem::path& root, const std::filesystem::path& config) {
        root_ = root; file_ = (config.empty() ? root/"Config" : config)/"RenderPipeline.json";
        std::error_code ec;
        if(std::filesystem::exists(file_, ec)) { auto loaded = Load(); Applied(loaded); }
    }
    std::optional<PostProcessGraph> PostProcessGraphDocument::TakePending() {
        if(!pending_) return {};
        pending_ = false;
        auto result = graph_;
        ResolvePostProcessGraphShaders(result,root_);
        return result;
    }
    void PostProcessGraphDocument::Applied(const Result<void>& result) {
        error_ = !result;
        status_ = result ? "Applied" : result.error().GetMessageText();
    }
    std::string PostProcessGraphDocument::Serialize() const { return SerializePostProcessGraph(graph_); }
    Result<void> PostProcessGraphDocument::Deserialize(std::string_view text) {
        auto loaded=DeserializePostProcessGraph(text);
        if(!loaded) return std::unexpected(std::move(loaded.error()));
        graph_=std::move(*loaded); pending_=true; dirty_=false; return {};
    }
    Result<void> PostProcessGraphDocument::Save() {
        auto valid = graph_.ValidateLinks(); if(!valid) return valid;
        std::error_code ec;
        std::filesystem::create_directories(file_.parent_path(),ec);
        if(ec) return std::unexpected(Error(ErrorCategory::FileSystem,117,"Cannot create render graph configuration directory: "+ec.message()));
        auto saved = WriteFileAtomically(file_, Serialize());
        if(saved) { dirty_ = false; status_ = "Saved Config/RenderPipeline.json"; error_ = false; }
        return saved;
    }
    Result<void> PostProcessGraphDocument::Load() {
        auto loaded=LoadPostProcessGraph(file_);
        if(!loaded) return std::unexpected(std::move(loaded.error()));
        graph_=std::move(*loaded); pending_=true; dirty_=false; return {};
    }
}
