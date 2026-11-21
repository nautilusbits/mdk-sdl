#include <algorithm>
#include <atomic>
#include <format>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <doctest.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "core/Cancellation.h"
#include "core/DotNet.h"
#include "core/Files.h"
#include "core/Uri.h"
#include "engine/platform/Curl.h"
#include "engine/platform/HttpDownload.h"
#include "engine/platform/WinHttp.h"

using namespace Mdk;
using namespace Mdk::Engine::Platform;

// The upscaler's download through the OS's HTTP library (WinHTTP, libcurl): URLs, the
// backend per OS, errors, and real transfers from a local server.

namespace
{
    constexpr std::string_view Body = "the upscaler's release";

#if defined(_WIN32)
    using Socket = SOCKET;
    constexpr Socket NoSocket = INVALID_SOCKET;
    void CloseSocket(Socket s) { closesocket(s); }
#else
    using Socket = int;
    constexpr Socket NoSocket = -1;
    void CloseSocket(Socket s) { close(s); }
#endif

    // A one-request-per-connection HTTP/1.1 server on localhost:
    // /redirect ─► 302 /file ─► 200 Body; anything else 404.
    class TestServer
    {
    public:
        TestServer()
        {
#if defined(_WIN32)
            WSADATA data;
            WSAStartup(MAKEWORD(2, 2), &data);
#endif
            _listener = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            bind(_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address));
            listen(_listener, SOMAXCONN);
            socklen_t length = sizeof(address);
            getsockname(_listener, reinterpret_cast<sockaddr*>(&address), &length);
            _port = ntohs(address.sin_port);
            _loop = std::thread([this] { Serve(); });
        }

        ~TestServer()
        {
            _stopping = true;
            // Wakes the accept up.
            auto wake = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(static_cast<unsigned short>(_port));
            connect(wake, reinterpret_cast<sockaddr*>(&address), sizeof(address));
            CloseSocket(wake);
            _loop.join();
            CloseSocket(_listener);
        }

        std::string Url(const std::string& path) const { return std::format("http://127.0.0.1:{}/{}", _port, path); }

    private:
        void Serve()
        {
            while (true)
            {
                auto client = accept(_listener, nullptr, nullptr);
                if (_stopping || client == NoSocket)
                {
                    if (client != NoSocket)
                    {
                        CloseSocket(client);
                    }

                    return;
                }

                Answer(client);
                CloseSocket(client);
            }
        }

        void Answer(Socket stream)
        {
            std::string request;
            char buffer[1024];
            while (request.find("\r\n\r\n") == std::string::npos)
            {
                auto read = recv(stream, buffer, sizeof(buffer), 0);
                if (read <= 0)
                {
                    return;
                }

                request.append(buffer, static_cast<size_t>(read));
            }

            auto path = Str::Split(request, ' ')[1];
            std::string response;
            if (path == "/redirect")
            {
                response = std::format("HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:{}/file\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", _port);
            }
            else if (path == "/file")
            {
                response = std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", Body.size(), Body);
            }
            else
            {
                response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            }

            send(stream, response.data(), static_cast<int>(response.size()), 0);
        }

        Socket _listener = NoSocket;
        int _port = 0;
        std::atomic<bool> _stopping = false;
        std::thread _loop;
    };

    // Path.GetTempFileName(): a new empty file.
    std::string TempFile()
    {
        static std::atomic<int> next = 0;
        auto path = Path::Combine(Path::GetTempPath(), std::format("mdk-http-{}-{}.tmp", static_cast<long long>(StopwatchSeconds() * 1e6), next++));
        File::WriteAllText(path, "");
        return path;
    }
}

TEST_CASE("HttpDownloadTests.CracksHttpsUrls")
{
    auto target = HttpTarget::Of(Uri("https://github.com/x/releases/download/v1/a.zip?b=1"));

    CHECK(target == HttpTarget{"github.com", 443, "/x/releases/download/v1/a.zip?b=1", Transport::Tls});
}

TEST_CASE("HttpDownloadTests.CracksHttpUrlsWithPorts")
{
    auto target = HttpTarget::Of(Uri("http://localhost:8080/"));

    CHECK(target == HttpTarget{"localhost", 8080, "/", Transport::Plain});
}

TEST_CASE("HttpDownloadTests.RefusesOtherSchemes")
{
    CHECK_THROWS_AS(HttpTarget::Of(Uri("ftp://example.org/a.zip")), ArgumentException);
}

TEST_CASE("HttpDownloadTests.PicksTheOsLibrary")
{
    const std::vector<std::pair<HostOs, HttpBackend>> data = {
        {HostOs::Windows, HttpBackend::WinHttp},
        {HostOs::Linux, HttpBackend::Curl},
        {HostOs::MacOs, HttpBackend::Curl},
        {HostOs::Other, HttpBackend::Curl},
        {HostOs::Android, HttpBackend::None},
    };
    for (auto [os, backend] : data)
    {
        CHECK(HttpDownload::BackendOf(os) == backend);
    }
}

TEST_CASE("HttpDownloadTests.CurlFollowsRedirectsAndFailsOnErrors")
{
    auto settings = Curl::Settings(Uri("https://example.org/a.zip"));
    auto contains = [&](const CurlSetting& setting) { return std::find(settings.begin(), settings.end(), setting) != settings.end(); };

    CHECK(contains(CurlSetting{CurlOption::Url, 0, "https://example.org/a.zip"}));
    CHECK(contains(CurlSetting{CurlOption::FollowLocation, 1, std::nullopt}));
    CHECK(contains(CurlSetting{CurlOption::FailOnError, 1, std::nullopt}));
    CHECK(contains(CurlSetting{CurlOption::NoProgress, 0, std::nullopt}));
    CHECK(std::any_of(settings.begin(), settings.end(), [](const CurlSetting& s) { return s.Option == CurlOption::UserAgent && s.Text && !s.Text->empty(); }));
}

TEST_CASE("HttpDownloadTests.CurlErrorsAreNamed")
{
    struct Case { int Code; long long Status; const char* Text; const char* Expected; };
    const Case data[] = {
        {22, 404, "HTTP response code said error", "HTTP 404"},
        {6, 0, "Couldn't resolve host name", "Couldn't resolve host name"},
    };
    for (auto& c : data)
    {
        CHECK(Curl::Describe(c.Code, c.Status, c.Text) == c.Expected);
    }
}

TEST_CASE("HttpDownloadTests.WinHttpErrorsAreNamed")
{
    const std::vector<std::pair<int, std::string>> data = {
        {12007, "host not found"},
        {12029, "can't connect"},
        {12175, "secure connection failed"},
        {12345, "WinHTTP error 12345"},
    };
    for (auto& [code, expected] : data)
    {
        CHECK(WinHttp::Describe(code) == expected);
    }
}

TEST_CASE("HttpDownloadTests.SavesFollowingRedirects")
{
    TestServer server;
    auto path = TempFile();
    long long done = 0, total = 0;
    try
    {
        HttpDownload::Save(server.Url("redirect"), path, [&](long long d, long long t) { done = d; total = t; }, CancellationToken::None());

        CHECK(File::ReadAllText(path) == Body);
        CHECK(done == static_cast<long long>(Body.size()));
        CHECK(total == static_cast<long long>(Body.size()));
    }
    catch (const std::exception& e)
    {
        FAIL(e.what());
    }

    File::Delete(path);
}

TEST_CASE("HttpDownloadTests.NamesTheStatusOfAFailure")
{
    TestServer server;
    auto path = TempFile();
    std::string message;
    try
    {
        HttpDownload::Save(server.Url("missing"), path, nullptr, CancellationToken::None());
    }
    catch (const IOException& error)
    {
        message = error.what();
    }

    CHECK(Str::Contains(message, "HTTP 404"));
    File::Delete(path);
}

TEST_CASE("HttpDownloadTests.Cancels")
{
    TestServer server;
    auto path = TempFile();
    CancellationTokenSource cancel;
    cancel.Cancel();

    CHECK_THROWS_AS(HttpDownload::Save(server.Url("file"), path, nullptr, cancel.Token()), OperationCanceledException);
    File::Delete(path);
}
