// RU-8 panel widgets: the raised key, the brand knob (with the glide and the double-click to the kit's value), the lockup link.
#pragma once
#include "UiCommon.h"
#include <map>

namespace ru8ui
{
//==============================================================================
/** One key of the panel in the Water edition's looks (gbtn, tools, row buttons, the readout's steps and arranger keys, segments). */
class PanelButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Kind { gbtn, tool, rowbtn, mood, help, start, clip, step, key, seg, preset };
    PanelButton (Ctx&, Kind);

    juce::String icon;                    // interface glyph (24 grid)
    juce::String text;                    // display text instead of the icon
    juce::String small;                   // the little letter at the bottom right (copy target)
    float iconSize = 22.0f;
    bool led = false;                     // arranger keys: a lamp in front
    std::function<juce::String()> voiceGlyph;   // clip key: the selected pad's glyph (set name via padGlyph)
    std::function<bool()> isOn, isRunning, isBlink, isUsable;
    std::function<juce::String()> textOf;            // preset key: the text, polled every tick
    std::function<bool()> isMarked;                  // preset key: the gold "modified" dot
    std::function<void()> onClick;
    std::function<void (const juce::MouseEvent&)> onDragStart;   // clip keys: dragged out

    void setFace (juce::Rectangle<int> face);       // the key's own rectangle; the bounds add room for its shadow
    juce::Rectangle<int> face() const noexcept { return getLocalBounds().withTrimmedBottom (kKeySlack); }
    void tick();                                    // polls the state functions; repaints when they changed

    void paint (juce::Graphics&) override;
    void forgetFaces() { faces.clear(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; down = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override { return face().contains (x, y); }
    void click();                                   // as a mouse click (tests)

private:
    Ctx& ctx;
    Kind kind;
    bool hover = false, down = false, dragged = false;
    bool lastOn = false, lastRun = false, lastBlinkPhase = false, lastUsable = true;
    juce::String lastGlyph, lastText;
    bool lastMarked = false;
    void paintFace (juce::Graphics&) const;
    std::map<juce::String, juce::Image> faces;
};

//==============================================================================
/** The brand knob with its glyph and read-out. The parameter takes a value at once; only the drawing glides.
    Glide (SubverseLab signature): the product sets the value (kit, set, SPACE type, reset, double-click) -> 30 Hz, 30 % of the
    remaining distance per tick, at least one grid step. A held knob, a hidden knob and a move by hand never glide. */
class KnobCell : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Spec { juce::String paramId, label, icon; float knobSize = 46.0f; bool darkTicks = false; bool showValue = true; };
    KnobCell (Ctx&, const Spec&);
    ~KnobCell() override;

    void bind (const juce::String& paramId, bool glide);     // another parameter (the CHANNEL rail follows the selected pad)
    const juce::String& paramId() const noexcept { return id; }
    void tick();                                             // one 30 Hz step of the glide
    float drawn() const noexcept { return drawnNorm; }      // what is drawn (0..1 of the parameter's range)
    float target() const noexcept { return targetNorm; }
    float parameterNorm() const;                             // the parameter itself
    bool gliding() const noexcept { return isGliding; }
    bool isHeld() const noexcept { return held; }
    juce::String valueText() const;

    void resetToKit();                                       // double-click
    void glideTo (float norm);                               // draw a travel to norm (the parameter already has it)
    static constexpr float cellH (float knobSize) noexcept { return knobSize + 32.0f; }
    juce::Rectangle<float> knobRect() const;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool hitTest (int x, int y) override { return getLocalBounds().contains (x, y); }

private:
    void onParam (float plain);
    void paintFace (juce::Graphics&, int litLo, int litHi) const;
    void setByHand (float norm);
    void endGesture();
    float gridStep() const;

    Ctx& ctx;
    Spec spec;
    juce::String id;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float drawnNorm = 0.0f, targetNorm = 0.0f, startNorm = 0.0f, startY = 0.0f;
    bool held = false, gestureOpen = false, isGliding = false, forceGlide = false, hover = false, bipolar = false;
    juce::Image face;
    juce::String faceKey;
};

//==============================================================================
/** The SubverseLab lockup, drawn in the mood's ink; a link to https://subverselab.com. */
class LockupLink : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LockupLink (Ctx& c);
    juce::URL getUrl() const { return juce::URL ("https://subverselab.com"); }
    void open() const { getUrl().launchInDefaultBrowser(); }
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && contains (e.getPosition())) open(); }
    void moodChanged() { tinted = false; repaint(); }

private:
    Ctx& ctx;
    juce::Image mark, wordmark, markTint, wordTint;
    bool hover = false, tinted = false;
};
} // namespace ru8ui
