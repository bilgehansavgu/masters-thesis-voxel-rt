// The data formats shared by C++ and the shaders (Ray, Hit, RayWork, Aabb and the two structs of
// the brick buffers) are declared in both languages. This test reads the shader compiler's own layout report for every
// compiled shader and checks each shared struct against the C++ declaration, field by field. Needs no GPU.

#include "thesis/brick2012/BrickBuffers.h"
#include "thesis/core/GpuTypes.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace thesis;

namespace {

struct Field {
    std::string name;
    std::size_t offset;
    std::size_t size;
};

// The C++ side, written with offsetof so it always describes the real C++ layout. Aabb is
// declared with two vec3 in C++ and six scalars in Slang (see types.slang), hence the mapping.
std::map<std::string, std::vector<Field>> cppLayouts()
{
    using brick2012::BrickModelHeader;
    using brick2012::BrickRecord;
    using core::Aabb;
    using core::Hit;
    using core::Ray;
    using core::RayWork;
    return {
        {"Ray",
         {{"origin", offsetof(Ray, origin), 12},
          {"tMin", offsetof(Ray, tMin), 4},
          {"direction", offsetof(Ray, direction), 12},
          {"tMax", offsetof(Ray, tMax), 4}}},
        {"Hit",
         {{"t", offsetof(Hit, t), 4},
          {"primitive", offsetof(Hit, primitive), 4},
          {"face", offsetof(Hit, face), 4},
          {"attributes", offsetof(Hit, attributes), 4}}},
        {"RayWork",
         {{"primitiveTests", offsetof(RayWork, primitiveTests), 4},
          {"cells", offsetof(RayWork, cells), 4},
          {"runs", offsetof(RayWork, runs), 4},
          {"hitCell", offsetof(RayWork, hitCell), 4}}},
        {"BrickModelHeader",
         {{"origin", offsetof(BrickModelHeader, origin), 12},
          {"voxelSize", offsetof(BrickModelHeader, voxelSize), 4},
          {"gridSize", offsetof(BrickModelHeader, gridSize), 12},
          {"brickSize", offsetof(BrickModelHeader, brickSize), 4},
          {"brickCount", offsetof(BrickModelHeader, brickCount), 4},
          {"layout", offsetof(BrickModelHeader, layout), 4},
          {"reserved0", offsetof(BrickModelHeader, reserved0), 4},
          {"reserved1", offsetof(BrickModelHeader, reserved1), 4}}},
        {"BrickRecord",
         {{"position", offsetof(BrickRecord, position), 4},
          {"cellBase", offsetof(BrickRecord, cellBase), 4},
          {"indirectionBase", offsetof(BrickRecord, indirectionBase), 4},
          {"attributeBase", offsetof(BrickRecord, attributeBase), 4}}},
        {"Aabb",
         {{"minX", offsetof(Aabb, lo) + 0, 4},
          {"minY", offsetof(Aabb, lo) + 4, 4},
          {"minZ", offsetof(Aabb, lo) + 8, 4},
          {"maxX", offsetof(Aabb, hi) + 0, 4},
          {"maxY", offsetof(Aabb, hi) + 4, 4},
          {"maxZ", offsetof(Aabb, hi) + 8, 4},
          {"pad0", offsetof(Aabb, pad0), 4},
          {"pad1", offsetof(Aabb, pad1), 4}}},
    };
}

// Collects every struct named `name` anywhere in a reflection report, with its field layout.
void findStructs(const nlohmann::json& node, const std::string& name, std::vector<std::vector<Field>>& found)
{
    if (node.is_object()) {
        if (node.value("kind", "") == "struct" && node.value("name", "") == name && node.contains("fields")) {
            std::vector<Field> fields;
            for (const nlohmann::json& field : node.at("fields")) {
                const nlohmann::json& binding = field.at("binding");
                fields.push_back({field.at("name"), binding.at("offset"), binding.at("size")});
            }
            found.push_back(std::move(fields));
        }
        for (const auto& [key, child] : node.items()) {
            findStructs(child, name, found);
        }
    } else if (node.is_array()) {
        for (const nlohmann::json& child : node) {
            findStructs(child, name, found);
        }
    }
}

} // namespace

TEST_CASE("shared structs have the same layout in C++ and in every shader", "[layout]")
{
    const std::filesystem::path shaderDirectory = THESIS_DEFAULT_SHADER_DIR;
    std::vector<std::filesystem::path> reports;
    for (const auto& entry : std::filesystem::directory_iterator(shaderDirectory)) {
        if (entry.path().extension() == ".json") {
            reports.push_back(entry.path());
        }
    }
    REQUIRE_FALSE(reports.empty());

    for (const auto& [structName, expected] : cppLayouts()) {
        std::size_t occurrences = 0;
        for (const std::filesystem::path& reportPath : reports) {
            std::ifstream file(reportPath);
            const nlohmann::json report = nlohmann::json::parse(file);
            std::vector<std::vector<Field>> found;
            findStructs(report, structName, found);

            for (const std::vector<Field>& actual : found) {
                ++occurrences;
                INFO(structName << " in " << reportPath.filename().string());
                REQUIRE(actual.size() == expected.size());
                for (std::size_t i = 0; i < expected.size(); ++i) {
                    INFO("field " << expected[i].name);
                    CHECK(actual[i].name == expected[i].name);
                    CHECK(actual[i].offset == expected[i].offset);
                    CHECK(actual[i].size == expected[i].size);
                }
            }
        }
        INFO(structName << " must appear in at least one shader, or this test checks nothing");
        CHECK(occurrences > 0);
    }
}
