#include "Widgets.h"

namespace ru8ui
{
//==============================================================================
// PanelButton
//==============================================================================
PanelButton::PanelButton (Ctx& c, Kind k) : ctx (c), kind (k)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    if (k == Kind::start) iconSize = 28.0f;
    else if (k == Kind::mood) iconSize = 17.0f;
    else if (k == Kind::step) iconSize = 18.0f;
    else if (k == Kind::key) iconSize = 17.0f;
    else if (k == Kind::seg) iconSize = 19.0f;
}

void PanelButton::setFace (juce::Rectangle<int> f) { setBounds (f.withHeight (f.getHeight() + kKeySlack)); }

void PanelButton::tick()
{
    const bool on = isOn && isOn(), run = isRunning && isRunning(), usable = ! isUsable || isUsable();
    const bool blink = isBlink && isBlink();
    const bool phase = blink && ((juce::Time::getMillisecondCounter() / 250) % 2) == 0;
    juce::String glyph = voiceGlyph ? voiceGlyph() : juce::String();
    if (textOf) text = textOf();
    const bool marked = isMarked && isMarked();
    if (on != lastOn || run != lastRun || usable != lastUsable || (blink && phase != lastBlinkPhase) || glyph != lastGlyph || (! blink && lastBlinkPhase)
        || text != lastText || marked != lastMarked)
    {
        lastOn = on; lastRun = run; lastUsable = usable; lastBlinkPhase = blink && phase; lastGlyph = glyph; lastText = text; lastMarked = marked;
        repaint();
    }
}

void PanelButton::click()
{
    if ((! isUsable || isUsable()) && onClick) onClick();
}

void PanelButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    down = true; dragged = false; repaint();
}

void PanelButton::mouseDrag (const juce::MouseEvent& e)
{
    if (onDragStart && ! dragged && e.getDistanceFromDragStart() > 5)
    {
        dragged = true; down = false; repaint();
        onDragStart (e);
    }
}

void PanelButton::mouseUp (const juce::MouseEvent& e)
{
    const bool wasDown = down;
    down = false; repaint();
    if (wasDown && ! dragged && e.mouseWasClicked() && face().contains (e.getPosition())) click();
}

void PanelButton::paint (juce::Graphics& g)
{
    // a key is one picture per look (mood, hover, pressed, lit, glyph): drawing its rounded rectangles and glyph afresh on every repaint is slow
    const bool usable = ! isUsable || isUsable();
    const bool on = isOn && isOn(), run = isRunning && isRunning();
    const bool blink = isBlink && isBlink();
    const bool litLed = on || (blink && ((juce::Time::getMillisecondCounter() / 250) % 2) == 0);
    juce::String key = ctx.proc.mood + (hover ? "h" : "-") + (down ? "d" : "-") + (on ? "o" : "-") + (run ? "r" : "-") + (usable ? "u" : "-") + (litLed ? "l" : "-")
                     + "|" + icon + "|" + text + "|" + small + "|" + juce::String (getWidth()) + (isMarked && isMarked() ? "m" : "-");
    if (voiceGlyph) key << voiceGlyph();
    auto it = faces.find (key);
    if (it == faces.end())
    {
        if (faces.size() > 24) faces.clear();
        juce::Image img (juce::Image::ARGB, getWidth() * 2, getHeight() * 2, true);
        {
            juce::Graphics gi (img);
            gi.addTransform (juce::AffineTransform::scale (2.0f));
            paintFace (gi);
        }
        it = faces.emplace (key, img).first;
    }
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.setOpacity (usable ? 1.0f : 0.4f);   // a key that takes no clicks now is drawn faint (inside the face, a later setColour would undo it)
    g.drawImage (it->second, getLocalBounds().toFloat());
}

void PanelButton::paintFace (juce::Graphics& g) const
{
    const auto& p = ctx.p();
    const bool usable = ! isUsable || isUsable();
    const bool on = isOn && isOn(), run = isRunning && isRunning();
    const bool active = (down && hover && usable) || on || run;
    auto f = face().toFloat();

    auto drawIcon = [&] (const juce::String& name, juce::Rectangle<float> r, juce::Colour c, float sw = 2.0f) { drawGlyph (g, GlyphSet::icon, name, r, c, sw); };

    if (kind == Kind::step)
    {
        if (hover && usable) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (f, 6.0f); }
        const auto c = hover && usable ? p.glassFg : p.rdDim;
        drawIcon (icon, juce::Rectangle<float> (iconSize, iconSize).withCentre (f.getCentre()), c);
        return;
    }

    if (kind == Kind::key)
    {
        g.setColour (juce::Colours::white.withAlpha (0.03f)); g.fillRoundedRectangle (f, 5.0f);
        g.setColour (hover ? p.rdDim : p.rdStep); g.drawRoundedRectangle (f.reduced (0.5f), 5.0f, 1.0f);
        const bool blink = isBlink && isBlink();
        const bool litLed = on || (blink && ((juce::Time::getMillisecondCounter() / 250) % 2) == 0);
        const float ledD = 6.0f, gap = 6.0f;
        const float contentW = icon.isNotEmpty() ? iconSize : trackedWidth (text, display (15.0f), 0.08f);
        const float total = (led ? ledD + gap : 0.0f) + contentW;
        float x = f.getCentreX() - total * 0.5f;
        if (led)
        {
            const juce::Rectangle<float> d (x, f.getCentreY() - ledD * 0.5f, ledD, ledD);
            if (litLed)
            {
                g.setColour (p.gold.withAlpha (0.28f)); g.fillEllipse (d.expanded (3.5f));
                g.setColour (p.gold); g.fillEllipse (d);
                g.setColour (p.goldDeep); g.drawEllipse (d.reduced (0.5f), 1.0f);
            }
            else { g.setColour (p.rdDim); g.drawEllipse (d.reduced (0.5f), 1.0f); }
            x += ledD + gap;
        }
        if (icon.isNotEmpty()) drawIcon (icon, { x, f.getCentreY() - iconSize * 0.5f, iconSize, iconSize }, p.glassFg);
        else { g.setColour (p.glassFg); drawTracked (g, text, { x, f.getY(), contentW + 2.0f, f.getHeight() }, display (15.0f), 0.08f, juce::Justification::centredLeft); }
        return;
    }

    // ---- raised keys: gbtn family and segments
    const bool seg = kind == Kind::seg;
    const float radius = kind == Kind::help ? f.getHeight() * 0.5f : kind == Kind::start ? 12.0f : kind == Kind::mood ? 8.0f : seg ? 7.0f : 10.0f;
    const float border = kind == Kind::gbtn || kind == Kind::start || kind == Kind::help ? 2.0f : 1.5f;
    const bool transparentFace = (kind == Kind::tool || kind == Kind::clip || kind == Kind::preset || seg) && ! active;
    const juce::Colour borderCol = seg ? (on ? p.ink : p.inkSoft) : (kind == Kind::rowbtn || kind == Kind::mood ? p.inkSoft : p.ink);
    juce::Colour bg = transparentFace ? juce::Colours::transparentBlack : (hover && usable && ! active ? p.hover : p.cream);
    juce::Colour fg = kind == Kind::mood ? p.inkSoft : p.ink;
    if (active) { bg = p.ink; fg = p.cream; }
    if (run) fg = p.runFg;

    juce::Graphics::ScopedSaveState ss (g);
    const float shift = active ? 2.0f : 0.0f;
    auto body = f.translated (0.0f, shift);
    if (usable)
    {
        juce::Graphics::ScopedSaveState s2 (g);
        if (transparentFace)   // an outer shadow is not seen through a transparent face
            g.excludeClipRegion (body.reduced (border * 0.5f).toNearestInt());
        drawKeyShadow (g, p, body, radius, active, ! seg);
    }
    if (bg.getAlpha() > 0) { g.setColour (bg); g.fillRoundedRectangle (body, radius); }
    g.setColour (borderCol); g.drawRoundedRectangle (body.reduced (border * 0.5f), radius, border);

    if (kind == Kind::help) { g.setColour (fg); drawTracked (g, "?", body, display (16.0f), 0.0f, juce::Justification::centred); }
    else if (kind == Kind::preset)
    {   // the loaded preset's name, condensed to fit, and the gold dot when anything changed since it loaded
        const float dotD = 7.0f;
        const auto area = body.reduced (10.0f, 0.0f).withTrimmedRight (dotD + 6.0f);
        auto font = display (15.0f);
        const float w = trackedWidth (text, font, 0.04f);
        if (w > area.getWidth()) font = display (15.0f, 0.80f * area.getWidth() / w);
        g.setColour (fg); drawTracked (g, text, area, font, 0.04f, juce::Justification::centred);
        if (isMarked && isMarked())
        {
            const juce::Rectangle<float> d (body.getRight() - 10.0f - dotD, body.getCentreY() - dotD * 0.5f, dotD, dotD);
            g.setColour (p.gold); g.fillEllipse (d);
            g.setColour (p.goldDeep); g.drawEllipse (d.reduced (0.5f), 1.0f);
        }
    }
    else if (seg && icon.isEmpty()) { g.setColour (on ? p.cream : p.inkSoft); drawTracked (g, text, body, display (14.0f), 0.04f, juce::Justification::centred); }
    else if (kind == Kind::clip)
    {
        const bool solo = voiceGlyph != nullptr;
        const float pw = solo ? 19.0f : 22.0f, ow = 15.0f, gap = 3.0f, total = pw + gap + ow;
        float x = body.getCentreX() - total * 0.5f;
        if (solo)
        {
            const auto gl = voiceGlyph();
            const int colon = gl.indexOfChar (':');
            if (colon > 0) drawGlyph (g, gl.startsWith ("voice") ? GlyphSet::voice : GlyphSet::type, gl.substring (colon + 1), { x, body.getCentreY() - pw * 0.5f, pw, pw }, fg, 2.2f);
        }
        else drawIcon (icon, { x, body.getCentreY() - pw * 0.5f, pw, pw }, fg);
        drawIcon ("out", { x + pw + gap, body.getCentreY() - ow * 0.5f, ow, ow }, p.goldDeep);
    }
    else if (icon.isNotEmpty())
        drawIcon (icon, juce::Rectangle<float> (iconSize, iconSize).withCentre (body.getCentre()), fg);
    if (small.isNotEmpty())
    {
        g.setColour (fg);
        drawTracked (g, small, { body.getRight() - 14.0f, body.getBottom() - 14.0f, 11.0f, 13.0f }, display (11.0f), 0.0f, juce::Justification::centredRight);
    }
}

//==============================================================================
// KnobCell
//==============================================================================
KnobCell::KnobCell (Ctx& c, const Spec& s) : ctx (c), spec (s)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setTooltip (s.label);
    bind (s.paramId, false);
}

KnobCell::~KnobCell() { attachment.reset(); }

juce::Rectangle<float> KnobCell::knobRect() const
{
    return juce::Rectangle<float> (spec.knobSize, spec.knobSize).withCentre ({ (float) getWidth() * 0.5f, spec.knobSize * 0.5f });
}

float KnobCell::parameterNorm() const { return param != nullptr ? param->getValue() : 0.0f; }

float KnobCell::gridStep() const
{
    if (param == nullptr) return 0.005f;
    const auto& r = param->getNormalisableRange();
    const float span = r.end - r.start;
    return r.interval > 0.0f && span > 0.0f ? juce::jmax (1.0e-4f, r.interval / span) : 0.005f;
}

void KnobCell::bind (const juce::String& newId, bool glide)
{
    if (gestureOpen) endGesture();
    attachment.reset();
    id = newId;
    param = ctx.proc.apvts.getParameter (id);
    jassert (param != nullptr);
    if (param == nullptr) return;
    bipolar = param->getNormalisableRange().start < 0.0f && std::abs (param->convertTo0to1 (0.0f) - 0.5f) < 0.03f;
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { onParam (v); }, nullptr);
    const float n = param->getValue();
    if (glide) glideTo (n);
    else { drawnNorm = targetNorm = n; isGliding = false; }
    repaint();
}

void KnobCell::glideTo (float n)
{
    targetNorm = n;
    if (held || ! chainVisible (this) || std::abs (n - drawnNorm) < 1.0e-6f) { drawnNorm = n; isGliding = false; }
    else isGliding = true;
    repaint();
}

void KnobCell::onParam (float plain)
{
    if (param == nullptr) return;
    const float n = param->convertTo0to1 (plain);
    if (held) { drawnNorm = targetNorm = n; isGliding = false; repaint(); return; }
    if (forceGlide || ctx.glideArmed()) glideTo (n);
    else { drawnNorm = targetNorm = n; isGliding = false; repaint(); }
}

void KnobCell::tick()
{
    if (! isGliding) return;
    if (held) { drawnNorm = targetNorm; isGliding = false; repaint(); return; }
    const float step = gridStep(), t = targetNorm, v = drawnNorm;
    float move = (t - v) * 0.3f;
    if (std::abs (move) < step) move = t > v ? step : -step;
    float next = v + move;
    if (std::abs (t - v) <= step * 1.01f || (t - next) * (t - v) <= 0.0f) { next = t; isGliding = false; }
    drawnNorm = next;
    repaint();
}

juce::String KnobCell::valueText() const { return param != nullptr ? param->getText (juce::jlimit (0.0f, 1.0f, drawnNorm), 0) : juce::String(); }

void KnobCell::setByHand (float n)
{
    if (param == nullptr || attachment == nullptr) return;
    n = juce::jlimit (0.0f, 1.0f, n);
    const auto& r = param->getNormalisableRange();
    const float plain = r.snapToLegalValue (param->convertFrom0to1 (n));
    if (! gestureOpen) { attachment->beginGesture(); gestureOpen = true; }
    attachment->setValueAsPartOfGesture (plain);
    drawnNorm = targetNorm = param->convertTo0to1 (plain);
    isGliding = false;
    repaint();
}

void KnobCell::endGesture()
{
    if (gestureOpen && attachment != nullptr) attachment->endGesture();
    gestureOpen = false;
}

void KnobCell::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || param == nullptr) return;
    held = true; isGliding = false;
    drawnNorm = targetNorm = param->getValue();
    startNorm = drawnNorm; startY = e.position.y;
}

void KnobCell::mouseDrag (const juce::MouseEvent& e)
{
    if (! held) return;
    const float scale = juce::jmax (0.25f, juce::Component::getApproximateScaleFactorForComponent (this));
    const float sens = (e.mods.isShiftDown() ? 600.0f : 180.0f) / scale;
    setByHand (startNorm + (startY - e.position.y) / sens);
}

void KnobCell::mouseUp (const juce::MouseEvent&)
{
    held = false;
    endGesture();
}

void KnobCell::mouseDoubleClick (const juce::MouseEvent&) { resetToKit(); }

void KnobCell::resetToKit()
{
    if (param == nullptr) return;
    held = false;
    if (gestureOpen) endGesture();
    const auto& r = param->getNormalisableRange();
    const float plain = r.snapToLegalValue (juce::jlimit (r.start, r.end, ctx.proc.kitDefault (id)));   // asked now: the loaded kit's value
    forceGlide = true;
    svl::setParamAsGesture (param, plain);
    forceGlide = false;
    const float n = param->getValue();
    if (std::abs (n - targetNorm) > 1.0e-6f) glideTo (n);
}

void KnobCell::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (param == nullptr || w.deltaY == 0.0f) return;
    held = true;
    setByHand (param->getValue() + (w.deltaY > 0.0f ? 0.02f : -0.02f));
    held = false;
    endGesture();
}

void KnobCell::paintFace (juce::Graphics& g, int litLo, int litHi) const
{
    const auto& p = ctx.p();
    const auto k = knobRect();
    const float s = k.getWidth() / 64.0f, cx = k.getCentreX(), cy = k.getCentreY();
    auto pt = [&] (float x, float y) { return juce::Point<float> (cx + (x - 32.0f) * s, cy + (y - 32.0f) * s); };
    // ticks: 11 graded marks over 270 degrees; lit from the start (or from the centre of a bipolar knob) to the value
    for (int i = 0; i <= 10; ++i)
    {
        const float a = juce::degreesToRadians (-135.0f + 27.0f * (float) i), r1 = 27.0f, r2 = i % 5 == 0 ? 31.5f : 30.5f;
        const bool lit = i >= litLo && i <= litHi, end = i == 0 || i == 10;
        g.setColour (spec.darkTicks && (lit || end) ? p.ink : (lit || end) ? p.inkSoft : p.rule);
        g.drawLine ({ pt (32.0f + r1 * std::sin (a), 32.0f - r1 * std::cos (a)), pt (32.0f + r2 * std::sin (a), 32.0f - r2 * std::cos (a)) }, (end ? 3.0f : 2.2f) * s);
    }
    // cast shadow (blurred), the lower rim, the ring, the hub
    for (int i = 0; i < 4; ++i)
    {
        const float rr = (21.0f + (float) i * 1.1f) * s;
        g.setColour (p.cast.withMultipliedAlpha (0.28f));
        g.fillEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (pt (34.5f, 37.5f)));
    }
    const float R = 21.0f * s;
    const juce::Colour ringCol = hover || held ? p.ink : (spec.darkTicks ? p.ink : p.inkSoft);
    g.setColour (p.capLo); g.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (pt (32.0f, 34.4f)));
    g.setColour (p.inkSoft); g.drawEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (pt (32.0f, 34.4f)).reduced (1.3f * s), 2.6f * s);
    g.setColour (p.cream); g.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (pt (32.0f, 32.0f)));
    g.setColour (ringCol); g.drawEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (pt (32.0f, 32.0f)).reduced (1.3f * s), 2.6f * s);
    g.setColour (p.rule); g.drawEllipse (juce::Rectangle<float> (13.0f * s, 13.0f * s).withCentre (pt (32.0f, 32.0f)), 1.6f * s);
    drawGlyph (g, GlyphSet::icon, spec.icon, juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ cx, k.getBottom() + 10.0f }), p.inkSoft);
}

void KnobCell::paint (juce::Graphics& g)
{
    const auto& p = ctx.p();
    const auto k = knobRect();
    const float s = k.getWidth() / 64.0f, cx = k.getCentreX(), cy = k.getCentreY();
    const float n = juce::jlimit (0.0f, 1.0f, drawnNorm);
    const float lo = bipolar ? juce::jmin (n, 0.5f) : 0.0f, hi = bipolar ? juce::jmax (n, 0.5f) : n;
    const int litLo = juce::roundToInt (std::ceil ((lo - 0.001f) * 10.0f)), litHi = juce::roundToInt (std::floor ((hi + 0.001f) * 10.0f));
    // the face (ticks, rim, ring, glyph) is one picture per look: it changes when a tick lights or the pointer is held
    const juce::String key = ctx.proc.mood + juce::String (litLo) + ":" + juce::String (litHi) + ((hover || held) ? "h" : "-") + spec.icon + juce::String (getWidth());
    if (! face.isValid() || key != faceKey)
    {
        face = juce::Image (juce::Image::ARGB, getWidth() * 2, getHeight() * 2, true);
        juce::Graphics gi (face);
        gi.addTransform (juce::AffineTransform::scale (2.0f));
        paintFace (gi, litLo, litHi);
        faceKey = key;
    }
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImage (face, getLocalBounds().toFloat());
    // pointer
    auto pt = [&] (float x, float y) { return juce::Point<float> (cx + (x - 32.0f) * s, cy + (y - 32.0f) * s); };
    const float ang = juce::degreesToRadians (-135.0f + n * 270.0f);
    const auto a0 = pt (32.0f + 7.0f * std::sin (ang), 32.0f - 7.0f * std::cos (ang)), a1 = pt (32.0f + 18.5f * std::sin (ang), 32.0f - 18.5f * std::cos (ang));
    g.setColour (p.gold);
    g.drawLine ({ a0, a1 }, 3.4f * s);
    g.fillEllipse (juce::Rectangle<float> (3.4f * s, 3.4f * s).withCentre (a0));
    g.fillEllipse (juce::Rectangle<float> (3.4f * s, 3.4f * s).withCentre (a1));
    if (spec.showValue)
    {
        g.setColour (p.inkSoft); g.setFont (heros (10.0f));
        g.drawText (valueText(), juce::Rectangle<float> (0.0f, k.getBottom() + 18.0f, (float) getWidth(), 14.0f), juce::Justification::centred, false);
    }
}

//==============================================================================
// LockupLink
//==============================================================================
LockupLink::LockupLink (Ctx& c) : ctx (c)
{
    mark = juce::ImageFileFormat::loadFrom (BinaryData::mark_cream_png, (size_t) BinaryData::mark_cream_pngSize);
    wordmark = juce::ImageFileFormat::loadFrom (BinaryData::wordmark_cream_png, (size_t) BinaryData::wordmark_cream_pngSize);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("SubverseLab");
}

void LockupLink::paint (juce::Graphics& g)
{
    const auto ink = ctx.p().ink;
    if (! tinted)
    {
        markTint = tintedImage (mark, ink); wordTint = tintedImage (wordmark, ink);
        tinted = true;
    }
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (hover ? 0.8f : 1.0f);
    const float h = (float) getHeight();
    g.drawImage (markTint, juce::Rectangle<float> (0.0f, 0.0f, h * 1.41f, h), juce::RectanglePlacement::stretchToFit);
    const float ww = 60.6f, wh = ww * 90.0f / 698.0f;
    g.drawImage (wordTint, juce::Rectangle<float> ((float) getWidth() - ww, (h - wh) * 0.5f, ww, wh), juce::RectanglePlacement::stretchToFit);
}
} // namespace ru8ui
