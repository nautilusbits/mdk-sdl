#include "core/Uri.h"

#include <cctype>
#include <format>

#include "core/DotNet.h"

namespace Mdk
{
    namespace
    {
        constexpr int HttpPort = 80;
        constexpr int HttpsPort = 443;
        constexpr int FtpPort = 21;
        constexpr int MaxPort = 65535;
        constexpr int NoPort = -1;
        // "C:" is a drive, not a scheme.
        constexpr size_t MinSchemeLength = 2;

        bool IsSchemeChar(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.';
        }

        bool IsDrive(std::string_view text)
        {
            return text.size() >= 3 && std::isalpha(static_cast<unsigned char>(text[0])) && text[1] == ':' && (text[2] == '/' || text[2] == '\\');
        }

        // Escapes what may not appear in a URI as is (space ─► %20, UTF-8 bytes); '\' ─► '/'.
        std::string Escape(std::string_view text, bool slashes)
        {
            static constexpr char Digits[] = "0123456789ABCDEF";
            static constexpr std::string_view Unsafe = " \"<>`{}|^";
            std::string escaped;
            for (char c : text)
            {
                auto b = static_cast<unsigned char>(c);
                if (slashes && c == '\\')
                {
                    escaped += '/';
                    continue;
                }

                if (b < 0x20 || b >= 0x7F || Unsafe.find(c) != std::string_view::npos)
                {
                    escaped += '%';
                    escaped += Digits[b >> 4];
                    escaped += Digits[b & 0xF];
                    continue;
                }

                escaped += c;
            }

            return escaped;
        }

        int HexValue(char c)
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }

            auto lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower >= 'a' && lower <= 'f')
            {
                return lower - 'a' + 10;
            }

            return -1;
        }

        std::string Unescape(std::string_view text)
        {
            std::string plain;
            for (size_t i = 0; i < text.size(); i++)
            {
                if (text[i] != '%' || i + 2 >= text.size() || HexValue(text[i + 1]) < 0 || HexValue(text[i + 2]) < 0)
                {
                    plain += text[i];
                    continue;
                }

                plain += static_cast<char>(HexValue(text[i + 1]) * 16 + HexValue(text[i + 2]));
                i += 2;
            }

            return plain;
        }
    }

    Uri::Uri(std::string_view text)
    {
        if (!Parse(text, *this))
        {
            throw FormatException(std::format("Invalid URI: {}", text));
        }
    }

    std::optional<Uri> Uri::TryCreate(std::string_view text)
    {
        Uri uri;
        if (!Parse(text, uri))
        {
            return std::nullopt;
        }

        return uri;
    }

    int Uri::DefaultPort(std::string_view scheme)
    {
        if (scheme == UriSchemeHttp)
        {
            return HttpPort;
        }

        if (scheme == UriSchemeHttps)
        {
            return HttpsPort;
        }

        if (scheme == "ftp")
        {
            return FtpPort;
        }

        return NoPort;
    }

    // A local path taken as a file URI: /tmp/a (not on Windows), C:\a, C:/a.
    bool Uri::ParseImplicitFile(std::string_view text, Uri& uri)
    {
#if defined(_WIN32)
        auto rooted = false;
#else
        auto rooted = !text.empty() && text[0] == '/';
#endif
        auto drive = IsDrive(text);
        if (!rooted && !drive)
        {
            return false;
        }

        uri._scheme = UriSchemeFile;
        uri._authority = true;
        uri._port = NoPort;
        uri._path = (drive ? "/" : "") + Escape(text, true);
        return true;
    }

    bool Uri::Parse(std::string_view text, Uri& uri)
    {
        auto trimmed = Str::Trim(text);
        std::string_view s = trimmed;
        if (ParseImplicitFile(s, uri))
        {
            return true;
        }

        // scheme ":"
        auto colon = s.find(':');
        if (colon == std::string_view::npos || colon < MinSchemeLength || !std::isalpha(static_cast<unsigned char>(s[0])))
        {
            return false;
        }

        for (size_t i = 0; i < colon; i++)
        {
            if (!IsSchemeChar(s[i]))
            {
                return false;
            }
        }

        uri._scheme = Str::ToLower(s.substr(0, colon));
        auto rest = s.substr(colon + 1);
        auto web = uri._scheme == UriSchemeHttp || uri._scheme == UriSchemeHttps;

        // "//" authority: [user@]host[:port]
        uri._authority = rest.starts_with("//") || (web && rest.starts_with("\\\\"));
        if (!uri._authority && web)
        {
            return false;
        }

        if (uri._authority)
        {
            rest.remove_prefix(2);
            auto end = rest.find_first_of("/\\?#");
            auto authority = rest.substr(0, end);
            rest = end == std::string_view::npos ? std::string_view() : rest.substr(end);

            auto at = authority.rfind('@');
            if (at != std::string_view::npos)
            {
                uri._userInfo = std::string(authority.substr(0, at));
                authority.remove_prefix(at + 1);
            }

            auto portColon = authority.starts_with('[') ? authority.find(':', authority.find(']')) : authority.find(':');
            uri._host = Str::ToLower(authority.substr(0, portColon));
            uri._port = DefaultPort(uri._scheme);
            if (portColon != std::string_view::npos)
            {
                auto digits = authority.substr(portColon + 1);
                int port = 0;
                if (!Str::TryParseInt(digits, port) || port < 0 || port > MaxPort || digits.empty() || !std::isdigit(static_cast<unsigned char>(digits[0])))
                {
                    return false;
                }

                uri._port = port;
                uri._explicitPort = true;
            }

            if (web && uri._host.empty())
            {
                return false;
            }
        }

        // path ["?" query] ["#" fragment]
        auto hash = rest.find('#');
        if (hash != std::string_view::npos)
        {
            uri._fragment = Escape(rest.substr(hash), false);
            rest = rest.substr(0, hash);
        }

        auto question = rest.find('?');
        if (question != std::string_view::npos)
        {
            uri._query = Escape(rest.substr(question), false);
            rest = rest.substr(0, question);
        }

        uri._path = Escape(rest, web || uri.IsFile());
        if (uri._path.empty() && uri._authority)
        {
            uri._path = "/";
        }

        return true;
    }

    std::string Uri::LocalPath() const
    {
        auto path = Unescape(_path);
        if (!IsFile())
        {
            return path;
        }

        // /C:/a ─► C:/a
        if (path.size() >= 3 && path[0] == '/' && IsDrive(std::string_view(path).substr(1)))
        {
            path.erase(0, 1);
        }

        if (!_host.empty())
        {
            path = "//" + _host + path;
        }

#if defined(_WIN32)
        path = Str::Replace(path, "/", "\\");
#endif
        return path;
    }

    std::string Uri::AbsoluteUri() const
    {
        auto text = _scheme + ":";
        if (_authority)
        {
            text += "//";
            if (!_userInfo.empty())
            {
                text += _userInfo + "@";
            }

            text += _host;
            if (_explicitPort && _port != DefaultPort(_scheme))
            {
                text += std::format(":{}", _port);
            }
        }

        return text + _path + _query + _fragment;
    }
}
