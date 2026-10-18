#include "PluginEditor.h"

using namespace juce;

//==============================================================================
GlowLookAndFeel::GlowLookAndFeel()
{
    setColour (Slider::textBoxTextColourId, textDim);
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

// Black skirted knob: tick dots (and a printed 0-10 scale on the big ones), a knurled skirt,
// a glossy cap and a cream pointer line.
void GlowLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                        float a0, float a1, Slider& s)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float size = jmin (bounds.getWidth(), bounds.getHeight());
    const bool nums = (bool) s.getProperties().getWithDefault ("numbers", false);
    const float kd = size * (nums ? 0.66f : 0.80f);   // skirt diameter
    const float kr = kd * 0.5f;
    const auto c = bounds.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    const float alpha = s.isEnabled() ? 1.0f : 0.4f;

    // Copper progress arc and precise graduations share the existing control geometry.
    const float rt = kr + jmax (4.0f, kd * 0.08f);
    const float dotR = jmax (1.1f, kd * 0.012f);
    g.setColour (textDim.withAlpha (0.65f * alpha));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (float) i / 10.0f * (a1 - a0);
        g.fillEllipse (c.x + rt * std::sin (a) - dotR, c.y - rt * std::cos (a) - dotR, dotR * 2.0f, dotR * 2.0f);
    }
    // bipolar controls (gain, envelope amount, output trim) grow the arc from 0, not from the minimum
    float arcFrom = a0;
    if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
        arcFrom = a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0);
    Path progress;
    progress.addCentredArc (c.x, c.y, rt, rt, 0.0f, jmin (arcFrom, ang), jmax (arcFrom, ang), true);
    g.setColour (amber.withAlpha (0.9f * alpha));
    g.strokePath (progress, PathStrokeType (jmax (1.5f, size * 0.012f), PathStrokeType::curved, PathStrokeType::rounded));
    if (nums)
    {
        const float rn = kr + jmax (11.0f, kd * 0.2f);
        const float fs = jmax (9.0f, kd * 0.075f);
        g.setFont (FontOptions (fs, Font::bold));
        g.setColour (textDim.withAlpha (alpha));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = a0 + (float) i / 10.0f * (a1 - a0);
            g.drawText (String (i), Rectangle<float> (c.x + rn * std::sin (a) - 10.0f, c.y - rn * std::cos (a) - fs * 0.55f, 20.0f, fs * 1.1f),
                        Justification::centred);
        }
    }

    // shadow, skirt with knurling
    g.setColour (Colours::black.withAlpha (0.55f * alpha));
    g.fillEllipse (c.x - kr, c.y - kr + kd * 0.04f, kd, kd);
    g.setGradientFill (ColourGradient (Colour (0xff414851), c.x - kr * 0.4f, c.y - kr * 0.5f, Colour (0xff11161b), c.x + kr * 0.5f, c.y + kr, true));
    g.fillEllipse (c.x - kr, c.y - kr, kd, kd);
    const int ridges = jlimit (24, 72, (int) (kd * 0.6f));
    for (int i = 0; i < ridges; ++i)
    {
        const float a = (float) i / (float) ridges * MathConstants<float>::twoPi;
        const float sn = std::sin (a), cs = std::cos (a);
        g.setColour ((i % 2 == 0 ? Colours::black.withAlpha (0.45f) : Colours::white.withAlpha (0.06f)).withMultipliedAlpha (alpha));
        g.drawLine (c.x + sn * kr * 0.78f, c.y - cs * kr * 0.78f, c.x + sn * kr * 0.99f, c.y - cs * kr * 0.99f, jmax (1.0f, kd * 0.012f));
    }
    // cap
    const float cr = kr * 0.72f;
    g.setGradientFill (ColourGradient (Colour (0xff626b73).withMultipliedAlpha (alpha), c.x - cr * 0.35f, c.y - cr * 0.55f,
                                       Colour (0xff171b20), c.x + cr * 0.4f, c.y + cr, true));
    g.fillEllipse (c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f);
    g.setColour (Colours::white.withAlpha (0.12f * alpha));
    g.drawEllipse (c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f, 1.0f);
    // pointer
    Path p;
    const float pw = jmax (2.0f, cr * 0.09f);
    p.addRoundedRectangle (-pw * 0.5f, -cr * 0.95f, pw, cr * 0.55f, pw * 0.5f);
    g.setColour (text.withAlpha (alpha));
    g.fillPath (p, AffineTransform::rotation (ang).translated (c.x, c.y));
}

void GlowLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        Slider::SliderStyle style, Slider& s)
{
    if (style != Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    // console-style fader: dark slot with an amber fill below a silver cap
    const float cx = (float) x + (float) w * 0.5f;
    Rectangle<float> groove (cx - 2.5f, (float) y, 5.0f, (float) h);
    g.setColour (Colour (0xff0e1216));
    g.fillRoundedRectangle (groove, 2.5f);
    g.setGradientFill (ColourGradient (amber, 0.0f, pos, Colour (0xff965536), 0.0f, (float) (y + h), false));
    g.fillRoundedRectangle (groove.withTop (pos), 2.5f);

    Rectangle<float> cap (cx - 9.0f, pos - 14.0f, 18.0f, 28.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);
    g.setGradientFill (ColourGradient (Colour (0xffd9dee0), cap.getX(), cap.getY(), Colour (0xff78838b), cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (Colour (0xff333b42));
    g.fillRect (cap.getX() + 2.0f, pos - 1.0f, cap.getWidth() - 4.0f, 2.0f);
}

Label* GlowLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (FontOptions (11.0f));
    l->setJustificationType (Justification::centred);
    l->setColour (Label::textColourId, textDim);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::outlineWhenEditingColourId, amber);
    l->setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    l->setColour (TextEditor::focusedOutlineColourId, amber);
    return l;
}

// Buttons: dark well; when on, a warm lit face with an amber lamp (Mute/Solo keep their own colours)
void GlowLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    if (b.isColourSpecified (TextButton::buttonOnColourId))
    {
        const auto onCol = b.findColour (TextButton::buttonOnColourId);
        g.setColour (on ? onCol : well.brighter (over ? 0.15f : 0.0f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? onCol : edge);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        return;
    }
    g.setColour (on ? lampBg.brighter (over ? 0.15f : 0.0f) : well.brighter (over ? 0.15f : 0.0f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? amberDim : edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void GlowLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    auto r = b.getLocalBounds().toFloat();
    if (b.isColourSpecified (TextButton::buttonOnColourId))
    {
        g.setColour (on ? bg : textDim);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (b.getButtonText(), r, Justification::centred);
        return;
    }
    const bool lamp = (bool) b.getProperties().getWithDefault ("lamp", false);
    g.setFont (FontOptions (jmin (12.0f, r.getHeight() * 0.5f), Font::bold).withKerningFactor (0.12f));
    const float tw = GlyphArrangement::getStringWidth (g.getCurrentFont(), b.getButtonText());
    float tx = r.getCentreX() - tw * 0.5f;
    if (lamp)
    {
        tx += 6.0f;
        drawLamp (g, { tx - 9.0f, r.getCentreY() }, 3.0f, on);
    }
    g.setColour (on ? amber : textDim);
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
    g.setColour (GlowLookAndFeel::well.brighter (over ? 0.15f : 0.0f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (GlowLookAndFeel::edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
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
    for (auto* k : { &drive, &even, &odd }) k->numbers();
    for (auto* k : { &drive, &even, &odd, &density }) k->popupValue();
    drive.tip ("DRIVE: how hard the tubes are pushed, for more of both even and odd harmonics. The glow shows how hard "
               "they're working on the incoming audio.");
    even.tip ("EVEN: 2nd/4th harmonics (warmth, thickness). 50% = as voiced, 0% = none, 100% = 3x.");
    odd.tip ("ODD: 3rd/5th harmonics (edge, grit, clipping). 50% = as voiced; the level is matched for you.");
    density.tip ("DENSITY: how hard the first tube stage is hit. More = lopsided bloom and 'breathing' after loud hits.");

    // ---- OUTPUT + envelope
    output.setup (*this, s, "output", "LEVEL", 0.0);
    output.popupValue();
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
    st.label.setFont (FontOptions (15.0f, Font::bold).withKerningFactor (0.2f));
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
        if (std::abs (shown - lastGlow) > 0.004f) { lastGlow = shown; repaint (glowArea); repaint (tubeWindow.expanded (12)); }
    }
}

void GlowEditor::paint (Graphics& g)
{
    using L = GlowLookAndFeel;
    g.fillAll (L::well);
    const auto face = getLocalBounds().toFloat().reduced (1.0f);
    g.setGradientFill (ColourGradient (Colour (0xff252a30), 0.0f, 0.0f,
                                      Colour (0xff111519), 0.0f, (float) getHeight(), false));
    g.fillRoundedRectangle (face, 12.0f);
    g.setColour (L::edge);
    g.drawRoundedRectangle (face, 12.0f, 1.0f);
    // Restrained divider separates navigation from the signal path.
    g.setColour (L::edge.withAlpha (0.9f));
    g.fillRect (24, 57, getWidth() - 48, 1);

    // header: pilot lamp + wordmark, company on the right
    L::drawLamp (g, { 34.0f, 31.0f }, 5.0f, true);
    g.setColour (L::text);
    g.setFont (FontOptions (30.0f, Font::bold).withKerningFactor (0.15f));
    g.drawText ("GLOW", 48, 12, 160, 38, Justification::centredLeft);
    g.setColour (L::amber);
    g.fillRoundedRectangle (48.0f, 49.0f, 72.0f, 2.0f, 1.0f);
    g.setColour (L::text);
    g.setFont (FontOptions (15.0f, Font::bold).withKerningFactor (0.22f));
    g.drawText ("NORKA DSP", getWidth() - 228, 14, 200, 18, Justification::centredRight);
    g.setColour (L::textDim);
    g.setFont (FontOptions (10.5f).withKerningFactor (0.3f));
    g.drawText ("TUBE SATURATOR", getWidth() - 228, 32, 200, 14, Justification::centredRight);

    auto panelBox = [&g] (Rectangle<int> r)
    {
        const auto box = r.toFloat();
        g.setGradientFill (ColourGradient (GlowLookAndFeel::panel, box.getX(), box.getY(),
                                          GlowLookAndFeel::well, box.getRight(), box.getBottom(), false));
        g.fillRoundedRectangle (box, 10.0f);
        g.setColour (GlowLookAndFeel::edge);
        g.drawRoundedRectangle (box.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (GlowLookAndFeel::amber.withAlpha (0.55f));
        g.fillRoundedRectangle (box.getX() + 14.0f, box.getY(), 36.0f, 2.0f, 1.0f);
    };
    auto caption = [&g] (const String& t, Rectangle<int> r, Justification j = Justification::centredLeft, Colour c = L::textDim)
    {
        g.setColour (c);
        g.setFont (FontOptions (11.5f, Font::bold).withKerningFactor (0.28f));
        g.drawText (t, r, j);
    };
    // Functional groups match the signal flow without changing component ownership.
    const int eqTop = preEqLink.button.getY() - 12;
    panelBox ({ inputArea.getX(), inputArea.getY(), inputArea.getWidth(), eqTop - inputArea.getY() - 8 });
    panelBox ({ inputArea.getX(), eqTop, inputArea.getWidth(), inputArea.getBottom() - eqTop });
    panelBox ({ tubeArea.getX(), tubeArea.getY(), tubeArea.getWidth(), 70 });   // ends above the knob scales
    panelBox ({ outputArea.getX(), outputArea.getY(), outputArea.getWidth(),
                autoReadout.getBottom() - outputArea.getY() + 18 });
    const int envTop = envAmount.slider.getY() - 25;
    const int envBottom = jmin (outputArea.getBottom(), envAmount.label.getBottom() + 14);
    panelBox ({ outputArea.getX(), envTop, outputArea.getWidth(), envBottom - envTop });
    panelBox (mixArea);

    // INPUT captions, divider, cheat sheet
    caption ("INPUT", { inputArea.getX() + 12, inputArea.getY() + 8, 120, 14 });
    caption ("PRE-SAT EQ", { inputArea.getX() + 12, preEqLink.button.getY(), 120, preEqLink.button.getHeight() }, Justification::centredLeft, L::text.withAlpha (0.85f));
    {
        const char* hints[6][2] { { "Drag", "freq + gain" }, { "Cmd drag", "Q" }, { "Opt drag", "freq only" },
                                  { "Shift hold", "solo" }, { "Dbl-click", "bypass" }, { "HP point", "the high-pass" } };
        auto r = cheatArea;
        const int colW = r.getWidth() / 2, rowH = r.getHeight() / 3;
        for (int i = 0; i < 6; ++i)
        {
            auto cell = Rectangle<int> (r.getX() + (i % 2) * colW, r.getY() + (i / 2) * rowH, colW, rowH);
            g.setFont (FontOptions (11.0f, Font::bold));
            g.setColour (L::text.withAlpha (0.85f));
            const String k (hints[i][0]);
            const int kw = (int) GlyphArrangement::getStringWidth (g.getCurrentFont(), k) + 5;
            g.drawText (k, cell.removeFromLeft (kw), Justification::centredLeft);
            g.setFont (FontOptions (11.0f));
            g.setColour (L::textDim.withAlpha (0.8f));
            g.drawText (hints[i][1], cell, Justification::centredLeft);
        }
    }

    // tube window: glows with how hard the tubes are working
    {
        const float d = jlimit (0.0f, 1.0f, lastGlow);
        auto w = tubeWindow.toFloat();
        const float rr = w.getHeight() * 0.5f;
        g.setColour (Colour (0xff0a0807));
        g.fillRoundedRectangle (w, rr);
        g.saveState();
        Path clip; clip.addRoundedRectangle (w, rr);
        g.reduceClipRegion (clip);
        const auto c = w.getCentre().translated (0.0f, w.getHeight() * 0.1f);
        ColourGradient fil (Colour (0xffffba68).withAlpha (0.25f + 0.7f * d), c.x, c.y,
                            Colour (0xfff08232).withAlpha (0.0f), c.x + w.getWidth() * (0.18f + 0.3f * d), c.y, true);
        g.setGradientFill (fil);
        g.fillRect (w);
        g.setColour (Colours::black.withAlpha (0.38f));
        for (float gx = w.getX() + 6.0f; gx < w.getRight(); gx += 13.0f) g.fillRect (gx, w.getY(), 2.0f, w.getHeight());
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), 0.0f, w.getY() + 2.0f, Colours::white.withAlpha (0.0f), 0.0f, w.getY() + 14.0f, false));
        g.fillRoundedRectangle (w.reduced (w.getWidth() * 0.08f, 2.0f).withHeight (12.0f), 6.0f);
        g.restoreState();
        g.setColour (Colour (0xff4a3d2e));
        g.drawRoundedRectangle (w, rr, 1.0f);
        g.setColour (L::textDim.withAlpha (0.75f));
        g.setFont (FontOptions (9.5f).withKerningFactor (0.24f));
        auto lr = tubeWindow.translated (0, tubeWindow.getHeight() + 3).withHeight (12).reduced (8, 0);
        g.drawText ("CLEAN", lr, Justification::centredLeft);
        g.drawText ("GLOW", lr, Justification::centred);
        g.drawText ("DRIVEN", lr, Justification::centredRight);
    }

    // Drive glow behind the knob
    {
        const float d = jlimit (0.0f, 1.0f, lastGlow);
        auto gc = glowArea.toFloat().getCentre();
        const float gr = (float) glowArea.getWidth() * 0.5f * (0.9f + 0.12f * d);
        ColourGradient grad (L::amber.withAlpha (0.06f + 0.6f * d), gc.x, gc.y, L::amber.withAlpha (0.0f), gc.x + gr, gc.y, true);
        g.setGradientFill (grad);
        g.fillEllipse (glowArea.toFloat());
    }

    // OUTPUT / ENVELOPE / MIX captions
    caption ("OUTPUT", { outputArea.getX(), outputArea.getY() + 8, outputArea.getWidth(), 14 }, Justification::centred);
    caption ("ENVELOPE", { outputArea.getX() - 6, envAmount.slider.getY() - 18, outputArea.getWidth() + 12, 14 }, Justification::centred);
    caption ("MIX", { mixArea.getX() + 12, mixArea.getY() + 8, 60, 14 });

    // fader scales
    g.setFont (FontOptions (9.0f));
    for (auto* st : { &dry, &wet })
    {
        const auto& f = st->fader;
        for (double db : { 6.0, 0.0, -6.0, -12.0, -24.0, -48.0 })
        {
            const float y = (float) f.getY() + (float) f.getPositionOfValue (db);
            g.setColour (L::textDim.withAlpha (std::abs (db) < 0.1 ? 0.95f : 0.55f));
            g.drawText (db > 0.0 ? "+" + String ((int) db) : String ((int) db), f.getX() - 22, (int) y - 6, 18, 12,
                        Justification::centredRight);
        }
    }

    // fader values under each strip
    g.setFont (FontOptions (11.0f));
    for (auto* st : { &dry, &wet })
    {
        const auto& f = st->fader;
        const double v = f.getValue();
        const String t = v <= f.getMinimum() + 0.01 ? String ("Off") : (v > 0.05 ? "+" : "") + String (v, 1) + " dB";
        g.setColour (L::textDim);
        g.drawText (t, f.getX() - 26, f.getBottom() + 2, f.getWidth() + 52, 14, Justification::centred);
    }

    // pinstripes along the bottom, version
    g.setColour (L::amber.withAlpha (0.7f));
    g.fillRect (28, getHeight() - 15, getWidth() - 56, 1);
    g.setColour (L::text.withAlpha (0.2f));
    g.fillRect (28, getHeight() - 11, getWidth() - 56, 1);
    g.setColour (L::textDim.withAlpha (0.6f));
    g.setFont (FontOptions (9.5f));
    g.drawText ("v" JucePlugin_VersionString, getWidth() - 140, getHeight() - 30, 112, 12, Justification::centredRight);
}

// geometry of the Drive knob as the look-and-feel draws it
static void driveGeometry (const Slider& sl, Point<float>& c, float& ringR)
{
    auto r = sl.getLocalBounds().toFloat();
    const float size = jmin (r.getWidth(), r.getHeight());
    c = r.getCentre();
    ringR = size * 0.66f * 0.5f + jmax (4.0f, size * 0.66f * 0.08f);   // the tick ring
}

// Envelope follower: a bright dot on the Drive ring at the Drive it's currently using.
// Drawn in modDot's own coordinates (it covers exactly the Drive knob).
void GlowEditor::paintModDot (Graphics& g)
{
    if (shownModDrive < 0.0f || panel != nullptr) return;
    auto& sl = drive.slider;
    Point<float> c; float rad;
    driveGeometry (sl, c, rad);
    const auto rp = sl.getRotaryParameters();
    const float a0 = rp.startAngleRadians, a1 = rp.endAngleRadians;
    const float pos = (float) sl.valueToProportionOfLength (jlimit (0.0, 100.0, (double) shownModDrive));
    const float ang = a0 + pos * (a1 - a0);
    const float knobAng = a0 + (float) sl.valueToProportionOfLength (sl.getValue()) * (a1 - a0);
    Path arc;   // the stretch the envelope is adding/removing
    rad += 6.0f;   // just outside the copper value arc, which sits on the tick ring
    arc.addCentredArc (c.x, c.y, rad, rad, 0, jmin (ang, knobAng), jmax (ang, knobAng), true);
    g.setColour (GlowLookAndFeel::amber.withAlpha (0.7f));
    g.strokePath (arc, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    const Point<float> pt (c.x + rad * std::sin (ang), c.y - rad * std::cos (ang));
    GlowLookAndFeel::drawLamp (g, pt, 4.0f, true);
}

// Discreet horizontal signal-flow hint between ENVELOPE and the Drive ring.
// Uses the existing editor-only geometry; no processor interaction or allocation beyond drawing.
void GlowEditor::paintLead (Graphics& g)
{
    if (panel != nullptr) return;

    Point<float> localCentre;
    float ringRadius = 0.0f;
    driveGeometry (drive.slider, localCentre, ringRadius);
    const auto driveCentre = localCentre + drive.slider.getPosition().toFloat();
    const auto envelope = envAmount.slider.getBounds().toFloat();

    // The right-hand intersection of the ring at the envelope knob's centre height.
    // A single y coordinate keeps the lead perfectly straight at every layout size.
    const float y = envelope.getCentreY();
    const float dy = y - driveCentre.y;
    if (std::abs (dy) >= ringRadius) return;
    const float endX = driveCentre.x + std::sqrt (ringRadius * ringRadius - dy * dy);
    const float startX = envelope.getX() - 5.0f;
    if (startX <= endX + 8.0f) return;

    Path line;
    line.startNewSubPath (endX, y);
    line.lineTo (startX, y);
    constexpr float dashLengths[] { 1.5f, 5.0f };
    Path dashes;
    PathStrokeType (1.0f).createDashedStroke (dashes, line, dashLengths, 2);

    const bool active = shownModDrive >= 0.0f;
    g.setColour (GlowLookAndFeel::amber.withAlpha (active ? 0.45f : 0.25f));
    g.fillPath (dashes);
    g.setColour (GlowLookAndFeel::amber.withAlpha (active ? 0.65f : 0.38f));
    g.fillEllipse (endX - 1.6f, y - 1.6f, 3.2f, 3.2f);
}

void GlowEditor::layoutStrip (Strip& st, Rectangle<int> r)
{
    st.label.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);
    auto icons = r.removeFromRight (50);
    icons.removeFromTop (4);
    // the icons stack in signal order; the swap button between them flips it
    const bool compFirst = st.compFirst != nullptr && st.compFirst->load() > 0.5f;
    st.shownCompFirst = compFirst;
    (compFirst ? st.compIcon : st.eqIcon).setBounds (icons.removeFromTop (44).reduced (2));
    st.swap.setBounds (icons.removeFromTop (20));
    (compFirst ? st.eqIcon : st.compIcon).setBounds (icons.removeFromTop (44).reduced (2));
    icons.removeFromTop (10);
    auto ms = icons.removeFromTop (22);
    st.mute.button.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (1, 0));
    st.solo.button.setBounds (ms.reduced (1, 0));
    auto meterCol = r.removeFromRight (8);
    r.removeFromRight (2);
    r.removeFromLeft (20);   // column for the dB scale
    r.removeFromBottom (16);   // value readout
    st.fader.setBounds (r);
    // meter spans exactly the fader's travel (+6 dB at the top, Off at the bottom)
    const int yTop = st.fader.getY() + (int) st.fader.getPositionOfValue (st.fader.getMaximum());
    const int yBot = st.fader.getY() + (int) st.fader.getPositionOfValue (st.fader.getMinimum());
    st.meter.setBounds (meterCol.getX(), yTop, meterCol.getWidth(), jmax (10, yBot - yTop));
}

void GlowEditor::resized()
{
    // header
    presetBar.setBounds (Rectangle<int> (0, 0, 400, 28).withCentre ({ getWidth() / 2 - 20, 31 }));
    abA.setBounds (presetBar.getRight() + 14, 18, 26, 26);
    abB.setBounds (abA.getRight() + 4, 18, 26, 26);
    undoButton.setBounds (abB.getRight() + 10, 18, 26, 26);
    redoButton.setBounds (undoButton.getRight() + 4, 18, 26, 26);

    // columns
    const int top = 68, h = getHeight() - top - 22;
    inputArea = { 24, top, 290, h };
    tubeArea = { inputArea.getRight() + 14, top, 372, h };
    outputArea = { tubeArea.getRight() + 14, top, 118, h };
    mixArea = { outputArea.getRight() + 14, top, getWidth() - 24 - (outputArea.getRight() + 14), h };
    overlayArea = { inputArea.getX(), top, outputArea.getRight() - inputArea.getX(), h };

    {   // INPUT: the high-pass up top, then the pre-saturation EQ
        auto r = inputArea.reduced (12, 0);
        hpf.placeBelow ({ r.getX() - 2, top + 26, 96, 112 });
        const int bx = hpf.slider.getRight() + 10;
        keepLows.button.setBounds (bx, top + 46, 124, 28);
        slope12.setBounds (bx, top + 84, 58, 22);
        slope24.setBounds (bx + 62, top + 84, 58, 22);
        const int eqTop = hpf.label.getBottom() + 28;
        preEqOn.button.setBounds (r.getRight() - 44, eqTop, 44, 22);
        preEqLink.button.setBounds (preEqOn.button.getX() - 58, eqTop, 54, 22);
        preCurve.setBounds (r.getX(), eqTop + 30, r.getWidth(), 176);
        cheatArea = { r.getX() + 2, preCurve.getBottom() + 8, r.getWidth() - 2, 60 };
    }
    {   // TUBES: tube window, Even / Density / Odd, then Drive
        const int cx = tubeArea.getCentreX();
        tubeWindow = { cx - 125, top + 8, 250, 40 };
        const int rowY = top + 78;
        even.placeBelow ({ cx - 149, rowY, 93, 108 });
        density.placeBelow ({ cx - 38, rowY + 10, 76, 91 });
        odd.placeBelow ({ cx + 56, rowY, 93, 108 });
        drive.placeBelow ({ cx - 120, rowY + 140, 240, 240 }, 20);
        glowArea = drive.slider.getBounds().expanded (30);
        modDot.setBounds (drive.slider.getBounds());
    }
    {   // OUTPUT + envelope
        const int cx = outputArea.getCentreX();
        output.placeBelow ({ cx - 45, top + 26, 91, 106 });
        autoGain.button.setBounds (cx - 38, output.label.getBottom() + 12, 76, 26);
        autoReadout.setBounds (outputArea.getX(), autoGain.button.getBottom() + 3, outputArea.getWidth(), 14);
        const int ey = drive.slider.getBounds().getCentreY() - 10;   // down and to the right of Drive
        envAmount.placeBelow ({ cx - 54, ey, 54, 70 });
        envSpeed.placeBelow ({ cx, ey, 54, 70 });
    }
    {   // MIX: Dry and Wet strips
        auto r = mixArea.withTrimmedTop (26).reduced (10, 10);
        layoutStrip (dry, r.removeFromLeft (r.getWidth() / 2).reduced (3, 0));
        layoutStrip (wet, r.reduced (3, 0));
    }
    lead.setBounds (getLocalBounds());
    if (panel != nullptr) panel->setBounds (overlayArea);
}

AudioProcessorEditor* GlowProcessor::createEditor() { return new GlowEditor (*this); }
