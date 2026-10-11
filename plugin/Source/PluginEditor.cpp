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
                                        float a0, float a1, Slider&)
{
    auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float size = jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float rad = size * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    const float track = jmax (3.0f, size * 0.055f);

    // value arc
    Path bgArc, valArc;
    bgArc.addCentredArc (c.x, c.y, rad - track, rad - track, 0, a0, a1, true);
    valArc.addCentredArc (c.x, c.y, rad - track, rad - track, 0, a0, ang, true);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.strokePath (bgArc, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (amber);
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
    g.setColour (text);
    g.fillPath (p, AffineTransform::rotation (ang).translated (c.x, c.y));
}

Label* GlowLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (FontOptions (13.0f));
    l->setJustificationType (Justification::centred);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    l->setColour (Label::backgroundColourId, Colours::black.withAlpha (0.25f));
    l->setColour (Label::outlineWhenEditingColourId, amber);
    l->setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    l->setColour (TextEditor::focusedOutlineColourId, amber);
    return l;
}

GlowEditor::GlowEditor (GlowProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    setupKnob (drive, "drive", "DRIVE");
    setupKnob (tone, "tone", "TONE");
    setupKnob (density, "density", "DENSITY");
    setupKnob (mix, "mix", "MIX");
    setupKnob (output, "output", "OUTPUT");
    tone.slider.setDoubleClickReturnValue (true, 0.0);
    sendLookAndFeelChange(); // re-skin the value boxes created before the knobs were parented
    setSize (600, 320);
    startTimerHz (30);
}

GlowEditor::~GlowEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GlowEditor::setupKnob (Knob& k, const String& id, const String& name)
{
    k.slider.setSliderStyle (Slider::RotaryVerticalDrag);
    k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 80, 18);
    k.slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    k.label.setText (name, dontSendNotification);
    k.label.setJustificationType (Justification::centred);
    k.label.setFont (FontOptions (12.0f, Font::bold));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
}

void GlowEditor::paint (Graphics& g)
{
    g.fillAll (GlowLookAndFeel::bg);
    auto area = getLocalBounds().reduced (12);
    g.setColour (GlowLookAndFeel::panel);
    g.fillRoundedRectangle (area.toFloat(), 10.0f);

    // title
    g.setColour (GlowLookAndFeel::text);
    g.setFont (FontOptions (24.0f, Font::bold));
    g.drawText ("GLOW", area.removeFromTop (44).reduced (20, 0), Justification::centredLeft);
    g.setColour (GlowLookAndFeel::textDim);
    g.setFont (FontOptions (12.0f));
    g.drawText ("TUBE SATURATOR  //  HAREN AUDIO", getLocalBounds().reduced (32, 0).removeFromTop (56),
                Justification::centredRight);
    g.setFont (FontOptions (10.0f));
    g.drawText ("v" JucePlugin_VersionString, getLocalBounds().reduced (24, 16).removeFromBottom (14),
                Justification::bottomRight);

    // "tube" glow: brightens with drive
    const float d = (float) drive.slider.getValue() / 100.0f;
    auto gc = glowArea.toFloat().getCentre();
    const float gr = (float) glowArea.getWidth() * 0.5f;
    ColourGradient grad (GlowLookAndFeel::amber.withAlpha (0.15f + 0.6f * d), gc.x, gc.y,
                         GlowLookAndFeel::amber.withAlpha (0.0f), gc.x + gr, gc.y, true);
    g.setGradientFill (grad);
    g.fillEllipse (glowArea.toFloat());
}

void GlowEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (40);

    auto place = [] (Knob& k, Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (18));
        k.slider.setBounds (r);
    };

    const int side = 110;
    auto left = area.removeFromLeft (side);
    auto right = area.removeFromRight (side);
    place (tone, left.removeFromTop (left.getHeight() / 2).reduced (4));
    place (density, left.reduced (4));
    place (mix, right.removeFromTop (right.getHeight() / 2).reduced (4));
    place (output, right.reduced (4));

    auto centre = area.reduced (20, 0);
    glowArea = centre.withSizeKeepingCentre (centre.getHeight() + 40, centre.getHeight() + 40).translated (0, 6);
    place (drive, centre.withSizeKeepingCentre (jmin (centre.getWidth(), centre.getHeight()), centre.getHeight()));
}

AudioProcessorEditor* GlowProcessor::createEditor() { return new GlowEditor (*this); }
