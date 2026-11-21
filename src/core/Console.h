#pragma once

// System.Console's Out / Error writers, replaceable (Console.SetOut) so the log can copy them.
//   Console::WriteLine("hello");   Console::Error().WriteLine("oops");

#include <string>
#include <string_view>

namespace Mdk
{
    // System.IO.TextWriter: text goes out through Write; lines end with "\n".
    class TextWriter
    {
    public:
        virtual ~TextWriter() = default;
        virtual void Write(std::string_view text) = 0;
        virtual void Flush() {}
        void WriteLine(std::string_view text);
    };

    namespace Console
    {
        TextWriter& Out();
        TextWriter& Error();

        // The writer stays owned by the caller and must outlive its use (the log's live forever).
        void SetOut(TextWriter* writer);
        void SetError(TextWriter* writer);

        void WriteLine(std::string_view text);
        void Write(std::string_view text);
    }
}
