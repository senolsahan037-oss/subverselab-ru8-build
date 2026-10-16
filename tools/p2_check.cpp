// p2_check: the sequencer, arrangement, MIDI and state checks of phase P2 (no JUCE; the plug-in runs the same code).
//   p2_check            prints one line per check and a summary; exit code 1 if anything fails
#include "../Source/RU8Sequencer.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace ru8;
static int fails = 0, passes = 0;
static void check (bool ok, const char* what, const std::string& detail = "")
{
    (ok ? passes : fails)++;
    std::printf ("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
}

struct Rec { float dust = 0; std::vector<Hit> v; void hitAt (int p, float vel, double t) { v.push_back ({ p, vel, t }); } };

// all hits of `bars` bars from a fixed origin, cut into blocks of `block`
static std::vector<Hit> run (const SetPatterns& set, double sr, int block, int bars, bool autoArr, float human, bool hostPpq)
{
    Sequencer q; q.set = &set; q.autoArrange = autoArr; q.human = human;
    Rec r;
    const double spq = 60.0 / set.bpm * sr;
    const int64_t total = (int64_t) std::ceil (bars * 4 * spq) + 4096;
    for (int64_t pos = 0; pos < total; pos += block)
    {
        const int n = (int) std::min<int64_t> (block, total - pos);
        double origin = 0;
        if (hostPpq)
        {   // as a host gives it: the block's position in quarter notes, rounded the way a DAW passes a double
            const double ppq = (double) pos / spq;
            origin = (double) pos - ppq * spq;
        }
        q.process (r, pos, origin, set.bpm, n, sr, bars * 4.0, 0.0, 0.0);
    }
    return r.v;
}

static bool sameHits (const std::vector<Hit>& a, const std::vector<Hit>& b, double tol, double* worst = nullptr)
{
    if (a.size() != b.size()) return false;
    double w = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].pad != b[i].pad || a[i].vel != b[i].vel) return false;
        w = std::max (w, std::fabs (a[i].time - b[i].time));
    }
    if (worst) *worst = w;
    return w <= tol;
}

static std::vector<Hit> sorted (std::vector<Hit> v)
{
    std::stable_sort (v.begin(), v.end(), [] (const Hit& a, const Hit& b) { return a.time != b.time ? a.time < b.time : a.pad < b.pad; });
    return v;
}

// minimal Standard MIDI File reader: header and (tick, status, note, vel) events, end-of-track tick
struct Smf { int format = -1, tracks = -1, ppq = -1; std::vector<std::array<int, 4>> ev; int endTick = -1; bool ok = false; };
static Smf readSmf (const std::vector<uint8_t>& b)
{
    Smf s;
    if (b.size() < 22 || std::memcmp (b.data(), "MThd", 4) != 0) return s;
    s.format = b[8] << 8 | b[9]; s.tracks = b[10] << 8 | b[11]; s.ppq = b[12] << 8 | b[13];
    if (std::memcmp (b.data() + 14, "MTrk", 4) != 0) return s;
    const size_t len = (size_t) b[18] << 24 | b[19] << 16 | b[20] << 8 | b[21];
    if (22 + len != b.size()) return s;
    size_t i = 22; int t = 0;
    auto vlq = [&] () { int v = 0; while (i < b.size()) { const uint8_t c = b[i++]; v = (v << 7) | (c & 0x7f); if (! (c & 0x80)) break; } return v; };
    while (i < b.size())
    {
        t += vlq();
        const uint8_t st = b[i++];
        if (st == 0xFF) { const uint8_t type = b[i++]; const int l = vlq(); if (type == 0x2F) { s.endTick = t; s.ok = true; break; } i += (size_t) l; }
        else { const int n = b[i++], v = b[i++]; s.ev.push_back ({ t, st, n, v }); }
    }
    return s;
}

int main()
{
    Song song;
    // 1. timing: the same hits at every block size, at 44.1 / 48 / 96 kHz (fixed origin, HUMAN 0 and 12 %)
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (float human : { 0.0f, 0.12f })
            for (int si : { Song::findSlot ("house"), Song::findSlot ("jazz"), Song::findSlot ("lofi") })
            {
                const auto ref = sorted (run (song.slot[si], sr, 4096, 8, true, human, false));
                bool all = ! ref.empty();
                for (int block : { 1, 7, 64, 512 }) all = all && sameHits (sorted (run (song.slot[si], sr, block, 8, true, human, false)), ref, 0.0);
                char what[160]; std::snprintf (what, sizeof what, "hits identical at blocks 1/7/64/512/4096: %s, %.1f kHz, HUMAN %d %%", Song::slotId (si), sr / 1000, (int) std::lround (human * 100));
                check (all, what, std::to_string (ref.size()) + " hits");
            }
    // 2. host PPQ per block (origin re-derived each block) lands on the same samples within 1e-6
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const int si = Song::findSlot ("house");
        const auto ref = sorted (run (song.slot[si], sr, 512, 8, true, 0, false));
        double worst = 0; bool ok = true;
        for (int block : { 1, 64, 480, 4096 }) { double w = 0; ok = ok && sameHits (sorted (run (song.slot[si], sr, block, 8, true, 0, true)), ref, 1e-6, &w); worst = std::max (worst, w); }
        char what[96]; std::snprintf (what, sizeof what, "host PPQ path matches the exact step times at %.1f kHz", sr / 1000);
        char det[64]; std::snprintf (det, sizeof det, "worst %.2e samples", worst);
        check (ok, what, det);
    }
    // 3. step positions: 16 per bar, on the quarter-note grid, swing as Tone's transport
    {
        const auto& h = song.slot[Song::findSlot ("house")];
        const double sr = 48000, spq = 60.0 / h.bpm * sr;
        const auto hits = sorted (run (h, sr, 512, 1, false, 0, false));
        bool ok = true;
        for (const auto& x : hits)
        {
            const double steps = x.time / (spq / 4);
            const int k = (int) std::lround (steps - 0.2);
            const double expect = (k / 4.0 + ((k % 2) ? (1.0 / 6.0) * h.swing : 0)) * spq;
            ok = ok && std::fabs (x.time - expect) < 1e-6;
        }
        check (ok && ! hits.empty(), "house: kick on every quarter, odd 16ths late by swing/6 of a beat", std::to_string (hits.size()) + " hits");
    }
    // 4. a host starting at bar 23 plays bar 23 (AUTO: 23 mod 8 = 7, the B fill)
    {
        const auto& set = song.slot[Song::findSlot ("house")];
        Sequencer q; q.set = &set; q.autoArrange = true; Rec r;
        const double sr = 48000, spq = 60.0 / set.bpm * sr;
        const int64_t pos0 = 1000000;
        const double origin = (double) pos0 - 23 * 4 * spq;   // the host says: quarter note 92 at sample 1 000 000
        for (int64_t pos = pos0; pos < pos0 + (int64_t) (4 * spq); pos += 512) q.process (r, pos, origin, set.bpm, 512, sr);
        bool ok = ! r.v.empty(); int inBar = 0;
        for (const auto& x : r.v)
        {
            const int step = (int) std::lround ((x.time - origin) / (spq / 4) - 0.2) - 23 * 16;
            if (step >= 16) continue;   // the last block reaches into bar 24
            ok = ok && step >= 0 && q.stepChar (1, true, x.pad, step) != '.'; ++inBar;
        }
        int expected = 0; for (int p = 0; p < kNumPads; ++p) for (int st = 0; st < 16; ++st) expected += q.stepChar (1, true, p, st) != '.';
        ok = ok && inBar == expected;
        int sec; bool fill, on; q.barPlan (23, origin + 92 * spq, sec, fill, on);
        check (ok && sec == 1 && fill, "host starts at bar 23: plays bar 23 of the arrangement (B fill)", std::to_string (r.v.size()) + " hits");
    }
    // 5. section automation and trigger notes switch on the next bar line
    {
        const auto& set = song.slot[Song::findSlot ("house")];
        const double sr = 48000, spq = 60.0 / set.bpm * sr, bar = 4 * spq;
        Sequencer q; q.set = &set; q.autoArrange = false; q.setInitial (Section::A);
        q.requestSection (Section::B, 1.5 * bar);             // parameter moved in bar 1 → bar 2 is B
        q.noteRequest (kTriggerFill, 2.25 * bar);              // E-1 in bar 2 → bar 3 is a fill of B, then B again
        q.noteRequest (kTriggerOff, 4.5 * bar);                // F-1 in bar 4 → off from bar 5
        q.noteRequest (kTriggerA, 6.75 * bar);                 // C-1 in bar 6 → A from bar 7
        std::string plan;
        for (int b = 0; b < 9; ++b)
        {
            int sec; bool fill, on; q.barPlan (b, b * bar, sec, fill, on);
            plan += ! on ? "-" : fill ? (sec ? "b" : "a") : (sec ? "B" : "A");
        }
        check (plan == "AABbB--AA", "Section parameter and C-1 / D-1 / E-1 / F-1 act on the next bar line", plan + " (A/B section, a/b fill, - off)");
        // the parameter held on Fill (an automation lane) fills every bar until it moves; E-1 is one bar
        {
            Sequencer q2; q2.set = &set; q2.autoArrange = false; q2.setInitial (Section::A);
            q2.requestSection (Section::B, 0.5 * bar);
            q2.requestSection (Section::fill, 1.5 * bar);
            q2.requestSection (Section::A, 4.5 * bar);
            std::string plan2;
            for (int b = 0; b < 7; ++b) { int sec; bool fill, on; q2.barPlan (b, b * bar, sec, fill, on); plan2 += ! on ? "-" : fill ? (sec ? "b" : "a") : (sec ? "B" : "A"); }
            check (plan2 == "ABbbbAA", "Section parameter held on Fill: every bar a fill of the current section until it moves", plan2);
        }
        check (Sequencer::isTriggerNote (12) && Sequencer::isTriggerNote (14) && Sequencer::isTriggerNote (16) && Sequencer::isTriggerNote (17) && ! Sequencer::isTriggerNote (36),
               "trigger notes are C-1 12, D-1 14, E-1 16, F-1 17 (C4 = 60)");
    }
    // 6. MIDI files: type 0, one track, 96 PPQ, channel 10; 1 bar = 384 ticks, 8 bars = 3072; swing as the sequencer
    {
        const int si = Song::findSlot ("house");
        const auto bar = makeMidiClip (song.slot[si], "House", "Analog", false, 0, -1, false, 124);
        const auto full = makeMidiClip (song.slot[si], "House", "Analog", true, 0, -1, false, 124);
        const auto solo = makeMidiClip (song.slot[si], "House", "Analog", false, 0, BD, false, 124, "Kick");
        const Smf a = readSmf (bar.bytes), b = readSmf (full.bytes), c = readSmf (solo.bytes);
        bool ch10 = true; for (const auto* s : { &a, &b, &c }) for (const auto& e : s->ev) ch10 = ch10 && (e[1] == 0x99 || e[1] == 0x89);
        check (a.ok && a.format == 0 && a.tracks == 1 && a.ppq == 96 && ch10, "MIDI: type 0, one track, 96 PPQ, channel 10");
        check (a.endTick == 384 && b.endTick == 3072, "MIDI: 1 bar = 384 ticks, 8 bars = 3072", std::to_string (a.endTick) + " / " + std::to_string (b.endTick));
        int kicks = 0; for (const auto& e : c.ev) if (e[1] == 0x99) kicks += e[2] == 36;
        check (kicks == 4 && solo.name == "RU-8 House Analog Kick Sec A 124bpm.mid", "MIDI: solo kick = 4 notes on 36, file name", solo.name);
        // swing in the file equals the sequencer's step offset, rounded to the tick, for every swung set
        bool swingOk = true; int checked = 0;
        for (int i = 0; i < kNumSets; ++i)
        {
            const auto& set = song.slot[i];
            if (set.swing <= 0) continue;
            const auto clip = makeMidiClip (set, kSets[i].id, "Analog", false, 0, -1, false, set.bpm);
            Sequencer q; q.set = &set;
            // every note-on in the file is a step at its swung tick (or a roll's second note 12 ticks later)
            std::map<int, int> expect;
            for (int p = 0; p < kNumPads; ++p)
                for (int st = 0; st < 16; ++st)
                {
                    const char c = q.stepChar (0, false, p, st);
                    if (c == '.') continue;
                    const int t = st * 24 + (int) std::lround ((q.stepPpq (st) - st / 4.0) * 96);
                    expect[t]++; if (c == 'r') expect[t + 12]++;
                }
            std::map<int, int> got;
            for (const auto& e : readSmf (clip.bytes).ev) if (e[1] == 0x99) { got[e[0]]++; ++checked; }
            swingOk = swingOk && got == expect;
        }
        check (swingOk && checked > 0, "MIDI: swing written as the sequencer plays it (every swung set)", std::to_string (checked) + " notes");
        check (full.name == "RU-8 House Analog 8 bars 124bpm.mid", "MIDI: full clip name follows what plays", full.name);
        // clip length: every factory set x {8 bars, section A, section B} x {GM, pads} ends exactly at bars * 384 (a swung last
        // step's note-off is cut at the bar line, the end of track sits on it); note-ons = note-offs, each off at or after its on,
        // nothing past the end
        auto clipOk = [] (const MidiClip& m, int bars, std::string& why)
        {
            const Smf f = readSmf (m.bytes);
            const int end = bars * 384;
            if (! f.ok || f.endTick != end) { why = "end " + std::to_string (f.endTick) + " != " + std::to_string (end); return false; }
            int ons = 0, offs = 0; std::map<int, std::vector<int>> open;
            for (const auto& e : f.ev)
            {
                if (e[0] > end) { why = "event at " + std::to_string (e[0]); return false; }
                if (e[1] == 0x99) { ++ons; open[e[2]].push_back (e[0]); }
                else if (e[1] == 0x89)
                {
                    ++offs;
                    auto& o = open[e[2]];
                    if (o.empty() || o.front() > e[0]) { why = "off before on, note " + std::to_string (e[2]) + " at " + std::to_string (e[0]); return false; }
                    o.erase (o.begin());
                }
            }
            if (ons != offs || ons != m.notes) { why = std::to_string (ons) + " on / " + std::to_string (offs) + " off"; return false; }
            return true;
        };
        int clips = 0, bad = 0, onsAll = 0; std::string firstBad;
        for (int i = 0; i < kNumSets; ++i)
            for (bool padMap : { false, true })
                for (int v = 0; v < 3; ++v)
                {
                    const auto& set = song.slot[i];
                    const auto m = makeMidiClip (set, kSets[i].id, "Analog", v == 0, v == 2 ? 1 : 0, -1, padMap, set.bpm);
                    std::string why; ++clips; onsAll += m.notes;
                    if (! clipOk (m, v == 0 ? 8 : 1, why)) { if (! bad++) firstBad = m.name + ": " + why; }
                }
        check (bad == 0 && clips == kNumSets * 6, "MIDI: every set x {8 bars, A, B} x {GM, pads} is exactly 1 or 8 bars, ons = offs, none past the end",
               std::to_string (clips) + " clips, " + std::to_string (onsAll) + " notes, " + std::to_string (bad) + " bad" + (bad ? " (" + firstBad + ")" : std::string()));
        // the worst case a user can make: swing 80 % on 8ths, a roll on the last step of every pad: still one bar, nothing past it
        {
            SetPatterns u = song.slot[Song::findSlot ("jazz")]; u.swing = 0.8f; u.swingSub = 8;
            for (int p = 0; p < kNumPads; ++p) { u.steps[0][p][15] = 'r'; u.steps[0][p][14] = 'X'; }
            const auto m = makeMidiClip (u, "User", "Analog", false, 0, -1, true, u.bpm);
            std::string why; const bool ok = clipOk (m, 1, why);
            check (ok && m.notes >= kNumPads * 2, "MIDI: rolls on the last step at 80 % swing still end at 384, roll hits past the end left out",
                   std::to_string (m.notes) + " notes" + (ok ? std::string() : " " + why));
        }
    }
    // 7. MIDI in: GM and pads from C1
    {
        check (noteToPad (36, false) == BD && noteToPad (38, false) == SD && noteToPad (42, false) == CH && noteToPad (46, false) == OH && noteToPad (49, false) == P6,
               "MIDI in: GM 36 kick, 38 snare, 42 closed hat, 46 open hat, 49 crash");
        bool padsOk = true; for (int p = 0; p < kNumPads; ++p) padsOk = padsOk && noteToPad (36 + p, true) == p && padToNote (p, true) == 36 + p;
        check (padsOk && noteToPad (35, true) == -1 && noteToPad (52, true) == -1, "MIDI in: pads map C1..D#2 = pads 1..16");
        bool roundTrip = true; for (int p = 0; p < kNumPads; ++p) roundTrip = roundTrip && noteToPad (padToNote (p, false), false) == p;
        check (roundTrip, "MIDI out notes read back to the same pads (GM)");
    }
    // 8. state round trip: edited steps, a USER slot, kit, set, note map
    {
        Song a; a.slot[0].steps[0][SD][3] = 'X'; a.slot[0].steps[1][P8][15] = 'r'; a.slot[kNumSets + 1].steps[0][BD][0] = 'X';
        a.slot[kNumSets + 1].bpm = 97.5f; a.slot[kNumSets + 1].swing = 0.25f;
        const std::string text = a.save (kNumSets + 1, 12, true);
        Song b; int slot = 0, kit = 0; bool pads = false;
        const bool ok = b.load (text, slot, kit, pads);
        check (ok && slot == kNumSets + 1 && kit == 12 && pads && std::memcmp (a.slot, b.slot, sizeof a.slot) == 0 && b.save (slot, kit, pads) == text,
               "state: steps of every set, USER slots, kit, set and note map survive save / load", std::to_string (text.size()) + " bytes");
        Song c; int s2 = 3, k2 = 4; bool p2 = false;
        check (! c.load ("garbage", s2, k2, p2) && s2 == 3 && k2 == 4, "state: unknown text is refused and changes nothing");
    }
    std::printf ("\n%d of %d checks pass\n", passes, passes + fails);
    return fails ? 1 : 0;
}
