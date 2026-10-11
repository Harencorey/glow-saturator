#include "PluginProcessor.h"
#include "PluginEditor.h"

GlowProcessor::GlowProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GLOW", createLayout())
{
    pDrive = apvts.getRawParameterValue ("drive");
    pTone = apvts.getRawParameterValue ("tone");
    pDensity = apvts.getRawParameterValue ("density");
    pMix = apvts.getRawParameterValue ("mix");
    pOutput = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorValueTreeState::ParameterLayout GlowProcessor::createLayout()
{
    using namespace juce;
    auto pct = [] (float v, int) { return String (v, 0) + " %"; };
    auto db = [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; };
    auto bip = [] (float v, int) { return std::abs (v) < 0.05f ? String ("0") : (v > 0 ? "+" : "") + String (v, 0); };

    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive",
              NormalisableRange<float> (0.0f, 100.0f, 0.01f), 25.0f,
              AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 }, "Tone",
              NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
              AudioParameterFloatAttributes().withStringFromValueFunction (bip)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "density", 1 }, "Density",
              NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
              AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
              NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
              AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
              NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
              AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    return l;
}

bool GlowProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void GlowProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (1, samplesPerBlock);
    dsp.prepare (sampleRate, maxBlock, getTotalNumOutputChannels());
    setLatencySamples (dsp.getLatencySamples());
}

void GlowProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    GlowDSP::Params p;
    p.drive = pDrive->load();
    p.tone = pTone->load();
    p.density = pDensity->load();
    p.mix = pMix->load();
    p.outputDb = pOutput->load();
    dsp.setParams (p);

    // Some hosts occasionally send blocks larger than announced; process in chunks
    // so the oversampler never exceeds the size it was prepared for.
    juce::dsp::AudioBlock<float> block (buffer);
    for (size_t start = 0; start < block.getNumSamples(); start += (size_t) maxBlock)
    {
        auto sub = block.getSubBlock (start, juce::jmin ((size_t) maxBlock, block.getNumSamples() - start));
        dsp.process (sub);
    }
}

void GlowProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void GlowProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GlowProcessor(); }
