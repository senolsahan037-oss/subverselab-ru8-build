// RU-8 engine: plain C++17, no JUCE. The raw render tool and the plug-in run this same code.
//
// Ported behaviour for behaviour from the web prototype (prototype/web/ru8-water.html, Tone.js 14.7.77 on Chrome's Web Audio),
// which is the reference for every sound and level:
//   - Tone envelopes: linear attack (from the current value), then exponentialApproachValueAtTime(0, decay):
//     setTarget with time constant ln(decay + 1) / ln(200) until 0.9·decay, held there, then a linear ramp to 0 at decay;
//     the oscillator or noise source stops at attack + decay.
//   - MembraneSynth pitch: f·octaves → f, an exponential ramp over pitchDecay (Tone multiplies by octaves, it does not use 2^octaves).
//   - Oscillators: Chrome's band-limited tables (1/3-octave ranges, partials culled per range, normalised by the full table's peak).
//   - Noise: Tone's buffers are stereo; a pad channel takes one channel (Tone.Channel, channelCount 1), so the noise is the
//     average of two independent sources. Pink is Paul Kellet's filter as Tone generates it.
//   - Filters: Web Audio biquads (lowpass / highpass Q in dB, bandpass / peaking Q linear).
//   - Channel: mono in, equal-power pan, level. Bus: Color (4096-point shaper, 2x oversampled) → low-pass → glue compressor
//     (Chrome's DynamicsCompressor, see below) → master volume → limiter (-1 dBFS, 4x true-peak detection).
//
// Real time: prepare() allocates; process() never allocates, locks or does I/O. Hits start at their exact sample inside the
// block, and every time-dependent stage is aligned to the absolute sample count, so the output does not depend on block size.
//
// The glue compressor follows the algorithm of Chromium's DynamicsCompressor
// (third_party/blink/renderer/platform/audio/dynamics_compressor.cc), which carries this notice:
//   Copyright (C) 2011 Google Inc. All rights reserved.
//   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
//   following conditions are met: 1. Redistributions of source code must retain the above copyright notice, this list of
//   conditions and the following disclaimer. 2. Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation and/or other materials provided with the
//   distribution. 3. Neither the name of Apple Computer, Inc. ("Apple") nor the names of its contributors may be used to
//   endorse or promote products derived from this software without specific prior written permission.
//   THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
//   SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
//   BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//   POSSIBILITY OF SUCH DAMAGE.
#pragma once
#include "RU8KitData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <cstdint>
#include <memory>
#include <vector>

namespace ru8 {

constexpr double kPi = 3.14159265358979323846;
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float orDefault (float v, float def) { return std::isnan (v) ? def : v; }
inline float semi (float st) { return std::pow (2.0f, st / 12.0f); }

// ---------------------------------------------------------------- random (deterministic)
struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    void seed (uint64_t v)
    {   // splitmix64, so that neighbouring seeds give unrelated sequences from the first sample on
        uint64_t z = v + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; z ^= z >> 31;
        s = z ? z : 0x9E3779B97F4A7C15ull;
    }
    float uni()   // [-1, 1)
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        const uint64_t r = s * 0x2545F4914F6CDD1Dull;
        return (float) ((double) (r >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0);
    }
};

// Tone's noise as a pad channel hears it: the mean of two independent channels; pink by Kellet's filter (Tone's coefficients).
// A noise start between two samples (the clap's later bursts, rolls) makes Chrome read the buffer with linear interpolation
// at that fraction for the whole hit: a gentle top cut (up to -1.8 dB at 8–16 kHz, -4.5 dB above). Starts on a sample: none.
struct NoiseGen
{
    Rng a, b;
    float p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0;
    float alpha = 0, prevRaw = 0;
    void restart (double startSample) { alpha = (float) (std::ceil (startSample) - startSample); }
    void seed (uint64_t v) { a.seed (v * 2 + 1); b.seed (v * 2 + 2); }
    float white() { return 0.5f * (a.uni() + b.uni()); }
    float pink()
    {
        const float e = white();
        p0 = 0.99886f * p0 + 0.0555179f * e;  p1 = 0.99332f * p1 + 0.0750759f * e;
        p2 = 0.969f * p2 + 0.153852f * e;     p3 = 0.8665f * p3 + 0.3104856f * e;
        p4 = 0.55f * p4 + 0.5329522f * e;     p5 = -0.7616f * p5 - 0.016898f * e;
        const float o = (p0 + p1 + p2 + p3 + p4 + p5 + p6 + 0.5362f * e) * 0.11f;
        p6 = 0.115926f * e;
        return o;
    }
    float next (Noise t)
    {
        const float raw = t == Noise::pink ? pink() : white();
        const float y = (1 - alpha) * prevRaw + alpha * raw;
        prevRaw = raw;
        return y;
    }
};

// ---------------------------------------------------------------- band-limited tables (as Chrome's PeriodicWave)
class WaveTables
{
public:
    void build (Wave shape, double sampleRate)
    {
        size = sampleRate <= 24000 ? 2048 : (sampleRate <= 88200 ? 4096 : 16384);
        ranges = (int) std::lround (3.0 * std::log2 ((double) size));
        lowest = (float) (sampleRate / size);
        rateScale = (double) size / sampleRate;
        tables.assign ((size_t) ranges, std::vector<float> ((size_t) size, 0.0f));
        const int half = size / 2;
        std::vector<double> b ((size_t) half, 0.0);
        for (int n = 1; n < half; ++n)
        {
            const double pf = 2.0 / (n * kPi);
            switch (shape)
            {
                case Wave::sine:     b[(size_t) n] = n == 1 ? 1 : 0; break;
                case Wave::square:   b[(size_t) n] = (n & 1) ? 2 * pf : 0; break;
                case Wave::sawtooth: b[(size_t) n] = pf * ((n & 1) ? 1 : -1); break;
                case Wave::triangle: b[(size_t) n] = (n & 1) ? 2 * pf * pf * ((((n - 1) >> 1) & 1) ? -1 : 1) : 0; break;
            }
        }
        double norm = 0.5;
        std::vector<std::complex<double>> spec ((size_t) size);
        for (int r = 0; r < ranges; ++r)
        {
            const double cull = std::pow (2.0, -(r * 400.0) / 1200.0);
            const int partials = (int) (float) (cull * half);
            std::fill (spec.begin(), spec.end(), std::complex<double> (0, 0));
            for (int n = 1; n < std::min (half, partials + 1); ++n)
            {   // x[k] = sum b_n sin(2 pi n k / N): conjugate-symmetric spectrum of an imaginary sine series
                spec[(size_t) n] = std::complex<double> (0, -0.5 * b[(size_t) n]);
                spec[(size_t) (size - n)] = std::complex<double> (0, 0.5 * b[(size_t) n]);
            }
            fft (spec, true);
            auto& t = tables[(size_t) r];
            for (int k = 0; k < size; ++k) t[(size_t) k] = (float) spec[(size_t) k].real();
            if (r == 0)
            {
                float mx = 0; for (float v : t) mx = std::max (mx, std::fabs (v));
                if (mx > 0) norm = 1.0 / mx;
            }
            for (auto& v : t) v = (float) (v * norm);
        }
    }

    // One sample at a read position (in table samples) for a fundamental frequency, as Chrome's oscillator reads it.
    float read (double index, float freq) const
    {
        // Chrome (PeriodicWave::WaveDataForFundamentalFrequency): a negative frequency uses the table of |f|; only 0 Hz takes
        // the richest table. FM carriers swing below 0 Hz half of the time.
        const float ratio = freq != 0 ? std::fabs (freq) / lowest : 0.5f;
        const float pitchRange = std::clamp (1.0f + std::log2 (ratio) * 1200.0f / 400.0f, 0.0f, (float) (ranges - 1));
        const int r1 = (int) pitchRange, r2 = r1 < ranges - 1 ? r1 + 1 : r1;
        const float tf = pitchRange - (float) r1;
        const int i1 = (int) index, i2 = (i1 + 1) & (size - 1);
        const float f = (float) (index - i1);
        const auto& hi = tables[(size_t) r1];
        const auto& lo = tables[(size_t) r2];
        const float sh = (1 - f) * hi[(size_t) i1] + f * hi[(size_t) i2];
        const float sl = (1 - f) * lo[(size_t) i1] + f * lo[(size_t) i2];
        return (1 - tf) * sh + tf * sl;
    }
    int size = 4096, ranges = 36;
    float lowest = 11.71875f;
    double rateScale = 4096.0 / 48000.0;

private:
    std::vector<std::vector<float>> tables;
    static void fft (std::vector<std::complex<double>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2 * kPi / (double) len * (inverse ? 1 : -1);
            const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1, 0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
                }
            }
        }
    }
};

struct Tables
{
    WaveTables tri, sq, saw;
    void build (double sr) { tri.build (Wave::triangle, sr); sq.build (Wave::square, sr); saw.build (Wave::sawtooth, sr); }
};

// An oscillator node: starts at phase 0 when started, frequency per sample.
struct Osc
{
    const Tables* tables = nullptr;
    Wave wave = Wave::sine;
    double index = 0;   // read position in table samples
    float tick (float freq)
    {
        // Chrome: beyond Nyquist an oscillator outputs 0 while its phase moves on at the clamped frequency (half a turn a
        // sample); FM carriers swing far past it
        const float nyq = (float) (0.5 * tables->sq.size / tables->sq.rateScale);
        if (std::fabs (freq) > nyq)
        {
            index += (double) (freq > 0 ? nyq : -nyq) * tables->sq.rateScale;
            const double n = tables->sq.size;
            if (index >= n) index -= n * std::floor (index / n);
            if (index < 0) index += n * std::ceil (-index / n);
            return 0.0f;
        }
        float v;
        if (wave == Wave::sine)
        {
            v = (float) std::sin (2 * kPi * index / tables->sq.size);
        }
        else
        {
            const WaveTables& t = wave == Wave::square ? tables->sq : (wave == Wave::triangle ? tables->tri : tables->saw);
            v = t.read (index, freq);
        }
        index += (double) freq * tables->sq.rateScale;
        const double n = tables->sq.size;
        if (index >= n) index -= n * std::floor (index / n);
        if (index < 0) index += n * std::ceil (-index / n);
        return v;
    }
    void restart() { index = 0; }
};

// ---------------------------------------------------------------- biquad (Web Audio formulas)
struct Biquad
{
    enum Type { lowpass, highpass, bandpass, peaking };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set (Type type, double freq, double q, double gainDb, double sr)
    {
        const double nyq = sr / 2, f = std::clamp (freq / nyq, 0.0, 1.0);
        if (type == lowpass && f >= 1) { setRaw (1, 0, 0, 1, 0, 0); return; }
        if (type == highpass && f >= 1) { setRaw (0, 0, 0, 1, 0, 0); return; }
        const double w0 = kPi * f, cw = std::cos (w0), sw = std::sin (w0);
        switch (type)
        {
            case lowpass:
            {
                const double alpha = 0.5 * sw * std::pow (10.0, -0.05 * q);
                setRaw ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
                break;
            }
            case highpass:
            {
                const double alpha = 0.5 * sw * std::pow (10.0, -0.05 * q);
                setRaw ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
                break;
            }
            case bandpass:
            {
                const double alpha = sw / (2 * std::max (q, 1e-4));
                setRaw (alpha, 0, -alpha, 1 + alpha, -2 * cw, 1 - alpha);
                break;
            }
            case peaking:
            {
                const double A = std::pow (10.0, gainDb / 40), alpha = sw / (2 * std::max (q, 1e-4));
                setRaw (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
                break;
            }
        }
    }
    void setRaw (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    float tick (float xin)
    {
        const double x = xin, y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return (float) y;
    }
    void reset() { x1 = x2 = y1 = y2 = 0; }
    void copyCoeffs (const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
};

// ---------------------------------------------------------------- Tone.Envelope (sustain 0)
// Attack: linear (Synth, NoiseSynth, MetalSynth) or "exponential" (MembraneSynth's default): exponentialApproachValueAtTime
// toward the velocity with time constant ln(attack + 1) / ln(200), held at 0.9·attack, then linear to the velocity at attack.
// Decay: the same approach toward 0 over the decay time.
struct ToneEnv
{
    double start = -1;           // absolute sample of the trigger
    double atk = 0, dec = 0;     // seconds (attack already shortened for a retrigger)
    float vel = 0, v0 = 0, tc = 1, vHold = 0, atc = 1, aHold = 0;
    double sr = 48000;
    bool active = false, expAttack = false;

    float valueAt (double n) const
    {
        if (! active) return 0;
        const double t = (n - start) / sr;
        if (t < 0) return 0;
        if (t < atk)
        {
            if (! expAttack) return (float) (v0 + (vel - v0) * t / atk);
            if (t < 0.9 * atk) return (float) (vel + (v0 - vel) * std::exp (-t / atc));
            return (float) (aHold + (vel - aHold) * (t - 0.9 * atk) / (0.1 * atk));
        }
        const double td = t - atk;
        if (td < 0.9 * dec) return (float) (vel * std::exp (-td / tc));
        if (td < dec) return (float) (vHold * (1.0 - (td - 0.9 * dec) / (0.1 * dec)));
        return 0;
    }
    // A trigger at absolute sample n: Tone ramps from the current value; a running envelope shortens the attack by (1 - value).
    void trigger (double n, float velocity, double attack, double decay)
    {
        const float cur = active ? valueAt (n) : 0.0f;
        double a = attack;
        if (cur > 0) a = (1 - cur) * attack;
        start = n; vel = velocity; dec = decay; v0 = cur;
        atk = a < 1.0 / sr ? 0 : a;   // shorter than a sample: Tone sets the value at once
        tc = (float) (std::log (decay + 1) / std::log (200.0));
        atc = (float) std::max (1e-9, std::log (atk + 1) / std::log (200.0));
        aHold = (float) (vel + (v0 - vel) * std::exp (-0.9 * atk / atc));
        vHold = (float) (velocity * std::exp (-0.9 * decay / tc));
        active = true;
    }
    double stopAt() const { return start + (atk + dec) * sr; }   // where Tone stops the source
};

// ---------------------------------------------------------------- Tone voices
// The frequency a Tone synth's oscillator is driven with: constant, or a MembraneSynth's f·octaves → f exponential ramp.
struct FreqCurve
{
    double start = -1e18, sr = 48000;
    float f = 440, octaves = 1, pitchDecay = 0.05f;
    bool membrane = false;
    float at (double n) const
    {
        if (! membrane || n < start) return f;
        const double t = (n - start) / sr;
        if (t >= pitchDecay) return f;
        const double f0 = (double) f * octaves;
        return (float) (f0 * std::pow ((double) f / f0, t / pitchDecay));
    }
};

// Tone.Synth / MembraneSynth: an oscillator restarted at each hit, an envelope, the synth's volume.
// Chrome quirk kept on purpose (it is in every reference): an oscillator started m samples into a 128-sample render quantum
// reads its a-rate frequency m samples late until that quantum ends, so the first moments of a hit run at the frequency the
// synth had before (440 Hz for a fresh synth). The quantum grid is the engine's absolute sample count, so the result does
// not depend on the host's block size.
struct ToneSynth
{
    Osc osc; ToneEnv env;
    float volume = 1;
    float attack = 0.001f;
    bool membrane = false;
    float octaves = 1, pitchDecay = 0.05f;
    FreqCurve cur, prev;
    double stop = -1, quantumEnd = -1;
    int lag = 0;
    double sr = 48000;
    void prepare (const Tables* t, double rate) { osc.tables = t; sr = rate; env.sr = rate; cur = prev = FreqCurve{}; cur.sr = prev.sr = rate; }
    // A hit while the oscillator still runs (before its scheduled stop) keeps it: Tone cancels the stop instead of starting
    // a new oscillator, so the phase carries on and there is no start lag; only the envelope and the frequency restart.
    void trigger (double n, float freq, float vel, float decay)
    {
        const bool running = env.active && n < stop;
        prev = cur;
        cur.start = n; cur.f = freq; cur.octaves = octaves; cur.pitchDecay = pitchDecay; cur.membrane = membrane; cur.sr = sr;
        env.trigger (n, vel, attack, decay);
        stop = env.stopAt();
        if (running) { lag = 0; return; }
        osc.restart();
        lag = (int) ((int64_t) n & 127);
        quantumEnd = n - lag + 128;
    }
    float freqAt (double n) const
    {
        if (lag > 0 && n < quantumEnd)
        {
            const double x = n - lag;
            return x < cur.start ? prev.at (x) : cur.at (x);
        }
        return cur.at (n);
    }
    float tick (double n)
    {
        if (! env.active || n < env.start) return 0;
        if (n >= stop) { env.active = false; return 0; }
        const float s = osc.tick (freqAt (n));
        return s * env.valueAt (n) * volume;
    }
};

// Tone.NoiseSynth: the noise source starts at the hit and stops at attack + decay. A retrigger while it plays starts a new
// source but leaves the old one running to its own stop (Tone restarts the source, the earlier stop stays scheduled), so
// for a moment two noises sum under the envelope (+3 dB): the tambourine's second strike, snare rolls.
struct ToneNoise
{
    static constexpr int kSources = 4;
    NoiseGen gen[kSources]; double srcStop[kSources] { -1, -1, -1, -1 }; int next = 0;
    ToneEnv env;
    Noise type = Noise::white;
    float volume = 1, attack = 0.001f;
    double stop = -1;
    void prepare (double rate, uint64_t seedValue) { env.sr = rate; seed (seedValue); }
    void seed (uint64_t v) { for (int i = 0; i < kSources; ++i) gen[i].seed (v * kSources + (uint64_t) i); }
    void trigger (double n, float vel, float decay)
    {
        env.trigger (n, vel, attack, decay);
        stop = env.stopAt();
        auto& g = gen[next];
        g.restart (n); srcStop[next] = stop;
        next = (next + 1) % kSources;
    }
    float tick (double n)
    {
        if (! env.active || n < env.start) return 0;
        float sum = 0; bool any = false;
        for (int i = 0; i < kSources; ++i)
            if (n < srcStop[i]) { sum += gen[i].next (type); any = true; }
        if (! any) { env.active = false; return 0; }
        return sum * env.valueAt (n) * volume;
    }
};

// A Web Audio gain param driven by cancelAndHold / linearRamp / setTarget (the hats' VCAs).
struct GainParam
{
    double sr = 48000;
    // segment: linear from (t0, v0) to (t1, v1), then exponential approach to `target` with time constant tau from t1
    double t0 = 0, t1 = 0; float v0 = 0, v1 = 0, target = 0, tau = 1;
    float valueAt (double n) const
    {
        if (n < t0) return v0;
        if (n < t1) return (float) (v0 + (v1 - v0) * (n - t0) / (t1 - t0));
        return (float) (target + (v1 - target) * std::exp (-(n - t1) / (sr * tau)));
    }
    void rampThenDecay (double n, float peak, double rampSec, float decayTau)
    {
        const float held = valueAt (n);
        t0 = n; v0 = held; t1 = n + rampSec * sr; v1 = peak; target = 0; tau = decayTau;
    }
    void decayFrom (double n, float decayTau)
    {
        const float held = valueAt (n);
        t0 = t1 = n; v0 = v1 = held; target = 0; tau = decayTau;
    }
};

// ---------------------------------------------------------------- pads
enum Pad { BD = 0, SD, CP, RS, CH, OH, LT, HT, P1, P2, P3, P4, P5, P6, P7, P8 };

struct PadState { float levelDb = 0, tune = 0, decay = 1, pan = 0, rev = 0, dly = 0; bool mute = false; };

struct Kick
{
    ToneSynth body; ToneNoise click; Biquad hp;
    void prepare (const Tables* t, double sr)
    {
        body.prepare (t, sr); body.membrane = true; body.attack = 0.001f; body.env.expAttack = true;
        click.prepare (sr, 101); click.attack = 0.0005f; click.type = Noise::white;
        hp.set (Biquad::highpass, 2500, 1, 0, sr);
    }
    void trigger (const KitDef& k, double n, float vel, const PadState& p)
    {
        body.osc.wave = k.bd.wave; body.octaves = k.bd.oct; body.pitchDecay = k.bd.pd; body.volume = 1;
        click.volume = dbToGain (k.bd.clickDb);
        body.trigger (n, k.bd.f * semi (p.tune), vel, k.bd.dec * p.decay);
        click.trigger (n, vel, 0.006f);
    }
    float tick (double n) { return body.tick (n) + hp.tick (click.tick (n)); }
};

struct Snare
{
    ToneSynth body, body2; ToneNoise noise; Biquad hp, pk;
    double sr = 48000; float hpF = -1, pkF = -1;
    void prepare (const Tables* t, double rate)
    {
        sr = rate;
        body.prepare (t, sr); body.membrane = true; body.pitchDecay = 0.015f; body.osc.wave = Wave::triangle; body.attack = 0.001f; body.env.expAttack = true;
        body2.prepare (t, sr); body2.osc.wave = Wave::sine; body2.attack = 0.001f;
        noise.prepare (sr, 202); noise.attack = 0.001f;
    }
    void trigger (const KitDef& k, double n, float vel, const PadState& p)
    {
        const auto& s = k.sd;
        const float m = semi (p.tune);
        body.octaves = s.oct; body.volume = dbToGain (s.bodyDb); body2.volume = dbToGain (s.body2Db);
        noise.volume = dbToGain (s.nvolDb); noise.type = s.noise;
        if (hpF != s.hp) { hpF = s.hp; hp.set (Biquad::highpass, s.hp, 1, 0, sr); }
        if (pkF != s.pk) { pkF = s.pk; pk.set (Biquad::peaking, s.pk, 0.8, 3, sr); }
        body.trigger (n, s.f * m, vel, s.dec * p.decay);
        body2.trigger (n, s.f2 * m, vel, s.dec * 0.7f * p.decay);
        noise.trigger (n, vel, s.ndec * p.decay);
    }
    float tick (double n) { return body.tick (n) + body2.tick (n) + pk.tick (hp.tick (noise.tick (n))); }
};

// Six free-running squares → band-pass → high-pass (two stages) → two VCAs; the closed hat chokes the open one.
struct Hats
{
    Osc osc[6]; float freq[6] {};
    Biquad bp, hp1, hp2; GainParam chAmp, ohAmp;
    double sr = 48000; float bpF = -1, hpF = -1;
    void prepare (const Tables* t, double rate)
    {
        sr = rate;
        for (auto& o : osc) { o.tables = t; o.wave = Wave::square; o.restart(); }
        chAmp.sr = ohAmp.sr = rate;
    }
    void setKit (const KitDef& k)
    {
        for (int i = 0; i < 6; ++i) freq[i] = k.hh.freqs[i];
        if (bpF != k.hh.bp) { bpF = k.hh.bp; bp.set (Biquad::bandpass, k.hh.bp, 0.9, 0, sr); }
        if (hpF != k.hh.hp) { hpF = k.hh.hp; hp1.set (Biquad::highpass, k.hh.hp, 1, 0, sr); hp2.copyCoeffs (hp1); }
    }
    void tune (const KitDef& k, float st) { const float m = semi (st); for (int i = 0; i < 6; ++i) freq[i] = k.hh.freqs[i] * m; }
    void triggerClosed (const KitDef& k, double n, float vel, const PadState& p)
    {
        tune (k, p.tune);
        ohAmp.decayFrom (n, 0.006f);
        chAmp.rampThenDecay (n, vel * 2.2f, 0.0015, k.hh.ch * p.decay / 4.5f);
    }
    void triggerOpen (const KitDef& k, double n, float vel, const PadState& p)
    {
        tune (k, p.tune);
        ohAmp.rampThenDecay (n, vel * 2.2f, 0.0015, k.hh.oh * p.decay / 4.5f);
    }
    // the shared source runs always (its phase is free), the two VCAs read it
    void tick (double n, float& outCh, float& outOh)
    {
        float sum = 0;
        for (int i = 0; i < 6; ++i) sum += osc[i].tick (freq[i]);
        const float x = hp2.tick (hp1.tick (bp.tick (sum * 0.5011872f * 0.5f)));   // each square at -6 dB, summed into a gain of 0.5
        outCh = x * chAmp.valueAt (n);
        outOh = x * ohAmp.valueAt (n);
    }
};

// ---------------------------------------------------------------- bus
// Chrome's DynamicsCompressor (see the notice at the top): 6 ms pre-delay, soft knee, adaptive release, makeup gain.
class Compressor
{
public:
    void prepare (double rate)
    {
        sr = (float) rate;
        preFrames = (int) (0.006f * sr);
        for (auto& b : buf) b.assign (kMax, 0.0f);
        reset();
    }
    void reset()
    {
        detectorAverage = 0; compressorGain = 1; maxAttackDiff = -1;
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        readIdx = 0; writeIdx = preFrames;
    }
    void setParams (float thresholdDb, float kneeDb, float ratio, float attack, float release)
    {
        thr = thresholdDb; knee = kneeDb; rat = ratio; att = attack; rel = release;
        updateCurve();
    }
    // One frame. `absFrame` is the absolute sample index; the envelope rate is updated every 32 frames as Chrome does.
    void tick (float& l, float& r, int64_t absFrame)
    {
        if ((absFrame & 31) == 0) beginDivision();
        buf[0][(size_t) writeIdx] = l; buf[1][(size_t) writeIdx] = r;
        const float in = std::max (std::fabs (l), std::fabs (r));
        const float shaped = saturate (in, k);
        const float attenuation = in <= 0.0001f ? 1 : shaped / in;
        const float dbAtt = std::max (2.0f, -lin2db (attenuation));
        const float satReleaseRate = db2lin (dbAtt / (0.0025f * sr)) - 1;
        const float rate = attenuation > detectorAverage ? satReleaseRate : 1;
        detectorAverage += (attenuation - detectorAverage) * rate;
        detectorAverage = std::min (1.0f, detectorAverage);
        if (! std::isfinite (detectorAverage)) detectorAverage = 1;
        if (envelopeRate < 1) compressorGain += (scaledDesiredGain - compressorGain) * envelopeRate;
        else { compressorGain *= envelopeRate; compressorGain = std::min (1.0f, compressorGain); }
        const float postWarp = (float) std::sin ((double) (float) (kPi / 2) * compressorGain);
        const float total = postGain * postWarp;
        l = buf[0][(size_t) readIdx] * total; r = buf[1][(size_t) readIdx] * total;
        readIdx = (readIdx + 1) & (kMax - 1); writeIdx = (writeIdx + 1) & (kMax - 1);
        lastGainDb = lin2db (postWarp);
    }
    float gainReductionDb() const { return lastGainDb; }
    int latency() const { return preFrames; }

private:
    static constexpr int kMax = 1024;
    static float db2lin (float db) { return std::pow (10.0f, 0.05f * db); }
    static float lin2db (float x) { return 20 * std::log10 (x); }
    float kneeCurve (float x, float kk) const
    {
        if (x < linThr) return x;
        return linThr + (1 - (float) std::exp ((double) (-kk * (x - linThr)))) / kk;
    }
    float saturate (float x, float kk) const
    {
        if (x < kneeThr) return kneeCurve (x, kk);
        return db2lin (dbYKneeThr + slope * (lin2db (x) - dbKneeThr));
    }
    float kAtSlope (float desired) const
    {
        const float dbX = thr + knee, x = db2lin (dbX);
        float x2 = 1, dbX2 = 0;
        if (! (x < linThr)) { x2 = x * 1.001f; dbX2 = lin2db (x2); }
        float minK = 0.1f, maxK = 10000, kk = 5, s = 1;
        for (int i = 0; i < 15; ++i)
        {
            if (! (x < linThr)) s = (lin2db (kneeCurve (x2, kk)) - lin2db (kneeCurve (x, kk))) / (dbX2 - dbX);
            if (s < desired) maxK = kk; else minK = kk;
            kk = std::sqrt (minK * maxK);
        }
        return kk;
    }
    void updateCurve()
    {
        linThr = db2lin (thr); slope = 1 / rat;
        k = kAtSlope (1 / rat);
        dbKneeThr = thr + knee; kneeThr = db2lin (dbKneeThr);
        dbYKneeThr = lin2db (kneeCurve (kneeThr, k));
        postGain = std::pow (1 / saturate (1, k), 0.6f);
        const float releaseFrames = sr * rel;
        constexpr float z1 = 0.09f, z2 = 0.16f, z3 = 0.42f, z4 = 0.98f;
        ra = releaseFrames * (0.9999999999999998f * z1 + 1.8432219684323923e-16f * z2 - 1.9373394351676423e-16f * z3 + 8.824516011816245e-18f * z4);
        rb = releaseFrames * (-1.5788320352845888f * z1 + 2.3305837032074286f * z2 - 0.9141194204840429f * z3 + 0.1623677525612032f * z4);
        rc = releaseFrames * (0.5334142869106424f * z1 - 1.272736789213631f * z2 + 0.9258856042207512f * z3 - 0.18656310191776226f * z4);
        rd = releaseFrames * (0.08783463138207234f * z1 - 0.1694162967925622f * z2 + 0.08588057951595272f * z3 - 0.00429891410546283f * z4);
        re = releaseFrames * (-0.042416883008123074f * z1 + 0.1115693827987602f * z2 - 0.09764676325265872f * z3 + 0.028494263462021576f * z4);
        attackFrames = std::max (0.001f, att) * sr;
    }
    void beginDivision()
    {
        if (! std::isfinite (detectorAverage)) detectorAverage = 1;
        const float desired = detectorAverage;
        scaledDesiredGain = std::asin (desired) / (float) (kPi / 2);
        const bool releasing = scaledDesiredGain > compressorGain;
        float diff = scaledDesiredGain == 0 ? (releasing ? -1.0f : 1.0f) : lin2db (compressorGain / scaledDesiredGain);
        if (releasing)
        {
            maxAttackDiff = -1;
            if (! std::isfinite (diff)) diff = -1;
            float x = std::clamp (diff, -12.0f, 0.0f);
            x = 0.25f * (x + 12);
            const float x2 = x * x, x3 = x2 * x, x4 = x2 * x2;
            const float frames = ra + rb * x + rc * x2 + rd * x3 + re * x4;
            envelopeRate = db2lin (5.0f / frames);
        }
        else
        {
            if (! std::isfinite (diff)) diff = 1;
            if (maxAttackDiff == -1 || maxAttackDiff < diff) maxAttackDiff = diff;
            const float eff = std::max (0.5f, maxAttackDiff);
            envelopeRate = 1 - std::pow (0.25f / eff, 1 / attackFrames);
        }
    }
    float sr = 48000, thr = -24, knee = 30, rat = 12, att = 0.003f, rel = 0.25f;
    float linThr = 0, slope = 1, k = 5, dbKneeThr = 0, kneeThr = 1, dbYKneeThr = 0, postGain = 1;
    float ra = 0, rb = 0, rc = 0, rd = 0, re = 0, attackFrames = 1;
    float detectorAverage = 0, compressorGain = 1, maxAttackDiff = -1, scaledDesiredGain = 0, envelopeRate = 0, lastGainDb = 0;
    std::array<std::vector<float>, 2> buf;
    int preFrames = 288, readIdx = 0, writeIdx = 288;
};

// Color: tape (tanh, level-matched at -6 dBFS) and crush (bits) as one 4096-point curve, read with linear interpolation,
// run at 2x as the prototype's WaveShaper (oversample '2x') does. The 2x filters have the same design as Chrome's: up, a
// 128-tap Blackman-windowed sinc for the odd samples (even samples are the input delayed by 64); down, a 256-tap half-band
// (odd taps from a windowed sinc, centre tap 0.5). Latency 128 samples, flat to about 21 kHz at 48 kHz.
class Shaper2x
{
public:
    void prepare()
    {
        const double a0 = 0.5 * (1 - 0.16), a1 = 0.5, a2 = 0.5 * 0.16;
        for (int i = 0; i < kUp; ++i)
        {
            const double s = kPi * (i - kUp / 2 + 0.5);
            const double x = (i + 0.5) / kUp;
            up[(size_t) i] = (float) ((s == 0 ? 1.0 : std::sin (s) / s) * (a0 - a1 * std::cos (2 * kPi * x) + a2 * std::cos (4 * kPi * x)));
        }
        for (int i = 1; i < kDown; i += 2)
        {
            const double s = 0.5 * kPi * (i - kDown / 2);
            const double x = (double) i / kDown;
            down[(size_t) (i - 1) / 2] = (float) (0.5 * (s == 0 ? 1.0 : std::sin (s) / s) * (a0 - a1 * std::cos (2 * kPi * x) + a2 * std::cos (4 * kPi * x)));
        }
        reset();
        setCurve (0, 0);
    }
    void reset() { for (auto& c : ch) { c.in.fill (0); c.odd.fill (0); c.even.fill (0); c.inPos = c.oddPos = c.evenPos = 0; } }
    void setCurve (float tape, float crush)
    {
        const float kk = 1 + 4 * tape, norm = std::tanh (kk * 0.5f) / 0.5f;
        const float steps = crush > 0.001f ? std::pow (2.0f, std::round (15 - 9 * crush) - 1) : 0;
        for (int i = 0; i < kLen; ++i)
        {
            const float x = (float) i / (kLen - 1) * 2 - 1;
            float y = tape > 0.001f ? std::tanh (kk * x) / norm : x;
            if (steps > 0) y = std::round (y * steps) / steps;
            curve[(size_t) i] = std::clamp (y, -1.0f, 1.0f);
        }
    }
    // Tone.Distortion's curve (distortion d): (3 + 100 d)·x·20°/(pi + 100 d·|x|), zero below |x| = 0.001
    void setDistortion (float d)
    {
        const double e = 100.0 * d, deg = kPi / 180;
        for (int i = 0; i < kLen; ++i)
        {
            const double x = (double) i / (kLen - 1) * 2 - 1;
            curve[(size_t) i] = std::fabs (x) < 0.001 ? 0.0f : (float) ((3 + e) * x * 20 * deg / (kPi + e * std::fabs (x)));
        }
    }
    float tick (int c, float x)
    {
        auto& s = ch[(size_t) c];
        // up: history of the input; even = input 64 samples ago, odd = the 128-tap interpolation
        s.inPos = (s.inPos + kUp - 1) % kUp;
        s.in[(size_t) s.inPos] = x; s.in[(size_t) (s.inPos + kUp)] = x;
        float odd = 0;
        for (int i = 0; i < kUp; ++i) odd += up[(size_t) i] * s.in[(size_t) (s.inPos + i)];
        const float even = s.in[(size_t) (s.inPos + kUp / 2)];
        // shape both 2x samples (u[2m] = even, u[2m+1] = odd)
        const float ue = shape (even), uo = shape (odd);
        // down: out = sum rk[j] * u[2m - 1 - 2j] + 0.5 * u[2m - 128]; keep the odd and the even 2x samples apart
        s.oddPos = (s.oddPos + kHalf - 1) % kHalf;           // newest odd sample is u[2m - 1] = the previous uo
        s.odd[(size_t) s.oddPos] = s.lastOdd; s.odd[(size_t) (s.oddPos + kHalf)] = s.lastOdd;
        s.lastOdd = uo;
        float acc = 0;
        for (int j = 0; j < kHalf; ++j) acc += down[(size_t) j] * s.odd[(size_t) (s.oddPos + j)];
        s.evenPos = (s.evenPos + kEven - 1) % kEven;         // even history: u[2m], u[2m - 2], ... ; u[2m - 128] is 64 back
        s.even[(size_t) s.evenPos] = ue; s.even[(size_t) (s.evenPos + kEven)] = ue;
        return acc + 0.5f * s.even[(size_t) (s.evenPos + 64)];
    }

private:
    static constexpr int kUp = 128, kDown = 256, kHalf = kDown / 2, kEven = 65, kLen = 4096;
    float shape (float x) const
    {
        const float v = (kLen - 1) * 0.5f * (x + 1);
        if (v <= 0) return curve[0];
        if (v >= kLen - 1) return curve[kLen - 1];
        const int i = (int) v; const float f = v - (float) i;
        return curve[(size_t) i] + f * (curve[(size_t) i + 1] - curve[(size_t) i]);
    }
    struct Chan
    {
        std::array<float, 2 * kUp> in {}; std::array<float, 2 * kHalf> odd {}; std::array<float, 2 * kEven> even {};
        int inPos = 0, oddPos = 0, evenPos = 0; float lastOdd = 0;
    };
    std::array<Chan, 2> ch;
    std::array<float, kUp> up {};
    std::array<float, kHalf> down {};
    std::array<float, kLen> curve {};
};

using ColorShaper = Shaper2x;

// ---------------------------------------------------------------- the other voices
// Clap: three short noise bursts (gap, 2.1·gap) and a tail at 2.8·gap, through a band-pass (tuned) and a high-pass at 600 Hz.
struct Clap
{
    ToneNoise burst, tail; Biquad bp, hp;
    double sr = 48000;
    struct Pending { double at; float vel; bool isTail; } pend[4] {}; int numPend = 0;
    void prepare (double rate)
    {
        sr = rate;
        burst.prepare (sr, 303); burst.attack = 0.0008f; burst.volume = dbToGain (14);
        tail.prepare (sr, 304); tail.attack = 0.002f; tail.volume = dbToGain (11);
        hp.set (Biquad::highpass, 600, 1, 0, sr);
    }
    float q = 1, tailDec = 0.2f;
    void trigger (const KitDef& k, double n, float vel, const PadState& p)
    {
        bp.set (Biquad::bandpass, k.cp.bp * semi (p.tune), k.cp.q, 0, sr);
        tailDec = k.cp.tail * p.decay;
        const double g = k.cp.gap * sr;
        numPend = 0;
        for (int i = 0; i < 3; ++i) pend[numPend++] = { n + (i == 0 ? 0 : (i == 1 ? g : g * 2.1)), vel * (0.8f + 0.1f * i), false };
        pend[numPend++] = { n + g * 2.8, vel * 0.9f, true };
    }
    float tick (double n)
    {
        for (int i = 0; i < numPend; ++i)
            if (pend[i].at <= n && pend[i].at > -1)
            {
                if (pend[i].isTail) tail.trigger (pend[i].at, pend[i].vel, tailDec); else burst.trigger (pend[i].at, pend[i].vel, 0.009f);
                pend[i].at = -2;
            }
        return hp.tick (bp.tick (burst.tick (n) + tail.tick (n)));
    }
};

// Rim: a triangle (+6 dB) through a band-pass at 1700 Hz (Q 3), and a square (-8 dB, 12 ms), both through a high-pass at 350 Hz.
struct Rim
{
    ToneSynth a, b; Biquad bp, hp;
    void prepare (const Tables* t, double sr)
    {
        a.prepare (t, sr); a.osc.wave = Wave::triangle; a.attack = 0.0005f; a.volume = dbToGain (6);
        b.prepare (t, sr); b.osc.wave = Wave::square; b.attack = 0.0005f; b.volume = dbToGain (-8);
        bp.set (Biquad::bandpass, 1700, 3, 0, sr); hp.set (Biquad::highpass, 350, 1, 0, sr);
    }
    void trigger (const KitDef& k, double n, float vel, const PadState& p)
    {
        const float m = semi (p.tune);
        a.trigger (n, k.rs.f1 * m, vel, k.rs.dec * p.decay);
        b.trigger (n, k.rs.f2 * m, vel, 0.012f);
    }
    float tick (double n) { return hp.tick (bp.tick (a.tick (n)) + b.tick (n)); }
};

// Tom: a membrane through Tone.Distortion(0.12, 2x) at 45 % wet (equal-power cross-fade; the wet path carries the
// oversampler's 128-sample delay, as in the prototype).
struct Tom
{
    ToneSynth s; Shaper2x drive; std::array<float, 129> dry {}; int dpos = 0;
    float dryGain = 0, wetGain = 0;
    void prepare (const Tables* t, double sr)
    {
        s.prepare (t, sr); s.membrane = true; s.pitchDecay = 0.06f; s.attack = 0.001f; s.env.expAttack = true;
        drive.prepare(); drive.setDistortion (0.12f);
        dryGain = (float) std::cos (0.45 * kPi / 2); wetGain = (float) std::sin (0.45 * kPi / 2);
    }
    void trigger (const KitDef& k, double n, float freq, float vel, float decay)
    {
        s.osc.wave = k.tom.wave; s.octaves = k.tom.oct;
        s.trigger (n, freq, vel, decay);
    }
    float tick (double n)
    {
        const float x = s.tick (n);
        return x * dryGain + drive.tick (0, x) * wetGain;
    }
};

// Tone's FMOscillator / FMSynth pair: the carrier's frequency is F·(1 + index·modulator). Both oscillators start at the hit
// and read their frequency late in the first render quantum (see ToneSynth); the modulator's output is kept for that.
struct FMPair
{
    Osc car, mod;
    float ratio = 1, harm = 1, index = 1;
    float fPrev = 0, f = 0;
    double start = -1e18, carStop = -1, modStop = -1, quantumEnd = -1;
    int lag = 0;
    std::array<float, 128> modHist {};
    void prepare (const Tables* t, Wave carrier, Wave modulator) { car.tables = mod.tables = t; car.wave = carrier; mod.wave = modulator; }
    void trigger (double n, float freq, double carStopAt, double modStopAt)
    {
        const bool running = n < carStop && n >= start;   // as ToneSynth: a running pair carries on (phases, no lag)
        fPrev = f; f = freq; start = n; carStop = carStopAt; modStop = modStopAt;
        if (running) { lag = 0; return; }
        car.restart(); mod.restart();
        lag = (int) ((int64_t) n & 127); quantumEnd = n - lag + 128;
    }
    double lagged (double n) const { return (lag > 0 && n < quantumEnd) ? n - lag : n; }
    float sig (double x) const { return (x < start ? fPrev : f) * ratio; }
    // modAmp: the modulator's gain at n (FMSynth: its envelope at -10 dB; MetalSynth: 1)
    float tick (double n, float modAmp)
    {
        const bool modOn = n >= start && n < modStop, carOn = n >= start && n < carStop;
        const float mo = modOn ? mod.tick (sig (lagged (n)) * harm) * modAmp : 0.0f;
        modHist[(size_t) ((int64_t) n & 127)] = mo;
        if (! carOn) return 0;
        const double x = lagged (n);
        const float fx = sig (x);
        return car.tick (fx * (1 + index * modHist[(size_t) ((int64_t) x & 127)]));
    }
};

// Tone.MetalSynth: six FM pairs (squares) at the inharmonic ratios → high-pass swept by the envelope
// (resonance → resonance·2^octaves, Q 0 dB) → the envelope (linear attack, sustain 0).
struct Metal
{
    FMPair p[6]; ToneEnv env; Biquad hp; double sr = 48000, stop = -1;
    float res = 4000, octs = 1.5f, lastHp = -1;
    void prepare (const Tables* t, double rate)
    {
        sr = rate; env.sr = rate;
        static constexpr float ratios[6] = { 1.0f, 1.483f, 1.932f, 2.546f, 2.63f, 3.897f };
        for (int i = 0; i < 6; ++i) { p[i].prepare (t, Wave::square, Wave::square); p[i].ratio = ratios[i]; }
    }
    void trigger (const AuxDef& d, double n, float freq, float vel, float decay)
    {
        res = d.res; octs = d.oct;
        for (auto& o : p) { o.harm = d.h; o.index = d.mi; }
        env.trigger (n, vel, 0.001, decay);
        stop = env.stopAt();
        for (auto& o : p) o.trigger (n, freq, stop, stop);
    }
    float tick (double n)
    {
        if (! env.active || n < env.start) return 0;
        if (n >= stop) { env.active = false; return 0; }
        float sum = 0;
        for (auto& o : p) sum += o.tick (n, 1.0f);
        const float e = env.valueAt (n);
        const float fc = res + e * (res * std::pow (2.0f, octs) - res);
        if (fc != lastHp) { lastHp = fc; hp.set (Biquad::highpass, fc, 0, 0, sr); }
        return hp.tick (sum) * e;
    }
};

// Tone.FMSynth (blip): sine carrier and square modulator, each a Synth at -10 dB with its own envelope.
struct Blip
{
    FMPair p; ToneEnv env, menv; double sr = 48000;
    void prepare (const Tables* t, double rate) { sr = rate; env.sr = menv.sr = rate; p.prepare (t, Wave::sine, Wave::square); }
    void trigger (const AuxDef& d, double n, float freq, float vel, float decay)
    {
        p.harm = d.h; p.index = d.mi;
        env.trigger (n, vel, 0.001, decay);
        menv.trigger (n, vel, 0.001, d.dec * 0.6);   // the page sets the modulation envelope once, from the kit's decay
        p.trigger (n, freq, env.stopAt(), menv.stopAt());
    }
    float tick (double n)
    {
        if (! env.active && ! menv.active) return 0;
        const float m = menv.active ? menv.valueAt (n) * 0.31622777f : 0.0f;
        if (menv.active && n >= menv.stopAt()) menv.active = false;
        const float c = p.tick (n, m);
        if (! env.active) return 0;
        if (n >= env.stopAt()) { env.active = false; return 0; }
        return c * env.valueAt (n) * 0.31622777f;
    }
};

// Pads 9–16: the kit's percussion engines (makeAux in the prototype), each into a volume of its own (KIT_CAL).
struct Aux
{
    AuxType type = AuxType::conga;
    ToneSynth s, s2; ToneNoise nz; Biquad f1, f2; Metal metal; Blip blip;
    float vol = 1; double sr = 48000;
    // noise sweep: band-pass from f0 to f1 (exponential) over the decay
    double sweepStart = -1, sweepLen = 1; float sweepF0 = 1000, sweepF1 = 1000, lastF = -1;
    double tambSecond = -1; float tambVel = 0, tambDec = 0;
    void prepare (const Tables* t, double rate, uint64_t seed)
    {
        sr = rate;
        s.prepare (t, sr); s2.prepare (t, sr); nz.prepare (sr, seed); metal.prepare (t, sr); blip.prepare (t, sr);
    }
    void setKit (const AuxDef& d)
    {
        type = d.type; vol = dbToGain (orDefault (d.volDb, 0));
        // the prototype builds new percussion synths for each kit: fresh frequency signals (440 Hz; FM pairs 0 Hz)
        s.cur = s.prev = s2.cur = s2.prev = FreqCurve{}; s.cur.sr = s.prev.sr = s2.cur.sr = s2.prev.sr = sr;
        for (auto& o : metal.p) o.f = o.fPrev = 0;
        blip.p.f = blip.p.fPrev = 0;
        s.membrane = false; s.env.expAttack = false; s.attack = 0.001f; s.volume = 1; s2.volume = 1;
        f1 = Biquad{}; f2 = Biquad{};
        switch (type)
        {
            case AuxType::conga: case AuxType::zap: case AuxType::sub: case AuxType::hand:
                s.membrane = true; s.env.expAttack = true; s.pitchDecay = d.pd; s.octaves = d.oct;
                s.osc.wave = type == AuxType::hand ? Wave::sine : d.wave;
                if (type == AuxType::hand) { nz.type = d.noise; nz.attack = 0.0008f; nz.volume = dbToGain (orDefault (d.nvol, 0)); }
                break;
            case AuxType::shaker: case AuxType::tamb: nz.type = d.noise; nz.attack = orDefault (d.att, 0.003f); nz.volume = 1; break;
            case AuxType::snap: nz.type = Noise::white; nz.attack = 0.0008f; nz.volume = 1; break;
            case AuxType::noise: nz.type = d.noise; nz.attack = d.att; nz.volume = 1; break;
            case AuxType::cowbell: s.osc.wave = Wave::square; s2.osc.wave = Wave::square; f1.set (Biquad::bandpass, d.bp, d.q, 0, sr); break;
            case AuxType::clave: s.osc.wave = d.wave; f1.set (Biquad::bandpass, d.f, 2, 0, sr); break;
            case AuxType::metal: f1.set (Biquad::highpass, d.hp, 1, 0, sr); break;
            case AuxType::blip: break;
        }
    }
    void trigger (const AuxDef& d, double n, float vel, const PadState& p)
    {
        const float m = semi (p.tune), dec = d.dec * p.decay;
        switch (type)
        {
            case AuxType::conga: case AuxType::zap: case AuxType::sub: s.trigger (n, d.f * m, vel, dec); break;
            case AuxType::hand:
                f1.set (Biquad::bandpass, d.nf * m, d.nq, 0, sr);
                s.trigger (n, d.f * m, vel, dec); nz.trigger (n, vel, d.ndec * p.decay); break;
            case AuxType::shaker: case AuxType::tamb:
                f1.set (Biquad::highpass, d.hp * m, 1, 0, sr); f2.copyCoeffs (f1);   // two stages (rolloff -24), states kept
                if (type == AuxType::tamb) { nz.trigger (n, vel * 0.8f, dec); tambSecond = n + std::round (0.013 * sr); tambVel = vel; tambDec = dec; }
                else nz.trigger (n, vel, dec);
                break;
            case AuxType::snap: f1.set (Biquad::bandpass, d.f * m, d.q, 0, sr); nz.trigger (n, vel, dec); break;
            case AuxType::noise:
                sweepStart = n; sweepLen = dec * sr; sweepF0 = d.f0 * m; sweepF1 = d.f1 * m; lastF = -1;
                nz.trigger (n, vel, dec); break;
            case AuxType::cowbell: s.trigger (n, d.f1 * m, vel, dec); s2.trigger (n, d.f2 * m, vel, dec); break;
            case AuxType::clave: s.trigger (n, d.f * m, vel, dec); break;
            case AuxType::metal: metal.trigger (d, n, d.f * m, vel, dec); break;
            case AuxType::blip: blip.trigger (d, n, d.f * m, vel, dec); break;
        }
    }
    float tick (double n)
    {
        float y = 0;
        switch (type)
        {
            case AuxType::conga: case AuxType::zap: case AuxType::sub: y = s.tick (n); break;
            case AuxType::hand: y = s.tick (n) + f1.tick (nz.tick (n)); break;
            case AuxType::shaker: case AuxType::tamb:
                if (tambSecond >= 0 && n >= tambSecond) { nz.trigger (tambSecond, tambVel, tambDec); tambSecond = -1; }
                y = f2.tick (f1.tick (nz.tick (n))); break;
            case AuxType::snap: y = f1.tick (nz.tick (n)); break;
            case AuxType::noise:
            {
                if (sweepStart >= 0)
                {
                    const double t = std::min (1.0, std::max (0.0, (n - sweepStart) / sweepLen));
                    const float fc = (float) (sweepF0 * std::pow ((double) sweepF1 / sweepF0, t));
                    if (fc != lastF) { lastF = fc; f1.set (Biquad::bandpass, fc, 1.2, 0, sr); }
                }
                y = f1.tick (nz.tick (n)); break;
            }
            case AuxType::cowbell: y = f1.tick (s.tick (n) + s2.tick (n)); break;
            case AuxType::clave: y = f1.tick (s.tick (n)); break;
            case AuxType::metal: y = f1.tick (metal.tick (n)); break;
            case AuxType::blip: y = blip.tick (n); break;
        }
        return y * vol;
    }
};


// Safety limiter at -1 dBFS: 4x oversampled peak detection, 1.5 ms look-ahead, 80 ms release. It should never act.
class Limiter
{
public:
    void prepare (double rate)
    {
        sr = rate;
        look = std::max (1, (int) std::lround (0.0015 * rate));
        delay[0].assign ((size_t) look + 1, 0); delay[1].assign ((size_t) look + 1, 0);
        gmin.assign ((size_t) look + 1, 1);
        rel = (float) std::exp (-1.0 / (0.080 * rate));
        // 4x interpolator: 4 phases of a 32-tap windowed sinc
        for (int p = 0; p < 4; ++p)
            for (int t = 0; t < 8; ++t)
            {
                const double x = t - 4 + p / 4.0;   // between h[4] (p = 0) and h[3]
                const double w = 0.5 + 0.5 * std::cos (kPi * x / 4.5);
                interp[p][t] = (float) ((x == 0 ? 1.0 : std::sin (kPi * x) / (kPi * x)) * w);
            }
        reset();
    }
    void reset()
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (gmin.begin(), gmin.end(), 1.0f);
        hist[0].fill (0); hist[1].fill (0); pos = 0; g = 1; acted = false;
    }
    void tick (float& l, float& r)
    {
        // true-peak estimate of the incoming frame
        float tp = 0;
        for (int c = 0; c < 2; ++c)
        {
            auto& h = hist[(size_t) c];
            for (int i = 7; i > 0; --i) h[(size_t) i] = h[(size_t) i - 1];
            h[0] = c == 0 ? l : r;
            tp = std::max (tp, std::fabs (h[4]));
            for (int p = 0; p < 4; ++p)
            {
                float v = 0; for (int t = 0; t < 8; ++t) v += interp[p][t] * h[(size_t) t];
                tp = std::max (tp, std::fabs (v));
            }
        }
        const float ceiling = 0.89125094f;   // -1 dBFS
        const float need = tp > ceiling ? ceiling / tp : 1.0f;
        // minimum over the look-ahead window, then a linear ramp toward it so the gain is down before the peak leaves the delay
        const int n = look + 1;
        gmin[(size_t) pos] = need;
        float m = 1; for (int i = 0; i < n; ++i) m = std::min (m, gmin[(size_t) i]);
        if (m < g) g = std::max (m, g - (1 - m) / (float) look); else g = m + (g - m) * rel;
        if (g < 0.9999f) acted = true;
        delay[0][(size_t) pos] = l; delay[1][(size_t) pos] = r;
        const int rd = (pos + 1) % n;
        l = delay[0][(size_t) rd] * g; r = delay[1][(size_t) rd] * g;
        pos = rd;
    }
    bool hasActed() const { return acted; }
    int latency() const { return look; }

private:
    double sr = 48000; int look = 72, pos = 0; float g = 1, rel = 0.9997f; bool acted = false;
    std::array<std::vector<float>, 2> delay; std::vector<float> gmin;
    std::array<std::array<float, 8>, 2> hist {};
    float interp[4][8] {};
};

// ---------------------------------------------------------------- SPACE, ECHO, dust
// A delay line read with linear interpolation (Chrome's DelayNode).
struct DelayLine
{
    std::vector<float> buf; int w = 0;
    void prepare (int capacity) { buf.assign ((size_t) capacity + 3, 0.0f); w = 0; }
    void push (float x) { w = (w + 1) % (int) buf.size(); buf[(size_t) w] = x; }
    // the value written `d` samples before the newest one
    float read (double d) const
    {
        const int n = (int) buf.size();
        const int i = (int) d; const float f = (float) (d - i);
        const float a = buf[(size_t) ((w - i + 2 * n) % n)], b = buf[(size_t) ((w - i - 1 + 2 * n) % n)];
        return a + f * (b - a);
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
};

struct Allpass : Biquad
{
    void setAllpass (double f, double q, double rate)
    {
        const double w0 = 2 * kPi * f / rate, alpha = std::sin (w0) / (2 * q), cw = std::cos (w0);
        setRaw (1 - alpha, -2 * cw, 1 + alpha, 1 + alpha, -2 * cw, 1 - alpha);
    }
};

// SPACE: high-pass → pre-delay → Tone's Freeverb → low-pass → return. Tone's Freeverb: per side four comb filters
// (Jezar's tunings 1557/1617/1491/1422 left, 1277/1356/1188/1116 right, samples at 44.1 kHz; feedback = size; Tone's comb
// worklet reads before it writes, so the loop is one sample longer), each fed through a one-pole low-pass at the damping
// frequency (on the input only, not inside the loop, as Tone builds it), summed into four all-pass biquads
// (225, 556, 441, 341 Hz, Q 1).
// A value moved as Tone's rampTo moves a parameter: a straight line to the target over a time; set at once when told.
struct Ramp
{
    double v = 0, target = 0, step = 0; int left = 0;
    void set (double t, double samples, bool now)
    {
        target = t;
        if (now || samples < 1) { v = t; left = 0; return; }
        left = (int) samples; step = (t - v) / left;
    }
    double tick() { if (left > 0) { v += step; if (--left == 0) v = target; } return v; }
};

class SpaceFx
{
public:
    void prepare (double rate)
    {
        sr = rate;
        static constexpr int tunings[8] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
        for (int i = 0; i < 8; ++i) { combDelay[i] = (int) std::floor (tunings[i] / 44100.0 * sr); comb[i].prepare (combDelay[i] + 2); }
        pre[0].prepare ((int) (0.2 * sr)); pre[1].prepare ((int) (0.2 * sr));
        static constexpr float ap[4] = { 225, 556, 441, 341 };
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 4; ++i) allpass[c][i].setAllpass (ap[i], 1, sr);
        set (kKits[0].space);
    }
    // now: at once (prepare, a bounce); otherwise the prototype's ramps (size 0.1 s, pre-delay and return 0.05 s)
    void set (const SpaceDef& d, bool now = true)
    {
        size.set (d.size, 0.1 * sr, now); preSamples.set (d.pre / 1000.0 * sr, 0.05 * sr, now); ret.set (d.ret * 0.5, 0.05 * sr, now);
        if (d.hp != hpF) { hpF = d.hp; for (auto& h : hp) h.set (Biquad::highpass, d.hp, 1, 0, sr); }
        if (d.lp != lpF) { lpF = d.lp; for (auto& l : lp) l.set (Biquad::lowpass, d.lp, 1, 0, sr); }
        onePole = (float) (2 * kPi * d.damp / sr);
    }
    // silence the tail (no allocation)
    void clear()
    {
        for (auto& d : pre) d.clear(); for (auto& d : comb) d.clear();
        damp[0] = damp[1] = 0;
        for (auto& b : hp) b.reset(); for (auto& b : lp) b.reset(); for (auto& c : allpass) for (auto& a : c) a.reset();
    }
    void tick (float inL, float inR, float& outL, float& outR)
    {
        const double preNow = preSamples.tick(); const float sizeNow = (float) size.tick(), retNow = (float) ret.tick();
        float x[2] = { hp[0].tick (inL), hp[1].tick (inR) };
        for (int c = 0; c < 2; ++c) { pre[c].push (x[c]); x[c] = pre[c].read (preNow); }
        float y[2] = { 0, 0 };
        for (int c = 0; c < 2; ++c)
        {
            damp[c] = onePole * x[c] + (1 - onePole) * damp[c];
            for (int i = c * 4; i < c * 4 + 4; ++i)
            {
                const float d = comb[i].read (combDelay[i]);
                comb[i].push (damp[c] + d * sizeNow);
                y[c] += d;
            }
            for (auto& a : allpass[c]) y[c] = a.tick (y[c]);
        }
        outL = lp[0].tick (y[0]) * retNow; outR = lp[1].tick (y[1]) * retNow;
    }

private:
    double sr = 48000;
    Ramp preSamples, size, ret;
    float onePole = 0.5f, damp[2] {}, hpF = -1, lpF = -1;
    Biquad hp[2], lp[2]; Allpass allpass[2][4]; DelayLine pre[2], comb[8]; int combDelay[8] {};
};

// ECHO: high-pass 450 Hz → low-pass (TONE) → Tone's PingPongDelay (left: one delay; right: a pre-delay and a delay; the
// left output feeds the right delay and the right output the left one, both times the feedback) → return.
class EchoFx
{
public:
    void prepare (double rate)
    {
        sr = rate;
        for (auto* d : { &dl, &dr, &dpre }) d->prepare ((int) (2.0 * sr) + 4);
        for (auto& h : hp) h.set (Biquad::highpass, 450, 1, 0, sr);
        set (kKits[0].echo, 0.5);
    }
    // now: at once; otherwise the prototype's ramps (feedback and return 0.05 s, time 0.08 s)
    void set (const EchoDef& d, double delaySeconds, bool now = true)
    {
        fb.set (d.fb, 0.05 * sr, now); ret.set (d.ret * 0.6, 0.05 * sr, now);
        if (d.tone != toneF) { toneF = d.tone; for (auto& l : lp) l.set (Biquad::lowpass, d.tone, 1, 0, sr); }
        delay.set (std::clamp (delaySeconds * sr, 128.0, 2.0 * sr), 0.08 * sr, now);   // Chrome: a delay in a cycle is at least one quantum
    }
    void clear() { dl.clear(); dr.clear(); dpre.clear(); for (auto& b : hp) b.reset(); for (auto& b : lp) b.reset(); }
    void tick (float inL, float inR, float& outL, float& outR)
    {
        const double dNow = delay.tick(); const float fbNow = (float) fb.tick(), retNow = (float) ret.tick();
        const float xl = lp[0].tick (hp[0].tick (inL)), xr = lp[1].tick (hp[1].tick (inR));
        // outputs read before this sample's writes (the loop goes through the delays)
        const float L = dl.read (dNow - 1), R = dr.read (dNow - 1);
        dpre.push (xr);
        const float preOut = dpre.read (dNow);
        dl.push (xl + fbNow * R);
        dr.push (preOut + fbNow * L);
        outL = L * retNow; outR = R * retNow;
    }

private:
    double sr = 48000; float toneF = -1;
    Ramp delay, fb, ret;
    Biquad hp[2], lp[2]; DelayLine dl, dr, dpre;
};

// Color · dust: pink hiss (high-pass 700 Hz, low-pass 5.2 kHz) at -64 + 24·dust dB while the sequencer runs, faded in and out
// over 0.1 s; the crackles (4 ms white bursts at -22 dB through a high-pass at 1.8 kHz) are placed by the sequencer.
class Dust
{
public:
    void prepare (double rate)
    {
        sr = rate; gen.seed (77);
        hp1.set (Biquad::highpass, 700, 1, 0, sr); lp1.set (Biquad::lowpass, 5200, 1, 0, sr);
        crackle.prepare (sr, 78); crackle.attack = 0.0003f; crackle.volume = dbToGain (-22);
        chp.set (Biquad::highpass, 1800, 1, 0, sr);
        gain = 0; tgt = 0; step = 0;
    }
    void silence() { crackle = ToneNoise(); crackle.prepare (sr, 78); crackle.attack = 0.0003f; crackle.volume = dbToGain (-22);
                     hp1.reset(); lp1.reset(); chp.reset(); gain = 0; tgt = 0; step = 0; }
    // a linear 0.1 s ramp from where the gain is when the target changes; the same target again (every block) leaves the
    // ramp alone, so the fade does not depend on the block size
    void setTarget (float target) { if (target == tgt) return; tgt = target; step = (float) (std::fabs (target - gain) / (0.1 * sr)); }
    void crackleAt (double n, float vel) { crackle.trigger (n, vel, 0.004f); }
    float tick (double n)
    {
        if (gain < tgt) gain = std::min (tgt, gain + step); else if (gain > tgt) gain = std::max (tgt, gain - step);
        float y = chp.tick (crackle.tick (n));
        // the hiss source runs always (as the prototype's), only its gain moves
        const float h = lp1.tick (hp1.tick (gen.next (Noise::pink)));
        return y + h * gain;
    }
    ToneNoise crackle;

private:
    double sr = 48000; NoiseGen gen; Biquad hp1, lp1, chp; float gain = 0, tgt = 0, step = 0;
};

// ---------------------------------------------------------------- engine
// A hit at an absolute, fractional sample time (Tone's sequencer times are not on samples). pad -1 is a dust crackle.
struct Hit { int pad; float vel; double time; };

// What the plug-in sets each block (the engine moves to it with the prototype's ramps).
struct EngineParams
{
    PadState pad[kNumPads];
    bool solo[kNumPads] {};
    float glue = 0.15f, volumeDb = -2, tape = 0, dust = 0, crush = 0;
    SpaceDef space = kKits[0].space;
    EchoDef echo = kKits[0].echo;
    int echoTime = 2;
    double bpm = 120;
};

class Engine
{
public:
    static constexpr int kMaxHits = 512;

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        // a host may prepare again (new rate or block size, re-activation): every voice, effect and ramp starts from rest,
        // nothing keeps a time from the previous run
        color = ColorShaper(); for (auto& b : colorLP) b = Biquad(); glue = Compressor(); limiter = Limiter();
        space = SpaceFx(); echo = EchoFx(); dustFx = Dust();
        for (auto& c : chan) c = Channel(); master = Ramp(); bank = 0;
        tables = std::make_unique<Tables>(); tables->build (sr);
        color.prepare(); glue.prepare (sr); limiter.prepare (sr);
        space.prepare (sr); echo.prepare (sr); dustFx.prepare (sr);
        freshVoices();
        limiterOn = true;
        now = 0; numHits = 0;
        soundKit = -1; glueSet = tapeSet = crushSet = -1;   // prepare() reset these stages: set them again
        setKit (kitIndex);
    }
    // Every voice and tail to rest without allocating (a host's reset: transport stop or jump). Parameters stay.
    void silence()
    {
        freshVoices();
        color.reset(); for (auto& b : colorLP) b.reset(); glue.reset(); limiter.reset();
        space.clear(); echo.clear(); dustFx.silence();
        numHits = 0;
        const int k = kitIndex; soundKit = -1; setKitSounds (k);
    }

    // Loads a kit: its sounds and every value it sets (channels, glue, Color, SPACE, ECHO), as the prototype's kit load
    // does (the raw tool; the plug-in moves its parameters instead and calls setKitSounds).
    void setKit (int k)
    {
        const KitDef& kd = kKits[std::clamp (k, 0, kNumKits - 1)];
        for (int p = 0; p < kNumPads; ++p)
        {
            pad[p] = PadState{};
            pad[p].levelDb = kd.ch[p].levelDb; pad[p].pan = kd.ch[p].pan; pad[p].rev = kd.ch[p].rev; pad[p].dly = kd.ch[p].dly;
            solo[p] = false;
        }
        glueAmount = kd.glue; tape = kd.color.tape; dust = kd.color.dust; crush = kd.color.crush;
        spaceDef = kd.space; echoDef = kd.echo;
        setKitSounds (k);
        applyBus (true);
        applyPads (true);
    }
    // The kit's sounds only. Main voices change at once (the prototype changes its synths in place); the eight percussion
    // voices are built anew for the new kit while the old ones ring out (two banks), as the prototype rebuilds them.
    void setKitSounds (int k)
    {
        k = std::clamp (k, 0, kNumKits - 1);
        if (k == soundKit) return;
        kitIndex = k; const KitDef& kd = kKits[k];
        hats.setKit (kd);
        if (soundKit >= 0) bank ^= 1;
        for (int i = 0; i < 8; ++i) aux[bank][i].setKit (kd.aux[i]);
        soundKit = k;
    }
    // Every value a block can change, moved with the prototype's ramps (30 ms channels, 50 ms bus and returns).
    void setParams (const EngineParams& p)
    {
        bool padsChanged = false;
        for (int i = 0; i < kNumPads; ++i)
        {
            const PadState& a = p.pad[i]; PadState& b = pad[i];
            if (a.levelDb != b.levelDb || a.pan != b.pan || a.rev != b.rev || a.dly != b.dly || a.mute != b.mute || p.solo[i] != solo[i]) padsChanged = true;
            b = a; solo[i] = p.solo[i];
        }
        if (padsChanged) applyPads (false);
        const bool busChanged = p.glue != glueAmount || p.volumeDb != volumeDb || p.tape != tape || p.crush != crush || p.dust != dust
                             || std::memcmp (&p.space, &spaceDef, sizeof spaceDef) != 0 || std::memcmp (&p.echo, &echoDef, sizeof echoDef) != 0
                             || kEchoBeats[std::clamp (p.echoTime, 0, 3)] != echoBeats || p.bpm != bpm;
        if (busChanged)
        {
            glueAmount = p.glue; volumeDb = p.volumeDb; tape = p.tape; crush = p.crush; dust = p.dust;
            spaceDef = p.space; echoDef = p.echo; echoBeats = kEchoBeats[std::clamp (p.echoTime, 0, 3)]; bpm = p.bpm;
            applyBus (false);
        }
    }
    void applyBus (bool now = true)
    {
        if (glueAmount != glueSet) { glueSet = glueAmount; glue.setParams (-3 - 20 * glueAmount, 6, 1.2f + 2.8f * glueAmount, 0.012f, 0.16f); }
        if (tape != tapeSet || crush != crushSet)
        {
            tapeSet = tape; crushSet = crush;
            color.setCurve (tape, crush);
            const float lp = std::min (20000.0f * std::pow (0.4f, tape), 20000.0f * std::pow (0.55f, crush));
            for (auto& b : colorLP) b.set (Biquad::lowpass, lp, 1, 0, sr);
        }
        master.set (dbToGain (volumeDb), 0.05 * sr, now);
        space.set (spaceDef, now);
        echo.set (echoDef, echoBeats * 60.0 / std::max (1.0, bpm), now);
        dustFx.setTarget (running && dust > 0.001f ? dbToGain (-64 + 24 * dust) : 0.0f);
    }
    // the echo's tempo-synced time: 0 = 1/16 D, 1 = 1/8, 2 = 1/8 D, 3 = 1/4
    void setEchoTime (int index, double tempo)
    {
        echoBeats = kEchoBeats[std::clamp (index, 0, 3)]; bpm = tempo;
        echo.set (echoDef, echoBeats * 60.0 / std::max (1.0, bpm), true);
    }
    void setRunning (bool on) { running = on; dustFx.setTarget (running && dust > 0.001f ? dbToGain (-64 + 24 * dust) : 0.0f); }

    PadState pad[kNumPads];
    bool solo[kNumPads] {};
    float glueAmount = 0.15f, volumeDb = -2, tape = 0, dust = 0, crush = 0;
    SpaceDef spaceDef = kKits[0].space; EchoDef echoDef = kKits[0].echo;
    bool limiterOn = true;

    // a hit `offset` samples into the next process() call, or at an absolute sample time
    void hit (int p, float vel, double offset) { hitAt (p, vel, (double) now + offset); }
    void hitAt (int p, float vel, double time) { if (numHits < kMaxHits) hits[numHits++] = { p, vel, time }; }

    // Measurement taps (raw tool only): the channel output of one pad, and the master before the limiter.
    int tapPad = -1;
    float* tapL = nullptr; float* tapR = nullptr; float* preL = nullptr; float* preR = nullptr;

    void process (float* outL, float* outR, int n)
    {
        std::sort (hits, hits + numHits, [] (const Hit& a, const Hit& b) { return a.time < b.time; });
        int h = 0;
        float blockPeak = 0, blockGr = 0;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) now;
            while (h < numHits && hits[h].time <= t) { fire (hits[h], hits[h].time); ++h; }
            float padOut[kNumPads] {};
            padOut[BD] = kick.tick (t);
            padOut[SD] = snare.tick (t);
            padOut[CP] = clap.tick (t);
            padOut[RS] = rim.tick (t);
            padOut[LT] = lt.tick (t);
            padOut[HT] = ht.tick (t);
            for (int a = 0; a < 8; ++a) padOut[P1 + a] = aux[bank][a].tick (t) + aux[bank ^ 1][a].tick (t);
            hats.tick (t, padOut[CH], padOut[OH]);
            float busL = 0, busR = 0, revL = 0, revR = 0, dlyL = 0, dlyR = 0;
            for (int p = 0; p < kNumPads; ++p)
            {
                auto& c = chan[p];
                const float gl = (float) c.gl.tick(), gr = (float) c.gr.tick(), rv = (float) c.rev.tick(), dl = (float) c.dly.tick();
                // Tone.Panner: equal power from a mono input, then the channel's level; mute and solo silence the channel
                // (tails too); sends after the channel
                const float l = padOut[p] * gl, r = padOut[p] * gr;
                busL += l; busR += r;
                revL += l * rv; revR += r * rv;
                dlyL += l * dl; dlyR += r * dl;
                if (p == tapPad && tapL) { tapL[i] = l; tapR[i] = r; }
            }
            // Color (2x shaper) → low-pass → glue
            float l = colorLP[0].tick (color.tick (0, busL)), r = colorLP[1].tick (color.tick (1, busR));
            glue.tick (l, r, now);
            blockGr = std::min (blockGr, glue.gainReductionDb());
            // ECHO (18 % of its return into SPACE), SPACE, dust: into the mix after the glue
            float el, er, sl, sr2;
            echo.tick (dlyL, dlyR, el, er);
            space.tick (revL + el * 0.18f, revR + er * 0.18f, sl, sr2);
            const float d = dustFx.tick (t);
            const float m = (float) master.tick();
            l = (l + el + sl + d) * m; r = (r + er + sr2 + d) * m;
            if (preL) { preL[i] = l; preR[i] = r; }
            if (limiterOn) limiter.tick (l, r);
            outL[i] = l; outR[i] = r;
            blockPeak = std::max (blockPeak, std::max (std::fabs (l), std::fabs (r)));
            ++now;
        }
        int keep = 0;
        for (int j = h; j < numHits; ++j) hits[keep++] = hits[j];
        numHits = keep;
        lastPeak = blockPeak; lastGr = blockGr;
    }

    // Seeds every noise source (the prototype's noise starts at a random place; the engine is deterministic per seed).
    void seed (uint64_t v)
    {
        kick.click.seed (v * 16 + 1); snare.noise.seed (v * 16 + 2);
        clap.burst.seed (v * 16 + 3); clap.tail.seed (v * 16 + 4);
        for (int b = 0; b < 2; ++b) for (int i = 0; i < 8; ++i) aux[b][i].nz.seed (v * 16 + 5 + (uint64_t) (b * 8 + i) * 977);
        dustFx.crackle.seed (v * 16 + 14);
    }

    int64_t sampleTime() const { return now; }
    bool limiterActed() const { return limiter.hasActed(); }
    int latency() const { return glue.latency() + 128 + (limiterOn ? limiter.latency() : 0); }   // glue pre-delay, Color's 2x filters, limiter
    double sampleRate() const { return sr; }
    int kit() const { return kitIndex; }
    float lastPeak = 0, lastGr = 0;   // the last block's output peak (linear) and the glue's deepest reduction (dB, <= 0)

    // after changing pad[] or solo[] directly (the raw tool): channels to their new values, at once or with the 30 ms ramp
    void applyPads (bool now)
    {
        bool anySolo = false; for (bool b : solo) anySolo = anySolo || b;
        for (int p = 0; p < kNumPads; ++p)
        {
            const bool audible = ! pad[p].mute && (! anySolo || solo[p]);
            const float g = audible ? dbToGain (pad[p].levelDb) : 0.0f;
            const float x = (pad[p].pan + 1) * 0.5f * (float) (kPi / 2);
            const double ramp = 0.03 * sr;
            chan[p].gl.set (g * std::cos (x), ramp, now); chan[p].gr.set (g * std::sin (x), ramp, now);
            chan[p].rev.set (pad[p].rev, ramp, now); chan[p].dly.set (pad[p].dly, ramp, now);
        }
    }

private:
    void freshVoices()
    {
        kick = Kick(); snare = Snare(); hats = Hats(); clap = Clap(); rim = Rim(); lt = Tom(); ht = Tom();
        for (auto& b : aux) for (auto& a : b) a = Aux();
        kick.prepare (tables.get(), sr); snare.prepare (tables.get(), sr); hats.prepare (tables.get(), sr);
        clap.prepare (sr); rim.prepare (tables.get(), sr); lt.prepare (tables.get(), sr); ht.prepare (tables.get(), sr);
        for (int b = 0; b < 2; ++b) for (int i = 0; i < 8; ++i) aux[b][i].prepare (tables.get(), sr, 1000 + (uint64_t) (b * 8 + i));
        bank = 0;
    }
    void fire (const Hit& e, double t)
    {
        if (e.pad < 0) { dustFx.crackleAt (t, e.vel); return; }
        const KitDef& k = kKits[kitIndex];
        const PadState& p = pad[e.pad];
        const float trim = e.pad < 8 ? dbToGain (k.trimDb[e.pad]) : 1.0f;
        const float v = e.vel * trim;
        switch (e.pad)
        {
            case BD: kick.trigger (k, t, v, p); break;
            case SD: snare.trigger (k, t, v, p); break;
            case CH: hats.triggerClosed (k, t, v, p); break;
            case OH: hats.triggerOpen (k, t, v, p); break;
            case CP: clap.trigger (k, t, v, p); break;
            case RS: rim.trigger (k, t, v, p); break;
            case LT: lt.trigger (k, t, k.tom.lt * semi (p.tune), v, k.tom.dec * p.decay); break;
            case HT: ht.trigger (k, t, k.tom.ht * semi (p.tune), v, k.tom.dec * 0.82f * p.decay); break;
            default: aux[bank][e.pad - P1].trigger (k.aux[e.pad - P1], t, v, p); break;
        }
    }

    struct Channel { Ramp gl, gr, rev, dly; };
    double sr = 48000, bpm = 120, echoBeats = 0.75;
    bool running = false;
    std::unique_ptr<Tables> tables;
    Kick kick; Snare snare; Hats hats; Clap clap; Rim rim; Tom lt, ht; Aux aux[2][8]; int bank = 0, soundKit = -1;
    ColorShaper color; Biquad colorLP[2]; Compressor glue; Limiter limiter;
    SpaceFx space; EchoFx echo; Dust dustFx;
    Channel chan[kNumPads];
    Ramp master;
    float glueSet = -1, tapeSet = -1, crushSet = -1;
    int kitIndex = 0;
    int64_t now = 0;
    Hit hits[kMaxHits]; int numHits = 0;
};

} // namespace ru8
