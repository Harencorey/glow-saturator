#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Presets: a few factory starting points built in, plus user presets saved as small XML files in
//   macOS:   ~/Library/Audio/Presets/Norka DSP/Glow/<name>.glowpreset
//   Windows: %APPDATA%/Norka DSP/Glow/Presets      Linux: ~/.config/Norka DSP/Glow/Presets
// (plain files, so they can be backed up, copied to another Mac or shared).
//
// A preset holds every parameter except Mute and Solo: those are monitoring switches, so
// loading a preset never silences a path you're listening to. Loading runs on the message
// thread and goes through the host (begin/end gesture), so it's undoable/automatable like a
// knob move; parameters missing from an older preset file fall back to their defaults.
class PresetManager
{
public:
    static inline const juce::String kExt = ".glowpreset";

    struct Factory
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;   // only what differs from the defaults
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : state (s) {}

    static juce::File userFolder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Audio/Presets/Norka DSP/Glow");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Norka DSP/Glow/Presets");
       #endif
    }

    // Factory starting points. Values are in each parameter's real units (dB, Hz, %, index).
    static const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> f {
            { "Default", {} },
            { "Subtle Warmth", { { "drive", 15 }, { "even", 62 }, { "odd", 40 } } },
            { "Tube Bus Glue", { { "drive", 30 }, { "density", 55 }, { "autoGain", 1 },
                                 { "wetCompOn", 1 }, { "wetCompInput", 6 }, { "wetCompRatio", 1 }, { "wetCompAttack", 10 },
                                 { "wetCompRelease", 300 }, { "wetCompAuto", 1 } } },
            { "Vocal Air & Grit", { { "drive", 35 }, { "even", 65 }, { "hpf", 90 }, { "keepLows", 1 },
                                    { "preEqOn", 1 }, { "preEqFreq4", 6000 }, { "preEqGain4", 4 }, { "preEqLink", 1 },
                                    { "autoGain", 1 },
                                    { "wetCompOn", 1 }, { "wetCompInput", 8 }, { "wetCompRatio", 2 }, { "wetCompAttack", 5 },
                                    { "wetCompRelease", 120 }, { "wetCompAuto", 1 } } },
            { "Bass Weight", { { "drive", 45 }, { "even", 80 }, { "odd", 30 }, { "density", 60 },
                               { "dryLevel", -6 }, { "autoGain", 1 } } },
            { "Parallel Drum Smash", { { "drive", 70 }, { "odd", 65 }, { "hpf", 40 }, { "dryLevel", 0 }, { "wetLevel", -6 },
                                       { "autoGain", 1 }, { "wetCompOn", 1 }, { "wetCompInput", 18 }, { "wetCompRatio", 4 },
                                       { "wetCompAttack", 3 }, { "wetCompRelease", 150 }, { "wetCompColour", 1 },
                                       { "wetCompAuto", 1 } } },
            { "Lo-Fi Crush", { { "drive", 90 }, { "odd", 85 }, { "density", 80 }, { "hpf", 150 }, { "autoGain", 1 },
                               { "wetEqOn", 1 }, { "wetEqFreq4", 6000 }, { "wetEqGain4", -8 },
                               { "wetCompOn", 1 }, { "wetCompInput", 12 }, { "wetCompRatio", 7 }, { "wetCompColour", 2 },
                               { "wetCompMix", 60 }, { "wetCompAuto", 1 } } },
        };
        return f;
    }

    // All preset names in menu order: factory first, then user (alphabetical).
    juce::StringArray factoryNames() const
    {
        juce::StringArray n;
        for (auto& f : factory()) n.add (f.name);
        return n;
    }
    juce::StringArray userNames() const
    {
        juce::StringArray n;
        for (auto& f : userFolder().findChildFiles (juce::File::findFiles, false, "*" + kExt))
            n.add (f.getFileNameWithoutExtension());
        n.sortNatural();
        return n;
    }
    juce::StringArray allNames() const { auto a = factoryNames(); a.addArray (userNames()); return a; }

    bool isFactory (const juce::String& name) const { return factoryNames().contains (name); }
    bool isUser (const juce::String& name) const { return fileFor (name).existsAsFile(); }

    juce::String getCurrentName() const { return state.state.getProperty (kPresetProp, "Default").toString(); }

    // True when any parameter differs from the preset as loaded/saved.
    bool isModified()
    {
        if (pendingSnapshot.exchange (false)) takeSnapshot();
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! isExcluded (rp->getParameterID()))
                {
                    const auto it = snapshot.find (rp->getParameterID());
                    const float ref = it != snapshot.end() ? it->second : rp->getDefaultValue();
                    if (std::abs (rp->getValue() - ref) > 1.0e-4f) return true;
                }
        return false;
    }

    bool load (const juce::String& name)
    {
        std::map<juce::String, float> values;   // real units
        if (isUser (name))
        {
            auto xml = juce::XmlDocument::parse (fileFor (name));
            if (xml == nullptr || ! xml->hasTagName ("GlowPreset")) return false;
            for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
        }
        else
        {
            const Factory* f = nullptr;
            for (auto& x : factory()) if (name == x.name) f = &x;
            if (f == nullptr) return false;
            for (auto& [id, v] : f->values) values[id] = v;
        }

        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto id = rp->getParameterID();
                if (isExcluded (id)) continue;
                const auto it = values.find (id);
                const float norm = it != values.end() ? rp->convertTo0to1 (rp->getNormalisableRange().snapToLegalValue (it->second))
                                                      : rp->getDefaultValue();
                if (std::abs (rp->getValue() - norm) > 1.0e-6f)
                {
                    rp->beginChangeGesture();
                    rp->setValueNotifyingHost (norm);
                    rp->endChangeGesture();
                }
            }
        setCurrent (name);
        return true;
    }

    // Saves the current settings as a user preset (overwrites one with the same name).
    bool save (const juce::String& rawName)
    {
        const auto name = juce::File::createLegalFileName (rawName.trim());
        if (name.isEmpty()) return false;
        juce::XmlElement xml ("GlowPreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("plugin", "Glow");
        xml.setAttribute ("version", JucePlugin_VersionString);
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! isExcluded (rp->getParameterID()))
                {
                    auto* e = xml.createNewChildElement ("PARAM");
                    e->setAttribute ("id", rp->getParameterID());
                    e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
                }
        userFolder().createDirectory();
        if (! xml.writeTo (fileFor (name))) return false;
        setCurrent (name);
        return true;
    }

    bool remove (const juce::String& name)
    {
        if (! isUser (name) || ! fileFor (name).deleteFile()) return false;
        if (getCurrentName() == name) state.state.setProperty (kPresetProp, "Default", nullptr);   // settings stay as they are
        return true;
    }

    // Step through all presets (wraps around). dir = +1 / -1.
    void step (int dir)
    {
        const auto all = allNames();
        if (all.isEmpty()) return;
        const int i = all.indexOf (getCurrentName());
        load (all[(juce::jmax (0, i) + dir + all.size()) % all.size()]);
    }

    // After the host restores a session (any thread): those settings become the "unmodified"
    // reference. The snapshot itself is taken on the message thread, next time the UI asks.
    void sessionRestored() { pendingSnapshot = true; }

private:
    static constexpr const char* kPresetProp = "presetName";

    static bool isExcluded (const juce::String& id) { return id.endsWith ("Mute") || id.endsWith ("Solo"); }

    juce::File fileFor (const juce::String& name) const { return userFolder().getChildFile (name + kExt); }

    void setCurrent (const juce::String& name)
    {
        state.state.setProperty (kPresetProp, name, nullptr);   // saved with the session
        takeSnapshot();
    }

    void takeSnapshot()
    {
        snapshot.clear();
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                snapshot[rp->getParameterID()] = rp->getValue();
    }

    juce::AudioProcessorValueTreeState& state;
    std::map<juce::String, float> snapshot;   // normalised values when the current preset was loaded/saved
    std::atomic<bool> pendingSnapshot { true };
};
