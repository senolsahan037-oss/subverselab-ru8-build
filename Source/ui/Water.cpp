#include "Water.h"

namespace ru8ui
{
namespace
{
constexpr int kPostA = 0, kPostB = 1, kPostF = 2;
constexpr int kMaxShapes = 6, kMaxBubbles = 70;
constexpr float kGap = 0.85f, kVGap = 2.0f, kGrav = 0.55f;

int kitIndex (Ctx& c) { return juce::jlimit (0, 13, juce::roundToInt (c.proc.apvts.getRawParameterValue ("kit")->load())); }
}

WaterTank::WaterTank (Ctx& c) : ctx (c)
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
    for (auto& s : lastShot) s = -10.0;
}

void WaterTank::setTank (juce::Rectangle<int> t)
{
    haveTank = ! t.isEmpty();
    const float cx = (float) t.getCentreX() - (float) kInset;
    tankBase = (float) t.getBottom() - (float) kInset - 3.0f;
    tankTop = (float) t.getBottom() - (float) kInset - 34.0f;
    postX = { cx - 30.0f, cx, cx + 30.0f };
    for (auto& st : stacks)
        for (size_t k = 0; k < st.size(); ++k) st[k].y = tankBase - 4.0f - (float) k * 5.0f;
    repaint();
}

void WaterTank::setCovered (std::initializer_list<juce::Rectangle<int>> covered, const std::vector<juce::Rectangle<int>>& controls)
{
    covers.clear(); controlRects.clear();
    for (auto r : covered) covers.push_back (r.translated (-kInset, -kInset));
    for (auto r : controls) controlRects.push_back (r.translated (-kInset, -kInset));
    haveMask = true;
    computeCovered();
}

bool WaterTank::bubbleHidden (float x, float y) const
{
    // bubbles live in the clear water between the controls; a control cell is where they pass behind and are not drawn
    const int tx = (int) std::ceil (W / (float) kTile);
    const int ix = (int) x / kTile, iy = (int) y / kTile;
    if (x < 0 || y < 0 || ix >= tx || (size_t) (iy * tx + ix) >= bubbleCovered.size()) return false;
    return bubbleCovered[(size_t) (iy * tx + ix)] != 0;
}

void WaterTank::computeCovered()
{
    const int tx = (int) std::ceil (W / (float) kTile), ty = (int) std::ceil (H / (float) kTile);
    coveredTile.assign ((size_t) (tx * ty), 0);
    bubbleCovered.assign ((size_t) (tx * ty), 0);
    for (int y = 0; y < ty; ++y)
        for (int x = 0; x < tx; ++x)
        {
            const juce::Rectangle<int> t (x * kTile, y * kTile, kTile, kTile);
            for (auto& c : covers) if (c.contains (t)) { coveredTile[(size_t) (y * tx + x)] = 1; bubbleCovered[(size_t) (y * tx + x)] = 1; break; }
            if (! bubbleCovered[(size_t) (y * tx + x)])
                for (auto& c : controlRects) if (c.intersects (t)) { bubbleCovered[(size_t) (y * tx + x)] = 1; break; }
        }
}

juce::Point<float> WaterTank::flyPos (const Flying& f) const
{
    const float px = postX[(size_t) f.post], top = tankTop - 10.0f, sy = tankBase - 4.0f - (float) stacks[(size_t) f.post].size() * 5.0f;
    if (f.t < 0.78f)
    {
        const float u = f.t / 0.78f, cx = (f.x0 + px) / 2, cy = H - P * 0.45f;
        return { (1 - u) * (1 - u) * f.x0 + 2 * (1 - u) * u * cx + u * u * px, (1 - u) * (1 - u) * f.y0 + 2 * (1 - u) * u * cy + u * u * top };
    }
    const float u = (f.t - 0.78f) / 0.22f;
    return { px, top + (sy - top) * u * u };
}

bool WaterTank::busy() const
{
    return ! parts.empty() || ! bubbles.empty() || ! flying.empty() || ! falling.empty() || energy > 0.03f
        || std::any_of (lights.begin(), lights.end(), [] (float l) { return l > 0.02f; });
}

void WaterTank::kick()
{
    if (isShowing() && ! isTimerRunning()) { lastTime = juce::Time::getMillisecondCounterHiRes(); startTimerHz (30); }
}

void WaterTank::timerCallback()
{
    if (! isShowing()) { stopTimer(); return; }
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.05, (now - lastTime) / 1000.0);
    lastTime = now;
    advance (dt);
}

void WaterTank::launch (int id, float vel, float x0)
{
    const float g = kGrav * P;
    const float h = P * (0.14f + 0.34f * vel) * (0.8f + rng.nextFloat() * 0.4f);
    const float ang = (rng.nextFloat() - 0.5f) * 0.8f, v = std::sqrt (2.0f * g * h) * 1.15f;   // 1.15 makes up for the water's drag
    if ((int) parts.size() >= kMaxShapes) parts.erase (parts.begin());
    parts.push_back ({ id, x0, H + size * 0.6f, std::sin (ang) * v, -std::cos (ang) * v, (rng.nextFloat() - 0.5f) * 1.2f, (rng.nextFloat() - 0.5f) * 1.4f,
                       0.4f + 0.6f * vel, size * (0.85f + 0.3f * vel) });
    bubble (x0, 5 + juce::roundToInt (4.0f * vel), 40.0f);
}

void WaterTank::bubble (float x0, int n, float spread)
{
    for (int i = 0; i < n && (int) bubbles.size() < kMaxBubbles; ++i)
        bubbles.push_back ({ x0 + (rng.nextFloat() - 0.5f) * spread, H - 3.0f - rng.nextFloat() * 10.0f, 0.9f + rng.nextFloat() * 2.4f,
                             -(0.05f + rng.nextFloat() * 0.07f) * P, H - P * (0.16f + rng.nextFloat() * 0.18f), rng.nextFloat() * 6.0f, 1.0f });
}

int WaterTank::pixelsPerUnit() const
{
    // the picture of the water has as many pixels as the screen shows for a unit (at most 2), so a piece is copied one to one
    float s = juce::Component::getApproximateScaleFactorForComponent (this);
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) s *= (float) d->scale;
    return juce::jlimit (1, 2, (int) std::ceil (s - 0.01f));
}

void WaterTank::makeBackdrop (int px)
{
    const auto& p = ctx.p();
    bgImg = juce::Image (juce::Image::ARGB, (int) W * px, (int) H * px, true);
    { juce::Graphics gi (bgImg); gi.addTransform (juce::AffineTransform::scale ((float) px)); gi.setGradientFill (juce::ColourGradient (p.waterTop, 0.0f, 0.0f, p.waterBot, 0.0f, H, false)); gi.fillAll(); }
    {   // the glass sheet (165 degrees) is baked into the picture: it sits under the bubbles and shapes here, which nobody can tell apart
        juce::Graphics gi (bgImg);
        gi.addTransform (juce::AffineTransform::scale ((float) px));
        const float a = juce::degreesToRadians (165.0f), dx = std::sin (a), dy = -std::cos (a);
        const float len = std::abs (W * dx) + std::abs (H * dy), cx = W * 0.5f, cy = H * 0.5f;
        juce::ColourGradient gr (p.sheen, cx - dx * len * 0.5f, cy - dy * len * 0.5f, p.sheenLo, cx + dx * len * 0.5f, cy + dy * len * 0.5f, false);
        gr.addColour (0.26, p.sheen.withAlpha (0.0f)); gr.addColour (0.72, p.sheenLo.withAlpha (0.0f));
        gi.setGradientFill (gr); gi.fillAll();
    }
    bgKey = ctx.proc.mood + juce::String (px);
}

juce::Image WaterTank::bubbleImage (int rq, int px)
{
    // a ring of one of a few sizes, drawn once; its opacity is set when it is copied
    const auto key = ctx.proc.mood + ":b" + juce::String (rq) + ":" + juce::String (px);
    auto it = shapeCache.find (key);
    if (it != shapeCache.end()) return it->second;
    juce::Image img (juce::Image::ARGB, 12 * px, 12 * px, true);
    {
        juce::Graphics gi (img);
        gi.addTransform (juce::AffineTransform::scale ((float) px));
        const float r = (float) rq * 0.5f;
        gi.setColour (ctx.p().waterBubble);
        gi.drawEllipse (6.0f - r, 6.0f - r, r * 2.0f, r * 2.0f, 1.0f);
    }
    return shapeCache[key] = img;
}

juce::Image WaterTank::shapeImage (int pad, int kit, bool halo)
{
    // one small picture per shape and look (the shape in its colour, drawn once); drawing it with an opacity is cheap
    const auto key = ctx.proc.mood + ":" + juce::String (pad) + ":" + juce::String (kit) + (halo ? "h" : "-");
    auto it = shapeCache.find (key);
    if (it != shapeCache.end()) return it->second;
    juce::Image img (juce::Image::ARGB, kShapePx, kShapePx, true);
    {
        juce::Graphics gi (img);
        const auto gl = padGlyph (pad, kit);
        drawGlyph (gi, gl.first, gl.second, juce::Rectangle<float> ((float) kShapePx, (float) kShapePx).reduced (halo ? 0.0f : 0.0f), ctx.p().shape[pad % 4], halo ? 3.4f : 2.2f);
    }
    return shapeCache[key] = img;
}

juce::Point<float> WaterTank::reducedSpot (int pad) const
{
    // reduced motion: the shapes do not travel; each lights where it would rest, along the base of the machine
    const float x = pad < 8 ? 112.0f + 50.0f * (float) pad : 700.0f + 40.0f * (float) (pad - 8);
    return { x - (float) kInset, 796.0f - (float) kInset };
}

void WaterTank::hit (int pad, float vel)
{
    if (pad < 0 || pad > 15) return;
    if (ctx.reducedMotion)
    {
        lights[(size_t) pad] = juce::jmax (lights[(size_t) pad], 0.35f + 0.65f * vel);
        const auto c = reducedSpot (pad);
        markDirty (juce::Rectangle<float> (40.0f, 40.0f).withCentre (c));
        kick();
        return;
    }
    bubble (W * (0.04f + rng.nextFloat() * 0.92f), 2 + juce::roundToInt (3.0f * vel), 24.0f);   // every hit lets a few bubbles go
    const double now = simClock >= 0.0 ? simClock : juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (now - lastAny >= kGap && now - lastShot[(size_t) pad] >= kVGap && rng.nextFloat() < 0.3f + 0.5f * vel)
    {
        lastAny = lastShot[(size_t) pad] = now;
        launch (pad, vel, W * (0.06f + rng.nextFloat() * 0.88f));
    }
    energy = juce::jmin (2.0f, energy + 0.1f * vel);
    kick();
}

void WaterTank::stir()
{
    if (! ctx.reducedMotion)
    {
        for (int i = 0; i < 5; ++i) launch (rng.nextInt (16), 0.4f + rng.nextFloat() * 0.5f, W * (0.06f + rng.nextFloat() * 0.88f));
        energy = 2.0f;
    }
    kick();
    repaint();
}

void WaterTank::dropAll()
{
    for (int p = 0; p < 3; ++p)
    {
        for (auto& r : stacks[(size_t) p]) falling.push_back ({ haveTank ? postX[(size_t) p] : W * 0.5f, r.y, 0.0f, 1.0f });
        stacks[(size_t) p].clear();
    }
    flying.clear();
    markDirty ({ 0.0f, tankTop - 20.0f, W, 60.0f });
    kick();
}

void WaterTank::ring (int post)
{
    if (! haveTank) return;
    if (post == kPostA && ! stacks[kPostB].empty()) dropAll();   // a new round: A after B clears the posts
    auto& st = stacks[(size_t) post];
    if (st.size() >= 4)
    {
        for (auto& r : st) falling.push_back ({ postX[(size_t) post], r.y, 0.0f, 1.0f });
        st.clear();
    }
    if (ctx.reducedMotion)
    {
        st.push_back ({ tankBase - 4.0f - (float) st.size() * 5.0f });
        markDirty ({ postX[0] - 20.0f, tankTop - 14.0f, 80.0f, 50.0f });
        return;
    }
    const bool side = rng.nextFloat() < 0.5f;
    flying.push_back ({ post, 0.0f, side ? 30.0f + rng.nextFloat() * W * 0.2f : W - 30.0f - rng.nextFloat() * W * 0.2f, H + 10.0f });
    energy = juce::jmin (2.0f, energy + 0.3f);
    kick();
}

void WaterTank::bar (int section, bool fill, int n)
{
    if (havePrev && n > 0) ring (prevFill ? kPostF : prevSec == 1 ? kPostB : kPostA);
    prevSec = section; prevFill = fill; havePrev = true;
}

void WaterTank::stopped() { havePrev = false; dropAll(); }

float WaterTank::surf (float x) const
{
    return 8.0f + std::sin (x * 0.012f + phase * 1.6f) * (1.0f + energy * 2.0f) + std::sin (x * 0.031f - phase) * (0.5f + energy);
}

void WaterTank::markDirty (juce::Rectangle<float> r)
{
    dirty.add (r.expanded (2.0f).getSmallestIntegerContainer());
}

void WaterTank::advance (float dt)
{
    const float g = kGrav * P;
    for (auto& p : parts)
    {
        p.vy += g * dt; p.vx *= std::exp (-0.6f * dt); p.vy *= std::exp (-0.35f * dt); p.vr *= std::exp (-0.3f * dt);
        p.x += p.vx * dt; p.y += p.vy * dt; p.rot += p.vr * dt;
        if (p.x < p.s / 2) { p.x = p.s / 2; p.vx = std::abs (p.vx) * 0.6f; }
        if (p.x > W - p.s / 2) { p.x = W - p.s / 2; p.vx = -std::abs (p.vx) * 0.6f; }
        if (p.y < p.s / 2 + 12.0f) { p.y = p.s / 2 + 12.0f; p.vy = std::abs (p.vy) * 0.3f; }
        p.lit *= std::exp (-1.6f * dt);
    }
    // the places the moving things were and are: only these repaint
    // (`now`: things that move every tick, repainted where they are and, next tick, where they were; `once`: what is repainted this tick only)
    std::vector<juce::Rectangle<int>> now, once;
    auto note = [&] (juce::Rectangle<float> r) { now.push_back (r.expanded (2.0f).getSmallestIntegerContainer()); };
    auto noteOnce = [&] (juce::Rectangle<float> r) { once.push_back (r.expanded (2.0f).getSmallestIntegerContainer()); };
    // the bubbles are slow and faint: they move every third tick (10 Hz) by three steps, so the water repaints a third as often for them
    bubbleDt += dt;
    if (++bubbleTick % 3 == 0)
    {
        for (auto& b : bubbles)
        {
            if (! bubbleHidden (b.x, b.y)) noteOnce (juce::Rectangle<float> (b.r * 2.0f + 4.0f, b.r * 2.0f + 4.0f).withCentre ({ b.x, b.y }));
            b.t += bubbleDt * 3.0f; b.y += b.vy * bubbleDt; b.x += std::sin (b.t) * 7.0f * bubbleDt;
            if (b.y < b.top) b.life -= bubbleDt * 4.0f;
            if (! bubbleHidden (b.x, b.y)) noteOnce (juce::Rectangle<float> (b.r * 2.0f + 4.0f, b.r * 2.0f + 4.0f).withCentre ({ b.x, b.y }));
        }
        bubbleDt = 0.0f;
    }
    if (bubbleTick % 3 == 0) bubbles.erase (std::remove_if (bubbles.begin(), bubbles.end(), [] (const Bub& b) { return b.life <= 0.0f; }), bubbles.end());
    for (auto& f : flying)
    {
        const auto before = flyPos (f);
        note (juce::Rectangle<float> (30.0f, 18.0f).withCentre (before));
        f.t += dt / 1.3f;
        if (f.t >= 1.0f)
        {
            stacks[(size_t) f.post].push_back ({ tankBase - 4.0f - (float) stacks[(size_t) f.post].size() * 5.0f });
            f.done = true;
            note ({ postX[0] - 24.0f, tankTop - 24.0f, 84.0f, 60.0f });
        }
        else note (juce::Rectangle<float> (30.0f, 18.0f).withCentre (flyPos (f)));
    }
    flying.erase (std::remove_if (flying.begin(), flying.end(), [] (const Flying& f) { return f.done; }), flying.end());
    for (auto& r : falling) { note ({ r.x - 16.0f, r.y - 12.0f, 32.0f, 24.0f }); r.vy += 0.6f * P * dt; r.y += r.vy * dt; r.a -= dt * 1.4f; note ({ r.x - 16.0f, r.y - 12.0f, 32.0f, 24.0f }); }
    falling.erase (std::remove_if (falling.begin(), falling.end(), [this] (const Falling& r) { return r.a <= 0.0f || r.y >= H + 10.0f; }), falling.end());
    parts.erase (std::remove_if (parts.begin(), parts.end(), [this] (const Part& p) { return p.vy > 0.0f && p.y > H + p.s; }), parts.end());
    for (auto& l : lights)
        if (l > 0.0f) { l *= std::exp (-3.0f * dt); if (l < 0.02f) l = 0.0f; }
    for (int i = 0; i < 16; ++i)
        if (lights[(size_t) i] > 0.0f || ctx.reducedMotion) note (juce::Rectangle<float> (40.0f, 40.0f).withCentre (reducedSpot (i)));
    energy *= std::exp (-1.2f * dt);
    if (! ctx.reducedMotion) phase += dt * (1.0f + energy);
    for (auto& p : parts) note (juce::Rectangle<float> (p.s * 1.8f, p.s * 1.8f).withCentre ({ p.x, p.y }));
    if (! ctx.reducedMotion && (energy > 0.03f || ! parts.empty()) && bubbleTick % 2 == 0) noteOnce ({ 0.0f, 0.0f, W, 18.0f });   // the surface, at 15 Hz

    // what changed, in 16 px tiles: the tiles the things were on and are on, minus what is covered by opaque controls
    const int tx = (int) std::ceil (W / (float) kTile), ty = (int) std::ceil (H / (float) kTile);
    if ((int) tiles.size() != tx * ty) { tiles.assign ((size_t) (tx * ty), 0); coveredTile.assign ((size_t) (tx * ty), 0); computeCovered(); }
    auto mark = [&] (juce::Rectangle<int> r)
    {
        r = r.getIntersection ({ 0, 0, (int) W, (int) H });
        if (r.isEmpty()) return;
        for (int y = r.getY() / kTile; y <= (r.getBottom() - 1) / kTile; ++y)
            for (int x = r.getX() / kTile; x <= (r.getRight() - 1) / kTile; ++x)
                tiles[(size_t) (y * tx + x)] = 1;
    };
    for (auto& r : now) mark (r);
    for (auto& r : once) mark (r);
    for (auto& r : lastRects) mark (r);
    for (auto r : dirty) mark (r);
    dirty.clear();
    lastRects = std::move (now);
    juce::RectangleList<int> grid;
    int prevRowStart = -1;
    std::vector<juce::Rectangle<int>> runs, prevRuns;
    for (int y = 0; y < ty; ++y)
    {
        std::vector<juce::Rectangle<int>> row;
        for (int x = 0; x < tx;)
        {
            const size_t i = (size_t) (y * tx + x);
            if (! tiles[i] || coveredTile[i]) { tiles[i] = 0; ++x; continue; }
            int x1 = x;
            while (x1 < tx && tiles[(size_t) (y * tx + x1)] && ! coveredTile[(size_t) (y * tx + x1)]) { tiles[(size_t) (y * tx + x1)] = 0; ++x1; }
            row.push_back ({ x * kTile, y * kTile, (x1 - x) * kTile, kTile });
            x = x1;
        }
        // a run directly under a run of the same width joins it
        for (auto& r : row)
        {
            bool joined = false;
            for (auto& q : runs)
                if (q.getBottom() == r.getY() && q.getX() == r.getX() && q.getWidth() == r.getWidth()) { q.setHeight (q.getHeight() + kTile); joined = true; break; }
            if (! joined) runs.push_back (r);
        }
    }
    juce::ignoreUnused (prevRowStart, prevRuns);
    for (auto& r : runs) { grid.addWithoutMerging (r); repaint (r); }
    lastRepaint = grid;

    if (! busy()) { stopTimer(); lastRects.clear(); }
}

void WaterTank::drawRing (juce::Graphics& g, float x, float y, float a) const
{
    g.setColour (ctx.p().waterRing.withMultipliedAlpha (a));
    g.drawEllipse (x - 11.0f, y - 3.8f, 22.0f, 7.6f, 3.0f);
}

void WaterTank::paint (juce::Graphics& g)
{
    // The clip is what the water (or a control above it) asked to repaint: often many small pieces. The water is copied from a picture
    // made once per look, one to one in screen pixels, so a piece is a plain copy; things are drawn only where the clip reaches them.
    const auto& p = ctx.p();
    const auto clip = g.getClipBounds().getIntersection ({ 0, 0, (int) W, (int) H });
    if (clip.isEmpty()) return;
    // the machine's rounded corners
    if (g.clipRegionIntersects ({ 0, 0, 20, 20 }) || g.clipRegionIntersects ({ (int) W - 20, 0, 20, 20 })
        || g.clipRegionIntersects ({ 0, (int) H - 20, 20, 20 }) || g.clipRegionIntersects ({ (int) W - 20, (int) H - 20, 20, 20 }))
    {
        juce::Path rr; rr.addRoundedRectangle (0.0f, 0.0f, W, H, 19.0f);
        g.reduceClipRegion (rr);
    }
    const int px = pixelsPerUnit();
    if (! bgImg.isValid() || bgKey != ctx.proc.mood + juce::String (px)) makeBackdrop (px);
    auto copyPiece = [&] (juce::Rectangle<int> r)
    {
        r = r.getIntersection (clip);
        if (! r.isEmpty()) g.drawImage (bgImg, r.getX(), r.getY(), r.getWidth(), r.getHeight(), r.getX() * px, r.getY() * px, r.getWidth() * px, r.getHeight() * px);
    };
    const int bandH = 24;
    copyPiece ({ 0, bandH, (int) W, (int) H - bandH });
    if (clip.getY() < bandH)
    {
        // the surface moves in the top band: the water under a wavy line
        juce::Path body;
        body.startNewSubPath (0.0f, H);
        for (float x = 0.0f; x <= W + 12.0f; x += 12.0f) body.lineTo (juce::jmin (x, W), surf (juce::jmin (x, W)));
        body.lineTo (W, H); body.closeSubPath();
        // above the surface the machine's cream shows
        const auto band = clip.getIntersection ({ 0, 0, (int) W, bandH });
        g.setColour (p.cream); g.fillRect (band);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (body);
        copyPiece ({ 0, 0, (int) W, bandH });
    }
    if (clip.getY() < bandH + 16)
    {
        juce::Path line;
        for (float x = 0.0f; x <= W + 12.0f; x += 12.0f) { const float xx = juce::jmin (x, W); if (x == 0.0f) line.startNewSubPath (xx, surf (xx)); else line.lineTo (xx, surf (xx)); }
        g.setColour (p.waterSurf);
        g.strokePath (line, juce::PathStrokeType (1.2f));
    }

    for (const auto& b : bubbles)
    {
        if (bubbleHidden (b.x, b.y)) continue;
        if (! g.clipRegionIntersects (juce::Rectangle<float> (b.r * 2.0f + 6.0f, b.r * 2.0f + 6.0f).withCentre ({ b.x, b.y }).getSmallestIntegerContainer())) continue;
        g.setOpacity (juce::jmax (0.0f, b.life) * 0.6f);
        g.drawImage (bubbleImage (juce::roundToInt (b.r * 2.0f), px), juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ b.x, b.y }));
    }
    g.setOpacity (1.0f);

    const int kit = kitIndex (ctx);
    for (const auto& pt : parts)
    {
        const float d = pt.s * (1.0f + 0.12f * pt.lit);
        if (! g.clipRegionIntersects (juce::Rectangle<float> (d * 1.6f, d * 1.6f).withCentre ({ pt.x, pt.y }).getSmallestIntegerContainer())) continue;
        const float a = juce::jmin (1.0f, p.waterAlpha + 0.35f * pt.lit);
        const auto tf = juce::AffineTransform::scale (d / (float) kShapePx).translated (-d * 0.5f, -d * 0.5f).rotated (pt.rot).translated (pt.x, pt.y);
        if (p.waterGlow && pt.lit > 0.03f)
        {
            g.setOpacity (0.25f * pt.lit);
            g.drawImageTransformed (shapeImage (pt.id, kit, true), tf);
        }
        g.setOpacity (a);
        g.drawImageTransformed (shapeImage (pt.id, kit, false), tf);
    }
    if (ctx.reducedMotion)
        for (int i = 0; i < 16; ++i)
            if (lights[(size_t) i] > 0.0f)
            {
                g.setOpacity (0.12f + 0.7f * lights[(size_t) i]);
                g.drawImage (shapeImage (i, kit, false), juce::Rectangle<float> (22.0f, 22.0f).withCentre (reducedSpot (i)));
            }
    g.setOpacity (1.0f);

    if (haveTank && clip.getBottom() > (int) tankTop - 30 && clip.getX() < (int) postX[2] + 40 && clip.getRight() > (int) postX[0] - 40)
    {
        auto half = [&] (float x, float y, bool front)
        {
            // JUCE measures arcs clockwise from 12 o'clock: the lower (near) half runs 3 -> 9 o'clock, the upper one 9 -> 3
            juce::Path arc;
            arc.addCentredArc (x, y, 11.0f, 3.8f, 0.0f, front ? juce::MathConstants<float>::halfPi : -juce::MathConstants<float>::halfPi,
                               front ? juce::MathConstants<float>::pi * 1.5f : juce::MathConstants<float>::halfPi, true);
            g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        for (int post = 0; post < 3; ++post)
        {
            const float x = postX[(size_t) post];
            g.setColour (p.waterRing);
            for (const auto& r : stacks[(size_t) post]) half (x, r.y, false);   // the back of each ring behind the post
            g.setColour (p.waterPost);
            g.drawLine (x, tankTop, x, tankBase, 2.4f);
            g.drawLine (x - 8.0f, tankBase, x + 8.0f, tankBase, 1.6f);
            g.setColour (p.waterRing);
            for (const auto& r : stacks[(size_t) post]) half (x, r.y, true);    // and the front
            g.setColour (p.waterPost);
            if (post == kPostF)
                for (int k = 0; k < 3; ++k) g.drawLine (x + 7.0f + (float) k * 4.0f, tankTop + 9.0f, x + 9.0f + (float) k * 4.0f, tankTop + 3.0f, 1.3f);
            else
            {
                g.setFont (display (12.0f, 0.85f));
                g.drawText (post == kPostA ? "A" : "B", juce::Rectangle<float> (x + 7.0f, tankTop - 1.0f, 14.0f, 13.0f), juce::Justification::centredLeft, false);
            }
        }
    }
    if (haveTank)
        for (const auto& f : flying)
        {
            const auto pos = flyPos (f);
            if (g.clipRegionIntersects (juce::Rectangle<float> (40.0f, 28.0f).withCentre (pos).getSmallestIntegerContainer())) drawRing (g, pos.x, pos.y, 1.0f);
        }
    if (haveTank)
        for (const auto& r : falling) drawRing (g, r.x, r.y, juce::jmax (0.0f, r.a));

}
} // namespace ru8ui
