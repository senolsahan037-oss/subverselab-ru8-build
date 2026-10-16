#include "Front.h"

namespace ru8ui
{
namespace
{
const juce::String kMinus = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
const char* const kPreviewTip = "Preview: play / stop at the host's tempo while the host is stopped";
const char* const kHostTip = "The host's transport is playing RU-8";
constexpr int kMeterBars = 18;
}

//==============================================================================
/** A printed or lit part of the front that takes no clicks. It paints with front coordinates, so every part keeps to its own small
    bounds: when the water repaints a corner of the panel only the parts that are there paint again. */
class FrontPanel::Plate : public juce::Component
{
public:
    Plate (juce::Rectangle<int> b, std::function<void (juce::Graphics&)> p, std::function<juce::String()> k) : painter (std::move (p)), keyFn (std::move (k))
    {
        setInterceptsMouseClicks (false, false);
        setBounds (b);
    }
    void paint (juce::Graphics& g) override
    {
        // one picture per content (the printing is text and glyphs: slow to set again on every repaint)
        const auto key = keyFn();
        if (! img.isValid() || key != imgKey)
        {
            img = juce::Image (juce::Image::ARGB, getWidth() * 2, getHeight() * 2, true);
            juce::Graphics gi (img);
            gi.addTransform (juce::AffineTransform::translation ((float) -getX(), (float) -getY()).scaled (2.0f));
            painter (gi);
            imgKey = key;
        }
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (img, getLocalBounds().toFloat());
    }
    std::function<void (juce::Graphics&)> painter;
    std::function<juce::String()> keyFn;
    juce::Image img;
    juce::String imgKey;
};

void FrontPanel::buildPlates()
{
    using namespace layout;
    auto add = [this] (juce::Rectangle<int> b, std::function<void (juce::Graphics&)> p, std::function<juce::String()> k = {}) -> Plate&
    {
        if (! k) k = [this] { return ctx.proc.mood; };
        plates.push_back (std::make_unique<Plate> (b, std::move (p), std::move (k)));
        addAndMakeVisible (*plates.back());
        return *plates.back();
    };
    // product, top left
    add (productBox.withTrimmedTop (30).withTrimmedBottom (8), [this] (juce::Graphics& g)
    {
        const auto& p = ctx.p();
        const auto bf = display (40.0f, 0.78f);
        const float wRU = trackedWidth ("RU", bf, 0.02f), wDash = trackedWidth ("-", bf, 0.02f), w8 = trackedWidth ("8", bf, 0.02f);
        float x = (float) productBox.getCentreX() - (wRU + wDash + w8) * 0.5f;
        const juce::Rectangle<float> row (0.0f, 55.0f, 200.0f, 40.0f);
        g.setColour (p.ink); drawTracked (g, "RU", row.withX (x), bf, 0.02f, juce::Justification::centredLeft); x += wRU;
        g.setColour (p.goldDeep); drawTracked (g, "-", row.withX (x), bf, 0.02f, juce::Justification::centredLeft); x += wDash;
        g.setColour (p.ink); drawTracked (g, "8", row.withX (x), bf, 0.02f, juce::Justification::centredLeft);
        g.setColour (p.inkSoft);
        drawTracked (g, "RHYTHM UNIT", { (float) productBox.getX(), 95.0f, (float) productBox.getWidth(), 20.0f }, display (15.0f, 0.82f), 0.22f, juce::Justification::centred);
    });
    auto head = [this] (juce::Graphics& g, float x, float y, float h, const juce::String& icon, const juce::String& label)
    {
        const auto& p = ctx.p();
        drawGlyph (g, GlyphSet::icon, icon, { x, y + (h - 16.0f) * 0.5f, 16.0f, 16.0f }, p.goldDeep);
        g.setColour (p.goldDeep);
        drawTracked (g, label, { x + 23.0f, y, 150.0f, h }, heros (11.0f, true), 0.24f, juce::Justification::centredLeft);
    };
    channelHead = &add ({ leftX - 2, leftT1, railW + 4, 22 }, [this, head] (juce::Graphics& g)
    {
        const auto& p = ctx.p();
        head (g, (float) leftX, (float) leftT1, 22.0f, "channel", "CHANNEL");
        g.setColour (p.ink);   // the voice shown in CHANNEL (the prototype prints it in capitals)
        drawTracked (g, padName (ctx.selectedPad, ctx.kit()).toUpperCase(), { (float) leftX + 90.0f, (float) leftT1, (float) railW - 90.0f, 22.0f }, display (18.0f), 0.03f, juce::Justification::centredRight, true);
    }, [this] { return ctx.proc.mood + juce::String (ctx.selectedPad) + ":" + juce::String (ctx.kit()); });
    add ({ leftX - 2, leftT2, railW + 4, 16 }, [head] (juce::Graphics& g) { head (g, (float) leftX, (float) leftT2, 16.0f, "master", "MASTER"); });
    add ({ rightX - 2, rightT1, railW + 4, 16 }, [head] (juce::Graphics& g) { head (g, (float) rightX, (float) rightT1, 16.0f, "space", "SPACE"); });
    add ({ rightX - 2, rightT2, railW + 4, 16 }, [head] (juce::Graphics& g) { head (g, (float) rightX, (float) rightT2, 16.0f, "echo", "ECHO"); });
    add ({ 21, leftT1 + 249, 212, 2 }, [this] (juce::Graphics& g) { g.setColour (ctx.p().rule); g.fillRect (21.0f, (float) leftT1 + 251.0f - 1.5f, 212.0f, 1.5f); });
    add ({ rightX - 2, rightT1 + 241, 212, 2 }, [this] (juce::Graphics& g) { g.setColour (ctx.p().rule); g.fillRect ((float) rightX - 2.0f, (float) rightT1 + 243.0f - 1.5f, 212.0f, 1.5f); });
    // the master meter: 18 bars, the last three hot
    meterPlate = &add ({ leftX, leftT2 + 26, railW, 36 }, [this] (juce::Graphics& g)
    {
        const auto& p = ctx.p();
        const float x0 = (float) leftX, y0 = (float) leftT2 + 26.0f, w = (float) railW - 42.0f, h = 34.0f;
        const float barW = (w - 3.0f * (float) (kMeterBars - 1)) / (float) kMeterBars, inner = h - 3.0f - 1.5f;
        const int lit = meterLit();
        for (int i = 0; i < kMeterBars; ++i)
        {
            const float bh = inner * (0.18f + 0.82f * std::pow ((float) i / (float) (kMeterBars - 1), 0.8f));
            const bool on = i < lit, hot = i >= kMeterBars - 3;
            g.setColour (! on ? p.rule : hot ? p.gold : p.ink);
            juce::Path bar; bar.addRoundedRectangle (x0 + (float) i * (barW + 3.0f), y0 + inner - bh, barW, bh, 2.0f, 2.0f, true, true, false, false);
            g.fillPath (bar);
        }
        g.setColour (p.inkSoft); g.fillRect (x0, y0 + h - 1.5f, w, 1.5f);
        g.setColour (p.ink); drawTracked (g, meterText(), { x0 + w + 6.0f, y0 + 1.0f, 36.0f, 18.0f }, display (18.0f), 0.0f, juce::Justification::centredRight);
        g.setColour (p.inkSoft); drawTracked (g, grText(), { x0 + w - 14.0f, y0 + 21.0f, 52.0f, 12.0f }, display (11.0f), 0.04f, juce::Justification::centredRight);
    }, [this] { return ctx.proc.mood + juce::String (meterLit()) + meterText() + grText(); });
}

//==============================================================================
FrontPanel::FrontPanel (Ctx& c, std::function<void()> onFlip, std::function<void()> onMood, std::function<void()> onHelp,
                        std::function<void()> onPresets, std::function<bool()> presetsOpen)
    : water (c), readout (c), bed (c, *this), ctx (c), flipCb (std::move (onFlip)), moodCb (std::move (onMood)), helpCb (std::move (onHelp)),
      presetsCb (std::move (onPresets)), presetsOpenFn (std::move (presetsOpen))
{
    setInterceptsMouseClicks (false, true);
    addAndMakeVisible (water);
    buildPlates();
    addAndMakeVisible (readout);
    addAndMakeVisible (bed);

    // ---- CHANNEL
    using namespace layout;
    struct S { const char* id; const char* label; const char* icon; };
    const S strip[6] = { { "level", "Level", "level" }, { "tune", "Tune", "tune" }, { "decay", "Decay", "decay" }, { "pan", "Pan", "pan" }, { "space", "Space send", "space" }, { "echo", "Echo send", "echo" } };
    for (int i = 0; i < 6; ++i)
    {
        KnobCell::Spec sp; sp.paramId = ru8::params::padId (ctx.selectedPad, strip[i].id); sp.label = strip[i].label; sp.icon = strip[i].icon;
        addKnob (sp, { leftX + 70 * (i % 3), leftT1 + 32 + 86 * (i / 3), 68, 78 }, &stripKnobs);
    }
    auto addRowBtn = [&] (std::unique_ptr<PanelButton>& b, const char* icon, const char* tip, int x)
    {
        b = std::make_unique<PanelButton> (ctx, PanelButton::Kind::rowbtn);
        b->icon = icon; b->setTooltip (tip); b->setFace ({ x, leftT1 + 206, 38, 32 });
        addAndMakeVisible (*b);
    };
    addRowBtn (muteBtn, "mute", "Mute", 62);
    addRowBtn (soloBtn, "solo", "Solo", 108);
    addRowBtn (resetBtn, "reset", "Reset this pad to the kit", 154);
    muteBtn->isOn = [this] { return ctx.padMute[(size_t) ctx.selectedPad]->load() > 0.5f; };
    soloBtn->isOn = [this] { return ctx.padSolo[(size_t) ctx.selectedPad]->load() > 0.5f; };
    muteBtn->onClick = [this] { act::setParam (ctx.proc, ru8::params::padId (ctx.selectedPad, "mute"), ctx.padMute[(size_t) ctx.selectedPad]->load() > 0.5f ? 0.0f : 1.0f); };
    soloBtn->onClick = [this] { act::setParam (ctx.proc, ru8::params::padId (ctx.selectedPad, "solo"), ctx.padSolo[(size_t) ctx.selectedPad]->load() > 0.5f ? 0.0f : 1.0f); };
    resetBtn->onClick = [this]
    {
        svl::ParamUndo::Group grp (ctx.proc.undo, "Reset pad");
        for (auto* k : stripKnobs) k->resetToKit();
    };

    // ---- MASTER
    {
        const S m[4] = { { "swing", "Swing", "swing" }, { "human", "Human", "human" }, { "glue", "Glue", "glue" }, { "volume", "Volume", "volume" } };
        for (int i = 0; i < 4; ++i)
        {
            KnobCell::Spec sp; sp.paramId = m[i].id; sp.label = m[i].label; sp.icon = m[i].icon;
            addKnob (sp, { leftX + 105 * (i % 2), leftT2 + 70 + 86 * (i / 2), 103, 78 }, &masterKnobs);
        }
    }
    // ---- SPACE and ECHO
    {
        const char* names[3] = { "Room", "Plate", "Hall" }; const char* icons[3] = { "room", "plate", "hall" };
        for (int i = 0; i < 3; ++i)
        {
            auto b = std::make_unique<PanelButton> (ctx, PanelButton::Kind::seg);
            b->icon = icons[i]; b->setTooltip (names[i]);
            b->setFace ({ rightX + juce::roundToInt (70.67f * (float) i), rightT1 + 26, 67, 30 });
            b->isOn = [this, i] { return juce::roundToInt (ctx.proc.apvts.getRawParameterValue ("spaceType")->load()) == i; };
            b->onClick = [this, i] { act::setParam (ctx.proc, "spaceType", (float) i); };
            addAndMakeVisible (*b); spaceSeg.push_back (std::move (b));
        }
        const S sp[4] = { { "spaceSize", "Size", "size" }, { "spaceDamp", "Damp", "damp" }, { "spacePre", "Pre-delay", "predelay" }, { "spaceReturn", "Return", "ret" } };
        for (int i = 0; i < 4; ++i)
        {
            KnobCell::Spec s; s.paramId = sp[i].id; s.label = sp[i].label; s.icon = sp[i].icon;
            addKnob (s, { rightX + 105 * (i % 2), rightT1 + 66 + 86 * (i / 2), 103, 78 }, &spaceKnobs);
        }
        const char* times[4] = { "1/16 D", "1/8", "1/8 D", "1/4" };
        for (int i = 0; i < 4; ++i)
        {
            auto b = std::make_unique<PanelButton> (ctx, PanelButton::Kind::seg);
            b->text = times[i]; b->setTooltip (juce::String ("Echo time ") + times[i]);
            b->setFace ({ rightX + 53 * i, rightT2 + 26, 49, 30 });
            b->isOn = [this, i] { return juce::roundToInt (ctx.proc.apvts.getRawParameterValue ("echoTime")->load()) == i; };
            b->onClick = [this, i] { act::setParam (ctx.proc, "echoTime", (float) i); };
            addAndMakeVisible (*b); echoSeg.push_back (std::move (b));
        }
        const S ek[3] = { { "echoFeedback", "Feedback", "feedback" }, { "echoTone", "Tone", "damp" }, { "echoReturn", "Return", "ret" } };
        for (int i = 0; i < 3; ++i)
        {
            KnobCell::Spec s; s.paramId = ek[i].id; s.label = ek[i].label; s.icon = ek[i].icon;
            addKnob (s, { rightX + 105 * (i % 2), rightT2 + 66 + 86 * (i / 2), 103, 78 }, &echoKnobs);
        }
    }

    // ---- transport: the standalone's own; in a host a preview at the host's tempo while the host is stopped
    //      (the host's transport wins when it runs: the key shows it running, dimmed, and takes no clicks), help
    helpBtn = std::make_unique<PanelButton> (ctx, PanelButton::Kind::help);
    helpBtn->setTooltip (ctx.standalone ? "Help (H)" : "Help"); helpBtn->setFace (layout::helpBtn); helpBtn->onClick = [this] { if (helpCb) helpCb(); };
    addAndMakeVisible (*helpBtn);
    startBtn = std::make_unique<PanelButton> (ctx, PanelButton::Kind::start);
    startBtn->setTooltip (ctx.standalone ? "Start / stop (Space)" : kPreviewTip);
    startBtn->setFace (layout::startBtn);
    startBtn->icon = "play";
    startBtn->isRunning = [this] { return ctx.proc.display().playing; };
    startBtn->isUsable = [this] { return ctx.standalone || ! ctx.proc.display().hostPlaying; };
    startBtn->onClick = [this] { startStop(); };
    addAndMakeVisible (*startBtn);
    // tap and tempo: in a host they stay in place, faint, and take no clicks (the host's tempo rules; the readout says HOST)
    tapBtn = std::make_unique<PanelButton> (ctx, PanelButton::Kind::gbtn);
    tapBtn->icon = "tap"; tapBtn->setFace (layout::tapBtn); tapBtn->onClick = [this] { tap(); };
    tapBtn->setTooltip (ctx.standalone ? "Tap tempo (T)" : "Tap tempo: the standalone's; in a host the host's tempo rules");
    tapBtn->isUsable = [this] { return ctx.standalone; };
    addAndMakeVisible (*tapBtn);
    KnobCell::Spec t; t.paramId = "tempo"; t.label = "Tempo"; t.icon = "tempo"; t.knobSize = 50.0f; t.darkTicks = true; t.showValue = false;
    tempoKnob = std::make_unique<KnobCell> (ctx, t);
    tempoKnob->setBounds (layout::tempoKnob.withWidth (65));
    if (! ctx.standalone)
    {
        tempoKnob->setAlpha (0.4f);
        tempoKnob->setInterceptsMouseClicks (false, false);
        tempoKnob->setTooltip ("Tempo: the standalone's; in a host the host's tempo rules");
    }
    addAndMakeVisible (*tempoKnob);

    // ---- the pad bed's tools
    auto tool = [&] (PanelButton*& slot, PanelButton::Kind kind, const char* icon, const char* tip)
    {
        auto b = std::make_unique<PanelButton> (ctx, kind);
        b->icon = icon; b->setTooltip (tip);
        slot = b.get();
        bed.addAndMakeVisible (*b);
        bed.tools.push_back (std::move (b));
    };
    tool (bed.dragSolo, PanelButton::Kind::clip, "", "Drag the selected pad's clip into a MIDI track");
    tool (bed.dragFull, PanelButton::Kind::clip, "full", "Drag the full drum clip into a MIDI track");
    tool (bed.save, PanelButton::Kind::tool, "save", "Save MIDI (click); drag the full set into a MIDI track");
    tool (bed.clrSec, PanelButton::Kind::tool, "clrSec", "Clear section");
    tool (bed.clrRow, PanelButton::Kind::tool, "clrRow", "Clear pad");
    tool (bed.copySec, PanelButton::Kind::tool, "copySec", "Copy section to the other section");
    tool (bed.copyPad, PanelButton::Kind::tool, "copyPad", ctx.standalone ? "Copy pad (Cmd+C)" : "Copy pad");
    tool (bed.pastePad, PanelButton::Kind::tool, "pastePad", ctx.standalone ? "Paste onto pad (Cmd+V)" : "Paste onto pad");
    tool (bed.presetPrev, PanelButton::Kind::tool, "chevL", "Previous preset");
    tool (bed.presetKey, PanelButton::Kind::preset, "", "Presets: the list of the 28 factory presets (a dot: changed since it loaded)");
    tool (bed.presetNext, PanelButton::Kind::tool, "chevR", "Next preset");
    bed.presetPrev->iconSize = bed.presetNext->iconSize = 18.0f;
    bed.presetKey->textOf = [this] { return juce::String (ru8::kPresets[ctx.proc.currentPreset()].name); };
    bed.presetKey->isMarked = [this] { return ctx.proc.presetModified(); };
    bed.presetKey->isOn = [this] { return presetsOpenFn && presetsOpenFn(); };
    bed.presetKey->onClick = [this] { if (presetsCb) presetsCb(); };
    bed.presetPrev->onClick = [this] { stepPreset (-1); };
    bed.presetNext->onClick = [this] { stepPreset (1); };
    bed.dragSolo->voiceGlyph = [this] { const auto g = padGlyph (ctx.selectedPad, ctx.kit()); return juce::String (g.first == GlyphSet::voice ? "voice:" : "type:") + g.second; };
    bed.dragSolo->onDragStart = [this] (const juce::MouseEvent&) { dragClip (false); };
    bed.dragFull->onDragStart = [this] (const juce::MouseEvent&) { dragClip (true); };
    bed.save->onDragStart = [this] (const juce::MouseEvent&) { dragClip (true); };     // the save arrow drags the full set too; a click saves
    bed.dragSolo->onClick = [this] { saveClips(); };
    bed.dragFull->onClick = [this] { saveClips(); };
    bed.save->onClick = [this] { saveClips(); };
    bed.clrSec->onClick = [this] { clearSection(); };
    bed.clrRow->onClick = [this] { clearRow(); };
    bed.copySec->onClick = [this] { copySection(); };
    bed.copySec->small = "B";
    bed.copyPad->onClick = [this] { copyPadSteps(); };
    bed.pastePad->onClick = [this] { pastePadSteps(); };
    bed.pastePad->isUsable = [this] { return clipboardFull(); };
    bed.layoutTools();

    // ---- base: mood, rear panel, lockup
    moodBtn = std::make_unique<PanelButton> (ctx, PanelButton::Kind::mood);
    moodBtn->icon = "mood"; moodBtn->setTooltip (ctx.standalone ? "Mood (M)" : "Mood"); moodBtn->setFace (baseMood); moodBtn->onClick = [this] { if (moodCb) moodCb(); };
    flipBtn = std::make_unique<PanelButton> (ctx, PanelButton::Kind::mood);
    flipBtn->icon = "flip"; flipBtn->setTooltip (ctx.standalone ? "Rear panel (B)" : "Rear panel"); flipBtn->setFace (baseFlip); flipBtn->onClick = [this] { if (flipCb) flipCb(); };
    addAndMakeVisible (*moodBtn); addAndMakeVisible (*flipBtn);
    lockup = std::make_unique<LockupLink> (ctx);
    lockup->setBounds (layout::lockup);
    addAndMakeVisible (*lockup);

    setSize (kW, kH);
    water.setTank (layout::tank);
    lastKit = ctx.kit();
    seenHits = ctx.proc.display().hitCounter;
}

FrontPanel::~FrontPanel() = default;

void FrontPanel::updateWaterMask()
{
    std::vector<juce::Rectangle<int>> cells;
    for (auto& k : knobs) cells.push_back (k->getBounds());
    if (tempoKnob) cells.push_back (tempoKnob->getBounds());
    for (auto* b : { muteBtn.get(), soloBtn.get(), resetBtn.get(), moodBtn.get(), flipBtn.get(), helpBtn.get(), startBtn.get(), tapBtn.get() })
        if (b != nullptr) cells.push_back (b->getBounds());
    for (auto& b : spaceSeg) cells.push_back (b->getBounds());
    for (auto& b : echoSeg) cells.push_back (b->getBounds());
    cells.push_back (layout::lockup);
    water.setCovered ({ layout::readout, layout::bed }, cells);
}

juce::Rectangle<int> FrontPanel::meterBounds() const { return meterPlate != nullptr ? meterPlate->getBounds() : juce::Rectangle<int>(); }

KnobCell& FrontPanel::addKnob (const KnobCell::Spec& s, juce::Rectangle<int> cell, std::vector<KnobCell*>* group)
{
    auto k = std::make_unique<KnobCell> (ctx, s);
    k->setBounds (cell);
    addAndMakeVisible (*k);
    auto* raw = k.get();
    knobs.push_back (std::move (k));
    if (group != nullptr) group->push_back (raw);
    return *raw;
}

KnobCell* FrontPanel::findKnob (const juce::String& id) const
{
    for (auto& k : knobs) if (k->paramId() == id) return k.get();
    if (tempoKnob != nullptr && tempoKnob->paramId() == id) return tempoKnob.get();
    return nullptr;
}

void FrontPanel::resized()
{
    water.setBounds (WaterTank::kInset, WaterTank::kInset, kW - 2 * WaterTank::kInset, kH - 2 * WaterTank::kInset);
    water.setTank (layout::tank);
    updateWaterMask();
    readout.setBounds (layout::readout);
    bed.setBounds (layout::bed);
}

void FrontPanel::paint (juce::Graphics& g)
{
    // The water fills everything inside the border, so only the border and the four corners are drawn here (a whole-panel rounded
    // rectangle is slow to fill under a clip of many small pieces).
    const auto& p = ctx.p();
    const auto clip = g.getClipBounds();
    const float R = 22.0f, w = (float) kW, h = (float) kH;
    g.setColour (p.ink);
    for (auto r : { juce::Rectangle<float> (R, 0.0f, w - 2 * R, 3.0f), juce::Rectangle<float> (R, h - 3.0f, w - 2 * R, 3.0f),
                    juce::Rectangle<float> (0.0f, R, 3.0f, h - 2 * R), juce::Rectangle<float> (w - 3.0f, R, 3.0f, h - 2 * R) })
        if (clip.intersects (r.getSmallestIntegerContainer())) g.fillRect (r);
    for (int c = 0; c < 4; ++c)
    {
        const float x = (c & 1) ? w - R : 0.0f, y = (c & 2) ? h - R : 0.0f;
        const juce::Rectangle<float> sq (x, y, R, R);
        if (! clip.intersects (sq.getSmallestIntegerContainer())) continue;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (sq.getSmallestIntegerContainer());
        const float cx = (c & 1) ? w - R : R, cy = (c & 2) ? h - R : R;
        g.setColour (p.desk); g.fillRect (sq);
        g.setColour (p.ink); g.fillEllipse (cx - R, cy - R, 2 * R, 2 * R);
        g.setColour (p.cream); g.fillEllipse (cx - 19.0f, cy - 19.0f, 38.0f, 38.0f);
    }
}

void FrontPanel::moodChanged()
{
    lockup->moodChanged();
    bed.moodChanged();
    readout.moodChanged();
    water.recolor();
    repaint();
}

int FrontPanel::meterLit() const
{
    if (meterDb <= -48.0f) return 0;
    return juce::jmin (kMeterBars, juce::roundToInt ((meterDb + 48.0f) / (48.0f / (float) kMeterBars)));
}

juce::String FrontPanel::meterText() const { return meterDb <= -90.0f ? kMinus + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) : juce::String (meterDb, 1); }
juce::String FrontPanel::grText() const { return "GR " + juce::String (std::abs (grDb), 1); }

//==============================================================================
void FrontPanel::select (int pad)
{
    pad = juce::jlimit (0, 15, pad);
    if (ctx.selectedPad == pad) return;
    ctx.selectedPad = pad;
    rebindStrip (true);
    channelHead->repaint();
    bed.repaint();
}

void FrontPanel::rebindStrip (bool glide)
{
    const char* ids[6] = { "level", "tune", "decay", "pan", "space", "echo" };
    for (int i = 0; i < 6; ++i) stripKnobs[(size_t) i]->bind (ru8::params::padId (ctx.selectedPad, ids[i]), glide);
}

void FrontPanel::flashPad (int pad, float velocity)
{
    if (pad < 0 || pad > 15) return;
    bed.padAt (pad).flash (velocity);
    water.hit (pad, velocity);
}

void FrontPanel::playPad (int pad, float velocity)
{
    if (pad < 0 || pad > 15) return;
    ctx.proc.hitPad (pad, velocity);
    seenHits = ctx.proc.display().hitCounter;
    flashPad (pad, velocity);
    select (pad);
}

void FrontPanel::stepClicked (int pad, int step)
{
    int hitPad = -1; float vel = 0.0f;
    act::cyclePadStep (ctx, pad, step, hitPad, vel);
    bed.padAt (pad).repaint();
    if (hitPad >= 0) playPad (pad, vel); else select (pad);
}

void FrontPanel::stepPreset (int d)
{
    ctx.proc.loadPreset ((ctx.proc.currentPreset() + d + ru8::kNumPresets) % ru8::kNumPresets);
    bed.repaint();
}

void FrontPanel::copyPadSteps()
{
    clipboard = juce::String (ctx.proc.song().slot[(size_t) ctx.proc.editSlot()].steps[ctx.viewSection()][ctx.selectedPad], 16);
    bed.pastePad->tick();
}

void FrontPanel::pastePadSteps()
{
    if (clipboard.length() != 16) return;
    auto& st = ctx.proc.song().slot[(size_t) ctx.proc.editSlot()].steps[ctx.viewSection()][ctx.selectedPad];
    for (int i = 0; i < 16; ++i) st[i] = (char) clipboard[i];
    bed.padAt (ctx.selectedPad).repaint();
}

void FrontPanel::clearSection()
{
    auto& s = ctx.proc.song().slot[(size_t) ctx.proc.editSlot()].steps[ctx.viewSection()];
    for (auto& pad : s) for (auto& c : pad) c = '.';
    bed.repaint();
}

void FrontPanel::clearRow()
{
    auto& s = ctx.proc.song().slot[(size_t) ctx.proc.editSlot()].steps[ctx.viewSection()][ctx.selectedPad];
    for (auto& c : s) c = '.';
    bed.padAt (ctx.selectedPad).repaint();
}

void FrontPanel::copySection()
{
    auto& slot = ctx.proc.song().slot[(size_t) ctx.proc.editSlot()];
    const int from = ctx.viewSection(), to = 1 - from;
    std::memcpy (slot.steps[to], slot.steps[from], sizeof slot.steps[from]);
    bed.repaint();
}

void FrontPanel::startStop() { ctx.proc.startStop(); }
void FrontPanel::tap() { if (ctx.standalone) ctx.proc.tap(); }

void FrontPanel::dragClip (bool full)
{
    int notes = 0;
    const auto f = writeClipFile (full, &notes);
    if (! f.existsAsFile()) return;
    // JUCE cannot tell where the drop landed: the drag arms MIDI, the clip's first notes while the host plays switch to it
    if (notes > 0) ctx.proc.armMidiSource();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, nullptr);
}

juce::File FrontPanel::writeClipFile (bool full, int* notes)
{
    const auto clip = ctx.proc.makeClip (full, ctx.selectedPad);
    if (notes != nullptr) *notes = clip.notes;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("RU-8 clips");
    if (! dir.createDirectory().wasOk()) return {};
    auto f = dir.getChildFile (clip.name);
    return f.replaceWithData (clip.bytes.data(), clip.bytes.size()) ? f : juce::File();
}

void FrontPanel::saveClips()
{
    const auto full = ctx.proc.makeClip (true, ctx.selectedPad);
    const auto solo = ctx.proc.makeClip (false, ctx.selectedPad);
    chooser = std::make_unique<juce::FileChooser> ("Save the MIDI clips (the full drum, and the selected pad next to it)",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (full.name), "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [full, solo] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f = f.withFileExtension ("mid");
                              f.replaceWithData (full.bytes.data(), full.bytes.size());
                              f.getSiblingFile (solo.name).replaceWithData (solo.bytes.data(), solo.bytes.size());
                          });
}

//==============================================================================
void FrontPanel::tick()
{
    const auto d = ctx.proc.display();
    const bool playing = d.playing;

    // pads played from the panel or by MIDI
    if (d.hitCounter != seenHits)
    {
        seenHits = d.hitCounter;
        flashPad (d.lastHitPad, d.lastHitVel);
    }
    // the sequencer: a pad whose step is not '.' flashes at the 16th that plays; every finished bar drops a ring
    ctx.curStep = playing ? d.step : -1;
    if (playing && d.step >= 0 && d.step != lastStep)
    {
        const auto& steps = ctx.proc.song().slot[(size_t) ctx.proc.currentSlot()].steps[d.section == 2 ? 1 : 0];
        for (int pad = 0; pad < 16; ++pad)
        {
            const char c = steps[pad][d.step];
            if (c != '.') flashPad (pad, c == 'X' ? ru8::kVel.X : c == 'x' ? ru8::kVel.x : c == 'g' ? ru8::kVel.g : ru8::kVel.r);
        }
        if (d.step == 0) water.bar (d.section == 2 ? 1 : 0, d.fill, d.bar);
    }
    lastStep = playing ? d.step : -1;
    if (! playing && lastPlaying) water.stopped();
    lastPlaying = playing;
    if (ctx.fillQueuedUi)
    {
        if (! playing) ctx.fillQueueBar = -1000;
        else if (ctx.fillQueueBar == -1000) ctx.fillQueueBar = d.bar;
        else if (d.bar != ctx.fillQueueBar) { ctx.fillQueuedUi = false; ctx.fillQueueBar = -1000; }
    }
    if (const int kit = ctx.kit(); kit != lastKit) { lastKit = kit; water.stir(); }

    // the meter
    {
        const float db = d.peak > 1.0e-5f ? 20.0f * std::log10 (d.peak) : -100.0f;
        meterDb = juce::jmax (db, meterDb - 3.0f);
        grDb = std::abs (d.gr) > std::abs (grDb) ? d.gr : grDb * 0.85f;
        const auto key = juce::String (meterLit()) + meterText() + grText();
        if (key != lastMeterKey) { lastMeterKey = key; meterPlate->repaint(); }
    }
    readout.refresh (d);
    for (auto& k : knobs) k->tick();
    if (tempoKnob) tempoKnob->tick();
    bed.tick();
    if (startBtn)
    {
        startBtn->icon = playing ? "stop" : "play";                  // dark with a gold square while it runs
        if (! ctx.standalone) startBtn->setTooltip (d.hostPlaying ? kHostTip : kPreviewTip);
    }
    allButtons ([] (PanelButton& b) { b.tick(); });
    const juce::String target = ctx.viewSection() == 0 ? "B" : "A";
    if (bed.copySec->small != target) { bed.copySec->small = target; bed.copySec->repaint(); }
}

void FrontPanel::allButtons (const std::function<void (PanelButton&)>& f)
{
    for (auto* b : { muteBtn.get(), soloBtn.get(), resetBtn.get(), moodBtn.get(), flipBtn.get(), helpBtn.get(), startBtn.get(), tapBtn.get() })
        if (b != nullptr) f (*b);
    for (auto& b : spaceSeg) f (*b);
    for (auto& b : echoSeg) f (*b);
    for (auto& b : bed.tools) f (*b);
}
} // namespace ru8ui
