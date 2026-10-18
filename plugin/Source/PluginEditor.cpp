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
        const float ra = S * 0.355f, aw = S * 0.03f;
        // warm halo around the knob
        const float rh = ra * 1.3f;
        g.setGradientFill (ColourGradient (amber.withAlpha ((0.05f + 0.2f * glow) * alpha), c.x, c.y,
                                           amber.withAlpha (0.0f), c.x + rh, c.y, true));
        g.fillEllipse (c.x - rh, c.y - rh, rh * 2.0f, rh * 2.0f);
        // ticks: 51, a long one every 5
        const float t0 = S * 0.39f;
        for (int i = 0; i <= 50; ++i)
        {
            const float a = a0 + (float) i / 50.0f * (a1 - a0);
            const bool major = i % 5 == 0;
            g.setColour (major ? text.withAlpha (0.7f * alpha) : textDim.withAlpha (0.45f * alpha));
            g.drawLine (Line<float> (polar (c, t0, a), polar (c, t0 + S * (major ? 0.028f : 0.014f), a)), major ? 1.4f : 0.9f);
        }
        const float fs = S * 0.062f;
        g.setFont (FontOptions (fs));
        g.setColour (text.withAlpha (0.88f * alpha));
        for (int i = 0; i <= 10; ++i)
        {
            const auto p = polar (c, S * 0.455f, a0 + (float) i / 10.0f * (a1 - a0));
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
        knobBody (g, c, S * 0.30f, S * 0.255f, ang, S * 0.02f, alpha, 9);
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
void GlowEditor::ChevronButton::paintButton (Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (GlowLookAndFeel::well.brighter (over ? 0.15f : 0.0f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (GlowLookAndFeel::edge.brighter (over ? 0.4f : 0.2f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    const auto c = r.getCentre();
    const float w = r.getWidth() * 0.22f, h = r.getHeight() * 0.13f, d = expanded ? -1.0f : 1.0f;
    Path p;
    p.startNewSubPath (c.x - w, c.y - d * h);
    p.lineTo (c.x, c.y + d * h);
    p.lineTo (c.x + w, c.y - d * h);
    g.setColour (GlowLookAndFeel::text.withAlpha (over ? 1.0f : 0.8f));
    g.strokePath (p, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
}

void GlowEditor::LampButton::paintButton (Graphics& g, bool over, bool)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    GlowLookAndFeel::drawLamp (g, c, over ? 5.0f : 4.5f, getToggleState());
}

//==============================================================================
GlowEditor::GlowEditor (GlowProcessor& p)
    : AudioProcessorEditor (p), proc (p), preCurve (p.apvts, "pre", { 1, 3 }), presetBar (p.presets)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;
    addAndMakeVisible (body);
    body.draw = [this] (Graphics& g) { paintBody (g); };
    body.onMouseDown = [this] (const MouseEvent& e) { if (e.mods.isPopupMenu()) showSizeMenu(); };
    body.addAndMakeVisible (presetBar);

    // ---- SATURATION (Drive first, so the smaller knobs around it sit on top of its bounds)
    drive.setup (body, s, "drive", "DRIVE", 25.0, 15.0f);
    drive.slider.getProperties().set ("hero", true);
    drive.label.setInterceptsMouseClicks (false, false);
    even.setup (body, s, "even", "EVEN", 50.0);
    density.setup (body, s, "density", "DENSITY", 50.0);
    odd.setup (body, s, "odd", "ODD", 50.0);
    envAmount.setup (body, s, "envAmount", "ENV AMOUNT", 0.0, 10.0f);
    envSpeed.setup (body, s, "envSpeed", "ENV SPEED", 50.0, 10.0f);
    for (auto* k : { &even, &odd, &density, &envAmount, &envSpeed }) k->numbers();
    for (auto* k : { &drive, &even, &odd, &density, &envAmount, &envSpeed }) k->popupValue();
    drive.tip ("DRIVE: how hard the tubes are pushed, for more of both even and odd harmonics. The glow and the tube meter "
               "show how hard they're working on the incoming audio.");
    even.tip ("EVEN: 2nd/4th harmonics (warmth, thickness). 50% = as voiced, 0% = none, 100% = 3x.");
    odd.tip ("ODD: 3rd/5th harmonics (edge, grit, clipping). 50% = as voiced; the level is matched for you.");
    density.tip ("DENSITY: how hard the first tube stage is hit. More = lopsided bloom and 'breathing' after loud hits.");
    envAmount.tip ("ENVELOPE amount: Drive follows the input's dynamics. + = louder moments get pushed harder (hits grit up, "
                   "quiet parts stay clean). - = quiet parts get more drive, loud parts less. "
                   "The cream dot on the Drive ring shows where it's pushing Drive right now.");
    envSpeed.tip ("ENVELOPE speed: how fast it follows. Fast = individual hits (drums), slow = phrases and sections.");
    envAmtP = s.getRawParameterValue ("envAmount");

    // ---- INPUT: high-pass (always visible); the full pre-sat EQ when expanded
    hpf.setup (body, s, "hpf", "HPF", GlowDSP::kHpfOffHz, 11.0f);
    hpf.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 14);
    hpf.tip ("HIGH-PASS: cuts lows before the tubes so they don't muddy the saturation. Bottom = Off, then 20 Hz to 1 kHz. "
             "Expand INPUT for the slope, Keep Lows and the pre-saturation EQ.");
    hpfP = s.getRawParameterValue ("hpf");
    body.addAndMakeVisible (inputExpand);
    inputExpand.setTooltip ("Show / hide the pre-saturation EQ (the window gets wider)");
    inputExpand.onClick = [this] { setExpanded (! inputOpen, mixerOpen); };
    keepLows.setup (body, s, "keepLows", "KEEP LOWS");
    keepLows.button.getProperties().set ("lamp", true);
    keepLows.button.setTooltip ("KEEP LOWS: everything the high-pass removes skips the tubes and comes back clean, so the low "
                                "end stays full and solid while only what's above it saturates. Off = a plain cut.");
    for (auto* b : { &slope12, &slope24 })
    {
        b->setClickingTogglesState (false);
        b->setTooltip ("High-pass slope: 12 dB/oct (gentle) or 24 dB/oct (steep)");
        body.addAndMakeVisible (b);
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
    preEqLink.setup (body, s, "preEqLink", "LINK");
    preEqLink.button.getProperties().set ("lamp", true);
    preEqLink.button.setTooltip ("LINK: the exact opposite of this EQ runs after the tubes (dashed curve). You change what "
                                 "saturates - push the mids into the tubes, say - while the overall tone stays the same.");
    body.addAndMakeVisible (preEqLamp);
    preEqLampAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, "preEqOn", preEqLamp);
    preEqLamp.setTooltip ("Pre-saturation EQ on/off (dragging a point switches it on)");
    preCurve.names = { "M", "H" };
    preCurve.fixedHighShelf = true;
    preCurve.axisOutside = true;
    preCurve.attachHpf (s.getParameter ("hpf"), s.getRawParameterValue ("hpfSlope"), s.getRawParameterValue ("keepLows"),
                        s.getRawParameterValue ("preEqLink"));
    preCurve.onSolo = [this] (int band) { proc.setEqSolo (3, band); };
    preCurve.setTooltip ("What goes into the tubes. M = mid bell, H = high shelf, HP = the high-pass. Drag a point: frequency + gain. "
                         "Cmd-drag: Q. Option-drag: frequency only. Hold Shift: solo the band. Double-click: bypass the band "
                         "(the HP point: Off). Option-double-click: gain to 0 dB.");
    body.addAndMakeVisible (preCurve);
    preFreq.setup (body, s, "preEqFreq2", "MID FREQ", 1000.0, 11.0f);
    preQ.setup (body, s, "preEqQ2", "MID Q", 0.7, 11.0f);
    for (auto* k : { &preFreq, &preQ }) k->slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 14);
    preFreq.tip ("Pre-saturation EQ: frequency of the mid bell (the M point)");
    preQ.tip ("Pre-saturation EQ: width of the mid bell (higher = narrower)");

    // ---- MIX & MASTER
    mix.setup (body, s, "mix", "MIX", 100.0, 11.0f);
    mix.popupValue();
    mix.tip ("MIX: dry/wet. 100% = all saturated, 0% = all clean (both paths stay phase-locked, so any blend is solid). "
             "Expand the mixer for each path's own trim, EQ and compressor.");
    mixP = s.getRawParameterValue ("mix");
    body.addAndMakeVisible (mixerExpand);
    mixerExpand.setTooltip ("Show / hide the Dry/Wet mixer (the window gets wider)");
    mixerExpand.onClick = [this] { setExpanded (inputOpen, ! mixerOpen); };
    for (auto* m : { &masterEq, &masterComp })
    {
        body.addAndMakeVisible (m);
        const bool isEq = m == &masterEq;
        m->setTooltip (String ("Master ") + (isEq ? "EQ" : "compressor") + " (after Dry and Wet are mixed): click the lamp to switch it on "
                       "or off, click the rest to open it. Double-click also switches it.");
        m->onSingleClick = [this, isEq] { togglePanel ("master", isEq ? ModuleIcon::Kind::eq : ModuleIcon::Kind::comp); };
        m->onDoubleClick = [this, isEq] { toggleParam (isEq ? "masterEqOn" : "masterCompOn"); };
    }
    masterEqOn = s.getRawParameterValue ("masterEqOn");
    masterCompOn = s.getRawParameterValue ("masterCompOn");
    masterCompFirst = s.getRawParameterValue ("masterCompFirst");
    body.addAndMakeVisible (masterSwap);
    masterSwapAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, "masterCompFirst", masterSwap);
    masterSwap.setTooltip ("Swap the master order: EQ then Comp, or Comp then EQ (top runs first)");
    setupStrip (dry, "dry", "DRY");
    setupStrip (wet, "wet", "WET");

    // ---- bottom strip: Input, Output, Auto
    input.setup (body, s, "inputGain", "INPUT", 0.0, 11.0f);
    output.setup (body, s, "output", "OUTPUT", 0.0, 11.0f);
    for (auto* k : { &input, &output }) k->slider.setTextBoxStyle (Slider::TextBoxBelow, false, 60, 13);
    input.tip ("INPUT: level into Glow (before the high-pass and the tubes). Push it to drive the tubes harder; Auto Gain "
               "keeps the output level steady.");
    output.tip ("OUTPUT: final level trim, after everything (and after Auto)");
    autoGain.setup (body, s, "autoGain", "AUTO");
    autoGain.button.getProperties().set ("lamp", true);

    // ---- header
    for (auto* b : { &abA, &abB })
    {
        b->setClickingTogglesState (false);
        body.addAndMakeVisible (b);
    }
    abA.setTooltip ("A/B: two complete settings to compare. Click B to switch (it starts as a copy of A).");
    abB.setTooltip (abA.getTooltip());
    abA.onClick = [this] { proc.switchAB (0); };
    abB.onClick = [this] { proc.switchAB (1); };
    undoButton.setTooltip ("Undo");
    redoButton.setTooltip ("Redo");
    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };
    body.addAndMakeVisible (undoButton);
    body.addAndMakeVisible (redoButton);

    // knob names: regular weight, letter-spaced
    for (auto* k : { &hpf, &even, &odd, &density, &envAmount, &envSpeed, &mix, &input, &output, &preFreq, &preQ })
    {
        k->label.setFont (FontOptions (k == &input || k == &output ? 10.5f : 11.5f).withKerningFactor (0.16f));
        k->label.setColour (Label::textColourId, GlowLookAndFeel::text.withAlpha (0.88f));
        k->label.setInterceptsMouseClicks (false, false);
    }
    for (auto* k : { &input, &output }) k->label.setJustificationType (Justification::centredRight);
    output.label.setJustificationType (Justification::centredLeft);
    drive.label.setFont (FontOptions (15.0f).withKerningFactor (0.3f));
    drive.label.setColour (Label::textColourId, GlowLookAndFeel::text);

    modDot.draw = [this] (Graphics& g) { paintModDot (g); };
    lead.draw = [this] (Graphics& g) { paintLead (g); };
    body.addAndMakeVisible (lead);
    body.addAndMakeVisible (modDot);

    sendLookAndFeelChange(); // re-skin value boxes created before the knobs were parented
    inputOpen = proc.uiInputOpen;
    mixerOpen = proc.uiMixerOpen;
    uiScale = proc.uiScale;
    applyLayout();
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
    st.fader.setPopupDisplayEnabled (true, true, nullptr);
    st.fader.onValueChange = [this] { body.repaint (mixCard); };
    st.fader.setDoubleClickReturnValue (true, 0.0);
    st.label.setText (name, dontSendNotification);
    st.label.setJustificationType (Justification::centred);
    st.label.setFont (FontOptions (11.5f).withKerningFactor (0.3f));
    st.label.setColour (Label::textColourId, GlowLookAndFeel::text);
    for (Component* c : { (Component*) &st.fader, (Component*) &st.label, (Component*) &st.meter, (Component*) &st.eqIcon,
                          (Component*) &st.compIcon, (Component*) &st.swap })
        body.addAndMakeVisible (c);
    st.swapAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, prefix + "CompFirst", st.swap);
    st.swap.setTooltip ("Swap the order: EQ then Comp, or Comp then EQ (top runs first)");
    st.mute.setup (body, proc.apvts, prefix + "Mute", "M");
    st.solo.setup (body, proc.apvts, prefix + "Solo", "S");
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
    st.fader.setTooltip (prefix == "dry" ? "DRY trim: level of the clean path after the Mix (bottom = Off)"
                                         : "WET trim: level of the saturated path after the Mix (bottom = Off)");
    st.meter.setTooltip (name + " level (after its trim and the Mix)");
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

    const String name = prefix == "dry" ? "DRY" : prefix == "wet" ? "WET" : "MASTER";
    if (kind == ModuleIcon::Kind::eq)
        panel = std::make_unique<EqPanel> (proc.apvts, prefix, name);
    else
        panel = std::make_unique<CompPanel> (proc.apvts, prefix, name, [this, prefix]
                     { return prefix == "dry" ? proc.dryGainReductionDb() : prefix == "wet" ? proc.wetGainReductionDb() : proc.masterGainReductionDb(); },
                     [this, prefix] { return prefix == "dry" ? proc.dryMakeupDb() : prefix == "wet" ? proc.wetMakeupDb() : proc.masterMakeupDb(); });
    panelPrefix = prefix;
    panelKind = kind;
    if (kind == ModuleIcon::Kind::eq)
    {
        const int path = prefix == "dry" ? 1 : prefix == "wet" ? 2 : 4;
        panel->setSoloHandler ([this, path] (int band) { proc.setEqSolo (path, band); });
    }
    panel->onClose = [this] { MessageManager::callAsync ([sp = Component::SafePointer<GlowEditor> (this)]
                                                         { if (sp != nullptr) sp->closePanel(); }); };
    body.addAndMakeVisible (*panel);
    panel->sendLookAndFeelChange();
    applyLayout();     // the drawer opens under the main controls
    panel->refresh();
}

void GlowEditor::closePanel()
{
    proc.setEqSolo (0, -1);   // never leave a band soloed
    if (panel != nullptr)
    {
        body.removeChildComponent (panel.get());
        panel.reset();
        applyLayout();
    }
}

void GlowEditor::setExpanded (bool in, bool mx)
{
    inputOpen = proc.uiInputOpen = in;
    mixerOpen = proc.uiMixerOpen = mx;
    applyLayout();
}

void GlowEditor::showSizeMenu()
{
    PopupMenu m;
    m.setLookAndFeel (&lnf);
    m.addSectionHeader ("INTERFACE SIZE");
    const float sizes[] { 0.75f, 0.9f, 1.0f, 1.15f, 1.3f };
    for (int i = 0; i < 5; ++i)
        m.addItem (i + 1, String (roundToInt (sizes[i] * 100.0f)) + " %", true, std::abs (sizes[i] - uiScale) < 0.01f);
    m.showMenuAsync (PopupMenu::Options(), [this, sp = Component::SafePointer<GlowEditor> (this)] (int r)
    {
        const float sz[] { 0.75f, 0.9f, 1.0f, 1.15f, 1.3f };
        if (sp == nullptr || r < 1 || r > 5) return;
        uiScale = proc.uiScale = sz[r - 1];
        applyLayout();
    });
}

void GlowEditor::timerCallback()
{
    // the host restored a session with a different view
    if (proc.uiInputOpen != inputOpen || proc.uiMixerOpen != mixerOpen || std::abs (proc.uiScale - uiScale) > 0.001f)
    {
        inputOpen = proc.uiInputOpen; mixerOpen = proc.uiMixerOpen; uiScale = proc.uiScale;
        applyLayout();
    }
    {   // auto gain: what it's applying shows on the AUTO button's tooltip
        const float a = std::round (proc.autoGainDb() * 10.0f) / 10.0f;
        if (std::abs (a - shownAutoDb) > 0.01f)
        {
            shownAutoDb = a;
            autoGain.button.setTooltip ("AUTO gain: keeps the output as loud as the input (slow, ~3 s), so Drive/EQ/comp/Input moves "
                                        "don't fool your ears. Applying " + String (a > 0.05f ? "+" : "") + String (a, 1) + " dB now. "
                                        "Output trims on top.");
        }
    }

    // one undo step per gesture: a new transaction whenever no mouse button is held
    if (! ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        proc.undoManager.beginNewTransaction();
    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());
    abA.setToggleState (proc.getAB() == 0, dontSendNotification);
    abB.setToggleState (proc.getAB() == 1, dontSendNotification);

    if (inputOpen) preCurve.refresh();
    dry.meter.push (proc.takeDryPeak());
    wet.meter.push (proc.takeWetPeak());
    for (auto* st : { &dry, &wet })
    {
        const bool mine = panel != nullptr && panelPrefix == st->prefix;
        st->eqIcon.setState (st->eqOn->load() > 0.5f, mine && panelKind == ModuleIcon::Kind::eq);
        st->compIcon.setState (st->compOn->load() > 0.5f, mine && panelKind == ModuleIcon::Kind::comp);
    }
    {
        const bool mine = panel != nullptr && panelPrefix == "master";
        masterEq.setState (masterEqOn->load() > 0.5f, mine && panelKind == ModuleIcon::Kind::eq);
        masterComp.setState (masterCompOn->load() > 0.5f, mine && panelKind == ModuleIcon::Kind::comp);
    }
    if (panel != nullptr) panel->refresh();
    presetBar.refresh();

    // section lamps (HPF in, Mix/master doing something, mixer modules in)
    {
        const int lamps = (hpfP->load() >= GlowDSP::kHpfMinHz - 0.5f ? 1 : 0)
                        | ((mixP->load() < 99.95f || masterEqOn->load() > 0.5f || masterCompOn->load() > 0.5f) ? 2 : 0)
                        | ((dry.eqOn->load() > 0.5f || dry.compOn->load() > 0.5f || wet.eqOn->load() > 0.5f || wet.compOn->load() > 0.5f) ? 4 : 0);
        if (lamps != (int) body.getProperties().getWithDefault ("lamps", -1))
        {
            body.getProperties().set ("lamps", lamps);
            body.repaint();
        }
    }

    // envelope follower: move the dot on the Drive ring (the leads light up while it's in use)
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
    if ((masterCompFirst->load() > 0.5f) != shownMasterCompFirst) relayout = true;
    if (relayout) applyLayout();

    // Drive glow + tube meter follow how hard the tubes are actually working (distortion vs the
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
            body.repaint (glowArea);
            body.repaint (tubeWindow.expanded (6));
        }
    }
}

void GlowEditor::paint (Graphics& g)
{
    g.fillAll (Colour (0xff090807));
}

juce::Rectangle<int> GlowEditor::designSize() const
{
    const bool tall = inputOpen || mixerOpen;
    const int w = tall ? 12 + (inputOpen ? 532 : 228) + 11 + 400 + 11 + (mixerOpen ? 548 : 341) + 13 : 1116;
    const int h = (tall ? 358 : 262) + (panel != nullptr ? 300 : 0);
    return { 0, 0, w, h };
}

void GlowEditor::paintBody (Graphics& g)
{
    using L = GlowLookAndFeel;
    const auto all = body.getLocalBounds().toFloat();
    g.fillAll (Colour (0xff090807));
    const auto face = all.reduced (1.0f);
    g.setGradientFill (ColourGradient (Colour (0xff1a1815), 0.0f, 0.0f, Colour (0xff0d0c0b), 0.0f, all.getHeight(), false));
    g.fillRoundedRectangle (face, 12.0f);
    {   // soft vignette towards the corners
        const auto c = face.getCentre();
        g.setGradientFill (ColourGradient (Colours::transparentBlack, c.x, c.y, Colours::black.withAlpha (0.3f), 0.0f, 0.0f, true));
        g.fillRoundedRectangle (face, 12.0f);
    }
    g.setColour (Colour (0xff2a2622));
    g.drawRoundedRectangle (face, 12.0f, 1.0f);

    const int W = body.getWidth();
    const int lamps = (int) body.getProperties().getWithDefault ("lamps", 0);

    // header: pilot lamp + wordmark, company on the right
    L::drawLamp (g, { 30.0f, 21.0f }, 6.5f, true);
    g.setColour (L::text);
    g.setFont (FontOptions (28.0f, Font::bold).withKerningFactor (0.42f));
    g.drawText ("GLOW", 50, 2, 200, 38, Justification::centredLeft);
    g.setFont (FontOptions (12.5f).withKerningFactor (0.4f));
    g.drawText ("NORKA DSP", W - 262, 8, 240, 15, Justification::centredRight);
    g.setColour (L::textDim);
    g.setFont (FontOptions (9.5f).withKerningFactor (0.45f));
    g.drawText ("TUBE SATURATOR", W - 262, 24, 240, 12, Justification::centredRight);

    auto card = [&g] (Rectangle<int> r, float alpha = 1.0f)
    {
        const auto b = r.toFloat();
        g.setColour (Colours::black.withAlpha (0.35f * alpha));
        g.fillRoundedRectangle (b.translated (0.0f, 2.0f), 9.0f);
        g.setGradientFill (ColourGradient (Colour (0xff191714).withAlpha (alpha), 0.0f, b.getY(), Colour (0xff121110).withAlpha (alpha), 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle (b, 9.0f);
        g.setColour (Colour (0xff2a2622).withAlpha (alpha));
        g.drawRoundedRectangle (b.reduced (0.5f), 9.0f, 1.0f);
    };
    auto title = [&g] (const String& t, int x, int y, bool lampOn, bool drawLampDot = true)
    {
        if (drawLampDot) L::drawLamp (g, { (float) x, (float) y }, 4.5f, lampOn);
        g.setColour (L::text.withAlpha (0.9f));
        g.setFont (FontOptions (12.5f).withKerningFactor (0.3f));
        g.drawText (t, x + 14, y - 8, 220, 16, Justification::centredLeft);
    };
    auto small = [&g] (const String& t, float cx, float cy, Justification j = Justification::centred, float alpha = 0.75f)
    {
        g.setColour (L::text.withAlpha (alpha));
        g.setFont (FontOptions (10.0f));
        g.drawText (t, Rectangle<float> (cx - 40.0f, cy - 6.0f, 80.0f, 12.0f), j);
    };
    auto rangeLabels = [&small] (const Knob& k, const String& lo, const String& hi)
    {
        const auto b = k.slider.getBounds().toFloat();
        const float S = jmin (b.getWidth(), b.getHeight() - (k.slider.getTextBoxHeight() > 0 ? (float) k.slider.getTextBoxHeight() : 0.0f));
        const float cy = b.getY() + S * 0.5f + S * 0.40f, cx = b.getCentreX();
        small (lo, cx - S * 0.42f, cy);
        small (hi, cx + S * 0.42f, cy);
    };

    // ---- left: HPF card or the full pre-saturation EQ
    card (leftCard);
    if (inputOpen)
    {
        // title lamp is the ON button (a component)
        g.setColour (L::text.withAlpha (0.9f));
        g.setFont (FontOptions (12.5f).withKerningFactor (0.3f));
        g.drawText ("PRE-SAT EQ", leftCard.getX() + 30, leftCard.getY() + 9, 160, 16, Justification::centredLeft);
        // EQ axes outside the plot
        const auto r = preCurve.getBounds().toFloat();
        g.setFont (FontOptions (10.0f));
        g.setColour (L::textDim);
        for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f })
        {
            const float y = r.getCentreY() - r.getHeight() * 0.5f * (db / 20.0f) * 0.9f;
            g.drawText ((db > 0.0f ? "+" : "") + String ((int) db), Rectangle<float> (r.getX() - 30.0f, y - 6.0f, 25.0f, 12.0f), Justification::centredRight);
        }
        for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const float x = r.getX() + r.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
            g.drawText (f >= 1000.0 ? String ((int) (f / 1000.0)) + "k" : String ((int) f),
                        Rectangle<float> (x - 16.0f, r.getBottom() + 5.0f, 32.0f, 12.0f), Justification::centred);
        }
        rangeLabels (hpf, "20 Hz", "1 kHz");
        rangeLabels (preFreq, "40 Hz", "12 kHz");
        rangeLabels (preQ, "0.1", "10");
    }
    else
    {
        title ("HPF", leftCard.getX() + 20, leftCard.getY() + 22, (lamps & 1) != 0);
        rangeLabels (hpf, "20 Hz", "1 kHz");
    }

    // ---- centre: saturation (a card in the tall view; open in the compact one)
    if (inputOpen || mixerOpen) card (centreCard);
    else card (centreCard, 0.55f);
    {   // tube meter: filaments that glow with how hard the tubes are working
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
            auto bar = Rectangle<float> (bx, w.getY() + 6.0f, barW, w.getHeight() - 12.0f);
            g.setGradientFill (ColourGradient (col.withMultipliedBrightness (0.7f), 0.0f, bar.getY(), col, 0.0f, bar.getCentreY(), false));
            g.fillRoundedRectangle (bar, 1.5f);
            if (k > 0.35f)
            {
                g.setColour (Colour (0xffffe0b0).withAlpha (jlimit (0.0f, 0.7f, (k - 0.35f) * 1.4f)));
                g.fillRect (bar.reduced (1.6f, 3.0f));
            }
        }
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.10f), 0.0f, w.getY() + 2.0f,
                                           Colours::white.withAlpha (0.0f), 0.0f, w.getY() + 10.0f, false));
        g.fillRoundedRectangle (w.reduced (w.getWidth() * 0.06f, 2.0f).withHeight (8.0f), 4.0f);
        g.restoreState();
        g.setColour (Colour (0xff3a332b));
        g.drawRoundedRectangle (w, rr, 1.2f);
    }
    {   // faint halo around Drive
        const float d = jlimit (0.0f, 1.0f, lastGlow);
        const auto gc = glowArea.toFloat().getCentre();
        const float gr = (float) glowArea.getWidth() * 0.5f;
        g.setGradientFill (ColourGradient (L::amber.withAlpha (0.03f + 0.10f * d), gc.x, gc.y, L::amber.withAlpha (0.0f), gc.x + gr, gc.y, true));
        g.fillEllipse (glowArea.toFloat());
    }

    // ---- right: MIX (& MASTER), and the mixer
    card (mixCard);
    title (mixerOpen ? "MIX & MASTER" : "MIX", mixCard.getX() + 18, mixCard.getY() + (mixerOpen ? 17 : 22), (lamps & 2) != 0);
    {
        const auto b = mix.slider.getBounds().toFloat();
        const float S = jmin (b.getWidth(), b.getHeight());
        small ("0%", b.getCentreX() - S * 0.40f, b.getCentreY() + S * 0.42f);
        small ("100%", b.getCentreX() + S * 0.40f, b.getCentreY() + S * 0.42f);
    }
    if (mixerOpen)
    {
        card (mixerCard);
        title ("MIXER", mixerCard.getX() + 18, mixerCard.getY() + 13, (lamps & 4) != 0);
        for (auto* st : { &dry, &wet })
        {
            const auto& f = st->fader;
            g.setFont (FontOptions (9.5f));
            for (double db : { 6.0, 0.0, -12.0, -48.0 })
            {
                const float y = (float) f.getY() + (float) f.getPositionOfValue (db);
                g.setColour (L::textDim.withAlpha (std::abs (db) < 0.1 ? 1.0f : 0.75f));
                g.drawText (db > 0.0 ? "+" + String ((int) db) : String ((int) db), st->meter.getX() - 26, (int) y - 5, 21, 10,
                            Justification::centredRight);
            }
            const double v = f.getValue();
            const String t = v <= f.getMinimum() + 0.01 ? String ("Off") : (v > 0.05 ? "+" : "") + String (v, 1) + " dB";
            g.setFont (FontOptions (11.0f));
            g.setColour (L::text.withAlpha (0.9f));
            g.drawText (t, st->meter.getX() - 10, f.getBottom() + 1, f.getRight() - st->meter.getX() + 20, 13, Justification::centred);
        }
        g.setColour (Colour (0xff2a2622));
        g.fillRect (mixerCard.getX() + 282, mixerCard.getY() + 26, 1, mixerCard.getHeight() - 34);
    }

    // ---- bottom strip: INPUT | OUTPUT  AUTO
    g.setColour (Colour (0xff2a2622));
    g.fillRect (bottomArea.getX() + 140, bottomArea.getY() + 4, 1, 22);

    // version, bottom left
    g.setColour (L::textDim.withAlpha (0.45f));
    g.setFont (FontOptions (9.0f));
    g.drawText ("v" JucePlugin_VersionString, 18, mainH - 16, 100, 11, Justification::centredLeft);
}

// geometry of the Drive knob's value arc, as the look-and-feel draws it
static void driveGeometry (const Slider& sl, Point<float>& c, float& ringR)
{
    auto r = sl.getLocalBounds().toFloat();
    c = r.getCentre();
    ringR = jmin (r.getWidth(), r.getHeight()) * 0.355f;
}

// Envelope follower: a cream arc + dot just outside Drive's amber value arc, at the Drive it's
// using right now. Drawn in modDot's own coordinates (it covers exactly the Drive knob).
void GlowEditor::paintModDot (Graphics& g)
{
    if (shownModDrive < 0.0f) return;
    auto& sl = drive.slider;
    Point<float> c; float rad;
    driveGeometry (sl, c, rad);
    rad += (float) sl.getWidth() * 0.05f;
    const auto rp = sl.getRotaryParameters();
    const float a0 = rp.startAngleRadians, a1 = rp.endAngleRadians;
    const float pos = (float) sl.valueToProportionOfLength (jlimit (0.0, 100.0, (double) shownModDrive));
    const float ang = a0 + pos * (a1 - a0);
    const float knobAng = a0 + (float) sl.valueToProportionOfLength (sl.getValue()) * (a1 - a0);
    Path arc;   // the stretch the envelope is adding/removing
    arc.addCentredArc (c.x, c.y, rad, rad, 0, jmin (ang, knobAng), jmax (ang, knobAng), true);
    g.setColour (GlowLookAndFeel::text.withAlpha (0.7f));
    g.strokePath (arc, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    const Point<float> pt (c.x + rad * std::sin (ang), c.y - rad * std::cos (ang));
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (pt.x - 4.5f, pt.y - 4.5f, 9.0f, 9.0f);
    g.setColour (GlowLookAndFeel::text);
    g.fillEllipse (pt.x - 3.0f, pt.y - 3.0f, 6.0f, 6.0f);
}

// Faint straight dotted leads from the envelope knobs to the Drive ring: "these move Drive".
void GlowEditor::paintLead (Graphics& g)
{
    Point<float> local;
    float ringR = 0.0f;
    driveGeometry (drive.slider, local, ringR);
    const auto dc = local + drive.slider.getPosition().toFloat();
    const bool active = shownModDrive >= 0.0f;
    constexpr float dashLengths[] { 1.5f, 4.0f };
    for (auto* k : { &envAmount, &envSpeed })
    {
        const auto kb = k->slider.getBounds().toFloat();
        const float y = kb.getCentreY();
        const float dy = y - dc.y;
        if (std::abs (dy) >= ringR) continue;
        const float edge = std::sqrt (ringR * ringR - dy * dy) + 9.0f;
        const float knobR = kb.getWidth() * 0.5f;
        const bool left = kb.getCentreX() < dc.x;
        const float x0 = left ? kb.getCentreX() + knobR : dc.x + edge;
        const float x1 = left ? dc.x - edge : kb.getCentreX() - knobR;
        if (x1 <= x0 + 4.0f) continue;
        Path line;
        line.startNewSubPath (x0, y);
        line.lineTo (x1, y);
        Path dashes;
        PathStrokeType (1.0f).createDashedStroke (dashes, line, dashLengths, 2);
        g.setColour (GlowLookAndFeel::amber.withAlpha (active ? 0.55f : 0.28f));
        g.fillPath (dashes);
    }
}

// one mixer strip: name, dB scale | meter | fader, and EQ / COMP (+ order) / M S
void GlowEditor::layoutStrip (Strip& st, Rectangle<int> r)
{
    const int x = r.getX(), y = r.getY();
    st.label.setBounds (x + 40, y, 70, 14);
    st.meter.setBounds (x + 42, y + 22, 8, 10);   // height set below from the fader travel
    st.fader.setBounds (x + 62, y + 14, 22, 80);   // leaves room for the value under it inside the card
    const int yTop = st.fader.getY() + (int) st.fader.getPositionOfValue (st.fader.getMaximum());
    const int yBot = st.fader.getY() + (int) st.fader.getPositionOfValue (st.fader.getMinimum());
    st.meter.setBounds (x + 42, yTop, 8, jmax (10, yBot - yTop));
    // icons stack in signal order; the arrows beside them flip it
    const bool compFirst = st.compFirst != nullptr && st.compFirst->load() > 0.5f;
    st.shownCompFirst = compFirst;
    const int ix = x + 100, iw = 62;
    (compFirst ? st.compIcon : st.eqIcon).setBounds (ix, y + 16, iw, 23);
    (compFirst ? st.eqIcon : st.compIcon).setBounds (ix, y + 44, iw, 23);
    st.swap.setBounds (ix + iw + 2, y + 24, 16, 36);
    st.mute.button.setBounds (ix + 2, y + 74, 27, 22);
    st.solo.button.setBounds (ix + 33, y + 74, 27, 22);
}

void GlowEditor::applyLayout()
{
    const bool tall = inputOpen || mixerOpen;
    mainH = tall ? 358 : 262;
    const auto ds = designSize();
    const int W = ds.getWidth();
    const int top = 45, gap = tall ? 11 : 7;

    // header: preset bar, A/B, undo/redo, roughly centred
    const int hx = W / 2 - 258;
    presetBar.setBounds (hx, 7, 427, 26);
    abA.setBounds (hx + 439, 7, 28, 26);
    abB.setBounds (hx + 476, 7, 28, 26);
    undoButton.setBounds (hx + 518, 7, 28, 26);
    redoButton.setBounds (hx + 552, 7, 28, 26);

    leftCard = { tall ? 12 : 17, top, inputOpen ? 532 : 228, tall ? 266 : 158 };
    centreCard = { leftCard.getRight() + gap, top, tall ? 400 : 485, tall ? 290 : 205 };
    mixCard = { centreCard.getRight() + gap, top, mixerOpen ? 548 : 341, tall ? 258 : 158 };

    auto knobAt = [] (Knob& k, Point<int> c, int size, int valueH, Rectangle<int> label)
    {
        k.slider.setBounds (c.x - size / 2, c.y - size / 2, size, size + valueH);
        k.label.setBounds (label);
    };
    auto rel = [] (Rectangle<int> card, int dx, int dy) { return Point<int> (card.getX() + dx, card.getY() + dy); };
    auto labelAt = [] (Point<int> c, int w = 110, int h = 14) { return Rectangle<int> (c.x - w / 2, c.y - h / 2, w, h); };

    // ---- INPUT
    for (Component* c : { (Component*) &keepLows.button, (Component*) &slope12, (Component*) &slope24, (Component*) &preEqLink.button,
                          (Component*) &preEqLamp, (Component*) &preCurve, (Component*) &preFreq.slider, (Component*) &preFreq.label,
                          (Component*) &preQ.slider, (Component*) &preQ.label })
        c->setVisible (inputOpen);
    inputExpand.expanded = inputOpen;
    if (inputOpen)
    {
        const auto L = leftCard;
        preEqLamp.setBounds (L.getX() + 8, L.getY() + 7, 20, 20);
        inputExpand.setBounds (L.getRight() - 34, L.getY() + 6, 24, 22);
        preEqLink.button.setBounds (L.getRight() - 104, L.getY() + 6, 62, 22);
        preCurve.setBounds (L.getX() + 40, L.getY() + 36, 478, 104);
        knobAt (hpf, rel (L, 78, 214), 85, 14, labelAt (rel (L, 78, 168)));
        keepLows.button.setBounds (L.getX() + 152, L.getY() + 186, 116, 26);
        slope12.setBounds (L.getX() + 152, L.getY() + 220, 55, 22);
        slope24.setBounds (L.getX() + 213, L.getY() + 220, 55, 22);
        knobAt (preFreq, rel (L, 345, 214), 85, 14, labelAt (rel (L, 345, 168)));
        knobAt (preQ, rel (L, 462, 214), 85, 14, labelAt (rel (L, 462, 168)));
    }
    else
    {
        const auto L = leftCard;
        const int oy = tall ? 50 : 0;
        inputExpand.setBounds (L.getRight() - 38, L.getY() + 10 + (tall ? 0 : 0), 25, 24);
        knobAt (hpf, rel (L, 113, 86 + oy), 107, 14, labelAt ({ 0, -100 }));   // the card's title names it
    }
    hpf.label.setVisible (inputOpen);

    // ---- SATURATION
    {
        const auto C = centreCard;
        if (! tall)
        {
            tubeWindow = { C.getX() + 95, C.getY() + 2, 297, 25 };
            knobAt (even, rel (C, 146, 62), 76, 0, labelAt (rel (C, 146, 97)));
            knobAt (density, rel (C, 242, 62), 76, 0, labelAt (rel (C, 242, 97)));
            knobAt (odd, rel (C, 341, 62), 76, 0, labelAt (rel (C, 341, 97)));
            knobAt (drive, rel (C, 243, 166), 122, 0, labelAt (rel (C, 243, 209), 70, 16));
            knobAt (envAmount, rel (C, 104, 160), 70, 0, labelAt (rel (C, 104, 196)));
            knobAt (envSpeed, rel (C, 382, 160), 70, 0, labelAt (rel (C, 382, 196)));
        }
        else
        {
            tubeWindow = { C.getX() + 47, C.getY() + 8, 306, 30 };
            knobAt (even, rel (C, 101, 82), 90, 0, labelAt (rel (C, 101, 121)));
            knobAt (density, rel (C, 200, 82), 90, 0, labelAt (rel (C, 200, 121)));
            knobAt (odd, rel (C, 299, 82), 90, 0, labelAt (rel (C, 299, 121)));
            knobAt (drive, rel (C, 200, 211), 160, 0, labelAt (rel (C, 200, 278), 80, 18));
            knobAt (envAmount, rel (C, 62, 214), 84, 0, labelAt (rel (C, 62, 254)));
            knobAt (envSpeed, rel (C, 338, 214), 84, 0, labelAt (rel (C, 338, 254)));
        }
        glowArea = drive.slider.getBounds().expanded (8);
        modDot.setBounds (drive.slider.getBounds());
    }

    // ---- MIX & MASTER
    {
        const auto M = mixCard;
        mixerExpand.expanded = mixerOpen;
        const bool cf = masterCompFirst->load() > 0.5f;
        shownMasterCompFirst = cf;
        if (! mixerOpen)
        {
            const int oy = tall ? 42 : 0;
            mixerExpand.setBounds (M.getRight() - 38, M.getY() + 10, 25, 24);
            knobAt (mix, rel (M, 100, 86 + oy), 110, 0, labelAt ({ 0, -100 }));
            (cf ? masterComp : masterEq).setBounds (M.getX() + 178, M.getY() + 42 + oy, 150, 36);
            (cf ? masterEq : masterComp).setBounds (M.getX() + 178, M.getY() + 90 + oy, 150, 36);
            masterSwap.setBounds (M.getX() + 158, M.getY() + 66 + oy, 18, 36);
        }
        else
        {
            mixerExpand.setBounds (M.getRight() - 34, M.getY() + 6, 24, 22);
            knobAt (mix, rel (M, 222, 64), 92, 0, labelAt ({ 0, -100 }));
            (cf ? masterComp : masterEq).setBounds (M.getX() + 318, M.getY() + 30, 206, 32);
            (cf ? masterEq : masterComp).setBounds (M.getX() + 318, M.getY() + 70, 206, 32);
            masterSwap.setBounds (M.getX() + 296, M.getY() + 46, 18, 40);
        }
        mix.label.setVisible (false);
        for (auto* st : { &dry, &wet })
            for (Component* c : { (Component*) &st->fader, (Component*) &st->label, (Component*) &st->meter, (Component*) &st->eqIcon,
                                  (Component*) &st->compIcon, (Component*) &st->swap, (Component*) &st->mute.button, (Component*) &st->solo.button })
                c->setVisible (mixerOpen);
        if (mixerOpen)
        {
            mixerCard = { M.getX() + 2, M.getY() + 116, M.getWidth() - 4, 140 };
            layoutStrip (dry, { mixerCard.getX() + 60, mixerCard.getY() + 24, 200, 112 });
            layoutStrip (wet, { mixerCard.getX() + 300, mixerCard.getY() + 24, 200, 112 });
        }
    }

    // ---- bottom strip (right): INPUT knob | OUTPUT knob, AUTO
    {
        const int R = mixCard.getRight(), cy = tall ? 322 : 226;
        bottomArea = { R - 300, cy - 15, 304, 30 };
        knobAt (input, { R - 228, cy }, 40, 13, { R - 300, cy - 7, 46, 14 });
        knobAt (output, { R - 148, cy }, 40, 13, { R - 124, cy - 7, 54, 14 });
        autoGain.button.setBounds (R - 66, cy - 13, 70, 26);
    }

    // ---- the EQ / COMP drawer under the main controls
    drawerArea = { 12, mainH, W - 24, 288 };
    if (panel != nullptr) panel->setBounds (drawerArea);

    lead.setBounds (0, 0, W, mainH);
    body.setBounds (ds);
    body.setTransform (AffineTransform::scale (uiScale));
    const int w = roundToInt ((float) ds.getWidth() * uiScale), h = roundToInt ((float) ds.getHeight() * uiScale);
    if (getWidth() != w || getHeight() != h) setSize (w, h);
    body.repaint();
}

void GlowEditor::resized()
{
    // the host decides the size (it follows applyLayout); keep the scaled body filling it
}

AudioProcessorEditor* GlowProcessor::createEditor() { return new GlowEditor (*this); }
