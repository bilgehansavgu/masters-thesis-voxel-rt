#include "thesis/mesh/TriangleMesh.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace thesis::mesh {
namespace {

std::string lowerExtension(const std::filesystem::path& path)
{
    const std::u8string text = path.extension().u8string();
    std::string extension(text.begin(), text.end());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

} // namespace

const std::vector<std::string>& meshFileExtensions()
{
    static const std::vector<std::string> extensions{".ply", ".obj", ".gltf", ".glb"};
    return extensions;
}

bool isMeshFile(const std::filesystem::path& path)
{
    const std::vector<std::string>& known = meshFileExtensions();
    return std::find(known.begin(), known.end(), lowerExtension(path)) != known.end();
}

TriangleMesh loadMesh(const std::filesystem::path& path)
{
    const std::string extension = lowerExtension(path);
    if (extension == ".ply")
        return loadPly(path);
    if (extension == ".obj")
        return loadObj(path);
    if (extension == ".gltf" || extension == ".glb")
        return loadGltf(path);
    throw std::runtime_error("Mesh file '" + path.string() + "' is of an unknown kind (.ply, .obj, .gltf or .glb)");
}

} // namespace thesis::mesh
