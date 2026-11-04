/*
  LevelTools.h — the 361-style level meter and the Learn function.
  No JUCE dependency, so both can be unit-tested on their own.

  Units match TypeAEngine: 1.0 = sine peak at reference level (Dolby Level,
  0 VU on the unit's panel meter).
*/
#pragma once

#include <cmath>
#include <array>
#include <cstdint>
#include <algorithm>

namespace typea
{

//==============================================================================
// VU-style meter: full-wave average responding, calibrated so a steady sine
// at reference level reads 0 VU. Two cascaded one-pole smoothers (45 ms each)
// give the standard VU rise: 99 % of a step in about 300 ms.
class VuMeter
{
public:
    void prepare (double sampleRate)
    {
        a = 1.0 - std::exp (-1.0 / (0.045 * sampleRate));
        reset();
    }

    void reset() { s1 = s2 = 0.0; }

    inline void process (double x)
    {
        s1 += a * (std::abs (x) - s1);
        s2 += a * (s1 - s2);
    }

    // Reading in VU (dB re reference). A sine's rectified average is 2/pi of its peak.
    double readingDb() const
    {
        constexpr double sineAverage = 0.63661977236758134;   // 2 / pi
        return 20.0 * std::log10 (std::max (s2, 1e-9) / sineAverage);
    }

private:
    double a = 0.0, s1 = 0.0, s2 = 0.0;
};

//==============================================================================
// Learn: collects VU readings while the track plays (silence is ignored),
// then reports the level the needle reaches on the louder passages, taken as
// the 90th percentile of readings. Setting Input to minus that value aligns
// the track so those passages read 0 VU, as an engineer would line up a
// vocal into the unit.
class LevelLearner
{
public:
    static constexpr double kGateDb     = -45.0;   // readings below this count as silence
    static constexpr double kPercentile = 0.90;
    static constexpr double kMinDb = -60.0, kMaxDb = 40.0, kBinDb = 0.25;
    static constexpr int    kNumBins = (int) ((kMaxDb - kMinDb) / kBinDb);

    // ticksNeeded: number of non-silent readings to collect before finishing.
    void start (int ticksNeeded)
    {
        bins.fill (0);
        count = 0;
        needed = (std::uint32_t) std::max (1, ticksNeeded);
        active = true;
        finished = false;
    }

    void cancel() { active = false; finished = false; }

    // Feed one VU reading. Returns true on the reading that completes the learn.
    bool feed (double vuDb)
    {
        if (! active || vuDb < kGateDb)
            return false;
        const int bin = std::clamp ((int) ((vuDb - kMinDb) / kBinDb), 0, kNumBins - 1);
        ++bins[(size_t) bin];
        if (++count >= needed)
        {
            active = false;
            finished = true;
            return true;
        }
        return false;
    }

    bool isActive() const   { return active; }
    bool isFinished() const { return finished; }
    double progress() const { return (double) count / (double) needed; }

    // Level (VU) reached by the louder passages
    double resultDb() const
    {
        const auto target = (std::uint32_t) std::ceil (kPercentile * (double) count);
        std::uint32_t sum = 0;
        for (int b = 0; b < kNumBins; ++b)
        {
            sum += bins[(size_t) b];
            if (sum >= target)
                return kMinDb + (b + 0.5) * kBinDb;
        }
        return kMaxDb;
    }

private:
    std::array<std::uint32_t, (size_t) kNumBins> bins {};
    std::uint32_t count = 0, needed = 1;
    bool active = false, finished = false;
};

} // namespace typea
