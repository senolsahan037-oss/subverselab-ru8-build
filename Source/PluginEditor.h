// RU-8 panel: the Water edition (prototype/web/ru8-water.html) drawn with JUCE Graphics at 1180 x 823 logical units and scaled as
// one piece, 100 % - 200 % at a fixed aspect. Front and rear panel, five moods, help popovers, the water tank behind it all.
#pragma once
#include <JuceHeader.h>
#include <set>
#include "PluginProcessor.h"
#include "ui/UiCommon.h"
#include "ui/Front.h"
#include "ui/Rear.h"
#include "ui/Help.h"
#include "ui/Presets.h"

#ifndef RU8_LICENSE_REQUIRED
 #define RU8_LICENSE_REQUIRED 0
#endif
#if RU8_LICENSE_REQUIRED
 #include "ui/LicenseGate.h"
#endif

class RU8LookAndFeel : public juce::LookAndFeel_V4
{
public:
    RU8LookAndFeel();
    void useMood (const ru8ui::Palette&);
private:
    juce::SharedResourcePointer<ru8ui::Faces> faces;   // the embedded typefaces stay loaded while an editor is open
};

class RU8Editor : public juce::AudioProcessorEditor,
                  private juce::Timer,
                  private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit RU8Editor (RU8AudioProcessor&);
    ~RU8Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isDown) override;
    void mouseDown (const juce::MouseEvent&) override;

    // ---- for the snapshot tool
    static inline bool previewAsStandalone = false;     // a console tool is no standalone: draw its transport anyway
    void tickForSnapshot() { timerCallback(); }
    void stopTicking() { stopTimer(); }
    ru8ui::FrontPanel& frontPanel() { return front; }
    ru8ui::RearPanel& rearPanel() { return rear; }
    ru8ui::HelpOverlay& helpOverlay() { return help; }
    ru8ui::PresetList& presetList() { return presets; }
    void togglePresets();
    ru8ui::Ctx& context() { return ctx; }
    void showRear (bool);
    bool rearShowing() const noexcept { return rear.isVisible(); }
    void setMood (const juce::String&);
    void cycleMood();
    void toggleHelp();
    void closeHelp() { help.close(); }
    void setReducedMotion (bool);

    static constexpr int kW = ru8ui::kW, kH = ru8ui::kH;
    // the largest width for a display's user area: 200 % at most, and the whole window (with a host's title bar) on the screen;
    // never under 100 % (the panel's own size)
    static constexpr int kTitleBarRoom = 40;
    static int maxWidthFor (juce::Rectangle<int> userArea);
    void updateSizeLimits();                             // from the display the editor is on (or the main display)

private:
    void timerCallback() override;
    void parameterChanged (const juce::String&, float) override { ctx.armGlide(); }   // the product is about to write values
    bool playKey (const juce::KeyPress&);
    int padForKey (const juce::KeyPress&) const;

    RU8AudioProcessor& proc;
    ru8ui::Ctx ctx;
    RU8LookAndFeel lnf;
    ru8ui::FrontPanel front;
    ru8ui::RearPanel rear;
    ru8ui::HelpOverlay help;
    ru8ui::PresetList presets;
    juce::TooltipWindow tooltips { nullptr, 700 };
    std::set<int> heldKeys;
    int limitTicks = 0, presetListKey = -1;
#if RU8_LICENSE_REQUIRED
    ru8ui::LicenseCard license;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RU8Editor)
};
