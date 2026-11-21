#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "core/DotNet.h"
#include "core/Numerics.h"
#include "core/Sha256.h"

using namespace Mdk;

// Expected values printed by .NET 10.

TEST_CASE("Seeded Random matches .NET")
{
    Random r(42);
    CHECK(r.Next(100) == 66);
    CHECK(r.Next(100) == 14);
    CHECK(r.Next(5, 50) == 10);
    CHECK(r.NextSingle() == 0.52276427f);
    CHECK(r.Next() == 361709742);
}

TEST_CASE("Matrices match .NET")
{
    auto m = Matrix4x4::CreateLookAt({1, 2, 3}, {4, -5, 6}, Vector3::UnitZ()) * Matrix4x4::CreatePerspectiveFieldOfView(1.1f, 1.5f, 0.1f, 100.0f);
    Matrix4x4 inv;
    Matrix4x4::Invert(m, inv);
    CHECK(m.M11 == doctest::Approx(-0.9994424f).epsilon(1e-6));
    CHECK(m.M23 == doctest::Approx(-0.8560422f).epsilon(1e-6));
    CHECK(m.M43 == doctest::Approx(0.14448336f).epsilon(1e-6));
    CHECK(inv.M11 == doctest::Approx(-0.8452989f).epsilon(1e-6));
    CHECK(inv.M42 == doctest::Approx(19.144815f).epsilon(1e-5)); // .NET inverts with SIMD on x64: last-bit differences

    auto q = Quaternion::Slerp(Quaternion::CreateFromYawPitchRoll(0.3f, 0.2f, 0.1f), Quaternion::Identity(), 0.3f);
    CHECK(q.X == doctest::Approx(0.0744257f).epsilon(1e-6));
    CHECK(q.W == doctest::Approx(0.9918287f).epsilon(1e-6));
    CHECK(Vector3::Lerp({1, 2, 3}, {7, 1, 0}, 0.37f).X == doctest::Approx(3.2200003f));
}

TEST_CASE("Float formatting matches .NET")
{
    CHECK(MathF::Round(2.5f) == 2.0f);
    CHECK(Str::FromFloat(1e9f) == "1E+09");
    CHECK(Str::FromFloat(16777216.0f) == "16777216");
    CHECK(Str::FromFloat(0.0001f) == "0.0001");
    CHECK(Str::FromFloat(0.00001f) == "1E-05");
    CHECK(Str::FromFloat(1.5e20f) == "1.5E+20");
    CHECK(Str::FromFloat(3.4028235e38f) == "3.4028235E+38");
    CHECK(Str::FromFloat(1e8f) == "100000000");
    CHECK(Str::FromFloat(123456789.0f) == "123456790");
    CHECK(Str::FromFloat(1.5e-5f) == "1.5E-05");
    CHECK(Str::FromFloat(-0.0f) == "-0");
    CHECK(Str::FromDouble(1e15) == "1000000000000000");
    CHECK(Str::FromDouble(123456789012345.0) == "123456789012345");
    CHECK(ToInt(3e10f) == 2147483647);
    CHECK(ToInt(std::nanf("")) == 0);
}

TEST_CASE("Sha256 matches FIPS 180-4 vectors")
{
    auto hex = [](std::string_view s, size_t chunk)
    {
        Sha256 hash;
        for (size_t i = 0; i < s.size(); i += chunk)
        {
            auto part = s.substr(i, chunk);
            hash.AppendData(std::span(reinterpret_cast<const byte*>(part.data()), part.size()));
        }

        return Convert::ToHexStringLower(hash.GetHashAndReset());
    };

    CHECK(hex("", 1) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hex("abc", 1) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for (size_t chunk : {1, 3, 55, 56, 64, 1000})
    {
        CHECK(hex(two, chunk) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }

    const std::string million(1'000'000, 'a');
    for (size_t chunk : {1, 63, 64, 4096, 1'000'000})
    {
        CHECK(hex(million, chunk) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }
}
