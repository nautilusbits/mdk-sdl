#include "Mdk.Core/ZipFile.h"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>

// Mdk.Core doesn't get miniz's include folder from CMake (yet): reach it from here.
#if __has_include(<miniz.h>)
#include <miniz.h>
#else
#include "../../third_party/miniz/miniz.h"
#endif

#include "Mdk.Core/FileStream.h"
#include "Mdk.Core/Files.h"

namespace Mdk
{
    namespace
    {
        namespace fs = std::filesystem;

        // The archive closed on every way out.
        struct Archive
        {
            mz_zip_archive Zip{};

            ~Archive() { mz_zip_end(&Zip); }

            std::string Error() { return mz_zip_get_error_string(mz_zip_get_last_error(&Zip)); }
        };

        void Write(const fs::path& path, const void* data, size_t size)
        {
            auto file = FileStream::Create(reinterpret_cast<const char*>(path.u8string().c_str()));
            file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            if (!file)
            {
                throw IOException(std::format("Could not write '{}'", reinterpret_cast<const char*>(path.u8string().c_str())));
            }
        }

        // Whether the entry's path stays in the destination: "../x" doesn't.
        bool IsInside(const fs::path& root, const fs::path& path)
        {
            auto relative = path.lexically_relative(root);
            return !relative.empty() && *relative.begin() != "..";
        }
    }

    void ZipFile::ExtractToDirectory(const std::string& source, const std::string& destination, Existing existing)
    {
        auto bytes = File::ReadAllBytes(source);
        Archive archive;
        if (!mz_zip_reader_init_mem(&archive.Zip, bytes.data(), bytes.size(), 0))
        {
            throw IOException(std::format("{}: {}", source, archive.Error()));
        }

        auto root = fs::absolute(FileStream::NativePath(destination)).lexically_normal();
        fs::create_directories(root);
        auto count = mz_zip_reader_get_num_files(&archive.Zip);
        for (mz_uint i = 0; i < count; i++)
        {
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&archive.Zip, i, &stat))
            {
                throw IOException(std::format("{}: {}", source, archive.Error()));
            }

            std::string_view name = stat.m_filename;
            auto path = (root / FileStream::NativePath(name)).lexically_normal();
            if (!IsInside(root, path))
            {
                throw IOException("Extracting Zip entry would have resulted in a file outside the specified destination directory.");
            }

            if (mz_zip_reader_is_file_a_directory(&archive.Zip, i))
            {
                fs::create_directories(path);
                continue;
            }

            if (existing == Existing::Fail && fs::exists(path))
            {
                throw IOException(std::format("The file '{}' already exists.", reinterpret_cast<const char*>(path.u8string().c_str())));
            }

            fs::create_directories(path.parent_path());
            size_t size = 0;
            std::unique_ptr<void, decltype(&std::free)> data(mz_zip_reader_extract_to_heap(&archive.Zip, i, &size, 0), &std::free);
            if (data == nullptr && stat.m_uncomp_size != 0)
            {
                throw IOException(std::format("{}: {}: {}", source, name, archive.Error()));
            }

            Write(path, data.get(), size);
        }
    }

    void ZipFile::Create(const std::string& path, std::span<const std::pair<std::string, std::vector<byte>>> entries)
    {
        Archive archive;
        if (!mz_zip_writer_init_heap(&archive.Zip, 0, 0))
        {
            throw IOException(std::format("{}: {}", path, archive.Error()));
        }

        for (auto& [name, content] : entries)
        {
            if (!mz_zip_writer_add_mem(&archive.Zip, name.c_str(), content.data(), content.size(), MZ_DEFAULT_COMPRESSION))
            {
                throw IOException(std::format("{}: {}", path, archive.Error()));
            }
        }

        void* buffer = nullptr;
        size_t size = 0;
        if (!mz_zip_writer_finalize_heap_archive(&archive.Zip, &buffer, &size))
        {
            throw IOException(std::format("{}: {}", path, archive.Error()));
        }

        std::unique_ptr<void, decltype(&std::free)> owned(buffer, &std::free);
        Write(FileStream::NativePath(path), buffer, size);
    }
}
