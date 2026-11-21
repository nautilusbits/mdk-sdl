#include <filesystem>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <doctest.h>

#include "core/Cancellation.h"
#include "core/DotNet.h"
#include "core/Files.h"
#include "core/Sha256.h"
#include "core/Uri.h"
#include "core/ZipFile.h"
#include "engine/upscale/RealEsrgan.h"
#include "engine/upscale/UpscalerSource.h"
#include "game/flow/Settings.h"
#include "game/hd_textures/HdGenerator.h"

using namespace Mdk;
using namespace Mdk::Engine::Upscale;

// Where the upscaler comes from: settings.cfg's upscaler_url and upscaler_sha256 over the
// platform's defaults, and the install checked against the SHA-256.
namespace
{
    constexpr const char* Url = "https://example.org/realesrgan.zip";
    constexpr const char* Sha256Text = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

    // Directory.CreateTempSubdirectory("mdk-upscaler-"), deleted at the end.
    class TempFolder
    {
    public:
        TempFolder()
        {
            std::random_device random;
            Path = Mdk::Path::Combine(Mdk::Path::GetTempPath(), std::format("mdk-upscaler-{:08x}", random()));
            Directory::CreateDirectory(Path);
        }

        ~TempFolder() { Directory::Delete(Path, true); }

        std::string Path;
    };

    // A release archive holding a stand-in tool, and its SHA-256.
    std::pair<std::string, std::string> FakeRelease(const std::string& folder)
    {
        auto zip = Path::Combine(folder, "release.zip");
#if defined(_WIN32)
        std::string name = "realesrgan-ncnn-vulkan.exe";
#else
        std::string name = "realesrgan-ncnn-vulkan";
#endif
        std::string tool = "tool";
        std::vector<std::pair<std::string, std::vector<byte>>> entries = {{name, std::vector<byte>(tool.begin(), tool.end())}};
        ZipFile::Create(zip, entries);

        return {zip, Convert::ToHexStringLower(Sha256::HashData(File::ReadAllBytes(zip)))};
    }
}

TEST_CASE("UpscalerSourceTests.DefaultsAreThePlatformsRelease")
{
    auto& source = UpscalerSource::Default();

    CHECK(Str::StartsWith(source.Url, "https://github.com/xinntao/Real-ESRGAN/releases/download/"));
    CHECK(Str::EndsWith(source.Url, ".zip"));
    CHECK(source.Sha256.size() == 64);
    CHECK(source == Game::Flow::Settings().Upscaler());
}

TEST_CASE("UpscalerSourceTests.EmptyValuesAreTheDefaults")
{
    auto settings = Game::Flow::Settings::Parse("upscaler_url=\nupscaler_sha256=\n");

    CHECK(settings.Upscaler() == UpscalerSource::Default());
    CHECK(Str::Contains(settings.Format(), "upscaler_url=\n"));
    CHECK(Str::Contains(settings.Format(), "upscaler_sha256=\n"));
}

TEST_CASE("UpscalerSourceTests.UserValuesOverride")
{
    auto settings = Game::Flow::Settings::Parse(std::string("upscaler_url=") + Url + "\nupscaler_sha256=" + Str::ToUpper(Sha256Text) + "\n");

    CHECK(settings.Upscaler() == UpscalerSource{Url, Sha256Text});
    CHECK(Game::Flow::Settings::Parse(settings.Format()).Format() == settings.Format());
    CHECK(Str::Contains(settings.Format(), std::string("upscaler_url=") + Url + "\n"));
}

TEST_CASE("UpscalerSourceTests.OptionsCarryTheSource")
{
    auto settings = Game::Flow::Settings::Parse(std::string("upscaler_url=") + Url + "\nupscaler_sha256=" + Sha256Text + "\n");

    CHECK(Game::HdTextures::HdOptions::Default().Source == UpscalerSource::Default());
    auto options = Game::HdTextures::HdOptions::Default();
    options.Source = settings.Upscaler();
    CHECK(options.Source == settings.Upscaler());
}

TEST_CASE("UpscalerSourceTests.InstallsACheckedLocalArchive")
{
    TempFolder temp;
    auto [zip, sha256] = FakeRelease(temp.Path);

    CHECK_NOTHROW(RealEsrgan::Install(Path::Combine(temp.Path, "tool"), UpscalerSource{Uri(zip).AbsoluteUri(), sha256}, nullptr, CancellationToken::None()));

    CHECK(RealEsrgan::IsInstalled(Path::Combine(temp.Path, "tool"), UpscalerSource{zip, sha256}));
    // Another checksum: another release, installed again.
    CHECK_FALSE(RealEsrgan::IsInstalled(Path::Combine(temp.Path, "tool"), UpscalerSource{zip, Sha256Text}));
}

TEST_CASE("UpscalerSourceTests.RefusesAWrongChecksum")
{
    TempFolder temp;
    auto [zip, sha256] = FakeRelease(temp.Path);
    auto folder = Path::Combine(temp.Path, "tool");

    std::string message;
    try
    {
        RealEsrgan::Install(folder, UpscalerSource{zip, Sha256Text}, nullptr, CancellationToken::None());
    }
    catch (const InvalidDataException& error)
    {
        message = error.what();
    }

    CHECK(Str::Contains(message, Sha256Text));
    CHECK_FALSE(RealEsrgan::IsInstalled(folder, UpscalerSource{zip, Sha256Text}));
    CHECK(Directory::GetFiles(folder).empty());
}
