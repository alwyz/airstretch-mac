/*
  JuceCompat.h — lets the same source build on JUCE 8 (current builds) and
  JUCE 6.1.6 (the legacy Mac build for OS X 10.8 / Pro Tools 11 via a wrapper).
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace compat
{
#if JUCE_MAJOR_VERSION >= 8
    inline juce::FontOptions font (float height, int style = juce::Font::plain) { return juce::FontOptions (height, style); }
#else
    inline juce::Font font (float height, int style = juce::Font::plain) { return juce::Font (height, style); }
#endif

    using StringFromFloat = std::function<juce::String (float, int)>;

    inline std::unique_ptr<juce::AudioParameterFloat> floatParam (const char* id, const juce::String& name,
                                                                  juce::NormalisableRange<float> range, float def,
                                                                  const juce::String& label, StringFromFloat toString)
    {
#if JUCE_MAJOR_VERSION >= 7
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def,
                   juce::AudioParameterFloatAttributes().withLabel (label).withStringFromValueFunction (std::move (toString)));
#else
        return std::make_unique<juce::AudioParameterFloat> (id, name, range, def, label,
                   juce::AudioProcessorParameter::genericParameter, std::move (toString));
#endif
    }

    inline std::unique_ptr<juce::AudioParameterChoice> choiceParam (const char* id, const juce::String& name,
                                                                    const juce::StringArray& choices, int def)
    {
#if JUCE_MAJOR_VERSION >= 7
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def);
#else
        return std::make_unique<juce::AudioParameterChoice> (id, name, choices, def);
#endif
    }

    inline std::unique_ptr<juce::AudioParameterBool> boolParam (const char* id, const juce::String& name, bool def)
    {
#if JUCE_MAJOR_VERSION >= 7
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
#else
        return std::make_unique<juce::AudioParameterBool> (id, name, def);
#endif
    }
}
