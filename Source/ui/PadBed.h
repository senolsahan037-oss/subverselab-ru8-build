// RU-8 panel: the pad bed (4 x 4, pad 1 bottom left) with the tools row. A pad shows its glyph at rest and its 16 steps on hover.
#pragma once
#include "Widgets.h"

namespace ru8ui
{
struct PadHost
{
    virtual ~PadHost() = default;
    virtual void padPressed (int pad, float velocity) = 0;     // play it, select it for the CHANNEL rail
    virtual void stepClicked (int pad, int step) = 0;          // cycle the step in the Song
};

class PadComponent : public juce::Component
{
public:
    PadComponent (Ctx&, PadHost&, int enginePad);
    int pad() const noexcept { return index; }
    void flash (float velocity);                               // lights with the velocity, fades with the voice's own length
    void setHeld (bool on) { if (held != on) { held = on; repaint(); } }
    void tick();                                               // 30 Hz: the flash, and state changes that need a repaint
    void setStepsShown (bool on) { forceSteps = on; repaint(); } // tests: as if the pointer were over the pad
    bool stepsShown() const noexcept { return hover || forceSteps; }
    juce::Rectangle<float> stepCell (int i) const;
    juce::Rectangle<float> nameRow() const;
    juce::Rectangle<float> stepArea() const;
    float flashAlpha() const noexcept { return glowNow; }
    void forgetLayers() { backImg = {}; frontImg = {}; }

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; held = false; hoverCell = -1; repaint(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override { return juce::Rectangle<int> (0, 0, layout::padSize, layout::padSize + 3).contains (x, y); }

    static constexpr int kBodyH = layout::padSize;

private:
    void paintBack (juce::Graphics&, bool heldLook) const;
    void paintFront (juce::Graphics&, bool steps) const;
    juce::Image render (std::function<void (juce::Graphics&)>) const;
    int cellAt (juce::Point<float>) const;
    juce::String stateKey() const;

    Ctx& ctx;
    PadHost& host;
    int index;
    bool hover = false, held = false, forceSteps = false, mouseHeld = false;
    int hoverCell = -1, downCell = -1;
    double flashStart = 0; float flashA0 = 0, glowNow = 0; double flashDur = 1;
    juce::String lastKey;
    juce::Image backImg, frontImg;
    juce::String backImgKey, frontImgKey;
};

class PadBed : public juce::Component
{
public:
    PadBed (Ctx&, PadHost&);
    PadComponent& padAt (int enginePad) { return *pads[(size_t) enginePad]; }
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick() { for (auto& p : pads) p->tick(); }
    juce::Rectangle<int> padRect (int enginePad) const;      // in the bed's coordinates
    // the tools row (created by the editor, laid out here)
    PanelButton* dragSolo = nullptr; PanelButton* dragFull = nullptr; PanelButton* save = nullptr;
    PanelButton* clrSec = nullptr; PanelButton* clrRow = nullptr; PanelButton* copySec = nullptr; PanelButton* copyPad = nullptr; PanelButton* pastePad = nullptr;
    PanelButton* presetPrev = nullptr; PanelButton* presetKey = nullptr; PanelButton* presetNext = nullptr;   // the factory preset selector
    void layoutTools();
    void moodChanged() { cache = {}; for (auto& p : pads) p->forgetLayers(); repaint(); }
    std::vector<std::unique_ptr<PanelButton>> tools;

private:
    Ctx& ctx;
    void paintStatic (juce::Graphics&);
    std::array<std::unique_ptr<PadComponent>, 16> pads;
    juce::Image cache;
    juce::String cacheKey;
};
} // namespace ru8ui
