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
    std::printf (ok ? "ALL OK\n" : "FAILURES\n");
    return ok ? 0 : 1;
}
