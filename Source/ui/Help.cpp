#include "Help.h"

namespace ru8ui
{
namespace
{
constexpr float kTipW = 250.0f, kPadX = 12.0f, kPadY = 10.0f, kTextSize = 12.5f;
}

juce::Rectangle<int> HelpOverlay::markerBounds (int i) const { return markers[(size_t) i]; }

void HelpOverlay::paintMarker (juce::Graphics& g, int index) const
{
    const auto& p = ctx.p();
    const auto r = markers[(size_t) index].toFloat();
    const bool on = at == index;
    g.setColour (p.shadow); g.fillEllipse (r.translated (0.0f, 2.0f).expanded (1.0f));
    g.setColour (on ? p.ink : p.gold); g.fillEllipse (r);
    g.setColour (on ? p.gold : p.cream); g.drawEllipse (r.reduced (1.0f), 1.75f);
    g.setColour (on ? p.runFg : p.onGold);
    drawTracked (g, juce::String (index + 1), r.translated (0.0f, 0.5f), display (12.5f), 0.0f, juce::Justification::centred);
}

int HelpOverlay::markerAt (juce::Point<int> pt) const
{
    for (int i = (int) markers.size(); --i >= 0;)
        if (markers[(size_t) i].toFloat().getCentre().getDistanceFrom (pt.toFloat()) <= (float) kMarkerD * 0.5f + 1.0f) return i;
    return -1;
}

HelpOverlay::~HelpOverlay() = default;

HelpOverlay::HelpOverlay (Ctx& c, std::function<void()> onClosed) : ctx (c), closedCb (std::move (onClosed))
{
    setVisible (false);
    setWantsKeyboardFocus (false);
}

void HelpOverlay::build()
{
    using R = juce::Rectangle<int>;
    entries.clear(); markers.clear();
    const R ro = layout::readout.withSize (0, 0), bd = layout::bed.withSize (0, 0);
    auto inReadout = [&] (R r) { return r.translated (ro.getX(), ro.getY()); };
    auto inBed = [&] (R r) { return r.translated (bd.getX(), bd.getY()); };
    const auto ar = [] (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); };
    juce::ignoreUnused (ar);
    auto add = [&] (const char* title, R target, char side, const juce::String& text) { entries.push_back ({ title, text, target, side }); };

    add ("Kit", inReadout ({ 14, 10, 546, 64 }), 'b',
         "The sound of all pads, and the space, echo and channel settings it was voiced with. {ico:chevL}{ico:chevR} step through the kits; click the name for the list. The knobs glide to the new kit.");
    const bool sa = ctx.standalone;   // the same markers in both editions; a host's texts name no computer keys
    add ("Set", inReadout ({ 92, 91, 186, 26 }), 'b',
         "A ready rhythm with its own tempo, swing and echo time. While playing, the change waits for the next bar and the readout says Next. User 1-4 start empty and are saved with your project.");
    add ("Set / MIDI", inReadout (layout::srcKey), 'b', sa
         ? "For a DAW: there SET lets RU-8 play its set with the host, MIDI lets only incoming notes play. The standalone always plays its set."
         : "SET: RU-8 plays its set with the host's transport, so you can try sets in your song. MIDI: its set is silent, only incoming notes play the pads. Drag a clip out and the lamp blinks; when the clip's notes reach RU-8 while the host plays, it switches to MIDI. Click to switch back. Saved with the project. Each RU-8 has its own switch.");
    add ("Arranger", inReadout ({ 322, 91, 206, 26 }), 'b', sa
         ? "A and B are two sections of the set {key:5} {key:6}. {ico:fill} plays a fill on the next bar {key:7}. {ico:auto} runs A x3, fill, B x3, fill {key:8}. A lit lamp is playing; a blinking one is waiting for the next bar. Section and Auto are also automation lanes."
         : "A and B are two sections of the set. {ico:fill} plays a fill on the next bar. {ico:auto} runs A x3, fill, B x3, fill. A lit lamp is playing; a blinking one is waiting for the next bar. Section and Auto are also automation lanes.");
    add ("Position", inReadout ({ 528, 91, 128, 26 }), 'b', "Bar and beat while playing. The four lamps light one per beat.");
    add ("Play", layout::startBtn, 'l', sa
         ? "Starts and stops {key:Space}. It turns dark with a gold square while playing."
         : "Preview: plays the set on RU-8's own clock at the host's tempo while the host is stopped. When the host starts, RU-8 carries on in step with the host's bar, without a break; the key turns faint, the host plays RU-8. When the host stops, RU-8 stops.");
    add ("Tempo", layout::tempoKnob.withHeight (50).withWidth (50), 'l', sa
         ? "{ico:tempo} Drag up or down; hold {key:Shift} for fine steps, or use the wheel. Double-click returns to the set's tempo."
         : "{ico:tempo} The standalone's tempo. In a host it rests, faint: the host's tempo rules.");
    add ("Tap tempo", layout::tapBtn, 'l', sa
         ? "{ico:tap} Tap two or more times in time ({key:T}); the tempo follows the average of the last taps."
         : "{ico:tap} The standalone's tap tempo. In a host it rests, faint.");
    add ("BPM", inReadout ({ 578, 0, 92, 74 }), 'b', sa
         ? "The tempo playing now."
         : "{ico:lock} HOST: the host's tempo, followed always and not changed here.");
    add ("MIDI out", inBed ({ 16, 14, 169, 36 }), 'b',
         "Drag a clip onto a MIDI track in your DAW. The pad button is the selected pad alone (its glyph shows which), {ico:full} and the save arrow are the full drum; the gold arrow means drag it out. With auto off you get one bar of the section you see; with auto on, the whole arrangement (A x3, fill, B x3, fill: 8 bars). A click on the save arrow writes both clips to files. Notes follow the note map on the rear panel (GM by default: C1 = kick), channel 10, swing written into the timing.");
    add ("Presets", inBed ({ 197, 14, 218, 36 }), 'b',
         "28 factory presets, two per kit: the kit's sound with a set's groove. {ico:chevL}{ico:chevR} step through them; click the name for the list, grouped by kit. A gold dot means something changed since the preset loaded. The knobs glide; while playing, the new set starts on the next bar. In a DAW the same 28 are its program list.");
    add ("Pattern", inBed ({ 422, 14, 232, 36 }), 'b', sa
         ? "{ico:clrSec} clears the whole section. {ico:clrRow} clears only the selected pad. {ico:copySec} copies this section to the other one (the letter is the target). {ico:copyPad} copies the steps of the selected pad and {ico:pastePad} pastes them onto another pad ({key:Cmd}{key:C} {key:Cmd}{key:V})."
         : "{ico:clrSec} clears the whole section. {ico:clrRow} clears only the selected pad. {ico:copySec} copies this section to the other one (the letter is the target). {ico:copyPad} copies the steps of the selected pad and {ico:pastePad} pastes them onto another pad.");
    add ("Pads", inBed ({ layout::padX0, layout::padY0, 4 * layout::padPitch - 12, 4 * layout::padPitch - 12 }), 'r', sa
         ? "Laid out like hardware: pad 1 bottom left. Keys match the layout, {key:Z}{key:X}{key:C}{key:V} bottom row up to {key:1}-{key:4}; {key:Shift} plays soft. The top two rows change with the kit."
         : "Laid out like hardware: pad 1 bottom left. Click to play. In a host the computer keys stay the host's (the letters on the pads are the standalone's); play the pads with MIDI notes, for example Live's computer MIDI keyboard. The top two rows change with the kit.");
    {
        int slot = 0;
        for (int i = 0; i < 16; ++i) if (layout::padSize > 0 && kDisplayOrder[i] == 3) slot = i;
        add ("Steps", inBed ({ layout::padX0 + (slot % 4) * layout::padPitch, layout::padY0 + (slot / 4) * layout::padPitch, layout::padSize, layout::padSize }), 'a',
            "Point at a pad to open its 16 steps; each row is one beat. Click a step: off, on (ink), accent (gold), ghost (dot). While playing, the current step has a gold ring.");
    }
    add ("Channel", { 21, layout::leftT1 - 4, 212, 255 }, 'r',
         "The pad you played last. {ico:level} level, {ico:tune} tune in semitones, {ico:decay} decay length, {ico:pan} pan, {ico:space} send to Space, {ico:echo} send to Echo. {ico:mute} mutes the pad, {ico:solo} solos it (several can be soloed; the others dim), {ico:reset} returns it to the kit.");
    add ("Master", { 21, layout::leftT2, 212, 246 }, 'r',
         "The meter is the output in dBFS; GR is how much the glue is pressing. {ico:swing} swing, {ico:human} human feel (small random timing and level), {ico:glue} glue (gentle bus compression), {ico:volume} output volume.");
    add ("Space", { 947, layout::rightT1, 212, 243 }, 'l',
         "Algorithmic reverb. {ico:room} room, {ico:plate} plate, {ico:hall} hall. {ico:size} size, {ico:damp} damping (how fast the highs fade), {ico:predelay} pre-delay, {ico:ret} return level.");
    add ("Echo", { 947, layout::rightT2, 212, 242 }, 'l',
         "Tempo-synced ping-pong delay; D means dotted. {ico:feedback} feedback (number of repeats), {ico:damp} tone (darker to the left), {ico:ret} return level.");
    add ("Knobs", { layout::leftX, layout::leftT1 + 32, 68, 78 }, 'r',
         "Drag up or down; {key:Shift} for fine steps; the wheel works too. Double-click returns a knob to the kit's value. When a kit or set is chosen, the knobs glide to their new places.");
    add ("Water", layout::tank, 'a',
         "The body of the machine is a water tank behind the pads and knobs. Each pad has its shape in it; a shape rises when the pad plays, as high as it was hit, and sinks back. While playing, every finished bar drops a ring on its post here: A, B or {ico:fill} fill, so you can see where the arrangement is. A new A after B clears the posts. A new kit stirs the water. Reduced motion is on the rear panel.");
    add ("Rear panel", layout::baseFlip, 'a',
         "Turns the machine round ({key:B}). On the back: Color (tape, dust and crush, the character each kit brings), the MIDI note map with the section trigger notes and the note table, and the stereo output.");
    add ("Mood", layout::baseMood.withWidth (30), 'a',
         "The drop steps through the colours of the panel: Cream (the SubverseLab face), Night, Powder and Pink. Only the look changes; the sound, the layout and your project stay the same. {key:M} does the same.");
    // one rule for every marker: centred on the top-right corner of its control's outline, kMarkerDx right and kMarkerDy up
    for (const auto& e : entries)
        markers.push_back (juce::Rectangle<int> (kMarkerD, kMarkerD).withCentre ({ e.target.getRight() + kMarkerDx, e.target.getY() + kMarkerDy }));
}

void HelpOverlay::open (int index)
{
    if (opened) return;
    build();
    opened = true;
    setBounds (0, 0, kW, kH);
    setVisible (true);
    toFront (false);
    openTip (index);
}

void HelpOverlay::close()
{
    if (! opened) return;
    opened = false;
    setVisible (false);
    markers.clear();
    entries.clear();
    if (closedCb) closedCb();
}

void HelpOverlay::openTip (int i)
{
    if (entries.empty()) return;
    at = ((i % count()) + count()) % count();
    layoutTip();
    repaint();
}

void HelpOverlay::layoutTip()
{
    const auto& e = entries[(size_t) at];
    const float inner = kTipW - 2.0f * kPadX;
    // words and inline glyphs, wrapped
    std::vector<Item> items;
    const auto body = heros (kTextSize);
    const auto kf = display (12.0f);
    int i = 0; const auto& s = e.text;
    while (i < s.length())
    {
        if (s[i] == '{')
        {
            const int close = s.indexOfChar (i, '}');
            const auto tag = s.substring (i + 1, close);
            const bool glued = i > 0 && s[i - 1] == '}';
            if (tag.startsWith ("ico:")) items.push_back ({ Item::icon, tag.substring (4), 16.0f, glued });
            else items.push_back ({ Item::key, tag.substring (4), juce::jmax (14.0f, trackedWidth (tag.substring (4), kf, 0.0f) + 8.0f), glued });
            i = close + 1;
        }
        else if (s[i] == ' ') ++i;
        else
        {
            int j = i;
            while (j < s.length() && s[j] != ' ' && s[j] != '{') ++j;
            const auto w = s.substring (i, j);
            items.push_back ({ Item::word, w, juce::GlyphArrangement::getStringWidth (body, w), false });
            i = j;
        }
    }
    lines.clear();
    std::vector<Item> cur; float x = 0.0f;
    const float space = 3.4f;
    for (size_t k = 0; k < items.size(); ++k)
    {
        // glued items (an icon right after another, a key right after a key) have no space between them
        const bool glue = items[k].glued;
        const float add = (cur.empty() || glue ? 0.0f : space) + items[k].w;
        if (x + add > inner && ! cur.empty()) { lines.push_back (cur); cur.clear(); x = 0.0f; }
        x += (cur.empty() ? 0.0f : (glue ? 0.0f : space)) + items[k].w;
        cur.push_back (items[k]);
    }
    if (! cur.empty()) lines.push_back (cur);

    const float h = kPadY + titleH + (float) lines.size() * lineH + 9.0f + 24.0f + kPadY;
    const auto t = e.target.toFloat();
    const auto mk = markers[(size_t) at].toFloat();
    const juce::Rectangle<float> panel (8.0f, 8.0f, (float) kW - 16.0f, (float) kH - 16.0f);
    // where the pointer touches the control: near its marker (the top-right corner), never past the control's own edges
    const float aimX = t.getRight() - juce::jmin (t.getWidth() * 0.5f, 24.0f);
    const float aimY = t.getY() + juce::jmin (t.getHeight() * 0.5f, 24.0f);
    auto place = [&] (char sd) -> juce::Rectangle<float>
    {   // the popover beside the control on side sd, slid along that side to stay inside the panel
        float x0 = 0, y0 = 0;
        if (sd == 'b' || sd == 'a')
        {
            x0 = juce::jlimit (panel.getX(), panel.getRight() - kTipW, aimX - kTipW * 0.5f);
            y0 = sd == 'b' ? t.getBottom() + 12.0f : t.getY() - 18.0f - h;     // above: clear of the marker that sits over the corner
        }
        else
        {
            x0 = sd == 'l' ? t.getX() - 12.0f - kTipW : t.getRight() + 14.0f;   // right: clear of the marker beside the corner
            y0 = juce::jlimit (panel.getY(), panel.getBottom() - h, aimY - 24.0f);
        }
        return { x0, y0, kTipW, h };
    };
    auto fits = [&] (juce::Rectangle<float> r)
    {
        return panel.contains (r) && ! r.intersects (t.expanded (3.0f)) && ! r.intersects (mk.expanded (1.0f));
    };
    char side = e.side;
    auto r = place (side);
    if (! fits (r))
        for (char sd : { 'b', 'a', 'r', 'l' })
            if (sd != e.side && fits (place (sd))) { side = sd; r = place (sd); break; }
    if (! fits (r)) { side = e.side; r = place (side).constrainedWithin (panel); }   // never reached by the panel's 22 targets (ui_snapshot checks it)
    tipRect = r.getSmallestIntegerContainer();
    const auto tr = tipRect.toFloat();
    arrowSide = side;
    if (side == 'b' || side == 'a')
    {
        arrowAt = juce::jlimit (14.0f, kTipW - 14.0f, juce::jlimit (t.getX() + 6.0f, t.getRight() - 6.0f, aimX) - tr.getX());
        arrowLen = side == 'b' ? tr.getY() - t.getBottom() - 3.0f : t.getY() - tr.getBottom() - 3.0f;   // the point stops 3 px short of the control
        arrowTip = { tr.getX() + arrowAt, side == 'b' ? tr.getY() - arrowLen : tr.getBottom() + arrowLen };
    }
    else
    {
        arrowAt = juce::jlimit (14.0f, h - 14.0f, juce::jlimit (t.getY() + 6.0f, t.getBottom() - 6.0f, aimY) - tr.getY());
        arrowLen = side == 'r' ? tr.getX() - t.getRight() - 3.0f : t.getX() - tr.getRight() - 3.0f;
        arrowTip = { side == 'r' ? tr.getX() - arrowLen : tr.getRight() + arrowLen, tr.getY() + arrowAt };
    }
    prevRect = { tipRect.getRight() - 12 - 26 - 6 - 26, tipRect.getBottom() - (int) kPadY - 24, 26, 24 };
    nextRect = { tipRect.getRight() - 12 - 26, prevRect.getY(), 26, 24 };
}

void HelpOverlay::mouseDown (const juce::MouseEvent&) {}

void HelpOverlay::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (prevRect.contains (p)) { step (-1); return; }
    if (nextRect.contains (p)) { step (1); return; }
    if (tipRect.contains (p)) return;
    if (const int m = markerAt (p); m >= 0) { if (e.mouseWasClicked()) openTip (m); return; }
    close();
}

void HelpOverlay::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const bool hand = prevRect.contains (p) || nextRect.contains (p) || (! tipRect.contains (p) && markerAt (p) >= 0);
    setMouseCursor (hand ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void HelpOverlay::paint (juce::Graphics& g)
{
    if (entries.empty()) return;
    const auto& p = ctx.p();
    g.setColour (p.veil); g.fillRoundedRectangle (getLocalBounds().toFloat(), 22.0f);
    const auto& e = entries[(size_t) at];
    // the control the popover is about
    g.setColour (p.gold); g.drawRoundedRectangle (e.target.toFloat().expanded (2.0f), 8.0f, 2.0f);
    // every marker first, the open one too: the popover below is painted over all of them
    if (drawMarkersForTest)
        for (int i = 0; i < (int) markers.size(); ++i) paintMarker (g, i);

    const auto r = tipRect.toFloat();
    g.setColour (p.shadow); g.fillRoundedRectangle (r.translated (0.0f, 6.0f).expanded (2.0f), 12.0f);
    g.setColour (p.ink); g.fillRoundedRectangle (r, 10.0f);
    {   // the pointer
        juce::Path d;
        const float a = arrowAt, L = juce::jlimit (6.0f, 14.0f, arrowLen);
        if (arrowSide == 'b') d.addTriangle (r.getX() + a - 7, r.getY(), r.getX() + a + 7, r.getY(), r.getX() + a, r.getY() - L);
        else if (arrowSide == 'a') d.addTriangle (r.getX() + a - 7, r.getBottom(), r.getX() + a + 7, r.getBottom(), r.getX() + a, r.getBottom() + L);
        else if (arrowSide == 'r') d.addTriangle (r.getX(), r.getY() + a - 7, r.getX(), r.getY() + a + 7, r.getX() - L, r.getY() + a);
        else d.addTriangle (r.getRight(), r.getY() + a - 7, r.getRight(), r.getY() + a + 7, r.getRight() + L, r.getY() + a);
        g.fillPath (d);
    }
    g.setColour (p.tipAccent);
    drawTracked (g, e.title.toUpperCase(), { r.getX() + kPadX, r.getY() + kPadY - 1.0f, r.getWidth() - 2 * kPadX, titleH }, display (15.0f), 0.06f, juce::Justification::centredLeft);
    const auto body = heros (kTextSize), kf = display (12.0f);
    float y = r.getY() + kPadY + titleH;
    for (const auto& line : lines)
    {
        float x = r.getX() + kPadX;
        for (size_t k = 0; k < line.size(); ++k)
        {
            const auto& it = line[k];
            const bool glued = it.glued;
            if (k > 0 && ! glued) x += 3.4f;
            if (it.kind == Item::word) { g.setColour (p.cream); g.setFont (body); g.drawText (it.text, juce::Rectangle<float> (x, y, it.w + 4.0f, lineH), juce::Justification::centredLeft, false); }
            else if (it.kind == Item::icon) drawGlyph (g, GlyphSet::icon, it.text, { x, y + (lineH - 14.0f) * 0.5f + 1.0f, 14.0f, 14.0f }, p.cream);
            else
            {
                const juce::Rectangle<float> kb (x, y + (lineH - 15.0f) * 0.5f, it.w, 15.0f);
                g.setColour (p.rdDim); g.drawRoundedRectangle (kb.reduced (0.5f), 3.0f, 1.0f);
                g.setColour (p.cream); drawTracked (g, it.text, kb, kf, 0.0f, juce::Justification::centred);
            }
            x += it.w;
        }
        y += lineH;
    }
    // nav
    g.setColour (p.rdDim);
    drawTracked (g, juce::String (at + 1) + " / " + juce::String (count()), { r.getX() + kPadX, (float) prevRect.getY(), 80.0f, 24.0f }, display (13.0f), 0.0f, juce::Justification::centredLeft);
    for (auto* b : { &prevRect, &nextRect })
    {
        const auto br = b->toFloat();
        g.setColour (p.rdDim); g.drawRoundedRectangle (br.reduced (0.75f), 6.0f, 1.5f);
        drawGlyph (g, GlyphSet::icon, b == &prevRect ? "chevL" : "chevR", juce::Rectangle<float> (14.0f, 14.0f).withCentre (br.getCentre()), p.cream);
    }
}
} // namespace ru8ui
