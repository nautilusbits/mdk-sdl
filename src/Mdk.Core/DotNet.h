#pragma once

// .NET BCL semantics the port relies on: Math/MathF (IEEE min/max, banker's rounding),
// saturating float -> int casts (.NET 9+), the seeded System.Random, string helpers.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mdk
{
    using byte = std::uint8_t;
    using sbyte = std::int8_t;
    using ushort = std::uint16_t;
    using uint = std::uint32_t;
    using ulong = std::uint64_t;

    // ---- Exceptions (C# exception types the code throws or catches by kind) ----

    struct InvalidDataException : std::runtime_error { using std::runtime_error::runtime_error; };
    struct FileNotFoundException : std::runtime_error { using std::runtime_error::runtime_error; };
    struct DirectoryNotFoundException : std::runtime_error { using std::runtime_error::runtime_error; };
    struct IOException : std::runtime_error { using std::runtime_error::runtime_error; };
    struct ArgumentException : std::invalid_argument { using std::invalid_argument::invalid_argument; };
    struct InvalidOperationException : std::logic_error { using std::logic_error::logic_error; };
    struct NotSupportedException : std::logic_error { using std::logic_error::logic_error; };
    struct FormatException : std::runtime_error { using std::runtime_error::runtime_error; };
    struct OperationCanceledException : std::runtime_error { using std::runtime_error::runtime_error; };

    // ---- Casts: C# (int)f / (uint)f etc. saturate (NaN -> 0) on .NET 9+; C++ casts are UB out of range ----

    template <typename TInt, typename TFloat>
    constexpr TInt SatCast(TFloat f)
    {
        static_assert(std::is_floating_point_v<TFloat>);
        if (f != f)
        {
            return 0;
        }

        if (f <= static_cast<TFloat>(std::numeric_limits<TInt>::min()))
        {
            return std::numeric_limits<TInt>::min();
        }

        if (f >= static_cast<TFloat>(std::numeric_limits<TInt>::max()))
        {
            return std::numeric_limits<TInt>::max();
        }

        return static_cast<TInt>(f);
    }

    constexpr int ToInt(float f) { return SatCast<int>(f); }
    constexpr int ToInt(double f) { return SatCast<int>(f); }

    // ---- MathF / Math ----

    struct MathF
    {
        static constexpr float PI = 3.14159265f;
        static constexpr float Tau = 6.28318531f;
        static constexpr float E = 2.71828183f;

        static float Abs(float x) { return std::fabs(x); }
        static float Sqrt(float x) { return std::sqrt(x); }
        static float Sin(float x) { return std::sin(x); }
        static float Cos(float x) { return std::cos(x); }
        static float Tan(float x) { return std::tan(x); }
        static float Asin(float x) { return std::asin(x); }
        static float Acos(float x) { return std::acos(x); }
        static float Atan(float x) { return std::atan(x); }
        static float Atan2(float y, float x) { return std::atan2(y, x); }
        static float Pow(float x, float y) { return std::pow(x, y); }
        static float Exp(float x) { return std::exp(x); }
        static float Log(float x) { return std::log(x); }
        static float Log(float x, float b) { return std::log(x) / std::log(b); }
        static float Log2(float x) { return std::log2(x); }
        static float Log10(float x) { return std::log10(x); }
        static float Floor(float x) { return std::floor(x); }
        static float Ceiling(float x) { return std::ceil(x); }
        static float Truncate(float x) { return std::trunc(x); }
        static float IEEERemainder(float x, float y) { return std::remainder(x, y); }
        static float CopySign(float x, float y) { return std::copysign(x, y); }
        static float FusedMultiplyAdd(float x, float y, float z) { return std::fma(x, y, z); }
        static std::pair<float, float> SinCos(float x) { return {std::sin(x), std::cos(x)}; }

        // Round: to even by default (banker's), as C#.
        static float Round(float x) { return std::nearbyint(x); }
        static float RoundAwayFromZero(float x) { return std::round(x); }
        static float Round(float x, int digits)
        {
            float power = std::pow(10.0f, static_cast<float>(digits));
            return std::nearbyint(x * power) / power;
        }

        // IEEE 754:2019 maximum/minimum: NaN wins, +0 > -0.
        static float Max(float x, float y)
        {
            if (x != x) { return x; }
            if (y != y) { return y; }
            if (x == y) { return std::signbit(x) ? y : x; }
            return x > y ? x : y;
        }

        static float Min(float x, float y)
        {
            if (x != x) { return x; }
            if (y != y) { return y; }
            if (x == y) { return std::signbit(x) ? x : y; }
            return x < y ? x : y;
        }

        static float MaxMagnitude(float x, float y) { return std::fabs(x) >= std::fabs(y) ? x : y; }

        static int Sign(float x)
        {
            if (x != x)
            {
                throw std::domain_error("Function does not accept floating point Not-a-Number values.");
            }

            return x > 0 ? 1 : (x < 0 ? -1 : 0);
        }

        static float Clamp(float v, float min, float max) { return v < min ? min : (v > max ? max : v); }
        static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    };

    struct Math
    {
        static constexpr double PI = 3.14159265358979323846;
        static constexpr double Tau = 6.28318530717958647692;

        static int Abs(int x) { return x < 0 ? -x : x; }
        static long long Abs(long long x) { return x < 0 ? -x : x; }
        static double Abs(double x) { return std::fabs(x); }
        static float Abs(float x) { return std::fabs(x); }

        template <typename T> static constexpr T Max(T a, T b) { return a > b ? a : b; }
        template <typename T> static constexpr T Min(T a, T b) { return a < b ? a : b; }
        static double Max(double a, double b)
        {
            if (a != a) { return a; }
            if (b != b) { return b; }
            if (a == b) { return std::signbit(a) ? b : a; }
            return a > b ? a : b;
        }
        static double Min(double a, double b)
        {
            if (a != a) { return a; }
            if (b != b) { return b; }
            if (a == b) { return std::signbit(a) ? a : b; }
            return a < b ? a : b;
        }
        static float Max(float a, float b) { return MathF::Max(a, b); }
        static float Min(float a, float b) { return MathF::Min(a, b); }

        // C#'s Math.Clamp throws when min > max; callers never pass that, so no check.
        template <typename T> static constexpr T Clamp(T v, T min, T max) { return v < min ? min : (v > max ? max : v); }

        template <typename T> static int Sign(T x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }

        static double Sqrt(double x) { return std::sqrt(x); }
        static double Sin(double x) { return std::sin(x); }
        static double Cos(double x) { return std::cos(x); }
        static double Tan(double x) { return std::tan(x); }
        static double Atan2(double y, double x) { return std::atan2(y, x); }
        static double Pow(double x, double y) { return std::pow(x, y); }
        static double Exp(double x) { return std::exp(x); }
        static double Log(double x) { return std::log(x); }
        static double Log(double x, double b) { return std::log(x) / std::log(b); }
        static double Log2(double x) { return std::log2(x); }
        static double Log10(double x) { return std::log10(x); }
        static double Floor(double x) { return std::floor(x); }
        static double Ceiling(double x) { return std::ceil(x); }
        static double Truncate(double x) { return std::trunc(x); }
        static double Round(double x) { return std::nearbyint(x); }
        static double RoundAwayFromZero(double x) { return std::round(x); }
        static double Round(double x, int digits)
        {
            double power = std::pow(10.0, digits);
            return std::nearbyint(x * power) / power;
        }

        // Integer division helpers (C# int / is truncating, as C++).
        static int DivRem(int a, int b, int& rem) { rem = a % b; return a / b; }
    };

    // ---- System.Random ----

    // new Random(seed): .NET's Net5CompatSeedImpl (Knuth's subtractive generator), so seeded
    // sequences match the C# version. new Random(): seeded from the clock (any sequence will do).
    class Random
    {
    public:
        Random();
        explicit Random(int seed);

        int Next();
        int Next(int maxValue);
        int Next(int minValue, int maxValue);
        double NextDouble();
        float NextSingle();
        void NextBytes(std::span<byte> buffer);

    private:
        double Sample();
        int InternalSample();
        double GetSampleForLargeRange();

        int seedArray_[56] = {};
        int inext_ = 0;
        int inextp_ = 21;
    };

    // ---- Strings (C# string semantics on UTF-8 std::string; the game's text is ASCII) ----

    namespace Str
    {
        std::string ToUpper(std::string_view s);
        std::string ToLower(std::string_view s);
        bool EqualsIgnoreCase(std::string_view a, std::string_view b);
        int CompareOrdinalIgnoreCase(std::string_view a, std::string_view b);
        bool StartsWith(std::string_view s, std::string_view prefix);
        bool EndsWith(std::string_view s, std::string_view suffix);
        bool StartsWithIgnoreCase(std::string_view s, std::string_view prefix);
        bool EndsWithIgnoreCase(std::string_view s, std::string_view suffix);
        bool Contains(std::string_view s, std::string_view part);
        bool ContainsIgnoreCase(std::string_view s, std::string_view part);
        int IndexOf(std::string_view s, std::string_view part, size_t start = 0);
        int IndexOf(std::string_view s, char c, size_t start = 0);
        int LastIndexOf(std::string_view s, std::string_view part);
        int LastIndexOf(std::string_view s, char c);
        std::string Trim(std::string_view s);
        std::string TrimStart(std::string_view s);
        std::string TrimEnd(std::string_view s);
        std::string Trim(std::string_view s, std::string_view chars);
        std::string TrimEnd(std::string_view s, std::string_view chars);
        std::string TrimStart(std::string_view s, std::string_view chars);
        std::string Replace(std::string_view s, std::string_view from, std::string_view to);

        enum class SplitOptions { None, RemoveEmpty, Trim, TrimRemoveEmpty };
        std::vector<std::string> Split(std::string_view s, char separator, SplitOptions options = SplitOptions::None);
        std::vector<std::string> Split(std::string_view s, std::string_view separators, SplitOptions options = SplitOptions::None);
        std::vector<std::string> SplitCount(std::string_view s, char separator, int count, SplitOptions options = SplitOptions::None);

        std::string Join(std::string_view separator, const std::vector<std::string>& parts);
        std::string PadLeft(std::string_view s, size_t width, char c = ' ');
        std::string PadRight(std::string_view s, size_t width, char c = ' ');
        bool IsNullOrWhiteSpace(std::string_view s);
        bool IsWhiteSpace(char c);

        // int.TryParse / float.TryParse with the invariant culture (whitespace allowed around).
        bool TryParseInt(std::string_view s, int& value);
        bool TryParseLong(std::string_view s, long long& value);
        bool TryParseFloat(std::string_view s, float& value);
        bool TryParseDouble(std::string_view s, double& value);
        bool TryParseHex(std::string_view s, unsigned& value);
        int ParseInt(std::string_view s);
        float ParseFloat(std::string_view s);

        // float.ToString(CultureInfo.InvariantCulture): shortest round trip ("R"), C#'s spelling.
        std::string FromFloat(float value);
        std::string FromDouble(double value);
        // value.ToString("0.##") style: up to `decimals` decimals, trailing zeros dropped.
        std::string FromFloatTrim(double value, int decimals);
    }

    // C# string.GetHashCode isn't stable; code that needs a stable hash uses this (FNV-1a).
    std::uint32_t StableHash(std::string_view s);

    // Environment.TickCount64 / Stopwatch.
    long long TickCount64();
    double StopwatchSeconds();
    long long StopwatchTicks();
    constexpr long long StopwatchFrequency = 1'000'000'000LL;

    // Environment.GetEnvironmentVariable: empty optional when unset.
    std::optional<std::string> GetEnv(const char* name);
}
