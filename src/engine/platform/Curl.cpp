#include "engine/platform/Curl.h"

#include <array>
#include <cstdint>
#include <exception>
#include <format>
#include <span>

#include <SDL3/SDL_loadso.h>

#include "core/DotNet.h"
#include "engine/platform/HttpDownload.h"

namespace Mdk::Engine::Platform
{
    namespace
    {
        constexpr std::array<const char*, 4> Libraries = {"libcurl.so.4", "libcurl.so", "libcurl.4.dylib", "/usr/lib/libcurl.4.dylib"};

        std::string OptionName(CurlOption option)
        {
            switch (option)
            {
                case CurlOption::WriteData: return "WriteData";
                case CurlOption::Url: return "Url";
                case CurlOption::UserAgent: return "UserAgent";
                case CurlOption::NoProgress: return "NoProgress";
                case CurlOption::FailOnError: return "FailOnError";
                case CurlOption::FollowLocation: return "FollowLocation";
                case CurlOption::WriteFunction: return "WriteFunction";
                case CurlOption::XferInfoFunction: return "XferInfoFunction";
                case CurlOption::XferInfoData: return "XferInfoData";
            }

            return std::to_string(static_cast<int>(option));
        }
    }

    // What a download's callbacks share with it.
    struct Curl::Transfer
    {
        std::ostream& To;
        const std::function<void(long long, long long)>& Progress;
        const CancellationToken& Cancel;
        std::exception_ptr Error;
    };

    // libcurl's functions. setopt and getinfo are C variadic functions: called through variadic
    // function pointers, the compiler passes the value as each ABI wants (Apple arm64: on the stack).
    class Curl::Api
    {
    public:
        explicit Api(SDL_SharedObject* library)
            : _init(reinterpret_cast<void* (*)()>(Export(library, "curl_easy_init"))),
              _setopt(reinterpret_cast<int (*)(void*, int, ...)>(Export(library, "curl_easy_setopt"))),
              _getinfo(reinterpret_cast<int (*)(void*, int, ...)>(Export(library, "curl_easy_getinfo"))),
              _perform(reinterpret_cast<int (*)(void*)>(Export(library, "curl_easy_perform"))),
              _strerror(reinterpret_cast<const char* (*)(int)>(Export(library, "curl_easy_strerror"))),
              _cleanup(reinterpret_cast<void (*)(void*)>(Export(library, "curl_easy_cleanup")))
        {
        }

        void* Init() const { return _init(); }

        int Perform(void* easy) const { return _perform(easy); }

        void Cleanup(void* easy) const { _cleanup(easy); }

        std::string Error(int code) const
        {
            auto text = _strerror(code);
            return text != nullptr ? std::string(text) : std::format("curl error {}", code);
        }

        // A number option (C long) or a pointer (text, callback, user data) as an intptr_t.
        void Set(void* easy, CurlOption option, std::intptr_t value) const
        {
            auto code = _setopt(easy, static_cast<int>(option), value);
            if (code != Ok)
            {
                throw IOException(std::format("curl_easy_setopt({}): {}", OptionName(option), Error(code)));
            }
        }

        void GetInfo(void* easy, int info, void* value) const { _getinfo(easy, info, value); }

    private:
        static SDL_FunctionPointer Export(SDL_SharedObject* library, const char* name)
        {
            auto function = SDL_LoadFunction(library, name);
            if (function == nullptr)
            {
                throw IOException(std::format("libcurl: {} not found", name));
            }

            return function;
        }

        void* (*_init)();
        int (*_setopt)(void*, int, ...);
        int (*_getinfo)(void*, int, ...);
        int (*_perform)(void*);
        const char* (*_strerror)(int);
        void (*_cleanup)(void*);
    };

    std::vector<CurlSetting> Curl::Settings(const Uri& uri)
    {
        return {
            {CurlOption::Url, 0, uri.AbsoluteUri()},
            {CurlOption::UserAgent, 0, HttpDownload::UserAgent},
            {CurlOption::FollowLocation, 1, std::nullopt},
            // A 4xx or 5xx fails instead of saving the error page.
            {CurlOption::FailOnError, 1, std::nullopt},
            {CurlOption::NoProgress, 0, std::nullopt},
        };
    }

    std::string Curl::Describe(int code, long long status, const std::string& text)
    {
        return code == HttpReturnedError ? HttpDownload::StatusText(status) : text;
    }

    void Curl::Get(const Uri& uri, std::ostream& to, const std::function<void(long long, long long)>& progress, const CancellationToken& cancel)
    {
        auto& api = Loaded();
        auto easy = api.Init();
        if (easy == nullptr)
        {
            throw HttpDownload::Failure(uri, "curl_easy_init failed");
        }

        // The texts stay alive (libcurl copies them anyway) until the transfer is over.
        Transfer transfer{to, progress, cancel, nullptr};
        auto settings = Settings(uri);
        try
        {
            for (auto& setting : settings)
            {
                auto value = !setting.Text ? static_cast<std::intptr_t>(setting.Number) : reinterpret_cast<std::intptr_t>(setting.Text->c_str());
                api.Set(easy, setting.Option, value);
            }

            api.Set(easy, CurlOption::WriteFunction, reinterpret_cast<std::intptr_t>(&OnWrite));
            api.Set(easy, CurlOption::WriteData, reinterpret_cast<std::intptr_t>(&transfer));
            api.Set(easy, CurlOption::XferInfoFunction, reinterpret_cast<std::intptr_t>(&OnProgress));
            api.Set(easy, CurlOption::XferInfoData, reinterpret_cast<std::intptr_t>(&transfer));

            auto code = api.Perform(easy);
            Finish(uri, api, easy, code, transfer);
        }
        catch (...)
        {
            api.Cleanup(easy);
            throw;
        }

        api.Cleanup(easy);
    }

    // Throws for a failed transfer: the callbacks' own error first, then curl's.
    void Curl::Finish(const Uri& uri, const Api& api, void* easy, int code, const Transfer& transfer)
    {
        if (transfer.Error != nullptr)
        {
            std::rethrow_exception(transfer.Error);
        }

        if (code == AbortedByCallback)
        {
            transfer.Cancel.ThrowIfCancellationRequested();
        }

        if (code == Ok)
        {
            return;
        }

        long status = 0;
        api.GetInfo(easy, ResponseCode, &status);
        throw HttpDownload::Failure(uri, Describe(code, status, api.Error(code)));
    }

    // curl's body callback: the bytes go to the file. Fewer bytes than given abort.
    size_t Curl::OnWrite(char* data, size_t size, size_t count, void* user)
    {
        auto& transfer = *static_cast<Transfer*>(user);
        auto length = size * count;
        try
        {
            transfer.To.write(data, static_cast<std::streamsize>(length));
            if (!transfer.To)
            {
                throw IOException("write failed");
            }

            return length;
        }
        catch (...)
        {
            transfer.Error = std::current_exception();
            return 0;
        }
    }

    // curl's progress callback: reports it, aborts when cancelled.
    int Curl::OnProgress(void* user, long long total, long long done, long long upTotal, long long upDone)
    {
        (void)upTotal;
        (void)upDone;
        auto& transfer = *static_cast<Transfer*>(user);
        try
        {
            if (done > 0 && transfer.Progress)
            {
                transfer.Progress(done, total);
            }

            return transfer.Cancel.IsCancellationRequested() ? CallbackAbort : CallbackContinue;
        }
        catch (...)
        {
            transfer.Error = std::current_exception();
            return CallbackAbort;
        }
    }

    const Curl::Api& Curl::Loaded()
    {
        static const Api api = Load();
        return api;
    }

    Curl::Api Curl::Load()
    {
        for (auto name : Libraries)
        {
            auto library = SDL_LoadObject(name);
            if (library != nullptr)
            {
                return Api(library);
            }
        }

        throw IOException("libcurl not found: install libcurl (e.g. apt install libcurl4)");
    }
}
