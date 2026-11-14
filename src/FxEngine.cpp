// SPDX-License-Identifier: GPL-3.0-or-later
#include "FxEngine.hpp"
#include "FxDsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace fxpad {

namespace {

/// Note values TIME and RATE step through with SYNC on, as fractions of a
/// whole note: 1/32, 1/16, 1/8, dotted 1/8, 1/4, 1/2.
constexpr float kNote[] = {1.0f / 32, 1.0f / 16, 1.0f / 8, 3.0f / 16, 1.0f / 4, 1.0f / 2};

/// TIME in seconds: six note values synced, 20 ms .. 1.5 s free.
float timeSec(float x, float bpm, bool sync)
{
    if (sync) return kNote[(int)std::lround(x * 5.0f)] * 240.0f / bpm;
    return 0.02f * std::pow(75.0f, x);
}

/// RATE as a period in seconds: five note values synced, 30 .. 500 ms free.
float rateSec(float x, float bpm, bool sync)
{
    if (sync) return kNote[(int)std::lround(x * 4.0f)] * 240.0f / bpm;
    return 0.03f * std::pow(16.7f, x);
}

/// SPEED of a modulation, in Hz: 0.05 .. 4.
float speedHz(float x) { return 0.05f * std::pow(80.0f, x); }

struct Ctx {
    float rate;
    float bpm;
    bool  sync;
};

/// A linear move from one block's value to the next, a sample at a time.
struct Ramp {
    float v, dv;
    Ramp(float from, float to, uint32_t n) : v(from), dv(n ? (to - from) / (float)n : 0.0f) {}
    float next() { const float r = v; v += dv; return r; }
};

} // namespace

/// One effect. render() works in place on interleaved stereo and returns the
/// peak of what it added (its wet signal), which says when a tail is over.
class Effect {
public:
    enum class State : uint8_t { Off, On, Tail, Fade };
    /// Add: a wet signal on top of the dry (delays, reverbs). Blend: the
    /// processed signal in place of the dry, by WET (filters, modulation,
    /// distortion, gate, roll).
    explicit Effect(bool add) : add_(add) {}
    virtual ~Effect() = default;
    virtual bool init(float rate) = 0;
    /// A fresh start: old state forgotten, cheaply.
    virtual void start() {}
    /// Every block, running or not, for an effect that needs what came
    /// before it was started (ROLL).
    virtual void listen(const float* buf, uint32_t n) { (void)buf; (void)n; }
    /// The pad was let go and the effect rings out (gate closing).
    virtual void release() {}
    /// It has stopped: nothing is rendered until the next start().
    virtual void stop() {}
    virtual float render(float* buf, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet,
                         float dry) = 0;

    bool add() const { return add_; }

    State    state = State::Off;
    uint32_t stamp = 0;
    Smooth   sx, sy, swet, sgate;
    uint32_t quiet = 0;      // samples of silence in a tail
    uint32_t age = 0;        // samples since it started ringing out

private:
    bool add_;
};

namespace {

// ── Delays ─────────────────────────────────────────────────────────────────

constexpr float kMaxDelaySec = 2.0f;

class EchoFx final : public Effect {
public:
    EchoFx() : Effect(true) {}
    bool init(float rate) override
    {
        lp_[0].setHz(7000.0f, rate);
        lp_[1] = lp_[0];
        return d_[0].init((uint32_t)(kMaxDelaySec * rate)) && d_[1].init((uint32_t)(kMaxDelaySec * rate));
    }
    void start() override { d_[0].clear(); d_[1].clear(); cur_ = -1.0f; }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        const float target = timeSec(x, c.bpm, c.sync) * c.rate;
        if (cur_ < 0.0f) cur_ = target;
        const float fb = clampf(y, 0.0f, 0.95f);
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            cur_ += (target - cur_) * 0.0005f;        // a change of time glides, as on tape
            const float g = gate.next(), w = wet.next();
            for (int ch = 0; ch < 2; ++ch) {
                const float e = d_[ch].read(cur_);
                d_[ch].write(b[2 * i + ch] * g + lp_[ch].lp(e) * fb);
                b[2 * i + ch] = b[2 * i + ch] * dry + e * w;
                peak = std::max(peak, std::fabs(e));
            }
        }
        return peak;
    }

private:
    Delay d_[2];
    OnePole lp_[2];
    float cur_ = -1.0f;
};

class TapeEchoFx final : public Effect {
public:
    TapeEchoFx() : Effect(true) {}
    bool init(float rate) override
    {
        rate_ = rate;
        lp_[0].setHz(3200.0f, rate);
        lp_[1] = lp_[0];
        return d_[0].init((uint32_t)(kMaxDelaySec * rate)) && d_[1].init((uint32_t)(kMaxDelaySec * rate));
    }
    void start() override { d_[0].clear(); d_[1].clear(); cur_ = -1.0f; }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        const float target = timeSec(x, c.bpm, c.sync) * c.rate;
        if (cur_ < 0.0f) cur_ = target;
        const float depth = clampf(y, 0.0f, 1.0f) * 0.004f * rate_;   // up to 4 ms of wow and flutter
        const float wowInc = 0.6f / rate_, flutInc = 5.3f / rate_;
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            cur_ += (target - cur_) * 0.0003f;
            wow_ += wowInc;   if (wow_ >= 1.0f) wow_ -= 1.0f;
            flut_ += flutInc; if (flut_ >= 1.0f) flut_ -= 1.0f;
            const float m = depth * (0.5f + 0.35f * sin2pi(wow_) + 0.15f * sin2pi(flut_));
            const float g = gate.next(), w = wet.next();
            for (int ch = 0; ch < 2; ++ch) {
                const float e = d_[ch].read(cur_ + m);
                d_[ch].write(b[2 * i + ch] * g + softClip(lp_[ch].lp(e) * 0.62f * 1.2f));
                b[2 * i + ch] = b[2 * i + ch] * dry + e * w;
                peak = std::max(peak, std::fabs(e));
            }
        }
        return peak;
    }

private:
    Delay d_[2];
    OnePole lp_[2];
    float rate_ = 44100.0f, cur_ = -1.0f, wow_ = 0.0f, flut_ = 0.0f;
};

class PingPongFx final : public Effect {
public:
    PingPongFx() : Effect(true) {}
    bool init(float rate) override
    {
        lp_.setHz(8000.0f, rate);
        return d_[0].init((uint32_t)(kMaxDelaySec * rate)) && d_[1].init((uint32_t)(kMaxDelaySec * rate));
    }
    void start() override { d_[0].clear(); d_[1].clear(); cur_ = -1.0f; }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        const float target = timeSec(x, c.bpm, c.sync) * c.rate;
        if (cur_ < 0.0f) cur_ = target;
        const float fb = clampf(y, 0.0f, 0.95f);
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            cur_ += (target - cur_) * 0.0005f;
            const float g = gate.next(), w = wet.next();
            const float el = d_[0].read(cur_), er = d_[1].read(cur_);
            const float m = 0.5f * (b[2 * i] + b[2 * i + 1]) * g;
            d_[0].write(m + lp_.lp(er) * fb);       // left takes the input and the right's repeat
            d_[1].write(el * fb);                  // right only ever the left's
            b[2 * i]     = b[2 * i] * dry + el * w;
            b[2 * i + 1] = b[2 * i + 1] * dry + er * w;
            peak = std::max(peak, std::max(std::fabs(el), std::fabs(er)));
        }
        return peak;
    }

private:
    Delay d_[2];
    OnePole lp_;
    float cur_ = -1.0f;
};

/// ROLL: what was just played, a note value long, repeated while the pad is
/// held, each pass quieter by DECAY. It listens all the time so the loop is
/// what came before the press.
class RollFx final : public Effect {
public:
    RollFx() : Effect(false) {}
    bool init(float rate) override
    {
        rate_ = rate;
        len_ = (uint32_t)(kMaxDelaySec * rate);
        buf_.reset(new (std::nothrow) float[len_ * 2]);
        if (!buf_) return false;
        std::memset(buf_.get(), 0, len_ * 2 * sizeof(float));
        return true;
    }
    void start() override { looping_ = false; }
    void stop() override { looping_ = false; }
    void listen(const float* b, uint32_t n) override
    {
        if (looping_) return;                       // the loop's audio must stay put
        for (uint32_t i = 0; i < n; ++i) {
            buf_[2 * pos_] = b[2 * i];
            buf_[2 * pos_ + 1] = b[2 * i + 1];
            if (++pos_ == len_) pos_ = 0;
        }
    }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        (void)gate; (void)dry;
        uint32_t loop = (uint32_t)(rateSec(x, c.bpm, c.sync) * c.rate);
        loop = std::min<uint32_t>(std::max<uint32_t>(loop, 256), len_ / 2);
        if (!looping_) {
            looping_ = true;
            loopLen_ = loop;
            start_ = (pos_ + len_ - loop) % len_;
            at_ = 0;
            gain_ = 1.0f;
        }
        const float keep = 1.0f - 0.7f * clampf(y, 0.0f, 1.0f);
        constexpr uint32_t kEdge = 64;
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t idx = start_ + at_;
            if (idx >= len_) idx -= len_;
            float edge = 1.0f;
            if (at_ < kEdge) edge = (float)at_ / kEdge;
            else if (at_ > loopLen_ - kEdge) edge = (float)(loopLen_ - at_) / kEdge;
            const float w = wet.next();
            for (int ch = 0; ch < 2; ++ch) {
                const float r = buf_[2 * idx + ch] * gain_ * edge;
                b[2 * i + ch] = b[2 * i + ch] * (1.0f - w) + r * w;
                peak = std::max(peak, std::fabs(r));
            }
            if (++at_ >= loopLen_) {                // a pass over: the next is quieter, a new RATE starts here
                at_ = 0;
                gain_ *= keep;
                loopLen_ = loop;
                start_ = (pos_ + len_ - loop) % len_;
            }
        }
        return peak;
    }

private:
    std::unique_ptr<float[]> buf_;
    float rate_ = 44100.0f, gain_ = 1.0f;
    uint32_t len_ = 0, pos_ = 0, start_ = 0, at_ = 0, loopLen_ = 1;
    bool looping_ = false;
};

// ── Reverbs ────────────────────────────────────────────────────────────────

/// A ring of floats a power of two long: a tap is a mask, not a modulo and
/// a branch. What was there before a clear() is not read -- the reverb
/// counts how much it has written since (`fresh`) instead of wiping the
/// buffer on the render thread.
struct Ring {
    std::unique_ptr<float[]> b;
    uint32_t mask = 0, pos = 0;
    bool init(uint32_t minLen)
    {
        uint32_t n = 1;
        while (n < minLen + 1) n <<= 1;
        b.reset(new (std::nothrow) float[n]);
        if (!b) return false;
        std::memset(b.get(), 0, n * sizeof(float));
        mask = n - 1;
        return true;
    }
    float tap(uint32_t d) const { return b[(pos - d) & mask]; }
    void write(float v) { b[pos & mask] = v; ++pos; }
};

/// Four allpasses diffusing the input, then a four-line feedback network
/// (Householder) with damping in every line -- run at half the sample rate:
/// a tail is dark anyway, and every PSRAM line it reads costs the render the
/// same whatever is in it. The input is averaged over each pair of frames
/// and the output interpolated back, a frame and a half late. ~25 kB of
/// PSRAM, ~20 operations a frame.
class Reverb {
public:
    bool init(float rate)
    {
        rate_ = rate * 0.5f;
        const float s = rate_ / 22050.0f;
        static const uint32_t kAp[4] = {105, 79, 281, 205};
        static const uint32_t kLine[4] = {713, 801, 933, 1027};
        for (int i = 0; i < 4; ++i) {
            apLen_[i] = (uint32_t)(kAp[i] * s);
            len_[i] = (uint32_t)(kLine[i] * s);
            if (!ap_[i].init(apLen_[i]) || !line_[i].init(len_[i])) return false;
        }
        return true;
    }
    void clear()
    {
        fresh_ = 0;
        for (auto& l : lp_) l.z = 0.0f;
        odd_ = false;
        acc_ = pl_ = pr_ = cl_ = cr_ = 0.0f;
    }
    /// Decay time; <= 0 = forever (FREEZE).
    void setDecay(float rt60)
    {
        for (int i = 0; i < 4; ++i) {
            g_[i] = rt60 <= 0.0f ? 1.0f : std::pow(10.0f, -3.0f * (float)len_[i] / (rt60 * rate_));
        }
    }
    void setDamp(float hz) { for (auto& l : lp_) l.setHz(hz, rate_); }
    /// One frame in (mono), L and R out.
    void tick(float in, float& l, float& r)
    {
        if (!odd_) {
            odd_ = true;
            acc_ = in;
            l = 0.5f * (pl_ + cl_);
            r = 0.5f * (pr_ + cr_);
            return;
        }
        odd_ = false;
        pl_ = cl_;
        pr_ = cr_;
        tank(0.5f * (acc_ + in), cl_, cr_);
        l = pl_;
        r = pr_;
    }

private:
    void tank(float x, float& l, float& r)
    {
        for (int i = 0; i < 4; ++i) {
            const float z = fresh_ > apLen_[i] ? ap_[i].tap(apLen_[i]) : 0.0f;
            const float y = z - 0.65f * x;
            ap_[i].write(x + 0.65f * y);
            x = y;
        }
        float y[4];
        for (int i = 0; i < 4; ++i) y[i] = fresh_ > len_[i] ? lp_[i].lp(line_[i].tap(len_[i])) * g_[i] : 0.0f;
        const float h = 0.5f * (y[0] + y[1] + y[2] + y[3]);
        for (int i = 0; i < 4; ++i) line_[i].write(y[i] - h + x + 1e-20f);
        if (fresh_ < 4096) ++fresh_;
        l = 0.7f * (y[0] - y[2]) + 0.3f * y[1];
        r = 0.7f * (y[1] - y[3]) + 0.3f * y[2];
    }

    Ring ap_[4], line_[4];
    OnePole lp_[4];
    uint32_t apLen_[4] = {}, len_[4] = {};
    float g_[4] = {};
    float rate_ = 22050.0f;
    uint32_t fresh_ = 0;
    bool odd_ = false;
    float acc_ = 0.0f, pl_ = 0.0f, pr_ = 0.0f, cl_ = 0.0f, cr_ = 0.0f;
};

class HallFx final : public Effect {
public:
    HallFx() : Effect(true) {}
    bool init(float rate) override { return rv_.init(rate); }
    void start() override { rv_.clear(); }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        (void)c;
        rv_.setDecay(0.3f * std::pow(25.0f, clampf(x, 0.0f, 1.0f)));           // 0.3 .. 7.5 s
        const float d = 1.0f - clampf(y, 0.0f, 1.0f);
        rv_.setDamp(1000.0f + 15000.0f * d * d);
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            const float g = gate.next(), w = wet.next();
            float l, r;
            rv_.tick(0.5f * (b[2 * i] + b[2 * i + 1]) * g, l, r);
            b[2 * i]     = b[2 * i] * dry + l * w;
            b[2 * i + 1] = b[2 * i + 1] * dry + r * w;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        }
        return peak;
    }

private:
    Reverb rv_;
};

/// A hall whose own sound comes back into it pitched up: each pass of the
/// tail climbs by PITCH.
class ShimmerFx final : public Effect {
public:
    ShimmerFx() : Effect(true) {}
    bool init(float rate) override { return rv_.init(rate) && ps_.init((uint32_t)(0.05f * rate)); }
    void start() override { rv_.clear(); ps_.clear(); fb_ = 0.0f; }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        (void)c;
        rv_.setDecay(0.8f * std::pow(10.0f, clampf(x, 0.0f, 1.0f)));            // 0.8 .. 8 s
        rv_.setDamp(9000.0f);
        ps_.setRatio(std::pow(2.0f, std::round(clampf(y, 0.0f, 1.0f) * 24.0f) / 12.0f));
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            const float g = gate.next(), w = wet.next();
            float l, r;
            rv_.tick(0.5f * (b[2 * i] + b[2 * i + 1]) * g + fb_, l, r);
            fb_ = ps_.tick(0.5f * (l + r)) * 0.42f;
            b[2 * i]     = b[2 * i] * dry + l * w;
            b[2 * i + 1] = b[2 * i + 1] * dry + r * w;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        }
        return peak;
    }

private:
    Reverb rv_;
    PitchShift ps_;
    float fb_ = 0.0f;
};

/// FREEZE: a quarter second of what plays when the pad goes down, held for
/// as long as it stays down; let go, it fades over 1.5 s.
class FreezeFx final : public Effect {
public:
    FreezeFx() : Effect(true) {}
    bool init(float rate) override { rate_ = rate; return rv_.init(rate); }
    void start() override
    {
        rv_.clear();
        rv_.setDecay(0.0f);
        rv_.setDamp(18000.0f);
        capture_ = (uint32_t)(0.25f * rate_);
    }
    void release() override { rv_.setDecay(1.5f); capture_ = 0; }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        (void)c; (void)gate;
        tone_[0].setHz(300.0f * std::pow(50.0f, clampf(x, 0.0f, 1.0f)), rate_);
        tone_[1].a = tone_[0].a;
        const float level = clampf(y, 0.0f, 1.0f);
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            const float w = wet.next() * level;
            float in = 0.0f;
            if (capture_) { in = 0.5f * (b[2 * i] + b[2 * i + 1]) * 0.35f; --capture_; }
            float l, r;
            rv_.tick(in, l, r);
            l = tone_[0].lp(l);
            r = tone_[1].lp(r);
            b[2 * i]     = b[2 * i] * dry + l * w;
            b[2 * i + 1] = b[2 * i + 1] * dry + r * w;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        }
        return peak;
    }

private:
    Reverb rv_;
    OnePole tone_[2];
    float rate_ = 44100.0f;
    uint32_t capture_ = 0;
};

// ── Gate ───────────────────────────────────────────────────────────────────

class GateFx final : public Effect {
public:
    GateFx() : Effect(false) {}
    bool init(float rate) override { smooth_.setHz(400.0f, rate); return true; }
    void start() override { phase_ = 0.0f; smooth_.z = 1.0f; }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        (void)gate; (void)dry;
        const float inc = 1.0f / (rateSec(x, c.bpm, c.sync) * c.rate);
        const float depth = clampf(y, 0.0f, 1.0f);
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            phase_ += inc;
            if (phase_ >= 1.0f) phase_ -= 1.0f;
            const float g = smooth_.lp(phase_ < 0.5f ? 1.0f : 1.0f - depth);
            const float w = wet.next();
            const float k = 1.0f - w + w * g;
            b[2 * i] *= k;
            b[2 * i + 1] *= k;
            peak = std::max(peak, w * (1.0f - g));
        }
        return peak;
    }

private:
    float phase_ = 0.0f;
    OnePole smooth_;
};

// ── Filters ────────────────────────────────────────────────────────────────

enum class FiltKind : uint8_t { Low, High, Band, Wah };

class Filter final : public Effect {
public:
    explicit Filter(FiltKind k) : Effect(false), kind_(k) {}
    bool init(float rate) override { (void)rate; return true; }
    void start() override { f_[0].reset(); f_[1].reset(); }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        (void)gate; (void)dry;
        x = clampf(x, 0.0f, 1.0f);
        y = clampf(y, 0.0f, 1.0f);
        float norm = 1.0f;
        switch (kind_) {
        case FiltKind::Low:  f_[0].set(80.0f * std::pow(200.0f, x), y, c.rate); break;
        case FiltKind::High: f_[0].set(80.0f * std::pow(200.0f, x), y, c.rate); break;
        case FiltKind::Band: f_[0].setK(200.0f * std::pow(20.0f, x), 0.15f + 1.85f * y, c.rate); norm = f_[0].k; break;
        case FiltKind::Wah:  f_[0].set(200.0f * std::pow(20.0f, x), 0.5f + 0.45f * y, c.rate); norm = f_[0].k * 1.6f; break;
        }
        f_[1].a1 = f_[0].a1; f_[1].a2 = f_[0].a2; f_[1].a3 = f_[0].a3; f_[1].k = f_[0].k;
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            const float w = wet.next();
            for (int ch = 0; ch < 2; ++ch) {
                float lo, bp, hi;
                const float in = b[2 * i + ch];
                f_[ch].tick(in, lo, bp, hi);
                float o;
                switch (kind_) {
                case FiltKind::Low:  o = lo; break;
                case FiltKind::High: o = hi; break;
                case FiltKind::Band: o = bp * norm; break;
                default:             o = bp * norm + 0.1f * in; break;
                }
                b[2 * i + ch] = in * (1.0f - w) + o * w;
                peak = std::max(peak, w);
            }
        }
        return peak;
    }

private:
    FiltKind kind_;
    Svf f_[2];
};

// ── Modulation ─────────────────────────────────────────────────────────────

class FlangerFx final : public Effect {
public:
    FlangerFx() : Effect(false) {}
    bool init(float rate) override { rate_ = rate; return d_[0].init((uint32_t)(0.012f * rate)) && d_[1].init((uint32_t)(0.012f * rate)); }
    void start() override { d_[0].clear(); d_[1].clear(); }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        (void)c; (void)gate; (void)dry;
        const float inc = speedHz(clampf(x, 0.0f, 1.0f)) / rate_;
        const float base = 0.0003f * rate_, span = clampf(y, 0.0f, 1.0f) * 0.005f * rate_;
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            phase_ += inc;
            if (phase_ >= 1.0f) phase_ -= 1.0f;
            const float w = wet.next();
            for (int ch = 0; ch < 2; ++ch) {
                float p = phase_ + (ch ? 0.25f : 0.0f);
                if (p >= 1.0f) p -= 1.0f;
                const float dly = base + span * (0.5f + 0.5f * sin2pi(p));
                const float e = d_[ch].read(dly);
                const float in = b[2 * i + ch];
                d_[ch].write(in + e * 0.55f);
                const float o = 0.7f * (in + e);
                b[2 * i + ch] = in * (1.0f - w) + o * w;
            }
            peak = std::max(peak, w);
        }
        return peak;
    }

private:
    Delay d_[2];
    float rate_ = 44100.0f, phase_ = 0.0f;
};

class PhaserFx final : public Effect {
public:
    PhaserFx() : Effect(false) {}
    bool init(float rate) override { rate_ = rate; return true; }
    void start() override { std::memset(z_, 0, sizeof z_); fb_[0] = fb_[1] = 0.0f; }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        (void)c; (void)gate; (void)dry;
        const float inc = speedHz(clampf(x, 0.0f, 1.0f)) / rate_;
        const float fbk = clampf(y, 0.0f, 0.9f);
        constexpr uint32_t kSub = 16;              // the sweep's coefficient, every 16 frames
        float peak = 0.0f;
        for (uint32_t i0 = 0; i0 < n; i0 += kSub) {
            phase_ += inc * kSub;
            if (phase_ >= 1.0f) phase_ -= 1.0f;
            const float f = 300.0f * std::pow(10.0f, 0.5f + 0.5f * sin2pi(phase_));   // 300 .. 3000 Hz
            const float t = std::tan(kPi * f / rate_);
            const float a = (t - 1.0f) / (t + 1.0f);
            const uint32_t end = std::min(n, i0 + kSub);
            for (uint32_t i = i0; i < end; ++i) {
                const float w = wet.next();
                for (int ch = 0; ch < 2; ++ch) {
                    const float in = b[2 * i + ch];
                    float s = in + fb_[ch] * fbk;
                    for (int k = 0; k < 6; ++k) {
                        const float o = a * s + z_[ch][k];
                        z_[ch][k] = s - a * o;
                        s = o;
                    }
                    fb_[ch] = s;
                    const float o = 0.5f * (in + s);
                    b[2 * i + ch] = in * (1.0f - w) + o * w;
                }
                peak = std::max(peak, w);
            }
        }
        return peak;
    }

private:
    float rate_ = 44100.0f, phase_ = 0.0f;
    float z_[2][6] = {};
    float fb_[2] = {};
};

// ── Distortion ─────────────────────────────────────────────────────────────

class BitcrushFx final : public Effect {
public:
    BitcrushFx() : Effect(false) {}
    bool init(float rate) override { (void)rate; return true; }
    void start() override { acc_ = 1.0f; held_[0] = held_[1] = 0.0f; }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        (void)c; (void)gate; (void)dry;
        const float bits = 16.0f - clampf(x, 0.0f, 1.0f) * 12.0f;
        const float step = 2.0f / std::pow(2.0f, bits);
        const float inv = 1.0f / step;
        const float hold = 1.0f + clampf(y, 0.0f, 1.0f) * 31.0f;      // 1 .. 32 frames held
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            acc_ += 1.0f;
            if (acc_ >= hold) {
                acc_ -= hold;
                held_[0] = std::floor(b[2 * i] * inv + 0.5f) * step;
                held_[1] = std::floor(b[2 * i + 1] * inv + 0.5f) * step;
            }
            const float w = wet.next();
            b[2 * i]     = b[2 * i] * (1.0f - w) + held_[0] * w;
            b[2 * i + 1] = b[2 * i + 1] * (1.0f - w) + held_[1] * w;
            peak = std::max(peak, w);
        }
        return peak;
    }

private:
    float acc_ = 1.0f;
    float held_[2] = {};
};

class DriveFx final : public Effect {
public:
    DriveFx() : Effect(false) {}
    bool init(float rate) override { rate_ = rate; return true; }
    void start() override { tone_[0].z = tone_[1].z = 0.0f; }
    float render(float* b, uint32_t n, const Ctx& c, float x, float y, Ramp gate, Ramp wet, float dry) override
    {
        (void)c; (void)gate; (void)dry;
        const float gain = std::pow(10.0f, clampf(x, 0.0f, 1.0f) * 36.0f / 20.0f);   // 0 .. 36 dB
        const float comp = 1.0f / (0.5f + 0.5f * std::sqrt(gain));
        tone_[0].setHz(800.0f * std::pow(20.0f, clampf(y, 0.0f, 1.0f)), rate_);
        tone_[1].a = tone_[0].a;
        float peak = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            const float w = wet.next();
            for (int ch = 0; ch < 2; ++ch) {
                const float in = b[2 * i + ch];
                const float o = tone_[ch].lp(softClip(in * gain)) * comp;
                b[2 * i + ch] = in * (1.0f - w) + o * w;
            }
            peak = std::max(peak, w);
        }
        return peak;
    }

private:
    OnePole tone_[2];
    float rate_ = 44100.0f;
};

Effect* makeEffect(int fx)
{
    switch (fx) {
    case Echo:      return new (std::nothrow) EchoFx();
    case TapeEcho:  return new (std::nothrow) TapeEchoFx();
    case PingPong:  return new (std::nothrow) PingPongFx();
    case Roll:      return new (std::nothrow) RollFx();
    case Hall:      return new (std::nothrow) HallFx();
    case Shimmer:   return new (std::nothrow) ShimmerFx();
    case Freeze:    return new (std::nothrow) FreezeFx();
    case Gate:      return new (std::nothrow) GateFx();
    case LowPass:   return new (std::nothrow) Filter(FiltKind::Low);
    case HighPass:  return new (std::nothrow) Filter(FiltKind::High);
    case BandSweep: return new (std::nothrow) Filter(FiltKind::Band);
    case Wah:       return new (std::nothrow) Filter(FiltKind::Wah);
    case Flanger:   return new (std::nothrow) FlangerFx();
    case Phaser:    return new (std::nothrow) PhaserFx();
    case Bitcrush:  return new (std::nothrow) BitcrushFx();
    case Drive:     return new (std::nothrow) DriveFx();
    default:        return nullptr;
    }
}

/// A tail is over after this long under kQuiet, or after kMaxTailSec anyway.
constexpr float kQuiet = 2e-4f;
constexpr float kQuietSec = 0.4f;
constexpr float kMaxTailSec = 15.0f;

} // namespace

// ── The engine ─────────────────────────────────────────────────────────────

FxEngine::FxEngine()
{
    for (auto& s : snap_) s.store(false, std::memory_order_relaxed);
    /* As the defaults in the UI's table: the filters, the gate, ROLL and the
     * distortion stop at once; the delays and reverbs ring out. */
    for (int f : {Roll, Gate, LowPass, HighPass, BandSweep, Wah, Bitcrush, Drive}) {
        snap_[f].store(true, std::memory_order_relaxed);
    }
}

FxEngine::~FxEngine()
{
    for (auto* e : fx_) delete e;
}

void FxEngine::setSlot(int slot, int fx, float x, float y, float wet)
{
    if (slot < 0 || slot >= kSlots) return;
    SlotState& s = slot_[slot];
    s.x.store(clampf(x, 0.0f, 1.0f), std::memory_order_relaxed);
    s.y.store(clampf(y, 0.0f, 1.0f), std::memory_order_relaxed);
    s.wet.store(clampf(wet, 0.0f, 1.0f), std::memory_order_relaxed);
    s.fx.store((int8_t)(fx >= 0 && fx < kFxCount ? fx : None), std::memory_order_release);
}

void FxEngine::setSnap(int fx, bool snap)
{
    if (fx >= 0 && fx < kFxCount) snap_[fx].store(snap, std::memory_order_relaxed);
}

void FxEngine::onPrepare(uint32_t maxFrames, uint32_t sampleRate)
{
    (void)maxFrames;
    if (ready_ && sampleRate == rate_) return;
    ready_ = false;
    rate_ = sampleRate;
    for (int f = 0; f < kFxCount; ++f) {
        delete fx_[f];
        fx_[f] = makeEffect(f);
        if (!fx_[f] || !fx_[f]->init((float)sampleRate)) {
            delete fx_[f];
            fx_[f] = nullptr;
        }
    }
    ready_ = true;
}

void FxEngine::process(const float* in, float* out, uint32_t frames)
{
    const uint32_t samples = frames * 2;
    if (in != out) std::memcpy(out, in, samples * sizeof(float));
    if (!ready_ || !rate_) return;

    const Ctx ctx{(float)rate_, bpm_.load(std::memory_order_relaxed), sync_.load(std::memory_order_relaxed)};
    const float blockSec = (float)frames / (float)rate_;
    const float kParam = 1.0f - std::exp(-blockSec / 0.03f);   // X / Y / WET follow over ~30 ms
    const float kGate  = 1.0f - std::exp(-blockSec / 0.01f);
    const float kFade  = 1.0f - std::exp(-blockSec / 0.005f);   // SNAP: gone in ~30 ms

    if (kill_.exchange(false, std::memory_order_relaxed)) {
        for (auto* e : fx_) {
            if (e && e->state != Effect::State::Off) { e->state = Effect::State::Off; e->stop(); }
        }
    }
    if (mono_.load(std::memory_order_relaxed)) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float m = 0.5f * (out[2 * i] + out[2 * i + 1]);
            out[2 * i] = out[2 * i + 1] = m;
        }
    }
    for (auto* e : fx_) if (e) e->listen(out, frames);

    /* What the pads hold now, and each effect's state from it. */
    int held[kSlots];
    for (int s = 0; s < kSlots; ++s) held[s] = slot_[s].fx.load(std::memory_order_acquire);
    Effect* run[kFxCount];
    int nrun = 0;
    for (int f = 0; f < kFxCount; ++f) {
        Effect* e = fx_[f];
        if (!e) continue;
        int s = -1;
        for (int k = 0; k < kSlots; ++k) if (held[k] == f) s = k;
        if (s >= 0) {
            if (e->state == Effect::State::Off) {
                e->start();
                e->sx.snap(slot_[s].x.load(std::memory_order_relaxed));
                e->sy.snap(slot_[s].y.load(std::memory_order_relaxed));
                e->swet.snap(0.0f);
                e->sgate.snap(1.0f);
                e->stamp = ++stamp_;
            }
            e->state = Effect::State::On;
            e->quiet = 0;
            e->age = 0;
        } else if (e->state == Effect::State::On) {
            const bool tail = e->add() && !snap_[f].load(std::memory_order_relaxed);
            e->state = tail ? Effect::State::Tail : Effect::State::Fade;
            e->release();
        }
        if (e->state != Effect::State::Off) run[nrun++] = e;
    }
    /* In the order they were started. */
    std::sort(run, run + nrun, [](const Effect* a, const Effect* b) { return a->stamp < b->stamp; });

    const bool thru = thru_.load(std::memory_order_relaxed);
    if (!thru && nrun == 0) std::memset(out, 0, samples * sizeof(float));

    bool raw = true;           // the buffer is still the sources as they came
    uint32_t bits = 0;
    for (int i = 0; i < nrun; ++i) {
        Effect* e = run[i];
        int f = 0;
        while (fx_[f] != e) ++f;
        float tx = e->sx.value, ty = e->sy.value, twet = e->swet.value, tgate = 1.0f;
        if (e->state == Effect::State::On) {
            for (int k = 0; k < kSlots; ++k) {
                if (held[k] != f) continue;
                tx = slot_[k].x.load(std::memory_order_relaxed);
                ty = slot_[k].y.load(std::memory_order_relaxed);
                twet = slot_[k].wet.load(std::memory_order_relaxed);
            }
        } else if (e->state == Effect::State::Tail) {
            tgate = 0.0f;
        } else {
            twet = 0.0f;
        }
        /* With THRU off the sources are heard only through an effect: a
         * blend effect on them is all wet, an added one leaves out the dry. */
        if (!thru && raw && !e->add() && e->state == Effect::State::On) twet = 1.0f;
        const float dry = (thru || !raw) ? 1.0f : 0.0f;

        const float x = e->sx.step(tx, kParam);
        const float y = e->sy.step(ty, kParam);
        const float wet0 = e->swet.value;
        const float wet1 = e->swet.step(twet, e->state == Effect::State::Fade ? kFade : kParam);
        const float gate0 = e->sgate.value;
        const float gate1 = e->sgate.step(tgate, kGate);
        const float peak = e->render(out, frames, ctx, x, y, Ramp(gate0, gate1, frames), Ramp(wet0, wet1, frames), dry);
        raw = false;

        if (e->state == Effect::State::Tail) {
            e->age += frames;
            e->quiet = peak < kQuiet ? e->quiet + frames : 0;
            if (e->quiet > (uint32_t)(kQuietSec * rate_) || e->age > (uint32_t)(kMaxTailSec * rate_)) {
                e->state = Effect::State::Off;
                e->stop();
            }
        } else if (e->state == Effect::State::Fade && wet1 < 3e-3f) {
            e->state = Effect::State::Off;
            e->stop();
        }
        if (e->state != Effect::State::Off) bits |= 1u << f;
    }
    running_.store(bits, std::memory_order_relaxed);
}

} // namespace fxpad
