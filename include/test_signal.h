#ifndef TEST_SIGNAL_H
#define TEST_SIGNAL_H

#include <algorithm>
#include <cstddef>
#include <cstdint>

// What the sender plays in place of its input for measuring receivers
// against each other: a short burst of noise at the start of every second of
// the stream, silence in between. Noise because it is broadband, so a cross-
// correlation of two recordings peaks sharply, at a fraction of a sample;
// the same burst every time, so one template finds them all.
//
// Driven by frame count, not by time: burst n starts at stream frame
// n * sampleRate, wherever the callbacks fall.
class TestSignal {
public:
    static constexpr double kBurstSeconds = 0.010;
    static constexpr std::int16_t kAmplitude = 8192;     // -12 dBFS peak

    void configure(unsigned int sampleRate, std::size_t channels) noexcept {
        period_ = sampleRate;
        burst_ = static_cast<std::uint64_t>(kBurstSeconds * sampleRate);
        channels_ = channels;
        frame_ = 0;
    }

    // Interleaved s16, `frames` frames: the next part of the signal.
    void fill(std::int16_t* out, std::size_t frames) noexcept {
        for (std::size_t i = 0; i < frames; ++i, ++frame_) {
            const std::uint64_t phase = frame_ % period_;
            if (phase == 0) {
                noise_ = kSeed;
            }
            const std::int16_t sample = phase < burst_ ? next() : 0;
            std::fill_n(out, channels_, sample);
            out += channels_;
        }
    }

    [[nodiscard]] std::uint64_t burstFrames() const noexcept { return burst_; }

private:
    static constexpr std::uint32_t kSeed = 0x12345678u;

    // xorshift32: cheap and allocation-free, fine on the audio thread.
    [[nodiscard]] std::int16_t next() noexcept {
        noise_ ^= noise_ << 13;
        noise_ ^= noise_ >> 17;
        noise_ ^= noise_ << 5;
        const auto unit = static_cast<std::int32_t>(noise_ >> 16) - 32768;     // -32768..32767
        return static_cast<std::int16_t>(unit * kAmplitude / 32768);
    }

    std::uint64_t period_ = 1;
    std::uint64_t burst_ = 0;
    std::size_t channels_ = 1;
    std::uint64_t frame_ = 0;
    std::uint32_t noise_ = kSeed;
};

#endif  // TEST_SIGNAL_H
