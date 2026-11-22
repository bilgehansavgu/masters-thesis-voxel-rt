#include "PathText.h"

namespace thesis::app {

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

std::string utf8Generic(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path pathFromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace thesis::app
