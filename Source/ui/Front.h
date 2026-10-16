// RU-8 panel: the front (1180 x 823 logical units): readout, pad bed, rails, tools, mood / flip keys and the lockup, over the water.
#pragma once
#include "Widgets.h"
#include "PadBed.h"
#include "Readout.h"
#include "Water.h"

namespace ru8ui
{
class FrontPanel : public juce::Component, private PadHost
{
public:
    FrontPanel (Ctx&, std::function<void()> onFlip, std::function<void()> onMood, std::function<void()> onHelp,
                std::function<void()> onPresets, std::function<bool()> presetsOpen);
    void stepPreset (int d);                               // the preset arrows: the previous / next of the 28, without the list
    ~FrontPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();                                           // one 30 Hz tick: processor.display() -> everything that is lit
    void moodChanged();

    // what the keyboard and the keys do
    void playPad (int pad, float velocity);                // play it and select it
    void select (int pad);
    void copyPadSteps();
    void pastePadSteps();
    void clearSection();
    void clearRow();
    void copySection();
    void startStop();
    void tap();
    bool clipboardFull() const noexcept { return clipboard.isNotEmpty(); }
    juce::File writeClipFile (bool full, int* notes = nullptr);   // the clip of what plays, in a temporary file (drag-out)
    void dragClip (bool full);                             // drag-out; a clip with notes arms SET -> MIDI
    void saveClips();                                      // the save arrow: full and solo, next to each other

    // parts (the tests and the help read them)
    WaterTank water;
    Readout readout;
    PadBed bed;
    KnobCell* findKnob (const juce::String& paramId) const;
    std::vector<KnobCell*> stripKnobs, masterKnobs, spaceKnobs, echoKnobs;
    std::unique_ptr<KnobCell> tempoKnob;
    std::unique_ptr<PanelButton> startBtn, tapBtn, helpBtn, muteBtn, soloBtn, resetBtn, moodBtn, flipBtn;
    std::vector<std::unique_ptr<PanelButton>> spaceSeg, echoSeg;
    std::unique_ptr<LockupLink> lockup;
    std::vector<std::unique_ptr<KnobCell>> knobs;
    float meterDb = -100.0f, grDb = 0.0f;
    int meterLit() const;
    juce::String meterText() const;
    juce::String grText() const;
    juce::Rectangle<int> meterBounds() const;

private:
    class Plate;
    void buildPlates();
    void updateWaterMask();
    void padPressed (int pad, float velocity) override { playPad (pad, velocity); }
    void stepClicked (int pad, int step) override;
    KnobCell& addKnob (const KnobCell::Spec&, juce::Rectangle<int> cell, std::vector<KnobCell*>* group);
    void flashPad (int pad, float velocity);
    void rebindStrip (bool glide);
    void allButtons (const std::function<void (PanelButton&)>&);

    Ctx& ctx;
    std::vector<std::unique_ptr<Plate>> plates;
    Plate* channelHead = nullptr; Plate* meterPlate = nullptr;
    juce::String lastMeterKey;
    std::function<void()> flipCb, moodCb, helpCb, presetsCb;
    std::function<bool()> presetsOpenFn;
    juce::String clipboard;
    juce::uint32 seenHits = 0;
    int lastStep = -1, lastKit = -1;
    bool lastPlaying = false;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace ru8ui
