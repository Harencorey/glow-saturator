#pragma once
#include "PresetManager.h"
#include "UiCommon.h"

// Header bar:  [<]  [ Preset name *  v ]  [>]  [SAVE]
// Click the name for the menu (factory + user presets, Save As, Delete, Open Folder).
// Arrows step through every preset; "*" means the settings changed since it was loaded.
class PresetBar : public juce::Component
{
public:
    std::function<void()> onLoaded;   // a preset was loaded from this bar

    explicit PresetBar (PresetManager& pm) : presets (pm)
    {
        for (auto* b : { &prev, &next, &save }) addAndMakeVisible (b);
        addAndMakeVisible (nameButton);
        prev.setTooltip ("Previous preset");
        next.setTooltip ("Next preset");
        save.setTooltip ("Save the current settings as a preset");
        nameButton.setTooltip ("Presets");
        prev.onClick = [this] { presets.step (-1); refresh(); if (onLoaded) onLoaded(); };
        next.onClick = [this] { presets.step (+1); refresh(); if (onLoaded) onLoaded(); };
        save.onClick = [this] { askSaveAs(); };
        nameButton.onClick = [this] { showMenu(); };
        refresh();
    }

    // called from the editor's timer
    void refresh()
    {
        const auto text = presets.getCurrentName() + (presets.isModified() ? "  *" : "");
        if (text != nameButton.getButtonText()) nameButton.setButtonText (text);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        save.setBounds (r.removeFromRight (54));
        r.removeFromRight (10);
        prev.setBounds (r.removeFromLeft (28));
        next.setBounds (r.removeFromRight (28));
        nameButton.setBounds (r.reduced (6, 0));
    }

private:
    // the name field: dark box, centred name, small down-arrow
    struct NameButton : public juce::Button
    {
        NameButton() : juce::Button ("preset") {}
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (GlowLookAndFeel::well.brighter (over ? 0.12f : 0.0f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (GlowLookAndFeel::edge.brighter (over ? 0.35f : 0.15f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
            g.setColour (GlowLookAndFeel::text);
            g.setFont (juce::FontOptions (13.0f).withKerningFactor (0.04f));
            g.drawFittedText (getButtonText(), getLocalBounds().reduced (22, 0), juce::Justification::centred, 1);
            juce::Path tri;
            const float x = r.getRight() - 14.0f, y = r.getCentreY();
            tri.addTriangle (x - 4.0f, y - 2.0f, x + 4.0f, y - 2.0f, x, y + 3.0f);
            g.setColour (GlowLookAndFeel::textDim);
            g.fillPath (tri);
        }
    };

    void showMenu()
    {
        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());
        const auto current = presets.getCurrentName();
        const auto factory = presets.factoryNames();
        const auto user = presets.userNames();
        m.addSectionHeader ("FACTORY");
        for (int i = 0; i < factory.size(); ++i)
            m.addItem (1 + i, factory[i], true, factory[i] == current);
        m.addSectionHeader ("YOUR PRESETS");
        if (user.isEmpty()) m.addItem (-1, "(none yet - use SAVE)", false);
        for (int i = 0; i < user.size(); ++i)
            m.addItem (1000 + i, user[i], true, user[i] == current);
        m.addSeparator();
        m.addItem (2000, "Save As...");
        m.addItem (2001, "Delete \"" + current + "\"", presets.isUser (current));
        m.addItem (2002, "Show Presets Folder");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nameButton).withMinimumWidth (nameButton.getWidth()),
            [this, factory, user, sp = juce::Component::SafePointer<PresetBar> (this)] (int id)
            {
                if (sp == nullptr || id == 0) return;
                if (id >= 1 && id <= factory.size())             { presets.load (factory[id - 1]); if (onLoaded) onLoaded(); }
                else if (id >= 1000 && id < 1000 + user.size())  { presets.load (user[id - 1000]); if (onLoaded) onLoaded(); }
                else if (id == 2000)                              askSaveAs();
                else if (id == 2001)                              askDelete();
                else if (id == 2002)
                {
                    PresetManager::userFolder().createDirectory();
                    PresetManager::userFolder().revealToUser();
                }
                refresh();
            });
    }

    void askSaveAs()
    {
        auto* w = new juce::AlertWindow ("Save Preset", "Name for this preset:", juce::MessageBoxIconType::NoIcon, this);
        w->setLookAndFeel (&getLookAndFeel());
        const auto current = presets.getCurrentName();
        w->addTextEditor ("name", presets.isFactory (current) ? juce::String() : current);
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, w, sp = juce::Component::SafePointer<PresetBar> (this)] (int result)
            {
                if (sp == nullptr || result != 1) return;
                const auto name = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
                if (name.isEmpty()) return;
                if (presets.isFactory (name))
                {
                    message ("Save Preset", "\"" + name + "\" is a factory preset name. Please pick another name.", {}, nullptr);
                    return;
                }
                if (presets.isUser (name) && name != presets.getCurrentName())
                {
                    message ("Replace Preset?", "A preset called \"" + name + "\" already exists. Replace it?", "Replace",
                             [this, name] { presets.save (name); refresh(); });
                    return;
                }
                presets.save (name);
                refresh();
            }), true);   // deletes the window when done
        if (auto* ed = w->getTextEditor ("name"))
        {
            ed->setSelectAllWhenFocused (true);
            ed->grabKeyboardFocus();
        }
    }

    // A small dialog in the plugin's look. okText empty = just an OK button.
    void message (const juce::String& title, const juce::String& text, const juce::String& okText, std::function<void()> onOk)
    {
        auto* w = new juce::AlertWindow (title, text, juce::MessageBoxIconType::NoIcon, this);
        w->setLookAndFeel (&getLookAndFeel());
        w->addButton (okText.isEmpty() ? "OK" : okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
        if (okText.isNotEmpty()) w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create (
            [onOk, sp = juce::Component::SafePointer<PresetBar> (this)] (int r)
            { if (sp != nullptr && r == 1 && onOk) onOk(); }), true);
    }

    void askDelete()
    {
        const auto name = presets.getCurrentName();
        message ("Delete Preset?", "Delete \"" + name + "\"? Your current settings stay as they are.", "Delete",
                 [this, name] { presets.remove (name); refresh(); });
    }

    PresetManager& presets;
    juce::TextButton prev { "<" }, next { ">" }, save { "SAVE" };
    NameButton nameButton;
};
