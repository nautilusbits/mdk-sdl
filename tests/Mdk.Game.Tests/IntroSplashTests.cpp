#include <doctest.h>

#include "Mdk.Core/Files.h"
#include "Mdk.Formats/Bni.h"
#include "Mdk.Game/Menu/IntroSplash.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace Mdk;
using namespace Mdk::Game::Menu;

// The splash before the main menu (#22).
namespace
{
    void WriteInt(std::vector<std::uint8_t>& bytes, size_t at, std::int32_t value)
    {
        std::memcpy(bytes.data() + at, &value, sizeof value);
    }

    // A BNI of one entry: u32 size, u32 count, char[12] name, u32 offset (from 4), data.
    std::string Archive(const std::string& name, const std::vector<std::uint8_t>& data)
    {
        constexpr int Header = 4 + 4 + 12 + 4;
        std::vector<std::uint8_t> bytes(Header + data.size());
        WriteInt(bytes, 0, static_cast<std::int32_t>(bytes.size() - 4));
        WriteInt(bytes, 4, 1);
        std::memcpy(bytes.data() + 8, name.data(), name.size());
        WriteInt(bytes, 20, Header - 4);
        std::memcpy(bytes.data() + Header, data.data(), data.size());

        static std::atomic<int> counter{0};
        auto path = Path::Combine(Path::GetTempPath(), "IntroSplashTests" + std::to_string(counter++) + ".bni");
        File::WriteAllBytes(path, bytes);
        return path;
    }
}

TEST_CASE("IntroSplashTests.ShortSplashIsSkipped")
{
    auto bni = Formats::Bni::Load(Archive("INTRO1A", std::vector<std::uint8_t>(100)));

    // The C# passes null! for the Ui: a short entry never touches it.
    Ui* ui = nullptr;
    CHECK(IntroSplash(*ui, *bni).Done());
}
