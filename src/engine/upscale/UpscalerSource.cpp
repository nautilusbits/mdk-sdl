#include "engine/upscale/UpscalerSource.h"

#include "core/DotNet.h"

namespace Mdk::Engine::Upscale
{
    namespace
    {
        constexpr const char* Release = "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/";
        constexpr const char* WindowsZip = "realesrgan-ncnn-vulkan-20220424-windows.zip";
        constexpr const char* WindowsSha256 = "abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d";
        constexpr const char* LinuxZip = "realesrgan-ncnn-vulkan-20220424-ubuntu.zip";
        constexpr const char* LinuxSha256 = "e5aa6eb131234b87c0c51f82b89390f5e3e642b7b70f2b9bbe95b6a285a40c96";
        constexpr const char* MacZip = "realesrgan-ncnn-vulkan-20220424-macos.zip";
        constexpr const char* MacSha256 = "e0ad05580abfeb25f8d8fb55aaf7bedf552c375b5b4d9bd3c8d59764d2cc333a";
    }

    const UpscalerSource& UpscalerSource::Default()
    {
#if defined(_WIN32)
        static const UpscalerSource source{std::string(Release) + WindowsZip, WindowsSha256};
#elif defined(__APPLE__)
        static const UpscalerSource source{std::string(Release) + MacZip, MacSha256};
#else
        static const UpscalerSource source{std::string(Release) + LinuxZip, LinuxSha256};
#endif
        return source;
    }

    UpscalerSource UpscalerSource::Of(const std::string& url, const std::string& sha256)
    {
        auto custom = !url.empty();
        auto hash = !sha256.empty() ? Str::ToLower(sha256) : custom ? "" : Default().Sha256;
        return UpscalerSource{custom ? url : Default().Url, hash};
    }
}
