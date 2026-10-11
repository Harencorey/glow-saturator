#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class GlowLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlowLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;

    static inline const juce::Colour bg { 0xff1b1714 }, panel { 0xff26201b }, section { 0xff2e2620 },
        amber { 0xffffa53a }, amberDim { 0xff7a4a1c }, text { 0xffe9dccb }, textDim { 0xff9c8c7a };
};

class GlowEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GlowEditor (GlowProcessor&);
    ~GlowEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, double resetTo);
    static void place (Knob&, juce::Rectangle<int>);

    GlowProcessor& proc;
    GlowLookAndFeel lnf;
    Knob drive, even, odd, density;
    Knob hpf, preFreq, preGain;
    Knob postFreq, postGain, mix, output;
    juce::TextButton linkButton { "LINK" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttach;

    juce::Rectangle<int> preArea, tubeArea, postArea, glowArea;
    float lastGlow = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlowEditor)
};
