// The microphone's PCM conversion: interleaved stereo int16 into mono floats in [-1, 1].
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "boards/korvo1/pcm.hpp"

#include <array>
#include <cstdint>

namespace {

float convert_one(std::int16_t l, std::int16_t r)
{
    std::array<std::int16_t, 2> in{l, r};
    std::array<float, 1> out{};
    korvo1::stereo_i16_to_mono(in, out);
    return out[0];
}

} // namespace

TEST_CASE("full scale maps to +-1")
{
    CHECK(convert_one(32767, 32767) == doctest::Approx(1.0).epsilon(1e-4));
    CHECK(convert_one(-32768, -32768) == -1.0f);
}

TEST_CASE("opposite channels cancel")
{
    CHECK(convert_one(1000, -1000) == 0.0f);
}

TEST_CASE("an odd-length input converts only the whole frames")
{
    std::array<std::int16_t, 5> in{32767, 32767, -32768, -32768, 12345};
    std::array<float, 3> out{7.f, 7.f, 7.f};
    korvo1::stereo_i16_to_mono(in, out);
    CHECK(out[0] == doctest::Approx(1.0).epsilon(1e-4));
    CHECK(out[1] == -1.0f);
    CHECK(out[2] == 7.f);
}
