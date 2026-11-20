#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Mdk.Formats/Fti.h"
#include "Mdk.Formats/SpriteAnimation.h"
#include "Mdk.Formats/Texture.h"

namespace Mdk::Formats
{
    // A BNI archive (TRAVSPRT.BNI, OPTIONS.BNI...): images and sprite animations,
    // names of 12 characters (see ArchiveIndex).
    class Bni
    {
    public:
        const SharedBytes& Bytes() const { return _index->Bytes(); }
        const Dictionary<std::string, ArchiveIndex::Entry>& Entries() const { return _index->Entries; }

        static std::shared_ptr<Bni> Load(const std::string& path);

        bool Has(const std::string& name) const { return _index->Entries.ContainsKey(name); }

        // Adds or replaces an entry (the 1996 demo's loose sprite files, BetaDemo).
        void Add(const std::string& name, std::span<const byte> data);

        // A plain image entry (u16 width, u16 height, palette indices).
        std::shared_ptr<Texture> GetImage(const std::string& name);

        // A sprite animation entry (u32 size, then the animation).
        std::shared_ptr<SpriteAnimation> GetAnimation(const std::string& name);

    private:
        std::shared_ptr<ArchiveIndex> _index;
        std::unordered_map<std::string, std::shared_ptr<Texture>> _images;
        std::unordered_map<std::string, std::shared_ptr<SpriteAnimation>> _animations;

        explicit Bni(SharedBytes bytes);
    };
}
