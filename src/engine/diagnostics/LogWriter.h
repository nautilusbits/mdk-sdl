#pragma once

#include <mutex>
#include <string>
#include <string_view>

#include "core/Console.h"
#include "engine/diagnostics/LogRing.h"

namespace Mdk::Engine::Diagnostics
{
    // A writer that passes everything on unchanged and copies whole lines to a ring.
    //   Console::WriteLine ──► LogWriter ──► stdout / stderr (as before)
    //                                  └──► LogRing (the console's log)
    class LogWriter final : public TextWriter
    {
    public:
        LogWriter(TextWriter& inner, LogRing& ring) : _inner(inner), _ring(ring) {}

        void Write(std::string_view value) override;
        void Flush() override { _inner.Flush(); }

    private:
        // A line ends at \n; \r is dropped.
        void Collect(char c);

        TextWriter& _inner;
        LogRing& _ring;
        std::string _line;
        std::mutex _lock;
    };

    // Captures the program's output (stdout and stderr) into one ring, still printing it.
    struct Log
    {
        // The lines the console keeps.
        static constexpr int Lines = 500;

        // The ring of the output's last lines; the first call installs it (it lives forever).
        static LogRing& Capture(int capacity);

    private:
        // Exceptions no one catches (any thread) are logged: a phone's only trace of a crash is
        // mdk.log.
        static void WatchCrashes();

        static inline LogRing* _ring = nullptr;
    };
}
