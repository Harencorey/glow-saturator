#include "PluginEditor.h"

using namespace juce;

//==============================================================================
GlowLookAndFeel::GlowLookAndFeel()
{
    setColour (Slider::textBoxTextColourId, text.withAlpha (0.9f));
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Label::textColourId, textDim);
    // preset menu + save/delete dialogs in the plugin's colours
    setColour (PopupMenu::backgroundColourId, section);
    setColour (PopupMenu::textColourId, text);
    setColour (PopupMenu::headerTextColourId, amber);
    setColour (PopupMenu::highlightedBackgroundColourId, amber);
    setColour (PopupMenu::highlightedTextColourId, bg);
    setColour (AlertWindow::backgroundColourId, panel);
    setColour (AlertWindow::textColourId, text);
    setColour (AlertWindow::outlineColourId, amberDim);
    setColour (TextEditor::backgroundColourId, Colours::black.withAlpha (0.35f));
    setColour (TextEditor::textColourId, text);
    setColour (TextEditor::outlineColourId, textDim.withAlpha (0.5f));
    setColour (TextEditor::focusedOutlineColourId, amber);
    setColour (CaretComponent::caretColourId, amber);
    setColour (TooltipWindow::backgroundColourId, section.brighter (0.15f));
    setColour (TooltipWindow::textColourId, text);
    setColour (TooltipWindow::outlineColourId, amberDim);
    setColour (BubbleComponent::backgroundColourId, amber);
    setColour (BubbleComponent::outlineColourId, Colours::transparentBlack);
}

namespace
{
    Point<float> polar (Point<float> c, float r, float a) { return { c.x + r * std::sin (a), c.y - r * std::cos (a) }; }

    // an arc with a soft glow under it (two wide faint passes, then the line itself)
    void glowArc (Graphics& g, Point<float> c, float r, float from, float to, float w, Colour col, float glow)
    {
        if (std::abs (to - from) < 0.002f) return;
        Path p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, jmin (from, to), jmax (from, to), true);
        const PathStrokeType::JointStyle j = PathStrokeType::curved;
        g.setColour (col.withMultipliedAlpha (0.07f * glow));
        g.strokePath (p, PathStrokeType (w * 3.4f, j, PathStrokeType::rounded));
        g.setColour (col.withMultipliedAlpha (0.16f * glow));
        g.strokePath (p, PathStrokeType (w * 2.0f, j, PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (p, PathStrokeType (w, j, PathStrokeType::rounded));
    }

    void track (Graphics& g, Point<float> c, float r, float a0, float a1, float w, float alpha)
    {
        Path p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
        g.setColour (Colour (0xff27221d).withMultipliedAlpha (alpha));
        g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // glossy black knob: rim, face with a soft top-left light and faint concentric brushing, cream pointer
    void knobBody (Graphics& g, Point<float> c, float rim, float face, float ang, float pointerW, float alpha, int rings)
    {
        g.setColour (Colours::black.withAlpha (0.6f * alpha));
        g.fillEllipse (c.x - rim, c.y - rim + rim * 0.06f, rim * 2.0f, rim * 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xff2c2925), c.x, c.y - rim, Colour (0xff070606), c.x, c.y + rim, false));
        g.fillEllipse (c.x - rim, c.y - rim, rim * 2.0f, rim * 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xff3d3934).withMultipliedAlpha (alpha), c.x - face * 0.45f, c.y - face * 0.6f,
                                           Colour (0xff0f0e0d), c.x + face * 0.5f, c.y + face * 0.9f, true));
        g.fillEllipse (c.x - face, c.y - face, face * 2.0f, face * 2.0f);
        for (int k = 1; k <= rings; ++k)
        {
            const float rr = face * (float) k / (float) (rings + 1);
            g.setColour (Colours::white.withAlpha (0.018f * alpha));
            g.drawEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 0.8f);
        }
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f * alpha), c.x, c.y - face,
                                           Colours::white.withAlpha (0.0f), c.x, c.y, false));
        g.drawEllipse (c.x - face, c.y - face, face * 2.0f, face * 2.0f, 1.0f);
        Path p;
        p.startNewSubPath (polar (c, face * 0.32f, ang));
        p.lineTo (polar (c, face * 0.9f, ang));
        g.setColour (GlowLookAndFeel::text.withAlpha (alpha));
        g.strokePath (p, PathStrokeType (pointerW, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

// Knobs: a glowing amber value arc hugging a glossy black knob, fine tick marks, and (EVEN /
// DENSITY / ODD) a 0-10 scale. DRIVE ("hero") sits in its own bezel with a thick glowing arc
// whose brightness follows how hard the tubes are working (the "glow" property, set by the editor).
void GlowLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                        float a0, float a1, Slider& s)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float S = jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const bool hero = (bool) s.getProperties().getWithDefault ("hero", false);
    const bool nums = (bool) s.getProperties().getWithDefault ("numbers", false);
    const float alpha = s.isEnabled() ? 1.0f : 0.4f;
    const float ang = a0 + pos * (a1 - a0);
    // bipolar controls (gain, envelope amount, output trim) grow the arc from 0, not from the minimum
    float arcFrom = a0;
    if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
        arcFrom = a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0);
    const auto amb = amber.withMultipliedAlpha (alpha);

    if (hero)
    {
        const float glow = jlimit (0.0f, 1.0f, (float) (double) s.getProperties().getWithDefault ("glow", 0.0));
        const float rb = S * 0.5f - 1.0f, ra = S * 0.317f, aw = S * 0.034f;
        // bezel
        g.setGradientFill (ColourGradient (Colour (0xff1c1916), c.x, c.y - rb, Colour (0xff0c0b0a), c.x, c.y + rb, false));
        g.fillEllipse (c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f);
        g.setColour (Colour (0xff2b2722));
        g.drawEllipse (c.x - rb, c.y - rb, rb * 2.0f, rb * 2.0f, 1.0f);
        // warm halo inside the bezel
        const float rh = ra * 1.3f;
        g.setGradientFill (ColourGradient (amber.withAlpha ((0.05f + 0.2f * glow) * alpha), c.x, c.y,
                                           amber.withAlpha (0.0f), c.x + rh, c.y, true));
        g.fillEllipse (c.x - rh, c.y - rh, rh * 2.0f, rh * 2.0f);
        // ticks: 51, a long one every 5
        const float t0 = S * 0.362f;
        for (int i = 0; i <= 50; ++i)
        {
            const float a = a0 + (float) i / 50.0f * (a1 - a0);
            const bool major = i % 5 == 0;
            g.setColour (major ? text.withAlpha (0.7f * alpha) : textDim.withAlpha (0.45f * alpha));
            g.drawLine (Line<float> (polar (c, t0, a), polar (c, t0 + S * (major ? 0.03f : 0.015f), a)), major ? 1.6f : 1.0f);
        }
        const float fs = S * 0.054f;
        g.setFont (FontOptions (fs));
        g.setColour (text.withAlpha (0.88f * alpha));
        for (int i = 0; i <= 10; ++i)
        {
            const auto p = polar (c, S * 0.43f, a0 + (float) i / 10.0f * (a1 - a0));
            g.drawText (String (i), Rectangle<float> (p.x - fs, p.y - fs * 0.6f, fs * 2.0f, fs * 1.2f), Justification::centred);
        }
        // value arc with a bright tip
        track (g, c, ra, a0, a1, aw, alpha);
        glowArc (g, c, ra, arcFrom, ang, aw, amb, 0.7f + 1.2f * glow);
        const auto tip = polar (c, ra, ang);
        const float tr = S * 0.05f;
        g.setGradientFill (ColourGradient (amber.withAlpha ((0.5f + 0.4f * glow) * alpha), tip.x, tip.y,
                                           amber.withAlpha (0.0f), tip.x + tr, tip.y, true));
        g.fillEllipse (tip.x - tr, tip.y - tr, tr * 2.0f, tr * 2.0f);
        g.setColour (Colour (0xffffdcaa).withAlpha (alpha));
        g.fillEllipse (tip.x - S * 0.02f, tip.y - S * 0.02f, S * 0.04f, S * 0.04f);
        knobBody (g, c, S * 0.255f, S * 0.215f, ang, S * 0.018f, alpha, 9);
        return;
    }

    const float rf = S * (nums ? 0.30f : 0.355f);       // knob radius
    const float ra = rf * 1.11f, aw = jmax (2.0f, rf * 0.09f);
    const int n = nums ? 21 : 11;
    for (int i = 0; i < n; ++i)
    {
        const float a = a0 + (float) i / (float) (n - 1) * (a1 - a0);
        const bool major = nums ? i % 2 == 0 : (i == 0 || i == n - 1 || i == n / 2);
        g.setColour (major ? text.withAlpha (0.55f * alpha) : textDim.withAlpha (0.42f * alpha));
        g.drawLine (Line<float> (polar (c, rf * 1.22f, a), polar (c, rf * (major ? 1.35f : 1.29f), a)), 1.0f);
    }
    if (nums)
    {
        const float fs = jmax (8.5f, rf * 0.32f);
        g.setFont (FontOptions (fs));
        g.setColour (text.withAlpha (0.72f * alpha));
        for (int i = 0; i <= 10; i += 2)
        {
            const auto p = polar (c, rf * 1.58f, a0 + (float) i / 10.0f * (a1 - a0));
            g.drawText (String (i), Rectangle<float> (p.x - fs, p.y - fs * 0.6f, fs * 2.0f, fs * 1.2f), Justification::centred);
        }
    }
    track (g, c, ra, a0, a1, aw, alpha);
    glowArc (g, c, ra, arcFrom, ang, aw, amb, 1.0f);
    knobBody (g, c, rf, rf * 0.84f, ang, jmax (2.0f, rf * 0.08f), alpha, 4);
}

// fader: slim slot with an amber fill under a silver cap
void GlowLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        Slider::SliderStyle style, Slider& s)
{
    if (style != Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const float cx = (float) x + (float) w * 0.5f;
    Rectangle<float> slot (cx - 2.0f, (float) y, 4.0f, (float) h);
    g.setColour (Colour (0xff090807));
    g.fillRoundedRectangle (slot, 2.0f);
    g.setColour (Colour (0xff26221e));
    g.drawRoundedRectangle (slot.expanded (0.5f), 2.0f, 1.0f);
    auto fill = slot.withTop (pos);
    g.setColour (amber.withAlpha (0.08f));
    g.fillRoundedRectangle (fill.expanded (3.0f, 0.0f), 3.0f);
    g.setGradientFill (ColourGradient (amber, 0.0f, pos, Colour (0xff6e3c14), 0.0f, (float) (y + h), false));
    g.fillRoundedRectangle (fill.reduced (0.5f, 0.0f), 1.5f);

    Rectangle<float> cap (cx - 8.0f, pos - 13.0f, 16.0f, 26.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 2.5f);
    g.setGradientFill (ColourGradient (Colour (0xffeae7e1), cap.getX(), cap.getY(), Colour (0xff8a8680), cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 2.5f);
    g.setColour (Colour (0xff2b2825));
    g.fillRect (cap.getX() + 2.0f, pos - 1.0f, cap.getWidth() - 4.0f, 2.0f);
    g.setColour (Colours::black.withAlpha (0.18f));
    g.fillRect (cap.getX() + 3.0f, pos - 7.0f, cap.getWidth() - 6.0f, 1.0f);
    g.fillRect (cap.getX() + 3.0f, pos + 6.0f, cap.getWidth() - 6.0f, 1.0f);
}

Label* GlowLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (FontOptions (12.0f));
    l->setJustificationType (Justification::centred);
    l->setColour (Label::textColourId, text.withAlpha (0.9f));
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::outlineWhenEditingColourId, amber);
    l->setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    l->setColour (TextEditor::focusedOutlineColourId, amber);
    return l;
}

// Buttons: dark with a thin outline; on = amber outline + amber text (Mute/Solo keep their own colours)
void GlowLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    if (b.isColourSpecified (TextButton::buttonOnColourId))
    {
        const auto onCol = b.findColour (TextButton::buttonOnColourId);
        g.setColour (on ? onCol : well.brighter (over ? 0.15f : 0.0f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (on ? onCol : edge.brighter (0.15f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        return;
    }
    g.setColour (on ? amber.withAlpha (over ? 0.12f : 0.07f) : well.brighter (over ? 0.12f : 0.0f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? amber.withAlpha (0.9f) : edge.brighter (over ? 0.35f : 0.15f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void GlowLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    auto r = b.getLocalBounds().toFloat();
    if (b.isColourSpecified (TextButton::buttonOnColourId))
    {
        g.setColour (on ? bg : text.withAlpha (0.85f));
        g.setFont (FontOptions (11.5f));
        g.drawText (b.getButtonText(), r, Justification::centred);
        return;
    }
    const bool lamp = (bool) b.getProperties().getWithDefault ("lamp", false);
    g.setFont (FontOptions (jmin (12.5f, r.getHeight() * 0.48f)).withKerningFactor (0.2f));
    const float tw = GlyphArrangement::getStringWidth (g.getCurrentFont(), b.getButtonText());
    const float total = tw + (lamp ? 15.0f : 0.0f);
    float tx = r.getCentreX() - total * 0.5f;
    if (lamp)
    {
        drawLamp (g, { tx + 3.0f, r.getCentreY() }, 3.0f, on);
        tx += 15.0f;
    }
    g.setColour (on ? amber : text.withAlpha (0.88f));
    g.drawText (b.getButtonText(), Rectangle<float> (tx, r.getY(), tw + 4.0f, r.getHeight()), Justification::centredLeft);
}

void GlowLookAndFeel::drawBubble (Graphics& g, BubbleComponent&, const Point<float>&, const Rectangle<float>& body)
{
    g.setColour (Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (body.translated (0.0f, 2.0f), 5.0f);
    g.setColour (amber);
    g.fillRoundedRectangle (body, 5.0f);
}

//==============================================================================
void GlowEditor::IconButton::paintButton (Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (GlowLookAndFeel::well.brighter (over ? 0.12f : 0.0f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (GlowLookAndFeel::edge.brighter (over ? 0.35f : 0.15f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    // curved arrow
    const auto c = r.getCentre();
    Path p;
    const float s = 5.0f, d = redo ? 1.0f : -1.0f;
    p.startNewSubPath (c.x + d * 3.5f, c.y + 4.0f);
    p.lineTo (c.x - d * 1.5f, c.y + 4.0f);
    p.quadraticTo (c.x - d * 5.5f, c.y + 4.0f, c.x - d * 5.5f, c.y);
    p.quadraticTo (c.x - d * 5.5f, c.y - 3.5f, c.x - d * 1.5f, c.y - 3.5f);
    p.lineTo (c.x + d * 4.5f, c.y - 3.5f);
    p.startNewSubPath (c.x + d * 2.0f, c.y - 3.5f - s * 0.5f);
    p.lineTo (c.x + d * 4.5f, c.y - 3.5f);
    p.lineTo (c.x + d * 2.0f, c.y - 3.5f + s * 0.5f);
    g.setColour ((isEnabled() ? GlowLookAndFeel::text : GlowLookAndFeel::textDim.withAlpha (0.4f)));
    g.strokePath (p, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));
}

//==============================================================================
GlowEditor::GlowEditor (GlowProcessor& p)
    : AudioProcessorEditor (p), proc (p), preCurve (p.apvts, "pre", { 1, 3 }), presetBar (p.presets)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;
    addAndMakeVisible (presetBar);

    // ---- INPUT: high-pass, Keep lows, slope; the pre-saturation EQ curve
    hpf.setup (*this, s, "hpf", "HIGH-PASS", GlowDSP::kHpfOffHz, 10.5f);
    hpf.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 16);
    hpf.tip ("HIGH-PASS: cuts lows before the tubes so they don't muddy the saturation. Bottom = Off, then 20 Hz to 1 kHz. "
             "It also shows as the HP point on the EQ below (drag it left/right).");
    keepLows.setup (*this, s, "keepLows", "KEEP LOWS");
    keepLows.button.getProperties().set ("lamp", true);
    keepLows.button.setTooltip ("KEEP LOWS: everything the high-pass removes skips the tubes and comes back clean, so the low "
                                "end stays full and solid while only what's above it saturates. Off = a plain cut.");
    for (auto* b : { &slope12, &slope24 })
    {
        b->setClickingTogglesState (false);
        b->setTooltip ("High-pass slope: 12 dB/oct (gentle) or 24 dB/oct (steep)");
        addAndMakeVisible (b);
    }
    if (auto* sp = s.getParameter ("hpfSlope"))
    {
        slopeAttach = std::make_unique<ParameterAttachment> (*sp, [this] (float v)
        {
            slope24.setToggleState (v > 0.5f, dontSendNotification);
            slope12.setToggleState (v <= 0.5f, dontSendNotification);
        });
        slopeAttach->sendInitialUpdate();
        slope12.onClick = [this] { slopeAttach->setValueAsCompleteGesture (0.0f); };
        slope24.onClick = [this] { slopeAttach->setValueAsCompleteGesture (1.0f); };
    }
    preEqLink.setup (*this, s, "preEqLink", "LINK");
    preEqOn.setup (*this, s, "preEqOn", "ON");
    for (auto* t : { &preEqLink, &preEqOn }) t->button.getProperties().set ("lamp", true);
    preEqLink.button.setTooltip ("LINK: the exact opposite of this EQ runs after the tubes (dashed curve). You change what "
                                 "saturates - push the mids into the tubes, say - while the overall tone stays the same.");
    preEqOn.button.setTooltip ("Pre-saturation EQ on/off (dragging a point switches it on)");
    preCurve.names = { "M", "H" };
    preCurve.fixedHighShelf = true;
    preCurve.attachHpf (s.getParameter ("hpf"), s.getRawParameterValue ("hpfSlope"), s.getRawParameterValue ("keepLows"),
                        s.getRawParameterValue ("preEqLink"));
    preCurve.onSolo = [this] (int band) { proc.setEqSolo (3, band); };
    preCurve.setTooltip ("What goes into the tubes. M = mid bell, H = high shelf, HP = the high-pass. Drag a point: frequency + gain. "
                         "Cmd-drag: Q. Option-drag: frequency only. Hold Shift: solo the band. Double-click: bypass the band "
                         "(the HP point: Off). Option-double-click: gain to 0 dB.");
    addAndMakeVisible (preCurve);

    // ---- TUBES
    drive.setup (*this, s, "drive", "DRIVE", 25.0, 15.0f);
    even.setup (*this, s, "even", "EVEN", 50.0);
    odd.setup (*this, s, "odd", "ODD", 50.0);
    density.setup (*this, s, "density", "DENSITY", 50.0);
    for (auto* k : { &even, &odd, &density }) k->numbers();
    drive.slider.getProperties().set ("hero", true);
    drive.label.setInterceptsMouseClicks (false, false);   // it sits inside the Drive bezel
    for (auto* k : { &drive, &even, &odd, &density }) k->popupValue();
    drive.tip ("DRIVE: how hard the tubes are pushed, for more of both even and odd harmonics. The glow shows how hard "
               "they're working on the incoming audio.");
    even.tip ("EVEN: 2nd/4th harmonics (warmth, thickness). 50% = as voiced, 0% = none, 100% = 3x.");
    odd.tip ("ODD: 3rd/5th harmonics (edge, grit, clipping). 50% = as voiced; the level is matched for you.");
    density.tip ("DENSITY: how hard the first tube stage is hit. More = lopsided bloom and 'breathing' after loud hits.");

    // ---- OUTPUT + envelope
    output.setup (*this, s, "output", "LEVEL", 0.0);
    output.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 16);
    output.tip ("OUTPUT: final level trim, after Dry and Wet are mixed (and after Auto)");
    autoGain.setup (*this, s, "autoGain", "AUTO");
    autoGain.button.getProperties().set ("lamp", true);
    autoGain.button.setTooltip ("Auto gain: keeps the output as loud as the input (slow, ~3 s), so Drive/EQ/comp moves "
                                "don't fool your ears. Output trims on top.");
    autoReadout.setJustificationType (Justification::centred);
    autoReadout.setFont (FontOptions (11.0f));
    autoReadout.setColour (Label::textColourId, GlowLookAndFeel::textDim);
    addAndMakeVisible (autoReadout);
    envAmount.setup (*this, s, "envAmount", "AMOUNT", 0.0, 10.0f);
    envSpeed.setup (*this, s, "envSpeed", "SPEED", 50.0, 10.0f);
    for (auto* k : { &envAmount, &envSpeed }) k->popupValue();
    envAmount.tip ("ENVELOPE amount: Drive follows the input's dynamics. + = louder moments get pushed harder (hits grit up, "
                   "quiet parts stay clean). - = quiet parts get more drive, loud parts less. "
                   "The dot on the Drive ring shows where it's pushing Drive right now.");
    envSpeed.tip ("ENVELOPE speed: how fast it follows. Fast = individual hits (drums), slow = phrases and sections.");
    envAmtP = s.getRawParameterValue ("envAmount");

    // knob names: regular weight, letter-spaced
    for (auto* k : { &hpf, &even, &odd, &density, &output, &envAmount, &envSpeed })
    {
        k->label.setFont (FontOptions (k == &envAmount || k == &envSpeed ? 10.5f : 12.0f).withKerningFactor (0.16f));
        k->label.setColour (Label::textColourId, GlowLookAndFeel::text.withAlpha (0.88f));
    }
    drive.label.setFont (FontOptions (16.0f).withKerningFactor (0.3f));
    drive.label.setColour (Label::textColourId, GlowLookAndFeel::text);
    preCurve.axisOutside = true;

    // ---- MIX
    setupStrip (dry, "dry", "DRY");
    setupStrip (wet, "wet", "WET");

    // ---- header: A/B, undo/redo
    for (auto* b : { &abA, &abB })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    abA.setTooltip ("A/B: two complete settings to compare. Click B to switch (it starts as a copy of A).");
    abB.setTooltip (abA.getTooltip());
    abA.onClick = [this] { proc.switchAB (0); };
    abB.onClick = [this] { proc.switchAB (1); };
    undoButton.setTooltip ("Undo");
    redoButton.setTooltip ("Redo");
    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    modDot.draw = [this] (Graphics& g) { paintModDot (g); };
    lead.draw = [this] (Graphics& g) { paintLead (g); };
    addAndMakeVisible (lead);
    addAndMakeVisible (modDot);   // just above the Drive knob, below the tooltip window

    sendLookAndFeelChange(); // re-skin value boxes created before the knobs were parented
    setSize (1100, 600);
    startTimerHz (30);
}

GlowEditor::~GlowEditor()
{
    stopTimer();
    panel.reset();
    setLookAndFeel (nullptr);
}

void GlowEditor::setupStrip (Strip& st, const String& prefix, const String& name)
{
    st.prefix = prefix;
    st.name = name;
    st.fader.setSliderStyle (Slider::LinearVertical);
    st.fader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    st.fader.setPopupDisplayEnabled (true, true, this);
    st.fader.onValueChange = [this] { repaint (mixArea); };
    st.fader.setDoubleClickReturnValue (true, prefix == "dry" ? ChannelStrip::kOffDb : 0.0);
    st.label.setText (name, dontSendNotification);
    st.label.setJustificationType (Justification::centred);
    st.label.setFont (FontOptions (13.5f).withKerningFactor (0.3f));
    st.label.setColour (Label::textColourId, GlowLookAndFeel::text);
    addAndMakeVisible (st.fader);
    addAndMakeVisible (st.label);
    addAndMakeVisible (st.meter);
    addAndMakeVisible (st.eqIcon);
    addAndMakeVisible (st.compIcon);
    addAndMakeVisible (st.swap);
    st.swapAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, prefix + "CompFirst", st.swap);
    st.swap.setTooltip ("Swap the order: EQ then Comp, or Comp then EQ (top icon runs first)");
    st.mute.setup (*this, proc.apvts, prefix + "Mute", "M");
    st.solo.setup (*this, proc.apvts, prefix + "Solo", "S");
    st.mute.button.setColour (TextButton::buttonOnColourId, GlowLookAndFeel::red);
    st.solo.button.setColour (TextButton::buttonOnColourId, GlowLookAndFeel::yellow);
    st.mute.button.setTooltip ("Mute " + name);
    st.solo.button.setTooltip ("Solo " + name);
    st.compFirst = proc.apvts.getRawParameterValue (prefix + "CompFirst");
    st.muteP = proc.apvts.getRawParameterValue (prefix + "Mute");
    st.soloP = proc.apvts.getRawParameterValue (prefix + "Solo");
    st.attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, prefix + "Level", st.fader);
    st.eqOn = proc.apvts.getRawParameterValue (prefix + "EqOn");
    st.compOn = proc.apvts.getRawParameterValue (prefix + "CompOn");
    st.eqIcon.setTooltip (name + " EQ: click to open it, double-click to switch it on or off (lit = on)");
    st.compIcon.setTooltip (name + " compressor: click to open it, double-click to switch it on or off (lit = on)");
    st.eqIcon.onDoubleClick = [this, prefix] { toggleParam (prefix + "EqOn"); };
    st.compIcon.onDoubleClick = [this, prefix] { toggleParam (prefix + "CompOn"); };
    st.fader.setTooltip (prefix == "dry" ? "DRY level: the clean, unsaturated signal blended in parallel (bottom = Off)"
                                         : "WET level: the saturated signal");
    st.meter.setTooltip (name + " level (after its fader)");
    st.eqIcon.onSingleClick = [this, prefix] { togglePanel (prefix, ModuleIcon::Kind::eq); };
    st.compIcon.onSingleClick = [this, prefix] { togglePanel (prefix, ModuleIcon::Kind::comp); };
}

// Flip a bool parameter as one host gesture (undoable / automatable like a click)
void GlowEditor::toggleParam (const String& id)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void GlowEditor::togglePanel (const String& prefix, ModuleIcon::Kind kind)
{
    const bool same = panel != nullptr && panelPrefix == prefix && panelKind == kind;
    closePanel();
    if (same) return;

    const String name = prefix == "dry" ? "DRY" : "WET";
    if (kind == ModuleIcon::Kind::eq)
        panel = std::make_unique<EqPanel> (proc.apvts, prefix, name);
    else
        panel = std::make_unique<CompPanel> (proc.apvts, prefix, name, [this, prefix]
                     { return prefix == "dry" ? proc.dryGainReductionDb() : proc.wetGainReductionDb(); },
                     [this, prefix] { return prefix == "dry" ? proc.dryMakeupDb() : proc.wetMakeupDb(); });
    panelPrefix = prefix;
    panelKind = kind;
    if (kind == ModuleIcon::Kind::eq)
    {
        const int path = prefix == "dry" ? 1 : 2;
        panel->setSoloHandler ([this, path] (int band) { proc.setEqSolo (path, band); });
    }
    panel->onClose = [this] { MessageManager::callAsync ([sp = Component::SafePointer<GlowEditor> (this)]
                                                         { if (sp != nullptr) sp->closePanel(); }); };
    addAndMakeVisible (*panel);
    panel->sendLookAndFeelChange();
    panel->setBounds (overlayArea);
    panel->refresh();
    lead.repaint();
}

void GlowEditor::closePanel()
{
    proc.setEqSolo (0, -1);   // never leave a band soloed
    if (panel != nullptr) { removeChildComponent (panel.get()); panel.reset(); }
    lead.repaint();
    modDot.repaint();
}

void GlowEditor::timerCallback()
{
    {   // master auto gain readout
        const float a = proc.autoGainDb();
        const bool on = autoGain.button.getToggleState();
        const float shown = on ? std::round (a * 10.0f) / 10.0f : 1.0e8f;
        if (std::abs (shown - shownAuto) > 0.01f)
        {
            shownAuto = shown;
            autoReadout.setText (on ? (a > 0.05f ? "+" : "") + String (a, 1) + " dB" : String(), dontSendNotification);
        }
    }

    // one undo step per gesture: a new transaction whenever no mouse button is held
    if (! ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        proc.undoManager.beginNewTransaction();
    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());
    abA.setToggleState (proc.getAB() == 0, dontSendNotification);
    abB.setToggleState (proc.getAB() == 1, dontSendNotification);

    preCurve.refresh();
    dry.meter.push (proc.takeDryPeak());
    wet.meter.push (proc.takeWetPeak());
    for (auto* st : { &dry, &wet })
    {
        const bool mine = panel != nullptr && panelPrefix == st->prefix;
        st->eqIcon.setState (st->eqOn->load() > 0.5f, mine && panelKind == ModuleIcon::Kind::eq);
        st->compIcon.setState (st->compOn->load() > 0.5f, mine && panelKind == ModuleIcon::Kind::comp);
    }
    if (panel != nullptr) panel->refresh();
    presetBar.refresh();

    // envelope follower: move the dot on the Drive ring (the lead lights up while it's in use)
    {
        const float md = std::abs (envAmtP->load()) > 0.05f ? proc.modulatedDrive() : -1.0f;
        if (std::abs (md - shownModDrive) > 0.15f)
        {
            if ((md < 0.0f) != (shownModDrive < 0.0f)) lead.repaint();
            shownModDrive = md;
            modDot.repaint();
        }
    }

    // chain order (icons stack top = first) and mute/solo state (name dims when not heard)
    bool relayout = false;
    const bool anySolo = dry.soloP->load() > 0.5f || wet.soloP->load() > 0.5f;
    for (auto* st : { &dry, &wet })
    {
        const bool cf = st->compFirst->load() > 0.5f;
        if (cf != st->shownCompFirst) { st->shownCompFirst = cf; relayout = true; }
        const bool heard = st->muteP->load() < 0.5f && (! anySolo || st->soloP->load() > 0.5f);
        if (heard != st->shownHeard)
        {
            st->shownHeard = heard;
            st->label.setColour (Label::textColourId, heard ? GlowLookAndFeel::text : GlowLookAndFeel::textDim.withAlpha (0.45f));
        }
    }
    if (relayout) resized();

    // Drive glow + tube window follow how hard the tubes are actually working (distortion vs the
    // clean signal, about -54 dB = clean to 0 dB = crushed), with meter-like ballistics:
    // jumps up on peaks, falls back over ~0.3 s. With no audio they rest at a faint glow
    // that just shows the knob setting.
    {
        const float ratio = proc.takeDrivenness();
        const float db = ratio > 0.0f ? 10.0f * std::log10 (ratio) : -100.0f;
        const float x = jlimit (0.0f, 1.0f, (db + 54.0f) / 52.0f);
        const float target = x * x;
        glowLevel = target > glowLevel ? glowLevel + 0.6f * (target - glowLevel) : glowLevel * 0.88f;
        const float shown = 0.12f * (float) drive.slider.getValue() / 100.0f + 0.88f * glowLevel;
        if (std::abs (shown - lastGlow) > 0.004f)
        {
            lastGlow = shown;
            drive.slider.getProperties().set ("glow", shown);   // the Drive arc glows with it
            repaint (glowArea);
            repaint (tubeWindow.expanded (8, 24));
        }
    }
}

void GlowEditor::paint (Graphics& g)
{
    using L = GlowLookAndFeel;
    g.fillAll (Colour (0xff090807));
    const auto face = getLocalBounds().toFloat().reduced (1.0f);
    g.setGradientFill (ColourGradient (Colour (0xff1a1815), 0.0f, 0.0f, Colour (0xff0d0c0b), 0.0f, (float) getHeight(), false));
    g.fillRoundedRectangle (face, 12.0f);
    {   // soft vignette towards the corners
        const auto c = face.getCentre();
        g.setGradientFill (ColourGradient (Colours::transparentBlack, c.x, c.y, Colours::black.withAlpha (0.35f),
                                           0.0f, 0.0f, true));
        g.fillRoundedRectangle (face, 12.0f);
    }
    g.setColour (Colour (0xff2a2622));
    g.drawRoundedRectangle (face, 12.0f, 1.0f);

    // header: pilot lamp + wordmark, company on the right
    L::drawLamp (g, { 44.0f, 37.0f }, 6.0f, true);
    g.setColour (L::text);
    g.setFont (FontOptions (33.0f, Font::bold).withKerningFactor (0.4f));
    g.drawText ("GLOW", 66, 16, 220, 42, Justification::centredLeft);
    g.setFont (FontOptions (14.0f).withKerningFactor (0.4f));
    g.drawText ("NORKA DSP", getWidth() - 262, 20, 232, 17, Justification::centredRight);
    g.setColour (L::textDim);
    g.setFont (FontOptions (10.0f).withKerningFactor (0.45f));
    g.drawText ("TUBE SATURATOR", getWidth() - 262, 39, 232, 13, Justification::centredRight);

    auto card = [&g] (Rectangle<int> r)
    {
        const auto b = r.toFloat();
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (b.translated (0.0f, 2.0f), 10.0f);
        g.setGradientFill (ColourGradient (Colour (0xff191714), 0.0f, b.getY(), Colour (0xff121110), 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (Colour (0xff2a2622));
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (Colours::white.withAlpha (0.03f));
        g.fillRect (b.getX() + 10.0f, b.getY() + 1.0f, b.getWidth() - 20.0f, 1.0f);
    };
    auto caption = [&g] (const String& t, Rectangle<int> r, Justification j = Justification::centredLeft)
    {
        g.setColour (L::text.withAlpha (0.88f));
        g.setFont (FontOptions (12.5f).withKerningFactor (0.38f));
        g.drawText (t, r, j);
    };
    for (auto r : { inputCard, eqCard, tubeCard, outputCard, envCard, mixArea }) card (r);

    caption ("INPUT", { inputCard.getX() + 16, inputCard.getY() + 12, 120, 16 });
    caption ("PRE-SAT EQ", { eqCard.getX() + 16, preEqLink.button.getY(), 140, preEqLink.button.getHeight() });
    caption ("OUTPUT", { outputCard.getX(), outputCard.getY() + 12, outputCard.getWidth(), 16 }, Justification::centred);
    caption ("ENVELOPE", { envCard.getX(), envCard.getY() + 10, envCard.getWidth(), 16 }, Justification::centred);
    caption ("MIX", { mixArea.getX() + 14, mixArea.getY() + 12, 60, 16 });

    // pre-sat EQ axes, outside the curve so the plot can be as big as possible
    {
        const auto r = preCurve.getBounds().toFloat();
        g.setFont (FontOptions (10.5f));
        g.setColour (L::textDim.withAlpha (0.9f));
        for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f })
        {
            const float y = r.getCentreY() - r.getHeight() * 0.5f * (db / 20.0f) * 0.9f;
            g.drawText ((db > 0.0f ? "+" : "") + String ((int) db), Rectangle<float> (r.getX() - 30.0f, y - 6.0f, 25.0f, 12.0f),
                        Justification::centredRight);
        }
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = r.getX() + r.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
            g.drawText (f >= 1000.0 ? String ((int) (f / 1000.0)) + "k" : String ((int) f),
                        Rectangle<float> (x - 16.0f, r.getBottom() + 6.0f, 32.0f, 12.0f), Justification::centred);
        }
    }
    {   // gesture cheat sheet
        const char* hints[6][2] { { "Drag", "freq + gain" }, { "Cmd drag", "Q" }, { "Opt drag", "freq only" },
                                  { "Shift hold", "solo" }, { "Dbl-click", "bypass" }, { "HP point", "the high-pass" } };
        auto r = cheatArea;
        const int colW = r.getWidth() / 2, rowH = r.getHeight() / 3;
        for (int i = 0; i < 6; ++i)
        {
            auto cell = Rectangle<int> (r.getX() + (i % 2) * colW, r.getY() + (i / 2) * rowH, colW, rowH);
            g.setFont (FontOptions (12.0f));
            g.setColour (L::text.withAlpha (0.88f));
            const String k (hints[i][0]);
            const int kw = (int) GlyphArrangement::getStringWidth (g.getCurrentFont(), k) + 9;
            g.drawText (k, cell.removeFromLeft (kw), Justification::centredLeft);
            g.setFont (FontOptions (12.0f, Font::italic));
            g.setColour (L::textDim.withAlpha (0.85f));
            g.drawText (hints[i][1], cell, Justification::centredLeft);
        }
    }

    // tube window: filaments that glow with how hard the tubes are working
    {
        const float d = jlimit (0.0f, 1.0f, lastGlow);
        auto w = tubeWindow.toFloat();
        const float rr = w.getHeight() * 0.5f;
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (w.expanded (2.0f).translated (0.0f, 1.0f), rr + 2.0f);
        g.setGradientFill (ColourGradient (Colour (0xff0b0908), 0.0f, w.getY(), Colour (0xff151210), 0.0f, w.getBottom(), false));
        g.fillRoundedRectangle (w, rr);
        g.saveState();
        Path clip;
        clip.addRoundedRectangle (w, rr);
        g.reduceClipRegion (clip);
        const auto c = w.getCentre();
        const float spread = w.getWidth() * (0.11f + 0.12f * d);
        g.setGradientFill (ColourGradient (Colour (0xffff9a30).withAlpha (0.12f + 0.45f * d), c.x, c.y,
                                           Colour (0xffff9a30).withAlpha (0.0f), c.x + spread * 2.2f, c.y, true));
        g.fillRect (w);
        const float pitch = 12.0f, barW = 5.0f;
        for (float bx = w.getX() + 10.0f; bx < w.getRight() - 8.0f; bx += pitch)
        {
            const float t = (bx + barW * 0.5f - c.x) / spread;
            const float k = std::exp (-t * t) * (0.25f + 0.75f * d);
            const auto col = Colour (0xff2a1a0e).interpolatedWith (Colour (0xffffb04a), jlimit (0.0f, 1.0f, k * 1.25f));
            auto bar = Rectangle<float> (bx, w.getY() + 7.0f, barW, w.getHeight() - 14.0f);
            g.setGradientFill (ColourGradient (col.withMultipliedBrightness (0.7f), 0.0f, bar.getY(), col, 0.0f, bar.getCentreY(), false));
            g.fillRoundedRectangle (bar, 1.5f);
            if (k > 0.35f)
            {
                g.setColour (Colour (0xffffe0b0).withAlpha (jlimit (0.0f, 0.7f, (k - 0.35f) * 1.4f)));
                g.fillRect (bar.reduced (1.6f, 4.0f));
            }
        }
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.10f), 0.0f, w.getY() + 2.0f,
                                           Colours::white.withAlpha (0.0f), 0.0f, w.getY() + 12.0f, false));
        g.fillRoundedRectangle (w.reduced (w.getWidth() * 0.06f, 2.0f).withHeight (10.0f), 5.0f);
        g.restoreState();
        g.setColour (Colour (0xff3a332b));
        g.drawRoundedRectangle (w, rr, 1.2f);
        g.setFont (FontOptions (10.5f).withKerningFactor (0.3f));
        auto lr = tubeWindow.translated (0, tubeWindow.getHeight() + 6).withHeight (13).reduced (24, 0);
        g.setColour (L::amber.withAlpha (0.55f));
        g.drawText ("CLEAN", lr, Justification::centredLeft);
        g.drawText ("DRIVEN", lr, Justification::centredRight);
        g.setColour (L::text.withAlpha (0.85f));
        g.drawText ("GLOW", lr, Justification::centred);
    }

    // faint halo around the Drive bezel
    {
        const float d = jlimit (0.0f, 1.0f, lastGlow);
        const auto gc = glowArea.toFloat().getCentre();
        const float gr = (float) glowArea.getWidth() * 0.5f;
        g.setGradientFill (ColourGradient (L::amber.withAlpha (0.04f + 0.12f * d), gc.x, gc.y, L::amber.withAlpha (0.0f), gc.x + gr, gc.y, true));
        g.fillEllipse (glowArea.toFloat());
    }

    // MIX: fader scales, values, divider between the strips
    for (auto* st : { &dry, &wet })
    {
        const auto& f = st->fader;
        g.setFont (FontOptions (10.0f));
        for (double db : { 6.0, 0.0, -6.0, -12.0, -24.0, -48.0 })
        {
            const float y = (float) f.getY() + (float) f.getPositionOfValue (db);
            g.setColour (L::textDim.withAlpha (std::abs (db) < 0.1 ? 1.0f : 0.75f));
            g.drawText (db > 0.0 ? "+" + String ((int) db) : String ((int) db), f.getX() - 26, (int) y - 6, 22, 12,
                        Justification::centredRight);
        }
        const double v = f.getValue();
        const String t = v <= f.getMinimum() + 0.01 ? String ("Off") : (v > 0.05 ? "+" : "") + String (v, 1) + " dB";
        g.setFont (FontOptions (12.0f));
        g.setColour (L::text.withAlpha (0.9f));
        g.drawText (t, f.getX() - 30, f.getBottom() + 4, f.getWidth() + 60, 15, Justification::centred);
    }
    g.setColour (Colour (0xff2a2622));
    g.fillRect (wet.label.getX() - 1, wet.label.getY(), 1, dry.fader.getBottom() + 18 - wet.label.getY());

    // amber line along the bottom, version
    g.setColour (Colour (0xffa8581f));
    g.fillRect (28, getHeight() - 18, getWidth() - 56, 1);
    g.setColour (L::textDim.withAlpha (0.5f));
    g.setFont (FontOptions (9.0f));
    g.drawText ("v" JucePlugin_VersionString, getWidth() - 140, getHeight() - 15, 112, 11, Justification::centredRight);
}

// geometry of the Drive knob's value arc, as the look-and-feel draws it
static void driveGeometry (const Slider& sl, Point<float>& c, float& ringR)
{
    auto r = sl.getLocalBounds().toFloat();
    c = r.getCentre();
    ringR = jmin (r.getWidth(), r.getHeight()) * 0.317f;
}

// Envelope follower: a cream arc + dot just outside Drive's amber value arc, at the Drive it's
// using right now. Drawn in modDot's own coordinates (it covers exactly the Drive knob).
void GlowEditor::paintModDot (Graphics& g)
{
    if (shownModDrive < 0.0f || panel != nullptr) return;
    auto& sl = drive.slider;
    Point<float> c; float rad;
    driveGeometry (sl, c, rad);
    rad += (float) sl.getWidth() * 0.045f;
    const auto rp = sl.getRotaryParameters();
    const float a0 = rp.startAngleRadians, a1 = rp.endAngleRadians;
    const float pos = (float) sl.valueToProportionOfLength (jlimit (0.0, 100.0, (double) shownModDrive));
    const float ang = a0 + pos * (a1 - a0);
    const float knobAng = a0 + (float) sl.valueToProportionOfLength (sl.getValue()) * (a1 - a0);
    Path arc;   // the stretch the envelope is adding/removing
    arc.addCentredArc (c.x, c.y, rad, rad, 0, jmin (ang, knobAng), jmax (ang, knobAng), true);
    g.setColour (GlowLookAndFeel::text.withAlpha (0.75f));
    g.strokePath (arc, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    const Point<float> pt (c.x + rad * std::sin (ang), c.y - rad * std::cos (ang));
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (pt.x - 5.0f, pt.y - 5.0f, 10.0f, 10.0f);
    g.setColour (GlowLookAndFeel::text);
    g.fillEllipse (pt.x - 3.5f, pt.y - 3.5f, 7.0f, 7.0f);
}

// Dotted lead from the Drive arc across to the ENVELOPE card: "these move Drive".
void GlowEditor::paintLead (Graphics& g)
{
    if (panel != nullptr) return;
    Point<float> local;
    float ringR = 0.0f;
    driveGeometry (drive.slider, local, ringR);
    const auto dc = local + drive.slider.getPosition().toFloat();
    const float y = envAmount.slider.getBounds().toFloat().getCentreY();
    const float dy = y - dc.y;
    if (std::abs (dy) >= ringR) return;
    const float x0 = dc.x + std::sqrt (ringR * ringR - dy * dy) + 8.0f;
    const float x1 = (float) envCard.getX() + 7.0f;
    if (x1 <= x0 + 8.0f) return;
    Path line;
    line.startNewSubPath (x0, y);
    line.lineTo (x1, y);
    constexpr float dashLengths[] { 2.0f, 4.0f };
    Path dashes;
    PathStrokeType (1.2f).createDashedStroke (dashes, line, dashLengths, 2);
    const bool active = shownModDrive >= 0.0f;
    g.setColour (GlowLookAndFeel::amber.withAlpha (active ? 0.8f : 0.5f));
    g.fillPath (dashes);
    g.fillEllipse (x1 - 2.2f, y - 2.2f, 4.4f, 4.4f);
}

// one MIX strip: name, dB scale | fader | meter | EQ, swap, COMP, M/S
void GlowEditor::layoutStrip (Strip& st, Rectangle<int> col)
{
    const int x = col.getX();
    st.label.setBounds (x, col.getY(), col.getWidth(), 18);
    const int top = col.getY() + 20;
    st.fader.setBounds (x + 26, top, 22, col.getBottom() - 22 - top);
    // icons stack in signal order; the swap button between them flips it
    const bool compFirst = st.compFirst != nullptr && st.compFirst->load() > 0.5f;
    st.shownCompFirst = compFirst;
    const int ix = x + 58, iw = col.getWidth() - 62;
    (compFirst ? st.compIcon : st.eqIcon).setBounds (ix, top + 7, iw, 42);
    st.swap.setBounds (ix, top + 52, iw, 18);
    (compFirst ? st.eqIcon : st.compIcon).setBounds (ix, top + 73, iw, 42);
    st.mute.button.setBounds (ix, top + 127, iw / 2 - 1, 21);
    st.solo.button.setBounds (ix + iw / 2 + 1, top + 127, iw - iw / 2 - 1, 21);
    // meter spans exactly the fader's travel (+6 dB at the top, Off at the bottom)
    const int yTop = st.fader.getY() + (int) st.fader.getPositionOfValue (st.fader.getMaximum());
    const int yBot = st.fader.getY() + (int) st.fader.getPositionOfValue (st.fader.getMinimum());
    st.meter.setBounds (x + 49, yTop, 5, jmax (10, yBot - yTop));
}

void GlowEditor::resized()
{
    // header
    presetBar.setBounds (329, 23, 404, 27);
    abA.setBounds (748, 23, 28, 27);
    abB.setBounds (785, 23, 28, 27);
    undoButton.setBounds (826, 23, 28, 27);
    redoButton.setBounds (863, 23, 28, 27);

    // cards
    inputCard = { 16, 77, 329, 162 };
    eqCard = { 16, 249, 329, 318 };
    tubeCard = { 361, 77, 367, 80 };
    outputCard = { 748, 77, 115, 209 };
    envCard = { 718, 374, 145, 114 };
    mixArea = { 878, 77, 208, 490 };
    inputArea = inputCard.getUnion (eqCard);
    tubeArea = { 361, 77, 367, 490 };
    outputArea = outputCard;
    overlayArea = { 16, 77, 847, 490 };

    // knob bounds: a square for the knob, plus a value box underneath where it shows one
    auto knobAt = [] (Knob& k, Point<int> c, int size, int valueH, Rectangle<int> label)
    {
        k.slider.setBounds (c.x - size / 2, c.y - size / 2, size, size + valueH);
        k.label.setBounds (label);
    };

    // INPUT: the high-pass with Keep lows and the slope, then the pre-sat EQ
    knobAt (hpf, { 84, 156 }, 84, 16, { 34, 215, 100, 15 });
    keepLows.button.setBounds (158, 127, 143, 28);
    slope12.setBounds (158, 169, 68, 26);
    slope24.setBounds (231, 169, 68, 26);
    preEqLink.button.setBounds (207, 256, 61, 24);
    preEqOn.button.setBounds (276, 256, 55, 24);
    preCurve.setBounds (46, 290, 288, 150);
    cheatArea = { 34, 476, 300, 66 };

    // TUBES: tube window, Even / Density / Odd, then Drive in its bezel
    tubeWindow = { 393, 88, 303, 42 };
    knobAt (even, { 422, 222 }, 98, 0, { 372, 265, 100, 16 });
    knobAt (density, { 545, 221 }, 98, 0, { 495, 263, 100, 16 });
    knobAt (odd, { 668, 222 }, 98, 0, { 618, 265, 100, 16 });
    knobAt (drive, { 545, 403 }, 246, 0, { 465, 493, 160, 22 });
    glowArea = drive.slider.getBounds().expanded (16);
    modDot.setBounds (drive.slider.getBounds());

    // OUTPUT, and the envelope follower below it
    knobAt (output, { 805, 156 }, 84, 16, { 755, 215, 100, 15 });
    autoGain.button.setBounds (765, 243, 81, 26);
    autoReadout.setBounds (outputCard.getX(), 270, outputCard.getWidth(), 13);
    knobAt (envAmount, { 756, 431 }, 58, 0, { 716, 461, 80, 14 });
    knobAt (envSpeed, { 823, 431 }, 58, 0, { 783, 461, 80, 14 });

    // MIX: Dry and Wet strips
    layoutStrip (dry, { 880, 108, 102, 446 });
    layoutStrip (wet, { 984, 108, 100, 446 });

    lead.setBounds (getLocalBounds());
    if (panel != nullptr) panel->setBounds (overlayArea);
}

AudioProcessorEditor* GlowProcessor::createEditor() { return new GlowEditor (*this); }
