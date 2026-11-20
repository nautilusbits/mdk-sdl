#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Mdk.Core/Dictionary.h"
#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/BinReader.h"
#include "Mdk.Formats/Model.h"
#include "Mdk.Formats/ModelAnimation.h"
#include "Mdk.Formats/TextureArchive.h"

namespace Mdk::Formats
{
    // A BSP node: triangles lie on its plane (n·p + d), in a front list (facing the
    // positive side) and a back list; children -1 are empty leaves. See godot-mdk docs/bsp.md.
    struct BspNode
    {
        Vector3 Normal;
        float D = 0;
        int Negative = 0;
        int Positive = 0;
        int FrontFirst = 0;
        int FrontCount = 0;
        int BackFirst = 0;
        int BackCount = 0;

        bool operator==(const BspNode&) const = default;

        float Distance(Vector3 p) const { return Vector3::Dot(Normal, p) + D; }
    };

    // One arena of a level (HMO_n in LEVELnO.MTO): textures, palette colours,
    // models and world geometry, or a corridor's world alone.
    class Arena
    {
    public:
        // Colours an arena brings to the palette (indices 64-175).
        static constexpr int PaletteColors = 112;

        std::string Name;
        // The arena's palette colours (RGB); empty for corridors.
        std::vector<byte> PaletteRgb;
        std::shared_ptr<TextureArchive> Textures = std::make_shared<TextureArchive>();
        Dictionary<std::string, std::shared_ptr<Model>> Models;
        Dictionary<std::string, std::shared_ptr<ModelAnimation>> Animations;
        // RIFF WAV bytes.
        Dictionary<std::string, std::vector<byte>> Sounds;

        // Texture names, or PEN_n for flat palette colours.
        std::vector<std::string> Materials;
        // MDK coordinates (Z up).
        std::vector<Vector3> Vertices;
        // 3 vertex indices per triangle.
        std::vector<int> TriangleIndices;
        // Per triangle: index into Materials, or -n for palette colour n.
        std::vector<int> TriangleMaterials;
        // 3 UVs per triangle, in texels.
        std::vector<Vector2> TriangleUvs;
        // Per triangle: group in bits 24-31, outline bits 20-23.
        std::vector<uint> TriangleFlags;
        // Node 0 is the root; children have higher indices.
        std::vector<BspNode> Nodes;
        // The flag of triangles that only stop Kurt: not drawn, passed by the scripts' rays
        // (the 1996 demo's 2); 0 for none.
        uint ClipFlag = 0;

        int TriangleCount() const { return static_cast<int>(TriangleMaterials.size()); }

        // An arena of the MTO; `offset` points to its size field.
        static std::shared_ptr<Arena> Parse(const std::string& name, const SharedBytes& bytes, int offset);

        // A world section alone (corridors in LEVELnO.SNI).
        static std::shared_ptr<Arena> ParseWorld(const std::string& name, std::span<const byte> bytes, int offset);

        // A world section of the 1996 demo (ARENAS/name.BSP); its material names, in
        // lower case, are made upper case.
        static std::shared_ptr<Arena> ParseBetaWorld(const std::string& name, std::span<const byte> bytes);

    private:
        static constexpr int BspNodeSize = 44;
        static constexpr int MaterialNameLength = 10;
        // The 1996 demo's world section (BetaDemo): 16-character material names,
        // 36-byte BSP nodes (f32 plane[4], ten s16; the first six taken as the retail ones).
        static constexpr int BetaBspNodeSize = 36;
        static constexpr int BetaMaterialNameLength = 16;
        static constexpr int Alignment = 4;

        void ParseModels(const SharedBytes& bytes, int baseOffset);

        void ParseWorld(std::span<const byte> bytes, int baseOffset, int nameLength = MaterialNameLength, int nodeSize = BspNodeSize);
    };
}
