#pragma once

#include <functional>
#include <string>

#include "Mdk.Core/Cancellation.h"
#include "Mdk.Core/DotNet.h"
#include "Mdk.Core/Uri.h"

namespace Mdk::Engine::Platform
{
    // The operating systems as the download sees them.
    enum class HostOs { Windows, Linux, MacOs, Android, Other };

    // The OS's HTTP library: WinHTTP (Windows), libcurl (Linux, macOS), none (Android).
    enum class HttpBackend { WinHttp, Curl, None };

    enum class Transport { Plain, Tls };

    // A URL as the connection needs it: https://github.com/a?b ─► github.com, 443, /a?b, Tls.
    struct HttpTarget
    {
        std::string Host;
        int Port = 0;
        std::string Path;
        Platform::Transport Transport = Platform::Transport::Plain;

        bool operator==(const HttpTarget&) const = default;

        static HttpTarget Of(const Uri& uri);
    };

    // Downloads a file over HTTP(S) through the OS's own library, not .NET's HttpClient
    // (2.8 MB of the native exe). Redirects are followed (GitHub's release links redirect).
    //   Save ─► WinHTTP   (winhttp.dll)            Windows
    //        ─► libcurl   (libcurl.so.4, .dylib)   Linux, macOS
    class HttpDownload
    {
    public:
        // Who asks (some servers refuse requests without one).
        static constexpr const char* UserAgent = "mdk-sdl";

        // Saves url into path (progress: bytes so far, total or 0). Throws IOException naming the failure.
        static void Save(const std::string& url, const std::string& path, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel);

        static HttpBackend BackendOf(HostOs os);

        // The error of a URL's download.
        static IOException Failure(const Uri& uri, const std::string& why);

        static std::string StatusText(long long status);

    private:
        static HostOs CurrentOs();
    };
}
