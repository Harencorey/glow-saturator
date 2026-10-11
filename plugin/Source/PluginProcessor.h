#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GlowDSP.h"

class GlowProcessor : public juce::AudioProcessor
{
public:
    GlowProcessor();
    ~GlowProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
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

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    GlowDSP dsp;
    int maxBlock = 512;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pEven = nullptr;
    std::atomic<float>* pOdd = nullptr;
    std::atomic<float>* pDensity = nullptr;
    std::atomic<float>* pHpf = nullptr;
    std::atomic<float>* pPreHz = nullptr;
    std::atomic<float>* pPreDb = nullptr;
    std::atomic<float>* pPostHz = nullptr;
    std::atomic<float>* pPostDb = nullptr;
    std::atomic<float>* pLink = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlowProcessor)
};
