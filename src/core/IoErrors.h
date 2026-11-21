#pragma once

// C#'s IOException family as one test: C++'s exception types don't share IOException's base
// (FileNotFoundException, DirectoryNotFoundException are IOExceptions in .NET).
//   catch (const std::exception& e) { if (!IsIOException(e)) { throw; } ... }

#include <exception>
#include <filesystem>

#include "core/DotNet.h"

namespace Mdk
{
    // An IOException in .NET (FileNotFound, DirectoryNotFound, std::filesystem's errors).
    inline bool IsIOException(const std::exception& e)
    {
        return dynamic_cast<const IOException*>(&e) != nullptr || dynamic_cast<const FileNotFoundException*>(&e) != nullptr
            || dynamic_cast<const DirectoryNotFoundException*>(&e) != nullptr
            || dynamic_cast<const std::filesystem::filesystem_error*>(&e) != nullptr;
    }

    // An IOException or an InvalidDataException (a file that can't be read, or is broken).
    inline bool IsReadError(const std::exception& e)
    {
        return IsIOException(e) || dynamic_cast<const InvalidDataException*>(&e) != nullptr;
    }
}
