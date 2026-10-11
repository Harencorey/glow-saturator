#include "PluginEditor.h"

using namespace juce;

GlowLookAndFeel::GlowLookAndFeel()
{
    setColour (Slider::textBoxTextColourId, text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Label::textColourId, textDim);
}

void GlowLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                        float a0, float a1, Slider& s)
{
    auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size = jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float rad = size * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    const float track = jmax (3.0f, size * 0.055f);
    const bool on = s.isEnabled();

    // value arc; controls that span zero (gains) draw from their 0 position
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0) : a0;
    Path bgArc, valArc;
    bgArc.addCentredArc (c.x, c.y, rad - track, rad - track, 0, a0, a1, true);
    valArc.addCentredArc (c.x, c.y, rad - track, rad - track, 0, jmin (from, ang), jmax (from, ang), true);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.strokePath (bgArc, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (on ? amber : amberDim.withAlpha (0.5f));
    g.strokePath (valArc, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));

    // knob body
    const float kr = rad - track * 2.6f;
    g.setGradientFill (ColourGradient (Colour (0xff4a3f36), c.x, c.y - kr, Colour (0xff1e1915), c.x, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.2f);

    // pointer
    Path p;
    const float pw = jmax (2.0f, kr * 0.09f);
    p.addRoundedRectangle (-pw * 0.5f, -kr * 0.92f, pw, kr * 0.42f, pw * 0.5f);
    g.setColour (on ? text : textDim.withAlpha (0.5f));
    g.fillPath (p, AffineTransform::rotation (ang).translated (c.x, c.y));
}

Label* GlowLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (FontOptions (12.5f));
    l->setJustificationType (Justification::centred);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    l->setColour (Label::backgroundColourId, Colours::black.withAlpha (0.25f));
    l->setColour (Label::outlineWhenEditingColourId, amber);
    l->setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    l->setColour (TextEditor::focusedOutlineColourId, amber);
    return l;
}

void GlowLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? amber : Colours::black.withAlpha (over ? 0.35f : 0.25f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? amber : textDim.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void GlowLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? bg : textDim);
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred);
}

//==============================================================================
GlowEditor::GlowEditor (GlowProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setupKnob (drive, "drive", "DRIVE", 25.0);
    setupKnob (even, "even", "EVEN", 50.0);
    setupKnob (odd, "odd", "ODD", 50.0);
    setupKnob (density, "density", "DENSITY", 50.0);
    setupKnob (hpf, "hpf", "HIGH-PASS", 10.0);
    setupKnob (preFreq, "preShelfFreq", "SHELF FREQ", 5000.0);
    setupKnob (preGain, "preShelfGain", "SHELF GAIN", 0.0);
    setupKnob (postFreq, "postShelfFreq", "SHELF FREQ", 5000.0);
    setupKnob (postGain, "postShelfGain", "SHELF GAIN", 0.0);
    setupKnob (mix, "mix", "MIX", 100.0);
    setupKnob (output, "output", "OUTPUT", 0.0);

    linkButton.setClickingTogglesState (true);
    linkButton.setTooltip ("Post shelf mirrors the pre shelf: highs hit the tubes harder or softer, overall balance stays put");
    addAndMakeVisible (linkButton);
    linkAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "shelfLink", linkButton);

    sendLookAndFeelChange(); // re-skin the value boxes created before the knobs were parented
    setSize (900, 440);
    startTimerHz (30);
}

GlowEditor::~GlowEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GlowEditor::setupKnob (Knob& k, const String& id, const String& name, double resetTo)
{
    k.slider.setSliderStyle (Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 76, 18);
    k.slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    k.slider.setDoubleClickReturnValue (true, resetTo);
    k.label.setText (name, dontSendNotification);
    k.label.setJustificationType (Justification::centred);
    k.label.setFont (FontOptions (11.0f, Font::bold));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
}

void GlowEditor::timerCallback()
{
    // linked post shelf follows the pre shelf, so its own knobs are inactive
    const bool link = linkButton.getToggleState();
    postFreq.slider.setEnabled (! link);
    postGain.slider.setEnabled (! link);

    const float d = (float) drive.slider.getValue();
    if (std::abs (d - lastGlow) > 0.01f) { lastGlow = d; repaint (glowArea); }
}

void GlowEditor::paint (Graphics& g)
{
    g.fillAll (GlowLookAndFeel::bg);
    auto area = getLocalBounds().reduced (12);
    g.setColour (GlowLookAndFeel::panel);
    g.fillRoundedRectangle (area.toFloat(), 10.0f);

    // title
    auto header = area.removeFromTop (46).reduced (20, 0);
    g.setColour (GlowLookAndFeel::text);
    g.setFont (FontOptions (24.0f, Font::bold));
    g.drawText ("GLOW", header, Justification::centredLeft);
    g.setColour (GlowLookAndFeel::textDim);
    g.setFont (FontOptions (12.0f));
    g.drawText ("TUBE SATURATOR  //  HAREN AUDIO", header, Justification::centredRight);

    // section panels + captions
    auto drawSection = [&g] (Rectangle<int> r, const String& name)
    {
        g.setColour (GlowLookAndFeel::section);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (GlowLookAndFeel::textDim.withAlpha (0.8f));
        g.setFont (FontOptions (10.5f, Font::bold));
        g.drawText (name, r.removeFromTop (20).reduced (10, 0), Justification::centredLeft);
    };
    drawSection (preArea, "PRE");
    drawSection (postArea, "POST");

    // "tube" glow behind Drive: brightens with drive
    const float d = (float) drive.slider.getValue() / 100.0f;
    auto gc = glowArea.toFloat().getCentre();
    const float gr = (float) glowArea.getWidth() * 0.5f;
    ColourGradient grad (GlowLookAndFeel::amber.withAlpha (0.15f + 0.6f * d), gc.x, gc.y,
                         GlowLookAndFeel::amber.withAlpha (0.0f), gc.x + gr, gc.y, true);
    g.setGradientFill (grad);
    g.fillEllipse (glowArea.toFloat());

    g.setColour (GlowLookAndFeel::textDim.withAlpha (0.7f));
    g.setFont (FontOptions (10.0f));
    g.drawText ("v" JucePlugin_VersionString, getLocalBounds().reduced (24, 16).removeFromBottom (14),
                Justification::bottomRight);
}

void GlowEditor::place (Knob& k, Rectangle<int> r)
{
    k.label.setBounds (r.removeFromTop (16));
    k.slider.setBounds (r);
}

void GlowEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (40);
    area.removeFromBottom (10);

    const int sideW = 220;
    preArea = area.removeFromLeft (sideW);
    postArea = area.removeFromRight (sideW);
    tubeArea = area.reduced (14, 0);

    // PRE: high-pass on top, shelf freq + gain below
    {
        auto r = preArea.withTrimmedTop (22).reduced (8, 4);
        auto top = r.removeFromTop (r.getHeight() / 2);
        place (hpf, top.withSizeKeepingCentre (110, top.getHeight()));
        place (preFreq, r.removeFromLeft (r.getWidth() / 2));
        place (preGain, r);
    }
    // POST: shelf freq + gain (+ link) on top, mix + output below
    {
        auto r = postArea.withTrimmedTop (22).reduced (8, 4);
        auto top = r.removeFromTop (r.getHeight() / 2);
        linkButton.setBounds (postArea.getRight() - 58, postArea.getY() + 3, 48, 16);
        place (postFreq, top.removeFromLeft (top.getWidth() / 2));
        place (postGain, top);
        place (mix, r.removeFromLeft (r.getWidth() / 2));
        place (output, r);
    }
    // TUBES: Drive big, Even / Density / Odd underneath
    {
        auto r = tubeArea;
        auto bottom = r.removeFromBottom (128);
        auto top = r;
        const int dsz = jmin (top.getWidth(), top.getHeight());
        auto driveBox = top.withSizeKeepingCentre (dsz, top.getHeight());
        glowArea = driveBox.withSizeKeepingCentre (dsz + 30, dsz + 30).translated (0, 8);
        place (drive, driveBox);
        const int w = bottom.getWidth() / 3;
        place (even, bottom.removeFromLeft (w).reduced (6, 0));
        place (density, bottom.removeFromLeft (w).reduced (6, 0));
        place (odd, bottom.reduced (6, 0));
    }
}

AudioProcessorEditor* GlowProcessor::createEditor() { return new GlowEditor (*this); }
