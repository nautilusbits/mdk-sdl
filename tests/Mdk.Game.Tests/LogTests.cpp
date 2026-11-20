#include <random>
#include <string>
#include <vector>

#include <doctest.h>

#include "Mdk.Core/Console.h"
#include "Mdk.Core/Files.h"
#include "Mdk.Engine/Diagnostics/LogRing.h"
#include "Mdk.Engine/Diagnostics/LogWriter.h"

using namespace Mdk;
using namespace Mdk::Engine::Diagnostics;

// The log the console shows: the last lines printed, still written to the output.

namespace
{
    // System.IO.StringWriter.
    class StringWriter final : public TextWriter
    {
    public:
        void Write(std::string_view text) override { Text += text; }

        std::string Text;
    };
}

TEST_CASE("LogTests.RingKeepsTheLastLines")
{
    LogRing ring(3);
    for (const char* line : {"a", "b", "c", "d"})
    {
        ring.Add(line);
    }

    CHECK(ring.Lines() == std::vector<std::string>{"b", "c", "d"});
    CHECK(ring.Lines(2) == std::vector<std::string>{"c", "d"});
    ring.Clear();
    CHECK(ring.Lines().empty());
}

TEST_CASE("LogTests.WriterCopiesWholeLinesToTheRing")
{
    LogRing ring(10);
    StringWriter output;
    LogWriter writer(output, ring);

    // Parts of a line wait for its end; \r\n ends a line too.
    writer.Write("Level 3: ");
    writer.Write(std::to_string(42));
    writer.WriteLine(" arenas");
    writer.Write("one\r\ntwo\nthree");

    CHECK(output.Text == "Level 3: 42 arenas\none\r\ntwo\nthree");
    CHECK(ring.Lines() == std::vector<std::string>{"Level 3: 42 arenas", "one", "two"});
}

TEST_CASE("LogTests.RingWritesItsLinesToAFile")
{
    std::random_device random;
    auto path = Path::Combine(Path::GetTempPath(), "mdk_log_" + std::to_string(random()) + std::to_string(random()) + ".log");
    LogRing ring(10);
    ring.Add("before");

    // The lines kept so far, then each new one as it comes.
    ring.WriteTo(path);
    ring.Add("after");
    ring.Close();

    CHECK(File::ReadAllLines(path) == std::vector<std::string>{"before", "after"});
    File::Delete(path);
}
