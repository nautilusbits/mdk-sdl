#pragma once

// System.IO.File / Path / Directory shapes over std::filesystem. Paths are UTF-8 strings,
// as C# strings were; `/` separators work on every platform.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Mdk
{
    namespace File
    {
        bool Exists(const std::string& path);
        std::vector<std::uint8_t> ReadAllBytes(const std::string& path);
        std::string ReadAllText(const std::string& path);
        std::vector<std::string> ReadAllLines(const std::string& path);
        void WriteAllBytes(const std::string& path, std::span<const std::uint8_t> bytes);
        void WriteAllText(const std::string& path, std::string_view text);
        void WriteAllLines(const std::string& path, const std::vector<std::string>& lines);
        void AppendAllText(const std::string& path, std::string_view text);
        void Delete(const std::string& path);
        void Copy(const std::string& from, const std::string& to, bool overwrite = false);
        void Move(const std::string& from, const std::string& to, bool overwrite = false);
        long long Length(const std::string& path);
        // Last write time, seconds since an arbitrary epoch (comparisons only).
        long long LastWriteTime(const std::string& path);
    }

    namespace Directory
    {
        bool Exists(const std::string& path);
        void CreateDirectory(const std::string& path);
        void Delete(const std::string& path, bool recursive = false);
        enum class Search { TopDirectoryOnly, AllDirectories };
        // Pattern: "*", "*.png", "name.*" (case-insensitive like Windows; sorted for stable results).
        std::vector<std::string> GetFiles(const std::string& path, std::string_view pattern = "*", Search search = Search::TopDirectoryOnly);
        std::vector<std::string> GetDirectories(const std::string& path, std::string_view pattern = "*");
        std::string GetCurrentDirectory();
    }

    namespace Path
    {
        std::string Combine(std::string_view a, std::string_view b);
        std::string Combine(std::string_view a, std::string_view b, std::string_view c);
        std::string Combine(std::string_view a, std::string_view b, std::string_view c, std::string_view d);
        std::string GetFileName(std::string_view path);
        std::string GetFileNameWithoutExtension(std::string_view path);
        // ".png" (with the dot), "" when none.
        std::string GetExtension(std::string_view path);
        std::string ChangeExtension(std::string_view path, std::string_view extension);
        // Empty when there is no parent.
        std::string GetDirectoryName(std::string_view path);
        std::string GetFullPath(std::string_view path);
        bool IsPathRooted(std::string_view path);
        std::string GetTempPath();
        // The folder holding the executable (AppContext.BaseDirectory).
        std::string BaseDirectory();
        bool MatchesPattern(std::string_view name, std::string_view pattern);
    }
}
