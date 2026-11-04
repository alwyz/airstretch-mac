#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
class StretchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StretchLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh,
                       juce::ComboBox&) override;
};

//==============================================================================
class BandMeter : public juce::Component
{
public:
    void setValue (float db, bool active);
    void paint (juce::Graphics&) override;
private:
    float value = 0.0f;
    bool on = true;
};

//==============================================================================
// Analog-style VU meter, like the 361's panel meter: 0 VU = reference level.
class VuMeterView : public juce::Component
{
public:
    void setValue (float db);
    void paint (juce::Graphics&) override;
private:
    float value = -60.0f;
};

//==============================================================================
class AirStretchEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AirStretchEditor (AirStretchProcessor&);
    ~AirStretchEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addKnob (juce::Slider&, juce::Label&, const char* paramId, const juce::String& name);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    AirStretchProcessor& proc;
    StretchLookAndFeel lnf;

    juce::ComboBox presetBox, modeBox, listenBox;
    std::unique_ptr<ComboAttachment> modeAtt, listenAtt;

    static constexpr int kNumKnobs = 7;
    juce::Slider knobs[kNumKnobs];
    juce::Label knobLabels[kNumKnobs];
    std::vector<std::unique_ptr<SliderAttachment>> knobAtts;

    juce::ToggleButton bandButtons[typea::kNumBands];
    BandMeter meters[typea::kNumBands];
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAtts;

    juce::ToggleButton limiterButton { "Limiter" }, linkButton { "Link I/O" }, filterButton { "Input LPF" };

    VuMeterView vuMeter;
    juce::TextButton learnButton { "LEARN" };
    juce::Label learnStatus;
    void updateLearnUi();

    float meterSmooth[typea::kNumBands] {};
    juce::Rectangle<int> bandArea, controlArea, meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AirStretchEditor)
};
