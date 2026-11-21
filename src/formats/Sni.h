#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "formats/BinReader.h"
#include "formats/SpriteAnimation.h"

namespace Mdk::Formats
{
    // An SNI archive (TRAVERSE.SNI, LEVELnS.SNI, LEVELnO.SNI).
    //   header, u32 count, then per entry:
    //     char[12] name, u16 flags, u16 volume, u32 offset (from 4), u32 length
    // Flag 1 loops, flag 2 marks music. Most entries are RIFF WAV; in LEVELnO.SNI the corridors'
    // (CHMO_n...) world geometry.
    class Sni
    {
    public:
        static constexpr int FlagLoop = 1;

        struct Entry
        {
            int Offset = 0;
            int Length = 0;
            int Flags = 0;
            int Volume = 0;

            bool operator==(const Entry&) const = default;
        };

        const SharedBytes& Bytes() const { return _bytes; }
        // In file order.
        std::vector<std::pair<std::string, Entry>> Entries;

        static std::shared_ptr<Sni> Load(const std::string& path);

        // An archive of the 1996 demo (BetaDemo).
        static std::shared_ptr<Sni> ParseBeta(SharedBytes bytes);

        bool IsSound(const Entry& entry) const;

        std::vector<byte> GetBytes(const Entry& entry) const;

        // A sprite animation stored in the archive (LEVEL4S.SNI's K_SURF), or null.
        std::shared_ptr<SpriteAnimation> GetAnimation(const std::string& name) const;

    private:
        static constexpr int DirectoryOffset = 0x14;
        // The 1996 demo's archives: u32 size, then the count; names in lower case.
        static constexpr int BetaDirectoryOffset = 4;

        SharedBytes _bytes;

        explicit Sni(SharedBytes bytes, int directory = DirectoryOffset);
    };
}
