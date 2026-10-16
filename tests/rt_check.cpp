/*  RU-8 real-time checks: the processor driven the way a host drives it.

        rt_check            all checks (exit code 0 only if every check passes)

    - No heap allocation (C++ new) inside processBlock while playing: sets with rolls and fills, MIDI notes on every pad,
      knob moves, kit and set changes made between blocks (counted by a replaced operator new on the audio thread).
    - Host automation written on the audio thread (a kit change, a section change): allocations counted separately.
    - CPU: processBlock at 48 kHz / 512 and 96 kHz / 64, a light set and the heaviest case (Lo-fi Color + dust, a dense set,
      every pad struck by MIDI every 16th), as a share of real time: median, 99.9th percentile and worst call; each call's
      time is its fastest of three identical runs, so OS preemption on a shared machine is not counted as the plug-in's cost.
*/
#include <JuceHeader.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include "../Source/PluginProcessor.h"
#include "../Source/Params.h"

namespace
{
thread_local bool inAudio = false;
std::atomic<long> audioAllocs { 0 };
}
void* operator new (std::size_t n)
{
    if (inAudio) audioAllocs.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (inAudio) audioAllocs.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
int failures = 0, passes = 0;
void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    (ok ? passes : failures)++;
    std::printf ("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what.toRawUTF8(), detail.isEmpty() ? "" : "  ", detail.toRawUTF8());
    std::fflush (stdout);
}

struct Host : juce::AudioPlayHead
{
    double sr = 48000, bpm = 120; int64_t pos = 0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i; const double ppq = (double) pos / sr * bpm / 60.0;
        i.setBpm (bpm); i.setIsPlaying (playing); i.setPpqPosition (ppq); i.setPpqPositionOfLastBarStart (std::floor (ppq / 4) * 4);
        i.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 }); i.setTimeInSamples (pos);
        return i;
    }
};

void setP (RU8AudioProcessor& p, const juce::String& id, float plain)
{
    auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (plain));
}

struct Run { long allocs = 0; double median = 0, p999 = 0, worst = 0; int calls = 0; std::vector<double> times; };

// median / 99.9th / worst of per-call times (ms) as a share of real time
void stats (Run& r, std::vector<double> ms, double rt)
{
    std::sort (ms.begin(), ms.end());
    r.calls = (int) ms.size();
    if (! ms.empty()) { r.median = ms[ms.size() / 2] / rt * 100; r.p999 = ms[(size_t) (ms.size() * 0.999)] / rt * 100; r.worst = ms.back() / rt * 100; }
}

// seconds of playing; between blocks `between` may move knobs; `midiEvery16th`: every pad struck each 16th
Run run (RU8AudioProcessor& p, Host& h, int block, double seconds, bool midiEvery16th, std::function<void (int blockIndex)> between)
{
    Run r; std::vector<double> ms;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    const int n = (int) (seconds * h.sr);
    const double sixteenth = 60.0 / h.bpm / 4 * h.sr;
    int bi = 0;
    for (int pos = 0; pos < n; pos += block, ++bi)
    {
        if (between) between (bi);
        midi.clear();
        if (midiEvery16th)
            for (int k = (int) std::ceil (pos / sixteenth); k * sixteenth < pos + block; ++k)
                for (int pad = 0; pad < 16; ++pad)
                    midi.addEvent (juce::MidiMessage::noteOn (10, 36 + pad, (juce::uint8) 100), juce::jlimit (0, block - 1, (int) (k * sixteenth) - pos));
        buf.clear(); h.pos = pos;
        const long before = audioAllocs.load();
        inAudio = true;
        const auto t0 = std::chrono::high_resolution_clock::now();
        p.processBlock (buf, midi);
        const double dt = std::chrono::duration<double, std::milli> (std::chrono::high_resolution_clock::now() - t0).count();
        inAudio = false;
        r.allocs += audioAllocs.load() - before;
        if (bi > 0) ms.push_back (dt);
    }
    r.times = ms;
    stats (r, ms, block / h.sr * 1000.0);
    return r;
}

std::unique_ptr<RU8AudioProcessor> make (Host& h, int block, std::function<void (RU8AudioProcessor&)> setup)
{
    auto p = std::make_unique<RU8AudioProcessor>();
    p->setPlayHead (&h); p->setRateAndBufferSizeDetails (h.sr, block);
    if (setup) setup (*p);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    p->prepareToPlay (h.sr, block);
    return p;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace ru8;

    // 1. no allocation in processBlock: every set (rolls, fills, swing) with auto, MIDI on every pad, knobs moving, kit and set changes
    {
        Host h; long total = 0; int blocks = 0;
        auto p = make (h, 512, [] (RU8AudioProcessor& q) { setP (q, "auto", 1); setP (q, "human", 0.5f); });
        juce::Random rnd (3);
        auto moves = [&] (int bi)
        {
            if (bi % 7 == 0) setP (*p, "pad" + juce::String (1 + bi % 16) + ".level", -20.0f + (float) (bi % 20));
            if (bi % 11 == 0) setP (*p, "spaceSize", 0.3f + 0.6f * rnd.nextFloat());
            if (bi % 13 == 0) setP (*p, "echoFeedback", 0.7f * rnd.nextFloat());
            if (bi % 17 == 0) setP (*p, "colorTape", rnd.nextFloat());
            if (bi % 19 == 0) setP (*p, "colorCrush", rnd.nextFloat());
            if (bi % 23 == 0) setP (*p, "glue", rnd.nextFloat());
            if (bi % 97 == 0) setP (*p, "kit", (float) ((bi / 97) % kNumKits));
            if (bi % 131 == 0) setP (*p, "set", (float) ((bi / 131) % kNumSets));
            if (bi % 29 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);   // the message thread applies kit / set values
        };
        const Run r = run (*p, h, 512, 120.0, true, moves);
        total += r.allocs; blocks += r.calls;
        check (total == 0, "no heap allocation inside processBlock (2 min: every kit and set, MIDI on all pads every 16th, knob moves)",
               juce::String (total) + " allocations over " + juce::String (blocks) + " blocks");
    }
    // 2. host automation on the audio thread: a host writes parameter values with setValue (VST3 / AU wrappers). The
    //    parameter objects are looked up before the measurement, as a host holds them.
    {
        Host h; auto p = make (h, 512, nullptr);
        std::vector<juce::AudioProcessorParameter*> ps;
        for (const char* id : { "kit", "set", "section", "spaceType", "pad3.tune", "volume", "auto", "pad7.mute" }) ps.push_back (p->apvts.getParameter (id));
        const long before = audioAllocs.load(); inAudio = true;
        for (int i = 0; i < 400; ++i) { auto* q = ps[(size_t) (i % (int) ps.size())]; q->setValue ((i / 8) % 2 ? 0.85f : 0.15f); }
        inAudio = false;
        const long a = audioAllocs.load() - before;
        check (a == 0, "host automation on the audio thread (setValue: kit, set, section, SPACE type, knobs, switches) allocates nothing",
               juce::String (a) + " allocations over 400 writes");
        // the kit / set / type loads they cause happen on the message thread (30 Hz timer), not here
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    }
    // 3. CPU
    struct Case { const char* name; double sr; int block; int kit; const char* set; bool midi; };
    const Case cases[] = {
        { "Analog, House, 48 kHz / 512", 48000, 512, 0, "house", false },
        { "Lo-fi (Color, dust), Breakbeat + every pad by MIDI each 16th, 48 kHz / 512", 48000, 512, 12, "breaks", true },
        { "Lo-fi (Color, dust), Breakbeat + every pad by MIDI each 16th, 96 kHz / 64", 96000, 64, 12, "breaks", true },
    };
    // The render is deterministic (same input, same work per call), so each case runs three times on a fresh processor and
    // the judged time of a call is its fastest of the three: a call the OS preempted once is not counted as the plug-in's
    // cost. The single first run is printed too.
    for (const auto& c : cases)
    {
        Run first; std::vector<double> best;
        for (int rep = 0; rep < 3; ++rep)
        {
            Host h; h.sr = c.sr; h.bpm = 128;
            auto p = make (h, c.block, [&] (RU8AudioProcessor& q) { setP (q, "kit", (float) c.kit); setP (q, "set", (float) Song::findSlot (c.set)); setP (q, "auto", 1); });
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            Run r = run (*p, h, c.block, 20.0, c.midi, nullptr);
            if (rep == 0) { first = r; best = r.times; }
            else for (size_t i = 0; i < best.size() && i < r.times.size(); ++i) best[i] = std::min (best[i], r.times[i]);
        }
        Run r; stats (r, best, c.block / c.sr * 1000.0);
        check (r.p999 < 50.0, juce::String ("CPU ") + c.name,
               juce::String::formatted ("fastest of 3 runs per call: median %.2f %%, 99.9th %.2f %%, worst %.2f %% of real time (%d calls); "
                                        "first run alone: median %.2f %%, 99.9th %.2f %%, worst %.2f %%",
                                        r.median, r.p999, r.worst, r.calls, first.median, first.p999, first.worst));
    }
    std::printf ("\nrt_check: %d of %d checks pass\n", passes, passes + failures);
    return failures ? 1 : 0;
}
