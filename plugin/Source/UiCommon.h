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

    // warm black faceplate, glowing amber, cream type
    static inline const juce::Colour bg { 0xff12100e }, panel { 0xff181614 }, section { 0xff1f1c19 },
        amber { 0xfff3a23c }, amberDim { 0xff7a4e1e }, text { 0xffede6da }, textDim { 0xff8e877c },
        well { 0xff0f0e0d }, edge { 0xff2e2a25 }, lampBg { 0xff2a1d10 },
        green { 0xff62c04a }, red { 0xffe8503a }, yellow { 0xfff2cf4a };

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
        g.setColour (juce::Colour (0xff0a0908));
        g.fillRoundedRectangle (r, 1.5f);
        auto toY = [&r] (float db) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };
        const float top = toY (level);
        // green -> yellow (-12 dB) -> red (0 dB)
        juce::ColourGradient grad (GlowLookAndFeel::green, 0.0f, r.getBottom(), GlowLookAndFeel::red, 0.0f, toY (0.0f), false);
        grad.addColour (48.0 / 60.0, GlowLookAndFeel::yellow);
        g.setGradientFill (grad);
        g.fillRect (r.withTop (top).reduced (1.0f, 0.0f));
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

// Horizontal stereo peak meter (L above R), -48..+6 dBFS, green / yellow (-12) / red (-3),
// a short peak hold, and a readout of the highest peak (red once it has gone over 0 dBFS).
// Click it to reset the readout. The OUT meter also prints the dB scale under its bars.
class StereoMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StereoMeter (const juce::String& n, bool scale = false) : name (n), showScale (scale) {}

    void push (float peakL, float peakR)
    {
        const float pk[2] { peakL, peakR };
        for (int c = 0; c < 2; ++c)
        {
            const float db = juce::Decibels::gainToDecibels (pk[c], -100.0f);
            level[c] = db > level[c] ? db : level[c] - 1.2f;          // ~36 dB/s fall at 30 Hz
            if (db >= hold[c]) { hold[c] = db; holdTicks[c] = 45; }   // ~1.5 s hold
            else if (--holdTicks[c] < 0) hold[c] -= 1.2f;
            maxDb = juce::jmax (maxDb, db);
        }
        repaint();
    }
    void mouseDown (const juce::MouseEvent&) override { maxDb = -100.0f; repaint(); }

    void paint (juce::Graphics& g) override
    {
        using L = GlowLookAndFeel;
        auto r = getLocalBounds().toFloat();
        g.setColour (L::text.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f).withKerningFactor (0.25f));
        g.drawText (name, r.removeFromLeft (38.0f).withHeight (barsH()), juce::Justification::centredLeft);
        auto readout = r.removeFromRight (52.0f).withHeight (barsH());
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (maxDb > 0.0f ? L::red : L::text.withAlpha (0.8f));
        g.drawText (maxDb <= -99.0f ? juce::String ("-inf") : (maxDb > 0.0f ? "+" : "") + juce::String (maxDb, 1),
                    readout, juce::Justification::centredRight);
        auto bars = r.withHeight (barsH()).reduced (0.0f, 0.5f);
        auto xOf = [&bars] (float db) { return bars.getX() + bars.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 54.0f); };
        const float bh = (bars.getHeight() - 2.0f) * 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            auto b = juce::Rectangle<float> (bars.getX(), bars.getY() + (float) c * (bh + 2.0f), bars.getWidth(), bh);
            g.setColour (juce::Colour (0xff1e1b18));
            g.fillRoundedRectangle (b, 1.5f);
            g.setColour (juce::Colour (0xff2e2a25));
            g.drawRoundedRectangle (b, 1.5f, 0.8f);
            juce::ColourGradient grad (L::green, xOf (-48.0f), 0.0f, L::red, xOf (0.0f), 0.0f, false);
            grad.addColour ((double) ((xOf (-12.0f) - xOf (-48.0f)) / (xOf (0.0f) - xOf (-48.0f))), L::green.interpolatedWith (L::yellow, 0.3f));
            grad.addColour ((double) ((xOf (-3.0f) - xOf (-48.0f)) / (xOf (0.0f) - xOf (-48.0f))), L::yellow);
            g.setGradientFill (grad);
            if (level[c] > -48.0f) g.fillRoundedRectangle (b.withRight (xOf (level[c])), 1.5f);
            if (hold[c] > -48.0f)
            {
                g.setColour (hold[c] > 0.0f ? L::red : L::text.withAlpha (0.85f));
                g.fillRect (xOf (hold[c]) - 1.0f, b.getY(), 2.0f, b.getHeight());
            }
        }
        // ticks, and (OUT) the scale
        g.setFont (juce::FontOptions (9.0f));
        for (float db : { -48.0f, -36.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f, 6.0f })
        {
            const float x = xOf (db);
            g.setColour (L::textDim.withAlpha (0.35f));
            g.fillRect (x - 0.5f, bars.getY(), 1.0f, bars.getHeight());
            if (showScale)
            {
                g.setColour (L::textDim.withAlpha (0.8f));
                g.drawText (db > 0.0f ? "+" + juce::String ((int) db) : juce::String ((int) db),
                            juce::Rectangle<float> (x - 14.0f, bars.getBottom() + 2.0f, 28.0f, 10.0f), juce::Justification::centred);
            }
        }
    }

private:
    float barsH() const { return showScale ? (float) getHeight() - 13.0f : (float) getHeight(); }
    juce::String name;
    bool showScale = false;
    float level[2] { -100.0f, -100.0f }, hold[2] { -100.0f, -100.0f }, maxDb = -100.0f;
    int holdTicks[2] { 0, 0 };
};
