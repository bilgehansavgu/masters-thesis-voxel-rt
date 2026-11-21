#include "RecentFiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace thesis::app {
namespace {

// JSON holds UTF-8 text; a path may hold any character.
std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path fromUtf8(const std::string& text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace

RecentFiles::RecentFiles(std::filesystem::path storage) : storage_(std::move(storage))
{
    std::ifstream file(storage_);
    if (!file)
        return; // no file yet: nothing opened so far
    try {
        const nlohmann::json json = nlohmann::json::parse(file);
        for (const auto& [kind, files] : json.items()) {
            for (const nlohmann::json& entry : files)
                lists_[kind].push_back(fromUtf8(entry.get<std::string>()));
        }
    } catch (const nlohmann::json::exception&) {
        lists_.clear();
    }
}

void RecentFiles::add(const std::string& kind, const std::filesystem::path& file)
{
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(file, error).lexically_normal();
    std::vector<std::filesystem::path>& files = lists_[kind];
    files.erase(std::remove(files.begin(), files.end(), absolute), files.end());
    files.insert(files.begin(), absolute);
    if (files.size() > kMaxPerKind)
        files.resize(kMaxPerKind);
    save();
}

void RecentFiles::remove(const std::filesystem::path& file)
{
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(file, error).lexically_normal();
    for (auto& [kind, files] : lists_)
        files.erase(std::remove(files.begin(), files.end(), absolute), files.end());
    save();
}

void RecentFiles::save() const
{
    if (storage_.empty())
        return;
    std::error_code error;
    nlohmann::json json = nlohmann::json::object();
    for (const auto& [name, list] : lists_) {
        nlohmann::json& entries = json[name] = nlohmann::json::array();
        for (const std::filesystem::path& path : list)
            entries.push_back(utf8(path));
    }
    std::filesystem::create_directories(storage_.parent_path(), error);
    std::ofstream out(storage_);
    out << json.dump(2);
    if (!out)
        throw std::runtime_error("Cannot write the list of recent files '" + storage_.string() + "'");
}

std::vector<std::filesystem::path> RecentFiles::list(const std::string& kind) const
{
    std::vector<std::filesystem::path> result;
    const auto it = lists_.find(kind);
    if (it == lists_.end())
        return result;
    for (const std::filesystem::path& path : it->second) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error))
            result.push_back(path);
    }
    return result;
}

std::filesystem::path RecentFiles::defaultStorage()
{
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData == nullptr || *localAppData == '\0')
        return {};
    return std::filesystem::path(localAppData) / "thesis_renderer" / "recent.json";
}

} // namespace thesis::app
