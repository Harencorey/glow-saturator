#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GlowDSP.h"
#include "PresetManager.h"

class GlowProcessor : public juce::AudioProcessor
{
public:
    GlowProcessor();
    ~GlowProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Bypass keeps the same latency as processing, so toggling bypass never shifts the track
    // in time against other tracks (which would comb-filter on shared sources).
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlockBypassed;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::UndoManager undoManager;   // the editor's Undo / Redo (parameter changes)
    // editor view state, kept with the session (not parameters, not undoable): open sections + size
    bool uiInputOpen = false, uiMixerOpen = false;
    float uiScale = 0.9f;
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };

    // A/B compare: two complete settings slots (message thread). The current one is live in apvts.
    int getAB() const { return abSel; }
    void switchAB (int target)
    {
        if (target == abSel || target < 0 || target > 1) return;
        abSlot[(size_t) abSel] = apvts.copyState();
        if (abSlot[(size_t) target].isValid()) apvts.replaceState (abSlot[(size_t) target].createCopy());
        abSel = target;   // a fresh slot starts as a copy of the other one
        undoManager.clearUndoHistory();
    }

    // meters (message thread)
    float takeMasterPeak() { return dsp.takeMasterPeak(); }
    void setEqSolo (int path, int band) { dsp.setEqSolo (path, band); }
    float masterGainReductionDb() const { return dsp.masterGainReductionDb(); }
    float masterMakeupDb() const { return dsp.masterMakeupDb(); }
    float autoGainDb() const { return dsp.getAutoGainDb(); }
    float takeDrivenness() { return dsp.takeDrivenness(); }
    float modulatedDrive() const { return dsp.getModulatedDrive(); }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    GlowDSP dsp;
    int maxBlock = 512;
    std::array<juce::ValueTree, 2> abSlot;
    int abSel = 0;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> bypassDelay { 4096 };
    void feedBypassDelay (juce::AudioBuffer<float>&, bool writeOutput);
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pEven = nullptr;
    std::atomic<float>* pOdd = nullptr;
    std::atomic<float>* pDensity = nullptr;
    std::atomic<float>* pHpf = nullptr;
    std::atomic<float>* pHpf24 = nullptr, * pKeepLows = nullptr;
    std::atomic<float>* pOutput = nullptr, * pAuto = nullptr, * pEnvAmt = nullptr, * pEnvSpeed = nullptr;
    std::atomic<float>* pInput = nullptr, * pMix = nullptr;
    std::map<std::string, std::atomic<float>*> preEqP;

    // raw parameter pointers for one channel strip ("dry" or "wet")
    struct StripParams
    {
        std::atomic<float>* level = nullptr;
        std::atomic<float>* eqOn = nullptr;
        std::atomic<float>* f[4] {}, * g[4] {}, * q[4] {}, * bandOn[4] {};
        std::atomic<float>* lowShelf = nullptr, * highShelf = nullptr;
        std::atomic<float>* compOn = nullptr, * in = nullptr, * atk = nullptr, * rel = nullptr;
        std::atomic<float>* ratio = nullptr, * out = nullptr, * colour = nullptr, * hpf = nullptr, * mix = nullptr;
        std::atomic<float>* mute = nullptr, * solo = nullptr, * compFirst = nullptr, * compAuto = nullptr;
        void bind (juce::AudioProcessorValueTreeState&, const juce::String& prefix);
        void read (ChannelStrip::Settings&) const;
    };
    StripParams masterParams;

  public:
    // older sessions/presets: Dry/Wet levels -> Mix (+ Output), the Wet strip's EQ/Comp -> the
    // master's, Keep Lows off where it wasn't stored
    static void upgradeLegacyValues (std::map<juce::String, float>& realUnits);
  private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlowProcessor)
};
