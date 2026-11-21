#pragma once

#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/DotNet.h"
#include "core/Numerics.h"

namespace Mdk::Formats
{
    // glTF 2.0 binary files (.glb), the subset mods use: named nodes with triangle meshes
    // (positions, first UVs, indices), materials of a base colour and an embedded PNG. Read: node
    // transforms (matrix or TRS, parents' included) are applied, normals and the rest ignored; written:
    // one node per mesh, no transform. Coordinates stay glTF's (Y up); callers convert.
    //   "glTF" u32 version 2, u32 length │ u32 length "JSON" text │ u32 length "BIN\0" data   (little-endian, 4-aligned)
    //   node ─► mesh ─► primitives ─► accessors (POSITION, TEXCOORD_0, indices) ─► bufferViews ─► BIN
    //                              └► material ─► pbrMetallicRoughness: baseColorFactor, baseColorTexture ─► image (PNG in a bufferView)
    struct Glb
    {
        // A material: its name, base colour (linear RGBA as written) and PNG, if any.
        struct Material
        {
            std::string Name;
            Vector4 Colour;
            std::optional<std::vector<byte>> Png;

            bool operator==(const Material&) const = default;
        };

        // Triangles of one material (-1: none).
        struct Primitive
        {
            std::vector<Vector3> Positions;
            std::vector<Vector2> Uvs;
            std::vector<int> Indices;
            int Material = -1;

            bool operator==(const Primitive&) const = default;
        };

        // A node's mesh, named after the node (else the mesh).
        struct Mesh
        {
            std::string Name;
            std::vector<Primitive> Primitives;

            bool operator==(const Mesh&) const = default;
        };

        struct Scene
        {
            std::vector<Mesh> Meshes;
            std::vector<Glb::Material> Materials;

            bool operator==(const Scene&) const = default;
        };

        // A file's meshes; InvalidDataException for any broken one (a mod's
        // file is untrusted: indices past the arrays, numbers out of range).
        static Scene Read(std::span<const byte> bytes);

        // A scene as a GLB: per mesh a node of its name; positions, UVs and u32 indices per
        // primitive; each material's PNG embedded; both faces shown.
        static std::vector<byte> Write(const Scene& scene);

    private:
        static constexpr uint Magic = 0x46546C67;
        static constexpr uint Version = 2;
        static constexpr uint JsonChunk = 0x4E4F534A;
        static constexpr uint BinChunk = 0x004E4942;
        static constexpr int HeaderSize = 12;
        static constexpr int ChunkHeader = 8;
        static constexpr int Alignment = 4;

        // Accessor component types.
        static constexpr int UnsignedByte = 5121;
        static constexpr int UnsignedShort = 5123;
        static constexpr int UnsignedInt = 5125;
        static constexpr int Float = 5126;

        static constexpr int Triangles = 4;
        static constexpr int ArrayBuffer = 34962;
        static constexpr int ElementArrayBuffer = 34963;
        static constexpr const char* PngType = "image/png";

        // A file's accessors and nodes over its BIN chunk.
        class Reader;

        static Scene Parse(std::span<const byte> bytes);

        // The JSON chunk and the BIN chunk (empty when absent).
        static std::pair<std::vector<byte>, std::vector<byte>> Chunks(std::span<const byte> bytes);

        // The header and both chunks, the JSON padded with spaces, the BIN with zeros.
        static std::vector<byte> Pack(const std::vector<byte>& json, const std::vector<byte>& bin);

        static int Aligned(int length);
    };
}
