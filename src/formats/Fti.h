#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/Dictionary.h"
#include "formats/BinReader.h"

namespace Mdk::Formats
{
    class ArchiveIndex;

    // MISC/MDKFONT.FTI: interface texts, fonts, palette and sounds.
    //   u32 size, u32 count, then per entry: char[8] name, u32 offset (from 4)
    // Texts (OPT0-OPT4, OM_*, KM_*) are NUL-terminated with \n escapes;
    // SYS_PAL the interface's 64 colours; FONTBIG, FONTSML fonts; SND_PUSH WAV.
    class Fti
    {
    public:
        const SharedBytes& Bytes() const;

        static std::shared_ptr<Fti> Load(const std::string& path);

        bool Has(const std::string& name) const;

        std::vector<byte> GetBytes(const std::string& name) const;

        // A text entry as bytes (the fonts' character set, \n escapes kept), or empty.
        std::vector<byte> GetTextBytes(const std::string& name) const;

        // A text entry, or `fallback`.
        std::string GetText(const std::string& name, const std::string& fallback = "") const;

    private:
        explicit Fti(SharedBytes bytes);

        std::shared_ptr<ArchiveIndex> _index;
    };

    // The directory shared by FTI and BNI: u32 size, u32 count, then per entry a
    // fixed-length name and u32 offset (from 4). Sizes run to the next offset.
    class ArchiveIndex
    {
    public:
        struct Entry
        {
            int Offset = 0;
            int Size = 0;

            bool operator==(const Entry&) const = default;
        };

        const SharedBytes& Bytes() const { return _bytes; }
        Dictionary<std::string, Entry> Entries;

        static std::shared_ptr<ArchiveIndex> Read(SharedBytes bytes, int nameLength);

        // Adds or replaces an entry, its bytes after the others.
        void Add(const std::string& name, std::span<const byte> data);

        std::vector<byte> GetBytes(const std::string& name) const;

    private:
        SharedBytes _bytes = Share({});
    };
}
