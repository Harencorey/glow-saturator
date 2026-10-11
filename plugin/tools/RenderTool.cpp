// GlowRender in.wav out.wav [name=value ...]
//   names: drive even odd density hpf preHz preDb postHz postDb mix out block
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
        else if (key == "mix")     p.mix = v;
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

    for (int start = 0; start < buf.getNumSamples(); start += block)
    {
        const int n = juce::jmin (block, buf.getNumSamples() - start);
        juce::dsp::AudioBlock<float> b (buf);
        auto sub = b.getSubBlock ((size_t) start, (size_t) n);
        dsp.process (sub);
    }

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
