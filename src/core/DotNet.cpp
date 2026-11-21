#include "DotNet.h"

#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>

namespace Mdk
{
    // ---- Random ----

    namespace
    {
        constexpr int MBig = std::numeric_limits<int>::max();
        constexpr int MSeed = 161803398;
    }

    Random::Random()
        : Random(static_cast<int>(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7fffffff))
    {
    }

    Random::Random(int seed)
    {
        int subtraction = seed == std::numeric_limits<int>::min() ? MBig : (seed < 0 ? -seed : seed);
        int mj = MSeed - subtraction;
        seedArray_[55] = mj;
        int mk = 1;

        int ii = 0;
        for (int i = 1; i < 55; i++)
        {
            if ((ii += 21) >= 55)
            {
                ii -= 55;
            }

            seedArray_[ii] = mk;
            mk = mj - mk;
            if (mk < 0)
            {
                mk += MBig;
            }

            mj = seedArray_[ii];
        }

        for (int k = 1; k < 5; k++)
        {
            for (int i = 1; i < 56; i++)
            {
                int n = i + 30;
                if (n >= 55)
                {
                    n -= 55;
                }

                seedArray_[i] -= seedArray_[1 + n];
                if (seedArray_[i] < 0)
                {
                    seedArray_[i] += MBig;
                }
            }
        }

        inext_ = 0;
        inextp_ = 21;
    }

    int Random::InternalSample()
    {
        int locINext = inext_;
        if (++locINext >= 56)
        {
            locINext = 1;
        }

        int locINextp = inextp_;
        if (++locINextp >= 56)
        {
            locINextp = 1;
        }

        int retVal = seedArray_[locINext] - seedArray_[locINextp];
        if (retVal == MBig)
        {
            retVal--;
        }

        if (retVal < 0)
        {
            retVal += MBig;
        }

        seedArray_[locINext] = retVal;
        inext_ = locINext;
        inextp_ = locINextp;
        return retVal;
    }

    double Random::Sample() { return InternalSample() * (1.0 / MBig); }

    double Random::GetSampleForLargeRange()
    {
        int result = InternalSample();
        if (InternalSample() % 2 == 0)
        {
            result = -result;
        }

        double d = result;
        d += MBig - 1;
        d /= 2.0 * MBig - 1;
        return d;
    }

    int Random::Next() { return InternalSample(); }

    int Random::Next(int maxValue) { return static_cast<int>(Sample() * maxValue); }

    int Random::Next(int minValue, int maxValue)
    {
        long long range = static_cast<long long>(maxValue) - minValue;
        if (range <= MBig)
        {
            return static_cast<int>(Sample() * static_cast<double>(range)) + minValue;
        }

        return static_cast<int>(static_cast<long long>(GetSampleForLargeRange() * static_cast<double>(range)) + minValue);
    }

    double Random::NextDouble() { return Sample(); }

    float Random::NextSingle()
    {
        while (true)
        {
            float f = static_cast<float>(Sample());
            if (f < 1.0f)
            {
                return f;
            }
        }
    }

    void Random::NextBytes(std::span<byte> buffer)
    {
        for (auto& b : buffer)
        {
            b = static_cast<byte>(InternalSample());
        }
    }

    // ---- Strings ----

    namespace Str
    {
        namespace
        {
            char Up(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }
            char Low(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
        }

        std::string ToUpper(std::string_view s)
        {
            std::string r(s);
            for (auto& c : r) { c = Up(c); }
            return r;
        }

        std::string ToLower(std::string_view s)
        {
            std::string r(s);
            for (auto& c : r) { c = Low(c); }
            return r;
        }

        bool EqualsIgnoreCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
            {
                return false;
            }

            for (size_t i = 0; i < a.size(); i++)
            {
                if (Up(a[i]) != Up(b[i]))
                {
                    return false;
                }
            }

            return true;
        }

        int CompareOrdinalIgnoreCase(std::string_view a, std::string_view b)
        {
            size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; i++)
            {
                int d = static_cast<unsigned char>(Up(a[i])) - static_cast<unsigned char>(Up(b[i]));
                if (d != 0)
                {
                    return d;
                }
            }

            return static_cast<int>(a.size()) - static_cast<int>(b.size());
        }

        bool StartsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
        bool EndsWith(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(s.size() - p.size()) == p; }
        bool StartsWithIgnoreCase(std::string_view s, std::string_view p) { return s.size() >= p.size() && EqualsIgnoreCase(s.substr(0, p.size()), p); }
        bool EndsWithIgnoreCase(std::string_view s, std::string_view p) { return s.size() >= p.size() && EqualsIgnoreCase(s.substr(s.size() - p.size()), p); }
        bool Contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }
        bool ContainsIgnoreCase(std::string_view s, std::string_view part) { return Contains(ToUpper(s), ToUpper(part)); }

        int IndexOf(std::string_view s, std::string_view part, size_t start)
        {
            auto i = s.find(part, start);
            return i == std::string_view::npos ? -1 : static_cast<int>(i);
        }

        int IndexOf(std::string_view s, char c, size_t start)
        {
            auto i = s.find(c, start);
            return i == std::string_view::npos ? -1 : static_cast<int>(i);
        }

        int LastIndexOf(std::string_view s, std::string_view part)
        {
            auto i = s.rfind(part);
            return i == std::string_view::npos ? -1 : static_cast<int>(i);
        }

        int LastIndexOf(std::string_view s, char c)
        {
            auto i = s.rfind(c);
            return i == std::string_view::npos ? -1 : static_cast<int>(i);
        }

        bool IsWhiteSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

        std::string Trim(std::string_view s) { return TrimEnd(TrimStart(s)); }

        std::string TrimStart(std::string_view s)
        {
            size_t i = 0;
            while (i < s.size() && IsWhiteSpace(s[i])) { i++; }
            return std::string(s.substr(i));
        }

        std::string TrimEnd(std::string_view s)
        {
            size_t n = s.size();
            while (n > 0 && IsWhiteSpace(s[n - 1])) { n--; }
            return std::string(s.substr(0, n));
        }

        std::string TrimStart(std::string_view s, std::string_view chars)
        {
            size_t i = 0;
            while (i < s.size() && chars.find(s[i]) != std::string_view::npos) { i++; }
            return std::string(s.substr(i));
        }

        std::string TrimEnd(std::string_view s, std::string_view chars)
        {
            size_t n = s.size();
            while (n > 0 && chars.find(s[n - 1]) != std::string_view::npos) { n--; }
            return std::string(s.substr(0, n));
        }

        std::string Trim(std::string_view s, std::string_view chars) { return TrimEnd(TrimStart(s, chars), chars); }

        std::string Replace(std::string_view s, std::string_view from, std::string_view to)
        {
            if (from.empty())
            {
                return std::string(s);
            }

            std::string r;
            size_t pos = 0;
            while (true)
            {
                size_t i = s.find(from, pos);
                if (i == std::string_view::npos)
                {
                    r.append(s.substr(pos));
                    return r;
                }

                r.append(s.substr(pos, i - pos));
                r.append(to);
                pos = i + from.size();
            }
        }

        namespace
        {
            void AddPart(std::vector<std::string>& parts, std::string_view part, SplitOptions options)
            {
                std::string p(part);
                if (options == SplitOptions::Trim || options == SplitOptions::TrimRemoveEmpty)
                {
                    p = Trim(p);
                }

                if ((options == SplitOptions::RemoveEmpty || options == SplitOptions::TrimRemoveEmpty) && p.empty())
                {
                    return;
                }

                parts.push_back(std::move(p));
            }
        }

        std::vector<std::string> Split(std::string_view s, std::string_view separators, SplitOptions options)
        {
            std::vector<std::string> parts;
            size_t start = 0;
            for (size_t i = 0; i <= s.size(); i++)
            {
                if (i < s.size() && separators.find(s[i]) == std::string_view::npos)
                {
                    continue;
                }

                AddPart(parts, s.substr(start, i - start), options);
                start = i + 1;
            }

            return parts;
        }

        std::vector<std::string> Split(std::string_view s, char separator, SplitOptions options)
        {
            return Split(s, std::string_view(&separator, 1), options);
        }

        std::vector<std::string> SplitCount(std::string_view s, char separator, int count, SplitOptions options)
        {
            std::vector<std::string> parts;
            size_t start = 0;
            while (static_cast<int>(parts.size()) < count - 1)
            {
                size_t i = s.find(separator, start);
                if (i == std::string_view::npos)
                {
                    break;
                }

                size_t before = parts.size();
                AddPart(parts, s.substr(start, i - start), options);
                start = i + 1;
                (void)before;
            }

            AddPart(parts, s.substr(start), options);
            return parts;
        }

        std::string Join(std::string_view separator, const std::vector<std::string>& parts)
        {
            std::string r;
            for (size_t i = 0; i < parts.size(); i++)
            {
                if (i > 0)
                {
                    r.append(separator);
                }

                r.append(parts[i]);
            }

            return r;
        }

        std::string PadLeft(std::string_view s, size_t width, char c)
        {
            if (s.size() >= width)
            {
                return std::string(s);
            }

            return std::string(width - s.size(), c) + std::string(s);
        }

        std::string PadRight(std::string_view s, size_t width, char c)
        {
            if (s.size() >= width)
            {
                return std::string(s);
            }

            return std::string(s) + std::string(width - s.size(), c);
        }

        bool IsNullOrWhiteSpace(std::string_view s)
        {
            for (char c : s)
            {
                if (!IsWhiteSpace(c))
                {
                    return false;
                }
            }

            return true;
        }

        namespace
        {
            template <typename T>
            bool ParseNumber(std::string_view s, T& value)
            {
                std::string t = Trim(s);
                if (t.empty())
                {
                    return false;
                }

                const char* first = t.data();
                const char* last = t.data() + t.size();
                if (*first == '+')
                {
                    first++;
                }

                if constexpr (std::is_floating_point_v<T>)
                {
                    // Apple's libc++ has no floating from_chars; strtod in the "C" locale (the
                    // program never changes it) parses the invariant culture.
                    std::string copy(first, last);
                    char* end = nullptr;
                    value = static_cast<T>(std::strtod(copy.c_str(), &end));
                    return end == copy.c_str() + copy.size() && !copy.empty() && copy[0] != ' ';
                }
                else
                {
                    auto [ptr, ec] = std::from_chars(first, last, value);
                    return ec == std::errc() && ptr == last;
                }
            }
        }

        bool TryParseInt(std::string_view s, int& value) { return ParseNumber(s, value); }
        bool TryParseLong(std::string_view s, long long& value) { return ParseNumber(s, value); }

        bool TryParseFloat(std::string_view s, float& value)
        {
            double d;
            if (!TryParseDouble(s, d))
            {
                return false;
            }

            value = static_cast<float>(d);
            return true;
        }

        bool TryParseDouble(std::string_view s, double& value)
        {
            std::string t = Trim(s);
            if (EqualsIgnoreCase(t, "NaN"))
            {
                value = std::numeric_limits<double>::quiet_NaN();
                return true;
            }

            if (EqualsIgnoreCase(t, "Infinity") || t == "∞")
            {
                value = std::numeric_limits<double>::infinity();
                return true;
            }

            if (EqualsIgnoreCase(t, "-Infinity") || t == "-∞")
            {
                value = -std::numeric_limits<double>::infinity();
                return true;
            }

            return ParseNumber(t, value);
        }

        bool TryParseHex(std::string_view s, unsigned& value)
        {
            std::string t = Trim(s);
            if (t.empty())
            {
                return false;
            }

            auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), value, 16);
            return ec == std::errc() && ptr == t.data() + t.size();
        }

        int ParseInt(std::string_view s)
        {
            int v;
            if (!TryParseInt(s, v))
            {
                throw FormatException(std::format("Not an integer: '{}'", s));
            }

            return v;
        }

        float ParseFloat(std::string_view s)
        {
            float v;
            if (!TryParseFloat(s, v))
            {
                throw FormatException(std::format("Not a number: '{}'", s));
            }

            return v;
        }

        namespace
        {
            // Shortest round-trip digits, laid out as .NET does: fixed for exponents -4 up to
            // 8 (float) or 15 (double), else "1.5E+20" / "1E-05".
            template <typename T>
            std::string ShortestDotNet(T value)
            {
                if (value != value)
                {
                    return "NaN";
                }

                if (std::isinf(value))
                {
                    return value > 0 ? "Infinity" : "-Infinity";
                }

                char buf[64];
                auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::scientific);
                std::string sci(buf, end);

                auto e = sci.find('e');
                std::string mantissa = sci.substr(0, e);
                int exponent = std::stoi(sci.substr(e + 1));

                bool negative = mantissa[0] == '-';
                if (negative)
                {
                    mantissa.erase(0, 1);
                }

                std::string digits;
                for (char c : mantissa)
                {
                    if (c != '.')
                    {
                        digits += c;
                    }
                }

                std::string out;
                constexpr int FixedLimit = sizeof(T) == 4 ? 9 : 16;
                if (exponent >= -4 && exponent < FixedLimit)
                {
                    if (exponent >= 0)
                    {
                        if (static_cast<int>(digits.size()) <= exponent + 1)
                        {
                            out = digits + std::string(exponent + 1 - digits.size(), '0');
                        }
                        else
                        {
                            out = digits.substr(0, exponent + 1) + "." + digits.substr(exponent + 1);
                        }
                    }
                    else
                    {
                        out = "0." + std::string(-exponent - 1, '0') + digits;
                    }
                }
                else
                {
                    out = digits.substr(0, 1);
                    if (digits.size() > 1)
                    {
                        out += "." + digits.substr(1);
                    }

                    int absExp = exponent < 0 ? -exponent : exponent;
                    out += std::format("E{}{:02}", exponent < 0 ? '-' : '+', absExp);
                }

                return negative ? "-" + out : out;
            }
        }

        std::string FromFloat(float value) { return ShortestDotNet(value); }
        std::string FromDouble(double value) { return ShortestDotNet(value); }

        std::string FromFloatTrim(double value, int decimals)
        {
            std::string s = std::format("{:.{}f}", value, decimals);
            if (s.find('.') != std::string::npos)
            {
                while (!s.empty() && s.back() == '0') { s.pop_back(); }
                if (!s.empty() && s.back() == '.') { s.pop_back(); }
            }

            if (s == "-0")
            {
                s = "0";
            }

            return s;
        }
    }

    std::uint32_t StableHash(std::string_view s)
    {
        std::uint32_t h = 2166136261u;
        for (char c : s)
        {
            h ^= static_cast<unsigned char>(c);
            h *= 16777619u;
        }

        return h;
    }

    long long TickCount64()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    long long StopwatchTicks()
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    double StopwatchSeconds() { return StopwatchTicks() / static_cast<double>(StopwatchFrequency); }

    std::optional<std::string> GetEnv(const char* name)
    {
        const char* v = std::getenv(name);
        if (v == nullptr)
        {
            return std::nullopt;
        }

        return std::string(v);
    }
}
