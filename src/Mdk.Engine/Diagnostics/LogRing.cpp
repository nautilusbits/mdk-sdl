#include "Mdk.Engine/Diagnostics/LogRing.h"

#include <algorithm>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Engine::Diagnostics
{
    void LogRing::Add(const std::string& line)
    {
        std::lock_guard guard(_lock);
        _lines.push_back(line);
        if (_file)
        {
            *_file << line << '\n' << std::flush;
        }

        if (static_cast<int>(_lines.size()) > _capacity)
        {
            _lines.pop_front();
        }
    }

    std::vector<std::string> LogRing::Lines(int count)
    {
        std::lock_guard guard(_lock);
        int skip = std::max(static_cast<int>(_lines.size()) - count, 0);
        return {_lines.begin() + skip, _lines.end()};
    }

    void LogRing::CopyLast(int count, std::vector<std::string>& lines)
    {
        lines.clear();
        std::lock_guard guard(_lock);
        int skip = std::max(static_cast<int>(_lines.size()) - count, 0);
        for (const auto& line : _lines)
        {
            if (skip-- <= 0)
            {
                lines.push_back(line);
            }
        }
    }

    void LogRing::Clear()
    {
        std::lock_guard guard(_lock);
        _lines.clear();
    }

    void LogRing::WriteTo(const std::string& path)
    {
        auto file = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!*file)
        {
            throw IOException("Could not open " + path);
        }

        std::lock_guard guard(_lock);
        _file = std::move(file);
        for (const auto& line : _lines)
        {
            *_file << line << '\n';
        }

        *_file << std::flush;
    }

    void LogRing::Close()
    {
        std::lock_guard guard(_lock);
        _file.reset();
    }
}
