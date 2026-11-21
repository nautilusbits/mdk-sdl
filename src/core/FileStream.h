#pragma once

// System.IO.File.Create / File.OpenRead as binary fstreams over UTF-8 paths; they throw
// IOException (FileNotFoundException for a missing file to read) as .NET does.
//   auto file = FileStream::Create(path);  file.write(...);

#include <filesystem>
#include <fstream>
#include <string_view>

namespace Mdk
{
    namespace FileStream
    {
        // A UTF-8 path as the OS wants it (UTF-16 on Windows).
        std::filesystem::path NativePath(std::string_view path);

        // A new or truncated file, for writing.
        std::ofstream Create(std::string_view path);

        std::ifstream OpenRead(std::string_view path);
    }
}
