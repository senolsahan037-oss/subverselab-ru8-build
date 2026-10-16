// RU-8 panel: the rear panel (same size as the front): Color (tape, dust, crush), MIDI (note map, trigger notes, the note table), Out (stereo),
// and the motion setting. The front's controls are inert while it shows.
#pragma once
#include "Widgets.h"

namespace ru8ui
{
class RearPanel : public juce::Component
{
public:
    RearPanel (Ctx&, std::function<void()> onFlip, std::function<void()> onReducedMotion);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    void moodChanged();

    KnobCell* findKnob (const juce::String& id) const;
    std::vector<std::unique_ptr<KnobCell>> knobs;
    std::unique_ptr<PanelButton> gmBtn, padsBtn, motionBtn, flipBack;
    std::unique_ptr<LockupLink> lockup;

private:
    Ctx& ctx;
    std::function<void()> motionCb;
};
} // namespace ru8ui
