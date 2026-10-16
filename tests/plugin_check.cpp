// plugin_check: the RU-8 processor (Source/, as the plug-in runs it) in a simulated host, offline.
//   plugin_check            one line per check, a summary; exit code 1 on any failure
// 1. the processor's output equals the raw engine + sequencer (RU8Engine.h / RU8Sequencer.h) for a set, bit for bit
// 2. the same at blocks 1, 64, 480, 512, 4096 and 44.1 / 48 / 96 kHz, host PPQ given per block
// 3. reported latency = the engine's (glue pre-delay + Color's 2x filters + limiter look-ahead)
// 4. MIDI in: a GM note plays its pad on its sample; D-1 switches to B on the next bar; F-1 stops the sequencer
// 5. a new set while playing starts on the next bar line
// 6. a kit change moves the kit's values (message thread), a state restore keeps the project's values
// 7. state round trip: parameters, steps of a factory set and a USER slot, note map
// 13. factory presets: host programs, what a load writes, levels of all 28, modified, undo, the set on the bar line, the restore rule
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Params.h"
#include <cstdio>
#include <cstring>
#include <thread>

static int fails = 0, passes = 0;
// a FAIL also prints its line and its condition as written (the detail is printed either way), so a failure in a loop of runs can be found again
static void checkAt (int line, const char* cond, bool ok, const juce::String& what, const juce::String& detail = {})
{
    (ok ? passes : fails)++;
    std::printf ("%s  %s%s%s", ok ? "PASS" : "FAIL", what.toRawUTF8(), detail.isEmpty() ? "" : "  ", detail.toRawUTF8());
    if (! ok) std::printf ("\n      condition (plugin_check.cpp:%d): %s", line, cond);
    std::printf ("\n");
    std::fflush (stdout);
}
#define check(ok, ...) checkAt (__LINE__, #ok, (ok), __VA_ARGS__)

// Waits until the processor's 30 Hz timer has run one complete callback after this point and has nothing left to apply (kit / set /
// SPACE type loads, a program set from another thread, the MIDI latch; the 'modified' check runs in every callback). The checks
// waited a fixed 60-200 ms before (2-6 timer periods), which a busy machine can exceed. Gives up after 5 s; the longest wait is reported.
static double gSettleWorstMs = 0.0; static int gSettles = 0, gSettleTimeouts = 0;
static double settle (RU8AudioProcessor& p, double maxMs = 5000.0)
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

struct Host : juce::AudioPlayHead
{
    double sr = 48000, bpm = 124, startPpq = 0; int64_t pos = 0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        const double ppq = startPpq + (double) pos / sr * bpm / 60.0;
        i.setBpm (bpm); i.setIsPlaying (playing); i.setPpqPosition (ppq);
        i.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        i.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        i.setTimeInSamples (pos);
        return i;
    }
};

static void setP (RU8AudioProcessor& p, const juce::String& id, float plain)
{
    auto* q = p.apvts.getParameter (id);
    q->setValueNotifyingHost (q->convertTo0to1 (plain));
}

// render `seconds` through the processor at block size `block`; MIDI events at absolute samples
static std::vector<float> renderProc (RU8AudioProcessor& p, Host& host, double seconds, int block, const std::vector<std::pair<int64_t, juce::MidiMessage>>& events = {})
{
    const int n = (int) (seconds * host.sr);
    std::vector<float> out ((size_t) n * 2);
    juce::AudioBuffer<float> buf (2, block);
    for (int pos = 0; pos < n; pos += block)
    {
        const int len = std::min (block, n - pos);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, len);
        view.clear();
        juce::MidiBuffer midi;
        for (const auto& [t, m] : events) if (t >= pos && t < pos + len) midi.addEvent (m, (int) (t - pos));
        host.pos = pos;
        p.processBlock (view, midi);
        for (int i = 0; i < len; ++i) { out[(size_t) (pos + i) * 2] = view.getSample (0, i); out[(size_t) (pos + i) * 2 + 1] = view.getSample (1, i); }
    }
    return out;
}

static std::unique_ptr<RU8AudioProcessor> makeProc (Host& host, int block, std::function<void (RU8AudioProcessor&)> setup = nullptr)
{
    auto p = std::make_unique<RU8AudioProcessor>();
    p->setPlayHead (&host);
    p->setRateAndBufferSizeDetails (host.sr, block);
    if (setup) setup (*p);
    p->prepareToPlay (host.sr, block);
    return p;
}

// the raw engine and sequencer as the raw tool runs them, from transport position 0 at sample 0
static std::vector<float> renderRaw (int kit, int slot, double sr, double bpm, double seconds, bool autoArr, float human)
{
    ru8::Engine e; e.setKit (kit); e.prepare (sr, 512);
    ru8::Song song; ru8::SetPatterns set = song.slot[slot];
    ru8::Sequencer q; q.set = &set; q.autoArrange = autoArr; q.human = human; q.setInitial (ru8::Section::A);
    e.setEchoTime (set.echoTime, bpm); e.setRunning (true);
    const int n = (int) (seconds * sr);
    std::vector<float> L ((size_t) n), R ((size_t) n), out ((size_t) n * 2);
    for (int pos = 0; pos < n; pos += 512)
    {
        const int len = std::min (512, n - pos);
        q.process (e, pos, 0.0, bpm, len, sr, 1e18, 0.0);
        e.process (L.data() + pos, R.data() + pos, len);
    }
    for (int i = 0; i < n; ++i) { out[(size_t) i * 2] = L[(size_t) i]; out[(size_t) i * 2 + 1] = R[(size_t) i]; }
    return out;
}

static double maxDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double m = a.size() == b.size() ? 0 : 1e9;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i) m = std::max (m, (double) std::abs (a[i] - b[i]));
    return m;
}
static double peakDb (const std::vector<float>& a, size_t from = 0, size_t to = SIZE_MAX)
{
    float m = 0; for (size_t i = from; i < std::min (to, a.size()); ++i) m = std::max (m, std::abs (a[i]));
    return m > 0 ? 20 * std::log10 (m) : -999;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace ru8;
    auto setupHouse = [] (RU8AudioProcessor& p) { setP (p, "auto", 1); setP (p, "human", 0); };

    // 1. processor == raw engine + sequencer (Analog, House, auto, HUMAN 0, 8 bars)
    {
        Host h; h.bpm = kSets[0].bpm;
        auto p = makeProc (h, 512, setupHouse);
        const double secs = 8 * 4 * 60.0 / h.bpm;
        const auto a = renderProc (*p, h, secs, 512);
        const auto b = renderRaw (0, 0, h.sr, h.bpm, secs, true, 0);
        const double d = maxDiff (a, b);
        // parameters pass through the host's normalised range, so a kit value like -0.22 comes back a float ulp away: -120 dB
        check (d <= 1.0e-6, "processor output equals the raw engine + sequencer within -120 dB (Analog, House, 8 bars, 48 kHz)",
               juce::String::formatted ("max |diff| %.3g, peak %.2f dBFS", d, peakDb (a)));
    }
    // 2. block sizes and sample rates (host PPQ per block)
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::vector<float> ref; double worst = 0; bool sized = true;
        for (int block : { 512, 1, 64, 480, 4096 })
        {
            Host h; h.sr = sr; h.bpm = 128; h.startPpq = 0;
            auto p = makeProc (h, block, [] (RU8AudioProcessor& q) { setP (q, "auto", 1); setP (q, "human", 0.12f); setP (q, "set", 8); });
            const auto out = renderProc (*p, h, 4.0, block);
            if (ref.empty()) ref = out; else { worst = std::max (worst, maxDiff (out, ref)); sized = sized && out.size() == ref.size(); }
        }
        check (sized && worst == 0.0, juce::String::formatted ("bit-identical at blocks 1/64/480/512/4096, %.1f kHz (Breakbeat, HUMAN 12 %%)", sr / 1000),
               juce::String::formatted ("max |diff| %.3g", worst));
    }
    // 3. latency
    {
        Host h; auto p = makeProc (h, 512);
        check (p->getLatencySamples() == p->engineForTest().latency() && p->getLatencySamples() == 288 + 128 + 72,
               "reported latency = glue 6 ms + Color 2x filters 128 + limiter 1.5 ms", juce::String (p->getLatencySamples()) + " samples at 48 kHz");
    }
    // 4. MIDI in
    {
        Host h; h.playing = false;
        auto p = makeProc (h, 512);
        const int64_t at = 12345;
        const auto out = renderProc (*p, h, 0.5, 512, { { at, juce::MidiMessage::noteOn (10, 38, (juce::uint8) 127) } });
        size_t first = 0; for (size_t i = 0; i < out.size(); i += 2) if (std::abs (out[i]) > 1e-5f) { first = i / 2; break; }
        const int lat = p->getLatencySamples();
        check (first > 0 && (int64_t) first >= at + lat - 2 && (int64_t) first <= at + lat + 4, "MIDI note 38 plays the snare on its sample (+ latency)",
               juce::String::formatted ("note at %d, sound from %d (latency %d)", (int) at, (int) first, lat));
    }
    {
        // D-1 in bar 1 → bar 2 plays B (House B adds a rim on step 11); F-1 in bar 3 → bar 4 is silent
        Host h; h.bpm = kSets[0].bpm;
        auto p = makeProc (h, 512, [] (RU8AudioProcessor& q) { setP (q, "human", 0); });
        const double bar = 4 * 60.0 / h.bpm * h.sr;
        const auto out = renderProc (*p, h, 4.6 * 4 * 60.0 / h.bpm, 512, { { (int64_t) (0.5 * bar), juce::MidiMessage::noteOn (1, 14, (juce::uint8) 100) },
                                                                          { (int64_t) (2.5 * bar), juce::MidiMessage::noteOn (1, 17, (juce::uint8) 100) } });
        auto ref = [&] (bool b) { auto q = makeProc (h, 512, [b] (RU8AudioProcessor& r) { setP (r, "human", 0); setP (r, "section", b ? 2.0f : 1.0f); });
                                  return renderProc (*q, h, 4.6 * 4 * 60.0 / h.bpm, 512); };
        const auto A = ref (false);
        // the section each bar plays, as the panel reads it (the same plan the hits come from)
        auto planAt = [&] (double bars, const std::vector<std::pair<int64_t, juce::MidiMessage>>& ev) {
            auto q = makeProc (h, 512, [] (RU8AudioProcessor& r) { setP (r, "human", 0); });
            renderProc (*q, h, bars * 4 * 60.0 / h.bpm, 512, ev); return q->display(); };
        const std::vector<std::pair<int64_t, juce::MidiMessage>> ev { { (int64_t) (0.5 * bar), juce::MidiMessage::noteOn (1, 14, (juce::uint8) 100) },
                                                                      { (int64_t) (2.5 * bar), juce::MidiMessage::noteOn (1, 17, (juce::uint8) 100) } };
        const auto d1 = planAt (0.9, ev), d2 = planAt (1.5, ev), d4 = planAt (3.5, ev);
        const double tailPeak = peakDb (out, (size_t) (3.5 * bar) * 2, (size_t) (4.5 * bar) * 2);
        check (d1.section == 1 && d2.section == 2 && d2.bar == 1, "D-1 in bar 1: bar 2 plays section B",
               juce::String::formatted ("bar %d section %d, bar %d section %d (1 = A, 2 = B)", d1.bar + 1, d1.section, d2.bar + 1, d2.section));
        check (d4.section == 0, "F-1 in bar 3: bar 4 shows the sequencer off", juce::String (d4.section));
        check (tailPeak < peakDb (A, (size_t) (3.5 * bar) * 2, (size_t) (4.5 * bar) * 2) - 12, "F-1 in bar 3: the sequencer is off from bar 4",
               juce::String::formatted ("bar 4 peak %.1f dBFS (with the sequencer %.1f)", tailPeak, peakDb (A, (size_t) (3.5 * bar) * 2, (size_t) (4.5 * bar) * 2)));
    }
    // 5. a new set while playing starts on the next bar line
    {
        Host h; h.bpm = 120;
        auto p = makeProc (h, 512, [] (RU8AudioProcessor& q) { setP (q, "human", 0); });
        const double bar = 4 * 60.0 / h.bpm * h.sr;
        renderProc (*p, h, 0.6 * 4 * 60.0 / h.bpm, 512);            // into bar 1
        setP (*p, "set", (float) Song::findSlot ("dnb"));
        const int before = p->currentSlot();
        h.pos = (int64_t) (0.6 * bar);
        // continue rendering to past the bar line
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi; int switchedAt = -1;
        for (int64_t pos = (int64_t) (0.6 * bar) / 512 * 512; pos < (int64_t) (1.5 * bar); pos += 512)
        {
            h.pos = pos; buf.clear(); p->processBlock (buf, midi);
            if (switchedAt < 0 && p->currentSlot() == Song::findSlot ("dnb")) switchedAt = (int) pos;
        }
        check (before == 0 && switchedAt >= (int) bar - 512 && switchedAt <= (int) bar, "a new set while playing takes over on the next bar line",
               juce::String::formatted ("bar line at %d, switched in the block at %d", (int) bar, switchedAt));
    }
    // 6. kit change moves the kit's values; a restore keeps the project's
    {
        Host h; auto p = makeProc (h, 512);
        setP (*p, "kit", 12.0f);   // Lo-fi
        settle (*p);
        const float tape = p->apvts.getRawParameterValue ("colorTape")->load(), lvl = p->apvts.getRawParameterValue ("pad1.level")->load();
        check (std::abs (tape - kKits[12].color.tape) < 1e-4f && std::abs (lvl - kKits[12].ch[0].levelDb) < 0.051f && std::abs (p->apvts.getRawParameterValue ("glue")->load() - kKits[12].glue) < 1e-4f,
               "loading Lo-fi moves Color, glue and channels to the kit's values", juce::String::formatted ("tape %.2f, glue %.2f", tape, p->apvts.getRawParameterValue ("glue")->load()));
        setP (*p, "colorTape", 0.9f); setP (*p, "pad2.level", -12.0f);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = makeProc (h, 512);
        q->setStateInformation (mb.getData(), (int) mb.getSize());
        settle (*q);
        check (std::abs (q->apvts.getRawParameterValue ("colorTape")->load() - 0.9f) < 1e-4f && std::abs (q->apvts.getRawParameterValue ("pad2.level")->load() + 12.0f) < 0.051f,
               "restoring a project with Lo-fi keeps its own values (no kit reload)",
               juce::String::formatted ("tape %.3f (saved 0.900), pad 2 level %.2f dB (saved -12.00)", q->apvts.getRawParameterValue ("colorTape")->load(),
                                        q->apvts.getRawParameterValue ("pad2.level")->load()));
    }
    // 7. state round trip
    {
        Host h; auto p = makeProc (h, 512);
        p->song().slot[3].steps[0][1][5] = 'X'; p->song().slot[kNumSets].steps[1][0][0] = 'r'; p->song().slot[kNumSets].bpm = 101;
        p->setPadNoteMap (true);
        juce::Random rnd (7);
        for (auto* prm : p->getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) if (r->getParameterID() != "kit" && r->getParameterID() != "set" && r->getParameterID() != "spaceType") r->setValueNotifyingHost (rnd.nextFloat());
        settle (*p);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = makeProc (h, 512); q->setStateInformation (mb.getData(), (int) mb.getSize());
        int differ = 0; double worstUlp = 0; juce::String which;
        for (int i = 0; i < p->getParameters().size(); ++i)
        {
            // compare legal values (a bool written as 0.46 reads back as 0 or 1 from the state, as a host would see it)
            auto legal = [] (juce::AudioProcessorParameter* x) { auto* r = dynamic_cast<juce::RangedAudioParameter*> (x);
                                                                  return r ? (double) r->convertTo0to1 (r->getNormalisableRange().snapToLegalValue (r->convertFrom0to1 (r->getValue()))) : (double) x->getValue(); };
            const double d = std::abs (legal (p->getParameters()[i]) - legal (q->getParameters()[i]));
            if (d > 1e-6) { ++differ; which << p->getParameters()[i]->getName (32) << " " << juce::String (d, 7) << "; "; }
            else worstUlp = std::max (worstUlp, d);
        }
        if (differ) std::printf ("    differ: %s\n", which.toRawUTF8());
        const bool steps = std::memcmp (p->song().slot, q->song().slot, sizeof (p->song().slot)) == 0;
        check (differ == 0 && steps && q->padNoteMap(), "state: every parameter, the steps of every set, USER slots and the note map survive",
               juce::String (p->getParameters().size()) + " parameters (largest float rounding " + juce::String (worstUlp, 9) + "), " + juce::String ((int) mb.getSize()) + " bytes");
    }
    // 8. a second prepareToPlay (a host changing rate or block size) starts from rest: silence stays silence
    {
        Host h; auto p = makeProc (h, 512, [] (RU8AudioProcessor& q) { setP (q, "human", 0); });
        renderProc (*p, h, 3.0, 512);                         // play three seconds (hats, reverb, echo)
        h.playing = false; h.pos = 0;
        p->prepareToPlay (44100.0, 256); h.sr = 44100;
        const auto out = renderProc (*p, h, 1.0, 256);
        check (peakDb (out) < -120, "after a second prepareToPlay nothing sounds without notes", juce::String (peakDb (out), 1) + " dBFS");
    }
    // 9. the preview key in a host (P3 Live finding 1): RU-8's own clock at the host's tempo while the host is stopped;
    //    the host's transport wins when it starts
    {
        Host h; h.playing = false; h.bpm = 131;               // a host tempo other than the set's (House 120)
        auto p0 = makeProc (h, 512, setupHouse);
        const auto quiet = renderProc (*p0, h, 0.5, 512);   // a stopped host without the preview: silence
        auto p = makeProc (h, 512, setupHouse);
        p->setPreviewPlaying (true);
        const double secs = 4 * 4 * 60.0 / h.bpm;
        std::vector<float> a ((size_t) (secs * h.sr) * 2);
        {   // the host stays stopped at ppq 0; the preview starts at bar 1 on the next block
            const int n = (int) (secs * h.sr); juce::AudioBuffer<float> buf (2, 512);
            for (int pos = 0; pos < n; pos += 512)
            {
                const int len = std::min (512, n - pos);
                juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, len); view.clear(); juce::MidiBuffer midi;
                h.pos = 0; p->processBlock (view, midi);
                for (int i = 0; i < len; ++i) { a[(size_t) (pos + i) * 2] = view.getSample (0, i); a[(size_t) (pos + i) * 2 + 1] = view.getSample (1, i); }
            }
        }
        const auto d1 = p->display();
        const auto b = renderRaw (0, 0, h.sr, h.bpm, secs, true, 0);
        const double diff = maxDiff (a, b);
        check (peakDb (quiet) < -120 && diff <= 1.0e-6 && d1.playing && ! d1.hostPlaying && std::abs (d1.bpm - 131.0f) < 1e-3f,
               "preview in a host: stopped host, the play key runs RU-8 from bar 1 at the host's tempo (131) = raw engine + sequencer",
               juce::String::formatted ("before %.1f dBFS, max |diff| %.3g, peak %.2f dBFS, bar %d", peakDb (quiet), diff, peakDb (a), d1.bar));
        h.playing = true; h.startPpq = 8.0;
        renderProc (*p, h, 0.2, 512);
        const auto d2 = p->display();
        p->setPreviewPlaying (true);                          // refused while the host plays
        h.playing = false;
        renderProc (*p, h, 0.2, 512);
        const auto d3 = p->display();
        check (d2.playing && d2.hostPlaying && ! d3.playing && ! d3.hostPlaying,
               "preview in a host: the host's start takes over and ends the preview; after the host stops RU-8 stays stopped",
               juce::String::formatted ("host running: playing %d host %d; host stopped: playing %d", (int) d2.playing, (int) d2.hostPlaying, (int) d3.playing));
        p->startStop(); renderProc (*p, h, 0.1, 512);
        const bool on = p->display().playing;
        p->startStop(); renderProc (*p, h, 0.1, 512);
        const bool offAgain = ! p->display().playing;
        check (on && offAgain, "preview in a host: the play key toggles the preview on and off", juce::String::formatted ("on %d, off again %d", (int) on, (int) offAgain));
    }
    // 10. SET / MIDI (DAW rule 3; was the clip lock): armed by a drag, switched to MIDI by a pad note while the host plays
    {
        const auto note = [] (int n) { return juce::MidiMessage::noteOn (10, n, (juce::uint8) 110); };
        Host h; h.playing = true;
        auto p = makeProc (h, 512, setupHouse);
        // not armed: a pad note while the host plays does not switch (pads played from a keyboard over the set)
        renderProc (*p, h, 0.5, 512, { { 1000, note (36) } });
        settle (*p);
        const bool unarmed = ! p->midiSource() && p->display().playing;
        // armed, no notes yet: the set still plays with the host
        p->armMidiSource();
        h.startPpq = 8.0; renderProc (*p, h, 0.5, 512);
        const bool armedPlays = p->midiSourceArmed() && ! p->midiSource() && p->display().playing;
        // a trigger note (C-1) is not proof: only notes that play a pad
        h.startPpq = 16.0; renderProc (*p, h, 0.2, 512, { { 100, note (ru8::kTriggerA) } });
        const bool triggerNoProof = ! p->midiSource();
        // the clip's first pad note: MIDI at once on the audio thread, the parameter follows on the message thread
        h.startPpq = 24.0; renderProc (*p, h, 0.2, 512, { { 100, note (38) } });
        const bool lockedNow = p->midiSource() && ! p->display().playing && ! p->midiSourceArmed();
        settle (*p);
        const bool param = p->apvts.getRawParameterValue ("source")->load() > 0.5f;
        check (unarmed && armedPlays && triggerNoProof && lockedNow && param,
               "set / midi: a drag arms MIDI; the set plays on until the clip's first pad note arrives with the host playing; then MIDI (parameter written)",
               juce::String::formatted ("unarmed note %d, armed plays %d, trigger no proof %d, midi %d, param %d", (int) unarmed, (int) armedPlays, (int) triggerNoProof, (int) lockedNow, (int) param));
        // MIDI, the host plays, no MIDI: after the tails, silence (the set does not run)
        h.startPpq = 32.0; renderProc (*p, h, 6.0, 512);
        h.startPpq = 64.0; const auto quiet = renderProc (*p, h, 2.0, 512);
        h.startPpq = 72.0; const auto hit = renderProc (*p, h, 1.0, 512, { { 4800, note (36) } });
        check (peakDb (quiet) < -100 && peakDb (hit) > -20,
               "set / midi: in MIDI the host's transport no longer runs the set; MIDI notes play the pads",
               juce::String::formatted ("2 s with the host playing and no MIDI %.1f dBFS; one kick note %.1f dBFS", peakDb (quiet), peakDb (hit)));
        // the preview still plays the set while the host is stopped
        h.playing = false; renderProc (*p, h, 0.1, 512);   // the processor sees the host stop (the key takes no clicks before)
        p->setPreviewPlaying (true);
        const auto prev = renderProc (*p, h, 2.0, 512);
        const bool previews = p->display().playing && peakDb (prev) > -20;
        p->setPreviewPlaying (false); renderProc (*p, h, 0.1, 512);
        // MIDI is saved and comes back; arm is not saved
        juce::MemoryBlock mb; p->getStateInformation (mb);
        Host h2; auto q = makeProc (h2, 512);
        q->setStateInformation (mb.getData(), (int) mb.getSize());
        const bool restored = q->midiSource() && ! q->midiSourceArmed();
        check (previews && restored, "set / midi: the preview still plays the set; MIDI is saved with the project and restored",
               juce::String::formatted ("preview peak %.1f dBFS, restored %d", peakDb (prev), (int) restored));
        // back to SET: the host runs the set
        setP (*q, "source", 0.0f);
        renderProc (*q, h2, 0.5, 512);
        check (! q->midiSource() && q->display().playing, "set / midi: back to SET, the host's transport runs the set again",
               juce::String::formatted ("midi source %d, playing %d", (int) q->midiSource(), (int) q->display().playing));
    }
    // 11. DAW rule 2: the preview hands over to the host without a cut, in step with the host's bar position
    {
        Host h; h.playing = false; h.bpm = 120;
        auto p = makeProc (h, 512, setupHouse);
        p->setPreviewPlaying (true);
        const auto pre = renderProc (*p, h, 1.3, 512);       // preview: about 2.6 beats on RU-8's own clock
        h.playing = true; h.startPpq = 16.5;                   // the host starts in bar 5, beat 3 (half a beat in)
        const auto post = renderProc (*p, h, 1.0, 512);
        const auto d = p->display();
        const double ppqLast = h.startPpq + (double) h.pos / h.sr * h.bpm / 60.0;   // host position of the last block
        const int hostBar = (int) std::floor (ppqLast / 4.0), hostStep = (int) std::floor ((ppqLast - hostBar * 4.0) * 4.0);
        // no cut: the 10 ms either side of the handover are not silent, and no step at the seam
        const size_t seam = pre.size();
        std::vector<float> both (pre); both.insert (both.end(), post.begin(), post.end());
        const double before = peakDb (both, seam - 960, seam), after = peakDb (both, seam, seam + 960);
        float jump = 0; for (size_t i = seam - 4; i < seam + 4; i += 2) jump = std::max (jump, std::abs (both[i + 2] - both[i]));
        check (d.playing && d.hostPlaying && d.bar == hostBar && d.step == hostStep && before > -60 && after > -60 && jump < 0.1f,
               "preview -> host: the host's start takes over in step with its bar (bar and 16th = the host's), no cut at the seam",
               juce::String::formatted ("RU-8 bar %d step %d, host bar %d step %d; 10 ms before %.1f dBFS, after %.1f dBFS; step at the seam %.4f",
                                        d.bar + 1, d.step + 1, hostBar + 1, hostStep + 1, before, after, (double) jump));
    }
    // 12. DAW rule 6a: VST3 note names (map- and kit-aware)
    {
        Host h; auto p = makeProc (h, 512);
        const auto n36 = p->getNameForMidiNoteNumber (36, 10), n38 = p->getNameForMidiNoteNumber (38, 10), n42 = p->getNameForMidiNoteNumber (42, 1);
        const auto none = p->getNameForMidiNoteNumber (100, 10), trig = p->getNameForMidiNoteNumber (ru8::kTriggerA, 10), neg = p->getNameForMidiNoteNumber (-3, 1);
        int named = 0; for (int n = 0; n < 128; ++n) named += p->getNameForMidiNoteNumber (n, 10).has_value() ? 1 : 0;
        // the aux pads follow the kit: kit 7 (Anatolia) pad 9
        int auxNote = -1; for (int n = 0; n < 128 && auxNote < 0; ++n) if (ru8::noteToPad (n, false) == 8) auxNote = n;
        setP (*p, "kit", 7.0f);
        const auto aux = p->getNameForMidiNoteNumber (auxNote, 10);
        // the pads map: 36 + i
        p->setPadNoteMap (true);
        const auto m37 = p->getNameForMidiNoteNumber (37, 10), m51 = p->getNameForMidiNoteNumber (51, 10), m52 = p->getNameForMidiNoteNumber (52, 10);
        const bool ok = n36 && *n36 == "Kick" && n38 && *n38 == "Snare" && n42 && *n42 == "Closed hat" && ! none && trig && ! neg
                     && aux && *aux == juce::String (ru8::kKits[7].aux[0].name) && m37 && *m37 == "Snare" && m51 && *m51 == RU8AudioProcessor::padNameFor (15, 7) && ! m52;
        check (ok, "note names: GM map (36 Kick, 38 Snare, 42 Closed hat), triggers named, aux pads follow the kit, pads map 36-51",
               juce::String::formatted ("%d named notes in GM; kit 7 note %d = %s; pads map 51 = %s", named, auxNote, aux ? aux->toRawUTF8() : "-", m51 ? m51->toRawUTF8() : "-"));
    }
    // 13. factory presets (P4): the host's programs, what a load writes, the groove, levels, modified, undo, the restore rule
    {
        Host h; auto p = makeProc (h, 512);
        bool names = p->getNumPrograms() == ru8::kNumPresets;
        for (int i = 0; i < ru8::kNumPresets; ++i) names = names && p->getProgramName (i) == juce::String (ru8::kPresets[i].name) && p->getProgramName (i).toStdString().find_first_not_of (
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -&") == std::string::npos;
        check (names && p->getCurrentProgram() == 0 && ! p->presetModified(), "presets: 28 host programs with their ASCII names; a new RU-8 is program 0, not modified",
               juce::String (p->getNumPrograms()) + " programs, current " + juce::String (p->getCurrentProgram()) + ", first " + p->getProgramName (0) + ", last " + p->getProgramName (27));

        // a new RU-8 equals preset 0 loaded (from another preset)
        auto snap = [] (RU8AudioProcessor& q) { std::vector<float> v; for (auto* r : q.getParameters()) v.push_back (r->getValue()); return v; };
        const auto fresh = snap (*p);
        p->loadPreset (5); p->loadPreset (0);
        const auto again = snap (*p);
        int diff0 = 0; for (size_t i = 0; i < fresh.size(); ++i) diff0 += std::abs (fresh[i] - again[i]) > 1e-6f ? 1 : 0;
        check (diff0 == 0, "presets: loading preset 0 gives exactly a new RU-8 (every parameter)", juce::String (diff0) + " parameters differ");
    }
    {   // every preset: what lands, and how loud it plays (8 bars, auto, HUMAN 12 %, its set's tempo, VOLUME -2 dB)
        int landed = 0, quiet = 0; double worstPeak = -100; juce::String worstAt, bad;
        for (int i = 0; i < ru8::kNumPresets; ++i)
        {
            Host h; h.playing = false;
            auto p = makeProc (h, 512);
            p->song().slot[ru8::presetSet (i)].steps[0][0][1] = 'X';   // an edit of that set: a load gives the factory steps back
            p->loadPreset (i);
            const int kit = ru8::presetKit (i), set = ru8::presetSet (i);
            const auto& kd = ru8::kKits[kit];
            auto raw = [&] (const char* id) { return p->apvts.getRawParameterValue (id)->load(); };
            bool ok = juce::roundToInt (raw ("kit")) == kit && juce::roundToInt (raw ("set")) == set && p->getCurrentProgram() == i && ! p->presetModified()
                   && std::abs (raw ("glue") - kd.glue) < 1e-4f && std::abs (raw ("pad1.level") - kd.ch[0].levelDb) < 0.051f && std::abs (raw ("spaceSize") - kd.space.size) < 1e-4f
                   && std::abs (raw ("colorTape") - kd.color.tape) < 1e-4f && std::abs (raw ("human") - 0.12f) < 1e-4f && std::abs (raw ("volume") + 2.0f) < 0.051f
                   && std::abs (raw ("swing") - ru8::kSets[set].swing) < 0.006f && juce::roundToInt (raw ("echoTime")) == ru8::kSets[set].echoTime
                   && std::memcmp (p->song().slot[set].steps[0][0], ru8::kSets[set].A[0], 16) == 0;
            for (const auto& v : ru8::kPresets[i].values)
                if (v.param != nullptr) ok = ok && std::abs (raw (v.param) - v.value) < 0.051f;
            landed += ok ? 1 : 0;
            if (! ok) bad << ru8::kPresets[i].name << "; ";
            setP (*p, "auto", 1.0f);
            h.bpm = ru8::kSets[set].bpm; h.playing = true;
            const double seconds = 8 * 4 * 60.0 / h.bpm + 2.0;
            const auto out = renderProc (*p, h, seconds, 512);
            const double pk = peakDb (out);
            const bool fine = ! p->engineForTest().limiterActed() && pk < -1.0;
            quiet += fine ? 1 : 0;
            if (pk > worstPeak) { worstPeak = pk; worstAt = ru8::kPresets[i].name; }
            if (! fine) bad << ru8::kPresets[i].name << " peak " << juce::String (pk, 2) << (p->engineForTest().limiterActed() ? " limiter" : "") << "; ";
            std::printf ("      %-24s peak %6.2f dBFS%s\n", ru8::kPresets[i].name, pk, p->engineForTest().limiterActed() ? "  LIMITER ACTED" : "");
        }
        check (landed == ru8::kNumPresets, "presets: each of the 28 loads its kit, its kit's values, its set with the factory steps, swing, echo time, HUMAN 12 %, VOLUME -2 dB",
               juce::String (landed) + " of 28" + (bad.isNotEmpty() ? "; " + bad : juce::String()));
        check (quiet == ru8::kNumPresets, "presets: each plays 8 bars (auto, HUMAN 12 %, its tempo) under -1 dBFS without the limiter acting at VOLUME -2 dB",
               juce::String (quiet) + " of 28; loudest " + worstAt + " " + juce::String (worstPeak, 2) + " dBFS");
    }
    {   // modified, undo, the set while playing
        Host h; auto p = makeProc (h, 512);
        p->loadPreset (3);
        settle (*p);
        const bool clean = ! p->presetModified();
        setP (*p, "pad2.level", -20.0f); settle (*p);
        const bool dirtyParam = p->presetModified();
        setP (*p, "pad2.level", ru8::kKits[ru8::presetKit (3)].ch[1].levelDb); settle (*p);
        const bool cleanAgain = ! p->presetModified();
        const char was = p->song().slot[ru8::presetSet (3)].steps[0][2][3];
        p->song().slot[ru8::presetSet (3)].steps[0][2][3] = was == '.' ? 'x' : '.'; settle (*p);
        const bool dirtyStep = p->presetModified();
        p->song().slot[ru8::presetSet (3)].steps[0][2][3] = was;
        setP (*p, "pad5.mute", 1.0f); setP (*p, "section", 2.0f / 3.0f); settle (*p);
        const bool muteNotDirty = ! p->presetModified();
        check (clean && dirtyParam && cleanAgain && dirtyStep && muteNotDirty,
               "presets: modified after a value or a step changes, clean when it is back; mute and section do not count",
               juce::String::formatted ("loaded %d, value %d, back %d, step %d, mute/section %d", clean, dirtyParam, cleanAgain, dirtyStep, muteNotDirty));

        const int kitBefore = juce::roundToInt (p->apvts.getRawParameterValue ("kit")->load());
        const float glueBefore = p->apvts.getRawParameterValue ("glue")->load();
        p->loadPreset (20);
        const bool moved = juce::roundToInt (p->apvts.getRawParameterValue ("kit")->load()) == ru8::presetKit (20);
        const int kitLoaded = juce::roundToInt (p->apvts.getRawParameterValue ("kit")->load());
        p->undo.undo();
        const int kitUndone = juce::roundToInt (p->apvts.getRawParameterValue ("kit")->load());
        const bool back = kitUndone == kitBefore && std::abs (p->apvts.getRawParameterValue ("glue")->load() - glueBefore) < 1e-5f;
        p->undo.redo();
        const int kitRedone = juce::roundToInt (p->apvts.getRawParameterValue ("kit")->load());
        const bool forward = kitRedone == ru8::presetKit (20);
        check (moved && back && forward, "presets: a load is one undo step (undo: the previous kit and values; redo: the preset again)",
               juce::String::formatted ("kit %d -> load %d -> undo %d -> redo %d", kitBefore, kitLoaded, kitUndone, kitRedone));
    }
    {   // while playing, the preset's set lands on the next bar line
        Host h; auto p = makeProc (h, 512);
        const double bar = 4 * 60.0 / h.bpm * h.sr;
        renderProc (*p, h, 0.5, 512);
        p->loadPreset (1);   // Techno - Analog: another set
        const int before = p->currentSlot();
        int switchedAt = -1;
        juce::AudioBuffer<float> buf (2, 512);
        for (int pos = (int) (0.5 * h.sr) / 512 * 512; pos < (int) (2 * bar); pos += 512)
        {
            juce::MidiBuffer midi; buf.clear(); h.pos = pos; p->processBlock (buf, midi);
            if (switchedAt < 0 && p->currentSlot() == ru8::presetSet (1)) switchedAt = pos;
        }
        check (before == ru8::presetSet (0) && switchedAt >= (int) bar - 512 && switchedAt <= (int) bar, "presets: while playing, the preset's set takes over on the next bar line",
               "bar line at " + juce::String ((int) bar) + ", switched in the block at " + juce::String (switchedAt));
    }
    {   // the restore rule: the project's program comes back; the host setting it again changes nothing; another program loads
        Host h; auto p = makeProc (h, 512);
        p->loadPreset (9);
        setP (*p, "pad1.level", -30.0f); settle (*p);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = makeProc (h, 512);
        q->setStateInformation (mb.getData(), (int) mb.getSize());
        settle (*q);
        const bool restored = q->getCurrentProgram() == 9 && q->presetModified() && std::abs (q->apvts.getRawParameterValue ("pad1.level")->load() + 30.0f) < 0.051f;
        q->setCurrentProgram (9); settle (*q);
        const bool kept = std::abs (q->apvts.getRawParameterValue ("pad1.level")->load() + 30.0f) < 0.051f && q->presetModified();
        q->setCurrentProgram (10); settle (*q);
        const bool loaded = q->getCurrentProgram() == 10 && ! q->presetModified() && juce::roundToInt (q->apvts.getRawParameterValue ("kit")->load()) == ru8::presetKit (10);
        // a program set from another thread loads on the message thread
        std::thread t ([&] { q->setCurrentProgram (12); }); t.join();
        settle (*q);
        const bool fromThread = q->getCurrentProgram() == 12;
        check (restored && kept && loaded && fromThread, "presets: a project restores its program and 'modified'; setCurrentProgram to it changes nothing; another program loads (also from another thread)",
               juce::String::formatted ("restored %d, kept %d, loaded %d, other thread %d", restored, kept, loaded, fromThread));
    }
    std::printf ("\nwaits for the processor's timer: %d, longest %.1f ms, timed out (5 s) %d\n", gSettles, gSettleWorstMs, gSettleTimeouts);
    std::printf ("\n%d of %d checks pass\n", passes, passes + fails);
    return fails ? 1 : 0;
}
