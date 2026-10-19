#include "PluginProcessor.h"
#include "PluginEditor.h"

GlowProcessor::GlowProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "GLOW", createLayout())
{
    pDrive = apvts.getRawParameterValue ("drive");
    pEven = apvts.getRawParameterValue ("even");
    pOdd = apvts.getRawParameterValue ("odd");
    pDensity = apvts.getRawParameterValue ("density");
    pHpf = apvts.getRawParameterValue ("hpf");
    pHpf24 = apvts.getRawParameterValue ("hpfSlope");
    pKeepLows = apvts.getRawParameterValue ("keepLows");
    pOutput = apvts.getRawParameterValue ("output");
    pAuto = apvts.getRawParameterValue ("autoGain");
    pEnvAmt = apvts.getRawParameterValue ("envAmount");
    pEnvSpeed = apvts.getRawParameterValue ("envSpeed");
    pInput = apvts.getRawParameterValue ("inputGain");
    pMix = apvts.getRawParameterValue ("mix");
    for (auto id : { "preEqOn", "preEqLink", "preEqFreq2", "preEqGain2", "preEqQ2", "preEqFreq4", "preEqGain4", "preEqQ4",
                     "preEqBand2On", "preEqBand4On" })
        preEqP[id] = apvts.getRawParameterValue (id);
    dryParams.bind (apvts, "dry");
    wetParams.bind (apvts, "wet");
    presets.upgrade = [] (std::map<juce::String, float>& v) { upgradeLegacyValues (v); };
}

void GlowProcessor::StripParams::bind (juce::AudioProcessorValueTreeState& s, const juce::String& p)
{
    auto get = [&s, &p] (const juce::String& id) { auto* v = s.getRawParameterValue (p + id); jassert (v != nullptr); return v; };
    eqOn = get ("EqOn");
    for (int b = 0; b < 4; ++b)
    {
        f[b] = get ("EqFreq" + juce::String (b + 1));
        g[b] = get ("EqGain" + juce::String (b + 1));
        q[b] = get ("EqQ" + juce::String (b + 1));
        bandOn[b] = get ("EqBand" + juce::String (b + 1) + "On");
    }
    lowShelf = get ("EqLowShelf");
    highShelf = get ("EqHighShelf");
    compOn = get ("CompOn");
    in = get ("CompInput");
    atk = get ("CompAttack");
    rel = get ("CompRelease");
    ratio = get ("CompRatio");
    out = get ("CompOutput");
    colour = get ("CompColour");
    hpf = get ("CompHpf");
    mix = get ("CompMix");
    compAuto = get ("CompAuto");
    compFirst = get ("CompFirst");
}

void GlowProcessor::StripParams::read (ChannelStrip::Settings& st) const
{
    st.levelDb = 0.0f;   // each path's level is the Mix's job
    st.eq.on = eqOn->load() > 0.5f;
    for (int b = 0; b < 4; ++b)
    {
        st.eq.freq[b] = f[b]->load(); st.eq.gain[b] = g[b]->load(); st.eq.q[b] = q[b]->load();
        st.eq.bandOn[b] = bandOn[b]->load() > 0.5f;
    }
    st.eq.lowShelf = lowShelf->load() > 0.5f;
    st.eq.highShelf = highShelf->load() > 0.5f;
    st.comp.on = compOn->load() > 0.5f;
    st.comp.inputDb = in->load();
    st.comp.attackMs = atk->load();
    st.comp.releaseMs = rel->load();
    st.comp.ratio = (int) std::lround (ratio->load());
    st.comp.outputDb = out->load();
    st.comp.colour = (int) std::lround (colour->load());
    st.comp.detectorHpf = hpf->load() > 0.5f;
    st.comp.mix = mix->load();
    st.comp.autoMakeup = compAuto->load() > 0.5f;
    st.compFirst = compFirst->load() > 0.5f;
}

juce::AudioProcessorValueTreeState::ParameterLayout GlowProcessor::createLayout()
{
    using namespace juce;
    auto pct = [] (float v, int) { return String (v, 0) + " %"; };
    auto db = [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (v, 0) + " Hz"; };
    auto hpfText = [hz] (float v, int len) { return v < GlowDSP::kHpfMinHz - 0.5f ? String ("Off") : hz (v, len); };

    auto freqRange = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi, 0.1f);
        r.setSkewForCentre (centre);
        return r;
    };

    auto addFloat = [] (AudioProcessorValueTreeState::ParameterLayout& l, const std::string& id, const String& name,
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
    // HPF: Off at the very bottom, then 20 Hz - 1 kHz (skewed so 100 Hz sits mid-travel).
    // Nothing between Off and 20 Hz: values snap to one or the other.
    {
        constexpr float lo = GlowDSP::kHpfOffHz, hi = 1000.0f;
        const float skew = std::log (0.5f) / std::log ((100.0f - lo) / (hi - lo));
        NormalisableRange<float> r (lo, hi,
            [skew] (float s, float e, float p) { return s + (e - s) * std::exp (std::log (juce::jmax (p, 1.0e-9f)) / skew); },
            [skew] (float s, float e, float v) { return std::pow (juce::jlimit (0.0f, 1.0f, (v - s) / (e - s)), skew); },
            [] (float s, float e, float v)
            {
                if (v < GlowDSP::kHpfMinHz - 0.5f) return s;   // Off
                return juce::jlimit (GlowDSP::kHpfMinHz, e, std::round (v * 10.0f) / 10.0f);
            });
        addFloat (l, "hpf", "HPF", r, GlowDSP::kHpfOffHz, hpfText);
    }
    // HPF slope (24 dB/oct Linkwitz-Riley, or a gentler 12 dB/oct) and Keep lows (what it removes
    // skips the tubes and comes back clean)
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "hpfSlope", 1 }, "HPF 24 dB", true,
               AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return v ? String ("24 dB/oct") : String ("12 dB/oct"); })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "keepLows", 1 }, "Keep Lows", true));   // on for new instances
    // output
    addFloat (l, "output", "Output", { -24.0f, 24.0f, 0.01f }, 0.0f, db);
    // input trim, before everything (drives the tubes harder; Auto Gain compensates it)
    addFloat (l, "inputGain", "Input", { -24.0f, 24.0f, 0.01f }, 0.0f, db);
    // dry/wet: linear crossfade (0 % = clean, 100 % = saturated)
    addFloat (l, "mix", "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
    // keeps the output as loud as the input (slow, K-weighted), so moves don't fool your ears
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "autoGain", 1 }, "Auto Gain", false));

    // envelope follower: moves Drive with the input's dynamics
    addFloat (l, "envAmount", "Envelope Amount", { -100.0f, 100.0f, 0.1f }, 0.0f,
              [] (float v, int) { return std::abs (v) < 0.05f ? String ("Off") : (v > 0.0f ? "+" : "") + String (v, 0) + " %"; });
    addFloat (l, "envSpeed", "Envelope Speed", { 0.0f, 100.0f, 0.1f }, 50.0f,
              [] (float v, int) { return v < 25.0f ? "Slow " + String (v, 0) : v > 75.0f ? "Fast " + String (v, 0) : String (v, 0); });

    // pre-saturation EQ: mid bell + high shelf (the HPF covers the bottom), + Link
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "preEqOn", 1 }, "Pre EQ On", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "preEqLink", 1 }, "Pre EQ Link", false));
    addFloat (l, "preEqFreq2", "Pre EQ Mid Freq", freqRange (40.0f, 12000.0f, 1000.0f), 1000.0f, hz);
    addFloat (l, "preEqGain2", "Pre EQ Mid Gain", { -15.0f, 15.0f, 0.01f }, 0.0f, db);
    {
        NormalisableRange<float> qr (0.1f, 10.0f, 0.001f);
        qr.setSkewForCentre (1.0f);
        auto qt = [] (float v, int) { return String (v, 2); };
        addFloat (l, "preEqQ2", "Pre EQ Mid Q", qr, 0.7f, qt);
        // High shelf Q: the shape of the corner (0.71 = plain shelf, higher = a bump/dip at the
        // turnover like a classic passive shelf). Cmd-drag the H point.
        addFloat (l, "preEqQ4", "Pre EQ High Q", qr, 0.7f, qt);
    }
    addFloat (l, "preEqFreq4", "Pre EQ High Freq", freqRange (1000.0f, 20000.0f, 5000.0f), 6000.0f, hz);
    addFloat (l, "preEqGain4", "Pre EQ High Gain", { -15.0f, 15.0f, 0.01f }, 0.0f, db);
    // per-band bypass (double-click a point on the curve)
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "preEqBand2On", 1 }, "Pre EQ Mid On", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "preEqBand4On", 1 }, "Pre EQ High On", true));

    // each path's EQ + compressor (before the Mix)
    auto ms = [] (float v, int) { return v < 1.0f ? String (v, 2) + " ms" : v < 1000.0f ? String (v, v < 10.0f ? 1 : 0) + " ms" : String (v / 1000.0f, 2) + " s"; };
    auto qText = [] (float v, int) { return String (v, 2); };
    for (const String p : { "dry", "wet" })
    {
        const String P = p == "dry" ? "Dry " : "Wet ";
        auto id = [&p] (const String& s) { return (p + s).toStdString(); };

        l.add (std::make_unique<AudioParameterBool> (ParameterID { p + "EqOn", 1 }, P + "EQ On", false));
        const float defF[4] { 80.0f, 400.0f, 2500.0f, 10000.0f }, defQ[4] { 0.7f, 1.0f, 1.0f, 0.7f };
        for (int b = 0; b < 4; ++b)
        {
            const String n (b + 1);
            addFloat (l, id ("EqFreq" + n).c_str(), (P + "EQ " + n + " Freq").toRawUTF8(), freqRange (20.0f, 20000.0f, 1000.0f), defF[b], hz);
            addFloat (l, id ("EqGain" + n).c_str(), (P + "EQ " + n + " Gain").toRawUTF8(), { -18.0f, 18.0f, 0.01f }, 0.0f, db);
            NormalisableRange<float> qr (0.1f, 10.0f, 0.001f);
            qr.setSkewForCentre (1.0f);
            addFloat (l, id ("EqQ" + n).c_str(), (P + "EQ " + n + " Q").toRawUTF8(), qr, defQ[b], qText);
        }
        l.add (std::make_unique<AudioParameterBool> (ParameterID { p + "EqLowShelf", 1 }, P + "EQ 1 Shelf", true));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { p + "EqHighShelf", 1 }, P + "EQ 4 Shelf", true));
        for (int b = 0; b < 4; ++b)   // per-band bypass (double-click a point on the curve)
            l.add (std::make_unique<AudioParameterBool> (ParameterID { p + "EqBand" + String (b + 1) + "On", 1 },
                                                          P + "EQ " + String (b + 1) + " On", true));

        l.add (std::make_unique<AudioParameterBool> (ParameterID { p + "CompOn", 1 }, P + "Comp On", false));
        addFloat (l, id ("CompInput").c_str(), (P + "Comp Input").toRawUTF8(), { -12.0f, 36.0f, 0.01f }, 0.0f, db);
        NormalisableRange<float> atk (0.05f, 30.0f, 0.01f);  atk.setSkewForCentre (2.0f);
        NormalisableRange<float> rel (50.0f, 3500.0f, 1.0f); rel.setSkewForCentre (300.0f);
        addFloat (l, id ("CompAttack").c_str(), (P + "Comp Attack").toRawUTF8(), atk, 5.0f, ms);
        addFloat (l, id ("CompRelease").c_str(), (P + "Comp Release").toRawUTF8(), rel, 200.0f, ms);
        StringArray ratios;
        for (auto* r : Compressor::ratioNames) ratios.add (r);
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { p + "CompRatio", 1 }, P + "Comp Ratio", ratios, 3));
        addFloat (l, id ("CompOutput").c_str(), (P + "Comp Output").toRawUTF8(), { -24.0f, 24.0f, 0.01f }, 0.0f, db);
        StringArray colours;
        for (auto* c : Compressor::colourNames) colours.add (c);
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { p + "CompColour", 1 }, P + "Comp Colour", colours, 0));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { p + "CompHpf", 1 }, P + "Comp Detector HPF", false));
        // parallel compression: blend of uncompressed signal (same path, same phase)
        // signal order inside the path: EQ -> Comp (default) or Comp -> EQ
        l.add (std::make_unique<AudioParameterBool> (ParameterID { p + "CompFirst", 1 }, P + "Comp Before EQ", false));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { p + "CompAuto", 1 }, P + "Comp Auto Makeup", false));
        addFloat (l, id ("CompMix").c_str(), (P + "Comp Mix").toRawUTF8(), { 0.0f, 100.0f, 0.1f }, 100.0f, pct);
    }
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

    bypassDelay.prepare ({ sampleRate, (juce::uint32) maxBlock, (juce::uint32) juce::jmax (1, getTotalNumOutputChannels()) });
    bypassDelay.setMaximumDelayInSamples (dsp.getLatencySamples() + 1);
    bypassDelay.setDelay ((float) dsp.getLatencySamples());
    bypassDelay.reset();
}

// Always keeps the bypass delay line fed with the input, so switching bypass on or off is
// seamless (the delayed signal is already there). writeOutput = true while bypassed.
void GlowProcessor::feedBypassDelay (juce::AudioBuffer<float>& buffer, bool writeOutput)
{
    const int nch = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());
    for (int c = 0; c < nch; ++c)
    {
        auto* x = buffer.getWritePointer (c);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            bypassDelay.pushSample (c, x[i]);
            const float d = bypassDelay.popSample (c);
            if (writeOutput) x[i] = d;
        }
    }
}

void GlowProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());
    feedBypassDelay (buffer, true);
}

void GlowProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());
    feedBypassDelay (buffer, false);

    GlowDSP::Params p;
    p.drive = pDrive->load();
    p.even = pEven->load();
    p.odd = pOdd->load();
    p.density = pDensity->load();
    p.hpfHz = pHpf->load();
    p.hpf24 = pHpf24->load() > 0.5f;
    p.keepLows = pKeepLows->load() > 0.5f;
    // Link: post shelf mirrors the pre shelf (same frequency, opposite gain), so the
    // highs hit the tubes harder/softer but the overall tonal balance stays put
    p.outputDb = pOutput->load();
    p.autoGain = pAuto->load() > 0.5f;
    p.envAmount = pEnvAmt->load();
    p.envSpeed = pEnvSpeed->load();
    p.inputDb = pInput->load();
    p.mix = pMix->load();
    {
        auto v = [this] (const char* id) { return preEqP[id]->load(); };
        auto& e = p.preEq;
        e.on = v ("preEqOn") > 0.5f;
        e.freq[0] = 100.0f;           e.gain[0] = 0.0f;              e.q[0] = 0.7f;   // unused (the HPF covers the lows)
        e.freq[1] = v ("preEqFreq2"); e.gain[1] = v ("preEqGain2"); e.q[1] = v ("preEqQ2");
        e.freq[2] = 2500.0f;          e.gain[2] = 0.0f;              e.q[2] = 1.0f;   // unused band
        e.freq[3] = v ("preEqFreq4"); e.gain[3] = v ("preEqGain4"); e.q[3] = v ("preEqQ4");
        e.lowShelf = false;
        e.highShelf = true;
        e.bandOn[0] = false;
        e.bandOn[2] = false;
        e.bandOn[1] = v ("preEqBand2On") > 0.5f;
        e.bandOn[3] = v ("preEqBand4On") > 0.5f;
        p.preEqLink = v ("preEqLink") > 0.5f;
    }
    dryParams.read (p.dry);
    wetParams.read (p.wet);
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
    {
        xml->setAttribute ("uiInput", uiInputOpen ? 1 : 0);
        xml->setAttribute ("uiScale", (double) uiScale);
        copyXmlToBinary (*xml, dest);
    }
}

void GlowProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            uiInputOpen = xml->getIntAttribute ("uiInput", 0) != 0;
            uiScale = (float) juce::jlimit (0.7, 1.5, xml->getDoubleAttribute ("uiScale", 0.9));
            xml->removeAttribute ("uiInput"); xml->removeAttribute ("uiMixer"); xml->removeAttribute ("uiScale");
            auto tree = juce::ValueTree::fromXml (*xml);
            {   // older sessions: convert, and give every parameter the file doesn't mention its default
                std::map<juce::String, float> v;
                for (const auto& c : tree)
                    if (c.hasType ("PARAM")) v[c.getProperty ("id").toString()] = (float) c.getProperty ("value");
                const auto before = v;
                upgradeLegacyValues (v);
                for (auto* prm : getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                        if (v.find (rp->getParameterID()) == v.end())
                            v[rp->getParameterID()] = rp->convertFrom0to1 (rp->getDefaultValue());
                for (auto& [id, val] : v)
                {
                    const auto it = before.find (id);
                    if (it != before.end() && juce::exactlyEqual (it->second, val)) continue;
                    auto c = tree.getChildWithProperty ("id", id);
                    if (! c.isValid()) { c = juce::ValueTree ("PARAM"); c.setProperty ("id", id, nullptr); tree.appendChild (c, nullptr); }
                    c.setProperty ("value", val, nullptr);
                }
            }
            apvts.replaceState (tree);
            presets.sessionRestored();
        }
}

void GlowProcessor::upgradeLegacyValues (std::map<juce::String, float>& v)
{
    const bool hasFaders = v.count ("dryLevel") || v.count ("wetLevel");
    const bool hasMix = v.find ("mix") != v.end();
    // drop what no longer exists (the faders/mute/solo are folded in below; a master EQ/comp
    // only existed in test builds). Sessions and presets saved by this version contain none of
    // these, so they pass through untouched.
    auto get = [&v] (const char* id, float def) { auto it = v.find (id); return it != v.end() ? it->second : def; };
    const float dryDb = get ("dryLevel", ChannelStrip::kOffDb), wetDb = get ("wetLevel", 0.0f);
    const bool dryMute = get ("dryMute", 0.0f) > 0.5f, wetMute = get ("wetMute", 0.0f) > 0.5f;
    const bool drySolo = get ("drySolo", 0.0f) > 0.5f, wetSolo = get ("wetSolo", 0.0f) > 0.5f;
    for (auto it = v.begin(); it != v.end();)
    {
        const auto& id = it->first;
        const bool gone = id == "dryLevel" || id == "wetLevel" || id.endsWith ("Mute") || id.endsWith ("Solo") || id.startsWith ("master");
        it = gone ? v.erase (it) : std::next (it);
    }
    if (! hasFaders && hasMix) return;   // saved by this version: nothing else to do

    // Keep Lows used to default to off (only versions without the Mix knob)
    if (! hasMix && v.find ("keepLows") == v.end()) v["keepLows"] = 0.0f;

    // Dry/Wet levels (independent faders, summed; later trims after the Mix) -> the same blend as
    // Mix + an overall gain. With Auto Gain on that gain is left to Auto Gain, as it was before.
    auto lin = [] (float db) { return db <= ChannelStrip::kOffDb ? 0.0f : juce::Decibels::decibelsToGain (db); };
    float d = lin (dryDb), w = lin (wetDb);
    if (hasMix) { const float m = juce::jlimit (0.0f, 1.0f, get ("mix", 100.0f) * 0.01f); d *= 1.0f - m; w *= m; }
    if (dryMute || (wetSolo && ! drySolo)) d = 0.0f;
    if (wetMute || (drySolo && ! wetSolo)) w = 0.0f;
    float mix = 100.0f, gainDb = 0.0f;
    GlowDSP::legacyMix (d, w, mix, gainDb);
    v["mix"] = mix;
    if (get ("autoGain", 0.0f) < 0.5f && std::abs (gainDb) > 1.0e-4f)
        v["output"] = juce::jlimit (-24.0f, 12.0f, get ("output", 0.0f) + gainDb);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GlowProcessor(); }
