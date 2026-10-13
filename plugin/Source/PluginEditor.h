#pragma once
#include "PluginProcessor.h"
#include "UiCommon.h"
#include "ModulePanels.h"
#include "PresetBar.h"

// Main window (1100 x 600), left to right:
//   INPUT   high-pass (+ Keep lows, slope), then the pre-saturation EQ as a curve (MID bell,
//           HIGH shelf, the HPF point; Link mirrors it after the tubes)
//   TUBES   tube window, Even / Density / Odd, Drive
//   OUTPUT  level + Auto, the envelope follower (a dotted lead shows it moves Drive)
//   MIX     Dry and Wet faders with their EQ / COMP modules
class GlowEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GlowEditor (GlowProcessor&);
    ~GlowEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // One path in the MIX section: fader + meter + EQ/COMP icons
    struct Strip
    {
        juce::String prefix, name;
        juce::Slider fader;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
        LevelMeter meter;
        ModuleIcon eqIcon { ModuleIcon::Kind::eq }, compIcon { ModuleIcon::Kind::comp };
        std::atomic<float>* eqOn = nullptr, * compOn = nullptr;
        SwapButton swap;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> swapAttach;
        ToggleParam mute, solo;
        std::atomic<float>* compFirst = nullptr, * muteP = nullptr, * soloP = nullptr;
        bool shownCompFirst = false, shownHeard = true;
    };
    void setupStrip (Strip&, const juce::String& prefix, const juce::String& name);
    void toggleParam (const juce::String& id);
    void layoutStrip (Strip&, juce::Rectangle<int>);
    void togglePanel (const juce::String& prefix, ModuleIcon::Kind);
    void closePanel();

    // small painted icon button (undo / redo)
    struct IconButton : public juce::Button
    {
        explicit IconButton (bool redoArrow) : juce::Button (redoArrow ? "Redo" : "Undo"), redo (redoArrow) {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        bool redo;
    };

    // a click-through layer for things drawn over the knobs (envelope dot, the dotted lead)
    struct Overlay : public juce::Component
    {
        std::function<void (juce::Graphics&)> draw;
        Overlay() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { if (draw) draw (g); }
    };
    void paintModDot (juce::Graphics&);
    void paintLead (juce::Graphics&);

    GlowProcessor& proc;
    GlowLookAndFeel lnf;

    // INPUT
    Knob hpf;
    ToggleParam keepLows, preEqLink, preEqOn;
    juce::TextButton slope12 { "12 dB" }, slope24 { "24 dB" };
    std::unique_ptr<juce::ParameterAttachment> slopeAttach;
    EqCurve preCurve;
    // TUBES
    Knob drive, even, odd, density;
    // OUTPUT
    Knob output, envAmount, envSpeed;
    ToggleParam autoGain;
    juce::Label autoReadout;
    float shownAuto = 1.0e9f;
    // MIX
    Strip dry, wet;
    // header
    PresetBar presetBar;
    juce::TextButton abA { "A" }, abB { "B" };
    IconButton undoButton { false }, redoButton { true };

    Overlay modDot, lead;
    std::atomic<float>* envAmtP = nullptr;
    float shownModDrive = -1.0f;
    juce::TooltipWindow tooltips { this, 3000 };   // hover help after ~3 s

    std::unique_ptr<ModulePanel> panel;           // the open EQ/COMP panel, if any
    juce::String panelPrefix;
    ModuleIcon::Kind panelKind = ModuleIcon::Kind::eq;

    juce::Rectangle<int> inputArea, tubeArea, outputArea, mixArea, overlayArea, glowArea, tubeWindow, cheatArea;
    float lastGlow = -1.0f;
    float glowLevel = 0.0f;   // 0..1, how hard the tubes are working right now (meter ballistics)
    juce::Image texture;      // faceplate grain, made once

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlowEditor)
};
