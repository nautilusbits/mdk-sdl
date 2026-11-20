#include "Mdk.Formats/Fti.h"

#include <algorithm>
#include <utility>

#include "Mdk.Core/Files.h"

namespace Mdk::Formats
{
    Fti::Fti(SharedBytes bytes) : _index(ArchiveIndex::Read(std::move(bytes), 8)) {}

    const SharedBytes& Fti::Bytes() const { return _index->Bytes(); }

    std::shared_ptr<Fti> Fti::Load(const std::string& path) { return std::shared_ptr<Fti>(new Fti(Share(File::ReadAllBytes(path)))); }

    bool Fti::Has(const std::string& name) const { return _index->Entries.ContainsKey(name); }

    std::vector<byte> Fti::GetBytes(const std::string& name) const { return _index->GetBytes(name); }

    std::vector<byte> Fti::GetTextBytes(const std::string& name) const
    {
        auto* entry = _index->Entries.Find(name);
        if (entry == nullptr)
        {
            return {};
        }

        auto span = Bin::Slice(*Bytes(), entry->Offset, entry->Size);
        auto end = std::find(span.begin(), span.end(), byte{0});
        span.erase(end, span.end());
        return span;
    }

    std::string Fti::GetText(const std::string& name, const std::string& fallback) const
    {
        auto* entry = _index->Entries.Find(name);
        if (entry == nullptr)
        {
            return fallback;
        }

        return Str::Replace(Bin::Ascii(*Bytes(), entry->Offset, entry->Size), "\\n", "\n");
    }

    std::shared_ptr<ArchiveIndex> ArchiveIndex::Read(SharedBytes bytes, int nameLength)
    {
        auto index = std::make_shared<ArchiveIndex>();
        index->_bytes = bytes;
        BinReader r(*bytes, 4);
        auto count = r.U32();
        std::vector<std::pair<int, std::string>> offsets;
        for (uint i = 0; i < count; i++)
        {
            auto name = r.Name(nameLength);
            offsets.emplace_back(4 + static_cast<int>(r.U32()), name);
        }

        std::sort(offsets.begin(), offsets.end());
        for (size_t i = 0; i < offsets.size(); i++)
        {
            auto end = i + 1 < offsets.size() ? offsets[i + 1].first : static_cast<int>(bytes->size());
            index->Entries[offsets[i].second] = Entry{offsets[i].first, end - offsets[i].first};
        }

        return index;
    }

    void ArchiveIndex::Add(const std::string& name, std::span<const byte> data)
    {
        Entries[name] = Entry{static_cast<int>(_bytes->size()), static_cast<int>(data.size())};
        std::vector<byte> bytes(*_bytes);
        bytes.insert(bytes.end(), data.begin(), data.end());
        _bytes = Share(std::move(bytes));
    }

    std::vector<byte> ArchiveIndex::GetBytes(const std::string& name) const
    {
        auto& entry = Entries.At(name);
        return Bin::Slice(*_bytes, entry.Offset, entry.Size);
    }
}
