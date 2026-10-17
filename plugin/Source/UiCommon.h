#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Shared look-and-feel and small helpers used by the main editor and the module panels.
class GlowLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlowLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getSliderPopupFont (juce::Slider&) override { return juce::FontOptions (12.0f, juce::Font::bold); }
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>&) override;

    // Refined graphite faceplate with copper signal accents; semantic status colours retained.
    static inline const juce::Colour bg { 0xff111418 }, panel { 0xff1a1e23 }, section { 0xff23282e },
        amber { 0xffeda666 }, amberDim { 0xff77513a }, text { 0xfff0eee9 }, textDim { 0xffa0a5a9 },
        well { 0xff101317 }, edge { 0xff343c43 }, lampBg { 0xff34261f },
        green { 0xff8fb85a }, red { 0xffe5533d }, yellow { 0xfff2cf4a };

    // a lit indicator dot (buttons, the pilot lamp)
    static void drawLamp (juce::Graphics& g, juce::Point<float> c, float r, bool on)
    {
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (amber.withAlpha (0.45f), c.x, c.y, amber.withAlpha (0.0f), c.x + r * 3.0f, c.y, true));
            g.fillEllipse (c.x - r * 3.0f, c.y - r * 3.0f, r * 6.0f, r * 6.0f);
        }
        g.setColour (on ? amber.brighter (0.2f) : juce::Colour (0xff3a2a1a));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }
};

// A labelled rotary knob attached to a parameter.
struct Knob
{
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;

    void setup (juce::Component& parent, juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                const juce::String& name, double resetTo, float labelSize = 11.0f)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
        slider.setPopupDisplayEnabled (false, false, nullptr);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setDoubleClickReturnValue (true, resetTo);
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (labelSize, juce::Font::bold));
        parent.addAndMakeVisible (slider);
        parent.addAndMakeVisible (label);
        attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    }

    // hover help (shown on the knob and its name label)
    void tip (const juce::String& t) { slider.setTooltip (t); label.setTooltip (t); }

    void place (juce::Rectangle<int> r, int labelH = 16)
    {
        label.setBounds (r.removeFromTop (labelH));
        slider.setBounds (r);
    }

    // faceplate style: knob (+ value text if shown), then its name underneath
    void placeBelow (juce::Rectangle<int> r, int labelH = 15)
    {
        label.setBounds (r.removeFromBottom (labelH));
        slider.setBounds (r);
    }

    // no value box: the value pops up while you turn it (and shows in the tooltip host-side)
    void popupValue() { slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); slider.setPopupDisplayEnabled (true, true, nullptr); }
    // 0-10 scale printed around the knob
    void numbers() { slider.getProperties().set ("numbers", true); }
};

// A toggle button attached to a bool parameter.
struct ToggleParam
{
    juce::TextButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attach;

    void setup (juce::Component& parent, juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                const juce::String& text)
    {
        button.setButtonText (text);
        button.setClickingTogglesState (true);
        parent.addAndMakeVisible (button);
        attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, button);
    }
};

// A row/grid of buttons that selects one option of a choice parameter (e.g. compressor ratio).
class ChoiceButtons : public juce::Component
{
public:
    void tip (const juce::String& t) { for (auto* b : buttons) b->setTooltip (t); }

    ChoiceButtons (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, int columns)
        : cols (columns)
    {
        auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));
        jassert (p != nullptr);
        for (int i = 0; i < p->choices.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (p->choices[i]));
            b->setClickingTogglesState (false);
            b->onClick = [this, i] { attachment->setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (b);
        }
        attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { select ((int) std::lround (v)); });
        attachment->sendInitialUpdate();
    }

    void resized() override
    {
        const int rows = (buttons.size() + cols - 1) / cols;
        const int w = getWidth() / cols, h = getHeight() / juce::jmax (1, rows);
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setBounds (juce::Rectangle<int> ((i % cols) * w, (i / cols) * h, w, h).reduced (2));
    }

private:
    void select (int idx)
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
    }

    int cols;
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Vertical peak meter, -60..+6 dBFS, with a falling bar and a short peak hold.
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    void push (float peakLinear)
    {
        const float db = juce::Decibels::gainToDecibels (peakLinear, -100.0f);
        level = db > level ? db : level - 1.2f;            // ~36 dB/s fall at 30 Hz refresh
        if (db >= hold) { hold = db; holdTicks = 45; }      // ~1.5 s hold
        else if (--holdTicks < 0) hold -= 1.2f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r, 2.0f);
        auto toY = [&r] (float db) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
        const float top = toY (level);
        juce::ColourGradient grad (GlowLookAndFeel::green, 0.0f, r.getBottom(), GlowLookAndFeel::red, 0.0f, r.getY(), false);
        grad.addColour ((60.0 - 6.0) / 66.0, GlowLookAndFeel::amber);
        g.setGradientFill (grad);
        g.fillRect (r.withTop (top).reduced (1.5f, 0.0f));
        if (hold > -60.0f)
        {
            g.setColour (hold > 0.0f ? GlowLookAndFeel::red : GlowLookAndFeel::text);
            g.fillRect (r.getX() + 1.5f, toY (hold) - 1.0f, r.getWidth() - 3.0f, 2.0f);
        }
    }

private:
    float level = -100.0f, hold = -100.0f;
    int holdTicks = 0;
};
