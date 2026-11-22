#pragma once

// Side: CPU

#include <filesystem>
#include <string>
#include <string_view>

namespace thesis::app {

// A path as UTF-8 text, for ImGui, messages, commands and files. WHY not path::string(): on
// Windows it converts to the ANSI code page and throws on a character outside it.
[[nodiscard]] std::string utf8(const std::filesystem::path& path);
// The same with forward slashes, for a command line that works in every shell.
[[nodiscard]] std::string utf8Generic(const std::filesystem::path& path);

// The path that UTF-8 text names (the other way round).
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text);

} // namespace thesis::app
