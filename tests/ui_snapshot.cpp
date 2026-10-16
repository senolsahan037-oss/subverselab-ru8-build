/*  ui_snapshot: draws the RU-8 panel to PNGs without a screen and runs the panel checks.

        ui_snapshot [outdir]                      (default: docs/ui under the current folder)
        ui_snapshot --cost                        only the water's cost check, six times (for sampling with a profiler)
        ui_snapshot --profile                     how long each part of the panel takes to paint at 200 %
        ui_snapshot --help-pages [outdir]         every help page (plug-in and standalone, four moods, 100 % and 200 %) as PNGs, no checks

    Pictures (the processor really plays a set through its own audio path first, so the readout, the meter, the beat lamps and the
    water show what the machine does):
      front_cream.png front_night.png front_powder.png front_pink.png   the front at 100 % in the four moods (plug-in layout)
      front_200.png                                                     the front at 200 %
      front_standalone.png                                              the standalone's transport (start, tempo, tap)
      front_preview.png front_hostplays.png                             the plug-in's play key: preview running / the host plays (faint)
      front_set.png front_midi.png                                      SET / MIDI beside the set name: SET (host plays the set) / MIDI
      rear.png rear_night.png                                           the rear panel
      help_1.png help_2.png help_3.png                                  the help open on three markers
      pad_hover.png                                                     a pad with its 16 steps (pointer over it)
    Checks (PASS / FAIL, and a total; exit code 0 only if every one held):
      1 glide, 2 double-click, 3 help never resizes, 4 lockup link, 5 layout (no overlap, inside, readout height),
      6 step editing writes the Song; and the rest of the panel's behaviour: arranger keys, keyboard, undo key, drag-out clip,
      reduced motion, mood and width kept, the water's cost (CPU time of the thread at 200 %, the best of six batches of 80 frames:
      the machine may be busy with other work).
*/
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>
#include <ctime>
#include <cstdlib>
#if defined (__APPLE__)
 #include <pthread/qos.h>
 #include <libproc.h>
 #include <sys/resource.h>
 #include <unistd.h>
#endif

using namespace ru8ui;

namespace
{
int gPass = 0, gFail = 0;
juce::File gOut;

/** One check. A FAIL also prints its detail (the values it saw, when the call gives them), its line and its condition as written, so a failure
    in a long loop of runs can be found again without rerunning. */
void checkAt (int line, const char* cond, bool ok, const juce::String& what, const juce::String& detail = {})
{
    if (ok) std::printf ("[PASS] %s\n", what.toRawUTF8());
    else    std::printf ("[FAIL] %s\n       detail: %s\n       condition (ui_snapshot.cpp:%d): %s\n", what.toRawUTF8(),
                         detail.isEmpty() ? "-" : detail.toRawUTF8(), line, cond);
    std::fflush (stdout);
    ok ? ++gPass : ++gFail;
}
#define check(ok, ...) checkAt (__LINE__, #ok, (ok), __VA_ARGS__)

/** CPU time of this thread in milliseconds (the machine may be busy with other work: wall-clock time would count that too). */
#if defined (__APPLE__)
double cpuMs() { timespec t {}; clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t); return (double) t.tv_sec * 1000.0 + (double) t.tv_nsec / 1.0e6; }
#else   // Windows CI: no CLOCK_THREAD_CPUTIME_ID; JUCE's hi-res clock (wall time) stands in
double cpuMs() { return juce::Time::getMillisecondCounterHiRes(); }
#endif

bool same (float a, float b) { return std::abs (a - b) < 1.0e-6f; }

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

/** Waits until the processor's 30 Hz timer has run one complete callback after this point and has nothing left to apply (a kit,
    set or SPACE type load, a program, the MIDI latch; the 'modified' check runs in every callback). A fixed wait (it was 30 or
    60 ms, one or two timer periods) misses the timer on a busy machine. Gives up after 5 s; the longest wait is reported. */
double gSettleWorstMs = 0.0; int gSettles = 0, gSettleTimeouts = 0;
double settle (RU8AudioProcessor& p, double maxMs = 5000.0)
{
    const auto t0 = p.timerTicksForTest();
    const double start = juce::Time::getMillisecondCounterHiRes();
    double ms = 0.0;
    while ((p.timerTicksForTest() == t0 || p.messageWorkPendingForTest()) && ms < maxMs)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        ms = juce::Time::getMillisecondCounterHiRes() - start;
    }
    ++gSettles;
    if (ms >= maxMs) ++gSettleTimeouts;
    gSettleWorstMs = juce::jmax (gSettleWorstMs, ms);
    return ms;
}

struct TestPlayHead : juce::AudioPlayHead
{
    double bpm = 124.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i; i.setBpm (bpm); i.setIsPlaying (playing); i.setPpqPosition (ppq); i.setPpqPositionOfLastBarStart (0.0);
        return i;
    }
};

constexpr int kBlock = 512, kTickEvery = 1600;

/** A processor that really plays, with the editor on it. */
struct Scene
{
    explicit Scene (bool standaloneLayout, int widthPx = 1180)
    {
        RU8Editor::previewAsStandalone = standaloneLayout;
        proc.setRateAndBufferSizeDetails (48000.0, kBlock);
        proc.prepareToPlay (48000.0, kBlock);
        proc.setPlayHead (&ph);
        ed.reset (proc.createEditor());
        editor().setVisible (true);                    // as a host shows it (there is no screen, so isShowing() stays false: the water's timer stays off)
        editor().setSize (widthPx, juce::roundToInt ((float) widthPx * (float) kH / (float) kW));
        editor().stopTicking();                       // the checks tick by hand, so every step is deterministic
        editor().frontPanel().water.setSeed (7);
        RU8Editor::previewAsStandalone = false;
    }
    ~Scene() { proc.setPlayHead (nullptr); }

    RU8Editor& editor() { return *static_cast<RU8Editor*> (ed.get()); }

    /** Plays for `seconds` (block by block); the editor ticks at 30 Hz, the water moves with it. */
    void play (double seconds, bool tick = true)
    {
        const int total = proc.getTotalNumOutputChannels();
        juce::AudioBuffer<float> buf (total, kBlock);
        juce::MidiBuffer midi;
        const int blocks = (int) std::ceil (seconds * 48000.0 / kBlock);
        int since = 0;
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            proc.processBlock (buf, midi);
            ph.ppq += (double) kBlock / 48000.0 * ph.bpm / 60.0;
            since += kBlock;
            if (tick && since >= kTickEvery)
            {
                since -= kTickEvery;
                editor().tickForSnapshot();
                editor().frontPanel().water.advance (1.0f / 30.0f);
            }
        }
    }

    RU8AudioProcessor proc;
    TestPlayHead ph;
    std::unique_ptr<juce::AudioProcessorEditor> ed;
};

void setParam (RU8AudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (plain)); prm->endChangeGesture();
}
float rawParam (RU8AudioProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

bool savePng (const juce::Image& image, const juce::String& name)
{
    auto f = gOut.getChildFile (name);
    f.deleteFile();
    juce::FileOutputStream stream (f);
    return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
}

bool snapshot (juce::Component& c, const juce::String& name)
{
    return savePng (c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f), name);
}

juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, juce::ModifierKeys m = {}, int clicks = 1, bool dragged = false)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, m, juce::MouseInputSource::defaultPressure,
                             juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                             juce::MouseInputSource::defaultTiltY, &c, &c, now, downPos, now, clicks, dragged);
}

void tickN (Scene& s, int n) { for (int i = 0; i < n; ++i) s.editor().tickForSnapshot(); }

/** What can be clicked: visible, takes clicks itself. */
void collectInteractive (juce::Component& root, juce::Component& editor, std::vector<std::pair<juce::Component*, juce::Rectangle<int>>>& out)
{
    for (auto* c : root.getChildren())
    {
        if (! c->isVisible()) continue;
        bool self = false, kids = false;
        c->getInterceptsMouseClicks (self, kids);
        if (self && c->getWidth() > 0 && c->getHeight() > 0)
            out.push_back ({ c, editor.getLocalArea (c, c->getLocalBounds()) });
        collectInteractive (*c, editor, out);
    }
}

juce::String describe (juce::Component* c)
{
    if (auto* t = dynamic_cast<juce::TooltipClient*> (c)) { const auto s = t->getTooltip(); if (s.isNotEmpty()) return s + " " + c->getBounds().toString(); }
    return juce::String (typeid (*c).name()) + " " + c->getBounds().toString();
}

struct LayoutReport { int count = 0; int overlaps = 0; int outside = 0; juce::String firstProblem; };

LayoutReport checkLayout (RU8Editor& editor)
{
    std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> v;
    collectInteractive (editor, editor, v);
    LayoutReport r; r.count = (int) v.size();
    const auto bounds = editor.getLocalBounds().expanded (1);
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (! bounds.contains (v[i].second)) { ++r.outside; if (r.firstProblem.isEmpty()) r.firstProblem = "outside: " + describe (v[i].first); }
        for (size_t j = i + 1; j < v.size(); ++j)
        {
            // two rectangles that only touch (or share a rounding pixel) do not overlap
            const auto x = v[i].second.reduced (1).getIntersection (v[j].second.reduced (1));
            if (! x.isEmpty()) { ++r.overlaps; if (r.firstProblem.isEmpty()) r.firstProblem = "overlap: " + describe (v[i].first) + " / " + describe (v[j].first); }
        }
    }
    return r;
}

// ------------------------------------------------------------------------------------------------ pictures
void pictures()
{
    for (const char* mood : { "cream", "night", "powder", "pink" })
    {
        Scene s (false);
        s.proc.mood = mood;
        s.editor().setMood (mood);
        auto& front = s.editor().frontPanel();
        front.water.stir();
        s.play (2.1);
        s.proc.hitPad (4, 0.9f); s.proc.hitPad (6, 1.0f);
        s.play (0.12);
        front.playPad (0, 1.0f);
        front.water.stir();
        s.play (0.6);
        check (snapshot (s.editor(), juce::String ("front_") + mood + ".png"), juce::String ("picture: front_") + mood + ".png (1180 x 823)");
    }
    {
        Scene s (false, 2360);
        s.editor().frontPanel().water.stir();
        s.play (2.4);
        const auto img = s.editor().createComponentSnapshot (s.editor().getLocalBounds(), true, 1.0f);
        check (img.getWidth() == 2360 && savePng (img, "front_200.png"), "picture: front_200.png (" + juce::String (img.getWidth()) + " x " + juce::String (img.getHeight()) + ")");
    }
    {
        Scene s (true);
        s.editor().frontPanel().water.stir();
        s.play (2.2);
        check (snapshot (s.editor(), "front_standalone.png"), "picture: front_standalone.png (start, tempo, tap)");
    }
    {
        // the play key in a host (P3 Live finding 1): host stopped, the key runs the preview at the host's tempo
        Scene s (false);
        s.ph.playing = false; s.ph.bpm = 96.0;
        auto& front = s.editor().frontPanel();
        s.play (0.2);
        // rule 1: tap and tempo stay in place, faint, and take no clicks; rule 7: the same controls as the standalone
        bool knobClicks = true, knobChildren = true;
        if (front.tempoKnob != nullptr) front.tempoKnob->getInterceptsMouseClicks (knobClicks, knobChildren);
        const bool shown = front.startBtn != nullptr && front.startBtn->isVisible() && front.tapBtn != nullptr && front.tempoKnob != nullptr
                        && ! front.tapBtn->isUsable() && front.tempoKnob->getAlpha() < 0.5f && ! knobClicks;
        const bool idle = ! s.proc.display().playing;
        front.startBtn->click();
        s.play (2.2);
        const auto d = s.proc.display();
        check (shown && idle && d.playing && ! d.hostPlaying && std::abs (d.bpm - 96.0f) < 1e-3f && d.step >= 0
               && front.startBtn->getTooltip().startsWith ("Preview"),
               "transport: the plug-in shows the play key, tap and tempo faint in place; a click with the host stopped runs the preview at the host's 96 BPM (bar "
               + juce::String (d.bar + 1) + ", step " + juce::String (d.step + 1) + ")");
        check (snapshot (s.editor(), "front_preview.png"), "picture: front_preview.png (host stopped, the preview runs)");
        s.ph.playing = true; s.ph.ppq = 0.0;
        s.play (0.5);
        const auto h = s.proc.display();
        const bool faint = front.startBtn->isUsable && ! front.startBtn->isUsable();
        const bool hostTip = front.startBtn->getTooltip().startsWith ("The host");
        front.startBtn->click();                      // takes no clicks while the host plays
        s.play (0.2);
        s.ph.playing = false;
        s.play (0.3);
        const bool stopped = ! s.proc.display().playing;
        check (h.playing && h.hostPlaying && faint && hostTip && stopped,
               "transport: when the host plays, the host wins: the key shows it running and faint, takes no clicks; the host stops, RU-8 stops ("
               + juce::String ((int) h.playing) + juce::String ((int) h.hostPlaying) + juce::String ((int) faint) + juce::String ((int) hostTip) + juce::String ((int) stopped) + ")");
        s.ph.playing = true; s.play (0.3);
        check (snapshot (s.editor(), "front_hostplays.png"), "picture: front_hostplays.png (the host plays: the key faint)");
        // rule 4: in a host RU-8 takes no key at all
        {
            auto& ed = s.editor();
            const auto hits = s.proc.display().hitCounter;
            int taken = 0;
            const juce::String keys ("abcdefghijklmnopqrstuvwxyz1234567890");
            for (auto c : keys) taken += ed.keyPressed (juce::KeyPress ((int) c, {}, c)) ? 1 : 0;
            for (auto c : juce::String ("zcv")) taken += ed.keyPressed (juce::KeyPress ((int) c, juce::ModifierKeys::commandModifier, c)) ? 1 : 0;
            for (int code : { juce::KeyPress::spaceKey, juce::KeyPress::escapeKey, juce::KeyPress::leftKey })
                taken += ed.keyPressed (juce::KeyPress (code)) ? 1 : 0;
            taken += ed.keyStateChanged (true) ? 1 : 0;
            check (taken == 0 && s.proc.display().hitCounter == hits && ! ed.getWantsKeyboardFocus(),
                   "keys: in a host RU-8 takes no key (36 letters/digits, Cmd+Z/C/V, Space, Escape, arrows: " + juce::String (taken) + " taken) and does not ask for focus");
        }
    }
    {
        // SET / MIDI (DAW rule 3): the switch beside the set name; arm by a drag, MIDI by proof or a click
        Scene s (false);
        auto& front = s.editor().frontPanel();
        auto& src = front.readout.srcKey;
        Scene st (true);
        auto& stSrc = st.editor().frontPanel().readout.srcKey;
        st.editor().tickForSnapshot();
        const bool both = src.isVisible() && stSrc.isVisible() && src.getBounds() == stSrc.getBounds() && ! stSrc.isUsable() && src.isUsable();
        s.play (0.3);
        const bool setFirst = src.text == "SET" && ! s.proc.midiSource();
        check (both && setFirst, "set / midi: the switch is in both editions at the same place (faint in the standalone); a host starts in SET");
        check (snapshot (s.editor(), "front_set.png"), "picture: front_set.png (SET, the host plays the set)");
        // an empty clip does not arm: clear the selected pad's row, drag its solo clip
        front.select (15);
        front.clearRow();
        front.dragClip (false);
        const bool emptyNoArm = ! s.proc.midiSourceArmed();
        front.dragClip (true);                        // the full clip has notes
        const bool armed = s.proc.midiSourceArmed() && ! s.proc.midiSource();
        s.play (0.3);
        check (emptyNoArm && armed && src.isBlink() && src.getTooltip().contains ("dragged out"),
               "set / midi: an empty clip does not arm; the full clip arms it (the lamp blinks, the tooltip says why)");
        // the save arrow drags the full set too (rule 5); a click still saves
        s.proc.disarmMidiSource();
        const bool saveDrags = front.bed.save->onDragStart != nullptr && front.bed.save->onClick != nullptr;
        juce::MouseEvent me (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, front.bed.save, front.bed.save, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false);
        front.bed.save->onDragStart (me);
        check (saveDrags && s.proc.midiSourceArmed(), "drag: the save arrow starts a drag of the full set (it arms MIDI like the full-clip key); a click still saves");
        // the proof: the clip's first pad note while the host plays
        juce::AudioBuffer<float> buf (2, kBlock); juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100), 10);
        s.proc.processBlock (buf, midi);
        settle (st.proc);
        s.play (0.3);
        const bool proof = s.proc.midiSource() && src.text == "MIDI" && src.isOn() && ! s.proc.display().playing && rawParam (s.proc, "source") > 0.5f;
        check (proof, "set / midi: the clip's first pad note with the host playing switches to MIDI (word MIDI, lamp lit, the set stops)");
        check (snapshot (s.editor(), "front_midi.png"), "picture: front_midi.png (MIDI: the host plays, the set is silent)");
        // one click back to SET; Cmd+Z in the standalone edition only, so undo is checked through the processor's undo
        src.click();
        s.play (0.3);
        const bool back = ! s.proc.midiSource() && src.text == "SET" && s.proc.display().playing;
        const bool undone = s.proc.undo.undo() && s.proc.midiSource();
        check (back && undone, "set / midi: one click returns to SET (the set plays again); the click is one undo step ("
               + juce::String ((int) back) + juce::String ((int) undone) + ")");
        // help explains it
        s.editor().helpOverlay().open (0);
        bool helpHas = false;
        for (int i = 0; i < s.editor().helpOverlay().count(); ++i) helpHas = helpHas || s.editor().helpOverlay().titleOf (i) == "Set / MIDI";
        s.editor().closeHelp();
        check (helpHas, "help: a Set / MIDI marker explains SET, MIDI, the drag and the way back");
    }
    {
        Scene s (false);
        s.editor().showRear (true);
        s.editor().tickForSnapshot();
        check (snapshot (s.editor(), "rear.png"), "picture: rear.png");
        s.editor().setMood ("night");
        check (snapshot (s.editor(), "rear_night.png"), "picture: rear_night.png");
    }
    {
        Scene s (false);
        auto& front = s.editor().frontPanel();
        front.water.stir();
        s.play (2.0);
        auto& help = s.editor().helpOverlay();
        help.open (0);
        const int n = help.count();
        for (int pick : { 0, 2, 8 })
        {
            help.openTip (pick);
            check (snapshot (s.editor(), "help_" + juce::String (pick == 0 ? 1 : pick == 2 ? 2 : 3) + ".png"),
                   "picture: help_" + juce::String (pick == 0 ? 1 : pick == 2 ? 2 : 3) + ".png (marker " + juce::String (pick + 1) + " of " + juce::String (n) + ": " + help.titleOf (pick) + ")");
        }
        help.close();
    }
    {
        Scene s (false);
        auto& front = s.editor().frontPanel();
        front.water.stir();
        s.play (2.3);
        front.select (0);
        front.bed.padAt (0).setStepsShown (true);
        front.bed.padAt (5).setStepsShown (false);
        s.play (0.2);
        s.editor().tickForSnapshot();
        check (snapshot (s.editor(), "pad_hover.png"), "picture: pad_hover.png (kick with its 16 steps)");
    }
}

// ------------------------------------------------------------------------------------------------ the checks
// 1
void checkGlide()
{
    Scene s (false);
    auto& front = s.editor().frontPanel();
    front.select (3);                               // Rim: its space send differs between kits
    tickN (s, 40);
    auto* k = front.findKnob ("pad4.space");
    check (k != nullptr, "glide: the CHANNEL rail has the selected pad's SPACE knob (pad4.space)");
    if (k == nullptr) return;
    const float start = k->drawn();
    setParam (s.proc, "kit", 4.0f);                 // Deep: the product moves every value of the kit
    settle (s.proc);
    const float target = k->parameterNorm();
    const float plain = rawParam (s.proc, "pad4.space");
    check (std::abs (plain - ru8::kKits[4].ch[3].rev) < 1.0e-3f, "glide: the parameter changed at once (pad4.space " + juce::String (plain, 3) + " = the kit's " + juce::String (ru8::kKits[4].ch[3].rev, 3) + ")");
    check (same (k->drawn(), start) && std::abs (target - start) > 0.05f, "glide: before the first tick the drawing is still at the start (" + juce::String (start, 3) + " -> " + juce::String (target, 3) + ")");
    s.editor().tickForSnapshot();
    const float d1 = k->drawn();
    check ((d1 - start) * (target - d1) > 0.0f && ! same (d1, start) && ! same (d1, target), "glide: after one 30 Hz tick the drawn value is strictly between start and target (" + juce::String (d1, 4) + ")");
    tickN (s, 30);
    check (same (k->drawn(), target) && ! k->gliding(), "glide: after 1 s the drawn value equals the target (" + juce::String (k->drawn(), 4) + ")");
    // other knobs of the product's writes glide too (master and space rails), hidden ones do not
    auto* glue = front.findKnob ("glue");
    setParam (s.proc, "kit", 1.0f);                  // Punch: glue 20 % (Deep has 15 %)
    settle (s.proc);
    check (glue != nullptr && glue->gliding(), "glide: GLUE (MASTER) glides to the kit's value as well");
    tickN (s, 40);

    // a held knob does not glide; a move by hand never glides
    auto* swing = front.findKnob ("swing");
    const auto down = mouse (*swing, { 30.0f, 23.0f }, { 30.0f, 23.0f });
    swing->mouseDown (down);
    setParam (s.proc, "kit", 3.0f);
    settle (s.proc);
    check (swing->isHeld() && ! swing->gliding() && std::abs (swing->drawn() - swing->parameterNorm()) < 1.0e-6f, "glide: a held knob jumps to the value and does not glide");
    swing->mouseUp (mouse (*swing, { 30.0f, 23.0f }, { 30.0f, 23.0f }));
    tickN (s, 40);
    auto* tone = front.findKnob ("echoTone");
    const auto d0 = mouse (*tone, { 50.0f, 40.0f }, { 50.0f, 40.0f });
    tone->mouseDown (d0);
    tone->mouseDrag (mouse (*tone, { 50.0f, 0.0f }, { 50.0f, 40.0f }, {}, 1, true));
    const bool jumped = ! tone->gliding() && std::abs (tone->drawn() - tone->parameterNorm()) < 1.0e-6f;
    tone->mouseUp (mouse (*tone, { 50.0f, 0.0f }, { 50.0f, 40.0f }));
    check (jumped, "glide: a move by hand never glides (the drawing follows the hand at once)");
}

// 2
void checkDoubleClick()
{
    Scene s (false);
    auto& front = s.editor().frontPanel();
    setParam (s.proc, "kit", 3.0f);                 // Dry
    settle (s.proc);
    tickN (s, 40);
    auto doubleClick = [&] (KnobCell* k, float dragPixels)
    {
        const juce::Point<float> p0 (30.0f, 40.0f), p1 (30.0f, 40.0f - dragPixels);
        k->mouseDown (mouse (*k, p0, p0));
        k->mouseDrag (mouse (*k, p1, p0, {}, 1, true));
        k->mouseUp (mouse (*k, p1, p0));
        const float moved = rawParam (s.proc, k->paramId().toRawUTF8());
        k->mouseDown (mouse (*k, p1, p1, {}, 2));
        k->mouseDoubleClick (mouse (*k, p1, p1, {}, 2));
        k->mouseUp (mouse (*k, p1, p1, {}, 2));
        return moved;
    };
    front.select (0);
    tickN (s, 40);
    auto* lvl = front.findKnob ("pad1.level");
    const float moved1 = doubleClick (lvl, 40.0f);
    const float now1 = rawParam (s.proc, "pad1.level");
    check (std::abs (moved1 - now1) > 1.0f && std::abs (now1 - ru8::kKits[3].ch[0].levelDb) < 0.051f,
           "double-click: pad 1 level moved by hand to " + juce::String (moved1, 1) + " dB, double-click returns it to the loaded kit's " + juce::String (now1, 1) + " dB");
    front.select (3);
    tickN (s, 40);
    auto* echo = front.findKnob ("pad4.echo");
    const float factory = s.proc.apvts.getParameter ("pad4.echo")->convertFrom0to1 (s.proc.apvts.getParameter ("pad4.echo")->getDefaultValue());
    const float moved2 = doubleClick (echo, 40.0f);
    const float now2 = rawParam (s.proc, "pad4.echo");
    check (std::abs (now2 - ru8::kKits[3].ch[3].dly) < 0.0051f && std::abs (now2 - factory) > 0.05f,
           "double-click: pad 4 echo send (moved to " + juce::String (moved2, 2) + ") returns to Dry's " + juce::String (now2, 2) + ", not the factory " + juce::String (factory, 2));
    check (echo->gliding(), "double-click: the knob travels to the value (glide)");
    tickN (s, 40);
    check (same (echo->drawn(), echo->parameterNorm()), "double-click: and settles on it");
    // asked at click time: another kit, another answer
    setParam (s.proc, "kit", 4.0f);
    settle (s.proc);
    tickN (s, 40);
    doubleClick (echo, 40.0f);
    check (std::abs (rawParam (s.proc, "pad4.echo") - ru8::kKits[4].ch[3].dly) < 0.0051f, "double-click: asked at click time (kit Deep now: " + juce::String (rawParam (s.proc, "pad4.echo"), 2) + ")");
}

// 3
void checkHelp()
{
    Scene s (false);
    auto& editor = s.editor();
    const auto before = editor.getBounds();
    auto& help = editor.helpOverlay();
    help.open (0);
    bool same = editor.getBounds() == before, inside = true;
    const int n = help.count();
    for (int i = 0; i < n; ++i)
    {
        help.openTip (i);
        same = same && editor.getBounds() == before;
        inside = inside && juce::Rectangle<int> (0, 0, kW, kH).contains (help.tipBounds()) && juce::Rectangle<int> (0, 0, kW, kH).contains (help.markerBounds (i));
    }
    help.close();
    check (n >= 16 && same, "help: all " + juce::String (n) + " markers opened, the editor's bounds never changed (" + juce::String (before.getWidth()) + " x " + juce::String (before.getHeight()) + ")");
    check (inside, "help: every marker and popover stays inside the panel");
    // placement, every page, in both editions: beside its control (never on it, never on its own marker), the pointer touching the
    // control; one marker rule (size, place); no two markers touching; the popover painted over every marker
    for (const bool sa : { false, true })
    {
        Scene sc (sa);
        auto& hp = sc.editor().helpOverlay();
        hp.open (0);
        const int m = hp.count();
        juce::String bad;
        int covered = 0, pixelPages = 0;
        for (int i = 0; i < m; ++i)
        {
            hp.openTip (i);
            const auto tip = hp.tipBounds(), tgt = hp.targetOf (i), mk = hp.markerBounds (i);
            const auto a = hp.arrowTipPoint();
            const float toTarget = juce::Rectangle<float> (tgt.toFloat()).expanded (4.0f).contains (a) ? 0.0f : 1.0f;
            if (tip.intersects (tgt)) bad << " " << (i + 1) << ":covers-its-control";
            if (tip.intersects (mk)) bad << " " << (i + 1) << ":covers-its-marker";
            if (toTarget > 0.0f) bad << " " << (i + 1) << ":pointer-off-control";
            if (mk.getWidth() != HelpOverlay::kMarkerD || mk.getHeight() != HelpOverlay::kMarkerD
                || mk.getCentre() != juce::Point<int> (tgt.getRight() + HelpOverlay::kMarkerDx, tgt.getY() + HelpOverlay::kMarkerDy))
                bad << " " << (i + 1) << ":marker-rule";
            for (int j = 0; j < m; ++j) if (j != i && tip.intersects (hp.markerBounds (j))) ++covered;
            if (! sa || i % 7 == 0)
            {   // the popover's own pixels with and without the markers under it
                const auto withM = sc.editor().createComponentSnapshot (tip, true, 1.0f);
                hp.drawMarkersForTest = false; hp.repaint();
                const auto without = sc.editor().createComponentSnapshot (tip, true, 1.0f);
                hp.drawMarkersForTest = true; hp.repaint();
                bool same = withM.getWidth() == without.getWidth() && withM.getHeight() == without.getHeight();
                for (int y = 0; same && y < withM.getHeight(); ++y)
                    for (int x = 0; same && x < withM.getWidth(); ++x)
                    {   // inside the rounded body only (the corners show the panel)
                        if ((x < 10 || x >= withM.getWidth() - 10) && (y < 10 || y >= withM.getHeight() - 10)) continue;
                        same = withM.getPixelAt (x, y) == without.getPixelAt (x, y);
                    }
                if (! same) bad << " " << (i + 1) << ":marker-over-popover";
                ++pixelPages;
            }
        }
        int touching = 0;
        for (int i = 0; i < m; ++i)
            for (int j = i + 1; j < m; ++j)
                if (hp.markerBounds (i).toFloat().getCentre().getDistanceFrom (hp.markerBounds (j).toFloat().getCentre()) < (float) HelpOverlay::kMarkerD + 2.0f) { ++touching; bad << " " << (i + 1) << "~" << (j + 1); }
        hp.close();
        check (bad.isEmpty(), juce::String ("help (") + (sa ? "standalone" : "plug-in") + "): all " + juce::String (m) + " popovers beside their control and clear of it and of their marker, pointer on the control; "
               "every marker " + juce::String (HelpOverlay::kMarkerD) + " px on its control's top-right corner; no two markers touching (" + juce::String (touching) + "); the popover drawn over the markers ("
               + juce::String (pixelPages) + " pages compared pixel by pixel; " + juce::String (covered) + " marker places hidden under open popovers)", bad);
    }
    // the same through the keys H and Escape (the standalone's; a host keeps its keys)
    Scene st (true);
    auto& se = st.editor();
    const auto sBefore = se.getBounds();
    const bool h = se.keyPressed (juce::KeyPress ('h'));
    const bool open = se.helpOverlay().isOpen();
    const bool esc = se.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    check (h && open && esc && ! se.helpOverlay().isOpen() && se.getBounds() == sBefore, "help: H opens it, Escape closes it (standalone), the window stays the same size");
    se.helpOverlay().open (0);
    help.open (0);
    bool sameTargets = se.helpOverlay().count() == n;
    for (int i = 0; sameTargets && i < n; ++i) sameTargets = se.helpOverlay().titleOf (i) == help.titleOf (i);
    help.close();
    check (sameTargets, "help: the standalone and the plug-in have the same " + juce::String (n) + " markers (texts differ only where keys or the host matter)");
}

// 4
void checkLockup()
{
    Scene s (false);
    auto& f = s.editor().frontPanel();
    const auto url = f.lockup->getUrl();
    check (url.toString (false) == "https://subverselab.com", "lockup: the front's lockup link is " + url.toString (false));
    check (s.editor().rearPanel().lockup->getUrl().toString (false) == "https://subverselab.com", "lockup: the rear panel's lockup link is the same");
    const auto b = f.lockup->getBounds();
    check (b.getRight() >= kW - 25 && b.getBottom() >= kH - 25 && b.getY() > kH - 60, "lockup: bottom right of the panel (" + b.toString() + ")");
    check (f.lockup->getTooltip() == "SubverseLab", "lockup: it takes clicks (opens the site)");
}

// 5
void checkLayout()
{
    for (bool standalone : { false, true })
        for (int width : { 1180, 2360 })
        {
            Scene s (standalone, width);
            auto& editor = s.editor();
            auto r = checkLayout (editor);
            check (r.overlaps == 0 && r.outside == 0 && r.count >= 60,
                   juce::String ("layout: ") + (standalone ? "standalone" : "plug-in") + " front at " + juce::String (width * 100 / kW) + " %: " + juce::String (r.count) + " controls, no overlap, all inside" + (r.firstProblem.isNotEmpty() ? " (" + r.firstProblem + ")" : ""));
            if (! standalone)
            {
                editor.showRear (true);
                auto rr = checkLayout (editor);
                check (rr.overlaps == 0 && rr.outside == 0 && rr.count >= 6, "layout: rear panel at " + juce::String (width * 100 / kW) + " %: " + juce::String (rr.count) + " controls, no overlap, all inside" + (rr.firstProblem.isNotEmpty() ? " (" + rr.firstProblem + ")" : ""));
                editor.showRear (false);
            }
        }
    {
        // window size (owner, Live 2026-10-15: "endlessly big"): 100 % minimum, maximum = 200 % or the display, aspect fixed
        const int fhd = RU8Editor::maxWidthFor ({ 70, 25, 1850, 1055 });      // 1920 x 1080, dock left, menu bar
        const int fhdNoDock = RU8Editor::maxWidthFor ({ 0, 25, 1920, 1055 });
        const int fiveK = RU8Editor::maxWidthFor ({ 0, 25, 5120, 2855 });     // 5120 x 2880
        const int small = RU8Editor::maxWidthFor ({ 0, 25, 1280, 775 });      // 1280 x 800: under 100 % would be needed
        const int fhdH = juce::roundToInt ((float) fhd * (float) kH / (float) kW);
        check (fiveK == 2 * kW && fhd < 2 * kW && fhdH + RU8Editor::kTitleBarRoom <= 1055 && fhd == fhdNoDock && small == kW,
               "window: the largest size follows the display: 1920 x 1080 -> " + juce::String (fhd) + " x " + juce::String (fhdH) + " (" + juce::String (fhd * 100 / kW)
               + " %), 5K -> " + juce::String (fiveK) + " (200 %), 1280 x 800 -> " + juce::String (small) + " (100 %, never smaller)");
        Scene s (false);
        auto& ed = s.editor();
        auto* c = ed.getConstrainer();
        const auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const int expect = RU8Editor::maxWidthFor (primary != nullptr ? primary->userArea : juce::Rectangle<int>());
        juce::Rectangle<int> big (0, 0, 4000, 2790), tiny (0, 0, 300, 200), odd (0, 0, 1500, 700);
        c->checkBounds (big, ed.getBounds(), juce::Rectangle<int> (0, 0, 100000, 100000), false, false, true, true);
        c->checkBounds (tiny, ed.getBounds(), juce::Rectangle<int> (0, 0, 100000, 100000), false, false, true, true);
        c->checkBounds (odd, ed.getBounds(), juce::Rectangle<int> (0, 0, 100000, 100000), false, false, true, true);
        const double aspect = (double) kW / (double) kH;
        check (c->getMaximumWidth() == expect && c->getMinimumWidth() == kW && big.getWidth() == expect && tiny.getWidth() == kW
               && std::abs ((double) odd.getWidth() / odd.getHeight() - aspect) < 0.01,
               "window: the editor's constrainer on this Mac's main display (user area " + (primary != nullptr ? primary->userArea.toString() : juce::String ("-"))
               + "): max " + juce::String (c->getMaximumWidth()) + ", a 4000 drag -> " + juce::String (big.getWidth()) + " x " + juce::String (big.getHeight()) + ", a 300 drag -> " + juce::String (tiny.getWidth())
               + ", aspect kept (" + juce::String ((double) odd.getWidth() / odd.getHeight(), 3) + ")");
    }
    {
        // the readout never changes size: every set name, the NEXT state
        Scene s (false);
        auto& ro = s.editor().frontPanel().readout;
        const auto base = ro.getBounds();
        bool same = true, fits = true; float widest = 0.0f, field = 1.0e9f;
        for (int slot = 0; slot < ru8::kNumSlots; ++slot)
        {
            setParam (s.proc, "set", (float) slot);
            settle (s.proc);
            tickN (s, 2);
            same = same && ro.getBounds() == base;
            widest = juce::jmax (widest, trackedWidth (ro.setLabel().toUpperCase(), display (19.0f), 0.03f));
            field = juce::jmin (field, layout::setNameRight - ro.setNameX());
        }
        fits = widest <= field;
        s.ph.playing = true;
        s.play (0.3);
        setParam (s.proc, "set", 5.0f);
        settle (s.proc);
        s.play (0.1);
        const bool next = ro.nextShown();
        same = same && ro.getBounds() == base;
        tickN (s, 3);
        const float fieldNext = layout::setNameRight - ro.setNameX();   // NEXT replaces the number: the name is condensed if needed, never cut
        check (same && next, "layout: the readout keeps its size (" + juce::String (base.getWidth()) + " x " + juce::String (base.getHeight()) + ") with every set name and in the NEXT state");
        check (fits, "layout: the longest set name fits its field beside SET / MIDI (" + juce::String (widest, 0) + " of " + juce::String (field, 0)
               + " units; in the NEXT state the field is " + juce::String (fieldNext, 0) + " and a wider name is condensed)");
    }
}

// 6
void checkSteps()
{
    Scene s (false);
    auto& front = s.editor().frontPanel();
    setParam (s.proc, "set", 0.0f);                 // House
    settle (s.proc);
    auto& song = s.proc.song();
    auto& A = song.slot[0].steps[0]; auto& B = song.slot[0].steps[1];
    auto& pad = front.bed.padAt (1);                // snare: empty in House A
    pad.setStepsShown (true);
    const int step = 6;
    auto click = [&] (int i)
    {
        const auto c = pad.stepCell (i).getCentre();
        pad.mouseDown (mouse (pad, c, c));
        pad.mouseUp (mouse (pad, c, c));
    };
    check (A[1][step] == '.', "steps: House A, snare, step 7 starts as '.'");
    const char expect[] = { 'x', 'X', 'g', '.' };
    juce::String seen;
    bool ok = true;
    for (char e : expect) { click (step); seen << A[1][step]; ok = ok && A[1][step] == e; }
    check (ok, "steps: clicking a step cycles '.' -> x -> X -> g -> '.' in Song.slot[edit].steps (saw " + seen + ")");
    A[1][step] = 'r';
    click (step);
    check (A[1][step] == '.', "steps: 'r' (a roll) cycles to '.'");
    check (front.bed.padAt (1).pad() == 1 && s.editor().context().selectedPad == 1, "steps: the click selected the pad for the CHANNEL rail");
    // the section in view is the Section parameter: B writes B
    setParam (s.proc, "section", 2.0f);
    click (step); click (step);
    check (B[1][step] == 'X' && A[1][step] == '.', "steps: with Section = B the click writes section B (B = '" + juce::String::charToString (B[1][step]) + "', A untouched)");
    setParam (s.proc, "section", 3.0f);
    click (step);
    check (A[1][step] == 'x', "steps: with Section = Fill the view is A");
    // another set is edited, not the playing one
    setParam (s.proc, "set", 4.0f);
    settle (s.proc);
    click (step);
    check (song.slot[4].steps[0][1][step] == 'x' && s.proc.editSlot() == 4, "steps: the edit goes to the set in view (Funk)");
    // a click on the pad's name row plays it
    const auto before = s.proc.display().hitCounter;
    pad.mouseDown (mouse (pad, { 30.0f, 20.0f }, { 30.0f, 20.0f }));
    pad.mouseUp (mouse (pad, { 30.0f, 20.0f }, { 30.0f, 20.0f }));
    check (s.proc.display().hitCounter == before + 1, "pads: a click on the pad plays it (processor.hitPad)");
    // tools
    auto& tools = front;
    tools.copyPadSteps();
    front.select (2);
    tools.pastePadSteps();
    check (juce::String (song.slot[4].steps[0][2], 16) == juce::String (song.slot[4].steps[0][1], 16), "tools: copy pad / paste pad copy the 16 steps");
    tools.clearRow();
    check (juce::String (song.slot[4].steps[0][2], 16) == "................", "tools: clear pad empties the selected row");
    tools.copySection();
    check (std::memcmp (song.slot[4].steps[0], song.slot[4].steps[1], sizeof song.slot[4].steps[0]) == 0, "tools: copy section A -> B");
    tools.clearSection();
    bool empty = true;
    for (auto& p : song.slot[4].steps[0]) for (char c : p) empty = empty && c == '.';
    check (empty, "tools: clear section empties it");
}

void checkArranger()
{
    Scene s (false);
    auto& ro = s.editor().frontPanel().readout;
    ro.keyB.click();
    check (juce::roundToInt (rawParam (s.proc, "section")) == 2, "arranger: key B writes the Section parameter (B)");
    ro.keyA.click();
    check (juce::roundToInt (rawParam (s.proc, "section")) == 1, "arranger: key A writes the Section parameter (A)");
    ro.keyAuto.click();
    check (rawParam (s.proc, "auto") > 0.5f, "arranger: AUTO toggles on");
    ro.keyB.click();
    check (rawParam (s.proc, "auto") < 0.5f && juce::roundToInt (rawParam (s.proc, "section")) == 2, "arranger: A / B turn AUTO off");
    // the lamps follow the display
    s.play (0.5);
    s.editor().tickForSnapshot();
    check (ro.keyA.isOn() && ! ro.keyB.isOn() && ro.keyB.isBlink(), "arranger: the lamp of the playing section (A) is lit; B, asked for, blinks until the bar line");
    ro.keyF.click();
    check (s.editor().context().fillQueuedUi && ro.keyF.isBlink && ro.keyF.isBlink(), "arranger: the fill key queues a fill and its lamp blinks until the bar line");
    // kit / set steps
    s.proc.apvts.getParameter ("kit");
    ro.kitNext.click(); settle (s.proc);
    check (juce::roundToInt (rawParam (s.proc, "kit")) == 1, "readout: the kit step key moves the kit parameter");
    ro.kitPrev.click(); ro.kitPrev.click(); settle (s.proc);
    check (juce::roundToInt (rawParam (s.proc, "kit")) == ru8::kNumKits - 1, "readout: kit steps wrap round");
    ro.setNext.click(); settle (s.proc);
    check (juce::roundToInt (rawParam (s.proc, "set")) == 1, "readout: the set step key moves the set parameter");
    // pending set while playing
    s.play (0.2);
    s.editor().tickForSnapshot();
    check (ro.nextShown(), "readout: while playing, another set shows the NEXT label");
    // space / echo / mute / solo
    auto& f = s.editor().frontPanel();
    f.spaceSeg[2]->click(); settle (s.proc);
    check (juce::roundToInt (rawParam (s.proc, "spaceType")) == 2, "space: the Hall key writes spaceType");
    f.echoSeg[3]->click();
    check (juce::roundToInt (rawParam (s.proc, "echoTime")) == 3, "echo: the 1/4 key writes echoTime");
    f.select (2);
    f.muteBtn->click(); f.soloBtn->click();
    check (rawParam (s.proc, "pad3.mute") > 0.5f && rawParam (s.proc, "pad3.solo") > 0.5f, "channel: mute and solo buttons write the selected pad's parameters");
    f.resetBtn->click();
    tickN (s, 40);
    check (std::abs (rawParam (s.proc, "pad3.level") - ru8::kKits[juce::roundToInt (rawParam (s.proc, "kit"))].ch[2].levelDb) < 0.051f, "channel: the reset glyph returns the pad's values to processor.kitDefault");
}

void checkKeys()
{
    Scene s (true);   // the standalone's keys (a host's keys stay the host's: checked with the transport)
    auto& editor = s.editor();
    auto key = [&] (juce::juce_wchar c, juce::ModifierKeys m = {}) { return editor.keyPressed (juce::KeyPress ((int) c, m, c)); };
    const auto h0 = s.proc.display().hitCounter;
    check (key ('x') && s.editor().context().selectedPad == 1 && s.proc.display().hitCounter == h0 + 1, "keys: X plays the snare and selects it");
    check (key ('1') && editor.context().selectedPad == 12, "keys: 1 plays pad 13 (top row, left)");
    editor.keyStateChanged (false);
    check (key ('z') && editor.context().selectedPad == 0, "keys: Z plays the kick (bottom left)");
    check (key ('6') && juce::roundToInt (rawParam (s.proc, "section")) == 2 && key ('5') && juce::roundToInt (rawParam (s.proc, "section")) == 1, "keys: 5 / 6 are section A / B");
    check (key ('8') && rawParam (s.proc, "auto") > 0.5f, "keys: 8 toggles AUTO");
    check (key ('m') && s.proc.mood == "night", "keys: M steps the mood (cream -> night, kept in processor.mood)");
    check (key ('b') && editor.rearShowing() && ! editor.frontPanel().isVisible(), "keys: B turns the machine round; the front is inert");
    {
        const auto hits = s.proc.display().hitCounter;
        const bool handled = key ('z');
        check (! handled && s.proc.display().hitCounter == hits, "keys: pads do not play while the rear shows");
    }
    check (key ('b') && ! editor.rearShowing(), "keys: B again returns to the front");
    // copy / paste with Cmd
    editor.frontPanel().select (0);
    check (key ('c', juce::ModifierKeys (juce::ModifierKeys::commandModifier)) && editor.frontPanel().clipboardFull(), "keys: Cmd+C copies the selected pad's steps");
    // undo
    setParam (s.proc, "volume", -20.0f);
    const bool undone = key ('z', juce::ModifierKeys (juce::ModifierKeys::commandModifier));
    check (undone && rawParam (s.proc, "volume") > -3.0f, "keys: Cmd+Z undoes a panel edit (svl::handleUndoKey): volume back to " + juce::String (rawParam (s.proc, "volume"), 1) + " dB");
    const bool redone = key ('z', juce::ModifierKeys (juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier));
    check (redone && rawParam (s.proc, "volume") < -19.0f, "keys: Cmd+Shift+Z redoes it");
    // space in the standalone
    auto sp = editor.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    check (sp, "keys: Space starts / stops the standalone transport");
}

void checkMisc()
{
    {
        Scene s (false);
        auto& front = s.editor().frontPanel();
        const auto f = front.writeClipFile (true), g = front.writeClipFile (false);
        juce::MemoryBlock mb;
        f.loadFileAsData (mb);
        check (f.existsAsFile() && mb.getSize() > 30 && std::memcmp (mb.getData(), "MThd", 4) == 0 && f.getFileName().endsWith (".mid") && g.existsAsFile() && g != f,
               "MIDI out: the drag-out writes the full clip and the pad's clip to temporary .mid files (" + f.getFileName() + ", " + juce::String ((int) mb.getSize()) + " bytes)");
        f.deleteFile(); g.deleteFile();
        // the solo glyph follows the kit's aux type
        setParam (s.proc, "kit", 7.0f);               // Anatolia
        settle (s.proc);
        front.select (8);
        const auto g8 = padGlyph (8, 7);
        check (g8.second == "darbuka", "pads: the aux pad's glyph follows the loaded kit (Anatolia p1 = " + g8.second + ")");
        const auto r1 = front.bed.padRect (0), r2 = front.bed.padRect (1), r13 = front.bed.padRect (12);
        check (r1.getX() < r2.getX() && r1.getY() > r13.getY() && r1.getY() == front.bed.padRect (3).getY(), "pads: hardware layout, pad 1 (kick) bottom left, pad 13 top left");
    }
    {
        // reduced motion: nothing travels, the shapes only light up
        Scene s (false);
        s.editor().context().reducedMotion = true;
        auto& w = s.editor().frontPanel().water;
        for (int i = 0; i < 16; ++i) w.hit (i, 1.0f);
        w.stir();
        for (int i = 0; i < 20; ++i) w.advance (1.0f / 30.0f);
        check (w.shapes() == 0 && w.bubbleCount() == 0, "reduced motion: no shape travels, no bubble rises");
        w.hit (3, 1.0f);
        check (w.lightOf (3) > 0.5f, "reduced motion: the shape lights up (" + juce::String (w.lightOf (3), 2) + ")");
        for (int i = 0; i < 70; ++i) w.advance (1.0f / 30.0f);
        check (w.lightOf (3) < 0.05f && ! w.busy(), "reduced motion: and fades; the water is idle again (no timer work)");
        w.bar (0, false, 0); w.bar (0, false, 1);
        check (w.ringCount (0) == 1, "reduced motion: a finished bar still puts its ring on the post (at once, no flight)");
        // and the normal tank is idle when nothing happens
        s.editor().context().reducedMotion = false;
        w.stir();
        check (w.shapes() == 5, "water: a new kit stirs the tank (5 shapes)");
        for (int i = 0; i < 400; ++i) w.advance (1.0f / 30.0f);
        check (! w.busy(), "water: it comes to rest (no timer, no repaint) when nothing plays");
    }
    {
        // the mood and the width are kept in the processor's state
        RU8AudioProcessor a;
        a.editorWidth = 1300;                         // fits every display of 1920 x 1080 and up
        a.mood = "pink";
        juce::MemoryBlock state;
        a.getStateInformation (state);
        RU8AudioProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        std::unique_ptr<juce::AudioProcessorEditor> ed (b.createEditor());
        // a display smaller than 1300 (the CI runner's) caps the width at its own limit
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const int expectW = juce::jmin (1300, RU8Editor::maxWidthFor (display != nullptr ? display->userArea : juce::Rectangle<int>()));
        check (b.mood == "pink" && ed->getWidth() == expectW && std::abs ((float) ed->getHeight() / (float) ed->getWidth() - (float) kH / (float) kW) < 0.002f,
               "state: the mood (" + b.mood + ") and the width (" + juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()) + ") come back, aspect kept");
        // a width saved on a larger display comes back no larger than this display allows
        RU8AudioProcessor c2;
        c2.editorWidth = 2360;
        juce::MemoryBlock big; c2.getStateInformation (big);
        RU8AudioProcessor d2; d2.setStateInformation (big.getData(), (int) big.getSize());
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (d2.createEditor());
        const auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const int limit = RU8Editor::maxWidthFor (primary != nullptr ? primary->userArea : juce::Rectangle<int>());
        check (ed2->getWidth() == limit, "state: a width saved at 200 % (2360) opens at this display's limit (" + juce::String (ed2->getWidth()) + " of " + juce::String (limit) + ")");
        ed->setSize (1900, 1900);
        check (b.editorWidth.load() == 1900, "state: a resize writes the width back (editorWidth)");
    }
}

// the water's cost at 200 %: the steps of the water and of the panel's tick, and the painting of what they invalidate, measured
// the process's retired instructions and CPU cycles (Apple silicon PMU counters via rusage v4): cycles per CPU millisecond = the clock the
// work ran at; instructions per cycle = how well it ran. Identical work retires the same instructions on any core at any load.
struct Counters { double instructions = 0, cycles = 0; };
#if defined (__APPLE__)
Counters counters()
{
    rusage_info_v4 ri {};
    if (proc_pid_rusage (getpid(), RUSAGE_INFO_V4, (rusage_info_t*) &ri) != 0) return {};
    return { (double) ri.ri_instructions, (double) ri.ri_cycles };
}
double loadAverage() { double l[3] = { -1, -1, -1 }; getloadavg (l, 3); return l[0]; }
#else   // no counters: the check falls back to CPU time, as documented below
Counters counters() { return {}; }
double loadAverage() { return -1.0; }
#endif

void checkCost()
{
    // What is checked: the water tank's work per frame at 200 % (the panel's tick, the water's step and the repaint of what they invalidate),
    // in the light batches (the best of six; two of them hold the water's stir), as a share of one performance core at 30 Hz (< 5 %).
    // Measured in retired instructions, not CPU time (flake hunt 2026-10-15, tools/out/logs/cost_counters_*, cost_batches_1.txt): the same
    // seeded work retires 14.2-14.3 M instructions per frame in its lightest batch at any load (+-0.5 %), while its CPU time swung from
    // 1.18 to 1.96 ms because other processes (Live, ffmpeg, WindowServer: shared caches and memory) dropped IPC from 3.7 to 2.5 at the
    // same 2.7-2.9 GHz. User-interactive QoS and best-of-3 attempts did not remove that (still 5.4-5.8 % at load ~22). The budget is
    // converted at kQuietRate, the lower end of this Mac's (M2) quiet speed for this code on a performance core (9.9-11.0 M instructions per
    // CPU ms measured; the evening's idle 4.0 % = 1.33 ms gives 10.7). A slower rate makes the budget smaller, so the check is not looser
    // than before. CPU time is still measured and printed. Without the counters (rusage v4 gives 0, e.g. not Apple silicon) the check
    // falls back to CPU time.
    constexpr double kQuietRate = 10.0e6;        // instructions per CPU millisecond
    constexpr int kBatches = 6, kFrames = 80;
    constexpr double kLimit = 5.0;
    const bool negative = std::getenv ("RU8_COST_NEGATIVE") != nullptr;
    const bool interactive = juce::String (std::getenv ("RU8_COST_QOS")) != "default";
#if defined (__APPLE__)
    const qos_class_t qosBefore = qos_class_self();
    if (interactive) pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);   // as a host's message thread (CPU-time figure only)
#endif
    juce::Image img (juce::Image::ARGB, 2360, 1646, true, juce::NativeImageType());
    auto scene = std::make_unique<Scene> (false, 2360);
    auto& editor = scene->editor();
    auto& front = editor.frontPanel();
    auto& water = front.water;
    scene->play (1.0, false);
    std::vector<double> batchMs, batchInstr, batchCyc;
    double worst = 0.0, areaPx = 0.0, tTick = 0.0, tAdv = 0.0; int rects = 0, frames = 0;
    juce::Random rnd (3);
    for (int bi = 0; bi < kBatches; ++bi)
    {
        double total = 0.0;
        const Counters b0 = counters();
        for (int f = 0; f < kFrames; ++f, ++frames)
        {
            // as busy as playing gets: six pads hit on every sixteenth (the water lets a shape go at most every 0.85 s)
            if (frames % 4 == 0)
                for (int p = 0; p < 6; ++p) water.hit (rnd.nextInt (16), 0.6f + 0.4f * rnd.nextFloat());
            if (frames % 240 == 0) water.stir();
            water.setSimulatedClock ((double) frames / 30.0);
            const double t0 = cpuMs();
            front.tick();
            const double t1 = cpuMs();
            water.advance (1.0f / 30.0f);
            const double t2 = cpuMs();
            tTick += t1 - t0; tAdv += t2 - t1;
            juce::Graphics g (img);
            juce::RectangleList<int> clip;
            for (auto r : water.lastRepaintRegion()) clip.add (r.translated (WaterTank::kInset, WaterTank::kInset) * 2);
            rects += clip.getNumRectangles();
            for (auto r : clip) areaPx += (double) r.getWidth() * r.getHeight() / 4.0;
            if (! negative) g.reduceClipRegion (clip);          // RU8_COST_NEGATIVE: every frame repaints the whole panel (a repaint regression; must FAIL)
            editor.paintEntireComponent (g, true);
            const double ms = cpuMs() - t0;
            total += ms; worst = juce::jmax (worst, ms);
        }
        const Counters b1 = counters();
        batchMs.push_back (total / kFrames);
        batchInstr.push_back ((b1.instructions - b0.instructions) / kFrames);
        batchCyc.push_back ((b1.cycles - b0.cycles) / kFrames);
        if (std::getenv ("RU8_COST_BATCHES") != nullptr)
            std::printf ("         batch %d: %.3f ms CPU, %.2f M instructions, %.2f M cycles per frame, IPC %.2f\n", bi + 1, batchMs.back(),
                         batchInstr.back() / 1.0e6, batchCyc.back() / 1.0e6, batchCyc.back() > 0 ? batchInstr.back() / batchCyc.back() : 0.0);
    }
    // the light batch: fewest instructions (identical at any load); its CPU time and IPC are what this machine gave it now
    const int light = (int) (std::min_element (batchInstr.begin(), batchInstr.end()) - batchInstr.begin());
    const bool haveCounters = batchInstr[(size_t) light] > 0.0;
    const double instr = batchInstr[(size_t) light], ipc = batchCyc[(size_t) light] > 0 ? instr / batchCyc[(size_t) light] : 0.0;
    const double bestMs = *std::min_element (batchMs.begin(), batchMs.end());
    const double cpuPercent = bestMs * 30.0 / 1000.0 * 100.0;
    const double percent = haveCounters ? instr / kQuietRate * 30.0 / 1000.0 * 100.0 : cpuPercent;
    std::printf ("       dirty area %.0f logical px per frame (%.1f %% of the panel), %.1f rectangles\n", areaPx / frames, areaPx / frames / (kW * kH) * 100.0, (double) rects / frames);
    std::printf ("       tank step %.3f ms and water step %.3f ms per frame; light batch %d: %.2f M instructions per frame, IPC %.2f; CPU time now: best batch %.2f ms (%.1f %%), worst frame %.1f ms, load average %.1f\n",
                 tTick / frames, tAdv / frames, light + 1, instr / 1.0e6, ipc, bestMs, cpuPercent, worst, loadAverage());
    check (percent < kLimit, "cost: at 200 % the water tank, with everything it repaints under it, costs " + juce::String (percent, 1) + " % of one core at 30 Hz ("
               + (haveCounters ? juce::String (instr / 1.0e6, 2) + " M instructions per frame at " + juce::String (kQuietRate / 1.0e6, 1) + " M per ms" : "CPU time, no counters") + "; < 5 %)",
           juce::String::formatted ("batches %s M instructions per frame; CPU time now %.2f ms per frame = %.1f %% (IPC %.2f, QoS %s, load average %.1f)",
                                    [&] { juce::StringArray a; for (auto v : batchInstr) a.add (juce::String (v / 1.0e6, 2)); return a.joinIntoString (" "); }().toRawUTF8(),
                                    bestMs, cpuPercent, ipc, interactive ? "user-interactive" : "default", loadAverage()));
    const double t1 = cpuMs();
    for (int i = 0; i < 10; ++i) { juce::Graphics g (img); editor.paintEntireComponent (g, true); }
    std::printf ("       (a complete repaint of the editor at 200 %%: %.1f ms)\n", (cpuMs() - t1) / 10.0);
    scene.reset();
#if defined (__APPLE__)
    if (interactive) pthread_set_qos_class_self_np (qosBefore == QOS_CLASS_UNSPECIFIED ? QOS_CLASS_DEFAULT : qosBefore, 0);   // the other checks run as before
#endif
}
} // namespace

static void profile()
{
    Scene s (false, 2360);
    auto& ed = s.editor();
    s.play (1.0);
    juce::Image img (juce::Image::ARGB, 2360, 1646, true, juce::NativeImageType());
    auto time = [&] (const char* what, juce::Component& c, bool entire = true)
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 5; ++i) { juce::Graphics g (img); g.addTransform (juce::AffineTransform::scale (2.0f)); if (entire) c.paintEntireComponent (g, true); else c.paint (g); }
        std::printf ("  %-28s %8.2f ms\n", what, (juce::Time::getMillisecondCounterHiRes() - t0) / 5.0);
    };
    auto& f = ed.frontPanel();
    time ("front own paint", f, false);
    time ("water paint", f.water, false);
    time ("readout (entire)", f.readout);
    time ("bed (entire)", f.bed);
    time ("bed own paint", f.bed, false);
    time ("one pad", f.bed.padAt (0));
    time ("one knob", *f.knobs[0]);
    time ("one tool button", *f.bed.tools[3]);
    time ("lockup", *f.lockup);
    {
        auto bench = [&] (const char* what, juce::Rectangle<int> r)
        {
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 20; ++i) { juce::Graphics g (img); g.reduceClipRegion (r); ed.paintEntireComponent (g, true); }
            std::printf ("  clip %-22s %8.2f ms\n", what, (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0);
        };
        bench ("32x32 over water gap", { 1000, 1560, 32, 32 });
        bench ("32x32 over a pad", { 800, 800, 32, 32 });
        bench ("32x32 top band", { 1000, 4, 32, 32 });
        bench ("rail 140x400", { 40, 400, 140, 400 });
        bench ("1 pad tile", { 656, 508, 244, 244 });
        for (int nrect : { 1, 4, 16, 40 })
        {
            const double tt = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 10; ++i)
            {
                juce::Graphics g (img);
                juce::RectangleList<int> rl;
                for (int k = 0; k < nrect; ++k) rl.addWithoutMerging ({ 40 + (k % 8) * 20, 400 + (k / 8) * 70 + (k % 8) * 20, 16, 16 });
                g.reduceClipRegion (rl);
                ed.paintEntireComponent (g, true);
            }
            std::printf ("  %2d small clip rects          %8.2f ms\n", nrect, (juce::Time::getMillisecondCounterHiRes() - tt) / 10.0);
        }
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i) { juce::Graphics g (img); }
        std::printf ("  Graphics ctor                  %8.2f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0);
    }
}


// factory presets: the selector in the tools row, the arrows, the modified dot, the list popover
void checkPresets()
{
    Scene s (false);
    auto& editor = s.editor();
    auto& front = editor.frontPanel();
    auto& list = editor.presetList();
    const auto before = editor.getBounds();
    tickN (s, 2);
    auto* key = front.bed.presetKey;
    check (key != nullptr && key->text == "House - Analog" && ! key->isMarked() && s.proc.currentPreset() == 0,
           "presets: the PRESET key shows the loaded preset (" + (key != nullptr ? key->text : juce::String ("-")) + "), no dot on a new RU-8");

    // arrows: step without the list; the knobs glide to the new values
    auto* glue = front.findKnob ("glue");
    const float glueFrom = glue->drawn();
    front.bed.presetPrev->click();                      // 0 -> 27 (Drum & bass - Breaks: another kit, another glue)
    const float glueTo = glue->target();
    tickN (s, 1);
    const float glueOneTick = glue->drawn();
    tickN (s, 30);
    const bool glided = std::abs (glueTo - glueFrom) > 0.01f && ((glueOneTick > glueFrom && glueOneTick < glueTo) || (glueOneTick < glueFrom && glueOneTick > glueTo))
                     && std::abs (glue->drawn() - glueTo) < 1e-5f && std::abs (glue->parameterNorm() - glueTo) < 1e-5f;
    const int afterPrev = s.proc.currentPreset();
    front.bed.presetNext->click(); front.bed.presetNext->click();   // 27 -> 0 -> 1
    tickN (s, 2);
    check (afterPrev == 27 && s.proc.currentPreset() == 1 && ! list.isOpen() && key->text == "Techno - Analog",
           "presets: the arrows step through the 28 and wrap round (0 -> 27, then -> 0 -> 1) without opening the list");
    check (glided, juce::String::formatted ("presets: a load glides the knobs (glue drawn %.3f -> %.3f after one tick -> %.3f; the parameter at once)", glueFrom, glueOneTick, glueTo));

    // the modified dot
    setParam (s.proc, "pad3.level", -24.0f);
    settle (s.proc); tickN (s, 2);
    const bool dot = key->isMarked();
    snapshot (front, "presets_modified.png");
    setParam (s.proc, "pad3.level", ru8::kKits[0].ch[2].levelDb);
    settle (s.proc); tickN (s, 2);
    check (dot && ! key->isMarked(), "presets: the gold dot shows after a change and goes when the value is back (snapshot presets_modified.png)");

    // the list: over the pad bed, the window does not grow
    key->click();
    tickN (s, 2);
    const bool opened = list.isOpen() && key->isOn();
    const auto keyOnPanel = editor.getLocalArea (key, key->face());
    bool inside = juce::Rectangle<int> (0, 0, kW, kH).contains (list.listBounds()) && juce::Rectangle<int> (layout::bed).contains (list.listBounds())
               && ! list.listBounds().intersects (keyOnPanel), apart = true;
    for (int i = 0; i < ru8::kNumPresets; ++i)
    {
        inside = inside && list.listBounds().contains (list.itemBounds (i));
        for (int j = i + 1; j < ru8::kNumPresets; ++j) apart = apart && ! list.itemBounds (i).intersects (list.itemBounds (j));
    }
    snapshot (editor, "presets_list.png");
    check (opened && inside && apart && editor.getBounds() == before,
           "presets: the PRESET key opens the list over the pad bed (" + list.listBounds().toString() + "), the key stays in sight, 28 entries inside it, none overlapping, the window the same size (snapshot presets_list.png)");
    list.choose (14);
    tickN (s, 2);
    snapshot (front, "presets_long_name.png");   // the longest name on the key
    check (! list.isOpen() && s.proc.currentPreset() == 14 && key->text == "Ciftetelli - Anatolia" && juce::roundToInt (rawParam (s.proc, "kit")) == ru8::presetKit (14),
           "presets: a click in the list loads that preset and closes the list (Ciftetelli - Anatolia; snapshot presets_long_name.png)");
    key->click();
    const auto outside = juce::Point<float> (60.0f, 60.0f);
    list.mouseDown (mouse (list, outside, outside));
    list.mouseUp (mouse (list, outside, outside));
    check (! list.isOpen() && s.proc.currentPreset() == 14, "presets: a click outside the list closes it and loads nothing");
    key->click();
    const bool hostEsc = editor.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    const bool stillOpen = list.isOpen();
    {   // a real click where the PRESET key is: the list (on top) receives it outside its own area and closes
        const auto at = list.getLocalPoint (key, key->face().getCentre()).toFloat();
        list.mouseDown (mouse (list, at, at)); list.mouseUp (mouse (list, at, at));
    }
    check (! hostEsc && stillOpen && ! list.isOpen() && s.proc.currentPreset() == 14,
           "presets: in a host Escape stays the host's (DAW rule 4); a click on the PRESET key closes the list");
    Scene st (true);
    st.editor().frontPanel().bed.presetKey->click();
    const bool esc = st.editor().keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    check (esc && ! st.editor().presetList().isOpen() && st.editor().getBounds().getWidth() == kW, "presets: Escape closes the list in the standalone");

    // the list follows a program change from the host while open
    key->click();
    s.proc.setCurrentProgram (22);
    tickN (s, 2);
    check (key->text == "Bossa nova - Glass" && list.isOpen(), "presets: a host's program change shows on the key while the list is open");
    list.close();
}

/** Every help page in every edition, mood and both ends of the window size (for eyes; the checks are in checkHelp). */
void helpPages (const juce::File& dir)
{
    dir.createDirectory();
    for (const bool sa : { false, true })
        for (const char* mood : { "cream", "night", "powder", "pink" })
            for (const int w : { kW, kW * 2 })
            {
                Scene s (sa, w);
                s.proc.mood = mood;
                s.editor().setMood (mood);
                s.editor().frontPanel().water.stir();
                s.play (1.0);
                auto& help = s.editor().helpOverlay();
                help.open (0);
                for (int i = 0; i < help.count(); ++i)
                {
                    help.openTip (i);
                    const auto img = s.editor().createComponentSnapshot (s.editor().getLocalBounds(), true, 1.0f);
                    auto f = dir.getChildFile (juce::String::formatted ("help_%s_%s_%d_%02d.png", sa ? "sa" : "host", mood, w == kW ? 100 : 200, i + 1));
                    f.deleteFile();
                    juce::FileOutputStream out (f);
                    juce::PNGImageFormat().writeImageToStream (img, out);
                }
                help.close();
            }
    std::printf ("help pages written to %s\n", dir.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--profile") { profile(); return 0; }
    if (argc > 1 && juce::String (argv[1]) == "--help-pages")
    { helpPages (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "build/help_pages")); return 0; }
    if (argc > 1 && juce::String (argv[1]) == "--cost") { for (int i = 0; i < 6; ++i) checkCost(); return 0; }
    gOut = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("docs/ui");
    gOut.createDirectory();
    std::printf ("ui_snapshot: writing to %s\n", gOut.getFullPathName().toRawUTF8());

    // first: it then measures in a fresh process, as ui_snapshot --cost does
    checkCost();
    pictures();
    checkGlide();
    checkDoubleClick();
    checkHelp();
    checkLockup();
    checkLayout();
    checkSteps();
    checkArranger();
    checkKeys();
    checkMisc();
    checkPresets();

    std::printf ("waits for the processor's timer: %d, longest %.1f ms, timed out (5 s) %d\n", gSettles, gSettleWorstMs, gSettleTimeouts);
    std::printf ("ui_snapshot: %d of %d checks passed\n", gPass, gPass + gFail);
    return gFail == 0 ? 0 : 1;
}
