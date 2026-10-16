// RU-8 panel: the readout, a recessed glass that never changes size: kit VFD (large) with its steps, BPM, set VFD (small) with its steps,
// the arranger keys A / B / fill / AUTO with lamps, bar.beat with four beat lamps. Everything is read from processor.display().
#pragma once
#include "Widgets.h"

namespace ru8ui
{
/** A transparent click area (the kit and set names open their list). */
class HitArea : public juce::Component, public juce::SettableTooltipClient
{
public:
    HitArea() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
};

class Readout : public juce::Component
{
public:
    explicit Readout (Ctx&);
    void refresh (const RU8AudioProcessor::Display&);        // one 30 Hz tick
    void paint (juce::Graphics&) override;
    void resized() override;

    PanelButton kitPrev, kitNext, setPrev, setNext, keyA, keyB, keyF, keyAuto;
    PanelButton srcKey;                                      // SET / MIDI (a host's; faint in the standalone)
    HitArea kitFace, setFace;
    juce::String setLabel() const;                           // "House · 124"
    float setNameX() const;                                  // where the set name starts (after the number or NEXT)
    void toggleSource();                                     // the SET / MIDI switch (one undo step)
    bool nextShown() const noexcept { return pending; }
    void moodChanged() { glass = {}; repaint(); }

private:
    void showKitList();
    void showSetList();

    Ctx& ctx;
    RU8AudioProcessor::Display d;
    bool pending = false, blinkPhase = false;
    juce::String lastKey;
    juce::Image glass;
    juce::String glassKey;
};
} // namespace ru8ui
