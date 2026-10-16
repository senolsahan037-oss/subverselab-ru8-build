// host_check: loads the RU-8 bundles the way a DAW does (VST3 and AU), plays MIDI notes and the sequencer with a host
// transport, turns knobs, saves and restores. It tests the bundle a buyer gets; plugin_check tests the processor built from
// Source/. Read-only: nothing is written to ~/Library/Audio/Plug-Ins.
//
//   host_check [path to .vst3 or .component ...]    (default: the two bundles installed in ~/Library/Audio/Plug-Ins)
#include <JuceHeader.h>
#include <cstdio>

static int fails = 0, passes = 0;
static void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    (ok ? passes : fails)++;
    std::printf ("[%s] %s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8(), detail.toRawUTF8());
}

struct Transport : juce::AudioPlayHead
{
    double sr = 48000, bpm = 124; int64_t pos = 0; bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i; const double ppq = (double) pos / sr * bpm / 60.0;
        i.setBpm (bpm); i.setIsPlaying (playing); i.setPpqPosition (ppq); i.setPpqPositionOfLastBarStart (std::floor (ppq / 4) * 4);
        i.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 }); i.setTimeInSamples (pos);
        return i;
    }
};

static juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* q : p.getParameters()) if (q->getName (64).equalsIgnoreCase (name)) return q;
    return nullptr;
}
static bool setNorm (juce::AudioPluginInstance& p, const juce::String& name, float v)
{
    auto* q = findParam (p, name); if (q == nullptr) return false;
    q->beginChangeGesture(); q->setValueNotifyingHost (v); q->endChangeGesture(); return true;
}
static bool setText (juce::AudioPluginInstance& p, const juce::String& name, const juce::String& text)
{
    auto* q = findParam (p, name); if (q == nullptr) return false;
    q->setValueNotifyingHost (q->getValueForText (text)); return true;
}

// seconds of output (RMS dBFS of the whole render) with MIDI notes at given times and the transport playing or not
static double render (juce::AudioPluginInstance& p, Transport& t, double seconds, const std::vector<std::pair<double, int>>& notes, double* peakDb = nullptr,
                      std::vector<float>* keep = nullptr)
{
    const int block = 512, n = (int) (seconds * t.sr);
    juce::AudioBuffer<float> buf (juce::jmax (2, p.getTotalNumOutputChannels()), block);
    double acc = 0; long cnt = 0; float pk = 0;
    for (int pos = 0; pos < n; pos += block)
    {
        buf.clear(); juce::MidiBuffer midi;
        for (const auto& [time, note] : notes)
        {
            const int at = (int) (time * t.sr);
            if (at >= pos && at < pos + block) midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 120), at - pos);
        }
        t.pos = pos; p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) { const float v = buf.getSample (c, i); acc += v * v; ++cnt; pk = juce::jmax (pk, std::abs (v)); if (keep && c == 0) keep->push_back (v); }
    }
    if (peakDb) *peakDb = pk > 0 ? 20 * std::log10 (pk) : -999;
    return 10 * std::log10 (juce::jmax (1e-20, acc / (double) juce::jmax (1L, cnt)));
}

static void testOne (juce::AudioPluginFormatManager& fm, const juce::File& file)
{
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType (file.getFullPathName())) f->findAllTypesForFile (types, file.getFullPathName());
    if (types.isEmpty()) { check (false, "load " + file.getFullPathName(), "no plug-in found"); return; }
    for (auto* d : types)
    {
        juce::String err;
        auto inst = fm.createPluginInstance (*d, 48000.0, 512, err);
        const juce::String tag = d->pluginFormatName + ": ";
        check (inst != nullptr, tag + "load " + d->name + " " + d->version, err);
        if (inst == nullptr) continue;
        auto& p = *inst;
        check (d->isInstrument && p.acceptsMidi() && p.getTotalNumInputChannels() == 0 && p.getTotalNumOutputChannels() == 2,
               tag + "an instrument with MIDI in and a stereo out", juce::String (p.getTotalNumInputChannels()) + " in / " + juce::String (p.getTotalNumOutputChannels()) + " out");
        Transport t; p.setPlayHead (&t);
        p.prepareToPlay (48000.0, 512);
        // JUCE's hosted AU does not pass reset() on; a host's AudioUnitReset reaches the plug-in through release + prepare here
        auto hardReset = [&] { p.reset(); if (d->pluginFormatName == "AudioUnit") { p.releaseResources(); p.prepareToPlay (48000.0, 512); } };
        check (p.getLatencySamples() == 488, tag + "reports its latency", juce::String (p.getLatencySamples()) + " samples");
        int own = 0; for (auto* q : p.getParameters()) if (! q->getName (64).startsWithIgnoreCase ("MIDI CC") && q->isAutomatable()) ++own;
        // the VST3 wrapper adds MIDI controller parameters (JUCE's CC emulation) and a bypass; RU-8's own are 150
        int ru8 = 0; for (auto* q : p.getParameters()) { const auto n = q->getName (64); if (n == "Kit" || n == "Set" || n.startsWith ("Pad ") || n.startsWith ("Space") || n.startsWith ("Echo")) ++ru8; }
        check (findParam (p, "Kit") && findParam (p, "Pad 16 solo") && findParam (p, "Tape") && ru8 == 2 + 128 + 5 + 4, tag + "RU-8's parameters are all there",
               juce::String (p.getParameters().size()) + " parameters in the host (" + juce::String (ru8) + " of kit / set / pads / space / echo)");

        // silence with nothing played
        const double quiet = render (p, t, 1.0, {});
        check (quiet < -120, tag + "silent without notes and with the transport stopped", juce::String (quiet, 1) + " dBFS");
        // every GM pad note sounds
        static const int gm[16] = { 36, 38, 39, 37, 42, 46, 45, 50, 64, 63, 70, 56, 75, 49, 51, 54 };
        int sounding = 0; juce::String weak;
        for (int note : gm)
        {
            hardReset(); double pk = 0; render (p, t, 0.7, { { 0.05, note } }, &pk);
            if (pk > -40) ++sounding; else weak << note << " (" << juce::String (pk, 1) << ") ";
        }
        check (sounding == 16, tag + "every pad plays from its GM note", juce::String (sounding) + " of 16 above -40 dBFS " + weak);
        // knobs act
        auto defaults = [&] { for (auto* q : p.getParameters()) if (! q->getName (64).startsWithIgnoreCase ("MIDI CC")) q->setValueNotifyingHost (q->getDefaultValue()); };
        auto sweep = [&] (const juce::String& what, const juce::String& name, float a, float b, std::vector<std::pair<double, int>> notes, double want)
        {
            defaults();
            setNorm (p, name, a); hardReset(); const double ya = render (p, t, 1.0, notes);
            setNorm (p, name, b); hardReset(); const double yb = render (p, t, 1.0, notes);
            check (std::abs (yb - ya) >= want, tag + what, juce::String::formatted ("%.2f -> %.2f dBFS (%+.2f dB, want >= %.1f)", ya, yb, yb - ya, want));
        };
        sweep ("VOLUME moves the output", "Volume", 0.2f, 0.95f, { { 0.05, 36 }, { 0.5, 38 } }, 6.0);
        sweep ("pad 1 level moves the kick", "Pad 1 level", 0.2f, 0.95f, { { 0.05, 36 } }, 6.0);
        sweep ("pad 2 decay moves the snare", "Pad 2 decay", 0.0f, 1.0f, { { 0.05, 38 } }, 1.0);
        // SPACE return: the clap's room after the clap itself (0.35 .. 0.75 s)
        {
            defaults();
            // ECHO off so only the room is left; the clap ends ~0.3 s after its hit, the room rings on
            auto tail = [&] (float v) { setNorm (p, "Echo return", 0.0f); setNorm (p, "Space return", v); render (p, t, 0.05, {}); hardReset();
                                        render (p, t, 0.35, { { 0.01, 39 } }); return render (p, t, 0.4, {}); };
            const double a = tail (0.0f), b = tail (1.0f);
            check (b - a >= 6.0, tag + "SPACE return moves the clap's room tail", juce::String::formatted ("%.2f -> %.2f dBFS (%+.2f dB)", a, b, b - a));
        }
        defaults(); setNorm (p, "Pad 1 mute", 1.0f); { hardReset(); double pk = 0; render (p, t, 0.7, { { 0.05, 36 } }, &pk); check (pk < -100, tag + "pad 1 mute silences the kick", juce::String (pk, 1) + " dBFS"); } setNorm (p, "Pad 1 mute", 0.0f);
        // kit changes the sound
        { defaults(); setNorm (p, "Kit", 0.0f); render (p, t, 0.05, {}); hardReset(); std::vector<float> A, B; const double ra = render (p, t, 0.8, { { 0.05, 36 } }, nullptr, &A);
          setNorm (p, "Kit", 4.0f / 13.0f); render (p, t, 0.05, {}); hardReset(); const double rb = render (p, t, 0.8, { { 0.05, 36 } }, nullptr, &B);
          double diff = 0; for (size_t i = 0; i < std::min (A.size(), B.size()); ++i) diff = juce::jmax (diff, (double) std::abs (A[i] - B[i]));
          check (diff > 0.05, tag + "another kit gives another kick", juce::String::formatted ("Analog %.2f, Deep %.2f dBFS RMS, largest sample difference %.3f", ra, rb, diff)); setNorm (p, "Kit", 0.0f); }
        // the sequencer follows the host transport
        {
            defaults(); t.playing = false; hardReset(); const double stopped = render (p, t, 2.0, {});
            t.playing = true; hardReset(); double pk = 0; const double playing = render (p, t, 2.0, {}, &pk);
            check (stopped < -100 && playing > -40, tag + "the sequencer plays with the host transport, not without",
                   juce::String::formatted ("stopped %.1f, playing %.1f dBFS RMS (peak %.1f)", stopped, playing, pk));
            t.playing = false;
        }
        // state through the host
        {
            setNorm (p, "Pad 3 level", 0.31f); setNorm (p, "Echo feedback", 0.77f);
            render (p, t, 0.05, {});   // a VST3 host hands parameter changes to the plug-in with the next process call
            juce::MemoryBlock mb; p.getStateInformation (mb);
            auto q = fm.createPluginInstance (*d, 48000.0, 512, err);
            bool same = q != nullptr;
            if (q) { q->setStateInformation (mb.getData(), (int) mb.getSize());
                     // compared as the host shows them (text), since a value written between grid steps comes back on the grid
                     same = findParam (*q, "Pad 3 level")->getCurrentValueAsText() == findParam (p, "Pad 3 level")->getCurrentValueAsText()
                         && findParam (*q, "Echo feedback")->getCurrentValueAsText() == findParam (p, "Echo feedback")->getCurrentValueAsText();
                     if (! same) std::printf ("    %s / %s vs %s / %s\n", findParam (p, "Pad 3 level")->getCurrentValueAsText().toRawUTF8(), findParam (p, "Echo feedback")->getCurrentValueAsText().toRawUTF8(),
                                              findParam (*q, "Pad 3 level")->getCurrentValueAsText().toRawUTF8(), findParam (*q, "Echo feedback")->getCurrentValueAsText().toRawUTF8()); }
            check (same, tag + "state saved and restored by a new instance", juce::String ((int) mb.getSize()) + " bytes");
        }
        // the editor opens and closes
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorIfNeeded());
            check (ed != nullptr && ed->getWidth() > 500 && ed->getHeight() > 300, tag + "the editor opens", ed ? juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()) : juce::String ("none"));
            if (ed) p.editorBeingDeleted (ed.get());
        }
        p.releaseResources();
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);
    juce::Array<juce::File> files;
    for (int i = 1; i < argc; ++i) files.add (juce::File (argv[i]));
    if (files.isEmpty())
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        files.add (home.getChildFile ("Library/Audio/Plug-Ins/VST3/RU-8.vst3"));
        files.add (home.getChildFile ("Library/Audio/Plug-Ins/Components/RU-8.component"));
    }
    for (auto& f : files) testOne (fm, f);
    std::printf ("\nhost_check: %d of %d checks pass\n", passes, passes + fails);
    return fails ? 1 : 0;
}
