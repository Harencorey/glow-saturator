#pragma once
#include "UiCommon.h"
#include "ChannelStrip.h"

// The pop-up panels for the Dry/Wet channel strips, plus the small icon buttons that open them.

//==============================================================================
// Small clickable icon: draws an EQ curve or a compressor knee. Lit when the module is on,
// outlined when its panel is open.
class ModuleIcon : public juce::Button
{
public:
    enum class Kind { eq, comp };
    ModuleIcon (Kind k) : juce::Button (k == Kind::eq ? "EQ" : "COMP"), kind (k) {}

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
    Kind kind;
    bool on = false, open = false;
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
        layoutContent (getLocalBounds().withTrimmedTop (36).reduced (12, 6));
    }

    virtual void refresh() {}   // called by the editor's timer

protected:
    virtual void layoutContent (juce::Rectangle<int>) = 0;

private:
    juce::String titleText;
    ToggleParam power;
    juce::TextButton close;
};

//==============================================================================
// EQ response display: reads the band parameters and draws the combined curve.
class EqCurve : public juce::Component
{
public:
    EqCurve (juce::AudioProcessorValueTreeState& s, const juce::String& prefix) : state (s), p (prefix) {}

    EqSettings read() const
    {
        EqSettings e;
        auto v = [this] (const juce::String& id) { return state.getRawParameterValue (p + id)->load(); };
        e.on = v ("EqOn") > 0.5f;
        for (int b = 0; b < 4; ++b)
        {
            const juce::String n (b + 1);
            e.freq[b] = v ("EqFreq" + n); e.gain[b] = v ("EqGain" + n); e.q[b] = v ("EqQ" + n);
        }
        e.lowShelf = v ("EqLowShelf") > 0.5f;
        e.highShelf = v ("EqHighShelf") > 0.5f;
        return e;
    }

    void refresh()
    {
        const auto e = read();
        bool same = e.on == shown.on && e.lowShelf == shown.lowShelf && e.highShelf == shown.highShelf;
        for (int b = 0; b < 4 && same; ++b)
            same = std::abs (e.freq[b] - shown.freq[b]) < 1.0e-3f && std::abs (e.gain[b] - shown.gain[b]) < 1.0e-4f
                   && std::abs (e.q[b] - shown.q[b]) < 1.0e-4f;
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

        // curve
        juce::Path path;
        for (int i = 0; i <= 200; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 200.0);
            const float y = yOf (FourBandEQ::responseDb (shown, f));
            if (i == 0) path.startNewSubPath (xOf (f), y); else path.lineTo (xOf (f), y);
        }
        auto fill = path;
        fill.lineTo (r.getRight(), yOf (0.0f));
        fill.lineTo (r.getX(), yOf (0.0f));
        fill.closeSubPath();
        const auto col = shown.on ? GlowLookAndFeel::amber : GlowLookAndFeel::textDim;
        g.setColour (col.withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (col);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        // band handles
        for (int b = 0; b < 4; ++b)
        {
            const float x = xOf (shown.freq[b]), y = yOf (shown.gain[b]);
            g.setColour (col);
            g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
            g.setColour (GlowLookAndFeel::bg);
            g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x - 5.0f, y - 5.0f, 10.0f, 10.0f), juce::Justification::centred);
        }
    }

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String p;
    EqSettings shown;
};

//==============================================================================
class EqPanel : public ModulePanel
{
public:
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
            bandLabel[b].setFont (juce::FontOptions (11.0f, juce::Font::bold));
            bandLabel[b].setJustificationType (juce::Justification::centredLeft);
            bandLabel[b].setColour (juce::Label::textColourId, GlowLookAndFeel::text);
            addAndMakeVisible (bandLabel[b]);
        }
        lowShelf.setup (*this, s, prefix + "EqLowShelf", "SHELF");
        highShelf.setup (*this, s, prefix + "EqHighShelf", "SHELF");
    }

    void refresh() override { curve.refresh(); }

protected:
    void layoutContent (juce::Rectangle<int> r) override
    {
        curve.setBounds (r.removeFromTop (juce::jmin (140, r.getHeight() / 2)));
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
    ToggleParam lowShelf, highShelf;
};

//==============================================================================
// Horizontal gain-reduction meter, 0 to -20 dB, drawn from the right.
class GrMeter : public juce::Component
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
               std::function<float()> grSource)
        : ModulePanel (s, pathName + "  ·  COMP", prefix + "CompOn"),
          ratio (s, prefix + "CompRatio", 4), colour (s, prefix + "CompColour", 3), getGr (std::move (grSource))
    {
        input.setup (*this, s, prefix + "CompInput", "INPUT", 0.0);
        attack.setup (*this, s, prefix + "CompAttack", "ATTACK", 5.0);
        release.setup (*this, s, prefix + "CompRelease", "RELEASE", 200.0);
        output.setup (*this, s, prefix + "CompOutput", "OUTPUT", 0.0);
        addAndMakeVisible (ratio);
        addAndMakeVisible (colour);
        hpf.setup (*this, s, prefix + "CompHpf", "DETECTOR HPF");
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

    void refresh() override { gr.set (getGr ? getGr() : 0.0f); }

protected:
    void layoutContent (juce::Rectangle<int> r) override
    {
        auto knobs = r.removeFromLeft (r.getWidth() / 2);
        auto top = knobs.removeFromTop (knobs.getHeight() / 2).withTrimmedBottom (8);
        input.place (top.removeFromLeft (top.getWidth() / 2).reduced (6, 0));
        output.place (top.reduced (6, 0));
        attack.place (knobs.removeFromLeft (knobs.getWidth() / 2).reduced (6, 0));
        release.place (knobs.reduced (6, 0));

        r.removeFromLeft (16);
        gr.setBounds (r.removeFromTop (26));
        r.removeFromTop (10);
        ratioLabel.setBounds (r.removeFromTop (16));
        ratio.setBounds (r.removeFromTop (64));
        r.removeFromTop (8);
        colourLabel.setBounds (r.removeFromTop (16));
        colour.setBounds (r.removeFromTop (32));
        r.removeFromTop (10);
        hpf.button.setBounds (r.removeFromTop (28).removeFromLeft (140));
    }

private:
    Knob input, attack, release, output;
    ChoiceButtons ratio, colour;
    ToggleParam hpf;
    juce::Label ratioLabel, colourLabel;
    GrMeter gr;
    std::function<float()> getGr;
};
