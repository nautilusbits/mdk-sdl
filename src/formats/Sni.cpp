#include "formats/Sni.h"

#include <algorithm>

#include "core/Files.h"

namespace Mdk::Formats
{
    namespace
    {
        constexpr std::string_view Riff = "RIFF";
    }

    Sni::Sni(SharedBytes bytes, int directory) : _bytes(std::move(bytes))
    {
        BinReader r(*_bytes, directory);
        auto count = r.U32();
        for (uint i = 0; i < count; i++)
        {
            auto name = directory == DirectoryOffset ? r.Name(12) : Str::ToUpper(r.Name(12));
            int flags = r.U16();
            int volume = r.U16();
            auto offset = 4 + static_cast<int>(r.U32());
            Entries.emplace_back(name, Entry{offset, static_cast<int>(r.U32()), flags, volume});
        }
    }

    std::shared_ptr<Sni> Sni::Load(const std::string& path) { return std::shared_ptr<Sni>(new Sni(Share(File::ReadAllBytes(path)))); }

    std::shared_ptr<Sni> Sni::ParseBeta(SharedBytes bytes) { return std::shared_ptr<Sni>(new Sni(std::move(bytes), BetaDirectoryOffset)); }

    bool Sni::IsSound(const Entry& entry) const
    {
        auto span = Bin::Slice(*_bytes, entry.Offset, static_cast<int>(Riff.size()));
        return std::equal(span.begin(), span.end(), Riff.begin());
    }

    std::vector<byte> Sni::GetBytes(const Entry& entry) const { return Bin::Slice(*_bytes, entry.Offset, entry.Length); }

    std::shared_ptr<SpriteAnimation> Sni::GetAnimation(const std::string& name) const
    {
        auto found = std::find_if(Entries.begin(), Entries.end(), [&](const auto& e) { return e.first == name; });
        if (found == Entries.end() || IsSound(found->second))
        {
            return nullptr;
        }

        return SpriteAnimation::Parse(name, _bytes, found->second.Offset + 4);
    }
}
