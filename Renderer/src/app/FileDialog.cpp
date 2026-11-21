#include "FileDialog.h"

#include <nfd.h>

#include <stdexcept>
#include <system_error>

namespace thesis::app {
namespace {

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

// NFD_Init and NFD_Quit around each dialog: they set up COM on Windows for this thread.
class NfdSession {
public:
    NfdSession()
    {
        if (NFD_Init() != NFD_OKAY)
            throw std::runtime_error(std::string("Cannot open the file dialog: ") + NFD_GetError());
    }
    ~NfdSession() { NFD_Quit(); }
    NfdSession(const NfdSession&) = delete;
    NfdSession& operator=(const NfdSession&) = delete;
};

} // namespace

std::optional<std::filesystem::path> pickFile(const std::vector<FileFilter>& filters,
                                              const std::filesystem::path& startDirectory)
{
    const NfdSession session;
    // nfd wants "ply,obj" per filter; the strings must live until the dialog returns.
    std::vector<std::string> specs;
    specs.reserve(filters.size());
    for (const FileFilter& filter : filters) {
        std::string spec;
        for (const std::string& extension : filter.extensions)
            spec += (spec.empty() ? "" : ",") + extension;
        specs.push_back(std::move(spec));
    }
    std::vector<nfdu8filteritem_t> items;
    for (std::size_t i = 0; i < filters.size(); ++i)
        items.push_back({filters[i].name.c_str(), specs[i].c_str()});

    // The Windows shell parses only a full path with backslashes and without "..": the model
    // folder comes from CMake as ".../Renderer/../Models". A folder that does not exist (any
    // more) is no error: the dialog then opens where it opened last.
    std::error_code error;
    std::filesystem::path folder;
    if (!startDirectory.empty()) {
        folder = std::filesystem::weakly_canonical(std::filesystem::absolute(startDirectory, error), error);
        if (error || !std::filesystem::is_directory(folder, error))
            folder.clear();
    }
    const std::string start = utf8(folder.make_preferred());
    nfdu8char_t* chosen = nullptr;
    const nfdresult_t result = NFD_OpenDialogU8(&chosen, items.data(), static_cast<nfdfiltersize_t>(items.size()),
                                                start.empty() ? nullptr : start.c_str());
    if (result == NFD_CANCEL)
        return std::nullopt;
    if (result != NFD_OKAY)
        throw std::runtime_error(std::string("File dialog: ") + NFD_GetError());
    const std::string text(chosen);
    NFD_FreePathU8(chosen);
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace thesis::app
