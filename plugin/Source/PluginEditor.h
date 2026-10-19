#pragma once
#include "PluginProcessor.h"
#include "UiCommon.h"
#include "ModulePanels.h"
#include "PresetBar.h"

// Main window. Compact by default:
//   HPF  |  tube meter, EVEN / DENSITY / ODD, DRIVE with ENV AMOUNT / ENV SPEED  |  DRY EQ/COMP, MIX, WET EQ/COMP
//   with IN / OUT meters and INPUT / OUTPUT / AUTO along the bottom.
// The HPF card expands (widening the window, covering nothing) into the full pre-saturation EQ.
// The EQ / COMP editors open in a drawer under the main controls.
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
    void applyLayout();      // design size -> window size, and lays everything out
    void showSizeMenu();
    juce::Rectangle<int> designSize() const;

    GlowProcessor& proc;
    GlowLookAndFeel lnf;
    Body body;
    float uiScale = 0.9f;
    // design-unit geometry (scaled by uiScale): one height for every view, sections add width
    static constexpr int kMargin = 14, kGap = 12, kTop = 54, kCardH = 306, kMainH = 440,
                         kInputW = 380, kCentreW = 440, kMixW = 380;
    static constexpr int kLine = 188;   // HPF, Drive and Mix knobs share this line: the middle of each card below its title

    // INPUT
    Knob hpf;
    ToggleParam keepLows, preEqLink;
    LampButton preEqLamp;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> preEqLampAttach;
    juce::TextButton slope6 { "6" }, slope12 { "12" }, slope24 { "24" }, slope36 { "36" };   // HPF slope, dB/oct
    // drive type selector (bottom strip, under Drive)
    juce::TextButton typeBtn[5] { juce::TextButton ("TUBE"), juce::TextButton ("IRON"), juce::TextButton ("TAPE"),
                                  juce::TextButton ("DIODE"), juce::TextButton ("CLIP") };
    std::unique_ptr<juce::ParameterAttachment> typeAttach;
    void showType (int t);   // highlights the button and sets the type-specific help text
    std::unique_ptr<juce::ParameterAttachment> slopeAttach;
    EqCurve preCurve;
    // SATURATION
    Knob drive, even, odd, density, envAmount, envSpeed;
    // MIX & MASTER
    Knob mix;
    // each path's EQ / COMP (either side of the Mix knob) and their order
    struct PathMods
    {
        juce::String prefix;
        ModuleIcon eq { ModuleIcon::Kind::eq, ModuleIcon::Look::wide }, comp { ModuleIcon::Kind::comp, ModuleIcon::Look::wide };
        SwapButton swap;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> swapAttach;
        std::atomic<float>* eqOn = nullptr, * compOn = nullptr, * compFirst = nullptr;
        bool shownCompFirst = false;
    };
    PathMods dryMods, wetMods;
    void setupPath (PathMods&, const juce::String& prefix);
    int drawerH() const { return panel != nullptr && panelKind == ModuleIcon::Kind::eq ? 372 : 300; }   // EQ: room for a big curve
    // input / output meters (bottom strip)
    StereoMeter inMeter { "IN" }, outMeter { "OUT", true };
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
