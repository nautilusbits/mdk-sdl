#pragma once

// A writer that passes text on and copies whole lines to logcat (tag "MDK"), so adb sees the
// game's output (e.g. --perf's "Perf:" lines):
//   Console::WriteLine ──► Logcat ──► inner writer (the log)
//                                └──► logcat

#include <mutex>
#include <string>
#include <string_view>

#include "core/Console.h"

namespace Mdk::Android
{
    class Logcat final : public TextWriter
    {
    public:
        explicit Logcat(TextWriter& inner) : _inner(inner) {}

        void Write(std::string_view text) override;
        void Flush() override { _inner.Flush(); }

        // Console's Out and Error copied to logcat from here (the writers live forever).
        static void Install();

    private:
        TextWriter& _inner;
        std::string _line;
        std::mutex _lock;
    };
}
