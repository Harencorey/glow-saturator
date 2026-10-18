#pragma once
#include "PluginProcessor.h"
#include "UiCommon.h"
#include "ModulePanels.h"
#include "PresetBar.h"

// Main window. Compact by default:
//   HPF  |  tube meter, EVEN / DENSITY / ODD, DRIVE with ENV AMOUNT / ENV SPEED  |  MIX + master EQ/COMP
//   with INPUT / OUTPUT / AUTO along the bottom right.
// INPUT expands (to the left part of the window) into the full pre-saturation EQ, MIX expands into the
// Dry/Wet mixer; each widens the window instead of covering anything. The EQ / COMP editors open in a
// drawer under the main controls.
//
// Everything is laid out in fixed "design" units and the whole body is scaled by the interface size
// (right-click the background), so it stays crisp at any size.
class GlowEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GlowEditor (GlowProcessor&);
    ~GlowEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // the scaled content: all controls live in here
    struct Body : public juce::Component
    {
        std::function<void (juce::Graphics&)> draw;
        std::function<void (const juce::MouseEvent&)> onMouseDown;
        void paint (juce::Graphics& g) override { if (draw) draw (g); }
        void mouseDown (const juce::MouseEvent& e) override { if (onMouseDown) onMouseDown (e); }
    };

    void toggleParam (const juce::String& id);
    void togglePanel (const juce::String& prefix, ModuleIcon::Kind);
    void closePanel();

    // small painted buttons
    struct IconButton : public juce::Button
    {
        explicit IconButton (bool redoArrow) : juce::Button (redoArrow ? "Redo" : "Undo"), redo (redoArrow) {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        bool redo;
    };
    struct ChevronButton : public juce::Button   // expand (down chevron) / collapse (up chevron)
    {
        ChevronButton() : juce::Button ("expand") {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        bool expanded = false;
    };
    struct LampButton : public juce::Button      // a section's title lamp that switches it (pre-sat EQ on)
    {
        LampButton() : juce::Button ("lamp") { setClickingTogglesState (true); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };

    // a click-through layer for things drawn over the knobs (envelope dot, the dotted leads)
    struct Overlay : public juce::Component
    {
        std::function<void (juce::Graphics&)> draw;
        Overlay() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { if (draw) draw (g); }
    };
    void paintBody (juce::Graphics&);
    void paintModDot (juce::Graphics&);
    void paintLead (juce::Graphics&);

    // view state: which sections are open, interface size (kept in the session, not parameters)
    void setExpanded (bool input, bool mixer);
    void applyLayout();      // design size -> window size, and lays everything out
    void showSizeMenu();
    juce::Rectangle<int> designSize() const;

    GlowProcessor& proc;
    GlowLookAndFeel lnf;
    Body body;
    bool inputOpen = false;
    static constexpr bool mixerOpen = false;   // no expanded mixer any more: Mix + master only
    float uiScale = 0.9f;
    // design-unit geometry (scaled by uiScale): one height for every view, sections add width
    static constexpr int kMargin = 14, kGap = 12, kTop = 54, kCardH = 312, kMainH = 446,
                         kInputW = 240, kInputOpenW = 548, kCentreW = 440, kMixW = 360, kMixOpenW = 580;

    // INPUT
    Knob hpf;
    ChevronButton inputExpand;
    ToggleParam keepLows, preEqLink;
    LampButton preEqLamp;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> preEqLampAttach;
    juce::TextButton slope12 { "12" }, slope24 { "24" };
    std::unique_ptr<juce::ParameterAttachment> slopeAttach;
    EqCurve preCurve;
    Knob preFreq, preQ;
    // SATURATION
    Knob drive, even, odd, density, envAmount, envSpeed;
    // MIX & MASTER
    Knob mix;
    ModuleIcon masterEq { ModuleIcon::Kind::eq, ModuleIcon::Look::wide }, masterComp { ModuleIcon::Kind::comp, ModuleIcon::Look::wide };
    SwapButton masterSwap;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> masterSwapAttach;
    std::atomic<float>* masterEqOn = nullptr, * masterCompOn = nullptr, * masterCompFirst = nullptr;
    bool shownMasterCompFirst = false;
    // bottom strip
    Knob input, output;
    ToggleParam autoGain;
    // header
    PresetBar presetBar;
    juce::TextButton abA { "A" }, abB { "B" };
    IconButton undoButton { false }, redoButton { true };

    Overlay modDot, lead;
    std::atomic<float>* envAmtP = nullptr, * hpfP = nullptr, * mixP = nullptr;
    float shownModDrive = -1.0f;
    float shownAutoDb = 1.0e9f;
    juce::TooltipWindow tooltips { this, 3000 };   // hover help after ~3 s

    std::unique_ptr<ModulePanel> panel;           // the open EQ/COMP editor, if any
    juce::String panelPrefix;
    ModuleIcon::Kind panelKind = ModuleIcon::Kind::eq;

    // layout results (design units)
    int mainH = 256;
    juce::Rectangle<int> leftCard, centreCard, mixCard, tubeWindow, glowArea, bottomArea, drawerArea;
    float lastGlow = -1.0f;
    float glowLevel = 0.0f;   // 0..1, how hard the tubes are working right now (meter ballistics)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlowEditor)
};
