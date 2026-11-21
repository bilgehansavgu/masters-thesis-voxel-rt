#pragma once

// Side: CPU

#include "Options.h"

#include <optional>
#include <string>
#include <vector>

namespace thesis::app {

// The result of reading a command line: the options, or the exit code when the program must stop
// at once (--help, or a wrong argument; CLI11 has printed why).
struct ParsedCommand {
    Options options;
    std::optional<int> exitCode;
};

// Reads the command line of thesis_renderer. main() and the check of "Copy as command" both use
// it, so a written command is read back by the same rules. `arguments` excludes the program name.
[[nodiscard]] ParsedCommand parseCommandLine(const std::vector<std::string>& arguments);

// What a written command runs.
enum class CommandForm {
    Screenshot, // one frame to a PNG file: the frame on screen
    Benchmark,  // the measurement of the thesis at that scene and camera
};

// The arguments that make thesis_renderer run `options` in `form`. Only what differs from the
// defaults is written, so the command stays short. Floats are written so that they read back as
// the same float.
[[nodiscard]] std::vector<std::string> commandArguments(const Options& options, CommandForm form);

// `program` followed by `arguments`, quoted where needed, as one line for a shell.
[[nodiscard]] std::string commandLine(const std::string& program, const std::vector<std::string>& arguments);

} // namespace thesis::app
