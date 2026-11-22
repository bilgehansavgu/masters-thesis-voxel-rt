#include "FileDialog.h"

#include "PathText.h"

#include <nfd.h>

#include <stdexcept>
#include <system_error>

namespace thesis::app {
namespace {

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

// nfd wants "ply,obj" per filter; the strings must live until the dialog returns.
struct FilterItems {
    std::vector<std::string> specs;
    std::vector<nfdu8filteritem_t> items;

    explicit FilterItems(const std::vector<FileFilter>& filters)
    {
        specs.reserve(filters.size());
        for (const FileFilter& filter : filters) {
            std::string spec;
            for (const std::string& extension : filter.extensions)
                spec += (spec.empty() ? "" : ",") + extension;
            specs.push_back(std::move(spec));
        }
        for (std::size_t i = 0; i < filters.size(); ++i)
            items.push_back({filters[i].name.c_str(), specs[i].c_str()});
    }
};

// The Windows shell parses only a full path with backslashes and without "..": the model folder
// comes from CMake as ".../Renderer/../Models". A folder that does not exist (any more) is no
// error: the dialog then opens where it opened last.
std::string dialogFolder(const std::filesystem::path& directory)
{
    std::error_code error;
    std::filesystem::path folder;
    if (!directory.empty()) {
        folder = std::filesystem::weakly_canonical(std::filesystem::absolute(directory, error), error);
        if (error || !std::filesystem::is_directory(folder, error))
            folder.clear();
    }
    return utf8(folder.make_preferred());
}

std::optional<std::filesystem::path> chosenPath(nfdresult_t result, nfdu8char_t* chosen)
{
    if (result == NFD_CANCEL)
        return std::nullopt;
    if (result != NFD_OKAY)
        throw std::runtime_error(std::string("File dialog: ") + NFD_GetError());
    const std::string text(chosen);
    NFD_FreePathU8(chosen);
    return pathFromUtf8(text);
}

} // namespace

std::optional<std::filesystem::path> pickFile(const std::vector<FileFilter>& filters,
                                              const std::filesystem::path& startDirectory)
{
    const NfdSession session;
    FilterItems filterItems(filters);
    const std::string start = dialogFolder(startDirectory);
    nfdu8char_t* chosen = nullptr;
    const nfdresult_t result =
        NFD_OpenDialogU8(&chosen, filterItems.items.data(), static_cast<nfdfiltersize_t>(filterItems.items.size()),
                         start.empty() ? nullptr : start.c_str());
    return chosenPath(result, chosen);
}

std::optional<std::filesystem::path> pickSaveFile(const std::vector<FileFilter>& filters,
                                                  const std::filesystem::path& suggested)
{
    const NfdSession session;
    FilterItems filterItems(filters);
    const std::string start = dialogFolder(suggested.parent_path());
    const std::string name = utf8(suggested.filename());
    nfdu8char_t* chosen = nullptr;
    const nfdresult_t result =
        NFD_SaveDialogU8(&chosen, filterItems.items.data(), static_cast<nfdfiltersize_t>(filterItems.items.size()),
                         start.empty() ? nullptr : start.c_str(), name.empty() ? nullptr : name.c_str());
    return chosenPath(result, chosen);
}

} // namespace thesis::app
