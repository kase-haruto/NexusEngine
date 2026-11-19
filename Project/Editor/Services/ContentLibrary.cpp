#include "ContentLibrary.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <algorithm>
#include <fstream>

namespace NexusEngine {
namespace {
using Json = nlohmann::json;
Error Invalid(std::string text) {
    return Error(ErrorCategory::Resource, 292, std::move(text));
}
bool NameValid(std::string_view value) {
    return !value.empty() && value.size() <= 256 && value.find_first_of("\r\n") == std::string_view::npos;
}
Json Ref(const ContentReference &value) {
    return {{"root", value.root}, {"reference", value.reference}};
}
std::vector<ContentReference> Refs(const Json &array, size_t limit) {
    std::vector<ContentReference> result;
    if (!array.is_array() || array.size() > limit) {
        throw std::runtime_error("Content reference limit exceeded.");
    }
    for (const auto &value : array) {
        ContentReference ref{value.at("root").get<std::string>(), value.at("reference").get<std::string>()};
        if (ref.root.empty() || ref.root.size() > 1024 || ref.reference.empty() || ref.reference.size() > 32768) {
            throw std::runtime_error("Invalid Content reference.");
        }
        if (std::ranges::find(result, ref) == result.end()) {
            result.push_back(std::move(ref));
        }
    }
    return result;
}
Json EncodeCollections(const std::vector<ContentCollection> &values, bool shared) {
    auto result = Json::array();
    for (const auto &value : values) {
        if (value.shared != shared) {
            continue;
        }
        auto items = Json::array();
        for (const auto &item : value.items) {
            items.push_back(Ref(item));
        }
        result.push_back({{"id", value.id}, {"name", value.name}, {"items", std::move(items)}});
    }
    return result;
}
std::string EncodeLocal(const std::vector<ContentCollection> &collections,
                        const std::vector<ContentReference> &recentItems,
                        const std::vector<ContentSavedSearch> &savedSearches) {
    auto recent = Json::array(), searches = Json::array();
    for (const auto &item : recentItems) {
        recent.push_back(Ref(item));
    }
    for (const auto &item : savedSearches) {
        auto scopes = Json::array();
        for (const auto &scope : item.scopes) {
            scopes.push_back(Ref(scope));
        }
        searches.push_back({{"name", item.name},
                            {"query", item.query},
                            {"recursive", item.recursive},
                            {"scopes", std::move(scopes)},
                            {"filters", item.filters}});
    }
    return Json{{"schemaVersion", 1},
                {"collections", EncodeCollections(collections, false)},
                {"recent", std::move(recent)},
                {"searches", std::move(searches)}}
        .dump();
}
} // namespace
void ContentLibrary::Initialize(EditorSettingsStore *settings, std::string projectKey, std::filesystem::path config) {
    settings_ = settings;
    key_ = std::move(projectKey) + ".library";
    sharedFile_ = config.empty() ? std::filesystem::path{} : config / "ContentCollections.json";
    static_cast<void>(Reload());
}
Result<void> ContentLibrary::Reload() {
    try {
        std::vector<ContentCollection> loaded;
        std::string loadedSharedBytes;
        const auto read = [&](const Json &json, bool shared) {
            if (json.value("schemaVersion", 1) != 1) {
                throw std::runtime_error("Unsupported collection schema.");
            }
            const auto values = json.value("collections", Json::array());
            if (!values.is_array() || values.size() > 256) {
                throw std::runtime_error("Collection limit exceeded.");
            }
            for (const auto &value : values) {
                ContentCollection collection{value.at("id").get<std::string>(), value.at("name").get<std::string>(),
                                             shared, Refs(value.value("items", Json::array()), 10000)};
                if (!NameValid(collection.name) || collection.id.empty() || collection.id.size() > 128) {
                    throw std::runtime_error("Invalid collection metadata.");
                }
                if (std::ranges::any_of(loaded, [&](const auto &old) {
                        return old.id == collection.id;
                    })) {
                    throw std::runtime_error("Duplicate collection id.");
                }
                loaded.push_back(std::move(collection));
            }
        };
        Json local = Json::object();
        if (settings_) {
            const auto found = settings_->user.contentBrowser.find(key_);
            if (found != settings_->user.contentBrowser.end()) {
                local = Json::parse(found->second);
                read(local, false);
            }
        }
        std::error_code error;
        if (!sharedFile_.empty() && std::filesystem::exists(sharedFile_, error)) {
            if (std::filesystem::file_size(sharedFile_, error) > 16 * 1024 * 1024 || error) {
                return std::unexpected(Invalid("Collection file is too large or unreadable."));
            }
            std::ifstream input(sharedFile_);
            std::string bytes((std::istreambuf_iterator<char>(input)), {});
            Json shared = Json::parse(bytes);
            loadedSharedBytes = std::move(bytes);
            read(shared, true);
        }
        auto recent = Refs(local.value("recent", Json::array()), 100);
        std::vector<ContentSavedSearch> searches;
        for (const auto &value : local.value("searches", Json::array())) {
            if (searches.size() >= 100) {
                break;
            }
            ContentSavedSearch search{value.at("name").get<std::string>(), value.at("query").get<std::string>(),
                                      value.value("recursive", true)};
            if (!NameValid(search.name) || search.query.size() > 4096) {
                throw std::runtime_error("Invalid saved search.");
            }
            search.scopes = Refs(value.value("scopes", Json::array()), 256);
            search.filters = value.value("filters", std::vector<std::string>{});
            if (search.filters.size() > 256) {
                throw std::runtime_error("Saved search filter limit exceeded.");
            }
            searches.push_back(std::move(search));
        }
        collections_ = std::move(loaded);
        recent_ = std::move(recent);
        searches_ = std::move(searches);
        sharedValue_ = Json{{"schemaVersion", 1}, {"collections", EncodeCollections(collections_, true)}}.dump(2);
        localValue_ = EncodeLocal(collections_, recent_, searches_);
        sharedBytes_ = std::move(loadedSharedBytes);
        ++generation_;
        return {};
    } catch (const std::exception &error) {
        return std::unexpected(Invalid(error.what()));
    }
}
Result<void> ContentLibrary::Store() {
    const auto shared = Json{{"schemaVersion", 1}, {"collections", EncodeCollections(collections_, true)}}.dump(2);
    const auto local = EncodeLocal(collections_, recent_, searches_);
    if (!sharedFile_.empty() && shared != sharedValue_) {
        // 読込時の内容と比較し、他Editorの変更を上書きしない。Local変更はSharedを書かない。
        const auto written = WithFileLock(sharedFile_, [&]() -> Result<void> {
            std::ifstream input(sharedFile_, std::ios::binary);
            const std::string current((std::istreambuf_iterator<char>(input)), {});
            // Windowsでは読取handleを閉じてからAtomic Replaceを行う。
            input.close();
            if (current != sharedBytes_) {
                return std::unexpected(
                    Invalid("Shared collections changed externally. Refresh collections before retrying."));
            }
            std::error_code error;
            std::filesystem::create_directories(sharedFile_.parent_path(), error);
            if (error) {
                return std::unexpected(Invalid(error.message()));
            }
            return WriteFileAtomically(sharedFile_, shared);
        });
        if (!written) {
            return written;
        }
        sharedBytes_ = shared;
        sharedValue_ = shared;
    }
    if (settings_ && local != localValue_) {
        const auto before = settings_->user.contentBrowser[key_];
        settings_->user.contentBrowser[key_] = local;
        if (!settings_->SaveUser()) {
            settings_->user.contentBrowser[key_] = before;
            return std::unexpected(Invalid("Could not save local collections and recent items."));
        }
        localValue_ = local;
    }
    ++generation_;
    return {};
}
Result<std::string> ContentLibrary::Create(std::string name, bool shared) {
    if (!NameValid(name) || collections_.size() >= 256 || (shared && sharedFile_.empty()) ||
        std::ranges::any_of(collections_, [&](const auto &value) {
            return value.name == name && value.shared == shared;
        })) {
        return std::unexpected(Invalid("Enter a unique collection name (maximum 256 collections)."));
    }
    const auto id = CreateUuid();
    if (!id) {
        return std::unexpected(id.error());
    }
    collections_.push_back({*id, std::move(name), shared, {}});
    const auto stored = Store();
    if (!stored) {
        collections_.pop_back();
        return std::unexpected(stored.error());
    }
    return *id;
}
Result<void> ContentLibrary::Rename(std::string_view id, std::string name) {
    auto found = std::ranges::find(collections_, id, &ContentCollection::id);
    if (found == collections_.end() || !NameValid(name) || std::ranges::any_of(collections_, [&](const auto &value) {
            return value.id != id && value.shared == found->shared && value.name == name;
        })) {
        return std::unexpected(Invalid("Invalid or duplicate collection name."));
    }
    auto before = found->name;
    found->name = std::move(name);
    const auto result = Store();
    if (!result) {
        found->name = std::move(before);
    }
    return result;
}
Result<void> ContentLibrary::Erase(std::string_view id) {
    const auto before = collections_;
    std::erase_if(collections_, [&](const auto &value) {
        return value.id == id;
    });
    const auto result = Store();
    if (!result) {
        collections_ = before;
    }
    return result;
}
Result<void> ContentLibrary::Add(std::string_view id, std::span<const ContentReference> items) {
    auto found = std::ranges::find(collections_, id, &ContentCollection::id);
    if (found == collections_.end() || found->items.size() + items.size() > 10000) {
        return std::unexpected(Invalid("Collection not found or full."));
    }
    const auto before = found->items;
    for (const auto &item : items) {
        if (std::ranges::find(found->items, item) == found->items.end()) {
            found->items.push_back(item);
        }
    }
    const auto result = Store();
    if (!result) {
        found->items = before;
    }
    return result;
}
Result<void> ContentLibrary::Remove(std::string_view id, std::span<const ContentReference> items) {
    auto found = std::ranges::find(collections_, id, &ContentCollection::id);
    if (found == collections_.end()) {
        return std::unexpected(Invalid("Collection not found."));
    }
    const auto before = found->items;
    std::erase_if(found->items, [&](const auto &item) {
        return std::ranges::find(items, item) != items.end();
    });
    const auto result = Store();
    if (!result) {
        found->items = before;
    }
    return result;
}
Result<void> ContentLibrary::Opened(ContentReference item) {
    const auto before = recent_;
    std::erase(recent_, item);
    recent_.insert(recent_.begin(), std::move(item));
    if (recent_.size() > 100) {
        recent_.resize(100);
    }
    const auto result = Store();
    if (!result) {
        recent_ = before;
    }
    return result;
}
Result<void> ContentLibrary::SaveSearch(ContentSavedSearch search) {
    if (!NameValid(search.name) || search.query.size() > 4096) {
        return std::unexpected(Invalid("Invalid saved search."));
    }
    const auto before = searches_;
    std::erase_if(searches_, [&](const auto &value) {
        return value.name == search.name;
    });
    if (searches_.size() >= 100) {
        return std::unexpected(Invalid("Saved search limit exceeded."));
    }
    searches_.push_back(std::move(search));
    const auto result = Store();
    if (!result) {
        searches_ = before;
    }
    return result;
}
Result<void> ContentLibrary::RemoveSearch(std::string_view name) {
    const auto before = searches_;
    std::erase_if(searches_, [&](const auto &value) {
        return value.name == name;
    });
    const auto result = Store();
    if (!result) {
        searches_ = before;
    }
    return result;
}
std::span<const ContentCollection> ContentLibrary::Collections() const {
    return collections_;
}
std::span<const ContentReference> ContentLibrary::Recent() const {
    return recent_;
}
std::span<const ContentSavedSearch> ContentLibrary::Searches() const {
    return searches_;
}
const ContentCollection *ContentLibrary::Find(std::string_view id) const {
    const auto found = std::ranges::find(collections_, id, &ContentCollection::id);
    return found == collections_.end() ? nullptr : &*found;
}
size_t ContentLibrary::Generation() const noexcept {
    return generation_;
}
} // namespace NexusEngine
