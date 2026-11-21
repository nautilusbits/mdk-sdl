#include "engine/platform/WinHttp.h"

#include <format>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

#include "core/DotNet.h"
#include "core/PlatformNotSupportedException.h"
#include "engine/platform/HttpDownload.h"

namespace Mdk::Engine::Platform
{
    std::string WinHttp::Describe(int error)
    {
        switch (error)
        {
            case ErrorTimeout: return "timed out";
            case ErrorNameNotResolved: return "host not found";
            case ErrorCannotConnect: return "can't connect";
            case ErrorConnectionLost: return "connection lost";
            case ErrorSecureFailure: return "secure connection failed";
            default: return std::format("WinHTTP error {}", error);
        }
    }

#if defined(_WIN32)
    namespace
    {
        std::wstring Utf16(const std::string& text)
        {
            if (text.empty())
            {
                return {};
            }

            auto length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
            return wide;
        }
    }

    WinHttp::Handle::~Handle()
    {
        if (!IsInvalid())
        {
            WinHttpCloseHandle(_handle);
        }
    }

    void WinHttp::Get(const Uri& uri, std::ostream& to, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel)
    {
        auto target = HttpTarget::Of(uri);
        Handle session(Check(uri, WinHttpOpen(Utf16(HttpDownload::UserAgent).c_str(), AccessAutomaticProxy, nullptr, nullptr, 0)));
        Handle connection(Check(uri, WinHttpConnect(session.Get(), Utf16(target.Host).c_str(), static_cast<INTERNET_PORT>(target.Port), 0)));
        auto flags = target.Transport == Transport::Tls ? FlagSecure : 0;
        Handle request(Check(uri, WinHttpOpenRequest(connection.Get(), L"GET", Utf16(target.Path).c_str(), nullptr, nullptr, nullptr, flags)));
        Check(uri, WinHttpSendRequest(request.Get(), nullptr, 0, nullptr, 0, 0, 0) != FALSE);
        Check(uri, WinHttpReceiveResponse(request.Get(), nullptr) != FALSE);

        DWORD status = 0;
        DWORD size = sizeof(DWORD);
        Check(uri, WinHttpQueryHeaders(request.Get(), QueryStatusCode | QueryFlagNumber, nullptr, &status, &size, nullptr) != FALSE);
        if (status != StatusOk)
        {
            throw HttpDownload::Failure(uri, HttpDownload::StatusText(status));
        }

        // No Content-Length (chunked): the total is unknown, 0.
        long long total = 0;
        size = sizeof(long long);
        WinHttpQueryHeaders(request.Get(), QueryContentLength | QueryFlagNumber64, nullptr, &total, &size, nullptr);

        Read(uri, request, to, total, progress, cancel);
    }

    // The body's bytes until WinHTTP gives none.
    void WinHttp::Read(const Uri& uri, const Handle& request, std::ostream& to, long long total, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel)
    {
        std::vector<char> buffer(ReadBuffer);
        long long done = 0;
        while (true)
        {
            cancel.ThrowIfCancellationRequested();
            DWORD read = 0;
            Check(uri, WinHttpReadData(request.Get(), buffer.data(), ReadBuffer, &read) != FALSE);
            if (read == 0)
            {
                return;
            }

            to.write(buffer.data(), static_cast<std::streamsize>(read));
            if (!to)
            {
                throw IOException(std::format("{}: write failed", uri.ToString()));
            }

            done += read;
            if (progress)
            {
                progress(done, total);
            }
        }
    }

    void* WinHttp::Check(const Uri& uri, void* handle)
    {
        if (handle == nullptr)
        {
            throw HttpDownload::Failure(uri, Describe(static_cast<int>(GetLastError())));
        }

        return handle;
    }

    void WinHttp::Check(const Uri& uri, bool done)
    {
        if (!done)
        {
            throw HttpDownload::Failure(uri, Describe(static_cast<int>(GetLastError())));
        }
    }
#else
    WinHttp::Handle::~Handle() = default;

    void WinHttp::Get(const Uri& uri, std::ostream& to, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel)
    {
        (void)uri;
        (void)to;
        (void)progress;
        (void)cancel;
        throw PlatformNotSupportedException("WinHTTP is Windows only");
    }
#endif
}
