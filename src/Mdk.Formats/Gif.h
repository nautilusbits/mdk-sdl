#pragma once

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Mdk.Formats/BinReader.h"

namespace Mdk::Formats
{
    // A still GIF (MISC/MDKS_00n.GIF, the menu's slideshow): the first image's palette
    // indices and its palette (256 RGB).
    //   header ─► global palette ─► extensions (skipped) ─► image descriptor ─► LZW data
    class Gif
    {
    public:
        int Width() const { return _width; }
        int Height() const { return _height; }
        const std::vector<byte>& Indices() const { return _indices; }
        std::array<byte, 768> Palette{};

        static std::shared_ptr<Gif> Load(const std::string& path);

    private:
        static constexpr int HeaderSize = 13;
        static constexpr byte Image = 0x2C;
        static constexpr byte Extension = 0x21;
        // Packed fields: a colour table follows (2 << low 3 bits entries); interlaced rows.
        static constexpr int HasTable = 0x80;
        static constexpr int TableSize = 0x07;
        static constexpr int Interlaced = 0x40;
        static constexpr int MaxCodeSize = 12;
        static constexpr int PaletteBytes = 768;
        // Interlaced rows come in four passes: start row and step.
        static constexpr std::array<std::pair<int, int>, 4> Passes = {{{0, 8}, {4, 8}, {2, 4}, {1, 2}}};

        enum class Interlace { Off, On };

        std::vector<byte> _bytes;
        int _width;
        int _height;
        std::vector<byte> _indices;

        explicit Gif(std::vector<byte> bytes);

        bool Parse();
        int ReadPalette(int p, int fields);
        int SkipSubBlocks(int p) const;
        bool ReadImage(int p);

        // The image's rows in the order they're stored.
        static std::vector<int> RowOrder(int rows, Interlace interlace);

        // Variable-width LZW: codes grow from min + 1 bits up to 12; a clear code starts over.
        static std::vector<byte> Lzw(const std::vector<byte>& data, int minSize, int count);
    };
}
