#pragma once

#include <functional>
#include <ostream>
#include <string>

#include "Mdk.Core/Cancellation.h"
#include "Mdk.Core/Uri.h"

namespace Mdk::Engine::Platform
{
    // Windows' HTTP library (winhttp.dll), synchronous:
    //   Open ─► Connect(host, port) ─► OpenRequest(GET path, TLS?) ─► Send ─► ReceiveResponse
    //        ─► QueryHeaders(status, length) ─► ReadData … 0 bytes
    // Redirects are followed by default; the system's proxy settings apply.
    // Off Windows, Get throws PlatformNotSupportedException (the backend choice never calls it there).
    class WinHttp
    {
    public:
        // Writes the body of uri (after redirects) into to.
        static void Get(const Uri& uri, std::ostream& to, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel);

        // A WinHTTP error's name: 12007 ─► "host not found".
        static std::string Describe(int error);

        // A WinHTTP handle (session, connection, request), closed once.
        class Handle
        {
        public:
            explicit Handle(void* handle) : _handle(handle) {}
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
            ~Handle();

            bool IsInvalid() const { return _handle == nullptr; }
            void* Get() const { return _handle; }

        private:
            void* _handle;
        };

    private:
        static constexpr unsigned AccessAutomaticProxy = 4;
        static constexpr unsigned FlagSecure = 0x00800000;
        static constexpr unsigned QueryStatusCode = 19;
        static constexpr unsigned QueryContentLength = 5;
        static constexpr unsigned QueryFlagNumber = 0x20000000;
        static constexpr unsigned QueryFlagNumber64 = 0x08000000;
        static constexpr int StatusOk = 200;
        static constexpr int ReadBuffer = 1 << 16;

        // WinHTTP's errors (winhttp.h), named in failures.
        static constexpr int ErrorTimeout = 12002;
        static constexpr int ErrorNameNotResolved = 12007;
        static constexpr int ErrorCannotConnect = 12029;
        static constexpr int ErrorConnectionLost = 12030;
        static constexpr int ErrorSecureFailure = 12175;

        static void Read(const Uri& uri, const Handle& request, std::ostream& to, long long total, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel);
        static void* Check(const Uri& uri, void* handle);
        static void Check(const Uri& uri, bool done);
    };
}
