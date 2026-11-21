#include "ShellFiles.h"

#include <windows.h>

#include <shellapi.h>

#include <format>
#include <stdexcept>
#include <string>
#include <system_error>

namespace thesis::app {
namespace {

// The shell wants a full path with backslashes.
std::wstring shellPath(const std::filesystem::path& file)
{
    std::error_code error;
    std::filesystem::path full = std::filesystem::absolute(file, error).lexically_normal();
    if (error)
        throw std::runtime_error("Not a valid path: '" + file.string() + "'");
    return full.make_preferred().wstring();
}

} // namespace

void revealFile(const std::filesystem::path& file)
{
    const std::wstring arguments = L"/select,\"" + shellPath(file) + L"\"";
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL));
    if (result <= 32) // ShellExecute returns a value above 32 when it worked
        throw std::runtime_error(std::format("Cannot open Explorer at '{}' (error {})", file.string(), result));
}

bool recycleFile(const std::filesystem::path& file)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error))
        throw std::runtime_error("Cannot move '" + file.string() + "' to the Recycle Bin: no such file");
    // SHFileOperation takes a list of paths ended by an empty one: two zero characters at the end.
    std::wstring from = shellPath(file);
    from.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    // ALLOWUNDO: to the Recycle Bin. WANTNUKEWARNING: ask before a delete that cannot be undone,
    // even with NOCONFIRMATION, which skips only the usual "are you sure" (the panel asked that).
    operation.fFlags = FOF_ALLOWUNDO | FOF_WANTNUKEWARNING | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int result = SHFileOperationW(&operation);
    if (result != 0)
        throw std::runtime_error(std::format(
            "Cannot move '{}' to the Recycle Bin (is it open in another program?), error {}", file.string(), result));
    return !operation.fAnyOperationsAborted;
}

} // namespace thesis::app
