#include "Mdk.Engine/Diagnostics/LogWriter.h"

#include <cstdlib>
#include <exception>
#include <format>

namespace Mdk::Engine::Diagnostics
{
    void LogWriter::Write(std::string_view value)
    {
        std::lock_guard guard(_lock);
        _inner.Write(value);
        for (char c : value)
        {
            Collect(c);
        }
    }

    void LogWriter::Collect(char c)
    {
        if (c == '\r')
        {
            return;
        }

        if (c != '\n')
        {
            _line += c;
            return;
        }

        _ring.Add(_line);
        _line.clear();
    }

    LogRing& Log::Capture(int capacity)
    {
        if (_ring != nullptr)
        {
            return *_ring;
        }

        _ring = new LogRing(capacity);
        Console::SetOut(new LogWriter(Console::Out(), *_ring));
        Console::SetError(new LogWriter(Console::Error(), *_ring));
        WatchCrashes();
        return *_ring;
    }

    void Log::WatchCrashes()
    {
        // C++ has no unhandled-exception event: the terminate handler logs, then aborts.
        std::set_terminate([]
        {
            std::string what = "unknown";
            if (auto current = std::current_exception())
            {
                try
                {
                    std::rethrow_exception(current);
                }
                catch (const std::exception& e)
                {
                    what = e.what();
                }
                catch (...)
                {
                }
            }

            Console::Error().WriteLine(std::format("Unhandled exception: {}", what));
            std::abort();
        });
    }
}
