#include "Mdk.Engine/Platform/HttpDownload.h"

#include <format>

#include "Mdk.Core/FileStream.h"
#include "Mdk.Core/PlatformNotSupportedException.h"
#include "Mdk.Engine/Platform/Curl.h"
#include "Mdk.Engine/Platform/WinHttp.h"

namespace Mdk::Engine::Platform
{
    HttpTarget HttpTarget::Of(const Uri& uri)
    {
        if (uri.Scheme() != Uri::UriSchemeHttp && uri.Scheme() != Uri::UriSchemeHttps)
        {
            throw ArgumentException(std::format("{}: not http(s)", uri.ToString()));
        }

        auto transport = uri.Scheme() == Uri::UriSchemeHttps ? Transport::Tls : Transport::Plain;
        return HttpTarget{uri.IdnHost(), uri.Port(), uri.PathAndQuery(), transport};
    }

    void HttpDownload::Save(const std::string& url, const std::string& path, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel)
    {
        cancel.ThrowIfCancellationRequested();
        Uri uri(url);
        auto file = FileStream::Create(path);
        switch (BackendOf(CurrentOs()))
        {
            case HttpBackend::WinHttp:
                WinHttp::Get(uri, file, progress, cancel);
                return;
            case HttpBackend::Curl:
                Curl::Get(uri, file, progress, cancel);
                return;
            default:
                throw PlatformNotSupportedException("No HTTP library on this platform");
        }
    }

    HttpBackend HttpDownload::BackendOf(HostOs os)
    {
        switch (os)
        {
            case HostOs::Windows:
                return HttpBackend::WinHttp;
            case HostOs::Android:
                return HttpBackend::None;
            default:
                return HttpBackend::Curl;
        }
    }

    HostOs HttpDownload::CurrentOs()
    {
#if defined(_WIN32)
        return HostOs::Windows;
#elif defined(__ANDROID__)
        return HostOs::Android;
#elif defined(__linux__)
        return HostOs::Linux;
#elif defined(__APPLE__)
        return HostOs::MacOs;
#else
        return HostOs::Other;
#endif
    }

    IOException HttpDownload::Failure(const Uri& uri, const std::string& why)
    {
        return IOException(std::format("{}: {}", uri.ToString(), why));
    }

    std::string HttpDownload::StatusText(long long status)
    {
        return std::format("HTTP {}", status);
    }
}
