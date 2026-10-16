// RU-8 sequencer, arrangement, MIDI in / out and song state: plain C++17, no JUCE (SPEC: Sequencer in the arrangement).
//
// The pad sequencer is driven by a transport position in quarter notes: the host's PPQ in the plug-in, a counter of its own
// standalone, the bounce origin in the raw tool. Every step's time is computed from its absolute position, so hits are the
// same whatever the block size, and a host that starts at bar 23 hears bar 23.
// What plays on a bar is decided at its bar line from, in this order: AUTO (A ×3, fill, B ×3, fill counted from the host's
// bar relative to an automatable start bar), else the latest request before the bar line: the Section parameter
// (Off / A / B / Fill) or a trigger note (C-1 = A, D-1 = B, E-1 = fill, F-1 = off). A fill is one bar of the current
// section with the set's fill template on steps 9–16. Piano-roll notes always play the pads on top.
#pragma once
#include "RU8Engine.h"
#include <cstdio>
#include <string>
#include <vector>

namespace ru8 {

enum class Section { off = 0, A = 1, B = 2, fill = 3 };   // the order of the automatable Section parameter
constexpr int kTriggerA = 12, kTriggerB = 14, kTriggerFill = 16, kTriggerOff = 17;   // C-1, D-1, E-1, F-1 (C4 = 60)
constexpr int kNumUser = 4, kNumSlots = kNumSets + kNumUser;                         // 17 factory sets + USER 1–4

// The steps of every set: factory sets keep the user's edits until reloaded; USER 1–4 start empty.
struct SetPatterns
{
    char steps[2][kNumPads][16];
    float bpm = 120, swing = 0; int swingSub = 16, fill = 0, echoTime = 2;
};

class Song
{
public:
    Song() { for (int i = 0; i < kNumSlots; ++i) reload (i); }
    SetPatterns slot[kNumSlots];
    // a factory set back to its factory steps (USER slots: emptied)
    void reload (int i)
    {
        SetPatterns& p = slot[i];
        if (i < kNumSets)
        {
            const SetDef& d = kSets[i];
            for (int pad = 0; pad < kNumPads; ++pad)
                for (int s = 0; s < 16; ++s) { p.steps[0][pad][s] = d.A[pad][s]; p.steps[1][pad][s] = d.B[pad][s]; }
            p.bpm = d.bpm; p.swing = d.swing; p.swingSub = d.swingSub; p.fill = d.fill; p.echoTime = d.echoTime;
        }
        else
        {
            for (auto& sec : p.steps) for (auto& pad : sec) for (auto& c : pad) c = '.';
            p.bpm = 120; p.swing = 0; p.swingSub = 16; p.fill = 2; p.echoTime = 2;   // as the prototype's USER slots (fill 'electro')
        }
    }
    static const char* slotId (int i) { static const char* user[kNumUser] = { "user1", "user2", "user3", "user4" }; return i < kNumSets ? kSets[i].id : user[i - kNumSets]; }
    static int findSlot (const std::string& id) { for (int i = 0; i < kNumSlots; ++i) if (id == slotId (i)) return i; return -1; }

    // State as text, one key per line (kept by the plug-in with its parameters). Unknown keys are ignored when read back.
    std::string save (int currentSlot, int kit, bool padNoteMap) const
    {
        std::string s = "ru8-song 1\n";
        s += "kit " + std::string (kKits[kit].id) + "\nset " + slotId (currentSlot) + "\nnotemap " + (padNoteMap ? "pads" : "gm") + "\n";
        char line[96];
        for (int i = 0; i < kNumSlots; ++i)
        {
            const SetPatterns& p = slot[i];
            std::snprintf (line, sizeof line, "slot %s %.3f %.3f %d %d %d\n", slotId (i), p.bpm, p.swing, p.swingSub, p.fill, p.echoTime);
            s += line;
            for (int sec = 0; sec < 2; ++sec)
                for (int pad = 0; pad < kNumPads; ++pad)
                    s += std::string ("steps ") + slotId (i) + " " + (sec ? "B " : "A ") + kPadIds[pad] + " " + std::string (p.steps[sec][pad], 16) + "\n";
        }
        return s;
    }
    bool load (const std::string& text, int& currentSlot, int& kit, bool& padNoteMap)
    {
        if (text.rfind ("ru8-song 1\n", 0) != 0) return false;
        size_t pos = 0;
        while (pos < text.size())
        {
            const size_t end = text.find ('\n', pos);
            const std::string ln = text.substr (pos, end == std::string::npos ? std::string::npos : end - pos);
            pos = end == std::string::npos ? text.size() : end + 1;
            char a[32] = {}, b[32] = {}, c[8] = {}, d[32] = {}, e[32] = {};
            if (std::sscanf (ln.c_str(), "kit %31s", a) == 1) { for (int k = 0; k < kNumKits; ++k) if (std::string (a) == kKits[k].id) kit = k; }
            else if (std::sscanf (ln.c_str(), "set %31s", a) == 1) { const int i = findSlot (a); if (i >= 0) currentSlot = i; }
            else if (std::sscanf (ln.c_str(), "notemap %31s", a) == 1) padNoteMap = std::string (a) == "pads";
            else if (ln.rfind ("slot ", 0) == 0)
            {
                float bpm, swing; int sub, fill, echo;
                if (std::sscanf (ln.c_str(), "slot %31s %f %f %d %d %d", a, &bpm, &swing, &sub, &fill, &echo) == 6)
                {
                    const int i = findSlot (a);
                    if (i >= 0) { auto& p = slot[i]; p.bpm = bpm; p.swing = swing; p.swingSub = sub == 8 ? 8 : 16; p.fill = std::clamp (fill, 0, (int) (sizeof kFills / sizeof kFills[0]) - 1); p.echoTime = std::clamp (echo, 0, 3); }
                }
            }
            else if (std::sscanf (ln.c_str(), "steps %31s %7s %31s %31s", b, c, d, e) == 4)
            {
                const int i = findSlot (b); int pad = -1;
                for (int q = 0; q < kNumPads; ++q) if (std::string (d) == kPadIds[q]) pad = q;
                if (i >= 0 && pad >= 0 && std::string (e).size() == 16)
                    for (int s = 0; s < 16; ++s)
                    {
                        const char ch = e[s];
                        slot[i].steps[c[0] == 'B' ? 1 : 0][pad][s] = (ch == 'X' || ch == 'x' || ch == 'g' || ch == 'r') ? ch : '.';
                    }
            }
        }
        return true;
    }
};

// Turns the transport into hits for the engine.
class Sequencer
{
public:
    const SetPatterns* set = nullptr;
    float human = 0;            // 0..1
    bool autoArrange = false;
    double autoStartBar = 0;    // auto counts from this bar
    bool fixedOrigin = false;   // bounce: the origin is given and not re-derived from the host each block

    // Section requests: the parameter (when it changes; Fill held on the lane means every bar is a fill until it moves) and
    // trigger notes (at their sample; E-1 is one bar of fill).
    void requestSection (Section s, double time) { push ({ time, s, true }); }
    void noteRequest (int note, double time)
    {
        if (note == kTriggerA) push ({ time, Section::A, false });
        else if (note == kTriggerB) push ({ time, Section::B, false });
        else if (note == kTriggerFill) push ({ time, Section::fill, false });
        else if (note == kTriggerOff) push ({ time, Section::off, false });
    }
    static bool isTriggerNote (int n) { return n == kTriggerA || n == kTriggerB || n == kTriggerFill || n == kTriggerOff; }

    // What the sequencer is set to now (the section the panel shows; Off when it is not playing).
    Section current() const { return state; }
    void setInitial (Section s) { state = s == Section::fill ? Section::A : s; fillHold = s == Section::fill; fillPending = false; cache[0].bar = cache[1].bar = INT64_MIN; reqCount = 0; }
    void resetCache() { cache[0].bar = cache[1].bar = INT64_MIN; }

    // section and fill of bar `bar`, whose line falls at absolute sample `barTime`
    void barPlan (int64_t bar, double barTime, int& sec, bool& fill, bool& on)
    {
        for (auto& c : cache) if (c.bar == bar) { sec = c.sec; fill = c.fill; on = c.on; return; }
        // apply the requests made before this bar line, in time order
        int keep = 0;
        for (int i = 0; i < reqCount; ++i)
        {
            if (reqs[i].time < barTime)
            {
                if (reqs[i].s == Section::fill) { if (reqs[i].hold) fillHold = true; else fillPending = true; }
                else { state = reqs[i].s; fillPending = false; if (reqs[i].hold) fillHold = false; }
            }
            else reqs[keep++] = reqs[i];
        }
        reqCount = keep;
        if (autoArrange)
        {   // AUTO plays the arrangement; Off (parameter or F-1) still stops the sequencer
            const int64_t p = (((bar - (int64_t) std::floor (autoStartBar)) % 8) + 8) % 8;
            sec = p < 4 ? 0 : 1; fill = (p == 3 || p == 7); on = state != Section::off; fillPending = false;
        }
        else
        {
            on = state != Section::off;
            sec = state == Section::B ? 1 : 0;
            fill = on && (fillPending || fillHold); fillPending = false;
        }
        Cached& c = cache[next]; next ^= 1;
        c = { bar, sec, fill, on };
    }

    char stepChar (int sec, bool fill, int pad, int step) const
    {
        if (fill && step >= 8)
        {
            const char* f = kFills[set->fill].steps[pad];
            if (f) return f[step - 8];
        }
        return set->steps[sec][pad][step];
    }
    // the swung position of 16th number k, in quarter notes (Tone's transport: 192 ticks per quarter)
    double stepPpq (int64_t k) const
    {
        double ppq = (double) k / 4.0;
        if (set->swing > 0)
        {
            const int64_t ticks = k * 48, swingTicks = set->swingSub == 8 ? 96 : 48;
            const int64_t m = ((ticks % (2 * swingTicks)) + 2 * swingTicks) % (2 * swingTicks);
            if (((ticks % 192) + 192) % 192 != 0 && m != 0)
            {
                const double progress = (double) m / (2 * swingTicks);
                ppq += (2.0 * swingTicks / 3.0) / 192.0 * std::sin (progress * kPi) * set->swing;
            }
        }
        return ppq;
    }

    // Queues every hit of [blockStart, blockStart + numSamples) (absolute samples). `origin`: the absolute sample of quarter
    // note 0 (a host: blockStart - ppq · samples per quarter). Steps play from `startPpq` (a bounce starts at 0; a host may
    // play negative positions) up to `stopPpq` (excluded; the end of a bounce).
    // Dust crackles are queued while steps play. 4/4 only (16 steps per bar); a host's bar start comes in as ppqBar.
    // Sink: the Engine, or anything with `float dust` and hitAt (pad, vel, time) (the checks record the hits).
    template <class Sink>
    void process (Sink& e, int64_t blockStart, double origin, double tempo, int numSamples, double sr, double stopPpq = 1e18,
                  double barOffsetPpq = 0, double startPpq = -1e18)
    {
        if (! set) return;
        const double spq = 60.0 / tempo * sr;
        const double ppqStart = ((double) blockStart - origin) / spq;
        const double ppqEnd = ppqStart + numSamples / spq;
        const double b0 = (double) blockStart, b1 = b0 + numSamples;
        const int64_t k0 = (int64_t) std::floor (ppqStart * 4) - 2, k1 = (int64_t) std::ceil (ppqEnd * 4) + 1;
        const double sixteenth = spq / 4;
        for (int64_t k = k0; k <= k1; ++k)
        {
            if ((double) k / 4.0 >= stopPpq || (double) k / 4.0 < startPpq) continue;
            // bars are counted from the host's bar line: barOffsetPpq is where a bar starts (ppq of the last bar line mod 4)
            const int64_t kb = k - (int64_t) std::llround (barOffsetPpq * 4);
            const int64_t bar = kb >= 0 ? kb / 16 : (kb - 15) / 16;
            const int step = (int) (kb - bar * 16);
            int sec; bool fill, on;
            barPlan (bar, origin + (double) (bar * 16 + std::llround (barOffsetPpq * 4)) / 4.0 * spq, sec, fill, on);
            if (! on) continue;
            double tStep = origin + stepPpq (k) * spq;
            // a step that falls on a sample (within rounding) is put exactly on it, so a transport that starts on a bar
            // line plays its downbeat in the first block instead of losing it to the block before (1e-10 samples early)
            if (std::fabs (tStep - std::round (tStep)) < 1e-6) tStep = std::round (tStep);
            for (int p = 0; p < kNumPads; ++p)
            {
                const char c = stepChar (sec, fill, p, step);
                if (c == '.' || c == 0) continue;
                float vel = c == 'X' ? kVel.X : c == 'x' ? kVel.x : c == 'g' ? kVel.g : c == 'r' ? kVel.r : kVel.x;
                double t = tStep;
                if (human > 0)
                {
                    Rng rng; rng.seed ((uint64_t) (bar * 1315423911ll) ^ ((uint64_t) step * 2654435761ull) ^ (uint64_t) (p * 97 + 1));
                    const float u1 = 0.5f * (rng.uni() + 1), u2 = 0.5f * (rng.uni() + 1);
                    const double jitter = (u1 - 0.5) * human * 0.010;
                    vel = std::clamp (vel * (1 - u2 * human * 0.22f), 0.05f, 1.0f);
                    t += std::max (-0.008, jitter) * sr;
                }
                if (t >= b0 && t < b1) e.hitAt (p, vel, t);
                if (c == 'r') { const double t2 = t + sixteenth / 2; if (t2 >= b0 && t2 < b1) e.hitAt (p, vel * 0.85f, t2); }
            }
            if (e.dust > 0.001f)
            {
                Rng rng; rng.seed ((uint64_t) k * 0x9E3779B97F4A7C15ull + 12345);
                const float u = 0.5f * (rng.uni() + 1);
                if (u < e.dust * 0.45f)
                {
                    const double tc = tStep + 0.5 * (rng.uni() + 1) * 0.05 * sr;
                    const float v = 0.25f + 0.5f * (rng.uni() + 1) * 0.75f * e.dust;
                    if (tc >= b0 && tc < b1) e.hitAt (-1, v, tc);
                }
            }
        }
    }

private:
    struct Req { double time; Section s; bool hold; };
    struct Cached { int64_t bar; int sec; bool fill, on; };
    void push (Req r)
    {
        if (reqCount == kMaxReq) { for (int i = 1; i < kMaxReq; ++i) reqs[i - 1] = reqs[i]; --reqCount; }
        int i = reqCount++;
        while (i > 0 && reqs[i - 1].time > r.time) { reqs[i] = reqs[i - 1]; --i; }
        reqs[i] = r;
    }
    static constexpr int kMaxReq = 64;
    Req reqs[kMaxReq] {}; int reqCount = 0;
    Section state = Section::A; bool fillPending = false, fillHold = false;
    Cached cache[2] { { INT64_MIN, 0, false, false }, { INT64_MIN, 0, false, false } }; int next = 0;
};

// Incoming MIDI note → pad: GM drums or pads from C1 (36 = pad 1). -1: none.
inline int noteToPad (int note, bool padMap)
{
    if (note < 0 || note > 127) return -1;
    if (padMap) { const int i = note - 36; return i >= 0 && i < kNumPads ? i : -1; }
    return kGmIn[note];
}
inline int padToNote (int pad, bool padMap) { return padMap ? 36 + pad : kGmOut[pad]; }

// A Standard MIDI File (type 0, 96 PPQ, channel 10) of what plays: one bar of a section, or the 8-bar arrangement
// (A, A, A, fill A, B, B, B, fill B); every pad or one. Swing is written with the sequencer's own formula, so the clip and
// the playback agree. Notes 20 ticks long; a roll is two notes 12 ticks apart. The clip is exactly 1 or 8 bars long.
struct MidiClip
{
    std::vector<uint8_t> bytes;
    std::string name;
    int notes = 0;                                 // note-ons in the clip (an empty clip does not arm the clip lock)
};

inline MidiClip makeMidiClip (const SetPatterns& set, const char* setName, const char* kitName, bool song, int barSection,
                              int onlyPad, bool padMap, double bpm, const char* padName = nullptr)
{
    struct Ev { int t; int on; int note; int vel; };
    std::vector<Ev> ev;
    Sequencer q; q.set = &set;
    const int bars = song ? 8 : 1, end = bars * 384;
    // the clip ends exactly at its last bar line: a swung last step's note-off is cut there; a note that would start at or
    // after the end (only a roll's second hit on step 16 at a high user swing; no factory set has one) is left out
    auto note = [&ev, end] (int on, int off, int n, int vel) { if (on >= end) return; ev.push_back ({ on, 1, n, vel }); ev.push_back ({ std::min (off, end), 0, n, 0 }); };
    for (int b = 0; b < bars; ++b)
    {
        const int sec = song ? (b < 4 ? 0 : 1) : barSection;
        const bool fill = song && (b == 3 || b == 7);
        for (int p = 0; p < kNumPads; ++p)
        {
            if (onlyPad >= 0 && p != onlyPad) continue;
            for (int i = 0; i < 16; ++i)
            {
                const char c = q.stepChar (sec, fill, p, i);
                if (c == '.' || c == 0) continue;
                const int t = b * 384 + i * 24 + (int) std::lround ((q.stepPpq (i) - i / 4.0) * 96);
                const float v = c == 'X' ? kVel.X : c == 'x' ? kVel.x : c == 'g' ? kVel.g : c == 'r' ? kVel.r : kVel.x;
                const int vel = std::clamp ((int) std::lround (v * 127), 1, 127), n = padToNote (p, padMap);
                if (c == 'r') { note (t, t + 10, n, vel); note (t + 12, t + 22, n, (int) std::lround (vel * 0.85)); }
                else note (t, t + 20, n, vel);
            }
        }
    }
    std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.t != b.t ? a.t < b.t : a.on < b.on; });
    std::string label = std::string ("RU-8 ") + setName + " " + kitName + (padName ? std::string (" ") + padName : std::string())
                      + (song ? " 8 bars" : std::string (" Sec ") + (barSection ? "B" : "A")) + " " + std::to_string ((int) std::lround (bpm)) + "bpm";
    std::vector<uint8_t> trk;
    auto vlq = [&trk] (uint32_t n) { uint8_t buf[5]; int k = 0; buf[k++] = n & 0x7f; while (n >>= 7) buf[k++] = (uint8_t) ((n & 0x7f) | 0x80); while (k) trk.push_back (buf[--k]); };
    const uint32_t tempo = (uint32_t) std::lround (60000000.0 / bpm);
    trk.insert (trk.end(), { 0, 0xFF, 0x51, 3, (uint8_t) (tempo >> 16), (uint8_t) (tempo >> 8), (uint8_t) tempo });
    trk.insert (trk.end(), { 0, 0xFF, 0x58, 4, 4, 2, 24, 8 });
    trk.insert (trk.end(), { 0, 0xFF, 0x03 }); vlq ((uint32_t) label.size()); trk.insert (trk.end(), label.begin(), label.end());
    int last = 0;
    for (const auto& e : ev) { vlq ((uint32_t) (e.t - last)); trk.push_back (e.on ? 0x99 : 0x89); trk.push_back ((uint8_t) e.note); trk.push_back ((uint8_t) e.vel); last = e.t; }
    vlq ((uint32_t) (end - last)); trk.insert (trk.end(), { 0xFF, 0x2F, 0 });
    MidiClip clip;
    for (const auto& e : ev) clip.notes += e.on;
    const uint32_t L = (uint32_t) trk.size();
    clip.bytes = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k',
                   (uint8_t) (L >> 24), (uint8_t) (L >> 16), (uint8_t) (L >> 8), (uint8_t) L };
    clip.bytes.insert (clip.bytes.end(), trk.begin(), trk.end());
    for (char ch : label) clip.name += (std::isalnum ((unsigned char) ch) || ch == ' ' || ch == '.' || ch == '-') ? ch : ' ';
    clip.name += ".mid";
    return clip;
}

} // namespace ru8
