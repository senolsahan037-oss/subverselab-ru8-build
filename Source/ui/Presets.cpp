#include "Presets.h"

namespace ru8ui
{
namespace
{
constexpr int kToolsH = 58, kHeadH = 44, kRowH = 34, kPadX = 18, kKitW = 118, kGap = 8;
}

PresetList::PresetList (Ctx& c, std::function<void()> onClosed) : ctx (c), closedCb (std::move (onClosed))
{
    setInterceptsMouseClicks (true, false);
    // over the pad bed below its tools row (the PRESET key stays in sight; a click on it closes the list): the window never grows
    listRect = juce::Rectangle<int> (layout::bed).withTrimmedTop (kToolsH);
}

void PresetList::open()
{
    opened = true; hover = -1;
    setVisible (true);
    toFront (false);
    repaint();
}

void PresetList::close()
{
    if (! opened) return;
    opened = false; hover = -1;
    setVisible (false);
    if (closedCb) closedCb();
}

juce::Rectangle<int> PresetList::itemBounds (int i) const
{
    if (i < 0 || i >= ru8::kNumPresets) return {};
    const int row = i / 2, col = i % 2;
    const int x0 = listRect.getX() + kPadX + kKitW + kGap;
    const int w = (listRect.getRight() - kPadX - x0 - kGap) / 2;
    return { x0 + col * (w + kGap), listRect.getY() + kHeadH + row * kRowH, w, kRowH - 5 };
}

int PresetList::itemAt (juce::Point<int> pt) const
{
    for (int i = 0; i < ru8::kNumPresets; ++i) if (itemBounds (i).contains (pt)) return i;
    return -1;
}

void PresetList::choose (int i)
{
    if (i < 0 || i >= ru8::kNumPresets) return;
    ctx.proc.loadPreset (i);
    close();
}

void PresetList::mouseMove (const juce::MouseEvent& e) { setHover (itemAt (e.getPosition())); }

void PresetList::mouseDown (const juce::MouseEvent& e) { downInside = listRect.contains (e.getPosition()); }

void PresetList::mouseUp (const juce::MouseEvent& e)
{
    const auto pt = e.getPosition();
    if (! listRect.contains (pt)) { if (! downInside) close(); return; }   // a click outside the list closes it
    if (const int i = itemAt (pt); i >= 0 && e.mouseWasClicked()) choose (i);
}

void PresetList::paint (juce::Graphics& g)
{
    const auto& p = ctx.p();
    const auto r = listRect.toFloat();
    g.setColour (p.shadow); g.fillRoundedRectangle (r.translated (0.0f, 6.0f).expanded (2.0f), 20.0f);
    g.setColour (p.ink); g.fillRoundedRectangle (r, 18.0f);

    // head: title, the loaded preset, the modified dot
    const int cur = ctx.proc.currentPreset();
    const bool dirty = ctx.proc.presetModified();
    g.setColour (p.tipAccent);
    drawTracked (g, "PRESETS", { r.getX() + kPadX, r.getY() + 12.0f, 160.0f, 22.0f }, display (17.0f), 0.08f, juce::Justification::centredLeft);
    const juce::String now = juce::String (ru8::kPresets[cur].name) + (dirty ? "  (modified)" : "");
    g.setColour (p.rdDim);
    drawTracked (g, now, { r.getX() + kPadX + 120.0f, r.getY() + 12.0f, r.getWidth() - 2 * kPadX - 120.0f, 22.0f }, heros (12.5f), 0.0f, juce::Justification::centredRight);
    g.setColour (p.rdLine); g.fillRect (juce::Rectangle<float> (r.getX() + kPadX, r.getY() + kHeadH - 8.0f, r.getWidth() - 2 * kPadX, 1.0f));

    for (int row = 0; row < ru8::kNumPresets / 2; ++row)
    {
        const auto a = itemBounds (row * 2).toFloat();
        const int kit = ru8::presetKit (row * 2);
        g.setColour (p.rdDim);
        drawTracked (g, juce::String (ru8::kKits[kit].name).toUpperCase(), { r.getX() + kPadX, a.getY(), (float) kKitW, a.getHeight() }, display (13.0f), 0.08f, juce::Justification::centredLeft);
        for (int col = 0; col < 2; ++col)
        {
            const int i = row * 2 + col;
            const auto b = itemBounds (i).toFloat();
            const bool isCur = i == cur, isHover = i == hover;
            if (isCur) { g.setColour (p.gold); g.fillRoundedRectangle (b, 7.0f); }
            else if (isHover) { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRoundedRectangle (b, 7.0f); }
            g.setColour (isCur ? p.goldDeep : p.rdStep); g.drawRoundedRectangle (b.reduced (0.5f), 7.0f, 1.0f);
            const juce::String name (ru8::kPresets[i].name);
            const juce::String style = name.upToFirstOccurrenceOf (" - ", false, false);   // the kit is the row's label
            g.setColour (isCur ? p.onGold : p.cream);
            drawTracked (g, style, b.reduced (12.0f, 0.0f), heros (14.0f, isCur), 0.0f, juce::Justification::centredLeft);
            if (isCur && dirty)
            {
                const juce::Rectangle<float> d (b.getRight() - 17.0f, b.getCentreY() - 3.5f, 7.0f, 7.0f);
                g.setColour (p.onGold); g.fillEllipse (d);
            }
        }
    }
}
} // namespace ru8ui
