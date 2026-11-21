#include "Files.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "DotNet.h"

#if defined(_WIN32)
#include <windows.h>
// windows.h macros would rename Directory::CreateDirectory etc.
#undef CreateDirectory
#undef GetCurrentDirectory
#undef GetTempPath
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Mdk
{
    namespace
    {
        fs::path P(std::string_view s) { return fs::path(std::u8string(s.begin(), s.end())); }

        std::string S(const fs::path& p)
        {
            auto u = p.generic_u8string();
            return std::string(u.begin(), u.end());
        }
    }

    // ---- File ----

    bool File::Exists(const std::string& path)
    {
        std::error_code ec;
        return fs::is_regular_file(P(path), ec);
    }

    std::vector<std::uint8_t> File::ReadAllBytes(const std::string& path)
    {
        std::ifstream in(P(path), std::ios::binary);
        if (!in)
        {
            throw FileNotFoundException("Could not find file '" + path + "'.");
        }

        in.seekg(0, std::ios::end);
        auto size = static_cast<size_t>(in.tellg());
        in.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> bytes(size);
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        return bytes;
    }

    std::string File::ReadAllText(const std::string& path)
    {
        auto bytes = ReadAllBytes(path);
        size_t start = 0;
        // Skip a UTF-8 BOM as .NET does.
        if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        {
            start = 3;
        }

        return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(start), bytes.end());
    }

    std::vector<std::string> File::ReadAllLines(const std::string& path)
    {
        std::string text = ReadAllText(path);
        std::vector<std::string> lines;
        std::string line;
        for (size_t i = 0; i < text.size(); i++)
        {
            char c = text[i];
            if (c == '\r')
            {
                if (i + 1 < text.size() && text[i + 1] == '\n')
                {
                    i++;
                }

                lines.push_back(line);
                line.clear();
                continue;
            }

            if (c == '\n')
            {
                lines.push_back(line);
                line.clear();
                continue;
            }

            line += c;
        }

        if (!line.empty())
        {
            lines.push_back(line);
        }

        return lines;
    }

    void File::WriteAllBytes(const std::string& path, std::span<const std::uint8_t> bytes)
    {
        std::ofstream out(P(path), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            throw IOException("Could not write '" + path + "'.");
        }

        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void File::WriteAllText(const std::string& path, std::string_view text)
    {
        WriteAllBytes(path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }

    void File::WriteAllLines(const std::string& path, const std::vector<std::string>& lines)
    {
        std::string text;
        for (const auto& l : lines)
        {
            text += l;
#if defined(_WIN32)
            text += "\r\n";
#else
            text += "\n";
#endif
        }

        WriteAllText(path, text);
    }

    void File::AppendAllText(const std::string& path, std::string_view text)
    {
        std::ofstream out(P(path), std::ios::binary | std::ios::app);
        if (!out)
        {
            throw IOException("Could not write '" + path + "'.");
        }

        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    void File::Delete(const std::string& path)
    {
        std::error_code ec;
        fs::remove(P(path), ec);
    }

    void File::Copy(const std::string& from, const std::string& to, bool overwrite)
    {
        auto options = overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none;
        std::error_code ec;
        fs::copy_file(P(from), P(to), options, ec);
        if (ec)
        {
            throw IOException("Could not copy '" + from + "' to '" + to + "': " + ec.message());
        }
    }

    void File::Move(const std::string& from, const std::string& to, bool overwrite)
    {
        std::error_code ec;
        if (overwrite)
        {
            fs::remove(P(to), ec);
        }

        fs::rename(P(from), P(to), ec);
        if (ec)
        {
            throw IOException("Could not move '" + from + "' to '" + to + "': " + ec.message());
        }
    }

    long long File::Length(const std::string& path)
    {
        std::error_code ec;
        auto size = fs::file_size(P(path), ec);
        if (ec)
        {
            throw FileNotFoundException("Could not find file '" + path + "'.");
        }

        return static_cast<long long>(size);
    }

    long long File::LastWriteTime(const std::string& path)
    {
        std::error_code ec;
        auto t = fs::last_write_time(P(path), ec);
        if (ec)
        {
            return 0;
        }

        return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
    }

    // ---- Directory ----

    bool Directory::Exists(const std::string& path)
    {
        std::error_code ec;
        return fs::is_directory(P(path), ec);
    }

    void Directory::CreateDirectory(const std::string& path)
    {
        std::error_code ec;
        fs::create_directories(P(path), ec);
    }

    void Directory::Delete(const std::string& path, bool recursive)
    {
        std::error_code ec;
        if (recursive)
        {
            fs::remove_all(P(path), ec);
            return;
        }

        fs::remove(P(path), ec);
    }

    std::vector<std::string> Directory::GetFiles(const std::string& path, std::string_view pattern, Search search)
    {
        std::vector<std::string> files;
        std::error_code ec;
        if (!fs::is_directory(P(path), ec))
        {
            throw DirectoryNotFoundException("Could not find a part of the path '" + path + "'.");
        }

        auto add = [&](const fs::directory_entry& e)
        {
            if (e.is_regular_file(ec) && Path::MatchesPattern(S(e.path().filename()), pattern))
            {
                files.push_back(S(e.path()));
            }
        };

        if (search == Search::AllDirectories)
        {
            for (const auto& e : fs::recursive_directory_iterator(P(path), fs::directory_options::skip_permission_denied, ec))
            {
                add(e);
            }
        }
        else
        {
            for (const auto& e : fs::directory_iterator(P(path), ec))
            {
                add(e);
            }
        }

        std::sort(files.begin(), files.end());
        return files;
    }

    std::vector<std::string> Directory::GetDirectories(const std::string& path, std::string_view pattern)
    {
        std::vector<std::string> dirs;
        std::error_code ec;
        if (!fs::is_directory(P(path), ec))
        {
            throw DirectoryNotFoundException("Could not find a part of the path '" + path + "'.");
        }

        for (const auto& e : fs::directory_iterator(P(path), ec))
        {
            if (e.is_directory(ec) && Path::MatchesPattern(S(e.path().filename()), pattern))
            {
                dirs.push_back(S(e.path()));
            }
        }

        std::sort(dirs.begin(), dirs.end());
        return dirs;
    }

    std::string Directory::GetCurrentDirectory() { return S(fs::current_path()); }

    // ---- Path ----

    std::string Path::Combine(std::string_view a, std::string_view b)
    {
        if (a.empty() || IsPathRooted(b))
        {
            return std::string(b);
        }

        if (b.empty())
        {
            return std::string(a);
        }

        char last = a.back();
        if (last == '/' || last == '\\')
        {
            return std::string(a) + std::string(b);
        }

        return std::string(a) + "/" + std::string(b);
    }

    std::string Path::Combine(std::string_view a, std::string_view b, std::string_view c) { return Combine(Combine(a, b), c); }

    std::string Path::Combine(std::string_view a, std::string_view b, std::string_view c, std::string_view d)
    {
        return Combine(Combine(Combine(a, b), c), d);
    }

    std::string Path::GetFileName(std::string_view path)
    {
        auto i = path.find_last_of("/\\");
        return std::string(i == std::string_view::npos ? path : path.substr(i + 1));
    }

    std::string Path::GetExtension(std::string_view path)
    {
        std::string name = GetFileName(path);
        auto i = name.rfind('.');
        if (i == std::string::npos || i == name.size() - 1)
        {
            return "";
        }

        return name.substr(i);
    }

    std::string Path::GetFileNameWithoutExtension(std::string_view path)
    {
        std::string name = GetFileName(path);
        auto i = name.rfind('.');
        return i == std::string::npos ? name : name.substr(0, i);
    }

    std::string Path::ChangeExtension(std::string_view path, std::string_view extension)
    {
        std::string p(path);
        std::string ext = GetExtension(p);
        if (!ext.empty())
        {
            p.resize(p.size() - ext.size());
        }

        if (extension.empty())
        {
            return p;
        }

        if (extension[0] != '.')
        {
            p += '.';
        }

        return p + std::string(extension);
    }

    std::string Path::GetDirectoryName(std::string_view path)
    {
        auto i = path.find_last_of("/\\");
        if (i == std::string_view::npos)
        {
            return "";
        }

        if (i == 0)
        {
            return path.size() > 1 ? "/" : "";
        }

        return std::string(path.substr(0, i));
    }

    std::string Path::GetFullPath(std::string_view path)
    {
        std::error_code ec;
        return S(fs::absolute(P(path), ec).lexically_normal());
    }

    bool Path::IsPathRooted(std::string_view path)
    {
        if (path.empty())
        {
            return false;
        }

        if (path[0] == '/' || path[0] == '\\')
        {
            return true;
        }

        return path.size() >= 2 && path[1] == ':';
    }

    std::string Path::GetTempPath()
    {
        std::error_code ec;
        return S(fs::temp_directory_path(ec));
    }

    std::string Path::BaseDirectory()
    {
#if defined(_WIN32)
        wchar_t buf[4096];
        DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
        return S(fs::path(std::wstring(buf, n)).parent_path());
#elif defined(__APPLE__)
        char buf[4096];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) == 0)
        {
            std::error_code ec;
            return S(fs::canonical(buf, ec).parent_path());
        }

        return S(fs::current_path());
#elif defined(__ANDROID__)
        return S(fs::current_path());
#else
        std::error_code ec;
        auto exe = fs::read_symlink("/proc/self/exe", ec);
        return ec ? S(fs::current_path()) : S(exe.parent_path());
#endif
    }

    bool Path::MatchesPattern(std::string_view name, std::string_view pattern)
    {
        // Glob with * and ?, case-insensitive.
        size_t n = 0;
        size_t p = 0;
        size_t starP = std::string_view::npos;
        size_t starN = 0;
        auto up = [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; };
        while (n < name.size())
        {
            if (p < pattern.size() && (pattern[p] == '?' || up(pattern[p]) == up(name[n])))
            {
                n++;
                p++;
                continue;
            }

            if (p < pattern.size() && pattern[p] == '*')
            {
                starP = p++;
                starN = n;
                continue;
            }

            if (starP != std::string_view::npos)
            {
                p = starP + 1;
                n = ++starN;
                continue;
            }

            return false;
        }

        while (p < pattern.size() && pattern[p] == '*')
        {
            p++;
        }

        return p == pattern.size();
    }
}
