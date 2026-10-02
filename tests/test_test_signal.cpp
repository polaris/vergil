#include "test_signal.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

constexpr unsigned int kRate = 48000;

std::vector<std::int16_t> generate(std::size_t frames, std::size_t channels, std::size_t chunk) {
    TestSignal signal;
    signal.configure(kRate, channels);
    std::vector<std::int16_t> out(frames * channels);
    for (std::size_t done = 0; done < frames; done += chunk) {
        signal.fill(out.data() + done * channels, std::min(chunk, frames - done));
    }
    return out;
}

}  // namespace

TEST_CASE("a burst at the start of every second, silence between", "[test_signal]") {
    const auto out = generate(3 * kRate, 1, 240);
    for (std::size_t second = 0; second < 3; ++second) {
        const std::size_t start = second * kRate;
        bool sound = false;
        for (std::size_t i = start; i < start + 480; ++i) {
            sound = sound || out[i] != 0;
        }
        REQUIRE(sound);
        for (std::size_t i = start + 480; i < start + kRate; ++i) {
            REQUIRE(out[i] == 0);
        }
    }
}

TEST_CASE("every burst is the same", "[test_signal]") {
    const auto out = generate(3 * kRate, 1, 240);
    for (std::size_t i = 0; i < 480; ++i) {
        REQUIRE(out[kRate + i] == out[i]);
        REQUIRE(out[2 * kRate + i] == out[i]);
    }
}

TEST_CASE("the callback size does not matter", "[test_signal]") {
    REQUIRE(generate(2 * kRate, 2, 240) == generate(2 * kRate, 2, 997));
}

TEST_CASE("all channels carry the same signal within the amplitude", "[test_signal]") {
    const auto out = generate(kRate, 2, 240);
    for (std::size_t i = 0; i < kRate; ++i) {
        REQUIRE(out[2 * i] == out[2 * i + 1]);
        REQUIRE(std::abs(out[2 * i]) <= TestSignal::kAmplitude);
    }
}
