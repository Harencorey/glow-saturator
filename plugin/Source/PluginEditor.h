#pragma once
#include "PluginProcessor.h"
#include "UiCommon.h"
#include "ModulePanels.h"
#include "PresetBar.h"

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
    void layoutStrip (Strip&, juce::Rectangle<int>);
    void togglePanel (const juce::String& prefix, ModuleIcon::Kind);
    void closePanel();

    GlowProcessor& proc;
    GlowLookAndFeel lnf;
    Knob drive, even, odd, density;
    Knob hpf, preFreq, preGain;
    Knob postFreq, postGain, output;
    juce::TextButton linkButton { "LINK" };
    ToggleParam autoGain;            // master auto gain (+ readout of what it's applying)
    juce::Label autoReadout;
    float shownAuto = 1.0e9f;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttach;
    Strip dry, wet;
    PresetBar presetBar;

    std::unique_ptr<ModulePanel> panel;           // the open EQ/COMP panel, if any
    juce::String panelPrefix;
    ModuleIcon::Kind panelKind = ModuleIcon::Kind::eq;

    juce::Rectangle<int> preArea, tubeArea, postArea, mixArea, overlayArea, glowArea;
    float lastGlow = -1.0f;
    float glowLevel = 0.0f;   // 0..1, how hard the tubes are working right now (meter ballistics)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlowEditor)
};
