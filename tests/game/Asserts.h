#pragma once

// xUnit's float asserts: Assert.Equal(expected, actual, precision) rounds both to that many
// decimals (to even); Assert.Equal(expected, actual, tolerance) allows that difference.

#include <cmath>

#include <doctest.h>

namespace Mdk::Tests
{
    inline double RoundTo(double value, int decimals)
    {
        double power = std::pow(10.0, decimals);
        return std::nearbyint(value * power) / power;
    }
}

#define CHECK_PRECISION(expected, actual, decimals) \
    CHECK(::Mdk::Tests::RoundTo((expected), (decimals)) == ::Mdk::Tests::RoundTo((actual), (decimals)))

#define CHECK_NEAR(expected, actual, tolerance) CHECK(std::fabs((double)(expected) - (double)(actual)) <= (tolerance))
