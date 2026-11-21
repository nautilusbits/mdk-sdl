#include "core/FileStream.h"

#include <format>
#include <string>

#include "core/DotNet.h"

namespace Mdk
{
    std::filesystem::path FileStream::NativePath(std::string_view path)
    {
        return std::filesystem::path(std::u8string(path.begin(), path.end()));
    }

    std::ofstream FileStream::Create(std::string_view path)
    {
        std::ofstream file(NativePath(path), std::ios::binary | std::ios::trunc);
        if (!file)
        {
            throw IOException(std::format("Could not create file '{}'", path));
        }

        return file;
    }

    std::ifstream FileStream::OpenRead(std::string_view path)
    {
        std::ifstream file(NativePath(path), std::ios::binary);
        if (!file)
        {
            throw FileNotFoundException(std::format("Could not find file '{}'", path));
        }

        return file;
    }
}
