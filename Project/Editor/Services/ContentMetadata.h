#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <map>
#include <vector>
#include <optional>
#include <stop_token>
#include <future>

namespace NexusEngine {
struct ContentDependency {
    std::string reference;
    std::filesystem::path resolved;
    bool missing = false;
};
struct ContentMetadata {
    std::filesystem::path file;
    std::string guid, language, className, nameSpace, error;
    uintmax_t bytes = 0;
    std::map<std::string, std::string> fields;
    std::vector<ContentDependency> dependencies;
    std::optional<uint64_t> triangles;
};
/*-----------------------------------------------------------------------------------------
 * ContentMetadataProvider
 * - 形式固有のheader/JSON/SourceをCPUで解釈する。UIとGPUロードから分離する
 * - 外部URIは実行せず、依存の読取は既存root resolverへ限定する
 *---------------------------------------------------------------------------------------*/
class ContentMetadataProvider final {
  public:
    static Result<ContentMetadata> Read(const std::filesystem::path &file, const std::filesystem::path &root,
                                        bool textureDetails = true, std::stop_token stop = {});
    static Result<std::string> Text(const std::filesystem::path &file, size_t limit = 16 * 1024 * 1024);
};
class ContentMetadataRequest final {
  public:
    ~ContentMetadataRequest();
    void Request(std::filesystem::path file, std::filesystem::path root);
    void Invalidate();
    void Poll();
    bool Busy() const;
    const std::optional<ContentMetadata> &Value() const;

  private:
    std::future<Result<ContentMetadata>> task_;
    std::stop_source cancellation_;
    std::optional<ContentMetadata> value_;
    std::filesystem::path requested_, root_, running_;
    size_t generation_ = 0, runningGeneration_ = 0;
};
} // namespace NexusEngine
