/*
  TypeAEngine.h — model of a four-band A-type noise-reduction processor
  (the circuit used for the "vocal trick" / "stretch" effect).

  No JUCE dependency, so it can be unit-tested on its own.

  Topology (as in the original hardware):
    * The main signal path runs straight through.
    * A side chain splits the signal into four bands:
        Band 1  low-pass   81 Hz   (Sallen-Key, Q 0.73)
        Band 2  band-pass  80 Hz - 3 kHz (derived: input - band 1 - band 3)
        Band 3  high-pass  2.98 kHz (Sallen-Key, Q 0.70)
        Band 4  high-pass  9.58 kHz (Sallen-Key, Q 0.79)
      (component values from the Cat. 22 schematic, drawing ADC 441)
    * Each band has its own feedback compressor (FET attenuator; the
      detector rectifies the band's own side-chain output). At low levels
      its gain is about +10 dB relative to the main path, so quiet detail
      is lifted; above about -40 dB (re reference level) the band output
      is held near a ceiling, so loud material passes almost unchanged.
    * Fast overshoot limiters (diode clippers in hardware) catch the side
      signal on transients before the compressor has reacted.
    * ENCODE  : out = in + sum(side(in))       (the "trick")
      DECODE  : out = in - sum(side(out))      (side chain in a feedback
                                               loop, which exactly undoes
                                               an encode at the same level)

  The decode loop is solved without a unit delay: each band filter's
  instantaneous response is linear (gain * x + state offset), so the
  loop is solved directly, then refined with Newton steps through the
  overshoot limiter.
*/
#pragma once

#include <cmath>
#include <array>
#include <algorithm>

namespace typea
{

static constexpr int kNumBands = 4;
static constexpr double kPi = 3.14159265358979323846;

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // y[n] = b0 * x[n] + z1   (instantaneous linear response)
    inline double gain() const   { return b0; }
    inline double offset() const { return z1; }

    void setLowPass (double fs, double f, double q)  { design (fs, f, q, false); }
    void setHighPass (double fs, double f, double q) { design (fs, f, q, true); }

private:
    void design (double fs, double f, double q, bool highPass)
    {
        f = std::min (f, fs * 0.45);
        const double w0 = 2.0 * kPi * f / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double a0 = 1.0 + alpha;
        if (highPass)
        {
            b0 = (1.0 + cw) * 0.5; b1 = -(1.0 + cw); b2 = b0;
        }
        else
        {
            b0 = (1.0 - cw) * 0.5; b1 = 1.0 - cw; b2 = b0;
        }
        a1 = -2.0 * cw; a2 = 1.0 - alpha;
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
};

//==============================================================================
// One or two biquads in series, with the cascade's instantaneous response.
struct BandFilter
{
    Biquad s[2];
    int numStages = 1;

    void reset() { s[0].reset(); s[1].reset(); }

    inline double process (double x)
    {
        double y = s[0].process (x);
        if (numStages > 1) y = s[1].process (y);
        return y;
    }

    inline double gain() const
    {
        return numStages > 1 ? s[0].gain() * s[1].gain() : s[0].gain();
    }

    inline double offset() const
    {
        return numStages > 1 ? s[1].gain() * s[0].offset() + s[1].offset()
                             : s[0].offset();
    }
};

//==============================================================================
// The card's input amplifier and low-pass filter, which every signal passes
// through: a Sallen-Key low-pass (4.75k/4.22k, 2700pF/390pF -> 34.6 kHz,
// Q 1.31) after two passive RC sections. In the audio band it is a gentle
// lift: about +0.4 dB at 10 kHz and +1.6 dB at 20 kHz.
// Bilinear transform prewarped at 20 kHz keeps it within 0.2 dB of the
// analog response below 20 kHz at 88.2 kHz and above.
struct InputFilter
{
    Biquad sk, rc;

    void prepare (double fs)
    {
        const double fp = std::min (20000.0, fs * 0.4);
        const double K = 2.0 * kPi * fp / std::tan (kPi * fp / fs);

        // Sallen-Key: H(s) = 1 / (1 + s/(w0 Q) + s^2/w0^2)
        const double w0 = 2.0 * kPi * 34642.0, Q = 1.31;
        const double a1 = 1.0 / (w0 * Q), a2 = 1.0 / (w0 * w0);
        const double d0 = 1.0 + a1 * K + a2 * K * K;
        sk.b0 = 1.0 / d0; sk.b1 = 2.0 / d0; sk.b2 = 1.0 / d0;
        sk.a1 = (2.0 - 2.0 * a2 * K * K) / d0;
        sk.a2 = (1.0 - a1 * K + a2 * K * K) / d0;

        // Passive RC sections, approximated as one pole at 66 kHz
        const double tau = 1.0 / (2.0 * kPi * 66000.0);
        const double e0 = 1.0 + tau * K;
        rc.b0 = 1.0 / e0; rc.b1 = 1.0 / e0; rc.b2 = 0.0;
        rc.a1 = (1.0 - tau * K) / e0; rc.a2 = 0.0;
        reset();
    }

    void reset() { sk.reset(); rc.reset(); }
    inline double process (double x) { return sk.process (rc.process (x)); }
};

//==============================================================================
struct Settings
{
    double depth = 1.0;           // 1.0 = +10 dB low-level noise-reduction signal
    double speed = 1.0;           // >1 = faster attack/release
    bool   limiter = true;        // overshoot limiters on the side chain
    std::array<bool, kNumBands> bandOn { true, true, true, true };
};

//==============================================================================
class Engine
{
public:
    enum class Direction { Encode, Decode };

    //--------------------------------------------------------------------------
    // Calibration from the Model 361 manual (Cat. 22 servicing and NRM test
    // set). Signal units: 1.0 = reference level (Dolby Level, 300 mV at the
    // module input). The compressor input sits at module level.
    //
    //   Compressor test points (in -> out):  GAIN 2 mV -> 17 mV,
    //   LAW 1 10 mV -> 59 mV, LAW 2 50 mV -> 72 mV.
    //   Module: 3 mV (-40 dB) in gives +10 dB with NR on.
    //   Encode curve: maximum slope about 2:1 near -30 dB.
    //
    // Fitted static law (exact through all three points):
    //   g = kG0 / (1 + (d / kD)^kN),  d = compressor out + kBeta * compressor in
    // i.e. feedback detection plus the R126 feed-forward path, which makes
    // the side output turn down at high levels.
    static constexpr double kG0   = 8.5;            // compressor low-level gain (17/2)
    // Detector knee. 65.1 mV from the static fit, times 0.85 because the
    // rectifier/integrator chain settles below the waveform peak; with it
    // the model reads 17.0 / 59.5 / 72.7 mV on the three test-set checks.
    static constexpr double kD    = 65.1 / 300.0 * 0.85;
    static constexpr double kN    = 9.84;           // law steepness
    static constexpr double kBeta = 0.090;          // feed-forward proportion
    // Compressor output -> noise-reduction signal: +10 dB at low level
    static constexpr double kSideScale = 2.16227766 / kG0;
    // D400/D401 overshoot limiter: knee about 0.5 V peak at the compressor
    // output, where reference-level input is 0.42 V peak
    static constexpr double kClipLevel = 1.18;

    //--------------------------------------------------------------------------
    // Control timing (manual section 10.10 and the schematic):
    //   pre-integrator  R136+R137 into C110 (0.1 uF), D104/D105 bypass R136,
    //                   discharge through R135
    //   final integrator R140 (47k) into C106 (1 uF in bands 3-4; bands 1-2
    //                   twice as slow), D106/D107 fast rise, D108 fast fall
    // Thresholds are the diode drops, converted with the manual's control
    // voltages (3.1 / 3.5 / 3.7 V at GAIN / LAW 1 / LAW 2: about 0.36 signal
    // units per volt).
    static constexpr double kPreAttackMs[kNumBands]   { 4.0, 4.0, 2.0, 2.0 };
    static constexpr double kPreFastMs[kNumBands]     { 2.0, 2.0, 1.0, 1.0 };
    static constexpr double kPreReleaseMs[kNumBands]  { 56.0, 56.0, 27.0, 27.0 };
    static constexpr double kFinalMs[kNumBands]       { 94.0, 94.0, 47.0, 47.0 };
    static constexpr double kFinalFastMs[kNumBands]   { 2.0, 2.0, 1.0, 1.0 };
    static constexpr double kPreFastThresh   = 0.43;   // two silicon diodes
    static constexpr double kFinalRiseThresh = 0.30;   // silicon + germanium
    static constexpr double kFinalFallThresh = 0.21;   // one silicon diode

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // Unity-gain Sallen-Key stages, values from the Cat. 22 schematic:
        //   Band 1 LP: 301k/274k, 0.01uF/4700pF  -> 81 Hz,  Q 0.73
        //   Band 3 HP: 5.62k/11k, 6800pF x2      -> 2.98 kHz, Q 0.70
        //   Band 4 HP: 4.75k/12k, 2200pF x2      -> 9.58 kHz, Q 0.79
        // Band 2 has no filter of its own: the card derives it by
        // subtraction (input - band 1 - band 3), so bands 1-3 sum to unity.
        bands[0].numStages = 1; bands[0].s[0].setLowPass  (fs, 80.8, 0.729);
        bands[2].numStages = 1; bands[2].s[0].setHighPass (fs, 2977.0, 0.700);
        bands[3].numStages = 1; bands[3].s[0].setHighPass (fs, 9581.0, 0.795);

        updateBallistics();
        reset();
    }

    void reset()
    {
        for (auto& b : bands) b.reset();
        pre.fill (0.0);
        fin.fill (0.0);
        lastGain.fill (0.0);
    }

    void setSettings (const Settings& s)
    {
        const bool speedChanged = std::abs (s.speed - settings.speed) > 1e-9;
        settings = s;
        if (speedChanged) updateBallistics();
    }

    // x is in reference-level units (1.0 = sine peak at reference level).
    inline double process (double x, Direction dir)
    {
        return dir == Direction::Encode ? encode (x) : decode (x);
    }

    // Current low-level boost contributed by each band, in dB (for meters).
    double bandBoostDb (int b) const
    {
        return 20.0 * std::log10 (1.0 + kSideScale * settings.depth * lastGain[(size_t) b]);
    }

    // Test hook: run one band's compressor alone on a signal sample and
    // return its output (before kSideScale), as the NRM test set does.
    double probeCompressor (int b, double in)
    {
        const double g = compressorGain (b);
        const double c = clip (g * in);
        updateControl (b, c + kBeta * in);
        return c;
    }

private:
    inline double compressorGain (int b) const
    {
        const double r = fin[(size_t) b] / kD;
        return kG0 / (1.0 + std::pow (r, kN));
    }

    // Overshoot limiter: hard-knee soft clip u / (1 + (u/C)^4)^(1/4),
    // slope (1 + z)^(-5/4)
    static inline double clipCurve (double u, double& slope)
    {
        const double v = u / kClipLevel, v2 = v * v, z = v2 * v2;
        const double k = 1.0 / std::sqrt (std::sqrt (1.0 + z));   // (1+z)^(-1/4)
        slope = k / (1.0 + z);
        return u * k;
    }

    inline double clip (double u) const
    {
        if (! settings.limiter) return u;
        double slope;
        return clipCurve (u, slope);
    }

    // Full-wave rectifier, then the two non-linear integrators. Small
    // changes move slowly; large ones pass quickly through the diodes.
    inline void updateControl (int b, double detector)
    {
        const size_t i = (size_t) b;
        const double r = std::abs (detector);

        double& p = pre[i];
        if (r > p)
        {
            const double dlt = r - p;
            p += std::min (dlt, preAtt[i] * dlt + preFast[i] * std::max (0.0, dlt - kPreFastThresh));
        }
        else
        {
            p -= preRel[i] * p;             // rectifier off: C110 discharges via R135
            p = std::max (p, r);
        }

        double& c = fin[i];
        const double dlt = p - c;
        const double th = dlt > 0.0 ? kFinalRiseThresh : kFinalFallThresh;
        double step = finSlow[i] * dlt;
        const double excess = std::abs (dlt) - th;
        if (excess > 0.0)
            step += std::copysign (finFast[i] * excess, dlt);
        c += std::abs (step) < std::abs (dlt) ? step : dlt;
    }

    inline double encode (double x)
    {
        double f[kNumBands];
        splitBands (x, f);
        double sum = 0.0;
        const double scale = kSideScale * settings.depth;
        for (int b = 0; b < kNumBands; ++b)
        {
            if (! settings.bandOn[(size_t) b]) { lastGain[(size_t) b] = 0.0; continue; }
            const double g = compressorGain (b);
            lastGain[(size_t) b] = g;
            const double c = clip (g * f[b]);
            updateControl (b, c + kBeta * f[b]);
            sum += scale * c;
        }
        return x + sum;
    }

    inline double decode (double x)
    {
        // Side chain sees the output: y = x - scale * sum clip(g_b * F_b(y))
        const double scale = kSideScale * settings.depth;
        double A[kNumBands], O[kNumBands], h[kNumBands], o[kNumBands];
        instantaneous (h, o);
        double sumA = 0.0, sumO = 0.0;
        for (int b = 0; b < kNumBands; ++b)
        {
            const bool on = settings.bandOn[(size_t) b];
            const double g = on ? compressorGain (b) : 0.0;
            lastGain[(size_t) b] = g;
            A[b] = on ? g * h[b] : 0.0;   // compressor output = A y + O
            O[b] = on ? g * o[b] : 0.0;
            sumA += A[b]; sumO += O[b];
        }

        double y = (x - scale * sumO) / (1.0 + scale * sumA);   // exact when limiter is off

        if (settings.limiter)
        {
            // F(y) = y + scale * sum clip(A y + O) - x is monotonic increasing,
            // and each clipped term is bounded by +/-kClipLevel, so the root
            // is bracketed. Newton steps, with bisection whenever a step
            // leaves the bracket.
            const double span = scale * kNumBands * kClipLevel;
            double lo = x - span, hi = x + span;
            y = std::clamp (y, lo, hi);
            for (int it = 0; it < 60; ++it)
            {
                double F = y - x, dF = 1.0;
                for (int b = 0; b < kNumBands; ++b)
                {
                    if (! settings.bandOn[(size_t) b]) continue;
                    double slope;
                    F  += scale * clipCurve (A[b] * y + O[b], slope);
                    dF += scale * A[b] * slope;
                }
                if (F > 0.0) hi = y; else lo = y;
                double next = y - F / dF;
                if (! (next > lo && next < hi))
                    next = 0.5 * (lo + hi);
                const bool done = std::abs (next - y) <= 1e-14 * (1.0 + std::abs (y));
                y = next;
                if (done) break;
            }
        }

        double f[kNumBands];
        splitBands (y, f);
        for (int b = 0; b < kNumBands; ++b)
            if (settings.bandOn[(size_t) b])
                updateControl (b, clip (lastGain[(size_t) b] * f[b]) + kBeta * f[b]);

        return y;
    }

    inline void splitBands (double x, double* f)
    {
        f[0] = bands[0].process (x);
        f[2] = bands[2].process (x);
        f[3] = bands[3].process (x);
        f[1] = x - f[0] - f[2];
    }

    // Each band's output this sample is h[b] * x + o[b].
    inline void instantaneous (double* h, double* o) const
    {
        for (int b : { 0, 2, 3 }) { h[b] = bands[b].gain(); o[b] = bands[b].offset(); }
        h[1] = 1.0 - h[0] - h[2];
        o[1] = -o[0] - o[2];
    }

    void updateBallistics()
    {
        const double sp = std::max (0.05, settings.speed);
        auto coef = [this, sp] (double ms) { return 1.0 - std::exp (-1000.0 / (ms / sp * fs)); };
        for (int b = 0; b < kNumBands; ++b)
        {
            const size_t i = (size_t) b;
            preAtt[i]  = coef (kPreAttackMs[b]);
            preFast[i] = coef (kPreFastMs[b]);
            preRel[i]  = coef (kPreReleaseMs[b]);
            finSlow[i] = coef (kFinalMs[b]);
            finFast[i] = coef (kFinalFastMs[b]);
        }
    }

    double fs = 48000.0;
    Settings settings;
    BandFilter bands[kNumBands];
    std::array<double, kNumBands> pre {}, fin {}, lastGain {};
    std::array<double, kNumBands> preAtt {}, preFast {}, preRel {}, finSlow {}, finFast {};
};

} // namespace typea
