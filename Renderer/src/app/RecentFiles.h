#pragma once

// Side: CPU

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace thesis::app {

// Files the user opened lately, newest first, one list per kind ("meshes", "models"). Kept in a
// small JSON file between runs, so that a file far from the model folder is one click away.
class RecentFiles {
public:
    static constexpr std::size_t kMaxPerKind = 8;

    // No storage: the lists live only while the program runs (used by --self-test).
    RecentFiles() = default;
    // Reads the lists from `storage` if it exists. A damaged file counts as empty and is
    // rewritten at the next add(): the lists are a convenience, not data.
    explicit RecentFiles(std::filesystem::path storage);

    // Puts `file` at the front of list `kind` (moving it there if it is in the list already) and
    // writes the storage file. Throws std::runtime_error if the storage file cannot be written.
    void add(const std::string& kind, const std::filesystem::path& file);
    // Takes `file` out of every list and writes the storage file. Throws as add() does.
    void remove(const std::filesystem::path& file);
    // The files of `kind` that still exist, newest first.
    [[nodiscard]] std::vector<std::filesystem::path> list(const std::string& kind) const;

    // %LOCALAPPDATA%/thesis_renderer/recent.json; empty if LOCALAPPDATA is not set.
    [[nodiscard]] static std::filesystem::path defaultStorage();

private:
    void save() const;

    std::filesystem::path storage_;
    std::map<std::string, std::vector<std::filesystem::path>> lists_;
};

} // namespace thesis::app
