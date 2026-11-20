#include "Mdk.Game/Mods/ZipFiles.h"

#include <algorithm>
#include <format>
#include <fstream>

#include <miniz.h>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/FileStream.h"
#include "Mdk.Core/Files.h"
#include "Mdk.Core/IoErrors.h"
#include "Mdk.Game/Mods/ModInfo.h"

namespace Mdk::Game::Mods
{
    // The open zip: miniz's reader over the file (System.IO.Compression's ZipArchive).
    struct ZipFiles::Archive
    {
        mz_zip_archive Zip{};
        std::ifstream Stream;

        ~Archive() { mz_zip_end(&Zip); }

        std::string Error() { return mz_zip_get_error_string(mz_zip_get_last_error(&Zip)); }

        // The entry's directory record (ZipArchiveEntry), or nothing when miniz can't read it.
        std::optional<ZipEntry> EntryAt(mz_uint index)
        {
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&Zip, index, &stat))
            {
                return std::nullopt;
            }

            return ZipEntry{stat.m_filename, static_cast<long long>(stat.m_uncomp_size), static_cast<long long>(stat.m_comp_size)};
        }

        // Every entry's directory record (archive.Entries).
        std::vector<ZipEntry> Entries()
        {
            std::vector<ZipEntry> entries;
            const auto count = mz_zip_reader_get_num_files(&Zip);
            for (mz_uint i = 0; i < count; i++)
            {
                auto entry = EntryAt(i);
                if (!entry)
                {
                    throw InvalidDataException(Error());
                }

                entries.push_back(std::move(*entry));
            }

            return entries;
        }

        // The file's bytes at an offset, for miniz.
        static size_t ReadAt(void* opaque, mz_uint64 offset, void* buffer, size_t n)
        {
            auto& stream = static_cast<Archive*>(opaque)->Stream;
            stream.clear();
            stream.seekg(static_cast<std::streamoff>(offset));
            stream.read(static_cast<char*>(buffer), static_cast<std::streamsize>(n));
            return static_cast<size_t>(stream.gcount());
        }
    };

    namespace
    {
        constexpr byte Delete = 0x7F;
        // UTF-8 lead byte of U+0080..U+00BF (C1 controls are U+0080..U+009F).
        constexpr byte Latin1Lead = 0xC2;
        constexpr byte C1First = 0x80;
        constexpr byte C1Last = 0x9F;

        // name.Any(char.IsControl) on the UTF-8 name: C0, DEL and C1 controls.
        bool HasControl(const std::string& name)
        {
            for (size_t i = 0; i < name.size(); i++)
            {
                const auto c = static_cast<byte>(name[i]);
                if (c < ' ' || c == Delete)
                {
                    return true;
                }

                if (c == Latin1Lead && i + 1 < name.size())
                {
                    const auto next = static_cast<byte>(name[i + 1]);
                    if (next >= C1First && next <= C1Last)
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        // names.Contains(name, StringComparer.OrdinalIgnoreCase).
        bool ContainsIgnoreCase(const std::vector<std::string>& names, const std::string& name)
        {
            return std::any_of(names.begin(), names.end(), [&](const std::string& n) { return Str::EqualsIgnoreCase(n, name); });
        }
    }

    ZipFiles::ZipFiles(const std::string& path, EntryNames entries)
        : ModFiles(Mdk::Path::GetFileNameWithoutExtension(path)), _path(path), _entries(std::move(entries))
    {
    }

    ZipFiles::~ZipFiles() = default;

    std::shared_ptr<ZipFiles> ZipFiles::Open(const std::string& path)
    {
        try
        {
            const auto archive = OpenRead(path);
            auto entries = Index(path, archive->Entries());
            return entries ? std::shared_ptr<ZipFiles>(new ZipFiles(path, std::move(*entries))) : nullptr;
        }
        catch (const std::exception& e)
        {
            if (!IsReadError(e))
            {
                throw;
            }

            Console::Error().WriteLine(std::format("Mod {}: {}", Mdk::Path::GetFileName(path), e.what()));
            return nullptr;
        }
    }

    std::optional<ZipFiles::EntryNames> ZipFiles::Index(const std::string& path, const std::vector<ZipEntry>& archive)
    {
        const auto zip = Mdk::Path::GetFileName(path);
        if (std::ssize(archive) > MaxEntries)
        {
            Console::Error().WriteLine(std::format("Mod {}: over {} entries, ignored", zip, MaxEntries));
            return std::nullopt;
        }

        // Directories aside; entries leaving the mod ignored.
        std::vector<ZipEntry> files;
        for (const auto& e : archive)
        {
            if (!Str::EndsWith(e.FullName, std::string_view(&Separator, 1)))
            {
                files.push_back(e);
            }
        }

        std::vector<ZipEntry> safe;
        for (const auto& e : files)
        {
            if (Safe(e.FullName))
            {
                safe.push_back(e);
            }
        }

        if (safe.size() < files.size())
        {
            Console::Error().WriteLine(std::format("Mod {}: {} entries outside the mod ignored", zip, files.size() - safe.size()));
        }

        const auto top = Top(safe);
        EntryNames entries;
        long long total = 0;
        for (const auto& entry : safe)
        {
            if (!Str::StartsWithIgnoreCase(entry.FullName, top))
            {
                continue;
            }

            if (const auto why = Refusal(entry))
            {
                Console::Error().WriteLine(std::format("Mod {}: {} {}, ignored", zip, entry.FullName, *why));
                continue;
            }

            total += entry.Length;
            auto name = entry.FullName.substr(top.size());
            if (entries.Names.try_emplace(name, entry.FullName).second)
            {
                entries.Order.push_back(std::move(name));
            }
        }

        if (total > MaxTotal)
        {
            Console::Error().WriteLine(std::format("Mod {}: {} MB unpacked, over {} MB, ignored", zip, total >> 20, MaxTotal >> 20));
            return std::nullopt;
        }

        return entries;
    }

    std::string ZipFiles::Top(const std::vector<ZipEntry>& entries)
    {
        std::vector<std::string> names;
        for (const auto& e : entries)
        {
            names.push_back(e.FullName);
        }

        if (ContainsIgnoreCase(names, ModInfo::FileName))
        {
            return "";
        }

        std::vector<std::string> tops;
        for (const auto& n : names)
        {
            const auto at = Str::IndexOf(n, Separator);
            auto top = at > 0 ? n.substr(0, static_cast<size_t>(at) + 1) : "";
            if (!ContainsIgnoreCase(tops, top))
            {
                tops.push_back(std::move(top));
            }
        }

        if (tops.size() != 1 || tops[0].empty())
        {
            return "";
        }

        return ContainsIgnoreCase(names, tops[0] + ModInfo::FileName) ? tops[0] : "";
    }

    bool ZipFiles::Safe(const std::string& name)
    {
        if (name.empty() || name[0] == Separator || Str::Contains(name, "\\") || Str::Contains(name, ":") || HasControl(name))
        {
            return false;
        }

        const auto parts = Str::Split(name, Separator);
        return std::all_of(parts.begin(), parts.end(), [](const std::string& part) { return part != "" && part != "." && part != ".."; });
    }

    std::optional<std::string> ZipFiles::Refusal(const ZipEntry& entry)
    {
        if (entry.Length > MaxEntry)
        {
            return std::format("is {} MB unpacked, over {} MB", entry.Length >> 20, MaxEntry >> 20);
        }

        if (entry.Length > RatioFrom && entry.Length > entry.CompressedLength * MaxRatio)
        {
            return std::format("unpacks over {} times its size", MaxRatio);
        }

        return std::nullopt;
    }

    bool ZipFiles::Has(const std::string& path)
    {
        return _entries.Names.contains(path);
    }

    std::vector<std::string> ZipFiles::List(const std::string& folder)
    {
        const auto prefix = folder + Separator;
        std::vector<std::string> paths;
        for (const auto& k : _entries.Order)
        {
            if (Str::StartsWithIgnoreCase(k, prefix))
            {
                paths.push_back(k);
            }
        }

        return paths;
    }

    std::vector<byte> ZipFiles::Read(const std::string& path)
    {
        const auto found = _entries.Names.find(path);
        if (found == _entries.Names.end())
        {
            throw FileNotFoundException(std::format("no {}", path));
        }

        const auto& name = found->second;
        std::lock_guard guard(_lock);
        if (_archive == nullptr)
        {
            _archive = Reopen();
        }

        const auto index = mz_zip_reader_locate_file(&_archive->Zip, name.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
        const auto entry = index >= 0 ? _archive->EntryAt(static_cast<mz_uint>(index)) : std::nullopt;
        if (!entry)
        {
            throw FileNotFoundException(std::format("no {}", path));
        }

        if (const auto why = Refusal(*entry))
        {
            throw InvalidDataException(*why);
        }

        // miniz inflates into exactly the declared size and fails on more (or a wrong CRC).
        std::vector<byte> bytes(static_cast<size_t>(entry->Length));
        if (!mz_zip_reader_extract_to_mem(&_archive->Zip, static_cast<mz_uint>(index), bytes.data(), bytes.size(), 0))
        {
            throw InvalidDataException(std::format("unpacks larger than it declares, or broken: {}", _archive->Error()));
        }

        return bytes;
    }

    std::unique_ptr<ZipFiles::Archive> ZipFiles::Reopen()
    {
        return OpenRead(_path);
    }

    std::unique_ptr<ZipFiles::Archive> ZipFiles::OpenRead(const std::string& path)
    {
        auto archive = std::make_unique<Archive>();
        archive->Stream = FileStream::OpenRead(path);
        if (!archive->Stream)
        {
            throw IOException(std::format("Could not open '{}'", path));
        }

        archive->Stream.seekg(0, std::ios::end);
        const auto size = static_cast<mz_uint64>(archive->Stream.tellg());
        archive->Zip.m_pRead = Archive::ReadAt;
        archive->Zip.m_pIO_opaque = archive.get();
        if (!mz_zip_reader_init(&archive->Zip, size, 0))
        {
            throw InvalidDataException(archive->Error());
        }

        return archive;
    }

    void ZipFiles::Close()
    {
        std::lock_guard guard(_lock);
        _archive.reset();
    }
}
