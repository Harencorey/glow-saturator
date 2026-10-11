#include "PluginProcessor.h"
#include "PluginEditor.h"

GlowProcessor::GlowProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GLOW", createLayout())
{
    pDrive = apvts.getRawParameterValue ("drive");
    pEven = apvts.getRawParameterValue ("even");
    pOdd = apvts.getRawParameterValue ("odd");
    pDensity = apvts.getRawParameterValue ("density");
    pHpf = apvts.getRawParameterValue ("hpf");
    pPreHz = apvts.getRawParameterValue ("preShelfFreq");
    pPreDb = apvts.getRawParameterValue ("preShelfGain");
    pPostHz = apvts.getRawParameterValue ("postShelfFreq");
    pPostDb = apvts.getRawParameterValue ("postShelfGain");
    pLink = apvts.getRawParameterValue ("shelfLink");
    pMix = apvts.getRawParameterValue ("mix");
    pOutput = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorValueTreeState::ParameterLayout GlowProcessor::createLayout()
{
    using namespace juce;
    auto pct = [] (float v, int) { return String (v, 0) + " %"; };
    auto db = [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (v, 0) + " Hz"; };
    auto hpfText = [hz] (float v, int len) { return v <= 10.5f ? String ("Off") : hz (v, len); };

    auto freqRange = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi, 0.1f);
        r.setSkewForCentre (centre);
        return r;
    };

    auto addFloat = [] (AudioProcessorValueTreeState::ParameterLayout& l, const char* id, const char* name,
                        NormalisableRange<float> range, float def, std::function<String (float, int)> text)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                   AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
    };

    AudioProcessorValueTreeState::ParameterLayout l;
    // Drive is the macro; Even/Odd trim each side of it (50 % = as measured on the reference)
    addFloat (l, "drive", "Drive", { 0.0f, 100.0f, 0.01f }, 25.0f, [] (float v, int) { return String (v, 1); });
    addFloat (l, "even", "Even", { 0.0f, 100.0f, 0.1f }, 50.0f, pct);
    addFloat (l, "odd", "Odd", { 0.0f, 100.0f, 0.1f }, 50.0f, pct);
    addFloat (l, "density", "Density", { 0.0f, 100.0f, 0.1f }, 50.0f, pct);
    // pre-saturation EQ
    addFloat (l, "hpf", "HPF", freqRange (10.0f, 1000.0f, 100.0f), 10.0f, hpfText);
    addFloat (l, "preShelfFreq", "Pre Shelf Freq", freqRange (1000.0f, 16000.0f, 5000.0f), 5000.0f, hz);
    addFloat (l, "preShelfGain", "Pre Shelf Gain", { -12.0f, 12.0f, 0.01f }, 0.0f, db);
    // post-saturation EQ
    addFloat (l, "postShelfFreq", "Post Shelf Freq", freqRange (1000.0f, 16000.0f, 5000.0f), 5000.0f, hz);
    addFloat (l, "postShelfGain", "Post Shelf Gain", { -12.0f, 12.0f, 0.01f }, 0.0f, db);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "shelfLink", 1 }, "Shelf Link", false));
    // output
    addFloat (l, "mix", "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
    addFloat (l, "output", "Output", { -24.0f, 12.0f, 0.01f }, 0.0f, db);
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
    p.even = pEven->load();
    p.odd = pOdd->load();
    p.density = pDensity->load();
    p.hpfHz = pHpf->load();
    p.preShelfHz = pPreHz->load();
    p.preShelfDb = pPreDb->load();
    // Link: post shelf mirrors the pre shelf (same frequency, opposite gain), so the
    // highs hit the tubes harder/softer but the overall tonal balance stays put
    const bool link = pLink->load() > 0.5f;
    p.postShelfHz = link ? p.preShelfHz : pPostHz->load();
    p.postShelfDb = link ? -p.preShelfDb : pPostDb->load();
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
