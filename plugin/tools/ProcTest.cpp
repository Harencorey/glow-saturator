// Drives the real GlowProcessor (as a host would) to check timing: an impulse must come out
// at the reported latency both when processing and when bypassed, including across a toggle.
#include "PluginProcessor.h"

static int impulsePosition (const juce::AudioBuffer<float>& b, int ch)
{
    int best = -1; float mx = 0.0f;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (ch, i)) > mx) { mx = std::abs (b.getSample (ch, i)); best = i; }
    return best;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    bool ok = true;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        GlowProcessor p;
        const int block = 256;
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        const int lat = p.getLatencySamples();

        auto run = [&] (bool bypassed, int impulseAt)
        {
            juce::AudioBuffer<float> all (2, block * 8);
            all.clear();
            all.setSample (0, impulseAt, 0.5f); all.setSample (1, impulseAt, 0.5f);
            juce::MidiBuffer m;
            for (int s = 0; s < all.getNumSamples(); s += block)
            {
                juce::AudioBuffer<float> b (all.getArrayOfWritePointers(), 2, s, block);
                if (bypassed) p.processBlockBypassed (b, m); else p.processBlock (b, m);
            }
            return impulsePosition (all, 0) - impulseAt;
        };
        // warm up both paths, then measure: processing, bypassed, processing again
        run (false, 100); run (true, 100);
        const int dProc = run (false, 300), dByp = run (true, 300), dProc2 = run (false, 300);
        const bool good = dProc == lat && dByp == lat && dProc2 == lat;
        ok &= good;
        std::printf ("%6.0f Hz: reported latency %d | impulse delay processing %d, bypassed %d, processing again %d  %s\n",
                     sr, lat, dProc, dByp, dProc2, good ? "OK" : "MISMATCH");

        // stereo: identical L/R in -> identical L/R out through the full processor
        juce::AudioBuffer<float> b (2, block);
        juce::Random r (7);
        for (int i = 0; i < block; ++i) { const float v = r.nextFloat() - 0.5f; b.setSample (0, i, v); b.setSample (1, i, v); }
        juce::MidiBuffer m;
        p.processBlock (b, m);
        float diff = 0.0f;
        for (int i = 0; i < block; ++i) diff = juce::jmax (diff, std::abs (b.getSample (0, i) - b.getSample (1, i)));
        std::printf ("          L/R identical through processor: max |L-R| = %g  %s\n", diff, diff <= 0.0f ? "OK" : "MISMATCH");
        ok &= diff <= 0.0f;
        p.releaseResources();
    }
    // ---- presets ----
    {
        auto check = [&ok] (bool c, const char* what) { std::printf ("  %-62s %s\n", what, c ? "OK" : "FAIL"); ok &= c; };
        std::printf ("presets:\n");
        GlowProcessor p;
        auto& pm = p.presets;
        auto val = [&p] (const char* id) { auto* rp = p.apvts.getParameter (id); return rp->convertFrom0to1 (rp->getValue()); };
        auto set = [&p] (const char* id, float v) { auto* rp = p.apvts.getParameter (id); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); };

        // every factory preset only names real parameters, and loading applies its values
        bool allIds = true, applied = true;
        for (auto& f : PresetManager::factory())
        {
            for (auto& [id, v] : f.values)
                if (p.apvts.getParameter (id) == nullptr) { std::printf ("    unknown id %s in %s\n", id, f.name); allIds = false; }
            pm.load (f.name);
            for (auto& [id, v] : f.values)
                if (auto* rp = p.apvts.getParameter (id); rp != nullptr && std::abs (rp->convertFrom0to1 (rp->getValue()) - v) > 0.05f)
                { std::printf ("    %s: %s = %.2f, want %.2f\n", f.name, id, rp->convertFrom0to1 (rp->getValue()), v); applied = false; }
            if (pm.isModified()) applied = false;
        }
        check (allIds, "factory presets only use existing parameter IDs");
        check (applied, "factory presets apply their values and read as unmodified");

        pm.load ("Default");
        check (! pm.isModified() && std::abs (val ("drive") - 25.0f) < 1e-3f, "Default = plugin defaults");

        // user preset round trip
        const juce::String name = "ProcTest Temp Preset";
        set ("drive", 63.0f); set ("wetEqOn", 1.0f); set ("wetEqGain2", -7.5f); set ("hpf", 120.0f); set ("dryCompRatio", 5.0f);
        check (pm.isModified(), "changing a knob marks the preset modified");
        check (pm.save (name) && pm.isUser (name) && pm.getCurrentName() == name && ! pm.isModified(), "save writes a user preset and makes it current");
        pm.load ("Default");
        check (val ("drive") < 25.01f && val ("wetEqGain2") > -0.01f, "loading Default resets everything");
        pm.load (name);
        check (std::abs (val ("drive") - 63.0f) < 0.02f && std::abs (val ("wetEqGain2") + 7.5f) < 0.02f
               && std::abs (val ("hpf") - 120.0f) < 0.2f && std::abs (val ("dryCompRatio") - 5.0f) < 0.01f, "user preset loads back exactly");

        // session save/restore keeps the preset name and its "unmodified" state
        juce::MemoryBlock mb; p.getStateInformation (mb);
        GlowProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.presets.getCurrentName() == name && ! q.presets.isModified(), "session recall keeps the preset name, unmodified");

        check (pm.remove (name) && ! pm.isUser (name), "delete removes the user preset");

        // HPF: Off at the bottom, then 20 Hz; an old session's 10 Hz (Off) stays Off
        auto* h = p.apvts.getParameter ("hpf");
        check (std::abs (h->convertFrom0to1 (0.0f) - GlowDSP::kHpfOffHz) < 1e-3f && h->getText (0.0f, 10) == "Off", "HPF bottom of the knob = Off");
        check (std::abs (h->getNormalisableRange().snapToLegalValue (19.7f) - 20.0f) < 1e-3f
               && std::abs (h->getNormalisableRange().snapToLegalValue (19.2f) - GlowDSP::kHpfOffHz) < 1e-3f, "HPF snaps to Off or 20 Hz, nothing in between");
        check (h->getText (h->convertTo0to1 (20.0f), 10) == "20 Hz", "HPF first step reads 20 Hz");
        auto xml = p.apvts.copyState().createXml();
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM")) if (e->getStringAttribute ("id") == "hpf") e->setAttribute ("value", 10.0);
        GlowProcessor r; juce::MemoryBlock mb2; juce::AudioProcessor::copyXmlToBinary (*xml, mb2); r.setStateInformation (mb2.getData(), (int) mb2.getSize());
        check (r.apvts.getParameter ("hpf")->getText (r.apvts.getParameter ("hpf")->getValue(), 10) == "Off", "old session with HPF at 10 Hz loads as Off");

        // a session saved before the Mix knob (v0.1.26): Dry -6 / Wet 0 summed, a Wet comp, no Keep Lows
        //  -> Mix 66.6 % + Output +3.53 dB (the same blend and level), the Wet comp kept, Keep Lows off
        auto oldSession = [] (std::function<void (juce::XmlElement&)> edit)
        {
            GlowProcessor fresh;
            auto old = fresh.apvts.copyState().createXml();
            juce::Array<juce::XmlElement*> drop;
            for (auto* e : old->getChildWithTagNameIterator ("PARAM"))
            {
                const auto id = e->getStringAttribute ("id");
                if (id == "mix" || id == "keepLows" || id == "inputGain") drop.add (e);
            }
            for (auto* e : drop) old->removeChildElement (e, true);
            edit (*old);
            auto t = std::make_unique<GlowProcessor>();
            t->apvts.getParameter ("output")->setValueNotifyingHost (0.9f);   // must not leak through
            juce::MemoryBlock m3; juce::AudioProcessor::copyXmlToBinary (*old, m3); t->setStateInformation (m3.getData(), (int) m3.getSize());
            return t;
        };
        auto addParam = [] (juce::XmlElement& x, const char* id, double value)
        {
            auto* e = x.createNewChildElement ("PARAM"); e->setAttribute ("id", id); e->setAttribute ("value", value);
        };
        {
            GlowProcessor fresh;
            check (fresh.apvts.getRawParameterValue ("keepLows")->load() > 0.5f
                   && std::abs (fresh.apvts.getRawParameterValue ("mix")->load() - 100.0f) < 1e-3f, "new instance: Keep Lows on, Mix 100 %");
            auto t = oldSession ([&] (juce::XmlElement& x)
            {
                addParam (x, "dryLevel", -6.0); addParam (x, "wetLevel", 0.0);
                for (auto* e : x.getChildWithTagNameIterator ("PARAM"))
                {
                    if (e->getStringAttribute ("id") == "wetCompOn") e->setAttribute ("value", 1.0);
                    if (e->getStringAttribute ("id") == "wetCompInput") e->setAttribute ("value", 9.0);
                }
                for (auto* e : x.getChildWithTagNameIterator ("PARAM")) if (e->getStringAttribute ("id") == "hpf") e->setAttribute ("value", 120.0);
            });
            auto v = [&t] (const char* id) { return t->apvts.getRawParameterValue (id)->load(); };
            const float d = juce::Decibels::decibelsToGain (-6.0f), w = 1.0f;
            const float g = juce::Decibels::decibelsToGain (v ("output"));
            check (std::abs (g * (1.0f - v ("mix") * 0.01f) - d) < 2e-3f && std::abs (g * v ("mix") * 0.01f - w) < 2e-3f,
                   (juce::String ("old session Dry -6 / Wet 0 -> Mix ") + juce::String (v ("mix"), 1) + " %, Output +"
                    + juce::String (v ("output"), 2) + " dB (same blend and level)").toRawUTF8());
            check (v ("keepLows") < 0.5f, "old session without Keep Lows loads with it off");
            check (v ("wetCompOn") > 0.5f && std::abs (v ("wetCompInput") - 9.0f) < 1e-3f && v ("dryEqOn") < 0.5f,
                   "old session: the Wet comp stays on");
            check (std::abs (v ("inputGain")) < 1e-6f, "old session: input 0 dB");
        }
        {   // a session saved by this version comes back exactly: no legacy conversion on reload
            GlowProcessor a;
            auto setp = [&a] (const char* id, float value) { auto* pa = a.apvts.getParameter (id); pa->setValueNotifyingHost (pa->convertTo0to1 (value)); };
            setp ("mix", 50.0f); setp ("output", 3.0f); setp ("keepLows", 0.0f); setp ("inputGain", 5.0f);
            setp ("wetCompOn", 1.0f); setp ("dryEqGain2", -4.0f); setp ("hpf", 150.0f);
            juce::MemoryBlock mbA; a.getStateInformation (mbA);
            GlowProcessor b; b.setStateInformation (mbA.getData(), (int) mbA.getSize());
            bool same = true;
            for (auto* prm : a.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    same = same && std::abs (rp->getValue() - b.apvts.getParameter (rp->getParameterID())->getValue()) < 1.0e-6f;
            check (same, "session saved by this version loads back unchanged");
            bool noOld = true;
            for (auto* prm : a.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                {
                    const auto id = rp->getParameterID();
                    noOld = noOld && ! (id == "dryLevel" || id == "wetLevel" || id.endsWith ("Mute") || id.endsWith ("Solo") || id.startsWith ("master"));
                }
            check (noOld, "removed parameters are really gone (no dead automation lanes)");
        }
        {   // a default v0.1.26 session (Dry Off): Mix 100 %, Output unchanged
            auto t = oldSession ([&] (juce::XmlElement& x) { addParam (x, "dryLevel", -60.0); addParam (x, "wetLevel", 0.0); });
            check (std::abs (t->apvts.getRawParameterValue ("mix")->load() - 100.0f) < 1e-3f
                   && std::abs (t->apvts.getRawParameterValue ("output")->load()) < 1e-4f, "old default session: Mix 100 %, Output 0 dB");
        }
    }

    // ---- Drive glow meter ----
    {
        std::printf ("drive glow meter (distortion re. clean signal, dB):\n");
        auto measure = [] (float drive, float levelDb)
        {
            GlowProcessor p;
            p.setPlayConfigDetails (2, 2, 48000.0, 512);
            p.prepareToPlay (48000.0, 512);
            auto* rp = p.apvts.getParameter ("drive"); rp->setValueNotifyingHost (rp->convertTo0to1 (drive));
            juce::Random rnd (7);
            juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
            float mx = 0.0f;
            for (int k = 0; k < 200; ++k)
            {
                const float a = juce::Decibels::decibelsToGain (levelDb) * 1.7f;
                for (int i = 0; i < 512; ++i) { const float v = a * (rnd.nextFloat() * 2.0f - 1.0f); b.setSample (0, i, v); b.setSample (1, i, v); }
                if (levelDb < -200.0f) b.clear();
                p.processBlock (b, m);
                if (k > 20) mx = juce::jmax (mx, p.takeDrivenness());
            }
            return mx > 0.0f ? 10.0f * std::log10 (mx) : -999.0f;
        };
        const float quiet = measure (10.0f, -30.0f), mid = measure (25.0f, -18.0f), hot = measure (90.0f, -12.0f), silent = measure (90.0f, -999.0f);
        std::printf ("  drive 10 @ -30 dBFS: %.1f   drive 25 @ -18: %.1f   drive 90 @ -12: %.1f   silence: %s\n",
                     quiet, mid, hot, silent < -900.0f ? "none" : "SIGNAL?!");
        const bool good = quiet < mid && mid < hot && hot > -6.0f && quiet < -30.0f && silent < -900.0f;
        std::printf ("  ordering and range %s\n", good ? "OK" : "FAIL");
        ok &= good;
    }

    std::printf (ok ? "ALL OK\n" : "FAILURES\n");
    return ok ? 0 : 1;
}
