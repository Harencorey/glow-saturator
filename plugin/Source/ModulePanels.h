#pragma once
#include "UiCommon.h"
#include "ChannelStrip.h"

// The pop-up panels for the Dry/Wet channel strips, plus the small icon buttons that open them.

//==============================================================================
// Small clickable icon: draws an EQ curve or a compressor knee. Lit when the module is on,
// outlined when its panel is open.
class ModuleIcon : public juce::Button, private juce::Timer
{
public:
    enum class Kind { eq, comp };
    ModuleIcon (Kind k) : juce::Button (k == Kind::eq ? "EQ" : "COMP"), kind (k) {}

    // Single click opens/closes the panel; double-click ONLY switches the module on/off.
    // A click waits out the double-click window first, so a double-click never opens a panel.
    std::function<void()> onSingleClick, onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        stopTimer();                 // cancel the pending single click
        swallowNextClick = true;     // the double-click's second mouse-up is not a click
        if (onDoubleClick) onDoubleClick();
    }

    void setState (bool moduleOn, bool panelOpen)
    {
        if (moduleOn != on || panelOpen != open) { on = moduleOn; open = panelOpen; repaint(); }
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::black.withAlpha (over ? 0.4f : 0.3f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (open ? GlowLookAndFeel::text : (on ? GlowLookAndFeel::amber : GlowLookAndFeel::textDim.withAlpha (0.5f)));
        g.drawRoundedRectangle (r, 4.0f, open ? 1.6f : 1.0f);

        auto icon = r.reduced (5.0f, 4.0f).withTrimmedBottom (9.0f);
        const auto c = on ? GlowLookAndFeel::amber : GlowLookAndFeel::textDim;
        g.setColour (c);
        juce::Path p;
        if (kind == Kind::eq)
        {
            // little bell curve
            for (int i = 0; i <= 20; ++i)
            {
                const float t = (float) i / 20.0f;
                const float y = std::exp (-std::pow ((t - 0.55f) * 4.5f, 2.0f));
                const juce::Point<float> pt (icon.getX() + t * icon.getWidth(), icon.getBottom() - (0.25f + 0.7f * y) * icon.getHeight());
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
        }
        else
        {
            // compressor transfer curve: 1:1 up to the knee, then flattening
            p.startNewSubPath (icon.getX(), icon.getBottom());
            p.lineTo (icon.getX() + icon.getWidth() * 0.5f, icon.getBottom() - icon.getHeight() * 0.6f);
            p.quadraticTo (icon.getX() + icon.getWidth() * 0.62f, icon.getBottom() - icon.getHeight() * 0.72f,
                           icon.getRight(), icon.getBottom() - icon.getHeight() * 0.85f);
        }
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText (getName(), r.removeFromBottom (11.0f), juce::Justification::centred);
    }

private:
    using juce::Button::clicked;
    void clicked() override
    {
        if (swallowNextClick) { swallowNextClick = false; return; }
        startTimer (juce::MouseEvent::getDoubleClickTimeout() + 20);
    }
    void timerCallback() override
    {
        stopTimer();
        if (onSingleClick) onSingleClick();
    }

    Kind kind;
    bool on = false, open = false, swallowNextClick = false;
};

//==============================================================================
// Small "swap" button between a strip's EQ and COMP icons: flips the order of the two
// in that path. Toggle state = Comp first; drawn as two opposing arrows either way.
class SwapButton : public juce::Button
{
public:
    SwapButton() : juce::Button ("Swap order") { setClickingTogglesState (true); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        const float cx = r.getCentreX(), cy = r.getCentreY(), h = juce::jmin (r.getHeight() * 0.42f, 6.0f);
        g.setColour (over ? GlowLookAndFeel::text : GlowLookAndFeel::textDim);
        juce::Path p;
        // down arrow on the left, up arrow on the right
        p.startNewSubPath (cx - 4.0f, cy - h); p.lineTo (cx - 4.0f, cy + h);
        p.startNewSubPath (cx - 6.5f, cy + h - 2.5f); p.lineTo (cx - 4.0f, cy + h); p.lineTo (cx - 1.5f, cy + h - 2.5f);
        p.startNewSubPath (cx + 4.0f, cy + h); p.lineTo (cx + 4.0f, cy - h);
        p.startNewSubPath (cx + 1.5f, cy - h + 2.5f); p.lineTo (cx + 4.0f, cy - h); p.lineTo (cx + 6.5f, cy - h + 2.5f);
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

//==============================================================================
// Base panel: dark card with a title, an ON switch for the module and a close button.
class ModulePanel : public juce::Component
{
public:
    ModulePanel (juce::AudioProcessorValueTreeState& state, const juce::String& title, const juce::String& onParamId)
    {
        titleText = title;
        power.setup (*this, state, onParamId, "ON");
        close.setButtonText ("CLOSE");
        close.onClick = [this] { if (onClose) onClose(); };
        power.button.setTooltip ("Switch this module on or off (you can also double-click its icon)");
        close.setTooltip ("Close this panel");
        addAndMakeVisible (close);
    }

    std::function<void()> onClose;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 10.0f);
        g.setColour (GlowLookAndFeel::section.brighter (0.05f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (GlowLookAndFeel::amber.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (GlowLookAndFeel::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (titleText, getLocalBounds().removeFromTop (36).reduced (16, 0), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto top = getLocalBounds().removeFromTop (36).reduced (12, 8);
        close.setBounds (top.removeFromRight (64));
        top.removeFromRight (8);
        power.button.setBounds (top.removeFromRight (44));
        if (headerExtra != nullptr) { top.removeFromRight (8); headerExtra->setBounds (top.removeFromRight (56)); }
        layoutContent (getLocalBounds().withTrimmedTop (36).reduced (12, 6));
    }

    virtual void refresh() {}   // called by the editor's timer
    virtual void setSoloHandler (std::function<void (int)>) {}   // EQ panels: Shift-solo a band

protected:
    virtual void layoutContent (juce::Rectangle<int>) = 0;
    juce::Component* headerExtra = nullptr;   // e.g. LINK on the pre-saturation EQ

private:
    juce::String titleText;
    ToggleParam power;
    juce::TextButton close;
};

//==============================================================================
// EQ response display: draws the combined curve, and the band points can be dragged
// (left/right = frequency, up/down = gain), scrolled (Q) and double-clicked (gain to 0 dB).
// While a band is soloed (Shift) the region the solo filter passes is shaded, the soloed band's
// own curve is drawn in colour and everything else is greyed out; dragging keeps working.
class EqCurve : public juce::Component, public juce::SettableTooltipClient
{
public:
    // bands: which of the 4 band slots exist (the pre-saturation EQ uses 1, 2 and 4)
    EqCurve (juce::AudioProcessorValueTreeState& s, const juce::String& prefix, std::vector<int> bandList = { 0, 1, 2, 3 })
        : state (s), p (prefix), bands (std::move (bandList))
    {
        for (int b = 0; b < 4; ++b)
        {
            const juce::String n (b + 1);
            freqParam[b] = state.getParameter (p + "EqFreq" + n);
            gainParam[b] = state.getParameter (p + "EqGain" + n);
            qParam[b] = state.getParameter (p + "EqQ" + n);
            bandOnParam[b] = state.getParameter (p + "EqBand" + n + "On");
        }
        onParam = state.getParameter (p + "EqOn");
        setRepaintsOnMouseActivity (false);
    }

    // Optional extras for the pre-saturation EQ on the main panel:
    // point names ("M", "H"), a fixed high shelf on band 4, and the HPF drawn in with its own
    // point (drag left/right = frequency, double-click = Off), Keep lows and the Link mirror.
    std::vector<juce::String> names;
    bool fixedHighShelf = false;
    void attachHpf (juce::RangedAudioParameter* hz, std::atomic<float>* slope24, std::atomic<float>* keep, std::atomic<float>* link)
    {
        hpfParam = hz; slopeP = slope24; keepP = keep; linkP = link;
    }

    static constexpr int kHp = 4;   // the HPF point

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hitBand (e.position);
        if (h != hover) { hover = h; repaint(); }
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override { if (hover >= 0) { hover = -1; repaint(); } }

    // Drag a point: frequency + gain.  Option/Alt: frequency only.  Cmd: Q (up = narrower).
    // Shift (held): solo the band, i.e. hear only what that band is working on.
    // Dragging is relative, so pressing/releasing a modifier mid-drag never makes the point jump.
    std::function<void (int)> onSolo;   // band slot 0..3, or -1 to stop soloing

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragBand = hitBand (e.position);
        if (dragBand < 0) return;
        if (dragBand == kHp)
        {
            hpfParam->beginChangeGesture();
            anchorPos = e.position;
            anchorF = juce::jmax (20.0f, valueOf (hpfParam));
            return;
        }
        if (onParam != nullptr && onParam->getValue() < 0.5f)   // dragging a point switches the EQ in
            onParam->setValueNotifyingHost (1.0f);
        if (freqParam[dragBand] == nullptr || gainParam[dragBand] == nullptr) { dragBand = -1; return; }
        freqParam[dragBand]->beginChangeGesture();
        gainParam[dragBand]->beginChangeGesture();
        if (qParam[dragBand] != nullptr) qParam[dragBand]->beginChangeGesture();
        anchor (e);
        updateSolo (e.mods.isShiftDown());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand < 0) return;
        if (dragBand == kHp)
        {
            const float dx = e.position.x - anchorPos.x;
            setParam (hpfParam, juce::jlimit (20.0f, 1000.0f, anchorF * std::pow (1000.0f, dx / (float) getWidth())));
            return;
        }
        updateSolo (e.mods.isShiftDown());
        const int mode = e.mods.isCommandDown() ? 2 : (e.mods.isAltDown() ? 1 : 0);   // 2 = Q, 1 = freq only
        if (mode != anchorMode) anchor (e);
        const auto r = getLocalBounds().toFloat();
        const float dx = e.position.x - anchorPos.x, dy = e.position.y - anchorPos.y;
        if (mode == 2)
        {
            if (qParam[dragBand] != nullptr)
                setParam (qParam[dragBand], juce::jlimit (0.1f, 10.0f, anchorQ * std::pow (2.0f, -dy / 40.0f)));
            return;
        }
        setParam (freqParam[dragBand], juce::jlimit (20.0f, 20000.0f, anchorF * std::pow (1000.0f, dx / r.getWidth())));
        if (mode == 0)
            setParam (gainParam[dragBand], juce::jlimit (-18.0f, 18.0f, anchorG - dy / (r.getHeight() * 0.5f * 0.9f) * 20.0f));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        updateSolo (false);
        if (dragBand < 0) return;
        if (dragBand == kHp) { hpfParam->endChangeGesture(); dragBand = -1; return; }
        freqParam[dragBand]->endChangeGesture();
        gainParam[dragBand]->endChangeGesture();
        if (qParam[dragBand] != nullptr) qParam[dragBand]->endChangeGesture();
        dragBand = -1;
    }

    void modifierKeysChanged (const juce::ModifierKeys& m) override
    {
        if (dragBand >= 0 && dragBand != kHp) updateSolo (m.isShiftDown());   // Shift pressed/released without moving
    }

    // Double-click a point: bypass / re-engage that band.  Option-double-click: reset its gain.
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int b = hitBand (e.position);
        if (b < 0) return;
        if (b == kHp)   // HPF point: double-click = Off
        {
            hpfParam->beginChangeGesture();
            hpfParam->setValueNotifyingHost (0.0f);
            hpfParam->endChangeGesture();
            return;
        }
        if (e.mods.isAltDown() || bandOnParam[b] == nullptr)
        {
            gainParam[b]->beginChangeGesture();
            setParam (gainParam[b], 0.0f);
            gainParam[b]->endChangeGesture();
            return;
        }
        bandOnParam[b]->beginChangeGesture();
        bandOnParam[b]->setValueNotifyingHost (bandOnParam[b]->getValue() > 0.5f ? 0.0f : 1.0f);
        bandOnParam[b]->endChangeGesture();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int b = dragBand >= 0 ? dragBand : hitBand (e.position);
        if (b < 0 || b == kHp || qParam[b] == nullptr) return;   // no Q on this band
        const float q = qParam[b]->convertFrom0to1 (qParam[b]->getValue());
        qParam[b]->beginChangeGesture();
        setParam (qParam[b], juce::jlimit (0.1f, 10.0f, q * std::pow (2.0f, w.deltaY * 2.0f)));
        qParam[b]->endChangeGesture();
    }

    EqSettings read() const
    {
        EqSettings e;
        auto v = [this] (const juce::String& id, float def = 0.0f)
        { auto* a = state.getRawParameterValue (p + id); return a != nullptr ? a->load() : def; };
        e.on = v ("EqOn") > 0.5f;
        for (int b = 0; b < 4; ++b)
        {
            const juce::String n (b + 1);
            e.freq[b] = v ("EqFreq" + n, e.freq[b]); e.gain[b] = v ("EqGain" + n); e.q[b] = v ("EqQ" + n, 0.7f);
        }
        e.lowShelf = v ("EqLowShelf") > 0.5f;
        e.highShelf = v ("EqHighShelf") > 0.5f;
        for (int b = 0; b < 4; ++b) e.bandOn[b] = v ("EqBand" + juce::String (b + 1) + "On", 1.0f) > 0.5f;
        if (fixedHighShelf) { e.lowShelf = false; e.highShelf = true; }
        for (int b = 0; b < 4; ++b)   // slots this curve doesn't use are out of the picture
            if (std::find (bands.begin(), bands.end(), b) == bands.end()) e.bandOn[b] = false;
        return e;
    }

    float hpfHz() const { return hpfParam != nullptr ? valueOf (hpfParam) : 0.0f; }
    bool hpfOn() const { return hpfParam != nullptr && hpfHz() >= 19.5f; }
    // the HPF's response in dB (the settled filter, as the DSP runs it)
    double hpfDb (double f) const
    {
        if (! hpfOn()) return 0.0;
        const auto k = slopeP != nullptr && slopeP->load() < 0.5f ? Coherent::Kind::highPass12 : Coherent::Kind::highPass;
        return Coherent::Stage::responseDb (k, hpfHz(), 0.0, 0.70710678, f, 48000.0);
    }

    void refresh()
    {
        const auto e = read();
        bool same = e.on == shown.on && e.lowShelf == shown.lowShelf && e.highShelf == shown.highShelf;
        for (int b = 0; b < 4 && same; ++b)
            same = e.bandOn[b] == shown.bandOn[b] && std::abs (e.freq[b] - shown.freq[b]) < 1.0e-3f && std::abs (e.gain[b] - shown.gain[b]) < 1.0e-4f
                   && std::abs (e.q[b] - shown.q[b]) < 1.0e-4f;
        if (hpfParam != nullptr)
        {
            const float h = hpfHz(), sl = slopeP != nullptr ? slopeP->load() : 1.0f,
                        kp = keepP != nullptr ? keepP->load() : 0.0f, lk = linkP != nullptr ? linkP->load() : 0.0f;
            if (std::abs (h - shownHpf) > 1.0e-3f || std::abs (sl - shownSlope) > 0.1f || std::abs (kp - shownKeep) > 0.1f || std::abs (lk - shownLink) > 0.1f)
            { shownHpf = h; shownSlope = sl; shownKeep = kp; shownLink = lk; same = false; }
        }
        if (! same) { shown = e; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 6.0f);

        auto xOf = [&r] (double f) { return r.getX() + r.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };
        auto yOf = [&r] (float db) { return r.getCentreY() - r.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db / 20.0f) * 0.9f; };

        // grid
        g.setFont (juce::FontOptions (9.0f));
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawVerticalLine ((int) xOf (f), r.getY(), r.getBottom());
            g.setColour (GlowLookAndFeel::textDim.withAlpha (0.6f));
            g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k" : juce::String ((int) f), (int) xOf (f) + 2, (int) r.getBottom() - 12, 30, 11, juce::Justification::left);
        }
        for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            g.setColour (juce::Colours::white.withAlpha (std::abs (db) < 0.1f ? 0.15f : 0.06f));
            g.drawHorizontalLine ((int) yOf (db), r.getX(), r.getRight());
        }

        const bool solo = soloing && dragBand >= 0;
        const auto col = shown.on ? GlowLookAndFeel::amber : GlowLookAndFeel::textDim;

        // solo: shade exactly the region the solo filter lets through (same maths as the DSP:
        // bell = band-pass at its frequency and Q (Q held to 0.5..10), low shelf = low-pass,
        // high shelf = high-pass), with dashed edges at its -3 dB points
        if (solo)
        {
            const int b = dragBand;
            const double f0 = juce::jlimit (20.0, 20000.0, (double) shown.freq[b]);
            double lo = 20.0, hi = 20000.0;
            if (b == 0 && shown.lowShelf)       hi = f0;
            else if (b == 3 && shown.highShelf) lo = f0;
            else
            {
                const double q = juce::jlimit (0.1, 10.0, (double) shown.q[b]);
                const double k = 1.0 / (2.0 * q);
                lo = f0 * (std::sqrt (1.0 + k * k) - k);
                hi = f0 * (std::sqrt (1.0 + k * k) + k);
            }
            const float x0 = juce::jmax (r.getX(), xOf (juce::jmax (20.0, lo)));
            const float x1 = juce::jmin (r.getRight(), xOf (juce::jmin (20000.0, hi)));
            // everything outside the soloed region is dimmed
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), x0 - r.getX(), r.getHeight()));
            g.fillRect (juce::Rectangle<float> (x1, r.getY(), r.getRight() - x1, r.getHeight()));
            g.setGradientFill (juce::ColourGradient (GlowLookAndFeel::amber.withAlpha (0.16f), 0.0f, r.getY(),
                                                     GlowLookAndFeel::amber.withAlpha (0.04f), 0.0f, r.getBottom(), false));
            g.fillRect (juce::Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight()));
            const float dash[] { 4.0f, 3.0f };
            g.setColour (GlowLookAndFeel::amber.withAlpha (0.8f));
            for (float ex : { x0, x1 })
                if (ex > r.getX() + 1.0f && ex < r.getRight() - 1.0f)
                    g.drawDashedLine (juce::Line<float> (ex, r.getY() + 2.0f, ex, r.getBottom() - 2.0f), dash, 2, 1.0f);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("SOLO", juce::Rectangle<float> (x0, r.getBottom() - 28.0f, juce::jmax (40.0f, x1 - x0), 12.0f)
                                    .constrainedWithin (r.reduced (3.0f)), juce::Justification::centred);
        }

        // curve (while soloing: the full curve greyed out, and the soloed band's own curve in colour)
        auto curvePath = [&] (const EqSettings& es, bool withHpf = true)
        {
            juce::Path path;
            for (int i = 0; i <= 200; ++i)
            {
                const double f = 20.0 * std::pow (1000.0, i / 200.0);
                const float y = yOf ((float) (FourBandEQ::responseDb (es, f) + (withHpf ? hpfDb (f) : 0.0)));
                if (i == 0) path.startNewSubPath (xOf (f), y); else path.lineTo (xOf (f), y);
            }
            return path;
        };
        auto fillUnder = [&] (juce::Path path)
        {
            path.lineTo (r.getRight(), yOf (0.0f));
            path.lineTo (r.getX(), yOf (0.0f));
            path.closeSubPath();
            return path;
        };
        const auto path = curvePath (shown);
        const float dashes[] { 5.0f, 4.0f };
        if (hpfOn())
        {
            // the cut region, and (Keep lows) the clean lows coming back
            const float hx = xOf (hpfHz());
            g.setColour (GlowLookAndFeel::text.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), hx - r.getX(), r.getHeight()));
            if (shownKeep > 0.5f)
            {
                juce::Path kp;
                for (int i = 0; i <= 120; ++i)
                {
                    const double f = 20.0 * std::pow (hpfHz() * 4.0 / 20.0, i / 120.0);
                    const double lin = juce::jmax (1.0e-4, 1.0 - std::pow (10.0, hpfDb (f) / 20.0));
                    const float y = yOf ((float) (20.0 * std::log10 (lin)));
                    if (i == 0) kp.startNewSubPath (xOf (f), y); else kp.lineTo (xOf (f), y);
                }
                juce::Path dashed;
                juce::PathStrokeType (1.5f).createDashedStroke (dashed, kp, dashes, 2);
                g.setColour (GlowLookAndFeel::text.withAlpha (0.55f));
                g.fillPath (dashed);
            }
        }
        if (shownLink > 0.5f && shown.on)
        {
            // Link: the exact opposite runs after the tubes
            EqSettings mirror = shown;
            for (auto& gdb : mirror.gain) gdb = -gdb;
            juce::Path dashed;
            juce::PathStrokeType (1.4f).createDashedStroke (dashed, curvePath (mirror, false), dashes, 2);
            g.setColour (GlowLookAndFeel::text.withAlpha (0.4f));
            g.fillPath (dashed);
        }
        if (solo)
        {
            g.setColour (GlowLookAndFeel::textDim.withAlpha (0.45f));
            g.strokePath (path, juce::PathStrokeType (1.2f));
            EqSettings one = shown;
            one.on = true;
            for (int b = 0; b < 4; ++b) one.bandOn[b] = b == dragBand && shown.bandOn[b];
            const auto bandPath = curvePath (one);
            g.setColour (GlowLookAndFeel::amber.withAlpha (0.18f));
            g.fillPath (fillUnder (bandPath));
            g.setColour (GlowLookAndFeel::amber);
            g.strokePath (bandPath, juce::PathStrokeType (2.0f));
        }
        else
        {
            g.setColour (col.withAlpha (0.12f));
            g.fillPath (fillUnder (path));
            g.setColour (col);
            g.strokePath (path, juce::PathStrokeType (2.0f));
        }

        // band handles (bigger when hovered or dragged), with a readout for the active one
        for (size_t bi = 0; bi < bands.size(); ++bi)
        {
            const int b = bands[bi];
            const float x = xOf (shown.freq[b]), y = yOf (shown.gain[b]);
            const bool active = solo ? b == dragBand : (b == hover || b == dragBand);
            const bool greyed = solo && b != dragBand;
            const bool bypassed = ! shown.bandOn[b];
            const float rad = active ? 8.0f : 6.0f;
            const auto hc = active ? GlowLookAndFeel::text : (greyed || bypassed) ? GlowLookAndFeel::textDim.withAlpha (bypassed ? 0.6f : 0.35f) : col;
            g.setColour (hc);
            if (bypassed)
            {
                // bypassed band: hollow ring with its number, so it's clearly out but still grabbable
                g.setColour (GlowLookAndFeel::bg.withAlpha (0.85f));
                g.fillEllipse (x - rad, y - rad, 2.0f * rad, 2.0f * rad);
                g.setColour (hc);
                g.drawEllipse (x - rad + 0.75f, y - rad + 0.75f, 2.0f * rad - 1.5f, 2.0f * rad - 1.5f, 1.5f);
            }
            else
                g.fillEllipse (x - rad, y - rad, 2.0f * rad, 2.0f * rad);
            g.setColour (bypassed ? hc : GlowLookAndFeel::bg);
            g.setFont (juce::FontOptions (active ? 10.0f : 8.5f, juce::Font::bold));
            g.drawText (bi < names.size() ? names[bi] : bands.size() == 3 ? juce::String::charToString ("LMH"[bi]) : juce::String (b + 1),
                        juce::Rectangle<float> (x - rad, y - rad, 2.0f * rad, 2.0f * rad), juce::Justification::centred);
            if (active)
            {
                const juce::String txt = (shown.freq[b] >= 1000.0f ? juce::String (shown.freq[b] / 1000.0f, 2) + " kHz" : juce::String ((int) shown.freq[b]) + " Hz")
                                         + "   " + (shown.gain[b] > 0.05f ? "+" : "") + juce::String (shown.gain[b], 1) + " dB"
                                         + (qParam[b] != nullptr ? "   Q " + juce::String (shown.q[b], 2) : juce::String())
                                         + (bypassed ? "   OFF" : "")
                                         + (soloing && b == dragBand ? "   SOLO" : "");
                auto box = juce::Rectangle<float> (x - 80.0f, y - rad - 22.0f, 160.0f, 16.0f).constrainedWithin (r.reduced (2.0f));
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.fillRoundedRectangle (box, 3.0f);
                g.setColour (GlowLookAndFeel::text);
                g.setFont (juce::FontOptions (10.5f));
                g.drawText (txt, box, juce::Justification::centred);
            }
        }
        if (hpfOn())
        {
            const auto pt = hpPoint();
            const bool active = hover == kHp || dragBand == kHp;
            const float rad = active ? 9.0f : 7.5f;
            g.setColour (GlowLookAndFeel::bg.withAlpha (0.9f));
            g.fillEllipse (pt.x - rad, pt.y - rad, 2.0f * rad, 2.0f * rad);
            g.setColour (GlowLookAndFeel::text);
            g.drawEllipse (pt.x - rad + 0.75f, pt.y - rad + 0.75f, 2.0f * rad - 1.5f, 2.0f * rad - 1.5f, 1.5f);
            g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
            g.drawText ("HP", juce::Rectangle<float> (pt.x - rad, pt.y - rad, 2.0f * rad, 2.0f * rad), juce::Justification::centred);
            if (active)
            {
                const float hz = hpfHz();
                auto box = juce::Rectangle<float> (pt.x - 40.0f, pt.y - rad - 22.0f, 80.0f, 16.0f).constrainedWithin (r.reduced (2.0f));
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.fillRoundedRectangle (box, 3.0f);
                g.setColour (GlowLookAndFeel::text);
                g.setFont (juce::FontOptions (10.5f));
                g.drawText ("HPF " + juce::String ((int) std::round (hz)) + " Hz", box, juce::Justification::centred);
            }
        }
    }

private:
    juce::Point<float> hpPoint() const
    {
        const auto r = getLocalBounds().toFloat();
        const float x = r.getX() + r.getWidth() * std::log (hpfHz() / 20.0f) / std::log (1000.0f);
        const float y = r.getCentreY() - r.getHeight() * 0.5f * (-3.0f / 20.0f) * 0.9f;
        return { x, y };
    }

    int hitBand (juce::Point<float> pos) const
    {
        const auto r = getLocalBounds().toFloat();
        int best = -1;
        float bestD = 14.0f;   // grab radius, px
        if (hpfOn() && pos.getDistanceFrom (hpPoint()) < bestD) { bestD = pos.getDistanceFrom (hpPoint()); best = kHp; }
        for (int b : bands)
        {
            const float x = r.getX() + r.getWidth() * std::log (shown.freq[b] / 20.0f) / std::log (1000.0f);
            const float y = r.getCentreY() - r.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, shown.gain[b] / 20.0f) * 0.9f;
            const float d = pos.getDistanceFrom ({ x, y });
            if (d < bestD) { bestD = d; best = b; }
        }
        return best;
    }

    float gainOf (int b) const { return gainParam[b]->convertFrom0to1 (gainParam[b]->getValue()); }

    static void setParam (juce::RangedAudioParameter* prm, float v)
    {
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }

    juce::AudioProcessorValueTreeState& state;
    juce::String p;
    std::vector<int> bands;
    EqSettings shown;
    juce::RangedAudioParameter* freqParam[4] {}, * gainParam[4] {}, * qParam[4] {};
    juce::RangedAudioParameter* bandOnParam[4] {};
    juce::RangedAudioParameter* onParam = nullptr;
    juce::RangedAudioParameter* hpfParam = nullptr;
    std::atomic<float>* slopeP = nullptr, * keepP = nullptr, * linkP = nullptr;
    float shownHpf = -1.0f, shownSlope = -1.0f, shownKeep = -1.0f, shownLink = -1.0f;
    int hover = -1, dragBand = -1;
    // relative-drag anchor (re-taken whenever the modifier mode changes)
    juce::Point<float> anchorPos;
    float anchorF = 1000.0f, anchorG = 0.0f, anchorQ = 1.0f;
    int anchorMode = -1;
    bool soloing = false;

    float valueOf (juce::RangedAudioParameter* prm) const { return prm->convertFrom0to1 (prm->getValue()); }
    void anchor (const juce::MouseEvent& e)
    {
        anchorPos = e.position;
        anchorF = valueOf (freqParam[dragBand]);
        anchorG = valueOf (gainParam[dragBand]);
        anchorQ = qParam[dragBand] != nullptr ? valueOf (qParam[dragBand]) : 1.0f;
        anchorMode = e.mods.isCommandDown() ? 2 : (e.mods.isAltDown() ? 1 : 0);
    }
    void updateSolo (bool want)
    {
        want = want && dragBand >= 0 && dragBand != kHp;
        if (want == soloing) return;
        soloing = want;
        if (onSolo) onSolo (want ? dragBand : -1);
        repaint();
    }
};

//==============================================================================
// A bypassed band: its name reads "... OFF" and its knobs dim (they still work).
inline void showBandOn (juce::Label& label, const juce::String& name, const std::atomic<float>* onParam,
                        std::initializer_list<Knob*> knobs)
{
    const bool on = onParam == nullptr || onParam->load() > 0.5f;
    const auto text = on ? name : name + "  OFF";
    if (label.getText() != text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, on ? GlowLookAndFeel::text : GlowLookAndFeel::textDim);
        for (auto* k : knobs) { k->slider.setAlpha (on ? 1.0f : 0.45f); k->label.setAlpha (on ? 1.0f : 0.45f); }
    }
}

//==============================================================================
class EqPanel : public ModulePanel
{
public:
    void setSoloHandler (std::function<void (int)> f) override { curve.onSolo = std::move (f); }
    EqPanel (juce::AudioProcessorValueTreeState& s, const juce::String& prefix, const juce::String& pathName)
        : ModulePanel (s, pathName + "  ·  EQ", prefix + "EqOn"), curve (s, prefix)
    {
        addAndMakeVisible (curve);
        const float defF[4] { 80.0f, 400.0f, 2500.0f, 10000.0f }, defQ[4] { 0.7f, 1.0f, 1.0f, 0.7f };
        for (int b = 0; b < 4; ++b)
        {
            const juce::String n (b + 1);
            freq[b].setup (*this, s, prefix + "EqFreq" + n, "FREQ", defF[b], 10.0f);
            gain[b].setup (*this, s, prefix + "EqGain" + n, "GAIN", 0.0, 10.0f);
            q[b].setup (*this, s, prefix + "EqQ" + n, "Q", defQ[b], 10.0f);
            bandLabel[b].setText ("BAND " + n, juce::dontSendNotification);
            bandOn[b] = s.getRawParameterValue (prefix + "EqBand" + n + "On");
            bandLabel[b].setFont (juce::FontOptions (11.0f, juce::Font::bold));
            bandLabel[b].setJustificationType (juce::Justification::centredLeft);
            bandLabel[b].setColour (juce::Label::textColourId, GlowLookAndFeel::text);
            addAndMakeVisible (bandLabel[b]);
        }
        lowShelf.setup (*this, s, prefix + "EqLowShelf", "SHELF");
        highShelf.setup (*this, s, prefix + "EqHighShelf", "SHELF");
        curve.setTooltip ("Drag a point: frequency and gain. Cmd-drag: Q. Option-drag: frequency only. Hold Shift while dragging: solo that band. Scroll over a point: Q. Double-click a point: bypass/re-engage that band. Option-double-click: reset its gain.");
        for (int b = 0; b < 4; ++b)
        {
            freq[b].tip ("Band " + juce::String (b + 1) + " frequency");
            gain[b].tip ("Band " + juce::String (b + 1) + " boost or cut");
            q[b].tip ("Band " + juce::String (b + 1) + " width: low Q = broad, high Q = narrow");
        }
        lowShelf.button.setTooltip ("Band 1: low shelf (on) or bell (off)");
        highShelf.button.setTooltip ("Band 4: high shelf (on) or bell (off)");
    }

    void refresh() override
    {
        curve.refresh();
        for (int b = 0; b < 4; ++b)
            showBandOn (bandLabel[b], "BAND " + juce::String (b + 1), bandOn[b], { &freq[b], &gain[b], &q[b] });
    }

protected:
    void layoutContent (juce::Rectangle<int> r) override
    {
        curve.setBounds (r.removeFromTop (juce::jmax (140, r.getHeight() - 150)));   // knobs keep ~150 px, the curve gets the rest
        r.removeFromTop (6);
        const int w = r.getWidth() / 4;
        for (int b = 0; b < 4; ++b)
        {
            auto col = r.removeFromLeft (w).reduced (4, 0);
            auto head = col.removeFromTop (20);
            if (b == 0) lowShelf.button.setBounds (head.removeFromRight (52).reduced (0, 1));
            if (b == 3) highShelf.button.setBounds (head.removeFromRight (52).reduced (0, 1));
            bandLabel[b].setBounds (head);
            const int kw = col.getWidth() / 3;
            freq[b].place (col.removeFromLeft (kw), 14);
            gain[b].place (col.removeFromLeft (kw), 14);
            q[b].place (col, 14);
        }
    }

private:
    EqCurve curve;
    Knob freq[4], gain[4], q[4];
    juce::Label bandLabel[4];
    std::atomic<float>* bandOn[4] {};
    ToggleParam lowShelf, highShelf;
};

//==============================================================================
// Horizontal gain-reduction meter, 0 to -20 dB, drawn from the right.
class GrMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    void set (float grDb)
    {
        const float v = juce::jlimit (0.0f, 20.0f, -grDb);
        shown = v > shown ? v : shown * 0.85f + v * 0.15f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r, 3.0f);
        auto bar = r.reduced (2.0f);
        g.setColour (GlowLookAndFeel::amber);
        g.fillRect (bar.withLeft (bar.getRight() - bar.getWidth() * shown / 20.0f));
        g.setColour (GlowLookAndFeel::textDim);
        g.setFont (juce::FontOptions (9.0f));
        for (int db : { 1, 3, 6, 10, 15 })
        {
            const float x = bar.getRight() - bar.getWidth() * (float) db / 20.0f;
            g.drawVerticalLine ((int) x, bar.getY(), bar.getY() + 4.0f);
        }
        g.setColour (GlowLookAndFeel::text);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("GR  " + (shown < 0.05f ? juce::String ("0.0") : juce::String (-shown, 1)) + " dB", getLocalBounds().reduced (6, 0), juce::Justification::centredLeft);
    }

private:
    float shown = 0.0f;
};

//==============================================================================
class CompPanel : public ModulePanel
{
public:
    CompPanel (juce::AudioProcessorValueTreeState& s, const juce::String& prefix, const juce::String& pathName,
               std::function<float()> grSource, std::function<float()> makeupSource)
        : ModulePanel (s, pathName + "  ·  COMP", prefix + "CompOn"),
          ratio (s, prefix + "CompRatio", 4), colour (s, prefix + "CompColour", 3), getGr (std::move (grSource)), getMakeup (std::move (makeupSource))
    {
        input.setup (*this, s, prefix + "CompInput", "INPUT", 0.0);
        attack.setup (*this, s, prefix + "CompAttack", "ATTACK", 5.0);
        release.setup (*this, s, prefix + "CompRelease", "RELEASE", 200.0);
        output.setup (*this, s, prefix + "CompOutput", "OUTPUT", 0.0);
        mix.setup (*this, s, prefix + "CompMix", "MIX", 100.0);   // parallel blend (phase-exact)
        input.tip ("How hard you hit the compressor. There's no threshold knob: more Input = more compression.");
        output.tip ("Level after the compressor (on top of Auto Makeup, if that's on)");
        attack.tip ("How fast it clamps down on peaks. Slower lets more transient punch through.");
        release.tip ("How fast it lets go after a peak. Short = pumpy and loud, long = smooth.");
        mix.tip ("Parallel compression: blends the uncompressed signal back in. 100% = fully compressed.");
        ratio.tip ("Ratio: how hard it compresses once it's working. The knee gets harder as the ratio goes up; NUKE is a brickwall.");
        colour.tip ("Distortion colour: CLEAN, DIST 2 (2nd harmonic, warm and thick) or DIST 3 (3rd harmonic, edgier)");
        hpf.button.setTooltip ("Detector high-pass: stops kick and bass from pumping the compressor");
        gr.setTooltip ("Gain reduction: how much the compressor is turning things down");
        addAndMakeVisible (ratio);
        addAndMakeVisible (colour);
        hpf.setup (*this, s, prefix + "CompHpf", "DETECTOR HPF");
        autoMakeup.setup (*this, s, prefix + "CompAuto", "AUTO MAKEUP");
        autoMakeup.button.setTooltip ("Automatic makeup gain: measures what the compressor takes away (averaged over ~1.5 s) and gives it back, so the level stays put at any Input setting. Output trims on top.");
        makeupReadout.setJustificationType (juce::Justification::centred);
        makeupReadout.setFont (juce::FontOptions (10.5f));
        makeupReadout.setColour (juce::Label::textColourId, GlowLookAndFeel::textDim);
        addAndMakeVisible (makeupReadout);
        pAuto = s.getRawParameterValue (prefix + "CompAuto");
        for (auto* l : { &ratioLabel, &colourLabel })
        {
            l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, GlowLookAndFeel::textDim);
            addAndMakeVisible (*l);
        }
        ratioLabel.setText ("RATIO", juce::dontSendNotification);
        colourLabel.setText ("DISTORTION", juce::dontSendNotification);
        addAndMakeVisible (gr);
    }

    void refresh() override
    {
        gr.set (getGr ? getGr() : 0.0f);
        // the makeup AUTO MAKEUP is applying right now (measured from the audio)
        juce::String t;
        if (pAuto->load() > 0.5f)
        {
            const float m = getMakeup ? getMakeup() : 0.0f;
            t = "makeup " + juce::String (m > 0.05f ? "+" : "") + juce::String (std::abs (m) < 0.05f ? 0.0f : m, 1) + " dB";
        }
        if (t != makeupReadout.getText()) makeupReadout.setText (t, juce::dontSendNotification);
    }

protected:
    void layoutContent (juce::Rectangle<int> r) override
    {
        // INPUT / OUTPUT over ATTACK / RELEASE, with MIX on its own at the end of the row
        auto knobs = r.removeFromLeft (r.getWidth() * 3 / 5);
        const int colW = knobs.getWidth() / 3;
        auto mixCol = knobs.removeFromRight (colW);
        auto top = knobs.removeFromTop (knobs.getHeight() / 2).withTrimmedBottom (8);
        input.place (top.removeFromLeft (top.getWidth() / 2).reduced (6, 0));
        output.place (top.reduced (6, 0));
        attack.place (knobs.removeFromLeft (knobs.getWidth() / 2).reduced (6, 0));
        release.place (knobs.reduced (6, 0));
        mix.place (mixCol.withSizeKeepingCentre (mixCol.getWidth(), top.getHeight()).reduced (6, 0));

        r.removeFromLeft (16);
        gr.setBounds (r.removeFromTop (26));
        r.removeFromTop (10);
        ratioLabel.setBounds (r.removeFromTop (16));
        ratio.setBounds (r.removeFromTop (64));
        r.removeFromTop (8);
        colourLabel.setBounds (r.removeFromTop (16));
        colour.setBounds (r.removeFromTop (32));
        r.removeFromTop (10);
        auto row = r.removeFromTop (28);
        hpf.button.setBounds (row.removeFromLeft (140));
        row.removeFromLeft (8);
        autoMakeup.button.setBounds (row.removeFromLeft (140));
        makeupReadout.setBounds (autoMakeup.button.getBounds().translated (0, 30).withHeight (16));
    }

private:
    Knob input, attack, release, output, mix;
    ChoiceButtons ratio, colour;
    ToggleParam hpf, autoMakeup;
    juce::Label makeupReadout;
    std::atomic<float>* pAuto = nullptr;
    juce::Label ratioLabel, colourLabel;
    GrMeter gr;
    std::function<float()> getGr, getMakeup;
};
