#include "CameraBookmarks.h"

#include "PathText.h"
#include "WindowCommand.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace thesis::app {
namespace {

// The length that offsets are measured in: the radius that framing() uses.
float unitOf(const core::Bounds& bounds)
{
    return std::max(bounds.radius(), 1.0f);
}

} // namespace

core::OrbitCamera CameraBookmark::cameraIn(const core::Bounds& bounds) const
{
    core::OrbitCamera camera = core::OrbitCamera::framing(bounds);
    camera.target += offset * unitOf(bounds);
    camera.yawDegrees = yaw;
    camera.pitchDegrees = pitch;
    camera.distance *= factor;
    return camera;
}

CameraBookmark CameraBookmark::of(std::string name, const core::OrbitCamera& camera, const core::Bounds& bounds)
{
    const core::OrbitCamera framing = core::OrbitCamera::framing(bounds);
    CameraBookmark bookmark;
    bookmark.name = std::move(name);
    bookmark.yaw = camera.yawDegrees;
    bookmark.pitch = camera.pitchDegrees;
    bookmark.factor = camera.distance / framing.distance;
    bookmark.offset = (camera.target - framing.target) / unitOf(bounds);
    return bookmark;
}

std::optional<std::array<float, 3>> CameraBookmark::preset() const
{
    if (offset != glm::vec3(0.0f))
        return std::nullopt;
    return std::array<float, 3>{yaw, pitch, factor};
}

CameraBookmarks::CameraBookmarks() : bookmarks_(defaults())
{}

CameraBookmarks::CameraBookmarks(std::filesystem::path storage) : storage_(std::move(storage)), bookmarks_(defaults())
{
    std::ifstream file(storage_);
    if (!file)
        return; // no file yet: the defaults
    try {
        std::vector<CameraBookmark> read;
        // WHY a named document: a range-for over parse(file).at(...) keeps only the reference that
        // at() returns; the parsed document is a temporary that C++20 destroys before the loop runs.
        const nlohmann::json document = nlohmann::json::parse(file);
        for (const nlohmann::json& entry : document.at("cameras")) {
            CameraBookmark bookmark;
            bookmark.name = entry.at("name").get<std::string>();
            bookmark.yaw = entry.at("yaw").get<float>();
            bookmark.pitch = entry.at("pitch").get<float>();
            bookmark.factor = entry.at("factor").get<float>();
            const nlohmann::json& offset = entry.at("offset");
            bookmark.offset = {offset.at(0).get<float>(), offset.at(1).get<float>(), offset.at(2).get<float>()};
            read.push_back(std::move(bookmark));
        }
        bookmarks_ = std::move(read);
    } catch (const nlohmann::json::exception& error) {
        // The next change would write the defaults over the file, so the file moves aside first:
        // the user's cameras stay on disk, and the panel says where.
        file.close();
        bookmarks_ = defaults();
        std::filesystem::path aside = storage_;
        aside.replace_extension(".damaged.json");
        std::error_code moveError;
        std::filesystem::rename(storage_, aside, moveError);
        if (moveError) {
            throw std::runtime_error("Cannot read the camera bookmarks '" + utf8(storage_) + "' (" + error.what() +
                                     "), and cannot move the file aside: " + moveError.message());
        }
        problem_ = "Camera bookmarks could not be read (" + std::string(error.what()) + "); the file is kept as '" +
                   utf8(aside) + "', the default cameras are shown.";
    }
}

void CameraBookmarks::put(CameraBookmark bookmark)
{
    const auto same = std::find_if(bookmarks_.begin(), bookmarks_.end(),
                                   [&](const CameraBookmark& known) { return known.name == bookmark.name; });
    if (same != bookmarks_.end())
        *same = std::move(bookmark);
    else
        bookmarks_.push_back(std::move(bookmark));
    save();
}

void CameraBookmarks::rename(std::size_t index, std::string name)
{
    // Names are keys: put() replaces by name, the panel marks the bookmark by name.
    for (std::size_t i = 0; i < bookmarks_.size(); ++i) {
        if (i != index && bookmarks_[i].name == name)
            throw std::invalid_argument("A kept camera is already called \"" + name + "\"");
    }
    bookmarks_.at(index).name = std::move(name);
    save();
}

void CameraBookmarks::remove(std::size_t index)
{
    if (index >= bookmarks_.size())
        throw std::out_of_range("CameraBookmarks::remove: no bookmark " + std::to_string(index));
    bookmarks_.erase(bookmarks_.begin() + static_cast<std::ptrdiff_t>(index));
    save();
}

void CameraBookmarks::restoreDefaults()
{
    bookmarks_ = defaults();
    save();
}

std::string CameraBookmarks::freeName() const
{
    for (std::size_t number = 1;; ++number) {
        std::string name = "view " + std::to_string(number);
        if (std::none_of(bookmarks_.begin(), bookmarks_.end(),
                         [&](const CameraBookmark& known) { return known.name == name; }))
            return name;
    }
}

std::vector<CameraBookmark> CameraBookmarks::defaults()
{
    std::vector<CameraBookmark> result;
    for (const CameraPreset& preset : kCameraPresets)
        result.push_back({preset.name, preset.camera[0], preset.camera[1], preset.camera[2], glm::vec3(0.0f)});
    return result;
}

void CameraBookmarks::save() const
{
    if (storage_.empty())
        return;
    nlohmann::json cameras = nlohmann::json::array();
    for (const CameraBookmark& bookmark : bookmarks_) {
        cameras.push_back({{"name", bookmark.name},
                           {"yaw", bookmark.yaw},
                           {"pitch", bookmark.pitch},
                           {"factor", bookmark.factor},
                           {"offset", {bookmark.offset.x, bookmark.offset.y, bookmark.offset.z}}});
    }
    std::error_code error;
    std::filesystem::create_directories(storage_.parent_path(), error);
    std::ofstream out(storage_);
    out << nlohmann::json{{"cameras", cameras}}.dump(2);
    if (!out)
        throw std::runtime_error("Cannot write the camera bookmarks '" + utf8(storage_) + "'");
}

std::filesystem::path CameraBookmarks::defaultStorage()
{
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData == nullptr || *localAppData == '\0')
        return {};
    return std::filesystem::path(localAppData) / "thesis_renderer" / "cameras.json";
}

} // namespace thesis::app
