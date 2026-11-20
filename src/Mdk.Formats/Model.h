#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Mdk.Core/Numerics.h"
#include "Mdk.Formats/BinReader.h"

namespace Mdk::Formats
{
    // Axis-aligned box in MDK coordinates.
    struct Box
    {
        Vector3 Min;
        Vector3 Max;

        bool operator==(const Box&) const = default;
    };

    // A model (object, alien, weapon...) from an arena's models section or a level's CMI.
    class Model
    {
    public:
        // Whether the model starts with a flags word (absent in XGHEAD and the fall's models).
        enum class Header { Flags, NoFlags };

        // Parts layout when there is no flags word.
        enum class Parts { Single, Named };

        class Part
        {
        public:
            std::string Name;
            // Unused by the game.
            Vector3 Pivot;
            std::vector<Vector3> Vertices;
            // Same layout as Arena.
            std::vector<int> TriangleIndices;
            std::vector<int> TriangleMaterials;
            std::vector<Vector2> TriangleUvs;
            Box Bounds;
        };

        std::string Name;
        std::vector<std::string> Materials;
        std::vector<std::shared_ptr<Part>> PartList;
        std::vector<Vector3> ReferencePoints;
        Box Bounds;

        static std::shared_ptr<Model> Parse(const std::string& name, std::span<const byte> bytes, int offset, Header header = Header::Flags, Parts parts = Parts::Single);

        // Every part's vertices, in part order.
        std::vector<std::vector<Vector3>> RestPose() const;

    private:
        // The game keeps the first 10 characters of material names.
        static constexpr int MaterialNameLength = 10;

        static std::shared_ptr<Part> ParsePart(BinReader& r, bool named);

        // Stored as min x, max x, min y, max y, min z, max z.
        static Box ReadBounds(BinReader& r);
    };
}
