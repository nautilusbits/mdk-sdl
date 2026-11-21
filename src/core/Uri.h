#pragma once

// System.Uri, the absolute URIs the downloads use:
//   https://user@Host.org:8443/a/b?c#d ─► Scheme https, Host host.org, Port 8443, PathAndQuery /a/b?c
//   file:///tmp/a%20b.zip, /tmp/a b.zip, C:\a.zip ─► IsFile, LocalPath /tmp/a b.zip (C:\a.zip)
// Not covered: IDN (IdnHost is Host), dot-segment and escape normalisation of the path.

#include <optional>
#include <string>
#include <string_view>

namespace Mdk
{
    class Uri
    {
    public:
        static constexpr std::string_view UriSchemeHttp = "http";
        static constexpr std::string_view UriSchemeHttps = "https";
        static constexpr std::string_view UriSchemeFile = "file";

        // new Uri(text): throws FormatException (UriFormatException) when it isn't an absolute URI.
        explicit Uri(std::string_view text);

        // Uri.TryCreate(text, UriKind.Absolute, out uri).
        static std::optional<Uri> TryCreate(std::string_view text);

        const std::string& Scheme() const { return _scheme; }
        const std::string& Host() const { return _host; }
        const std::string& IdnHost() const { return _host; }
        // The explicit port, else the scheme's (80, 443, -1 for file).
        int Port() const { return _port; }
        std::string PathAndQuery() const { return _path + _query; }
        bool IsFile() const { return _scheme == UriSchemeFile; }
        // A file URI's path, unescaped, in the OS's form.
        std::string LocalPath() const;
        std::string AbsoluteUri() const;
        std::string ToString() const { return AbsoluteUri(); }

    private:
        Uri() = default;

        static bool Parse(std::string_view text, Uri& uri);
        static bool ParseImplicitFile(std::string_view text, Uri& uri);
        static int DefaultPort(std::string_view scheme);

        std::string _scheme;
        // "//" followed the scheme.
        bool _authority = false;
        std::string _userInfo;
        std::string _host;
        int _port = -1;
        bool _explicitPort = false;
        // Escaped, as in the text ("/" when an http URI has none).
        std::string _path;
        // "?a=b" or empty; "#c" or empty.
        std::string _query;
        std::string _fragment;
    };
}
