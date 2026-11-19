#include "ContentDependencyGraph.h"
#include "ContentIndex.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Platform/FileSystem/AssetIdentity.h"
#include <algorithm>
#include <thread>

namespace NexusEngine {
ContentDependencyGraph::ContentDependencyGraph() : snapshot_(std::make_shared<const ContentGraphSnapshot>()) {
}
ContentDependencyGraph::~ContentDependencyGraph() {
    cancellation_.request_stop();
    if (task_.valid()) {
        task_.wait();
    }
}
void ContentDependencyGraph::SetSourceTypes(std::map<std::string, std::vector<std::filesystem::path>> types) {
    sourceTypes_ = std::move(types);
    Invalidate();
}
void ContentDependencyGraph::Invalidate() {
    cancellation_.request_stop();
    dirty_ = true;
}
void ContentDependencyGraph::Request(std::vector<ContentGraphRoot> roots) {
    Poll();
    if (task_.valid() || !dirty_) {
        return;
    }
    dirty_ = false;
    cancellation_ = std::stop_source{};
    error_.clear();
    task_ = std::async(
        std::launch::async,
        [roots = std::move(roots), sourceTypes = sourceTypes_,
         stop = cancellation_.get_token()]() -> Result<ContentGraphSnapshot> {
            // 外部editorの連続書込が静まってから調査する。待機中はCPUを回さず取消も受け付ける。
            const auto quietUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(750);
            while (std::chrono::steady_clock::now() < quietUntil) {
                if (stop.stop_requested()) {
                    return std::unexpected(Error(ErrorCategory::Resource, 296, "Dependency index cancelled."));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            ContentGraphSnapshot graph;
            const ContentItemRegistry registry;
            for (const auto &root : roots) {
                if (root.readOnly || stop.stop_requested()) {
                    continue;
                }
                const auto entries = ContentIndex::Build(root.path, registry, false, stop);
                if (!entries) {
                    graph.warnings.push_back(entries.error().GetMessageText());
                    continue;
                }
                for (const auto &entry : *entries) {
                    if (entry.directory || stop.stop_requested()) {
                        continue;
                    }
                    auto metadata = ContentMetadataProvider::Read(entry.path, root.path, false, stop);
                    if (!metadata) {
                        graph.warnings.push_back(AssetCatalog::Utf8(entry.path) + ": " +
                                                 metadata.error().GetMessageText());
                        continue;
                    }
                    const auto extension = entry.path.extension();
                    if ((extension == ".json" || extension == ".level" || extension == ".prefab" ||
                         extension == ".nxprefab") &&
                        !sourceTypes.empty()) {
                        const auto text = ContentMetadataProvider::Text(entry.path);
                        if (text) {
                            for (const auto &[id, sources] : sourceTypes) {
                                if (text->find("\"" + id + "\"") != std::string::npos) {
                                    for (const auto &source : sources) {
                                        std::filesystem::path actual = source;
                                        bool missing = true;
                                        for (const auto &sourceRoot : roots) {
                                            const auto relative = source.lexically_relative(sourceRoot.path);
                                            if (relative.empty() || relative.is_absolute() ||
                                                *relative.begin() == "..") {
                                                continue;
                                            }
                                            const auto resolved =
                                                ResolveAssetReference(sourceRoot.path, AssetCatalog::Utf8(relative));
                                            std::error_code error;
                                            if (resolved && std::filesystem::is_regular_file(*resolved, error) &&
                                                !error) {
                                                actual = *resolved;
                                                missing = false;
                                                break;
                                            }
                                        }
                                        metadata->dependencies.push_back({id, actual, missing});
                                    }
                                }
                            }
                        }
                    }
                    for (auto &dependency : metadata->dependencies) {
                        if (dependency.missing) {
                            for (const auto &other : roots) {
                                const auto resolved = ResolveAssetReference(other.path, dependency.reference);
                                std::error_code error;
                                if (resolved && std::filesystem::is_regular_file(*resolved, error) && !error) {
                                    dependency.resolved = *resolved;
                                    dependency.missing = false;
                                    break;
                                }
                            }
                        }
                        if (!dependency.resolved.empty()) {
                            auto &from = graph.references[dependency.resolved];
                            if (std::ranges::find(from, entry.path) == from.end()) {
                                from.push_back(entry.path);
                            }
                        }
                    }
                    graph.files.emplace(entry.path, std::move(*metadata));
                }
            }
            if (stop.stop_requested()) {
                return std::unexpected(Error(ErrorCategory::Resource, 296, "Dependency index cancelled."));
            }
            return graph;
        });
}
void ContentDependencyGraph::Poll() {
    if (!task_.valid() || task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    try {
        auto result = task_.get();
        if (result) {
            snapshot_ = std::make_shared<const ContentGraphSnapshot>(std::move(*result));
            ++revision_;
        } else {
            error_ = result.error().GetMessageText();
        }
    } catch (const std::exception &error) {
        error_ = error.what();
    }
}
bool ContentDependencyGraph::Busy() const {
    return task_.valid();
}
bool ContentDependencyGraph::Dirty() const {
    return dirty_;
}
size_t ContentDependencyGraph::Revision() const {
    return revision_;
}
std::shared_ptr<const ContentGraphSnapshot> ContentDependencyGraph::Snapshot() const {
    return snapshot_;
}
const std::string &ContentDependencyGraph::ErrorText() const {
    return error_;
}
} // namespace NexusEngine
