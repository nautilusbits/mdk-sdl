#pragma once

#include <climits>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Mdk::Engine::Diagnostics
{
    // The last lines printed, oldest first (the console shows them). Any thread may add.
    class LogRing
    {
    public:
        explicit LogRing(int capacity) : _capacity(capacity) {}

        void Add(const std::string& line);

        // The last `count` lines (all by default), oldest first.
        std::vector<std::string> Lines(int count = INT_MAX);

        // Lines into `lines` (emptied first): no list per call.
        void CopyLast(int count, std::vector<std::string>& lines);

        void Clear();

        // Also writes the lines to `path` (mdk.log, made anew), those kept so far first; each line
        // is flushed, so a crash keeps them.
        void WriteTo(const std::string& path);

        void Close();

    private:
        int _capacity;
        std::deque<std::string> _lines;
        std::mutex _lock;
        std::unique_ptr<std::ofstream> _file;
    };
}
