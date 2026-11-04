#include "PluginEditor.h"

namespace colours
{
    const juce::Colour panel     { 0xff1c1d1f };
    const juce::Colour panelHi   { 0xff2a2b2e };
    const juce::Colour strip     { 0xff232427 };
    const juce::Colour line      { 0xff3a3b3f };
    const juce::Colour text      { 0xffd9d6cf };
    const juce::Colour dimText   { 0xff8d8a84 };
    const juce::Colour amber     { 0xffe8a33d };
    const juce::Colour amberDim  { 0xff6b4c1f };
    const juce::Colour cut       { 0xff5fa8d3 };
}

//==============================================================================
StretchLookAndFeel::StretchLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::ComboBox::backgroundColourId, colours::panelHi);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::outlineColourId, colours::line);
    setColour (juce::ComboBox::arrowColourId, colours::amber);
    setColour (juce::PopupMenu::backgroundColourId, colours::panelHi);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::amberDim);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void StretchLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool enabled = s.isEnabled();

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::line);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc (from centre for bipolar ranges)
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle)
                               : startAngle;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (enabled ? colours::amber : colours::amberDim);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body
    const float kr = r * 0.72f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4b4f), c.x, c.y - kr,
                                             juce::Colour (0xff1e1f21), c.x, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.0f);

    // pointer
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.5f, -kr + 3.0f, 3.0f, kr * 0.55f, 1.5f);
    g.setColour (enabled ? colours::text : colours::dimText);
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void StretchLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? colours::amber : colours::panelHi.brighter (highlighted ? 0.1f : 0.0f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? colours::amber.brighter (0.3f) : colours::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (on ? juce::Colour (0xff1a1206) : colours::dimText);
    g.setFont (compat::font (12.5f, juce::Font::bold));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

void StretchLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (colours::amber);
    g.fillPath (arrow);
}

//==============================================================================
void BandMeter::setValue (float db, bool active)
{
    if (std::abs (db - value) > 0.01f || active != on)
    {
        value = db; on = active;
        repaint();
    }
}

void BandMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff121314));
    g.fillRoundedRectangle (r, 3.0f);

    const float range = 15.0f;
    const float mid = r.getCentreY();
    const float half = r.getHeight() * 0.5f - 4.0f;

    // scale ticks every 5 dB
    g.setColour (colours::line);
    for (int db = -15; db <= 15; db += 5)
    {
        const float yy = mid - (float) db / range * half;
        g.drawHorizontalLine ((int) yy, r.getX() + 3.0f, r.getRight() - 3.0f);
    }
    g.setColour (colours::dimText);
    g.drawHorizontalLine ((int) mid, r.getX() + 1.0f, r.getRight() - 1.0f);

    if (! on) return;

    const float v = juce::jlimit (-range, range, value);
    const float yy = mid - v / range * half;
    auto bar = juce::Rectangle<float>::leftTopRightBottom (r.getX() + 6.0f, juce::jmin (mid, yy),
                                                           r.getRight() - 6.0f, juce::jmax (mid, yy));
    g.setColour (v >= 0.0f ? colours::amber : colours::cut);
    g.fillRect (bar);
}

//==============================================================================
void VuMeterView::setValue (float db)
{
    if (std::abs (db - value) > 0.02f)
    {
        value = db;
        repaint();
    }
}

void VuMeterView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const juce::Colour face (0xffe9dfc6), ink (0xff26221c), red (0xffc0392b);
    g.setColour (face);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

    // Scale is linear in voltage, as on a real VU meter: -20 VU to +3 VU.
    // A shallow arc: pivot well below the face, so the needle sweeps +/-36 degrees.
    const float linMin = 0.1f, linMax = 1.41254f;
    const float span = juce::degreesToRadians (36.0f);
    const float chord = r.getWidth() * 0.80f;
    const float radius = chord / (2.0f * std::sin (span));
    const float arcTop = r.getY() + r.getHeight() * 0.42f;
    const auto pivot = juce::Point<float> (r.getCentreX(), arcTop + radius);
    auto angleFor = [&] (float db)
    {
        const float lin = std::pow (10.0f, juce::jlimit (-24.0f, 4.0f, db) / 20.0f);
        const float pos = (lin - linMin) / (linMax - linMin);
        return -span + 2.0f * span * juce::jlimit (-0.05f, 1.03f, pos);
    };
    auto pointAt = [&] (float angle, float rad)
    {
        return juce::Point<float> (pivot.x + rad * std::sin (angle), pivot.y - rad * std::cos (angle));
    };

    // red zone 0..+3
    juce::Path redArc;
    redArc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (0.0f), angleFor (3.0f), true);
    g.setColour (red);
    g.strokePath (redArc, juce::PathStrokeType (4.0f));
    juce::Path arc;
    arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (-20.0f), angleFor (0.0f), true);
    g.setColour (ink);
    g.strokePath (arc, juce::PathStrokeType (1.2f));

    g.setFont (compat::font (9.5f, juce::Font::bold));
    for (int db : { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 })
    {
        const float a = angleFor ((float) db);
        const bool major = db == -20 || db == -10 || db == -5 || db == 0 || db == 3;
        g.setColour (db > 0 ? red : ink);
        g.drawLine (juce::Line<float> (pointAt (a, radius), pointAt (a, radius + (major ? 7.0f : 4.5f))), 1.1f);
        const auto lp = pointAt (a, radius + 14.0f);
        const juce::String txt = db > 0 ? "+" + juce::String (db) : juce::String (std::abs (db));
        g.drawText (txt, juce::Rectangle<float> (lp.x - 12.0f, lp.y - 7.0f, 24.0f, 14.0f), juce::Justification::centred);
    }

    g.setColour (ink);
    g.setFont (compat::font (13.0f, juce::Font::bold));
    g.drawText ("VU", juce::Rectangle<float> (r.getX(), arcTop + 12.0f, r.getWidth(), 16.0f), juce::Justification::centred);

    // needle
    g.saveState();
    g.reduceClipRegion (getLocalBounds().reduced (2));
    const float a = angleFor (value);
    g.setColour (ink);
    g.drawLine (juce::Line<float> (pointAt (a, radius * 0.7f), pointAt (a, radius + 8.0f)), 1.6f);
    g.restoreState();
}

//==============================================================================
AirStretchEditor::AirStretchEditor (AirStretchProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    // Presets
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    modeBox.addItemList ({ "Encode", "Decode", "Encode > Decode" }, 1);
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<ComboAttachment> (proc.apvts, ids::mode, modeBox);

    listenBox.addItemList ({ "Listen: Output", "Listen: Side Chain" }, 1);
    addAndMakeVisible (listenBox);
    listenAtt = std::make_unique<ComboAttachment> (proc.apvts, ids::listen, listenBox);

    // Knobs
    addKnob (knobs[0], knobLabels[0], ids::input,    "INPUT");
    addKnob (knobs[1], knobLabels[1], ids::cal,      "REF LEVEL");
    addKnob (knobs[2], knobLabels[2], ids::depth,    "DEPTH");
    addKnob (knobs[3], knobLabels[3], ids::speed,    "SPEED");
    addKnob (knobs[4], knobLabels[4], ids::mismatch, "MISMATCH");
    addKnob (knobs[5], knobLabels[5], ids::mix,      "MIX");
    addKnob (knobs[6], knobLabels[6], ids::output,   "OUTPUT");

    // Band "cards"
    const char* bandText[] { "BAND 1", "BAND 2", "BAND 3", "BAND 4" };
    for (int b = 0; b < typea::kNumBands; ++b)
    {
        bandButtons[b].setButtonText (bandText[b]);
        addAndMakeVisible (bandButtons[b]);
        addAndMakeVisible (meters[b]);
        buttonAtts.push_back (std::make_unique<ButtonAttachment> (proc.apvts, ids::band[b], bandButtons[b]));
    }

    addAndMakeVisible (limiterButton);
    addAndMakeVisible (linkButton);
    addAndMakeVisible (filterButton);
    buttonAtts.push_back (std::make_unique<ButtonAttachment> (proc.apvts, ids::inFilter, filterButton));
    buttonAtts.push_back (std::make_unique<ButtonAttachment> (proc.apvts, ids::limiter, limiterButton));
    buttonAtts.push_back (std::make_unique<ButtonAttachment> (proc.apvts, ids::link, linkButton));

    addAndMakeVisible (vuMeter);

    learnButton.setClickingTogglesState (false);
    learnButton.setColour (juce::TextButton::buttonColourId, colours::panelHi);
    learnButton.setColour (juce::TextButton::buttonOnColourId, colours::amber);
    learnButton.setColour (juce::TextButton::textColourOffId, colours::text);
    learnButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1206));
    learnButton.setTooltip ("Listen to the track and set Input so its louder passages read 0 VU");
    learnButton.onClick = [this]
    {
        if (proc.getLearnState() == AirStretchProcessor::LearnState::Idle)
            proc.startLearn();
        else
            proc.cancelLearn();
        updateLearnUi();
    };
    addAndMakeVisible (learnButton);

    learnStatus.setFont (compat::font (12.5f));
    learnStatus.setColour (juce::Label::textColourId, colours::dimText);
    learnStatus.setJustificationType (juce::Justification::centredLeft);
    learnStatus.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (learnStatus);
    updateLearnUi();

    setSize (840, 572);
    startTimerHz (30);
}

AirStretchEditor::~AirStretchEditor()
{
    setLookAndFeel (nullptr);
}

void AirStretchEditor::addKnob (juce::Slider& s, juce::Label& l, const char* paramId, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff151618));
    s.setColour (juce::Slider::textBoxTextColourId, colours::text);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (s);
    knobAtts.push_back (std::make_unique<SliderAttachment> (proc.apvts, paramId, s));

    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (compat::font (12.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, colours::dimText);
    addAndMakeVisible (l);
}

void AirStretchEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);

    // header
    auto header = getLocalBounds().removeFromTop (58);
    g.setColour (colours::panelHi);
    g.fillRect (header);
    g.setColour (colours::line);
    g.drawHorizontalLine (header.getBottom(), 0.0f, (float) getWidth());

    g.setColour (colours::amber);
    g.setFont (compat::font (24.0f, juce::Font::bold));
    g.drawText ("AIRSTRETCH", 20, 8, 220, 28, juce::Justification::centredLeft);
    g.setColour (colours::dimText);
    g.setFont (compat::font (11.5f));
    g.drawText ("A-TYPE FOUR-BAND VOCAL STRETCH", 21, 33, 260, 16, juce::Justification::centredLeft);

    // band rack
    g.setColour (colours::strip);
    g.fillRoundedRectangle (bandArea.toFloat(), 6.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (bandArea.toFloat(), 6.0f, 1.0f);

    const char* bandFreq[] { "80 Hz LP", "80 Hz - 3 kHz", "3 kHz HP", "9.6 kHz HP" };
    const int cardW = bandArea.getWidth() / typea::kNumBands;
    g.setFont (compat::font (11.5f));
    for (int b = 0; b < typea::kNumBands; ++b)
    {
        auto card = juce::Rectangle<int> (bandArea.getX() + b * cardW, bandArea.getY(), cardW, bandArea.getHeight());
        if (b > 0)
        {
            g.setColour (colours::line);
            g.drawVerticalLine (card.getX(), (float) card.getY() + 10, (float) card.getBottom() - 10);
        }
        g.setColour (colours::dimText);
        g.drawText (bandFreq[b], card.getX(), card.getY() + 10, cardW, 16, juce::Justification::centred);
    }

    g.setColour (colours::dimText);
    g.setFont (compat::font (10.5f));
    g.drawText ("Switch a band off to pull its card. Meters: boost (amber) / cut (blue), +/-15 dB",
                bandArea.getX(), bandArea.getBottom() + 6, bandArea.getWidth(), 16, juce::Justification::centredLeft);

    // level strip
    g.setColour (colours::strip);
    g.fillRoundedRectangle (meterArea.toFloat(), 6.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (meterArea.toFloat(), 6.0f, 1.0f);
    g.setColour (colours::dimText);
    g.setFont (compat::font (11.0f, juce::Font::bold));
    g.drawText ("LEVEL INTO THE UNIT", learnStatus.getX(), meterArea.getY() + 12, 300, 14, juce::Justification::centredLeft);

    // control panel
    g.setColour (colours::strip);
    g.fillRoundedRectangle (controlArea.toFloat(), 6.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (controlArea.toFloat(), 6.0f, 1.0f);
}

void AirStretchEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (58).reduced (16, 14);
    presetBox.setBounds (header.removeFromRight (250));
    header.removeFromRight (10);
    modeBox.setBounds (header.removeFromRight (160));
    header.removeFromRight (10);
    listenBox.setBounds (header.removeFromRight (160));

    area.reduce (16, 14);
    meterArea = area.removeFromBottom (100);
    area.removeFromBottom (12);
    {
        auto m = meterArea.reduced (12, 10);
        vuMeter.setBounds (m.removeFromLeft (230));
        m.removeFromLeft (20);
        learnButton.setBounds (m.removeFromLeft (130).withSizeKeepingCentre (130, 40));
        m.removeFromLeft (20);
        learnStatus.setBounds (m.withTrimmedTop (16));
    }
    bandArea = area.removeFromLeft (380).withTrimmedBottom (24);
    area.removeFromLeft (14);
    controlArea = area;

    // band cards
    const int cardW = bandArea.getWidth() / typea::kNumBands;
    for (int b = 0; b < typea::kNumBands; ++b)
    {
        auto card = juce::Rectangle<int> (bandArea.getX() + b * cardW, bandArea.getY(), cardW, bandArea.getHeight());
        card.removeFromTop (32);
        bandButtons[b].setBounds (card.removeFromBottom (40).reduced (10, 6));
        meters[b].setBounds (card.reduced (26, 4));
    }

    // knobs: row 1 = drive section, row 2 = shaping/output
    auto ctl = controlArea.reduced (10);
    auto toggles = ctl.removeFromBottom (36);
    const int rowH = ctl.getHeight() / 2;
    auto row1 = ctl.removeFromTop (rowH);
    auto row2 = ctl;

    auto placeRow = [this] (juce::Rectangle<int> row, std::initializer_list<int> idx)
    {
        const int w = row.getWidth() / (int) idx.size();
        for (int i : idx)
        {
            auto cell = row.removeFromLeft (w).reduced (4, 2);
            knobLabels[i].setBounds (cell.removeFromTop (16));
            knobs[i].setBounds (cell);
        }
    };
    placeRow (row1, { 0, 1, 2, 3 });
    placeRow (row2, { 4, 5, 6 });

    toggles = toggles.reduced (16, 4);
    const int tw = toggles.getWidth() / 3;
    limiterButton.setBounds (toggles.removeFromLeft (tw).reduced (8, 0));
    linkButton.setBounds (toggles.removeFromLeft (tw).reduced (8, 0));
    filterButton.setBounds (toggles.reduced (8, 0));
}

void AirStretchEditor::timerCallback()
{
    const auto mode = (Mode) (int) proc.apvts.getRawParameterValue (ids::mode)->load();
    knobs[4].setEnabled (mode == Mode::EncodeDecode);
    knobLabels[4].setAlpha (mode == Mode::EncodeDecode ? 1.0f : 0.4f);

    for (int b = 0; b < typea::kNumBands; ++b)
    {
        const float target = proc.bandMeterDb[(size_t) b].load();
        meterSmooth[b] += 0.35f * (target - meterSmooth[b]);
        meters[b].setValue (meterSmooth[b], bandButtons[b].getToggleState());
    }

    vuMeter.setValue (proc.vuDb.load());
    updateLearnUi();

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
}

void AirStretchEditor::updateLearnUi()
{
    const bool listening = proc.getLearnState() != AirStretchProcessor::LearnState::Idle;
    learnButton.setToggleState (listening, juce::dontSendNotification);

    juce::String status;
    if (listening)
    {
        const int pct = juce::jlimit (0, 100, (int) (proc.getLearnProgress() * 100.0f));
        learnButton.setButtonText ("LISTENING " + juce::String (pct) + "%");
        status = "Play the track: " + juce::String ((int) AirStretchProcessor::kLearnSeconds)
               + " seconds of audio needed (silence is skipped). Click again to cancel.";
    }
    else
    {
        learnButton.setButtonText ("LEARN");
        const float cal = proc.apvts.getRawParameterValue (ids::cal)->load();
        if (proc.getLearnCount() > 0)
        {
            const float in = proc.getLastLearnedInputDb();
            status = "Input set to " + juce::String (in, 1) + " dB: the louder passages now read 0 VU. "
                     "Lower Input for more stretch, raise it for less.";
        }
        else
        {
            status = "0 VU = reference level (" + juce::String (cal, 1) + " dBFS sine). Play the track and press "
                     "Learn to line it up at 0 VU, as on the hardware.";
        }
    }
    if (learnStatus.getText() != status)
        learnStatus.setText (status, juce::dontSendNotification);
}
