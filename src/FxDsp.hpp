// SPDX-License-Identifier: GPL-3.0-or-later
//
// The building blocks the FX Pad's effects are made of. Plain C++, no
// platform: they run on the board's render thread and in the host tests
// alike. Nothing here allocates after init(), takes a lock or calls the OS.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

namespace fxpad {

constexpr float kPi = 3.14159265358979f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/// sin(2 pi p) for p in [0, 1), within 0.1 % -- an LFO's shape, not a tone's.
inline float sin2pi(float p)
{
    const float x = 2.0f * p - 1.0f;                     // -1..1
    float y = 4.0f * x * (1.0f - std::fabs(x));          // ~sin(pi x)
    y = 0.225f * (y * std::fabs(y) - y) + y;
    return -y;
}

/// A soft clip that never goes past +-1: x / (1 + |x|) shaped to unity slope.
inline float softClip(float x)
{
    x = clampf(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/// One parameter followed towards its target a block at a time.
struct Smooth {
    float value = 0.0f;
    void snap(float v) { value = v; }
    float step(float target, float coeff) { value += (target - value) * coeff; return value; }
};

/// A one-pole low pass, per channel.
struct OnePole {
    float z = 0.0f;
    float a = 1.0f;          // 1 = open
    void setHz(float hz, float rate) { a = 1.0f - std::exp(-2.0f * kPi * hz / rate); }
    float lp(float x) { z += a * (x - z); return z; }
};

/// Andrew Simper's trapezoidal state-variable filter: low, band and high
/// from one structure, stable under fast sweeps.
struct Svf {
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 2.0f;
    void set(float hz, float res, float rate)
    {
        const float g = std::tan(kPi * clampf(hz, 10.0f, rate * 0.45f) / rate);
        k = 2.0f - 2.0f * clampf(res, 0.0f, 0.98f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void setK(float hz, float kk, float rate) { set(hz, 0.0f, rate); k = kk; const float g = a2 / a1;
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    /// One sample; low, band and high out.
    void tick(float v0, float& low, float& band, float& high)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = v0 - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0.0f; }
};

/// A mono ring of floats with a fractional read, and an age: what has been
/// written since clear() -- a read further back than that is silence. That
/// is how an effect starts clean without the render clearing a buffer of a
/// second or more, which on PSRAM costs milliseconds.
class Delay {
public:
    bool init(uint32_t len)
    {
        len_ = len;
        buf_.reset(new (std::nothrow) float[len]);
        if (!buf_) return false;
        std::memset(buf_.get(), 0, len * sizeof(float));
        pos_ = 0;
        age_ = 0;
        return true;
    }
    uint32_t length() const { return len_; }
    void clear() { age_ = 0; }
    void write(float v)
    {
        buf_[pos_] = v;
        if (++pos_ == len_) pos_ = 0;
        if (age_ < len_) ++age_;
    }
    /// @p d samples back from the last write, 1 .. length-1, fractional.
    float read(float d) const
    {
        if (d < 1.0f) d = 1.0f;
        if (d > (float)(len_ - 2)) d = (float)(len_ - 2);
        const uint32_t di = (uint32_t)d;
        if (di + 1 > age_) return 0.0f;
        const float f = d - (float)di;
        uint32_t i0 = pos_ + len_ - di;
        if (i0 >= len_) i0 -= len_;
        const uint32_t i1 = i0 == 0 ? len_ - 1 : i0 - 1;
        return buf_[i0] + f * (buf_[i1] - buf_[i0]);
    }
    /// A whole number of samples back, no interpolation.
    float tap(uint32_t d) const
    {
        if (d == 0 || d > age_ || d >= len_) return 0.0f;
        uint32_t i = pos_ + len_ - d;
        if (i >= len_) i -= len_;
        return buf_[i];
    }

private:
    std::unique_ptr<float[]> buf_;
    uint32_t len_ = 0, pos_ = 0, age_ = 0;
};

/// A delay-line pitch shifter: two taps sweeping through a window, crossfaded
/// by a triangle so the jump of each never shows. Cheap and grainy -- the
/// shimmer it feeds wants exactly that.
class PitchShift {
public:
    bool init(uint32_t window) { win_ = (float)window; return d_.init(window + 4); }
    void setRatio(float r) { rate_ = 1.0f - r; }
    float tick(float x)
    {
        d_.write(x);
        phase_ += rate_ / win_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        if (phase_ < 0.0f) phase_ += 1.0f;
        float p2 = phase_ + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        const float a = d_.read(1.0f + phase_ * (win_ - 2.0f));
        const float b = d_.read(1.0f + p2 * (win_ - 2.0f));
        const float wa = 1.0f - std::fabs(2.0f * phase_ - 1.0f);
        return a * wa + b * (1.0f - wa);
    }
    void clear() { d_.clear(); }

private:
    Delay d_;
    float win_ = 1.0f, phase_ = 0.0f, rate_ = 0.0f;
};

} // namespace fxpad
