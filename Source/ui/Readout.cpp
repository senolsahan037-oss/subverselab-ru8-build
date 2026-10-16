#include "Readout.h"

namespace ru8ui
{
Readout::Readout (Ctx& c)
    : kitPrev (c, PanelButton::Kind::step), kitNext (c, PanelButton::Kind::step), setPrev (c, PanelButton::Kind::step), setNext (c, PanelButton::Kind::step),
      keyA (c, PanelButton::Kind::key), keyB (c, PanelButton::Kind::key), keyF (c, PanelButton::Kind::key), keyAuto (c, PanelButton::Kind::key),
      srcKey (c, PanelButton::Kind::key), ctx (c)
{
    setInterceptsMouseClicks (false, true);
    kitPrev.icon = "chevL"; kitNext.icon = "chevR"; setPrev.icon = "chevL"; setNext.icon = "chevR";
    setPrev.iconSize = setNext.iconSize = 13.0f;
    kitPrev.setTooltip ("Previous kit"); kitNext.setTooltip ("Next kit"); setPrev.setTooltip ("Previous set"); setNext.setTooltip ("Next set");
    kitPrev.onClick = [this] { act::stepKit (ctx, -1); };
    kitNext.onClick = [this] { act::stepKit (ctx, 1); };
    setPrev.onClick = [this] { act::stepSet (ctx, -1); };
    setNext.onClick = [this] { act::stepSet (ctx, 1); };
    kitFace.setTooltip ("Kit"); setFace.setTooltip ("Set");
    kitFace.onClick = [this] { showKitList(); };
    setFace.onClick = [this] { showSetList(); };

    keyA.led = keyB.led = keyF.led = keyAuto.led = true;
    keyA.text = "A"; keyB.text = "B"; keyF.icon = "fill"; keyAuto.icon = "auto";
    const auto k = [this] (const char* hint) { return ctx.standalone ? juce::String (hint) : juce::String(); };   // keys only in the standalone
    keyA.setTooltip ("Section A" + k (" (5)")); keyB.setTooltip ("Section B" + k (" (6)")); keyF.setTooltip ("Fill on the next bar" + k (" (7)"));
    keyAuto.setTooltip ("Auto: A x3, fill, B x3, fill" + k (" (8)"));
    auto secOn = [this] (int sec) { const bool autoOn = ctx.proc.apvts.getRawParameterValue ("auto")->load() > 0.5f; juce::ignoreUnused (autoOn); return d.section == sec && ! (d.fill || (! d.playing && d.section == 3)); };
    keyA.isOn = [secOn] { return secOn (1); };
    keyB.isOn = [secOn] { return secOn (2); };
    keyF.isOn = [this] { return d.fill || (! d.playing && d.section == 3); };
    keyAuto.isOn = [this] { return ctx.proc.apvts.getRawParameterValue ("auto")->load() > 0.5f; };
    auto autoOn = [this] { return ctx.proc.apvts.getRawParameterValue ("auto")->load() > 0.5f; };
    auto secParam = [this] { return juce::roundToInt (ctx.proc.apvts.getRawParameterValue ("section")->load()); };
    // a lamp that blinks waits for the next bar line
    keyA.isBlink = [=] { return d.playing && ! autoOn() && secParam() == 1 && ! keyA.isOn(); };
    keyB.isBlink = [=] { return d.playing && ! autoOn() && secParam() == 2 && ! keyB.isOn(); };
    keyF.isBlink = [this] { return ctx.fillQueuedUi && ! keyF.isOn(); };
    keyA.onClick = [this] { act::sectionKey (ctx, 1); };
    keyB.onClick = [this] { act::sectionKey (ctx, 2); };
    keyF.onClick = [this] { act::fillKey (ctx); };
    keyAuto.onClick = [this] { act::autoKey (ctx); };

    // SET / MIDI: the word shows the mode; the lamp is lit in MIDI and blinks while a dragged clip has armed it
    srcKey.led = true;
    srcKey.isOn = [this] { return ctx.proc.midiSource(); };
    srcKey.isBlink = [this] { return ctx.proc.midiSourceArmed() && ! ctx.proc.midiSource(); };
    srcKey.isUsable = [this] { return ! ctx.standalone; };
    srcKey.onClick = [this] { toggleSource(); };

    for (auto* b : { &kitPrev, &kitNext, &setPrev, &setNext, &keyA, &keyB, &keyF, &keyAuto, &srcKey }) addAndMakeVisible (*b);
    addAndMakeVisible (kitFace); addAndMakeVisible (setFace);
}

void Readout::resized()
{
    kitPrev.setFace (layout::kitPrev); kitNext.setFace (layout::kitNext);
    setPrev.setFace (layout::setPrev); setNext.setFace (layout::setNext);
    keyA.setFace (layout::keyA); keyB.setFace (layout::keyB); keyF.setFace (layout::keyF); keyAuto.setFace (layout::keyAuto);
    srcKey.setFace (layout::srcKey);
    kitFace.setBounds (layout::kitFace);
    setFace.setBounds (layout::setFace.withTrimmedLeft (2).withTrimmedRight (6));
}

juce::String Readout::setLabel() const
{
    const int slot = ctx.proc.editSlot();
    auto s = ru8::params::setNames()[slot];
    if (slot < ru8::kNumSets) s << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) << juce::roundToInt (ctx.proc.song().slot[(size_t) slot].bpm);
    return s;
}

void Readout::toggleSource()
{
    if (ctx.standalone) return;
    const bool midi = ctx.proc.midiSource();
    ctx.proc.disarmMidiSource();
    act::setParam (ctx.proc, "source", midi ? 0.0f : 1.0f);
}

float Readout::setNameX() const
{
    const auto num = pending ? juce::String ("NEXT") : juce::String (ctx.proc.editSlot() + 1).paddedLeft ('0', 2);
    return layout::setNumX + (pending ? trackedWidth (num, heros (9.5f, true), 0.24f) : trackedWidth (num, display (11.0f), 0.12f)) + 8.0f;
}

void Readout::refresh (const RU8AudioProcessor::Display& nd)
{
    d = nd;
    pending = d.playing && ctx.proc.editSlot() != ctx.proc.currentSlot();
    blinkPhase = pending && ((juce::Time::getMillisecondCounter() / 250) % 2) == 0;
    juce::String key;
    key << ctx.kit() << "|" << ctx.proc.editSlot() << "|" << (pending ? 1 : 0) << (blinkPhase ? 1 : 0) << "|" << juce::String (d.bpm, 1) << "|" << (d.playing ? d.bar : 0) << "|"
        << (d.playing ? d.step / 4 : -1) << "|" << setLabel();
    if (key != lastKey) { lastKey = key; repaint(); }
    for (auto* b : { &keyA, &keyB, &keyF, &keyAuto }) b->tick();
    for (auto* b : { &kitPrev, &kitNext, &setPrev, &setNext }) b->tick();
    const bool midi = ctx.proc.midiSource(), armed = ctx.proc.midiSourceArmed();
    const juce::String word = midi ? "MIDI" : "SET";
    if (srcKey.text != word) { srcKey.text = word; srcKey.repaint(); }
    srcKey.setTooltip (ctx.standalone ? "SET / MIDI is for a DAW: the standalone plays its set"
                       : midi ? "MIDI: only incoming notes play RU-8; its set is silent with the host. Click to go back to SET"
                       : armed ? "SET: a clip was dragged out; RU-8 switches to MIDI when the clip's notes arrive while the host plays"
                       : "SET: RU-8 plays its set with the host. Click for MIDI (only incoming notes play); a dragged clip switches it when it plays");
    srcKey.tick();
}

void Readout::showKitList()
{
    juce::PopupMenu m;
    const auto& names = ru8::params::kitNames();
    for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i], true, i == ctx.kit());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kitFace), [this] (int r) { if (r > 0) act::setParam (ctx.proc, "kit", (float) (r - 1)); });
}

void Readout::showSetList()
{
    juce::PopupMenu m;
    const auto& names = ru8::params::setNames();
    m.addSectionHeader ("Rhythms");
    for (int i = 0; i < ru8::kNumSets; ++i) m.addItem (i + 1, names[i], true, i == ctx.proc.editSlot());
    m.addSectionHeader ("Your patterns");
    for (int i = ru8::kNumSets; i < ru8::kNumSlots; ++i) m.addItem (i + 1, names[i], true, i == ctx.proc.editSlot());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setFace), [this] (int r) { if (r > 0) act::setParam (ctx.proc, "set", (float) (r - 1)); });
}

void Readout::paint (juce::Graphics& g)
{
    const auto& p = ctx.p();
    const auto r = getLocalBounds().toFloat();
    // the glass (cached: the scan lines are many small fills)
    if (! glass.isValid() || glassKey != ctx.proc.mood)
    {
        glass = juce::Image (juce::Image::ARGB, getWidth() * 2, getHeight() * 2, true);
        juce::Graphics gi (glass);
        gi.addTransform (juce::AffineTransform::scale (2.0f));
        gi.setColour (p.inkGround); gi.fillRoundedRectangle (r, 12.0f);
        {
            juce::Graphics::ScopedSaveState ss (gi);
            juce::Path clip; clip.addRoundedRectangle (r, 12.0f);
            gi.reduceClipRegion (clip);
            gi.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), 0.0f, 0.0f, juce::Colours::transparentBlack, 0.0f, 18.0f, false));
            gi.fillRect (r.withHeight (18.0f));
            gi.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0.0f, 0.0f, juce::Colours::transparentBlack, 0.0f, r.getHeight() * 0.35f, false));
            gi.fillRect (r.withHeight (r.getHeight() * 0.35f));
            gi.setColour (juce::Colours::white.withAlpha (0.018f));
            for (float y = 1.0f; y < r.getHeight(); y += 3.0f) gi.fillRect (0.0f, y, r.getWidth(), 1.0f);
        }
        gi.setColour (p.glassEdge); gi.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);
        glassKey = ctx.proc.mood;
    }
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImage (glass, r);

    const auto lbl = heros (9.5f, true);
    auto drawLbl = [&] (const juce::String& t, float x, float y) { g.setColour (p.rdDim); drawTracked (g, t, { x, y, 120.0f, 11.0f }, lbl, 0.24f, juce::Justification::centredLeft); return trackedWidth (t, lbl, 0.24f); };

    // kit
    {
        const int kit = ctx.kit();
        const float w = drawLbl ("KIT", 50.0f, 10.0f);
        g.setColour (p.gold);
        drawTracked (g, juce::String (kit + 1).paddedLeft ('0', 2) + " / " + juce::String (ru8::kNumKits).paddedLeft ('0', 2), { 50.0f + w + 7.0f, 9.0f, 100.0f, 13.0f }, display (11.0f), 0.12f, juce::Justification::centredLeft);
        const auto name = juce::String (ru8::kKits[kit].name).toUpperCase();
        g.setColour (p.glassFg.withAlpha (0.10f));
        drawTracked (g, name, { 50.0f, 22.0f, 474.0f, 36.0f }, display (34.0f), 0.03f, juce::Justification::centredLeft, true);
        g.setColour (p.glassFg);
        drawTracked (g, name, { 50.0f, 23.0f, 474.0f, 34.0f }, display (34.0f), 0.03f, juce::Justification::centredLeft, true);
        g.setColour (p.rdDim);
        drawTracked (g, juce::String::fromUTF8 (ru8::kKits[kit].tag), { 50.0f, 59.0f, 474.0f, 15.0f }, heros (11.0f), 0.06f, juce::Justification::centredLeft, true);
    }
    // BPM block
    {
        g.setColour (p.rdLine); g.fillRect ((float) layout::bpmBlock.getX(), (float) layout::bpmBlock.getY(), 1.0f, (float) layout::bpmBlock.getHeight());
        if (ctx.standalone) { g.setColour (p.rdDim); drawTracked (g, "BPM", { 590.0f, 10.0f, 66.0f, 11.0f }, lbl, 0.24f, juce::Justification::centredRight); }
        else
        {   // a host's tempo: HOST and a lock, not editable here
            const float w = trackedWidth ("HOST", lbl, 0.24f);
            g.setColour (p.gold); drawTracked (g, "HOST", { 590.0f, 10.0f, 66.0f, 11.0f }, lbl, 0.24f, juce::Justification::centredRight);
            drawGlyph (g, GlyphSet::icon, "lock", { 656.0f - w - 15.0f, 9.0f, 12.0f, 12.0f }, p.gold, 2.2f);
        }
        g.setColour (p.glassFg.withAlpha (0.10f)); drawTracked (g, juce::String (d.bpm, 1), { 582.0f, 32.0f, 74.0f, 36.0f }, display (34.0f, 0.72f), 0.0f, juce::Justification::centredRight);
        g.setColour (p.glassFg); drawTracked (g, juce::String (d.bpm, 1), { 582.0f, 33.0f, 74.0f, 34.0f }, display (34.0f, 0.72f), 0.0f, juce::Justification::centredRight);
    }
    // the low row
    g.setColour (p.rdLine);
    g.fillRect (14.0f, 82.0f, 642.0f, 1.0f);
    g.fillRect (322.0f, 91.0f, 1.0f, 26.0f);
    g.fillRect (528.0f, 91.0f, 1.0f, 26.0f);
    {
        // set: the SET / MIDI switch (a key, left), the number (or the blinking NEXT), then the name
        const float y = 91.0f, x0 = layout::setNumX;
        if (pending) { g.setColour (blinkPhase ? p.gold.withAlpha (0.35f) : p.gold); drawTracked (g, "NEXT", { x0, y, 60.0f, 26.0f }, lbl, 0.24f, juce::Justification::centredLeft); }
        else
        {
            const auto num = juce::String (ctx.proc.editSlot() + 1).paddedLeft ('0', 2);
            g.setColour (p.gold); drawTracked (g, num, { x0, y, 40.0f, 26.0f }, display (11.0f), 0.12f, juce::Justification::centredLeft);
        }
        const float x = setNameX(), field = layout::setNameRight - x;
        const auto name = setLabel().toUpperCase();
        const float w = trackedWidth (name, display (19.0f), 0.03f);
        g.setColour (pending ? (blinkPhase ? p.gold.withAlpha (0.35f) : p.gold) : p.glassFg);
        // a name wider than its field is condensed, never cut
        drawTracked (g, name, { x, y, field, 26.0f }, w > field ? display (19.0f, 0.80f * field / w) : display (19.0f), 0.03f, juce::Justification::centredLeft, true);
    }
    {
        // position: BAR 01.1 and the four beat lamps
        g.setColour (p.rdDim); drawTracked (g, "BAR", { 542.0f, 91.0f, 40.0f, 26.0f }, lbl, 0.24f, juce::Justification::centredLeft);
        const int bar = d.playing ? d.bar : 0, beat = d.playing && d.step >= 0 ? d.step / 4 : 0;
        g.setColour (p.glassFg);
        drawTracked (g, juce::String (bar + 1).paddedLeft ('0', 2) + "." + juce::String (beat + 1), { 576.0f, 91.0f, 36.0f, 26.0f }, display (18.0f), 0.04f, juce::Justification::centredLeft);
        for (int i = 0; i < 4; ++i)
        {
            const juce::Rectangle<float> b (614.0f + 12.0f * (float) i, 100.5f, 7.0f, 7.0f);
            if (d.playing && d.step >= 0 && i == beat) { g.setColour (p.gold.withAlpha (0.3f)); g.fillEllipse (b.expanded (3.0f)); g.setColour (p.gold); g.fillEllipse (b); }
            else { g.setColour (p.rdBeat); g.drawEllipse (b.reduced (0.5f), 1.0f); }
        }
    }
}
} // namespace ru8ui
