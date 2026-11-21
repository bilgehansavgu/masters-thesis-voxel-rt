#pragma once

// Side: View

#include <filesystem>

namespace thesis::app {

// File actions of the desktop shell, for the model list of the window.

// Opens the folder of `file` in Explorer with the file selected. Throws std::runtime_error if
// Explorer cannot be started.
void revealFile(const std::filesystem::path& file);

// Moves `file` to the Recycle Bin, so the user can restore it. On a drive without a Recycle Bin
// Windows asks before it deletes the file for good. Returns false if the user said no there.
// Throws std::runtime_error if the file cannot be moved (missing, in use).
[[nodiscard]] bool recycleFile(const std::filesystem::path& file);

} // namespace thesis::app
