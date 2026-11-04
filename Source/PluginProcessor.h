#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "JuceCompat.h"
#include "dsp/TypeAEngine.h"
#include "dsp/LevelTools.h"

namespace ids
{
    inline constexpr auto mode     = "mode";
    inline constexpr auto input    = "input";
    inline constexpr auto cal      = "cal";
    inline constexpr auto depth    = "depth";
    inline constexpr auto speed    = "speed";
    inline constexpr auto mismatch = "mismatch";
    inline constexpr auto mix      = "mix";
    inline constexpr auto output   = "output";
    inline constexpr auto limiter  = "limiter";
    inline constexpr auto link     = "link";
    inline constexpr auto listen   = "listen";
    inline constexpr auto inFilter = "infilter";
    inline const char* const band[] { "band1", "band2", "band3", "band4" };
}

enum class Mode { Encode = 0, Decode, EncodeDecode };

class AirStretchProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    AirStretchProcessor();
    ~AirStretchProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Per-band low-level boost for the meters (dB), written by the audio thread.
    std::array<std::atomic<float>, typea::kNumBands> bandMeterDb {};
    std::atomic<float> outMeterDb { -100.0f };

    // Level meter: what the unit sees (after Input), in VU re reference.
    std::atomic<float> vuDb { -60.0f };

    // Learn: listen to the track, then set Input so its louder passages read 0 VU.
    enum class LearnState { Idle = 0, Listening, Done };
    static constexpr double kLearnSeconds = 5.0;
    void startLearn()  { learnRequest.store (1); learnState.store ((int) LearnState::Listening); learnProgress.store (0.0f); }
    void cancelLearn() { learnRequest.store (2); learnState.store ((int) LearnState::Idle); }
    LearnState getLearnState() const { return (LearnState) learnState.load(); }
    float getLearnProgress() const   { return learnProgress.load(); }
    // Input setting chosen by the last completed learn, and a counter that
    // changes each time one is applied (for the editor's status line).
    float getLastLearnedInputDb() const { return lastLearnedInputDb.load(); }
    int getLearnCount() const           { return learnCount.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;   // applies a finished learn on the message thread

    typea::VuMeter vuPost[2], vuRaw[2];
    typea::LevelLearner learner;
    int vuDecimation = 1, vuCounter = 0;
    std::atomic<int> learnRequest { 0 }, learnState { 0 }, learnCount { 0 };
    std::atomic<float> learnProgress { 0.0f }, learnResultInputDb { 0.0f }, lastLearnedInputDb { 0.0f };

    static constexpr int kMaxChannels = 2;
    typea::Engine encoders[kMaxChannels], decoders[kMaxChannels];
    typea::InputFilter inputFilters[kMaxChannels];
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    juce::SmoothedValue<float> inGain, outGain, mixAmt, mismatchGain;
    int currentProgram = 0;

    std::atomic<float>* pMode {};
    std::atomic<float>* pInput {};
    std::atomic<float>* pCal {};
    std::atomic<float>* pDepth {};
    std::atomic<float>* pSpeed {};
    std::atomic<float>* pMismatch {};
    std::atomic<float>* pMix {};
    std::atomic<float>* pOutput {};
    std::atomic<float>* pLimiter {};
    std::atomic<float>* pLink {};
    std::atomic<float>* pListen {};
    std::atomic<float>* pInFilter {};
    std::atomic<float>* pBand[typea::kNumBands] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AirStretchProcessor)
};
