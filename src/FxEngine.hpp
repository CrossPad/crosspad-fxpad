// SPDX-License-Identifier: GPL-3.0-or-later
//
// The FX Pad's sound: sixteen performance effects, any of them running at
// once in the order they were started, over the board's FX bus
// (crosspad::IFxBus). The UI and the pads set what plays through atomics;
// the render thread does the rest -- starting an effect clean, letting one
// ring out after its pad is let go (TAIL) or fading it at once (SNAP),
// following X, Y and WET without zipper noise.
#pragma once

#include "crosspad/audio/IAudioNode.hpp"

#include <atomic>
#include <cstdint>

namespace fxpad {

constexpr int kFxCount = 16;
constexpr int kSlots   = 2;

/// The effects, in pad order (pad 1 = bottom left).
enum Fx : int8_t {
    Echo, TapeEcho, PingPong, Roll,
    Hall, Shimmer, Freeze, Gate,
    LowPass, HighPass, BandSweep, Wah,
    Flanger, Phaser, Bitcrush, Drive,
    None = -1
};

class Effect;

class FxEngine final : public crosspad::IAudioNode {
public:
    FxEngine();
    ~FxEngine() override;

    // ── From the UI and the pads (any thread) ─────────────────────────────
    /// Slot @p slot plays @p fx (None = empty) at X/Y 0..1 (mapped through
    /// the effect's range and curve already) and WET 0..1.
    void setSlot(int slot, int fx, float x, float y, float wet);
    /// How @p fx stops when its pad is let go: true = fade at once, false =
    /// ring out.
    void setSnap(int fx, bool snap);
    /// Times and rates follow the tempo in note values; off, they run free.
    void setSync(bool on) { sync_.store(on, std::memory_order_relaxed); }
    void setTempo(float bpm) { if (bpm > 0.0f) bpm_.store(bpm, std::memory_order_relaxed); }
    /// The sources pass dry with no effect on; off, only effects are heard.
    void setThru(bool on) { thru_.store(on, std::memory_order_relaxed); }
    void setMono(bool on) { mono_.store(on, std::memory_order_relaxed); }
    /// Every effect off at once, tails cut.
    void kill() { kill_.store(true, std::memory_order_relaxed); }
    /// Effects playing now, a bit each (held, ringing out or fading).
    uint32_t running() const { return running_.load(std::memory_order_relaxed); }

    // ── IAudioNode ─────────────────────────────────────────────────────────
    void process(const float* in, float* out, uint32_t frames) override;
    void onPrepare(uint32_t maxFrames, uint32_t sampleRate) override;
    const char* name() const override { return "FX Pad"; }

private:
    struct SlotState {
        std::atomic<int8_t> fx{None};
        std::atomic<float>  x{0.5f}, y{0.5f}, wet{0.6f};
    };
    SlotState slot_[kSlots];
    std::atomic<bool> snap_[kFxCount];
    std::atomic<bool> sync_{true}, thru_{true}, mono_{false}, kill_{false};
    std::atomic<float> bpm_{120.0f};
    std::atomic<uint32_t> running_{0};

    Effect* fx_[kFxCount] = {};
    uint32_t rate_ = 0;
    uint32_t stamp_ = 0;
    bool ready_ = false;
};

} // namespace fxpad
