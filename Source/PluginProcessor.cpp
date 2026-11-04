#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct Preset
    {
        const char* name;
        Mode mode;
        float input, cal, depth, speed, mismatch, mix, output;
        bool limiter, link, sideOnly;
        std::array<bool, typea::kNumBands> bands;
    };

    //                        name                          mode               in    cal    dep   spd  mism  mix  out   lim   link  side   bands
    const Preset presets[] = {
        { "A301 Stretch (bands 3+4)",    Mode::Encode,        0, -18, 100, 1.0f,  0, 100,  0, true,  true, false, { false, false, true,  true  } },
        { "Full Card Encode",            Mode::Encode,        0, -18, 100, 1.0f,  0, 100,  0, true,  true, false, { true,  true,  true,  true  } },
        { "Air Only (band 4)",           Mode::Encode,        0, -18, 100, 1.0f,  0, 100,  0, true,  true, false, { false, false, false, true  } },
        { "Presence + Air (bands 2-4)",  Mode::Encode,        0, -18, 100, 1.0f,  0, 100,  0, true,  true, false, { false, true,  true,  true  } },
        { "Parallel Stretch",            Mode::Encode,       -6, -18, 120, 1.0f,  0,  40,  0, true,  true, false, { false, false, true,  true  } },
        { "Deep Stretch (low drive)",    Mode::Encode,      -12, -18, 100, 1.0f,  0, 100,  0, true,  true, false, { false, false, true,  true  } },
        { "Spitty (limiters off)",       Mode::Encode,        0, -18, 100, 1.5f,  0, 100,  0, false, true, false, { false, false, true,  true  } },
        { "Side Chain Only (send)",      Mode::Encode,        0, -18, 100, 1.0f,  0, 100,  0, true,  true, true,  { false, false, true,  true  } },
        { "Decode Smooth (de-ess)",      Mode::Decode,        0, -18, 100, 1.0f,  0, 100,  0, true,  true, false, { false, false, true,  true  } },
        { "Decode Full Card",            Mode::Decode,        0, -18, 100, 1.0f,  0, 100,  0, true,  true, false, { true,  true,  true,  true  } },
        { "Mismatch +6 (gentle lift)",   Mode::EncodeDecode,  0, -18, 100, 1.0f,  6, 100,  0, true,  true, false, { true,  true,  true,  true  } },
        { "Mismatch -6 (expand/pump)",   Mode::EncodeDecode,  0, -18, 100, 1.0f, -6, 100,  0, true,  true, false, { true,  true,  true,  true  } },
    };
    constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));

    inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
}

//==============================================================================
AirStretchProcessor::AirStretchProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pMode     = apvts.getRawParameterValue (ids::mode);
    pInput    = apvts.getRawParameterValue (ids::input);
    pCal      = apvts.getRawParameterValue (ids::cal);
    pDepth    = apvts.getRawParameterValue (ids::depth);
    pSpeed    = apvts.getRawParameterValue (ids::speed);
    pMismatch = apvts.getRawParameterValue (ids::mismatch);
    pMix      = apvts.getRawParameterValue (ids::mix);
    pOutput   = apvts.getRawParameterValue (ids::output);
    pLimiter  = apvts.getRawParameterValue (ids::limiter);
    pLink     = apvts.getRawParameterValue (ids::link);
    pListen   = apvts.getRawParameterValue (ids::listen);
    pInFilter = apvts.getRawParameterValue (ids::inFilter);
    for (int b = 0; b < typea::kNumBands; ++b)
        pBand[b] = apvts.getRawParameterValue (ids::band[b]);

    for (auto& m : bandMeterDb) m.store (0.0f);
    startTimerHz (10);
}

void AirStretchProcessor::timerCallback()
{
    if (getLearnState() != LearnState::Done)
        return;

    const float newInput = juce::jlimit (-24.0f, 24.0f, learnResultInputDb.load());
    if (auto* param = apvts.getParameter (ids::input))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (newInput));
        param->endChangeGesture();
    }
    lastLearnedInputDb.store (newInput);
    learnCount.fetch_add (1);
    learnState.store ((int) LearnState::Idle);
}

juce::AudioProcessorValueTreeState::ParameterLayout AirStretchProcessor::createLayout()
{
    using namespace juce;
    using compat::floatParam; using compat::choiceParam; using compat::boolParam;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto dB   = [] (float v, int) { return String (v, 1) + " dB"; };
    auto dBFS = [] (float v, int) { return String (v, 1) + " dBFS"; };
    auto pct  = [] (float v, int) { return String ((int) v) + " %"; };
    auto mult = [] (float v, int) { return String (v, 2) + "x"; };

    p.push_back (choiceParam (ids::mode, "Mode", StringArray { "Encode", "Decode", "Encode > Decode" }, 0));
    p.push_back (floatParam (ids::input, "Input", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, "dB", dB));
    p.push_back (floatParam (ids::cal, "Reference Level", NormalisableRange<float> (-30.0f, -6.0f, 0.5f), -18.0f, "dBFS", dBFS));
    p.push_back (floatParam (ids::depth, "Depth", NormalisableRange<float> (0.0f, 150.0f, 1.0f), 100.0f, "%", pct));
    p.push_back (floatParam (ids::speed, "Speed", NormalisableRange<float> (0.25f, 4.0f, 0.01f, 0.5f), 1.0f, "", mult));
    p.push_back (floatParam (ids::mismatch, "Mismatch", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, "dB", dB));
    p.push_back (floatParam (ids::mix, "Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, "%", pct));
    p.push_back (floatParam (ids::output, "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, "dB", dB));
    p.push_back (boolParam (ids::limiter, "Overshoot Limiter", true));
    p.push_back (boolParam (ids::link, "Link In/Out", true));
    p.push_back (boolParam (ids::inFilter, "Input Filter", true));
    p.push_back (choiceParam (ids::listen, "Listen", StringArray { "Output", "Side Chain" }, 0));

    const char* bandNames[] { "Band 1 (80 Hz LP)", "Band 2 (80 Hz-3 kHz)", "Band 3 (3 kHz HP)", "Band 4 (9.6 kHz HP)" };
    const bool bandDefaults[] { false, false, true, true };   // default = the classic stretch
    for (int b = 0; b < typea::kNumBands; ++b)
        p.push_back (boolParam (ids::band[b], bandNames[b], bandDefaults[b]));

    return { p.begin(), p.end() };
}

bool AirStretchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void AirStretchProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = juce::jmax (1, getTotalNumOutputChannels());

    // 2x oversampling keeps the overshoot limiters and the 9 kHz band clean.
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numCh, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampling->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples ((int) std::round (oversampling->getLatencyInSamples()));

    const double osRate = sampleRate * 2.0;
    for (int c = 0; c < kMaxChannels; ++c)
    {
        encoders[c].prepare (osRate);
        decoders[c].prepare (osRate);
        inputFilters[c].prepare (osRate);
        vuPost[c].prepare (osRate);
        vuRaw[c].prepare (osRate);
    }
    vuDecimation = juce::jmax (1, (int) std::round (osRate / 1000.0));   // meter/learn tick: 1 ms
    vuCounter = 0;

    for (auto* s : { &inGain, &outGain, &mixAmt, &mismatchGain })
        s->reset (osRate, 0.03);
    inGain.setCurrentAndTargetValue (dbToGain (pInput->load()));
    outGain.setCurrentAndTargetValue (dbToGain (pOutput->load()));
    mixAmt.setCurrentAndTargetValue (pMix->load() * 0.01f);
    mismatchGain.setCurrentAndTargetValue (dbToGain (pMismatch->load()));
}

void AirStretchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), kMaxChannels);
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    const auto mode     = (Mode) (int) pMode->load();
    const bool link     = pLink->load() > 0.5f;
    const bool sideOnly = (int) pListen->load() == 1;
    const float calLin  = dbToGain (pCal->load());
    const bool inFilter = pInFilter->load() > 0.5f;

    typea::Settings s;
    s.depth   = pDepth->load() * 0.01;
    s.speed   = pSpeed->load();
    s.limiter = pLimiter->load() > 0.5f;
    for (int b = 0; b < typea::kNumBands; ++b)
        s.bandOn[(size_t) b] = pBand[b]->load() > 0.5f;
    for (int c = 0; c < kMaxChannels; ++c)
    {
        encoders[c].setSettings (s);
        decoders[c].setSettings (s);
    }

    inGain.setTargetValue (dbToGain (pInput->load()));
    outGain.setTargetValue (dbToGain (pOutput->load()));
    mixAmt.setTargetValue (pMix->load() * 0.01f);
    mismatchGain.setTargetValue (dbToGain (pMismatch->load()));

    // Learn requests from the editor
    if (const int req = learnRequest.exchange (0); req == 1)
        learner.start ((int) (kLearnSeconds * 1000.0));
    else if (req == 2)
        learner.cancel();

    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh,
                                        (size_t) buffer.getNumSamples());
    auto up = oversampling->processSamplesUp (block);
    const int n = (int) up.getNumSamples();

    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float drive = inGain.getNextValue();
        const float post  = outGain.getNextValue();
        const float mix   = mixAmt.getNextValue();
        const float mm    = mismatchGain.getNextValue();
        const double toInternal = drive / calLin;
        const double fromInternal = calLin / (link ? drive : 1.0f);

        for (int c = 0; c < numCh; ++c)
        {
            float* d = up.getChannelPointer ((size_t) c);
            const float dry = d[i];
            vuRaw[c].process (dry / calLin);          // before Input: used by Learn
            double x = dry * toInternal;
            vuPost[c].process (x);                    // what the unit sees: the meter
            if (inFilter) x = inputFilters[c].process (x);
            double y;

            switch (mode)
            {
                case Mode::Encode:
                    y = encoders[c].process (x, typea::Engine::Direction::Encode);
                    break;
                case Mode::Decode:
                    y = decoders[c].process (x, typea::Engine::Direction::Decode);
                    break;
                case Mode::EncodeDecode:
                default:
                    y = encoders[c].process (x, typea::Engine::Direction::Encode);
                    y = decoders[c].process (y * mm, typea::Engine::Direction::Decode) / mm;
                    break;
            }

            const float wet = (float) (y * fromInternal);
            float out = sideOnly ? (wet - dry * (link ? 1.0f : drive))
                                 : dry + mix * (wet - dry);
            out *= post;
            d[i] = out;
            peak = juce::jmax (peak, std::abs (out));
        }

        if (++vuCounter >= vuDecimation)
        {
            vuCounter = 0;
            double vuNow = -100.0, raw = -100.0;
            for (int c = 0; c < numCh; ++c)
            {
                vuNow = juce::jmax (vuNow, vuPost[c].readingDb());
                raw   = juce::jmax (raw,   vuRaw[c].readingDb());
            }
            vuDb.store ((float) vuNow);
            if (learner.isActive())
            {
                if (learner.feed (raw))
                {
                    learnResultInputDb.store ((float) -learner.resultDb());
                    learnState.store ((int) LearnState::Done);
                }
                learnProgress.store ((float) learner.progress());
            }
        }
    }

    oversampling->processSamplesDown (block);

    // Meters: signed low-level boost per band (negative = cut while decoding)
    for (int b = 0; b < typea::kNumBands; ++b)
    {
        float v = 0.0f;
        switch (mode)
        {
            case Mode::Encode:       v = (float) encoders[0].bandBoostDb (b); break;
            case Mode::Decode:       v = (float) -decoders[0].bandBoostDb (b); break;
            case Mode::EncodeDecode: v = (float) (encoders[0].bandBoostDb (b) - decoders[0].bandBoostDb (b)); break;
        }
        bandMeterDb[(size_t) b].store (v);
    }
    outMeterDb.store (juce::Decibels::gainToDecibels (peak, -100.0f));
}

//==============================================================================
int AirStretchProcessor::getNumPrograms() { return numPresets; }

const juce::String AirStretchProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, numPresets) ? presets[index].name : "";
}

void AirStretchProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, numPresets))
        return;
    currentProgram = index;
    const auto& pr = presets[index];

    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set (ids::mode, (float) (int) pr.mode);
    set (ids::input, pr.input);
    set (ids::cal, pr.cal);
    set (ids::depth, pr.depth);
    set (ids::speed, pr.speed);
    set (ids::mismatch, pr.mismatch);
    set (ids::mix, pr.mix);
    set (ids::output, pr.output);
    set (ids::limiter, pr.limiter ? 1.0f : 0.0f);
    set (ids::link, pr.link ? 1.0f : 0.0f);
    set (ids::listen, pr.sideOnly ? 1.0f : 0.0f);
    for (int b = 0; b < typea::kNumBands; ++b)
        set (ids::band[b], pr.bands[(size_t) b] ? 1.0f : 0.0f);
}

void AirStretchProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AirStretchProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = state.getProperty ("program", 0);
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* AirStretchProcessor::createEditor()
{
    return new AirStretchEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AirStretchProcessor();
}
