#include "thesis/metrics/SystemInfo.h"

#include "thesis/GitInfo.h"

#include <string>

namespace thesis::metrics {

nlohmann::json buildInfo()
{
#if defined(_MSC_FULL_VER)
    const std::string compiler = "MSVC " + std::to_string(_MSC_FULL_VER);
#elif defined(__clang__)
    const std::string compiler = std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    const std::string compiler = std::string("GCC ") + __VERSION__;
#else
    const std::string compiler = "unknown";
#endif
    return {{"git_commit", THESIS_GIT_COMMIT},
            {"git_dirty", THESIS_GIT_DIRTY},
            {"configuration", THESIS_BUILD_CONFIG},
            {"compiler", compiler}};
}

} // namespace thesis::metrics
