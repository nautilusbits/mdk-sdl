#pragma once

// System.IO.Compression.ZipFile over miniz: extract an archive into a folder, or write one.
//   ZipFile::ExtractToDirectory("a.zip", "out", ZipFile::Existing::Overwrite);

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/DotNet.h"

namespace Mdk
{
    struct ZipFile
    {
        // What an entry does to a file already there: IOException, or replace it.
        enum class Existing { Fail, Overwrite };

        // Throws IOException for a bad archive, an entry outside the folder or (Fail) an existing file.
        static void ExtractToDirectory(const std::string& source, const std::string& destination, Existing existing);

        // A new archive of (name, content) entries, deflated.
        static void Create(const std::string& path, std::span<const std::pair<std::string, std::vector<byte>>> entries);
    };
}
