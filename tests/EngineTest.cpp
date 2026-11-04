// Offline checks for the A-type engine against the Model 361 manual:
//   * NRM test-set compressor checks (GAIN / LAW 1 / LAW 2)
//   * module record/play tests at -40 dB
//   * specification: 10 dB NR 30 Hz-5 kHz rising to 15 dB at 15 kHz,
//     THD < 0.1 % at 1 kHz and < 0.2 % 40 Hz-20 kHz at +8 dBm
//   * encode -> decode null, pulled-card behaviour, input filter
#include "../Source/dsp/TypeAEngine.h"
#include "../Source/dsp/LevelTools.h"
#include <cstdio>
#include <vector>
#include <complex>
#include <random>

using namespace typea;
static constexpr double kFs = 96000.0;
static int failures = 0;

static void check (bool ok, const char* what, double v)
{
    if (! ok) { std::printf ("  FAIL %s (%.3f)\n", what, v); ++failures; }
}

// Steady-state encode (or decode) gain in dB for a sine at levelDb re reference
static double gainDb (Settings s, double freq, double levelDb, Engine::Direction dir = Engine::Direction::Encode)
{
    Engine e; e.prepare (kFs); e.setSettings (s);
    const double amp = std::pow (10.0, levelDb / 20.0);
    const int n = (int) (kFs * 2.0), skip = (int) (kFs * 1.5);
    double inSq = 0, outSq = 0;
    for (int i = 0; i < n; ++i)
    {
        const double x = amp * std::sin (2.0 * kPi * freq * i / kFs);
        const double y = e.process (x, dir);
        if (i >= skip) { inSq += x * x; outSq += y * y; }
    }
    return 10.0 * std::log10 (outSq / inSq);
}

// Compressor alone, as the NRM test set drives it (mV in -> mV out, 300 mV = ref)
static double compressorMv (int band, double mv)
{
    Engine e; e.prepare (kFs);
    const double a = mv / 300.0;
    const int n = (int) (kFs * 2.5), skip = (int) (kFs * 2.0);
    double sq = 0;
    for (int i = 0; i < n; ++i)
    {
        const double c = e.probeCompressor (band, a * std::sin (2.0 * kPi * 850.0 * i / kFs));
        if (i >= skip) sq += c * c;
    }
    return std::sqrt (sq / (n - skip) * 2.0) * 300.0;
}

// THD (%) of the encode output, harmonics 2-10
static double thd (Settings s, double f, double levelDb)
{
    Engine e; e.prepare (kFs); e.setSettings (s);
    const double a = std::pow (10.0, levelDb / 20.0);
    const int N = (int) kFs;
    std::vector<double> y ((size_t) N);
    for (int i = 0; i < 3 * N; ++i)
    {
        const double o = e.process (a * std::sin (2.0 * kPi * f * i / kFs), Engine::Direction::Encode);
        if (i >= 2 * N) y[(size_t) (i - 2 * N)] = o;
    }
    auto bin = [&] (double fr)
    {
        std::complex<double> c = 0;
        for (int i = 0; i < N; ++i)
            c += (0.5 - 0.5 * std::cos (2.0 * kPi * i / N)) * y[(size_t) i] * std::polar (1.0, -2.0 * kPi * fr * i / kFs);
        return std::abs (c);
    };
    const double h1 = bin (f);
    double hs = 0;
    for (int k = 2; k <= 10 && k * f < kFs / 2; ++k) { const double h = bin (k * f); hs += h * h; }
    return 100.0 * std::sqrt (hs) / h1;
}

int main()
{
    std::printf ("NRM test-set compressor checks (mV out; target 16-18 / 55-65 / 67-77)\n");
    for (int b = 0; b < kNumBands; ++b)
    {
        const double g = compressorMv (b, 2), l1 = compressorMv (b, 10), l2 = compressorMv (b, 50);
        std::printf ("  band %d: GAIN %.1f  LAW1 %.1f  LAW2 %.1f\n", b + 1, g, l1, l2);
        check (g > 16 && g < 18, "GAIN", g);
        check (l1 > 55 && l1 < 65, "LAW1", l1);
        check (l2 > 67 && l2 < 77, "LAW2", l2);
    }

    Settings all;
    {
        // Module record / play tests: 3 mV in (-40 dB), 850 Hz, 9.5-10.5 dB
        const double rec = gainDb (all, 850, -40.0);
        const double play = gainDb (all, 850, -40.0, Engine::Direction::Decode);
        std::printf ("\nModule tests at -40 dB, 850 Hz: record %+.2f dB, play %+.2f dB (target +/-9.5..10.5)\n", rec, play);
        check (rec > 9.5 && rec < 10.5, "record test", rec);
        check (play < -9.5 && play > -10.5, "play test", play);
    }

    {
        std::printf ("\nNoise-reduction spec at -60 dB: 10 dB 30 Hz-5 kHz, rising to 15 dB at 15 kHz\n ");
        for (double f : { 30.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0 })
            std::printf (" %5.0f Hz %+5.1f", f, gainDb (all, f, -60.0));
        std::printf ("\n");
        const double lo = gainDb (all, 100, -60), hi = gainDb (all, 15000, -60);
        check (lo > 9.0 && lo < 11.0, "NR at 100 Hz", lo);
        check (hi > 13.5 && hi < 16.0, "NR at 15 kHz", hi);
    }

    const double freqs[] = { 50, 300, 1000, 4000, 12000 };
    const double levels[] = { -60, -50, -40, -30, -20, -10, 0 };
    for (int cfg = 0; cfg < 2; ++cfg)
    {
        Settings s;
        if (cfg == 1) s.bandOn = { false, false, true, true };
        std::printf ("\n%s - encode boost (dB) vs level re reference\n",
                     cfg == 0 ? "All four bands" : "Pulled card (bands 3+4)");
        std::printf ("  level ");
        for (double f : freqs) std::printf ("%8.0fHz", f);
        std::printf ("\n");
        for (double l : levels)
        {
            std::printf ("%6.0f  ", l);
            for (double f : freqs) std::printf ("%10.2f", gainDb (s, f, l));
            std::printf ("\n");
        }
    }

    {
        // +8 dBm against Dolby Level (0 VU = +4 dBm) is +4 dB re reference
        std::printf ("\nTHD at +4 dB re reference (= +8 dBm): spec < 0.1 %% at 1 kHz, < 0.2 %% 40 Hz-20 kHz\n ");
        for (double f : { 40.0, 100.0, 1000.0, 5000.0, 10000.0 })
        {
            const double t = thd (all, f, 4.0);
            std::printf (" %5.0f Hz %.3f%%", f, t);
            check (t < (std::abs (f - 1000.0) < 1.0 ? 0.1 : 0.2), "THD spec", t);
        }
        std::printf ("\n  worst case across levels (informative):\n ");
        for (double f : { 40.0, 100.0, 1000.0 })
        {
            double worst = 0, at = 0;
            for (double l = -50; l <= 0; l += 10) { const double t = thd (all, f, l); if (t > worst) { worst = t; at = l; } }
            std::printf (" %5.0f Hz %.3f%% at %.0f dB", f, worst, at);
        }
        std::printf ("\n");
    }

    // Encode -> decode null on noisy, dynamic material
    for (int lim = 0; lim < 2; ++lim)
    {
        Settings s; s.limiter = lim == 1;
        Engine enc, dec; enc.prepare (kFs); dec.prepare (kFs);
        enc.setSettings (s); dec.setSettings (s);
        std::mt19937 rng (1); std::normal_distribution<double> nd;
        double errSq = 0, sigSq = 0;
        for (int i = 0; i < (int) kFs * 2; ++i)
        {
            const double envl = (i / 9600) % 2 ? 1.0 : 0.003;      // bursts: 0 dB / -50 dB
            const double x = envl * 0.3 * nd (rng);
            const double y = dec.process (enc.process (x, Engine::Direction::Encode),
                                          Engine::Direction::Decode);
            errSq += (y - x) * (y - x); sigSq += x * x;
        }
        const double nullDb = 10.0 * std::log10 (errSq / sigSq + 1e-300);
        std::printf ("\nEncode->decode null (limiter %s): %.1f dB\n", lim ? "on" : "off", nullDb);
        check (nullDb < -80.0, "null", nullDb);
    }

    {
        // Overshoot: tone burst from -40 dB to 0 dB; spec says under 2 dB
        Engine e; e.prepare (kFs);
        double peakSteady = 0, peakOnset = 0;
        for (int i = 0; i < (int) (kFs * 3); ++i)
        {
            const double amp = i < (int) (kFs * 2) ? 0.01 : 1.0;
            const double y = e.process (amp * std::sin (2.0 * kPi * 1000.0 * i / kFs), Engine::Direction::Encode);
            if (i >= (int) (kFs * 2) && i < (int) (kFs * 2.02)) peakOnset = std::max (peakOnset, std::abs (y));
            if (i >= (int) (kFs * 2.8)) peakSteady = std::max (peakSteady, std::abs (y));
        }
        const double os = 20.0 * std::log10 (peakOnset / peakSteady);
        std::printf ("\nOvershoot, -40 -> 0 dB burst at 1 kHz: %.2f dB (spec < 2 dB)\n", os);
        check (os < 2.0, "overshoot", os);
    }

    {
        const double fs = 96000.0;
        std::printf ("\nInput filter response:");
        const double fr[] = { 1000, 10000, 20000 }, expect[] = { 0.0, 0.42, 1.57 };
        for (int k = 0; k < 3; ++k)
        {
            InputFilter lp; lp.prepare (fs);
            double inSq = 0, outSq = 0;
            for (int i = 0; i < 96000; ++i)
            {
                const double x = std::sin (2.0 * kPi * fr[k] * i / fs), y = lp.process (x);
                if (i > 9600) { inSq += x * x; outSq += y * y; }
            }
            const double db = 10.0 * std::log10 (outSq / inSq);
            std::printf ("  %.0f Hz %+.2f dB", fr[k], db);
            check (std::abs (db - expect[k]) < 0.25, "input filter", db);
        }
        std::printf ("\n");
    }

    {
        // VU meter: sine at reference reads 0 VU; rise time ~300 ms to 99 %
        VuMeter vu; vu.prepare (kFs);
        double at300 = 0;
        for (int i = 0; i < (int) kFs * 2; ++i)
        {
            vu.process (std::sin (2.0 * kPi * 1000.0 * i / kFs));
            if (i == (int) (0.3 * kFs)) at300 = std::pow (10.0, vu.readingDb() / 20.0);
        }
        const double steady = vu.readingDb();
        std::printf ("\nVU meter: reference sine reads %+.2f VU, %.1f %% of final after 300 ms\n", steady, 100.0 * at300);
        check (std::abs (steady) < 0.1, "VU calibration", steady);
        check (at300 > 0.97 && at300 < 1.01, "VU rise time", at300);
    }

    {
        // Learn: phrases with pauses; louder passages should land at 0 VU after
        // applying the result, whatever the starting level.
        for (double startDb : { -6.0, 6.0, 12.0 })
        {
            auto runPhrase = [&] (VuMeter& vu, LevelLearner* lrn, double gainDb)
            {
                std::mt19937 rng (7); std::normal_distribution<double> nd;
                const double g = std::pow (10.0, gainDb / 20.0);
                double maxReading = -100;
                const int dec = (int) (kFs / 1000.0);
                for (int i = 0; i < (int) (kFs * 12); ++i)
                {
                    const double t = i / kFs;
                    const double phrase = std::fmod (t, 3.0) < 2.2 ? 1.0 : 0.0;        // 0.8 s gaps
                    const double syll = 0.55 + 0.45 * std::sin (2.0 * kPi * 4.0 * t);  // syllable movement
                    vu.process (g * phrase * syll * 0.4 * nd (rng));
                    if (i % dec == 0)
                    {
                        if (lrn) lrn->feed (vu.readingDb());
                        if (t > 6.0) maxReading = std::max (maxReading, vu.readingDb());
                    }
                }
                return maxReading;
            };
            VuMeter vu; vu.prepare (kFs);
            LevelLearner lrn; lrn.start (5000);
            runPhrase (vu, &lrn, startDb);
            const double setInput = -lrn.resultDb();
            VuMeter vu2; vu2.prepare (kFs);
            LevelLearner p90; p90.start (1 << 30);
            // re-run at the learned setting and look at the 90th percentile reading
            {
                std::mt19937 rng (7); std::normal_distribution<double> nd;
                const double g = std::pow (10.0, (startDb + setInput) / 20.0);
                const int dec = (int) (kFs / 1000.0);
                for (int i = 0; i < (int) (kFs * 12); ++i)
                {
                    const double t = i / kFs;
                    const double phrase = std::fmod (t, 3.0) < 2.2 ? 1.0 : 0.0;
                    const double syll = 0.55 + 0.45 * std::sin (2.0 * kPi * 4.0 * t);
                    vu2.process (g * phrase * syll * 0.4 * nd (rng));
                    if (i % dec == 0) p90.feed (vu2.readingDb());
                }
            }
            std::printf ("Learn: track %+.0f dB hot -> Input %+.1f dB, louder passages then read %+.2f VU (finished: %s)\n",
                         startDb, setInput, p90.resultDb(), lrn.isFinished() ? "yes" : "no");
            check (lrn.isFinished(), "learn finished", 0);
            check (std::abs (p90.resultDb()) < 0.5, "learned alignment", p90.resultDb());
        }
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL CHECKS PASSED\n", failures);
    return failures;
}
