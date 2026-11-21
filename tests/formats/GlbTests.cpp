#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "formats/Glb.h"
#include "formats/Png.h"

using namespace Mdk;
using namespace Mdk::Formats;

// glTF binary (.glb): the subset mods use (named mesh nodes, positions, UVs, indices, a
// base colour or an embedded PNG per material) reads back what Glb::Write wrote, and a
// file written by hand (another exporter's layout: u16 indices, node transforms, a hierarchy) reads.

namespace
{
    template <typename T>
    void Append(std::vector<byte>& bytes, T value)
    {
        byte raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        bytes.insert(bytes.end(), raw, raw + sizeof(T));
    }

    void AppendText(std::vector<byte>& bytes, const std::string& text) { bytes.insert(bytes.end(), text.begin(), text.end()); }

    Glb::Scene Triangle()
    {
        auto png = Png::Encode(1, 1, std::vector<byte>{10, 20, 30, 255}, Png::Channels::Rgba);
        Glb::Primitive primitive{{{0, 0, 0}, {1, 0, 0}, {0, 1, 2}}, {{0, 0}, {1, 0}, {0, 1}}, {0, 1, 2}, 0};
        Glb::Primitive flat{{{5, 5, 5}, {6, 5, 5}, {5, 6, 5}}, {{0, 0}, {0, 0}, {0, 0}}, {0, 2, 1}, 1};
        return Glb::Scene{
            {Glb::Mesh{"HEAD", {primitive, flat}}},
            {Glb::Material{"WALL", Vector4::One(), png}, Glb::Material{"PEN_12", Vector4(0.5f, 0.25f, 1.0f, 1.0f), std::nullopt}}};
    }

    // A GLB of a JSON chunk and a BIN chunk.
    std::vector<byte> Pack(const std::string& json, const std::vector<byte>& bin)
    {
        std::vector<byte> text(json.begin(), json.end());
        while (text.size() % 4 != 0)
        {
            text.push_back(' ');
        }

        std::vector<byte> file;
        AppendText(file, "glTF");
        Append(file, 2u);
        Append(file, static_cast<uint>(12 + 8 + text.size() + (bin.empty() ? 0 : 8 + bin.size())));
        Append(file, static_cast<uint>(text.size()));
        AppendText(file, "JSON");
        file.insert(file.end(), text.begin(), text.end());
        if (!bin.empty())
        {
            Append(file, static_cast<uint>(bin.size()));
            file.insert(file.end(), {'B', 'I', 'N', 0});
            file.insert(file.end(), bin.begin(), bin.end());
        }

        uint length;
        std::memcpy(&length, file.data() + 8, sizeof(uint));
        CHECK(file.size() == length);
        return file;
    }
}

TEST_CASE("GlbTests.RoundTrips")
{
    auto scene = Triangle();

    auto read = Glb::Read(Glb::Write(scene));

    REQUIRE(read.Meshes.size() == 1);
    auto& mesh = read.Meshes[0];
    CHECK("HEAD" == mesh.Name);
    REQUIRE(2 == mesh.Primitives.size());
    for (auto i = 0; i < 2; i++)
    {
        CHECK(scene.Meshes[0].Primitives[i].Positions == mesh.Primitives[i].Positions);
        CHECK(scene.Meshes[0].Primitives[i].Uvs == mesh.Primitives[i].Uvs);
        CHECK(scene.Meshes[0].Primitives[i].Indices == mesh.Primitives[i].Indices);
        CHECK(scene.Meshes[0].Primitives[i].Material == mesh.Primitives[i].Material);
    }

    CHECK("WALL" == read.Materials[0].Name);
    CHECK(scene.Materials[0].Png == read.Materials[0].Png);
    CHECK("PEN_12" == read.Materials[1].Name);
    CHECK(!read.Materials[1].Png.has_value());
    CHECK(Vector4(0.5f, 0.25f, 1.0f, 1.0f) == read.Materials[1].Colour);
}

// A root node scaled 2 with a child moved 10 along x: the child's mesh comes out in the
// scene's space; u16 indices; no UVs (zero); no material (-1, white).
TEST_CASE("GlbTests.ReadsAnotherExportersFile")
{
    std::vector<byte> bin;
    for (auto v : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})
    {
        Append(bin, v);
    }

    for (ushort i : {0, 1, 2})
    {
        Append(bin, i);
    }

    bin.insert(bin.end(), {0, 0});
    const std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
 "nodes":[{"name":"root","scale":[2,2,2],"children":[1]},{"name":"ARM","translation":[10,0,0],"mesh":0}],
 "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
 "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
              {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
 "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
 "buffers":[{"byteLength":44}]})";

    auto scene = Glb::Read(Pack(json, bin));

    REQUIRE(scene.Meshes.size() == 1);
    auto& mesh = scene.Meshes[0];
    CHECK("ARM" == mesh.Name);
    REQUIRE(mesh.Primitives.size() == 1);
    auto& primitive = mesh.Primitives[0];
    CHECK(std::vector<Vector3>{{20, 0, 0}, {22, 0, 0}, {20, 2, 0}} == primitive.Positions);
    CHECK(std::vector<int>{0, 1, 2} == primitive.Indices);
    for (auto& uv : primitive.Uvs)
    {
        CHECK(Vector2::Zero() == uv);
    }

    CHECK(-1 == primitive.Material);
}

TEST_CASE("GlbTests.RefusesOtherFiles")
{
    std::string text = "not a glb file at all";
    CHECK_THROWS_AS(Glb::Read(std::vector<byte>(text.begin(), text.end())), InvalidDataException);
    CHECK_THROWS_AS(Glb::Read(Pack(R"({"asset":{"version":"1.0"}})", {})), InvalidDataException);
}

// Crafted files (a mod from the internet) are refused, never a crash: a chunk's
// length near the end of the int, offsets that overflow, indices past the arrays.
TEST_CASE("GlbTests.RefusesCraftedFiles")
{
    auto chunk = Pack(R"({"asset":{"version":"2.0"}})", {});
    uint huge = 0x7FFFFFFF - 8;
    std::memcpy(chunk.data() + 12, &huge, sizeof(uint));
    CHECK_THROWS_AS(Glb::Read(chunk), InvalidDataException);

    const std::string views = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
 "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
 "accessors":[{"bufferView":0,"byteOffset":2147483000,"componentType":5126,"count":3,"type":"VEC3"}],
 "bufferViews":[{"buffer":0,"byteOffset":2147483000,"byteLength":2000}]})";
    CHECK_THROWS_AS(Glb::Read(Pack(views, std::vector<byte>(36))), InvalidDataException);

    const std::string missing = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":5}]})";
    CHECK_THROWS_AS(Glb::Read(Pack(missing, {})), InvalidDataException);

    const std::string big = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":1e20}]})";
    CHECK_THROWS_AS(Glb::Read(Pack(big, {})), InvalidDataException);
}
