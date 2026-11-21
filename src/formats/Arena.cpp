#include "formats/Arena.h"

namespace Mdk::Formats
{
    std::shared_ptr<Arena> Arena::Parse(const std::string& name, const SharedBytes& bytes, int offset)
    {
        auto arena = std::make_shared<Arena>();
        arena->Name = name;

        // The game loads the bytes after the size field; offsets are relative to that buffer.
        auto buf = offset + 4;
        auto models = buf + static_cast<int>(Bin::U32(*bytes, buf));
        auto palette = buf + static_cast<int>(Bin::U32(*bytes, buf + 4));
        auto world = buf + static_cast<int>(Bin::U32(*bytes, buf + 8));
        arena->PaletteRgb = Bin::Slice(*bytes, palette, PaletteColors * 3);
        arena->Textures = TextureArchive::Parse(*bytes, buf + 0x10);
        arena->ParseWorld(*bytes, world);
        arena->ParseModels(bytes, models + 4);
        return arena;
    }

    std::shared_ptr<Arena> Arena::ParseWorld(const std::string& name, std::span<const byte> bytes, int offset)
    {
        auto arena = std::make_shared<Arena>();
        arena->Name = name;
        arena->ParseWorld(bytes, offset);
        return arena;
    }

    std::shared_ptr<Arena> Arena::ParseBetaWorld(const std::string& name, std::span<const byte> bytes)
    {
        auto arena = std::make_shared<Arena>();
        arena->Name = name;
        arena->ParseWorld(bytes, 0, BetaMaterialNameLength, BetaBspNodeSize);
        for (auto& material : arena->Materials)
        {
            material = Str::ToUpper(material);
        }

        return arena;
    }

    void Arena::ParseModels(const SharedBytes& bytes, int baseOffset)
    {
        BinReader r(*bytes, baseOffset);
        auto animationCount = r.U32();
        auto modelCount = r.U32();
        auto soundCount = r.U32();
        for (uint i = 0; i < animationCount; i++)
        {
            auto name = r.Name(8);
            Animations[name] = ModelAnimation::Parse(name, bytes, baseOffset + static_cast<int>(r.U32()));
        }

        for (uint i = 0; i < modelCount; i++)
        {
            auto name = r.Name(8);
            Models[name] = Model::Parse(name, *bytes, baseOffset + static_cast<int>(r.U32()));
        }

        for (uint i = 0; i < soundCount; i++)
        {
            auto name = r.Name(12);
            r.Skip(4);
            auto offset = baseOffset + static_cast<int>(r.U32());
            Sounds[name] = Bin::Slice(*bytes, offset, static_cast<int>(r.U32()));
        }
    }

    void Arena::ParseWorld(std::span<const byte> bytes, int baseOffset, int nameLength, int nodeSize)
    {
        BinReader r(bytes, baseOffset);
        auto materialCount = r.U32();
        for (uint i = 0; i < materialCount; i++)
        {
            Materials.push_back(r.Name(nameLength));
        }

        // Names of 10 bytes: an odd count pads to 4.
        if (materialCount * static_cast<uint>(nameLength) % Alignment != 0)
        {
            r.Skip(2);
        }

        Nodes.resize(r.U32());
        for (size_t i = 0; i < Nodes.size(); i++)
        {
            auto next = r.Pos + nodeSize;
            auto normal = r.Vec3();
            auto d = r.F32();
            int negative = r.S16();
            int positive = r.S16();
            int frontCount = r.U16();
            int frontFirst = r.S16();
            int backCount = r.U16();
            int backFirst = r.S16();
            Nodes[i] = BspNode{normal, d, negative, positive, frontFirst, frontCount, backFirst, backCount};

            // Then two bit set offsets and two run-time object lists.
            r.Pos = next;
        }

        auto triangleCount = static_cast<int>(r.U32());
        TriangleIndices.resize(static_cast<size_t>(triangleCount) * 3);
        TriangleMaterials.resize(triangleCount);
        TriangleUvs.resize(static_cast<size_t>(triangleCount) * 3);
        TriangleFlags.resize(triangleCount);
        for (auto i = 0; i < triangleCount; i++)
        {
            TriangleIndices[i * 3] = r.S16();
            TriangleIndices[i * 3 + 1] = r.S16();
            TriangleIndices[i * 3 + 2] = r.S16();
            TriangleMaterials[i] = r.S16();
            for (auto k = 0; k < 3; k++)
            {
                TriangleUvs[i * 3 + k] = Vector2{r.F32(), r.F32()};
            }

            TriangleFlags[i] = r.U32();
        }

        Vertices.resize(r.U32());
        for (size_t i = 0; i < Vertices.size(); i++)
        {
            Vertices[i] = r.Vec3();
        }
    }
}
