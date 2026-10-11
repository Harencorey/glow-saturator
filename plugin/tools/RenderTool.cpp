// GlowRender in.wav out.wav [drive tone density mix outputDb] [blockSize]
// Runs the plugin's DSP offline, in host-sized blocks, and writes a 32-bit float WAV.
// The output is latency-compensated so it lines up sample-for-sample with the input.
#include <juce_audio_formats/juce_audio_formats.h>
#include "GlowDSP.h"

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: GlowRender in.wav out.wav [drive tone density mix outDb] [block]\n");
        return 1;
    }
    juce::File in (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    GlowDSP::Params p;
    if (argc > 3) p.drive = (float) std::atof (argv[3]);
    if (argc > 4) p.tone = (float) std::atof (argv[4]);
    if (argc > 5) p.density = (float) std::atof (argv[5]);
    if (argc > 6) p.mix = (float) std::atof (argv[6]);
    if (argc > 7) p.outputDb = (float) std::atof (argv[7]);
    const int block = argc > 8 ? std::atoi (argv[8]) : 512;

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
