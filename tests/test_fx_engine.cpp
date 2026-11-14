#include <catch2/catch_test_macros.hpp>

#include "FxEngine.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace fxpad;

namespace {

constexpr uint32_t kRate = 44100;
constexpr uint32_t kBlock = 64;

/* A source a block at a time: noise and a 220 Hz sine, -12 dB. */
struct Source {
    uint32_t seed = 1, n = 0;
    void fill(float* b, uint32_t frames, bool on = true)
    {
        for (uint32_t i = 0; i < frames; ++i, ++n) {
            seed = seed * 1664525u + 1013904223u;
            const float noise = ((float)(seed >> 8) / 16777216.0f - 0.5f) * 0.2f;
            const float s = 0.25f * std::sin(2.0f * 3.14159265f * 220.0f * (float)n / kRate);
            b[2 * i] = on ? s + noise : 0.0f;
            b[2 * i + 1] = on ? s - noise : 0.0f;
        }
    }
};

struct Run {
    float peak = 0.0f;
    bool finite = true;
};

/* @p blocks of the source through the engine, measuring what comes out. */
Run play(FxEngine& e, Source& src, int blocks, bool on = true)
{
    std::vector<float> b(kBlock * 2);
    Run r;
    for (int k = 0; k < blocks; ++k) {
        src.fill(b.data(), kBlock, on);
        e.process(b.data(), b.data(), kBlock);
        for (float v : b) {
            if (!std::isfinite(v)) r.finite = false;
            r.peak = std::max(r.peak, std::fabs(v));
        }
    }
    return r;
}

} // namespace

TEST_CASE("FX engine: every effect stays finite and bounded, at every corner of X/Y", "[fxpad]")
{
    FxEngine e;
    e.onPrepare(kBlock, kRate);
    Source src;
    for (int fx = 0; fx < kFxCount; ++fx) {
        for (float x : {0.0f, 0.5f, 1.0f}) {
            for (float y : {0.0f, 0.5f, 1.0f}) {
                e.setSlot(0, fx, x, y, 1.0f);
                const Run r = play(e, src, 200);                  // ~0.3 s held
                INFO("fx " << fx << " x " << x << " y " << y);
                CHECK(r.finite);
                CHECK(r.peak < 4.0f);
                e.setSlot(0, None, 0, 0, 0);
                e.kill();
                play(e, src, 2);
            }
        }
    }
}

TEST_CASE("FX engine: THRU passes the sources with nothing on; off, nothing is heard", "[fxpad]")
{
    FxEngine e;
    e.onPrepare(kBlock, kRate);
    std::vector<float> in(kBlock * 2), out(kBlock * 2);
    Source src;
    src.fill(in.data(), kBlock);
    e.process(in.data(), out.data(), kBlock);
    CHECK(out == in);
    e.setThru(false);
    e.process(in.data(), out.data(), kBlock);
    for (float v : out) CHECK(v == 0.0f);
}

TEST_CASE("FX engine: a delay rings out after its pad and then stops; a filter stops at once", "[fxpad]")
{
    FxEngine e;
    e.onPrepare(kBlock, kRate);
    Source src;
    e.setSlot(0, Echo, 0.6f, 0.5f, 1.0f);
    play(e, src, 100);
    e.setSlot(0, None, 0, 0, 0);
    play(e, src, 20, false);
    CHECK(e.running() & (1u << Echo));                   // still ringing
    play(e, src, (int)(16.0f * kRate / kBlock), false);
    CHECK(e.running() == 0);                             // over within the tail limit

    e.setSlot(0, LowPass, 0.2f, 0.5f, 1.0f);
    play(e, src, 50);
    e.setSlot(0, None, 0, 0, 0);
    play(e, src, 40);                                    // ~60 ms
    CHECK(e.running() == 0);
}

TEST_CASE("FX engine: two effects at once, started in order", "[fxpad]")
{
    FxEngine e;
    e.onPrepare(kBlock, kRate);
    Source src;
    e.setSlot(0, LowPass, 0.3f, 0.4f, 1.0f);
    e.setSlot(1, Hall, 0.5f, 0.5f, 0.8f);
    const Run r = play(e, src, 300);
    CHECK(r.finite);
    CHECK(e.running() == ((1u << LowPass) | (1u << Hall)));
}

TEST_CASE("FX engine: block time on this host, the heaviest pair", "[fxpad][perf]")
{
    FxEngine e;
    e.onPrepare(kBlock, kRate);
    Source src;
    e.setSlot(0, Shimmer, 0.8f, 0.5f, 1.0f);
    e.setSlot(1, PingPong, 0.6f, 0.6f, 1.0f);
    play(e, src, 50);
    const auto t0 = std::chrono::steady_clock::now();
    play(e, src, 2000);
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 2000.0;
    std::printf("shimmer + ping pong: %.1f us a %u-frame block on the host (block is %.0f us)\n", us, kBlock,
                1e6 * kBlock / kRate);
    CHECK(us < 1e6 * kBlock / kRate);
}

TEST_CASE("FX engine: TAPE STOP runs down to silence; SCRATCH is silent with the finger still", "[fxpad][bankb]")
{
    FxEngine e;
    e.onPrepare(kBlock, kRate);
    Source src;
    play(e, src, 100);                                   // the tape has something on it
    e.setSlot(0, TapeStop, 0.0f, 0.0f, 1.0f);            // the shortest run-down, a straight one
    play(e, src, 200);                                   // ~290 ms: run down, WET settled
    CHECK(play(e, src, 10).peak < 1e-2f);                // under -40 dB

    e.kill();
    e.setSlot(0, None, 0, 0, 0);
    play(e, src, 5);
    e.setSlot(0, Scratch, 0.5f, 0.5f, 1.0f);
    play(e, src, 100);                                   // the needle has settled under the finger
    CHECK(play(e, src, 10).peak < 1e-2f);
}

TEST_CASE("FX engine: STUTTER repeats one slice", "[fxpad][bankb]")
{
    FxEngine e;
    e.onPrepare(kBlock, kRate);
    Source src;
    play(e, src, 50);
    e.setSync(false);
    e.setSlot(0, Stutter, 0.0f, 1.0f, 1.0f);             // a 125 ms slice, open all the way
    std::vector<float> b(kBlock * 2), seq;
    for (int k = 0; k < 600; ++k) {                      // ~870 ms
        src.fill(b.data(), kBlock);
        e.process(b.data(), b.data(), kBlock);
        if (k >= 300) seq.insert(seq.end(), b.begin(), b.end());   // WET settled
    }
    const size_t slice = (size_t)(0.125f * kRate) * 2;  // stereo samples a slice
    size_t same = 0, total = 0;
    for (size_t i = 200; i + slice < seq.size(); i += 7, ++total) {
        if (std::fabs(seq[i] - seq[i + slice]) < 1e-4f) ++same;
    }
    CHECK(same > total * 9 / 10);                        // what plays now played a slice ago
}
