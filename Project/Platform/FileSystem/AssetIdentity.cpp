#include "AssetIdentity.h"
#include "AtomicFile.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <fstream>
#include <map>
#include <mutex>

namespace NexusEngine {
namespace {
using Json = nlohmann::json;
Error IdentityError(const std::string& message) {
    return Error(ErrorCategory::Resource, 399, "Asset identity: " + message);
}
std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
bool ValidId(const std::string& id) {
    if(id.size() != 36) {
        return false;
    }
    for(size_t index = 0; index < id.size(); ++index) {
        if(index == 8 || index == 13 || index == 18 || index == 23) {
            if(id[index] != '-') {
                return false;
            }
        } else if(!((id[index] >= '0' && id[index] <= '9') || (id[index] >= 'a' && id[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}
Result<Json> ReadIdentityJson(const std::filesystem::path& file, size_t limit) {
    try {
        if(std::filesystem::file_size(file) > limit) {
            return std::unexpected(IdentityError("metadata is too large"));
        }
        std::ifstream input(NativeFilePath(file));
        const auto json = Json::parse(input);
        if(!json.is_object() || json.at("version") != 1) {
            return std::unexpected(IdentityError("unsupported metadata"));
        }
        return json;
    } catch(const std::exception& error) {
        return std::unexpected(IdentityError(error.what()));
    }
}
Result<Json> ReadRegistry(const std::filesystem::path& root) {
    auto path = ResolveFileWithinRoot(root, ".nexusassets.json");
    if(!path) {
        return std::unexpected(path.error());
    }
    if(!std::filesystem::exists(*path)) {
        return Json{{"version", 1}, {"assets", Json::object()}};
    }
    auto json = ReadIdentityJson(*path, 16 * 1024 * 1024);
    if(!json || !json->contains("assets") || !json->at("assets").is_object() || json->at("assets").size() > 100000) {
        return std::unexpected(IdentityError("invalid registry"));
    }
    for(const auto& [id, entry] : json->at("assets").items()) {
        if(!ValidId(id) || !entry.is_string() || IsAssetIdentity(entry.get<std::string>()) || entry.get_ref<const std::string&>().size() > 32768) {
            return std::unexpected(IdentityError("invalid registry entry"));
        }
    }
    if(json->contains("aliases") && (!json->at("aliases").is_object() || json->at("aliases").size() > 100000)) {
        return std::unexpected(IdentityError("invalid path history"));
    }
    const auto aliases = json->value("aliases", Json::object());
    for(const auto& [pathKey, entry] : aliases.items()) {
        if(pathKey.size() > 32768 || !entry.is_string() || !ValidId(entry.get<std::string>())) {
            return std::unexpected(IdentityError("invalid path history entry"));
        }
    }
    return json;
}
struct IdentitySnapshot {
    std::filesystem::file_time_type modified{};
    uintmax_t bytes = 0;
    Json entries;
    Json aliases;
};
struct IdentityCache {
    std::mutex mutex;
    std::map<std::filesystem::path, IdentitySnapshot> roots;
};
IdentityCache& Cache() {
    static IdentityCache cache;
    return cache;
}
std::string PathKey(const std::string& reference) {
    const auto path = std::filesystem::path(reinterpret_cast<const char8_t*>(reference.c_str())).lexically_normal();
    auto key = Utf8(path);
    for(auto& character : key) {
        if(character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if(character == '\\') {
            character = '/';
        }
    }
    return key;
}
// 呼出側がCache.mutexを保持する。実file更新だけでsnapshotを交換する。
Result<IdentitySnapshot*> Snapshot(const std::filesystem::path& base) {
    auto registry = ResolveFileWithinRoot(base, ".nexusassets.json");
    if(!registry) {
        return std::unexpected(registry.error());
    }
    auto& cache = Cache();
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(*registry, error);
    const auto bytes = error ? uintmax_t{0} : std::filesystem::file_size(*registry, error);
    auto found = cache.roots.find(base);
    if(found == cache.roots.end() || found->second.modified != modified || found->second.bytes != bytes) {
        auto entries = ReadRegistry(base);
        if(!entries) {
            return std::unexpected(entries.error());
        }
        if(cache.roots.size() >= 32) {
            cache.roots.clear();
        }
        found = cache.roots.insert_or_assign(base, IdentitySnapshot{modified, bytes, entries->at("assets"), entries->value("aliases", Json::object())}).first;
    }
    return &found->second;
}
void Invalidate(const std::filesystem::path& root) {
    auto& cache = Cache();
    std::lock_guard lock(cache.mutex);
    cache.roots.erase(std::filesystem::weakly_canonical(root));
}
}
bool IsAssetIdentity(const std::string& reference) {
    return reference.starts_with("asset://");
}
Result<std::string> EnsureAssetIdentity(const std::filesystem::path& root, const std::filesystem::path& file) {
    const auto base = std::filesystem::weakly_canonical(root);
    const auto absolute = file.is_absolute() ? file : base / file;
    const auto relative = Utf8(absolute.lexically_relative(base));
    auto asset = ResolveFileWithinRoot(base, relative);
    auto meta = ResolveFileWithinRoot(base, relative + ".nxmeta");
    auto registry = ResolveFileWithinRoot(base, ".nexusassets.json");
    if(!asset || !meta || !registry || !std::filesystem::is_regular_file(*asset)) {
        return std::unexpected(IdentityError("choose an existing asset inside the root"));
    }
    std::string id;
    auto result = WithFileLock(*registry, [&]() -> Result<void> {
        auto entries = ReadRegistry(base);
        if(!entries) {
            return std::unexpected(entries.error());
        }
        if(std::filesystem::exists(*meta)) {
            auto json = ReadIdentityJson(*meta, 65536);
            if(!json || !json->contains("id") || !json->at("id").is_string()) {
                return std::unexpected(IdentityError("invalid asset sidecar"));
            }
            id = json->at("id").get<std::string>();
            if(!ValidId(id)) {
                return std::unexpected(IdentityError("invalid UUID"));
            }
        }
        if(!id.empty() && entries->at("assets").contains(id)) {
            const auto previous = entries->at("assets").at(id).get<std::string>();
            auto previousPath = ResolveFileWithinRoot(base, previous);
            if(previousPath && !SameFilePath(*previousPath, *asset) && std::filesystem::exists(*previousPath)) {
                // sidecarごと複製されたfileへは新IDを発行し、原本の参照を奪わない。
                id.clear();
            }
        }
        if(id.empty()) {
            auto generated = CreateUuid();
            if(!generated) {
                return std::unexpected(generated.error());
            }
            id = *generated;
            const auto saved = WriteFileAtomically(*meta, Json{{"version", 1}, {"id", id}}.dump(2) + "\n");
            if(!saved) {
                return saved;
            }
        }
        entries->at("assets")[id] = relative;
        (*entries)["aliases"][PathKey(relative)] = id;
        return WriteFileAtomically(*registry, entries->dump(2) + "\n");
    });
    if(!result) {
        return std::unexpected(result.error());
    }
    Invalidate(base);
    return "asset://" + id;
}
Result<std::filesystem::path> ResolveAssetIdentity(const std::filesystem::path& root, const std::string& reference) {
    if(!IsAssetIdentity(reference) || !ValidId(reference.substr(8))) {
        return std::unexpected(IdentityError("invalid asset reference"));
    }
    const auto id = reference.substr(8);
    const auto base = std::filesystem::weakly_canonical(root);
    auto& cache = Cache();
    std::lock_guard lock(cache.mutex);
    auto snapshot = Snapshot(base);
    if(!snapshot) {
        return std::unexpected(snapshot.error());
    }
    if((*snapshot)->entries.contains(id)) {
        const auto& entry = (*snapshot)->entries.at(id);
        if(!entry.is_string() || IsAssetIdentity(entry.get<std::string>())) {
            return std::unexpected(IdentityError("invalid registry path"));
        }
        auto asset = ResolveFileWithinRoot(base, entry.get<std::string>());
        if(asset && std::filesystem::is_regular_file(*asset)) {
            return asset;
        }
    }
    // 外部でfileとsidecarを一緒に移動した場合も、diskへ書かず現在地を見つける。
    std::optional<std::filesystem::path> relocated;
    std::error_code error;
    for(std::filesystem::recursive_directory_iterator it(base, std::filesystem::directory_options::skip_permission_denied, error), end;
        it != end && !error; it.increment(error)) {
        if(it->is_symlink() || it->path().extension() != ".nxmeta" || !it->is_regular_file()) {
            continue;
        }
        auto json = ReadIdentityJson(it->path(), 65536);
        if(json && json->contains("id") && json->at("id").is_string() && json->at("id").get<std::string>() == id) {
            auto candidate = it->path();
            candidate.replace_extension();
            auto valid = ResolveFileWithinRoot(base, Utf8(candidate.lexically_relative(base)));
            if(valid && std::filesystem::is_regular_file(*valid)) {
                if(relocated) {
                    return std::unexpected(IdentityError("duplicate sidecars claim the same UUID"));
                }
                relocated = *valid;
            }
        }
    }
    if(!relocated) {
        return std::unexpected(IdentityError("asset UUID was not found"));
    }
    (*snapshot)->entries[id] = Utf8(relocated->lexically_relative(base));
    return *relocated;
}
Result<std::filesystem::path> ResolveAssetReference(const std::filesystem::path& root, const std::string& reference) {
    auto direct = ResolveFileWithinRoot(root, reference);
    if(!direct || IsAssetIdentity(reference) || std::filesystem::is_regular_file(*direct)) {
        return direct;
    }
    std::string id;
    {
        auto& cache = Cache();
        std::lock_guard lock(cache.mutex);
        const auto snapshot = Snapshot(std::filesystem::weakly_canonical(root));
        if(!snapshot) {
            return std::unexpected(snapshot.error());
        }
        const auto key = PathKey(reference);
        if((*snapshot)->aliases.contains(key)) {
            id = (*snapshot)->aliases.at(key).get<std::string>();
        }
    }
    return id.empty() ? direct : ResolveAssetIdentity(root, "asset://" + id);
}
Result<void> RenameAssetWithIdentity(const std::filesystem::path& root, const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    const auto base = std::filesystem::weakly_canonical(root);
    auto from = ResolveFileWithinRoot(base, Utf8(source.is_absolute() ? source.lexically_relative(base) : source));
    auto to = ResolveFileWithinRoot(base, Utf8(destination.is_absolute() ? destination.lexically_relative(base) : destination));
    if(!from || !to || !std::filesystem::is_regular_file(*from) || std::filesystem::exists(*to) || from->extension() != to->extension()) {
        return std::unexpected(IdentityError("rename requires a file and unused destination with the same extension"));
    }
    auto id = EnsureAssetIdentity(base, *from);
    if(!id) {
        return std::unexpected(id.error());
    }
    const auto fromMeta = std::filesystem::path(from->wstring() + L".nxmeta");
    const auto toMeta = std::filesystem::path(to->wstring() + L".nxmeta");
    if(std::filesystem::exists(toMeta)) {
        return std::unexpected(IdentityError("rename sidecar destination already exists"));
    }
    std::error_code error;
    std::filesystem::rename(*from, *to, error);
    if(error) {
        return std::unexpected(IdentityError(error.message()));
    }
    std::filesystem::rename(fromMeta, toMeta, error);
    if(error) {
        std::error_code rollback;
        std::filesystem::rename(*to, *from, rollback);
        return std::unexpected(IdentityError("sidecar rename failed: " + error.message()));
    }
    auto updated = EnsureAssetIdentity(base, *to);
    if(!updated) {
        std::error_code rollback;
        std::filesystem::rename(toMeta, fromMeta, rollback);
        std::filesystem::rename(*to, *from, rollback);
        Invalidate(base);
        return std::unexpected(updated.error());
    }
    return {};
}
bool SameAssetReference(const std::filesystem::path& root, const std::string& first, const std::string& second) {
    if(first.empty() || second.empty()) {
        return false;
    }
    auto a = ResolveAssetReference(root, first), b = ResolveAssetReference(root, second);
    return a && b && SameFilePath(*a, *b);
}
}
