// RU-8 panel: the water tank, the body of the machine behind every control (a port of the prototype's canvas).
// Nothing rests in the water. Each hit may launch its pad's glyph from the bottom, as high as it was played; it falls back and
// is gone. Bubbles rise and pop low. Every finished bar drops a ring on the post of the section it played (A, B, fill).
// Drawing only: nothing here touches the sound or the controls. It runs on its own 30 Hz timer, only while something moves and
// only while the editor shows, and repaints just the places that changed.
#pragma once
#include "UiCommon.h"
#include <map>

namespace ru8ui
{
class WaterTank : public juce::Component, private juce::Timer
{
public:
    explicit WaterTank (Ctx&);
    ~WaterTank() override { stopTimer(); }

    void setTank (juce::Rectangle<int> tankInFront);    // the slot of the posts, in the front's coordinates
    void setCovered (std::initializer_list<juce::Rectangle<int>> covered, const std::vector<juce::Rectangle<int>>& controls = {});   // front coordinates: what hides the water (no repaint there), and the cells of controls (bubbles pass behind them)
    void hit (int pad, float velocity);
    void stir();                                        // a new kit
    void bar (int section /*0 A, 1 B*/, bool fill, int barNumber);
    void stopped();
    void recolor() { repaint(); }
    void setSeed (juce::int64 s) { rng.setSeed (s); }
    void setSimulatedClock (double seconds) noexcept { simClock = seconds; }   // tests: the time the launch limits count in

    void advance (float dt);                            // one step of the water (the timer calls it; tests do too)
    bool busy() const;
    int shapes() const noexcept { return (int) parts.size(); }
    int bubbleCount() const noexcept { return (int) bubbles.size(); }
    int ringCount (int post) const noexcept { return (int) stacks[(size_t) post].size(); }
    float lightOf (int pad) const noexcept { return lights[(size_t) pad]; }
    const juce::RectangleList<int>& lastRepaintRegion() const noexcept { return lastRepaint; }   // what the last step asked to be repainted (tests)

    void paint (juce::Graphics&) override;
    void visibilityChanged() override { if (! isShowing()) stopTimer(); }
    static constexpr int kInset = 3;                    // inside the 3 px border

private:
    struct Part { int id; float x, y, vx, vy, rot, vr, lit, s; };
    struct Bub { float x, y, r, vy, top, t, life; };
    struct Flying { int post; float t, x0, y0; bool done = false; };
    struct Ring { float y; };
    struct Falling { float x, y, vy, a; };

    void timerCallback() override;
    void kick();
    void launch (int id, float vel, float x0);
    void bubble (float x0, int n, float spread);
    void ring (int post);
    void dropAll();
    void markDirty (juce::Rectangle<float>);
    void drawRing (juce::Graphics&, float x, float y, float a) const;
    float surf (float x) const;
    juce::Point<float> reducedSpot (int pad) const;

    Ctx& ctx;
    juce::Random rng { 20260614 };
    float W = 1174, H = 817, P = 817, size = 29.0f;
    float phase = 0, energy = 0, bubbleDt = 0;
    int bubbleTick = 0;
    double lastAny = -10, lastTime = 0, simClock = -1.0;
    std::vector<Part> parts;
    std::vector<Bub> bubbles;
    std::vector<Flying> flying;
    std::vector<Falling> falling;
    std::array<std::vector<Ring>, 3> stacks;
    std::array<double, 16> lastShot {};
    std::array<float, 16> lights {};                    // reduced motion: a shape that lights up and fades
    bool haveTank = false;
    float tankBase = 0, tankTop = 0;
    std::array<float, 3> postX {};
    int prevSec = -1; bool prevFill = false, havePrev = false;
    juce::RectangleList<int> dirty;
    std::vector<juce::Rectangle<int>> lastRects;
    juce::RectangleList<int> lastRepaint;
    std::vector<juce::Rectangle<int>> covers;          // what hides the water (water coordinates)
    std::vector<juce::Rectangle<int>> controlRects;
    std::vector<uint8_t> tiles, coveredTile, bubbleCovered;
    bool bubbleHidden (float x, float y) const;
    static constexpr int kTile = 8;
    void computeCovered();
    bool haveMask = false;
    juce::Image bgImg;
    juce::String bgKey;
    void makeBackdrop (int px);
    int pixelsPerUnit() const;
    static constexpr int kShapePx = 96;
    std::map<juce::String, juce::Image> shapeCache;
    juce::Image shapeImage (int pad, int kit, bool halo);
    juce::Image bubbleImage (int rq, int px);
    juce::Point<float> flyPos (const Flying&) const;
};
} // namespace ru8ui
