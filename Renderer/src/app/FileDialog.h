#pragma once

// Side: View

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace thesis::app {

// One line of the file type list of the dialog: "Mesh files" with extensions {"ply", "obj"}.
struct FileFilter {
    std::string name;
    std::vector<std::string> extensions; // without the dot
};

// The file dialog of the system, to pick one file. Blocks until the user closes it. Returns
// nothing if the user cancelled. `startDirectory` may be empty, relative or missing; a missing
// folder opens the dialog where it opened last. Throws std::runtime_error if the
// dialog cannot be opened.
[[nodiscard]] std::optional<std::filesystem::path> pickFile(const std::vector<FileFilter>& filters,
                                                            const std::filesystem::path& startDirectory);

// The save dialog of the system: a file name to write, starting at `suggested` (folder and name).
// Returns nothing if the user cancelled. Throws std::runtime_error if the dialog cannot be opened.
[[nodiscard]] std::optional<std::filesystem::path> pickSaveFile(const std::vector<FileFilter>& filters,
                                                                const std::filesystem::path& suggested);

} // namespace thesis::app
