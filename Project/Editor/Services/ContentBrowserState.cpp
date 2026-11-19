#include "ContentBrowserState.h"
#include "EditorSettings.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace NexusEngine {
namespace {
std::string Lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
struct NumericCondition {
    std::string operation;
    double value = 0;
};
std::optional<NumericCondition> Numeric(std::string text) {
    NumericCondition result{"=", 0};
    if (!text.empty() && (text[0] == '>' || text[0] == '<' || text[0] == '=')) {
        result.operation = text.substr(0, 1);
        text.erase(0, 1);
        if (!text.empty() && text[0] == '=') {
            result.operation += '=';
            text.erase(0, 1);
        }
    }
    try {
        size_t end = 0;
        result.value = std::stod(text, &end);
        if (!std::isfinite(result.value) || result.value < 0) {
            return {};
        }
        const auto unit = Lower(text.substr(end));
        double multiplier = 1;
        if (unit == "kb") {
            multiplier = 1000;
        } else if (unit == "mb") {
            multiplier = 1000000;
        } else if (unit == "gb") {
            multiplier = 1000000000;
        } else if (unit == "kib") {
            multiplier = 1024;
        } else if (unit == "mib") {
            multiplier = 1024 * 1024;
        } else if (unit == "gib") {
            multiplier = 1024 * 1024 * 1024;
        } else if (!unit.empty() && unit != "b") {
            return {};
        }
        result.value *= multiplier;
        return result;
    } catch (...) {
        return {};
    }
}
bool Compare(double actual, const NumericCondition &value) {
    if (value.operation == ">") {
        return actual > value.value;
    }
    if (value.operation == ">=") {
        return actual >= value.value;
    }
    if (value.operation == "<") {
        return actual < value.value;
    }
    if (value.operation == "<=") {
        return actual <= value.value;
    }
    return actual == value.value;
}
std::optional<std::pair<std::time_t, std::time_t>> ModifiedRange(std::string value) {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);
    local.tm_hour = local.tm_min = local.tm_sec = 0;
    local.tm_isdst = -1;
    if (value == "today" || value == "yesterday") {
        if (value == "yesterday") {
            --local.tm_mday;
        }
        const auto first = std::mktime(&local);
        ++local.tm_mday;
        local.tm_isdst = -1;
        return std::pair{first, std::mktime(&local)};
    }
    if (value.size() > 1 && value.back() == 'd') {
        const auto days = Numeric(value.substr(0, value.size() - 1));
        if (!days || days->value > 365000) {
            return {};
        }
        return std::pair{now - static_cast<std::time_t>(days->value * 86400), now + 1};
    }
    std::istringstream input(value);
    std::tm parsed{};
    input >> std::get_time(&parsed, "%Y-%m-%d");
    if (input.fail()) {
        return {};
    }
    input >> std::ws;
    if (!input.eof()) {
        return {};
    }
    const std::chrono::year_month_day date{std::chrono::year{parsed.tm_year + 1900},
                                           std::chrono::month{static_cast<unsigned>(parsed.tm_mon + 1)},
                                           std::chrono::day{static_cast<unsigned>(parsed.tm_mday)}};
    if (!date.ok()) {
        return {};
    }
    parsed.tm_isdst = -1;
    const auto first = std::mktime(&parsed);
    ++parsed.tm_mday;
    parsed.tm_isdst = -1;
    return std::pair{first, std::mktime(&parsed)};
}

} // namespace
void ContentBrowserNavigationHistory::Visit(ContentLocation location) {
    if (!locations_.empty() && locations_[cursor_] == location) {
        return;
    }
    if (!locations_.empty()) {
        locations_.resize(cursor_ + 1);
    }
    locations_.push_back(std::move(location));
    if (locations_.size() > 128) {
        locations_.erase(locations_.begin());
    }
    cursor_ = locations_.size() - 1;
}
std::optional<ContentLocation> ContentBrowserNavigationHistory::Back() {
    if (!CanBack()) {
        return {};
    }
    return locations_[--cursor_];
}
std::optional<ContentLocation> ContentBrowserNavigationHistory::Forward() {
    if (!CanForward()) {
        return {};
    }
    return locations_[++cursor_];
}
bool ContentBrowserNavigationHistory::CanBack() const {
    return !locations_.empty() && cursor_ > 0;
}
bool ContentBrowserNavigationHistory::CanForward() const {
    return !locations_.empty() && cursor_ + 1 < locations_.size();
}
ContentSearchQuery ContentSearchParser::Parse(std::string_view text) {
    ContentSearchQuery query;
    std::string token;
    bool quoted = false;
    const auto append = [&] {
        if (token.empty()) {
            return;
        }
        const auto colon = token.find(':');
        ContentSearchTerm term;
        if (colon != std::string::npos) {
            term.field = Lower(token.substr(0, colon));
            term.value = Lower(token.substr(colon + 1));
            if (term.field == "path") {
                std::ranges::replace(term.value, static_cast<char>(92), '/');
            }
            if (term.field != "type" && term.field != "ext" && term.field != "path" && term.field != "size" &&
                term.field != "triangles" && term.field != "modified") {
                query.error = "Unknown search field: " + term.field;
            }
            if ((term.field == "size" || term.field == "triangles") && !Numeric(term.value)) {
                query.error = "Invalid numeric search condition: " + term.value;
            }
            if (term.field == "modified") {
                auto date = term.value;
                while (!date.empty() && (date.front() == '>' || date.front() == '<' || date.front() == '=')) {
                    date.erase(0, 1);
                }
                const auto operation = term.value.substr(0, term.value.size() - date.size());
                if (operation != "" && operation != "=" && operation != ">" && operation != "<" && operation != ">=" &&
                    operation != "<=") {
                    query.error = "Invalid modified comparison operator.";
                }
                if (!ModifiedRange(date)) {
                    query.error = "Use modified:today, yesterday, 7d or YYYY-MM-DD.";
                }
            }
            if (term.value.empty()) {
                query.error = "Empty search value";
            }
        } else {
            term.value = Lower(token);
        }
        query.terms.push_back(std::move(term));
        token.clear();
    };
    for (char c : text) {
        if (c == '"') {
            quoted = !quoted;
        } else if (std::isspace(static_cast<unsigned char>(c)) && !quoted) {
            append();
        } else {
            token += c;
        }
    }
    append();
    if (quoted) {
        query.error = "Unclosed search quote";
    }
    return query;
}
bool ContentSearchParser::Matches(const ContentSearchQuery &query, const AssetEntry &entry,
                                  const ContentItemRegistry &registry) {
    if (!query.error.empty()) {
        return false;
    }
    const auto &type = registry.Find(entry.typeKey);
    for (const auto &term : query.terms) {
        std::string candidate;
        if (term.field == "size" || term.field == "triangles") {
            const auto condition = Numeric(term.value);
            if (!condition || (term.field == "triangles" && !entry.triangles)) {
                return false;
            }
            const double actual =
                term.field == "size" ? static_cast<double>(entry.bytes) : static_cast<double>(*entry.triangles);
            if (!Compare(actual, *condition)) {
                return false;
            }
            continue;
        }
        if (term.field == "modified") {
            auto value = term.value;
            std::string operation;
            while (!value.empty() && (value.front() == '>' || value.front() == '<' || value.front() == '=')) {
                operation += value.front();
                value.erase(0, 1);
            }
            const auto range = ModifiedRange(value);
            if (!range) {
                return false;
            }
            const auto actual = std::chrono::system_clock::to_time_t(
                std::chrono::clock_cast<std::chrono::system_clock>(entry.modified));
            const bool matches = operation == ">"    ? actual >= range->second
                                 : operation == ">=" ? actual >= range->first
                                 : operation == "<"  ? actual < range->first
                                 : operation == "<=" ? actual < range->second
                                                     : actual >= range->first && actual < range->second;
            if (!matches) {
                return false;
            }
            continue;
        }
        if (term.field == "type") {
            candidate = Lower(type.id);
            if (term.value == "cpp" && type.id == "Header") {
                continue;
            }
            if (candidate != term.value) {
                return false;
            }
        } else {
            if (term.field == "ext") {
                candidate = AssetCatalog::Utf8(entry.path.extension());
            } else if (term.field == "path") {
                candidate = AssetCatalog::Utf8(entry.path.generic_wstring());
            } else {
                candidate = entry.name;
            }
            if (Lower(candidate).find(term.value) == std::string::npos) {
                return false;
            }
        }
    }
    return true;
}
std::vector<AssetEntry> ContentBrowserState::Visible(std::span<const AssetEntry> entries,
                                                     const ContentItemRegistry &registry) const {
    const auto query = ContentSearchParser::Parse(search);
    std::vector<AssetEntry> visible;
    for (const auto &entry : entries) {
        if ((!showFolders && entry.directory) || (!showSourceFiles && registry.Find(entry.typeKey).source)) {
            continue;
        }
        if (hiddenTypes.contains(entry.typeKey) || (!filters.empty() && !filters.contains(entry.typeKey))) {
            continue;
        }
        if (ContentSearchParser::Matches(query, entry, registry)) {
            visible.push_back(entry);
        }
    }
    std::ranges::sort(visible, [&](const auto &a, const auto &b) {
        if (folderFirst && a.directory != b.directory) {
            return a.directory;
        }
        int order = 0;
        if (sort == ContentSort::Size && a.bytes != b.bytes) {
            order = a.bytes < b.bytes ? -1 : 1;
        } else if (sort == ContentSort::Modified && a.modified != b.modified) {
            order = a.modified < b.modified ? -1 : 1;
        } else if (sort == ContentSort::Type && a.typeKey != b.typeKey) {
            order = a.typeKey < b.typeKey ? -1 : 1;
        } else {
            const auto first = Lower(a.name), second = Lower(b.name);
            order = first < second ? -1 : first > second ? 1 : 0;
            if (order == 0) {
                order = a.path < b.path ? -1 : a.path > b.path ? 1 : 0;
            }
        }
        return descending ? order > 0 : order < 0;
    });
    return visible;
}
void ContentBrowserState::Select(std::span<const AssetEntry> visible, size_t index, bool ctrl, bool shift) {
    if (index >= visible.size()) {
        return;
    }
    const auto path = visible[index].path;
    if (shift && !anchor_.empty()) {
        const auto anchor = std::ranges::find(visible, anchor_, &AssetEntry::path);
        if (!ctrl) {
            selection.clear();
        }
        const auto start = anchor == visible.end() ? index : static_cast<size_t>(anchor - visible.begin());
        for (size_t i = std::min(start, index); i <= std::max(start, index); ++i) {
            if (!Selected(visible[i].path)) {
                selection.push_back(visible[i].path);
            }
        }
    } else {
        if (!ctrl) {
            selection.clear();
        }
        const auto found = std::ranges::find(selection, path);
        if (found == selection.end()) {
            selection.push_back(path);
        } else if (ctrl) {
            selection.erase(found);
        }
        anchor_ = path;
    }
}
bool ContentBrowserState::Selected(const std::filesystem::path &path) const {
    return std::ranges::find(selection, path) != selection.end();
}
void ContentBrowserState::ToggleFavorite(const std::filesystem::path &path) {
    const auto found = std::ranges::find(favorites, path);
    if (found != favorites.end()) {
        favorites.erase(found);
    } else if (favorites.size() < 256) {
        favorites.push_back(path);
    }
}
void ContentBrowserState::Load(EditorSettingsStore *settings, const std::string &key) {
    if (!settings) {
        return;
    }
    const auto found = settings->user.contentBrowser.find(key);
    if (found == settings->user.contentBrowser.end()) {
        return;
    }
    // 壊れた個人設定は既定値へ戻し、ファイル操作やパスの自動生成へ使わない。
    try {
        const auto json = nlohmann::json::parse(found->second);
        view = static_cast<ContentViewMode>(std::clamp(json.value("view", 1), 0, 2));
        sort = static_cast<ContentSort>(std::clamp(json.value("sort", 0), 0, 3));
        filters = json.value("filters", std::set<std::string>{});
        hiddenTypes = json.value("hiddenTypes", std::set<std::string>{});
        descending = json.value("descending", false);
        folderFirst = json.value("folderFirst", true);
        showSources = json.value("sources", true);
        showFolders = json.value("folders", true);
        showSourceFiles = json.value("sourceFiles", true);
        thumbnailSize = std::clamp(json.value("thumbnail", 80.f), 40.f, 160.f);
        if (!std::isfinite(thumbnailSize)) {
            thumbnailSize = 80;
        }
        for (const auto &value : json.value("favorites", std::vector<std::string>{})) {
            if (value.size() < 32768 && favorites.size() < 256) {
                favorites.emplace_back(ConvertString(value));
            }
        }
    } catch (...) {
        *this = ContentBrowserState{};
    }
}
bool ContentBrowserState::Save(EditorSettingsStore *settings, const std::string &key) const {
    if (!settings) {
        return true;
    }
    std::vector<std::string> paths;
    for (const auto &path : favorites) {
        paths.push_back(AssetCatalog::Utf8(path));
    }
    const nlohmann::json json{
        {"view", static_cast<int>(view)}, {"sort", static_cast<int>(sort)}, {"descending", descending},
        {"folderFirst", folderFirst},     {"sources", showSources},         {"folders", showFolders},
        {"sourceFiles", showSourceFiles}, {"thumbnail", thumbnailSize},     {"favorites", paths}};
    auto serialized = json;
    serialized["filters"] = filters;
    serialized["hiddenTypes"] = hiddenTypes;
    settings->user.contentBrowser[key] = serialized.dump();
    return settings->SaveUser();
}
} // namespace NexusEngine
