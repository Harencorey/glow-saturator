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
    if ((bool) b.getProperties().getWithDefault ("tile", false))   // square: lamp on top, words stacked
    {
        drawLamp (g, { r.getCentreX(), r.getY() + 20.0f }, 4.0f, on);
        g.setColour (on ? amber : text.withAlpha (0.88f));
        g.setFont (FontOptions (12.0f).withKerningFactor (0.2f));
        const auto words = StringArray::fromTokens (b.getButtonText(), " ", "");
        float y = r.getCentreY() - 2.0f;
        for (auto& w : words) { g.drawText (w, Rectangle<float> (r.getX(), y, r.getWidth(), 15.0f), Justification::centred); y += 16.0f; }
        return;
    }
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
             "Expand (top right) for the pre-saturation EQ.");
    hpfP = s.getRawParameterValue ("hpf");
    body.addAndMakeVisible (inputExpand);
    inputExpand.setTooltip ("Show / hide the pre-saturation EQ (the window gets wider)");
    inputExpand.onClick = [this] { setExpanded (! inputOpen); };
    keepLows.setup (body, s, "keepLows", "KEEP LOWS");
    keepLows.button.getProperties().set ("lamp", true);
    keepLows.button.setTooltip ("KEEP LOWS: everything the high-pass removes skips the tubes and comes back clean, so the low "
                                "end stays full and solid while only what's above it saturates. Off = a plain cut.");
    for (auto* b : { &slope6, &slope12, &slope24, &slope48 })
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
    mix.tip ("MIX: dry/wet. 100% = all saturated, 0% = all clean (both paths stay phase-locked, so any blend is solid).");
    mixP = s.getRawParameterValue ("mix");
    // turning a knob that changes the level makes Auto Gain re-match quickly (not host automation)
    for (auto* k : { &input, &drive, &density, &even, &odd, &mix })
        k->slider.onValueChange = [this, sl = &k->slider] { if (sl->isMouseButtonDown()) proc.nudgeAutoGain(); };
    setupPath (dryMods, "dry");
    setupPath (wetMods, "wet");
    body.addAndMakeVisible (inMeter);
    body.addAndMakeVisible (outMeter);
    inMeter.setTooltip ("Input level (after the Input knob: what hits the tubes), L and R. The number is the highest peak; click to reset.");
    outMeter.setTooltip ("Output level, L and R. The number is the highest peak (red = over 0 dBFS); click to reset.");

    // ---- bottom strip: Input, Output, Auto
    input.setup (body, s, "inputGain", "INPUT", 0.0, 11.0f);
    output.setup (body, s, "output", "OUTPUT", 0.0, 11.0f);
    for (auto* k : { &input, &output }) k->slider.setTextBoxStyle (Slider::TextBoxBelow, false, 60, 13);
    input.tip ("INPUT: level into Glow (before the high-pass and the tubes). Push it to drive the tubes harder; Auto Gain "
               "keeps the output level steady.");
    output.tip ("OUTPUT: final level trim, after everything (and after Auto)");
    // typing a value into the Input box counts as a hand turn too (the text-to-value function
    // only runs for typed entry, never for host automation)
    if (auto prev = input.slider.valueFromTextFunction)
        input.slider.valueFromTextFunction = [this, prev] (const String& t) { proc.nudgeAutoGain(); return prev (t); };
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
    uiScale = proc.uiScale;
    applyLayout();
    startTimerHz (30);
}

GlowEditor::~GlowEditor()
{
    PopupMenu::dismissAllActiveMenus();   // the size menu, if it's open
    stopTimer();
    panel.reset();
    setLookAndFeel (nullptr);
}

void GlowEditor::setupPath (PathMods& pm, const String& prefix)
{
    auto& s = proc.apvts;
    pm.prefix = prefix;
    const String Name = prefix == "dry" ? "Dry" : "Wet";
    for (auto* m : { &pm.eq, &pm.comp })
    {
        body.addAndMakeVisible (m);
        const bool isEq = m == &pm.eq;
        m->setTooltip (Name + (isEq ? " EQ" : " compressor") + (prefix == "dry" ? " (on the clean path)" : " (on the saturated path)")
                       + ": click the lamp to switch it on or off, click the rest to open it. Double-click also switches it.");
        m->onSingleClick = [this, isEq, prefix] { togglePanel (prefix, isEq ? ModuleIcon::Kind::eq : ModuleIcon::Kind::comp); };
        m->onDoubleClick = [this, isEq, prefix] { toggleParam (prefix + (isEq ? "EqOn" : "CompOn")); };
    }
    pm.eqOn = s.getRawParameterValue (prefix + "EqOn");
    pm.compOn = s.getRawParameterValue (prefix + "CompOn");
    pm.compFirst = s.getRawParameterValue (prefix + "CompFirst");
    body.addAndMakeVisible (pm.swap);
    pm.swapAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, prefix + "CompFirst", pm.swap);
    pm.swap.setTooltip ("Swap the " + Name + " order: EQ then Comp, or Comp then EQ (top runs first)");
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

void GlowEditor::setExpanded (bool in)
{
    inputOpen = proc.uiInputOpen = in;
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
    if (proc.uiInputOpen != inputOpen || std::abs (proc.uiScale - uiScale) > 0.001f)
    {
        inputOpen = proc.uiInputOpen; uiScale = proc.uiScale;
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
    bool anyModule = false;
    for (auto* pm : { &dryMods, &wetMods })
    {
        const bool mine = panel != nullptr && panelPrefix == pm->prefix;
        pm->eq.setState (pm->eqOn->load() > 0.5f, mine && panelKind == ModuleIcon::Kind::eq);
        pm->comp.setState (pm->compOn->load() > 0.5f, mine && panelKind == ModuleIcon::Kind::comp);
        anyModule = anyModule || pm->eqOn->load() > 0.5f || pm->compOn->load() > 0.5f;
    }
    inMeter.push (proc.takeInPeak (0), proc.takeInPeak (1));
    outMeter.push (proc.takeOutPeak (0), proc.takeOutPeak (1));
    if (panel != nullptr) panel->refresh();
    presetBar.refresh();

    // section lamps (HPF in, Mix/master doing something, mixer modules in)
    {
        const int lamps = (hpfP->load() >= GlowDSP::kHpfMinHz - 0.5f ? 1 : 0)
                        | ((mixP->load() < 99.95f || anyModule) ? 2 : 0);
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

    // master chain order (icons stack top = first)
    bool relayout = false;
    for (auto* pm : { &dryMods, &wetMods })
        if ((pm->compFirst->load() > 0.5f) != pm->shownCompFirst) relayout = true;
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
    // one height in every view (more room than before); expanding a section only adds width
    const int w = kMargin + (inputOpen ? kInputOpenW : kInputW) + kGap + kCentreW + kGap + kMixW + kMargin;
    const int h = kMainH + (panel != nullptr ? drawerH() : 0);
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
        const auto sl = slope6.getBounds().getUnion (slope48.getBounds());
        g.setColour (L::textDim);
        g.setFont (FontOptions (10.0f).withKerningFactor (0.2f));
        g.drawText ("dB/OCT", sl.getX() - 10, sl.getBottom() + 6, sl.getWidth() + 20, 14, Justification::centred);
    }

    // ---- centre: saturation
    card (centreCard);
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
    title ("MIX", mixCard.getX() + 18, mixCard.getY() + 22, (lamps & 2) != 0);
    for (auto* pm : { &dryMods, &wetMods })   // which side is which
    {
        const auto top = (pm->shownCompFirst ? pm->comp : pm->eq).getBounds();
        g.setColour (L::text.withAlpha (0.9f));
        g.setFont (FontOptions (12.5f).withKerningFactor (0.35f));
        g.drawText (pm == &dryMods ? "DRY" : "WET", top.getX(), top.getY() - 26, top.getWidth(), 16, Justification::centred);
    }
    {
        const auto b = mix.slider.getBounds().toFloat();
        const float S = jmin (b.getWidth(), b.getHeight());
        small ("0%", b.getCentreX() - S * 0.40f, b.getCentreY() + S * 0.42f);
        small ("100%", b.getCentreX() + S * 0.40f, b.getCentreY() + S * 0.42f);
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

void GlowEditor::applyLayout()
{
    mainH = kMainH;
    const auto ds = designSize();
    const int W = ds.getWidth();
    const int top = kTop;

    // header: preset bar, A/B, undo/redo, roughly centred
    const int hx = W / 2 - 290;
    presetBar.setBounds (hx, 10, 440, 28);
    abA.setBounds (hx + 454, 10, 30, 28);
    abB.setBounds (hx + 490, 10, 30, 28);
    undoButton.setBounds (hx + 532, 10, 30, 28);
    redoButton.setBounds (hx + 568, 10, 30, 28);

    leftCard = { kMargin, top, inputOpen ? kInputOpenW : kInputW, kCardH };
    centreCard = { leftCard.getRight() + kGap, top, kCentreW, kCardH };
    mixCard = { centreCard.getRight() + kGap, top, kMixW, kCardH };

    auto knobAt = [] (Knob& k, Point<int> c, int size, int valueH, Rectangle<int> label)
    {
        k.slider.setBounds (c.x - size / 2, c.y - size / 2, size, size + valueH);
        k.label.setBounds (label);
    };
    auto rel = [] (Rectangle<int> card, int dx, int dy) { return Point<int> (card.getX() + dx, card.getY() + dy); };
    auto labelAt = [] (Point<int> c, int w = 110, int h = 14) { return Rectangle<int> (c.x - w / 2, c.y - h / 2, w, h); };

    // ---- INPUT
    for (Component* c : { (Component*) &preEqLink.button, (Component*) &preEqLamp, (Component*) &preCurve, (Component*) &preFreq.slider, (Component*) &preFreq.label,
                          (Component*) &preQ.slider, (Component*) &preQ.label })
        c->setVisible (inputOpen);
    inputExpand.expanded = inputOpen;
    if (inputOpen)
    {
        const auto L = leftCard;
        preEqLamp.setBounds (L.getX() + 10, L.getY() + 9, 20, 20);
        inputExpand.setBounds (L.getRight() - 38, L.getY() + 8, 26, 24);
        preEqLink.button.setBounds (L.getRight() - 112, L.getY() + 8, 64, 24);
        preCurve.setBounds (L.getX() + 46, L.getY() + 44, L.getWidth() - 68, 132);
        knobAt (hpf, rel (L, 86, 252), 84, 14, labelAt (rel (L, 86, 205)));
        keepLows.button.setBounds (L.getX() + 162, L.getY() + 224, 124, 28);
        slope12.setBounds (L.getX() + 162, L.getY() + 262, 59, 24);
        slope24.setBounds (L.getX() + 227, L.getY() + 262, 59, 24);
        knobAt (preFreq, rel (L, 370, 252), 84, 14, labelAt (rel (L, 370, 205)));
        knobAt (preQ, rel (L, 476, 252), 84, 14, labelAt (rel (L, 476, 205)));
    }
    else
    {
        const auto L = leftCard;
        inputExpand.setBounds (L.getRight() - 40, L.getY() + 12, 26, 24);
        // Keep Lows square | HPF knob | 2x2 slope squares, all on the knob line
        knobAt (hpf, rel (L, L.getWidth() / 2, kLine + 6), 130, 16, labelAt ({ 0, -100 }));
        constexpr int sq = 84, cell = 39;
        keepLows.button.setBounds (L.getX() + 22, L.getY() + kLine - sq / 2, sq, sq);
        const int qx = L.getRight() - 22 - sq, qy = L.getY() + kLine - sq / 2;
        slope6.setBounds (qx, qy, cell, cell);
        slope12.setBounds (qx + sq - cell, qy, cell, cell);
        slope24.setBounds (qx, qy + sq - cell, cell, cell);
        slope48.setBounds (qx + sq - cell, qy + sq - cell, cell, cell);
    }
    hpf.label.setVisible (inputOpen);
    keepLows.button.getProperties().set ("tile", ! inputOpen);
    slope6.setVisible (! inputOpen); slope48.setVisible (! inputOpen);

    // ---- SATURATION (identical in every view)
    {
        const auto C = centreCard;
        const int cx = C.getWidth() / 2;
        tubeWindow = { C.getX() + cx - 160, C.getY() + 12, 320, 28 };
        // Drive in the middle; Density small above it; Even / Odd on the upper flanks, the
        // envelope pair on the lower flanks
        knobAt (density, rel (C, cx, 68), 60, 0, labelAt (rel (C, cx, 103)));
        knobAt (even, rel (C, cx - 152, 112), 86, 0, labelAt (rel (C, cx - 152, 162)));
        knobAt (odd, rel (C, cx + 152, 112), 86, 0, labelAt (rel (C, cx + 152, 162)));
        knobAt (drive, rel (C, cx, kLine), 164, 0, labelAt (rel (C, cx, kLine + 90), 80, 18));   // ~8 % bigger than HPF / Mix
        knobAt (envAmount, rel (C, cx - 152, 238), 76, 0, labelAt (rel (C, cx - 152, 280)));
        knobAt (envSpeed, rel (C, cx + 152, 238), 76, 0, labelAt (rel (C, cx + 152, 280)));
        glowArea = drive.slider.getBounds().expanded (8);
        modDot.setBounds (drive.slider.getBounds());
    }

    // ---- MIX: Dry EQ / COMP | the Mix knob | Wet EQ / COMP
    {
        const auto M = mixCard;
        const int cx = M.getWidth() / 2;
        // square-ish module tiles either side of the knob
        constexpr int tileW = 96, tileH = 78, y1 = kLine - tileH - 15, y2 = kLine + 15;
        knobAt (mix, rel (M, cx, kLine), 122, 0, labelAt ({ 0, -100 }));
        mix.label.setVisible (false);
        auto column = [&] (PathMods& pm, int x)
        {
            const bool cf = pm.compFirst->load() > 0.5f;
            pm.shownCompFirst = cf;
            (cf ? pm.comp : pm.eq).setBounds (M.getX() + x, M.getY() + y1, tileW, tileH);
            (cf ? pm.eq : pm.comp).setBounds (M.getX() + x, M.getY() + y2, tileW, tileH);
            pm.swap.setBounds (M.getX() + x + tileW / 2 - 9, M.getY() + y1 + tileH + 4, 18, y2 - y1 - tileH - 8);
        };
        column (dryMods, 16);
        column (wetMods, M.getWidth() - 16 - tileW);
    }

    // ---- bottom strip (right): INPUT knob | OUTPUT knob, AUTO
    {
        const int R = mixCard.getRight(), cy = top + kCardH + 34;
        bottomArea = { R - 316, cy - 17, 320, 34 };
        knobAt (input, { R - 240, cy - 4 }, 44, 13, { R - 316, cy - 11, 48, 14 });
        knobAt (output, { R - 156, cy - 4 }, 44, 13, { R - 130, cy - 11, 56, 14 });
        autoGain.button.setBounds (R - 70, cy - 14, 74, 28);
        const int mx = leftCard.getX() + 30, mw = jmin (470, bottomArea.getX() - 40 - mx);
        inMeter.setBounds (mx, cy - 24, mw, 12);
        outMeter.setBounds (mx, cy - 4, mw, 25);
    }

    // ---- the EQ / COMP drawer under the main controls
    drawerArea = { 12, mainH, W - 24, drawerH() - 12 };
    if (panel != nullptr) panel->setBounds (drawerArea);

    lead.setBounds (0, 0, W, mainH);
    body.setBounds (ds);
    // the chosen interface size, made smaller if the window wouldn't fit on this screen
    float scale = uiScale;
    {
        auto& displays = Desktop::getInstance().getDisplays();
        const auto* d = isShowing() ? displays.getDisplayForRect (getScreenBounds()) : displays.getPrimaryDisplay();
        if (d != nullptr)
            scale = jmin (scale, (float) d->userBounds.getWidth() * 0.96f / (float) ds.getWidth(),
                                 (float) d->userBounds.getHeight() * 0.90f / (float) ds.getHeight());
        scale = jmax (0.5f, scale);
    }
    body.setTransform (AffineTransform::scale (scale));
    const int w = roundToInt ((float) ds.getWidth() * scale), h = roundToInt ((float) ds.getHeight() * scale);
    if (getWidth() != w || getHeight() != h) setSize (w, h);
    body.repaint();
}

void GlowEditor::resized()
{
    // the host decides the size (it follows applyLayout); keep the scaled body filling it
}

AudioProcessorEditor* GlowProcessor::createEditor() { return new GlowEditor (*this); }
