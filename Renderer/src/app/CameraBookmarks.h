#pragma once

// Side: CPU

#include "thesis/core/Camera.h"
#include "thesis/core/Scene.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace thesis::app {

// A named camera, relative to the scene: the same bookmark works on every model. yaw and pitch
// in degrees; `factor` scales the distance that shows the whole scene (as --camera does); the
// target sits at `offset` scene radii from the centre of the scene (zero for an orbit camera).
struct CameraBookmark {
    std::string name;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float factor = 1.0f;
    glm::vec3 offset{0.0f};

    // The camera of this bookmark in a scene with `bounds`.
    [[nodiscard]] core::OrbitCamera cameraIn(const core::Bounds& bounds) const;
    // A bookmark that gives `camera` back in a scene with `bounds`.
    [[nodiscard]] static CameraBookmark of(std::string name, const core::OrbitCamera& camera,
                                           const core::Bounds& bounds);
    // The --camera triple (yaw, pitch, factor) of an orbit bookmark; none if its target is moved
    // away from the centre of the scene (the command line then needs the exact camera).
    [[nodiscard]] std::optional<std::array<float, 3>> preset() const;
};

// The cameras the user keeps, in order. It starts with the cameras of the benchmark plan
// (kCameraPresets); every one of them can be renamed, moved to the current view or deleted, and
// "Restore defaults" brings them back. Kept in a small JSON file between runs.
class CameraBookmarks {
public:
    // No storage: the list lives only while the program runs (used by --self-test).
    CameraBookmarks();
    // Reads the list from `storage` if it exists. A damaged file is renamed to
    // <name>.damaged.json, so no later change writes over it, and the list starts from the
    // defaults; problem() then says so. Throws std::runtime_error if the file cannot be moved.
    explicit CameraBookmarks(std::filesystem::path storage);

    [[nodiscard]] const std::vector<CameraBookmark>& list() const { return bookmarks_; }
    // What went wrong reading the storage file; empty if nothing did.
    [[nodiscard]] const std::string& problem() const { return problem_; }
    // Adds `bookmark` at the end; a bookmark of the same name is replaced in place. Each change
    // writes the storage file; throws std::runtime_error if it cannot be written.
    void put(CameraBookmark bookmark);
    void rename(std::size_t index, std::string name);
    void remove(std::size_t index);
    void restoreDefaults();

    // A name that is not taken yet: "view 1", "view 2", ...
    [[nodiscard]] std::string freeName() const;

    // The cameras of the benchmark plan.
    [[nodiscard]] static std::vector<CameraBookmark> defaults();
    // %LOCALAPPDATA%/thesis_renderer/cameras.json; empty if LOCALAPPDATA is not set.
    [[nodiscard]] static std::filesystem::path defaultStorage();

private:
    void save() const;

    std::filesystem::path storage_;
    std::vector<CameraBookmark> bookmarks_;
    std::string problem_;
};

} // namespace thesis::app
