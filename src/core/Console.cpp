#include "core/Console.h"

#include <atomic>
#include <cstdio>

namespace Mdk
{
    void TextWriter::WriteLine(std::string_view text)
    {
        std::string line(text);
        line += '\n';
        Write(line);
    }

    namespace
    {
        // stdout / stderr, unbuffered past each write as .NET's console is.
        class FileWriter final : public TextWriter
        {
        public:
            explicit FileWriter(std::FILE* file) : _file(file) {}

            void Write(std::string_view text) override
            {
                std::fwrite(text.data(), 1, text.size(), _file);
                std::fflush(_file);
            }

            void Flush() override { std::fflush(_file); }

        private:
            std::FILE* _file;
        };

        FileWriter& StdOut()
        {
            static FileWriter writer(stdout);
            return writer;
        }

        FileWriter& StdErr()
        {
            static FileWriter writer(stderr);
            return writer;
        }

        std::atomic<TextWriter*> _out{nullptr};
        std::atomic<TextWriter*> _error{nullptr};
    }

    namespace Console
    {
        TextWriter& Out()
        {
            TextWriter* writer = _out.load();
            return writer != nullptr ? *writer : StdOut();
        }

        TextWriter& Error()
        {
            TextWriter* writer = _error.load();
            return writer != nullptr ? *writer : StdErr();
        }

        void SetOut(TextWriter* writer) { _out.store(writer); }
        void SetError(TextWriter* writer) { _error.store(writer); }

        void WriteLine(std::string_view text) { Out().WriteLine(text); }
        void Write(std::string_view text) { Out().Write(text); }
    }
}
