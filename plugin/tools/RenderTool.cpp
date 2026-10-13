// GlowRender in.wav out.wav [name=value ...]
//   names: drive even odd density hpf preHz preDb postHz postDb out block
//   strips (prefix dry/wet): Level Eq F1-4 G1-4 Q1-4 LS HS Comp In Atk Rel Ratio Out Colour Hpf Mix Mute Solo Order(1=comp first)
//          e.g. wetEq=1 wetG2=4 wetComp=1 wetIn=12 wetRatio=7 dryLevel=-6
//   e.g.   GlowRender in.wav out.wav drive=60 even=80 hpf=120 postDb=-3
//   automation: at=<seconds> applies the settings after it at that time (block-accurate)
//          e.g. wetEq=1 wetG2=6 at=1.5 wetEq=0 at=3 wetEq=1
//   solo=1 / solo=2: output only the wet / dry path (both still processed) - for measurements
// Runs the plugin's DSP offline, in host-sized blocks, and writes a 32-bit float WAV.
// The output is latency-compensated so it lines up sample-for-sample with the input.
#include <juce_audio_formats/juce_audio_formats.h>
#include "GlowDSP.h"

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: GlowRender in.wav out.wav [drive= even= odd= density= hpf= preHz= preDb= postHz= postDb= mix= out= block=]\n");
        return 1;
    }
    juce::File in (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    GlowDSP::Params p;
    int block = 512, solo = 0, bsPath = 0, bsBand = -1;
    // settings before the first at= apply from the start; each at=<seconds> starts a new
    // automation point whose settings are applied on top of the previous ones at that time
    std::vector<std::pair<double, std::vector<juce::String>>> points { { 0.0, {} } };
    for (int a = 3; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        if (arg.startsWith ("at=")) points.push_back ({ arg.substring (3).getDoubleValue(), {} });
        else points.back().second.push_back (arg);
    }
    auto apply = [&p, &block, &solo, &bsPath, &bsBand] (const juce::String& arg) -> bool
    {
        const auto key = arg.upToFirstOccurrenceOf ("=", false, false);
        const float v = arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if      (key == "drive")   p.drive = v;
        else if (key == "even")    p.even = v;
        else if (key == "odd")     p.odd = v;
        else if (key == "density") p.density = v;
        else if (key == "hpf")     p.hpfHz = v;
        else if (key == "preHz")   p.preShelfHz = v;
        else if (key == "preDb")   p.preShelfDb = v;
        else if (key == "postHz")  p.postShelfHz = v;
        else if (key == "postDb")  p.postShelfDb = v;
        else if (key.startsWith ("dry") || key.startsWith ("wet"))
        {
            auto& st = key.startsWith ("dry") ? p.dry : p.wet;
            const auto k = key.substring (3);
            const int b = k.getLastCharacter() - '1';
            if      (k == "Level")  st.levelDb = v;
            else if (k == "Eq")     st.eq.on = v > 0.5f;
            else if (k.length() == 2 && k[0] == 'F' && b >= 0 && b < 4) st.eq.freq[b] = v;
            else if (k.length() == 2 && k[0] == 'G' && b >= 0 && b < 4) st.eq.gain[b] = v;
            else if (k.length() == 2 && k[0] == 'Q' && b >= 0 && b < 4) st.eq.q[b] = v;
            else if (k == "LS")     st.eq.lowShelf = v > 0.5f;
            else if (k == "HS")     st.eq.highShelf = v > 0.5f;
            else if (k == "Comp")   st.comp.on = v > 0.5f;
            else if (k == "In")     st.comp.inputDb = v;
            else if (k == "Atk")    st.comp.attackMs = v;
            else if (k == "Rel")    st.comp.releaseMs = v;
            else if (k == "Ratio")  st.comp.ratio = (int) v;
            else if (k == "Out")    st.comp.outputDb = v;
            else if (k == "Colour") st.comp.colour = (int) v;
            else if (k == "Hpf")    st.comp.detectorHpf = v > 0.5f;
            else if (k == "Mix")    st.comp.mix = v;
            else if (k == "Mute")   st.mute = v > 0.5f;
            else if (k == "Auto")   st.comp.autoMakeup = v > 0.5f;
            else if (k == "Solo")   st.solo = v > 0.5f;
            else if (k == "Order")  st.compFirst = v > 0.5f;   // 1 = Comp -> EQ
            else return false;
        }
        else if (key == "out")     p.outputDb = v;
        else if (key == "auto")    p.autoGain = v > 0.5f;
        else if (key == "link")    p.shelfLink = v > 0.5f;   // (also set postHz/postDb to mirror pre)
        else if (key == "env")     p.envAmount = v;
        else if (key == "envSpeed") p.envSpeed = v;
        else if (key == "peq")     p.preEq.on = v > 0.5f;
        else if (key == "peqLink") p.preEqLink = v > 0.5f;
        else if (key == "peqLF")   p.preEq.freq[0] = v;
        else if (key == "peqLG")   p.preEq.gain[0] = v;
        else if (key == "peqMF")   p.preEq.freq[1] = v;
        else if (key == "peqMG")   p.preEq.gain[1] = v;
        else if (key == "peqMQ")   p.preEq.q[1] = v;
        else if (key == "peqHF")   p.preEq.freq[3] = v;
        else if (key == "peqHG")   p.preEq.gain[3] = v;
        else if (key == "block")   block = juce::jmax (1, (int) v);
        else if (key == "solo")    solo = (int) v;   // 1 = wet path only, 2 = dry path only (both still run)
        else if (key == "bsPath")  bsPath = (int) v; // EQ band solo: path 1 dry, 2 wet, 3 pre ...
        else if (key == "bsBand")  bsBand = (int) v; // ... band 0..3 (-1 = off)
        else return false;
        return true;
    };
    for (auto& pt : points)
        for (auto& arg : pt.second)
            if (! apply (arg)) { std::printf ("unknown option %s\n", arg.toRawUTF8()); return 1; }
    p = GlowDSP::Params {};
    for (auto& arg : points.front().second) apply (arg);

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (in));
    if (! reader) { std::printf ("cannot read %s\n", argv[1]); return 1; }

    const int nch = (int) reader->numChannels;
    const int len = (int) reader->lengthInSamples;
    GlowDSP dsp;
    dsp.prepare (reader->sampleRate, block, nch);
    dsp.setParams (p);
    dsp.testSolo = solo;
    dsp.setEqSolo (bsPath, bsBand);
    const int lat = dsp.getLatencySamples();

    juce::AudioBuffer<float> buf (nch, len + lat + block);
    buf.clear();
    reader->read (&buf, 0, len, 0, true, true);

    const double t0 = juce::Time::getMillisecondCounterHiRes();
    size_t nextPoint = 1;
    double drivenSum = 0.0; float drivenMax = 0.0f; int drivenN = 0;
    for (int start = 0; start < buf.getNumSamples(); start += block)
    {
        while (nextPoint < points.size() && start >= (int) (points[nextPoint].first * reader->sampleRate))
        {
            for (auto& arg : points[nextPoint].second) apply (arg);
            dsp.setParams (p);
            ++nextPoint;
        }
        const int n = juce::jmin (block, buf.getNumSamples() - start);
        juce::dsp::AudioBlock<float> b (buf);
        auto sub = b.getSubBlock ((size_t) start, (size_t) n);
        dsp.process (sub);
        if (const float d = dsp.takeDrivenness(); d > 0.0f) { drivenSum += d; drivenMax = juce::jmax (drivenMax, d); ++drivenN; }
    }
    if (drivenN > 0)
        std::printf ("driven: mean %.1f dB, max %.1f dB (distortion re. signal)\n",
                     10.0 * std::log10 (drivenSum / drivenN + 1e-30), 10.0 * std::log10 (drivenMax + 1e-30));

    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    const double audioMs = 1000.0 * buf.getNumSamples() / reader->sampleRate;
    std::printf ("dsp time %.1f ms for %.1f s of audio = %.2f %% of one core\n", ms, audioMs / 1000.0, 100.0 * ms / audioMs);

    juce::AudioBuffer<float> res (nch, len);
    for (int c = 0; c < nch; ++c) res.copyFrom (c, 0, buf, c, lat, len);

    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (out.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), reader->sampleRate, (unsigned) nch, 32, {}, 0));
    if (! w) { std::printf ("cannot write\n"); return 1; }
    os.release();
    w->writeFromAudioSampleBuffer (res, 0, len);
    std::printf ("rendered %d samples, latency %d\n", len, lat);
    return 0;
}
