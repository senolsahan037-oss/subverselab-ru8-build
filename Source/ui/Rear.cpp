#include "Rear.h"

namespace ru8ui
{
namespace
{
constexpr float kModY = 277.0f, kModH = 360.0f;
const juce::Rectangle<float> kColor { 21.0f, kModY, 512.8f, kModH }, kMidi { 555.8f, kModY, 341.9f, kModH }, kOut { 919.7f, kModY, 239.3f, kModH };

void drawModule (juce::Graphics& g, const Palette& p, juce::Rectangle<float> r)
{
    g.setColour (p.bed); g.fillRoundedRectangle (r, 18.0f);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (r.reduced (2.5f), 16.0f);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (p.well, 0.0f, r.getY(), p.well.withAlpha (0.0f), 0.0f, r.getY() + 12.0f, false));
        g.fillRect (r.withHeight (14.0f));
    }
    g.setColour (p.bevelHi); g.drawLine (r.getX() + 22.0f, r.getBottom() - 3.5f, r.getRight() - 22.0f, r.getBottom() - 3.5f, 1.0f);
    g.setColour (p.ink); g.drawRoundedRectangle (r.reduced (1.25f), 17.0f, 2.5f);
}

void head (juce::Graphics& g, const Palette& p, float x, float y, const juce::String& icon, const juce::String& label)
{
    drawGlyph (g, GlyphSet::icon, icon, { x, y, 16.0f, 16.0f }, p.goldDeep);
    g.setColour (p.goldDeep);
    drawTracked (g, label, { x + 23.0f, y - 3.0f, 150.0f, 22.0f }, heros (11.0f, true), 0.24f, juce::Justification::centredLeft);
}
}

RearPanel::RearPanel (Ctx& c, std::function<void()> onFlip, std::function<void()> onMotion) : ctx (c), motionCb (std::move (onMotion))
{
    setInterceptsMouseClicks (false, true);
    const char* ids[3] = { "colorTape", "colorDust", "colorCrush" }; const char* names[3] = { "Tape", "Dust", "Crush" }; const char* icons[3] = { "tape", "dust", "crush" };
    for (int i = 0; i < 3; ++i)
    {
        KnobCell::Spec s; s.paramId = ids[i]; s.label = names[i]; s.icon = icons[i]; s.knobSize = 66.0f;
        auto k = std::make_unique<KnobCell> (ctx, s);
        k->setBounds (juce::roundToInt (kColor.getX() + 20.0f + (float) i * (148.3f + 14.0f)), 329, 148, 98);
        addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    }
    auto seg = [&] (std::unique_ptr<PanelButton>& b, const juce::String& text, bool pads, int x)
    {
        b = std::make_unique<PanelButton> (ctx, PanelButton::Kind::seg);
        b->text = text;
        b->setFace ({ x, 329, 148, 30 });
        b->isOn = [this, pads] { return ctx.proc.padNoteMap() == pads; };
        b->onClick = [this, pads] { ctx.proc.setPadNoteMap (pads); repaint(); };
        addAndMakeVisible (*b);
    };
    seg (gmBtn, "GM drums", false, juce::roundToInt (kMidi.getX() + 20.0f));
    seg (padsBtn, juce::String (juce::CharPointer_UTF8 ("Pads 36\xe2\x80\x93" "51")), true, juce::roundToInt (kMidi.getX() + 20.0f + 152.0f));
    gmBtn->setTooltip ("GM drums: the pads answer General MIDI drum notes");
    padsBtn->setTooltip ("Pads: the 16 pads answer notes 36 to 51 (C1 up)");

    motionBtn = std::make_unique<PanelButton> (ctx, PanelButton::Kind::seg);
    motionBtn->text = "REDUCED MOTION";
    motionBtn->setTooltip ("Still water: shapes light up instead of travelling. The panel also follows macOS Reduce motion.");
    motionBtn->setFace ({ juce::roundToInt (kOut.getX() + 20.0f), 563, 199, 30 });
    motionBtn->isOn = [] { return userReducedMotion(); };
    motionBtn->onClick = [this] { setUserReducedMotion (! userReducedMotion()); if (motionCb) motionCb(); repaint(); };
    addAndMakeVisible (*motionBtn);

    flipBack = std::make_unique<PanelButton> (ctx, PanelButton::Kind::mood);
    flipBack->icon = "flip"; flipBack->setTooltip (ctx.standalone ? "Front panel (B)" : "Front panel"); flipBack->setFace (layout::baseMood);
    flipBack->onClick = std::move (onFlip);
    addAndMakeVisible (*flipBack);
    lockup = std::make_unique<LockupLink> (ctx);
    lockup->setBounds (layout::lockup);
    addAndMakeVisible (*lockup);
    setSize (kW, kH);
}

KnobCell* RearPanel::findKnob (const juce::String& id) const
{
    for (auto& k : knobs) if (k->paramId() == id) return k.get();
    return nullptr;
}

void RearPanel::resized() {}

void RearPanel::tick()
{
    for (auto& k : knobs) k->tick();
    for (auto* b : { gmBtn.get(), padsBtn.get(), motionBtn.get(), flipBack.get() }) b->tick();
}

void RearPanel::moodChanged() { lockup->moodChanged(); repaint(); }

void RearPanel::paint (juce::Graphics& g)
{
    const auto& p = ctx.p();
    g.fillAll (p.desk);
    const auto r = getLocalBounds().toFloat();
    g.setColour (p.cream); g.fillRoundedRectangle (r, 22.0f);
    {   // the glass sheet
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (r, 22.0f);
        g.reduceClipRegion (clip);
        juce::ColourGradient gr (p.sheen, 0.0f, 0.0f, p.sheenLo, r.getWidth() * 0.3f, r.getHeight(), false);
        gr.addColour (0.26, p.sheen.withAlpha (0.0f)); gr.addColour (0.72, p.sheenLo.withAlpha (0.0f));
        g.setGradientFill (gr); g.fillRect (r);
    }
    g.setColour (p.ink); g.drawRoundedRectangle (r.reduced (1.5f), 20.5f, 3.0f);

    // plate
    {
        const auto bf = display (40.0f, 0.78f);
        float x = 27.0f;
        const juce::Rectangle<float> row (0.0f, 25.0f, 200.0f, 40.0f);
        const float wRU = trackedWidth ("RU", bf, 0.02f), wDash = trackedWidth ("-", bf, 0.02f);
        g.setColour (p.ink); drawTracked (g, "RU", row.withX (x), bf, 0.02f, juce::Justification::centredLeft); x += wRU;
        g.setColour (p.goldDeep); drawTracked (g, "-", row.withX (x), bf, 0.02f, juce::Justification::centredLeft); x += wDash;
        g.setColour (p.ink); drawTracked (g, "8", row.withX (x), bf, 0.02f, juce::Justification::centredLeft);
        g.setColour (p.inkSoft); drawTracked (g, "RHYTHM UNIT", { 27.0f, 65.0f, 250.0f, 20.0f }, display (15.0f, 0.82f), 0.22f, juce::Justification::centredLeft);
        g.setColour (p.inkSoft); drawTracked (g, "REAR", { 900.0f, 40.0f, 253.0f, 20.0f }, display (13.0f, 0.82f), 0.32f, juce::Justification::centredRight);
    }
    // vents
    for (int i = 0; i < 28; ++i)
    {
        const juce::Rectangle<float> v (288.0f + 22.0f * (float) i, 105.0f, 10.0f, 74.0f);
        g.setColour (p.bevelHi); g.fillRoundedRectangle (v.translated (0.0f, 1.0f), 5.0f);
        g.setColour (p.bed); g.fillRoundedRectangle (v, 5.0f);
        g.setGradientFill (juce::ColourGradient (p.well, 0.0f, v.getY(), p.well.withAlpha (0.0f), 0.0f, v.getY() + 6.0f, false));
        g.fillRoundedRectangle (v.withHeight (8.0f), 5.0f);
    }

    // ---- Color
    drawModule (g, p, kColor);
    head (g, p, kColor.getX() + 20.0f, kModY + 18.0f, "wave", "COLOR");
    g.setColour (p.inkSoft);
    g.setFont (heros (12.5f));
    g.drawFittedText ("The character of the kit: tape saturation, vinyl dust (heard while playing) and bit crush. Each kit brings its own; most are clean. "
                      "The knobs glide when a kit loads; double-click returns to the kit's value.",
                      juce::Rectangle<int> ((int) kColor.getX() + 20, 445, (int) kColor.getWidth() - 40, 70), juce::Justification::topLeft, 4, 1.0f);

    // ---- MIDI
    drawModule (g, p, kMidi);
    head (g, p, kMidi.getX() + 20.0f, kModY + 18.0f, "midi", "MIDI");
    {
        const float x0 = kMidi.getX() + 20.0f;
        const char* keys[4] = { "C-1", "D-1", "E-1", "F-1" };
        const char* what[4] = { "A", "B", "fill", "off" };
        for (int i = 0; i < 4; ++i)
        {
            const float x = x0 + (float) (i % 2) * 152.0f, y = 375.0f + (float) (i / 2) * 30.0f;
            const float kw = trackedWidth (keys[i], display (13.0f), 0.0f) + 14.0f;
            g.setColour (p.inkSoft); g.drawRoundedRectangle ({ x + 0.75f, y + 2.0f, kw - 1.5f, 20.0f }, 4.0f, 1.5f);
            g.setColour (p.ink); drawTracked (g, keys[i], { x, y + 2.0f, kw, 20.0f }, display (13.0f), 0.0f, juce::Justification::centred);
            if (i == 2) drawGlyph (g, GlyphSet::icon, "fill", { x + kw + 12.0f, y + 4.0f, 16.0f, 16.0f }, p.ink);
            else drawTracked (g, what[i], { x + kw + 12.0f, y + 2.0f, 80.0f, 20.0f }, display (16.0f), 0.0f, juce::Justification::centredLeft);
        }
        // the note table, laid out as the pads are
        const bool pads = ctx.proc.padNoteMap();
        const float cw = (kMidi.getWidth() - 40.0f - 18.0f) / 4.0f, ch = 38.0f;
        for (int i = 0; i < 16; ++i)
        {
            const int pad = kDisplayOrder[i];
            const juce::Rectangle<float> c (x0 + (float) (i % 4) * (cw + 6.0f), 445.0f + (float) (i / 4) * (ch + 6.0f), cw, ch);
            g.setColour (p.cream.withAlpha (0.55f)); g.fillRoundedRectangle (c, 7.0f);
            g.setColour (p.rule); g.drawRoundedRectangle (c.reduced (0.5f), 7.0f, 1.0f);
            const auto gl = padGlyph (pad, ctx.kit());
            drawGlyph (g, gl.first, gl.second, { c.getX() + 6.0f, c.getCentreY() - 8.0f, 16.0f, 16.0f }, p.ink, 2.2f);
            g.setColour (p.inkSoft); drawTracked (g, padTag (pad, ctx.kit()), { c.getX() + 25.0f, c.getY() + 4.0f, cw - 27.0f, 13.0f }, display (10.5f), 0.06f, juce::Justification::centredLeft);
            g.setColour (p.goldDeep); drawTracked (g, noteName (ru8::padToNote (pad, pads)), { c.getX() + 25.0f, c.getY() + 17.0f, cw - 27.0f, 17.0f }, display (14.0f), 0.04f, juce::Justification::centredLeft);
        }
    }

    // ---- Out
    drawModule (g, p, kOut);
    head (g, p, kOut.getX() + 20.0f, kModY + 18.0f, "stereo", "OUT");
    for (int i = 0; i < 2; ++i)
    {
        const float cx = kOut.getCentreX() + (i == 0 ? -30.0f : 30.0f), cy = 379.0f;
        const juce::Rectangle<float> j = juce::Rectangle<float> (38.0f, 38.0f).withCentre ({ cx, cy });
        g.setColour (p.liftEdge); g.fillEllipse (j.translated (0.0f, 3.0f));
        g.setGradientFill (juce::ColourGradient (p.capHi, j.getX(), j.getY(), p.capLo, j.getRight(), j.getBottom(), false));
        g.fillEllipse (j);
        g.setColour (p.capLo); g.fillEllipse (j.reduced (j.getWidth() * 0.21f));
        g.setColour (p.ink); g.fillEllipse (j.reduced (j.getWidth() * 0.34f));
        g.drawEllipse (j.reduced (1.25f), 2.5f);
        g.setColour (p.inkSoft); drawTracked (g, i == 0 ? "L" : "R", { cx - 20.0f, cy + 26.0f, 40.0f, 18.0f }, display (14.0f), 0.14f, juce::Justification::centred);
    }
    g.setColour (p.inkSoft); g.setFont (heros (12.5f));
    g.drawText ("Stereo", juce::Rectangle<float> (kOut.getX(), 446.0f, kOut.getWidth(), 20.0f), juce::Justification::centred, false);
}
} // namespace ru8ui
