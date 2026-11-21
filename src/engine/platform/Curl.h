#pragma once

#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "core/Cancellation.h"
#include "core/Uri.h"

namespace Mdk::Engine::Platform
{
    // libcurl's options used (curl.h: type base + number).
    enum class CurlOption
    {
        WriteData = 10001,
        Url = 10002,
        UserAgent = 10018,
        NoProgress = 43,
        FailOnError = 45,
        FollowLocation = 52,
        WriteFunction = 20011,
        XferInfoFunction = 20219,
        XferInfoData = 10057,
    };

    // One curl_easy_setopt: a number, or a text when Text is set.
    struct CurlSetting
    {
        CurlOption Option;
        long long Number = 0;
        std::optional<std::string> Text;

        bool operator==(const CurlSetting&) const = default;
    };

    // libcurl, loaded at run time (Linux and macOS ship it):
    //   easy_init ─► setopt(URL, FOLLOWLOCATION, FAILONERROR, USERAGENT, write ─► file, progress)
    //             ─► easy_perform ─► getinfo(RESPONSE_CODE) on failure ─► easy_cleanup
    class Curl
    {
    public:
        // The options of a download of uri, before the callbacks.
        static std::vector<CurlSetting> Settings(const Uri& uri);

        // A failure's text: an HTTP error by its status (22, 404 ─► "HTTP 404"), else curl's.
        static std::string Describe(int code, long long status, const std::string& text);

        // Writes the body of uri (after redirects) into to.
        static void Get(const Uri& uri, std::ostream& to, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel);

    private:
        class Api;
        struct Transfer;

        static constexpr int Ok = 0;
        static constexpr int HttpReturnedError = 22;
        static constexpr int AbortedByCallback = 42;
        static constexpr int ResponseCode = 0x200000 + 2;
        static constexpr int CallbackContinue = 0;
        static constexpr int CallbackAbort = 1;

        static void Finish(const Uri& uri, const Api& api, void* easy, int code, const Transfer& transfer);
        static size_t OnWrite(char* data, size_t size, size_t count, void* user);
        static int OnProgress(void* user, long long total, long long done, long long upTotal, long long upDone);
        static const Api& Loaded();
        static Api Load();
    };
}
