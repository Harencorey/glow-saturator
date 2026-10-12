// GlowRender in.wav out.wav [name=value ...]
//   names: drive even odd density hpf preHz preDb postHz postDb out block
//   strips (prefix dry/wet): Level Eq F1-4 G1-4 Q1-4 LS HS Comp In Atk Rel Ratio Out Colour Hpf
//          e.g. wetEq=1 wetG2=4 wetComp=1 wetIn=12 wetRatio=7 dryLevel=-6
//   e.g.   GlowRender in.wav out.wav drive=60 even=80 hpf=120 postDb=-3
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
    int block = 512;
    for (int a = 3; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
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
            else { std::printf ("unknown option %s\n", argv[a]); return 1; }
        }
        else if (key == "out")     p.outputDb = v;
        else if (key == "block")   block = juce::jmax (1, (int) v);
        else { std::printf ("unknown option %s\n", argv[a]); return 1; }
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (in));
    if (! reader) { std::printf ("cannot read %s\n", argv[1]); return 1; }

    const int nch = (int) reader->numChannels;
    const int len = (int) reader->lengthInSamples;
    GlowDSP dsp;
    dsp.prepare (reader->sampleRate, block, nch);
    dsp.setParams (p);
    const int lat = dsp.getLatencySamples();

    juce::AudioBuffer<float> buf (nch, len + lat + block);
    buf.clear();
    reader->read (&buf, 0, len, 0, true, true);

    const double t0 = juce::Time::getMillisecondCounterHiRes();
    for (int start = 0; start < buf.getNumSamples(); start += block)
    {
        const int n = juce::jmin (block, buf.getNumSamples() - start);
        juce::dsp::AudioBlock<float> b (buf);
        auto sub = b.getSubBlock ((size_t) start, (size_t) n);
        dsp.process (sub);
    }

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
