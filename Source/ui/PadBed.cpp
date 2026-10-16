#include "PadBed.h"

namespace ru8ui
{
namespace
{
constexpr float kPadR = 14.0f, kBorder = 2.5f;
}

PadComponent::PadComponent (Ctx& c, PadHost& h, int p) : ctx (c), host (h), index (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> PadComponent::nameRow() const { return { 10.5f, 9.5f, 101.0f, 22.0f }; }
juce::Rectangle<float> PadComponent::stepArea() const { return { 10.5f, 37.5f, 101.0f, 74.0f }; }

juce::Rectangle<float> PadComponent::stepCell (int i) const
{
    const auto a = stepArea();
    const float cw = (a.getWidth() - 12.0f) / 4.0f, ch = (a.getHeight() - 12.0f) / 4.0f;
    return { a.getX() + (float) (i % 4) * (cw + 4.0f), a.getY() + (float) (i / 4) * (ch + 4.0f), cw, ch };
}

int PadComponent::cellAt (juce::Point<float> pt) const
{
    if (! stepsShown() || ! stepArea().expanded (2.0f).contains (pt)) return -1;
    for (int i = 0; i < 16; ++i)
        if (stepCell (i).expanded (2.0f).contains (pt)) return i;
    return -1;
}

void PadComponent::flash (float v)
{
    flashStart = juce::Time::getMillisecondCounterHiRes();
    flashA0 = 0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, v);
    const float decay = ctx.padDecay[(size_t) index]->load();
    flashDur = (double) padFlashMs (index, ctx.kit()) * (0.7 + 0.6 * (double) decay);
    glowNow = flashA0;
    repaint();
}

juce::String PadComponent::stateKey() const
{
    juce::String k;
    k << (ctx.selectedPad == index ? 's' : '-') << (ctx.padMute[(size_t) index]->load() > 0.5f ? 'm' : '-')
      << (ctx.padSolo[(size_t) index]->load() > 0.5f ? 'o' : '-') << (ctx.anySolo() ? 'a' : '-') << ctx.kit() << (stepsShown() ? 'h' : '-') << hoverCell;
    if (stepsShown())
    {
        k << ctx.curStep;
        const auto& st = ctx.proc.song().slot[(size_t) ctx.proc.editSlot()].steps[ctx.viewSection()][index];
        k << juce::String (st, 16);
    }
    return k;
}

void PadComponent::tick()
{
    if (glowNow > 0.0f)
    {
        const double t = (juce::Time::getMillisecondCounterHiRes() - flashStart) / flashDur;
        glowNow = t >= 1.0 ? 0.0f : flashA0 * (float) std::pow (1.0 - t, 2.2);
        repaint();
    }
    const auto k = stateKey();
    if (k != lastKey) { lastKey = k; repaint(); }
}

void PadComponent::mouseMove (const juce::MouseEvent& e)
{
    const int c = cellAt (e.position);
    if (c != hoverCell) { hoverCell = c; repaint(); }
}

void PadComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    downCell = cellAt (e.position);
    if (downCell >= 0) return;                       // a step is clicked on release
    mouseHeld = true; held = true; repaint();
    host.padPressed (index, e.mods.isShiftDown() ? 0.5f : 1.0f);
}

void PadComponent::mouseUp (const juce::MouseEvent& e)
{
    if (mouseHeld) { mouseHeld = false; held = false; repaint(); }
    if (downCell >= 0 && cellAt (e.position) == downCell) host.stepClicked (index, downCell);
    downCell = -1;
}

void PadComponent::paintBack (juce::Graphics& g, bool heldLook) const
{
    const auto& p = ctx.p();
    const juce::Rectangle<float> r (0.0f, 0.0f, (float) layout::padSize, (float) kBodyH);
    juce::Path clip; clip.addRoundedRectangle (r, kPadR);
    g.setColour (heldLook ? p.padHeld : index >= 8 ? p.padAux : p.cream); g.fillPath (clip);
    juce::Graphics::ScopedSaveState s2 (g);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (p.bevelHi, 0.0f, 0.0f, p.bevelHi.withAlpha (0.0f), 0.0f, (float) kBodyH * 0.45f, false));
    g.fillRect (r);
}

void PadComponent::paintFront (juce::Graphics& g, bool steps) const
{
    const auto& p = ctx.p();
    const bool sel = ctx.selectedPad == index;
    const bool muted = ctx.padMute[(size_t) index]->load() > 0.5f, soloed = ctx.padSolo[(size_t) index]->load() > 0.5f;
    const bool dim = ctx.anySolo() && ! soloed && ! muted;
    const float content = muted || dim ? 0.38f : 1.0f;
    const juce::Rectangle<float> r (0.0f, 0.0f, (float) layout::padSize, (float) kBodyH);

    // inset rings: selected (ink) and soloed (gold), then the border
    if (sel || soloed)
    {
        g.setColour (soloed ? p.gold : p.ink);
        g.drawRoundedRectangle (r.reduced (kBorder + 1.25f), kPadR - kBorder, 2.5f);
    }
    g.setColour (p.ink); g.drawRoundedRectangle (r.reduced (kBorder * 0.5f), kPadR, kBorder);

    // name row
    const auto row = nameRow();
    if (steps && hover)
    {
        const auto pos = getMouseXYRelative().toFloat();
        if (row.contains (pos)) { g.setColour (p.hitHover); g.fillRoundedRectangle (row, 8.0f); }
    }
    const auto name = padName (index, ctx.kit()).toUpperCase();
    const float nameSize = juce::jlimit (12.0f, 16.0f, (float) layout::padSize * 0.1f);
    const auto key = juce::String (padKey (index));
    const auto kf = display (12.0f);
    const float keyW = juce::jmax (18.0f, trackedWidth (key, kf, 0.0f) + 10.0f);
    const juce::Rectangle<float> keyBox (row.getRight () - 3.0f - keyW, row.getCentreY() - 8.0f, keyW, 16.0f);
    {
        g.setColour (p.ink.withMultipliedAlpha (content));
        const auto nf = display (nameSize);
        const juce::Rectangle<float> nameBox (row.getX() + 3.0f, row.getY(), keyBox.getX() - row.getX() - 9.0f, row.getHeight());
        drawTracked (g, name, nameBox, nf, 0.04f, juce::Justification::centredLeft, true);
        if (sel)
        {
            const float tw = juce::jmin (nameBox.getWidth(), trackedWidth (name, nf, 0.04f));
            g.setColour (p.gold.withMultipliedAlpha (content));
            g.fillRect (nameBox.getX(), nameBox.getCentreY() + nf.getAscent() * 0.5f + 3.0f, tw, 2.0f);
        }
    }
    // the computer key: only the standalone takes letter keys; in a host the badge stays (same panel) but faint
    const float keyAlpha = ctx.standalone ? 1.0f : 0.3f;
    g.setColour (p.inkSoft.withMultipliedAlpha (keyAlpha)); g.drawRoundedRectangle (keyBox.reduced (0.75f), 4.0f, 1.5f);
    g.setColour (p.ink.withMultipliedAlpha (keyAlpha)); drawTracked (g, key, keyBox, kf, 0.0f, juce::Justification::centred);

    if (! steps)
    {
        const auto gl = padGlyph (index, ctx.kit());
        const auto a = stepArea();
        const float s = a.getWidth() * 0.48f;
        drawGlyph (g, gl.first, gl.second, juce::Rectangle<float> (s, s).withCentre (a.getCentre()), p.ink.withMultipliedAlpha (content), 2.0f);
        return;
    }
    // the 16 steps
    const auto& chars = ctx.proc.song().slot[(size_t) ctx.proc.editSlot()].steps[ctx.viewSection()][index];
    for (int i = 0; i < 16; ++i)
    {
        const auto c = stepCell (i);
        const char ch = chars[i];
        const bool col = ctx.curStep == i;
        if (col) { g.setColour (p.gold.withMultipliedAlpha (content)); g.drawRoundedRectangle (c.expanded (1.0f), 6.0f, 2.0f); }
        const juce::Colour fill = ch == 'x' ? p.ink : ch == 'X' ? p.gold : (hoverCell == i ? p.stHover : p.cream);
        g.setColour (fill.withMultipliedAlpha (content)); g.fillRoundedRectangle (c, 5.0f);
        g.setColour (p.ink.withMultipliedAlpha (content)); g.drawRoundedRectangle (c.reduced (0.75f), 5.0f, 1.5f);
        if (i % 4 == 0) g.fillRect (c.getX(), c.getY() + 3.0f, 3.0f, c.getHeight() - 6.0f);
        if (ch == 'g')
        {
            const float d = c.getWidth() * 0.30f;
            g.setColour (p.ink.withMultipliedAlpha (content)); g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c.getCentre()));
        }
        else if (ch == 'r')
        {
            const float d = c.getWidth() * 0.19f;
            g.setColour (p.ink.withMultipliedAlpha (content));
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c.getCentre().translated (-c.getWidth() * 0.14f, 0.0f)));
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c.getCentre().translated (c.getWidth() * 0.14f, 0.0f)));
        }
    }
}

juce::Image PadComponent::render (std::function<void (juce::Graphics&)> draw) const
{
    juce::Image img (juce::Image::ARGB, layout::padSize * 2, kBodyH * 2, true);
    juce::Graphics gi (img);
    gi.addTransform (juce::AffineTransform::scale (2.0f));
    draw (gi);
    return img;
}

void PadComponent::paint (juce::Graphics& g)
{
    const auto& p = ctx.p();
    const bool steps = stepsShown();
    const float dy = held ? 3.0f : 0.0f;
    const juce::Rectangle<float> r (0.0f, 0.0f, (float) layout::padSize, (float) kBodyH);
    juce::Graphics::ScopedSaveState ss (g);
    g.addTransform (juce::AffineTransform::translation (0.0f, dy));
    juce::Path clip; clip.addRoundedRectangle (r, kPadR);

    // the body (cached per look), the flash over it, then everything printed on it (cached per look; the steps are drawn live)
    const juce::String backKey = ctx.proc.mood + (held ? "h" : "-") + (index >= 8 ? "a" : "m");
    if (! backImg.isValid() || backKey != backImgKey) { backImg = render ([this] (juce::Graphics& gi) { paintBack (gi, held); }); backImgKey = backKey; }
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImage (backImg, r);
    if (glowNow > 0.0f)
    {
        juce::Graphics::ScopedSaveState s2 (g);
        g.reduceClipRegion (clip);
        g.setColour (p.glow.withMultipliedAlpha (glowNow)); g.fillRect (r);
    }
    if (steps) { paintFront (g, true); return; }
    const juce::String frontKey = ctx.proc.mood + juce::String (ctx.kit()) + (ctx.selectedPad == index ? "s" : "-") + (ctx.padMute[(size_t) index]->load() > 0.5f ? "m" : "-")
                                + (ctx.padSolo[(size_t) index]->load() > 0.5f ? "o" : "-") + (ctx.anySolo() ? "a" : "-");
    if (! frontImg.isValid() || frontKey != frontImgKey) { frontImg = render ([this] (juce::Graphics& gi) { paintFront (gi, false); }); frontImgKey = frontKey; }
    g.drawImage (frontImg, r);
}

//==============================================================================
PadBed::PadBed (Ctx& c, PadHost& h) : ctx (c)
{
    setInterceptsMouseClicks (false, true);
    for (int i = 0; i < 16; ++i) { pads[(size_t) i] = std::make_unique<PadComponent> (c, h, i); addAndMakeVisible (*pads[(size_t) i]); }
}

juce::Rectangle<int> PadBed::padRect (int enginePad) const
{
    int slot = 0;
    for (int i = 0; i < 16; ++i) if (kDisplayOrder[i] == enginePad) slot = i;
    return { layout::padX0 + (slot % 4) * layout::padPitch, layout::padY0 + (slot / 4) * layout::padPitch, layout::padSize, layout::padSize + 3 };
}

void PadBed::resized()
{
    for (int i = 0; i < 16; ++i) pads[(size_t) i]->setBounds (padRect (i));
    layoutTools();
}

void PadBed::layoutTools()
{
    auto place = [] (PanelButton* b, int x, int w) { if (b != nullptr) b->setFace ({ x, 14, w, 36 }); };
    place (dragSolo, 16, 55); place (dragFull, 79, 58); place (save, 145, 40);
    place (presetPrev, 197, 32); place (presetKey, 233, 146); place (presetNext, 383, 32);   // between the clip keys and the pattern tools
    place (clrSec, 422, 40); place (clrRow, 470, 40); place (copySec, 518, 40); place (copyPad, 566, 40); place (pastePad, 614, 40);
}

void PadBed::paint (juce::Graphics& g)
{
    const juce::String key = ctx.proc.mood;
    if (! cache.isValid() || key != cacheKey)
    {
        cache = juce::Image (juce::Image::ARGB, getWidth() * 2, getHeight() * 2, true);
        juce::Graphics gi (cache);
        gi.addTransform (juce::AffineTransform::scale (2.0f));
        paintStatic (gi);
        cacheKey = key;
    }
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImage (cache, getLocalBounds().toFloat());
}

void PadBed::paintStatic (juce::Graphics& g)
{
    const auto& p = ctx.p();
    const auto r = getLocalBounds().toFloat();
    g.setColour (p.bed); g.fillRoundedRectangle (r, 18.0f);
    {   // inset shade: the pad bed is a well
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (r.reduced (2.5f), 16.0f);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (p.well, 0.0f, 0.0f, p.well.withAlpha (0.0f), 0.0f, 12.0f, false));
        g.fillRect (r.withHeight (14.0f));
    }
    g.setColour (p.bevelHi); g.drawLine (22.0f, r.getBottom() - 3.5f, r.getRight() - 22.0f, r.getBottom() - 3.5f, 1.0f);
    g.setColour (p.ink); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.25f), 17.0f, 2.5f);
    for (int i = 0; i < 16; ++i)
    {
        const auto pr = padRect (i).withHeight (layout::padSize).toFloat();
        drawSoftShadow (g, pr, kPadR, p.lift, 9.0f, 5.0f);
        g.setColour (p.liftEdge); g.fillRoundedRectangle (pr.translated (0.0f, 4.0f), kPadR);
    }
}
} // namespace ru8ui
