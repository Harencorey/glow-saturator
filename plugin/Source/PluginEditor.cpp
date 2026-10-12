#include "PluginEditor.h"

using namespace juce;

//==============================================================================
GlowLookAndFeel::GlowLookAndFeel()
{
    setColour (Slider::textBoxTextColourId, text);
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
    const float track = jmax (2.5f, size * 0.055f);
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

    const float kr = rad - track * 2.6f;
    g.setGradientFill (ColourGradient (Colour (0xff4a3f36), c.x, c.y - kr, Colour (0xff1e1915), c.x, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.2f);

    Path p;
    const float pw = jmax (2.0f, kr * 0.09f);
    p.addRoundedRectangle (-pw * 0.5f, -kr * 0.92f, pw, kr * 0.42f, pw * 0.5f);
    g.setColour (on ? text : textDim.withAlpha (0.5f));
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
    // console-style fader: groove + cap
    const float cx = (float) x + (float) w * 0.5f;
    Rectangle<float> groove (cx - 2.5f, (float) y, 5.0f, (float) h);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (groove, 2.5f);
    g.setColour (amber.withAlpha (0.5f));
    g.fillRoundedRectangle (groove.withTop (pos), 2.5f);

    // unity mark
    const float unityY = (float) y + (float) h * (1.0f - (float) s.valueToProportionOfLength (0.0));
    g.setColour (textDim.withAlpha (0.7f));
    g.drawHorizontalLine ((int) unityY, cx + 5.0f, cx + 12.0f);

    Rectangle<float> cap (cx - 13.0f, pos - 8.0f, 26.0f, 16.0f);
    g.setGradientFill (ColourGradient (Colour (0xff5a4d42), cap.getX(), cap.getY(), Colour (0xff2a231e), cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (cap, 3.0f, 1.0f);
    g.setColour (text);
    g.drawHorizontalLine ((int) pos, cap.getX() + 3.0f, cap.getRight() - 3.0f);
}

Label* GlowLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (FontOptions (12.0f));
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
    // buttons can set their own "on" colour (Mute red, Solo yellow); amber otherwise
    const auto onCol = b.isColourSpecified (TextButton::buttonOnColourId) ? b.findColour (TextButton::buttonOnColourId) : amber;
    g.setColour (on ? onCol : Colours::black.withAlpha (over ? 0.35f : 0.25f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? onCol : textDim.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void GlowLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? bg : textDim);
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred);
}

//==============================================================================
GlowEditor::GlowEditor (GlowProcessor& p) : AudioProcessorEditor (p), proc (p), presetBar (p.presets)
{
    addAndMakeVisible (presetBar);
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;
    drive.setup (*this, s, "drive", "DRIVE", 25.0);
    even.setup (*this, s, "even", "EVEN", 50.0);
    odd.setup (*this, s, "odd", "ODD", 50.0);
    density.setup (*this, s, "density", "DENSITY", 50.0);
    hpf.setup (*this, s, "hpf", "HIGH-PASS", GlowDSP::kHpfOffHz);
    preFreq.setup (*this, s, "preShelfFreq", "SHELF FREQ", 5000.0);
    preGain.setup (*this, s, "preShelfGain", "SHELF GAIN", 0.0);
    postFreq.setup (*this, s, "postShelfFreq", "SHELF FREQ", 5000.0);
    postGain.setup (*this, s, "postShelfGain", "SHELF GAIN", 0.0);
    output.setup (*this, s, "output", "OUTPUT", 0.0);

    linkButton.setClickingTogglesState (true);
    linkButton.setTooltip ("Post shelf mirrors the pre shelf: highs hit the tubes harder or softer, overall balance stays put");
    addAndMakeVisible (linkButton);
    autoGain.setup (*this, s, "autoGain", "AUTO");
    autoGain.button.setTooltip ("Auto gain: keeps the output as loud as the input (slow, ~3 s), so Drive/EQ/comp moves "
                                "don't fool your ears. Output trims on top.");
    autoReadout.setJustificationType (Justification::centred);
    autoReadout.setFont (FontOptions (10.5f));
    autoReadout.setColour (Label::textColourId, GlowLookAndFeel::textDim);
    addAndMakeVisible (autoReadout);
    linkAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, "shelfLink", linkButton);

    setupStrip (dry, "dry", "DRY");
    setupStrip (wet, "wet", "WET");

    sendLookAndFeelChange(); // re-skin value boxes created before the knobs were parented
    setSize (1140, 460);
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
    st.fader.setTextBoxStyle (Slider::TextBoxBelow, false, 64, 18);
    st.fader.setDoubleClickReturnValue (true, prefix == "dry" ? ChannelStrip::kOffDb : 0.0);
    st.label.setText (name, dontSendNotification);
    st.label.setJustificationType (Justification::centred);
    st.label.setFont (FontOptions (12.0f, Font::bold));
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
    st.eqIcon.setTooltip (name + " path EQ");
    st.compIcon.setTooltip (name + " path compressor");
    st.eqIcon.onClick = [this, prefix] { togglePanel (prefix, ModuleIcon::Kind::eq); };
    st.compIcon.onClick = [this, prefix] { togglePanel (prefix, ModuleIcon::Kind::comp); };
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
                     { return prefix == "dry" ? proc.dryGainReductionDb() : proc.wetGainReductionDb(); });
    panelPrefix = prefix;
    panelKind = kind;
    panel->onClose = [this] { MessageManager::callAsync ([sp = Component::SafePointer<GlowEditor> (this)]
                                                         { if (sp != nullptr) sp->closePanel(); }); };
    addAndMakeVisible (*panel);
    panel->sendLookAndFeelChange();
    panel->setBounds (overlayArea);
    panel->refresh();
}

void GlowEditor::closePanel()
{
    if (panel != nullptr) { removeChildComponent (panel.get()); panel.reset(); }
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
    const bool link = linkButton.getToggleState();
    postFreq.slider.setEnabled (! link);
    postGain.slider.setEnabled (! link);

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

    // Drive glow follows how hard the tubes are actually working (distortion vs the clean
    // signal, about -54 dB = clean to 0 dB = crushed), with meter-like ballistics:
    // jumps up on peaks, falls back over ~0.3 s. With no audio it rests at a faint glow
    // that just shows the knob setting.
    {
        const float ratio = proc.takeDrivenness();
        const float db = ratio > 0.0f ? 10.0f * std::log10 (ratio) : -100.0f;
        const float x = jlimit (0.0f, 1.0f, (db + 54.0f) / 52.0f);
        const float target = x * x;
        glowLevel = target > glowLevel ? glowLevel + 0.6f * (target - glowLevel) : glowLevel * 0.88f;
        const float shown = 0.12f * (float) drive.slider.getValue() / 100.0f + 0.88f * glowLevel;
        if (std::abs (shown - lastGlow) > 0.004f) { lastGlow = shown; repaint (glowArea); }
    }
}

void GlowEditor::paint (Graphics& g)
{
    g.fillAll (GlowLookAndFeel::bg);
    auto area = getLocalBounds().reduced (12);
    g.setColour (GlowLookAndFeel::panel);
    g.fillRoundedRectangle (area.toFloat(), 10.0f);

    auto header = area.removeFromTop (46).reduced (20, 0);
    g.setColour (GlowLookAndFeel::text);
    g.setFont (FontOptions (24.0f, Font::bold));
    g.drawText ("GLOW", header, Justification::centredLeft);
    g.setColour (GlowLookAndFeel::textDim);
    g.setFont (FontOptions (12.0f));
    g.drawText ("TUBE SATURATOR  //  NORKA DSP", header, Justification::centredRight);

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
    drawSection (mixArea, "MIX");

    // fader scales
    g.setFont (FontOptions (9.0f));
    for (auto* st : { &dry, &wet })
    {
        const auto& f = st->fader;
        for (double db : { 6.0, 0.0, -6.0, -12.0, -24.0, -48.0 })
        {
            const float y = (float) f.getY() + (float) f.getPositionOfValue (db);
            g.setColour (GlowLookAndFeel::textDim.withAlpha (std::abs (db) < 0.1 ? 0.95f : 0.6f));
            g.drawText (db > 0.0 ? "+" + String ((int) db) : String ((int) db), f.getX() - 20, (int) y - 6, 18, 12,
                        Justification::centredRight);
        }
    }

    const float d = jlimit (0.0f, 1.0f, lastGlow);   // set by the timer: knob + how driven the signal is
    auto gc = glowArea.toFloat().getCentre();
    const float gr = (float) glowArea.getWidth() * 0.5f * (0.9f + 0.12f * d);   // blooms a little when hot
    ColourGradient grad (GlowLookAndFeel::amber.withAlpha (0.06f + 0.74f * d), gc.x, gc.y,
                         GlowLookAndFeel::amber.withAlpha (0.0f), gc.x + gr, gc.y, true);
    g.setGradientFill (grad);
    g.fillEllipse (glowArea.toFloat());

    g.setColour (GlowLookAndFeel::textDim.withAlpha (0.7f));
    g.setFont (FontOptions (10.0f));
    g.drawText ("v" JucePlugin_VersionString, getLocalBounds().reduced (24, 16).removeFromBottom (14),
                Justification::bottomRight);
}

void GlowEditor::layoutStrip (Strip& st, Rectangle<int> r)
{
    st.label.setBounds (r.removeFromTop (18));
    auto icons = r.removeFromRight (46);
    icons.removeFromTop (4);
    // the icons stack in signal order; the swap button between them flips it
    const bool compFirst = st.compFirst != nullptr && st.compFirst->load() > 0.5f;
    st.shownCompFirst = compFirst;
    (compFirst ? st.compIcon : st.eqIcon).setBounds (icons.removeFromTop (40).reduced (2));
    st.swap.setBounds (icons.removeFromTop (16));
    (compFirst ? st.eqIcon : st.compIcon).setBounds (icons.removeFromTop (40).reduced (2));
    icons.removeFromTop (10);
    auto ms = icons.removeFromTop (22);
    st.mute.button.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (1, 0));
    st.solo.button.setBounds (ms.reduced (1, 0));
    auto meterCol = r.removeFromRight (12);
    r.removeFromLeft (16);   // column for the dB scale
    st.fader.setBounds (r);
    // meter spans exactly the fader's travel (+6 dB at the top, Off at the bottom)
    const int yTop = st.fader.getY() + (int) st.fader.getPositionOfValue (st.fader.getMaximum());
    const int yBot = st.fader.getY() + (int) st.fader.getPositionOfValue (st.fader.getMinimum());
    st.meter.setBounds (meterCol.getX(), yTop, meterCol.getWidth(), jmax (10, yBot - yTop));
}

void GlowEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    presetBar.setBounds (Rectangle<int> (0, 22, 400, 26).withCentre ({ getWidth() / 2, 35 }));
    area.removeFromTop (40);
    area.removeFromBottom (10);

    mixArea = area.removeFromRight (250);
    area.removeFromRight (10);
    overlayArea = area;                   // module panels open over Pre / Tubes / Post
    preArea = area.removeFromLeft (210);
    postArea = area.removeFromRight (190);
    tubeArea = area.reduced (12, 0);

    {   // PRE: high-pass on top, shelf freq + gain below
        auto r = preArea.withTrimmedTop (22).reduced (8, 4);
        auto top = r.removeFromTop (r.getHeight() / 2);
        hpf.place (top.withSizeKeepingCentre (110, top.getHeight()));
        preFreq.place (r.removeFromLeft (r.getWidth() / 2));
        preGain.place (r);
    }
    {   // POST: shelf freq + gain (+ link) on top, output below
        auto r = postArea.withTrimmedTop (22).reduced (8, 4);
        auto top = r.removeFromTop (r.getHeight() / 2);
        linkButton.setBounds (postArea.getRight() - 58, postArea.getY() + 3, 48, 16);
        postFreq.place (top.removeFromLeft (top.getWidth() / 2));
        postGain.place (top);
        output.place (r.withSizeKeepingCentre (100, r.getHeight()));
        // AUTO gain switch on the OUTPUT label's row (like LINK), what it's applying underneath
        autoGain.button.setBounds (postArea.getRight() - 58, r.getY(), 48, 16);
        autoReadout.setBounds (postArea.getRight() - 64, r.getY() + 18, 60, 14);
    }
    {   // TUBES: Drive big, Even / Density / Odd underneath
        auto r = tubeArea;
        auto bottom = r.removeFromBottom (128);
        const int dsz = jmin (r.getWidth(), r.getHeight());
        auto driveBox = r.withSizeKeepingCentre (dsz, r.getHeight());
        glowArea = driveBox.withSizeKeepingCentre (dsz + 30, dsz + 30).translated (0, 8);
        drive.place (driveBox);
        const int w = bottom.getWidth() / 3;
        even.place (bottom.removeFromLeft (w).reduced (6, 0));
        density.place (bottom.removeFromLeft (w).reduced (6, 0));
        odd.place (bottom.reduced (6, 0));
    }
    {   // MIX: Dry and Wet strips
        auto r = mixArea.withTrimmedTop (22).reduced (10, 6);
        layoutStrip (dry, r.removeFromLeft (r.getWidth() / 2).reduced (4, 0));
        layoutStrip (wet, r.reduced (4, 0));
    }
    if (panel != nullptr) panel->setBounds (overlayArea);
}

AudioProcessorEditor* GlowProcessor::createEditor() { return new GlowEditor (*this); }
