// RU-8 parameters (SPEC: Controls). Every control is automatable; the steps are state, not parameters.
#pragma once
#include <JuceHeader.h>
#include "RU8Sequencer.h"

namespace ru8::params
{
// pad N (1..16) follows the pad order of the engine: kick, snare, clap, rim, closed hat, open hat, low tom, high tom, 9..16
inline juce::String padId (int pad, const char* what) { return "pad" + juce::String (pad + 1) + "." + what; }

inline const juce::StringArray& kitNames()
{
    static const juce::StringArray a = [] { juce::StringArray s; for (const auto& k : kKits) s.add (k.name); return s; }();
    return a;
}
inline const juce::StringArray& setNames()
{
    static const juce::StringArray a = [] { juce::StringArray s; for (const auto& d : kSets) s.add (d.name); s.addArray ({ "User 1", "User 2", "User 3", "User 4" }); return s; }();
    return a;
}

inline juce::String fmtDb (float v) { return v <= -39.95f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e dB")) : (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
inline juce::String fmtPct (float v) { return juce::String (juce::roundToInt (v * 100)) + " %"; }
inline juce::String fmtHz (float v) { return v >= 1000 ? juce::String (v / 1000, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
inline juce::String fmtPan (float v) { return std::abs (v) < 0.02f ? "C" : juce::String (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100)); }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using F = juce::AudioParameterFloat; using C = juce::AudioParameterChoice; using B = juce::AudioParameterBool;
    using I = juce::AudioParameterInt; using A = juce::AudioParameterFloatAttributes; using R = juce::NormalisableRange<float>;
    using P = juce::ParameterID;
    auto fa = [] (std::function<juce::String (float)> f, const char* unit = "") {
        juce::ignoreUnused (unit);
        return A().withStringFromValueFunction ([f] (float v, int) { return f (v); });
    };
    const KitDef& k0 = kKits[0];
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<C> (P { "kit", 1 }, "Kit", kitNames(), 0));
    p.push_back (std::make_unique<C> (P { "set", 1 }, "Set", setNames(), 0));
    p.push_back (std::make_unique<C> (P { "section", 1 }, "Section", juce::StringArray { "Off", "A", "B", "Fill" }, 1));
    p.push_back (std::make_unique<B> (P { "auto", 1 }, "Auto", false));
    p.push_back (std::make_unique<I> (P { "autoStart", 1 }, "Auto start bar", 1, 999, 1));
    // SET / MIDI: what plays RU-8 with the host. SET: its own set follows the host's transport. MIDI: only incoming notes play
    // (a dragged clip switches to it when it plays). State, not an automation lane.
    p.push_back (std::make_unique<C> (P { "source", 1 }, "Source", juce::StringArray { "Set", "MIDI" }, 0, juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    p.push_back (std::make_unique<F> (P { "tempo", 1 }, "Tempo", R (50.0f, 200.0f, 0.5f), kSets[0].bpm, fa ([] (float v) { return juce::String (v, 1) + " BPM"; })));
    p.push_back (std::make_unique<F> (P { "swing", 1 }, "Swing", R (0.0f, 0.8f, 0.0f), kSets[0].swing, fa (fmtPct)));
    p.push_back (std::make_unique<F> (P { "human", 1 }, "Human", R (0.0f, 1.0f, 0.01f), 0.12f, fa (fmtPct)));
    p.push_back (std::make_unique<F> (P { "glue", 1 }, "Glue", R (0.0f, 1.0f, 0.01f), k0.glue, fa (fmtPct)));
    p.push_back (std::make_unique<F> (P { "volume", 1 }, "Volume", R (-40.0f, 3.0f, 0.1f), -2.0f, fa (fmtDb)));
    p.push_back (std::make_unique<C> (P { "spaceType", 1 }, "Space type", juce::StringArray { "Room", "Plate", "Hall" }, (int) k0.space.type));
    p.push_back (std::make_unique<F> (P { "spaceSize", 1 }, "Space size", R (0.3f, 0.96f, 0.01f), k0.space.size, fa (fmtPct)));
    { R r (1000.0f, 12000.0f, 1.0f); r.setSkewForCentre (4000.0f);
      p.push_back (std::make_unique<F> (P { "spaceDamp", 1 }, "Space damp", r, k0.space.damp, fa (fmtHz))); }
    p.push_back (std::make_unique<F> (P { "spacePre", 1 }, "Space pre-delay", R (0.0f, 60.0f, 1.0f), k0.space.pre, fa ([] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; })));
    p.push_back (std::make_unique<F> (P { "spaceReturn", 1 }, "Space return", R (0.0f, 1.0f, 0.01f), k0.space.ret, fa (fmtPct)));
    p.push_back (std::make_unique<C> (P { "echoTime", 1 }, "Echo time", juce::StringArray { "1/16 D", "1/8", "1/8 D", "1/4" }, kSets[0].echoTime));
    p.push_back (std::make_unique<F> (P { "echoFeedback", 1 }, "Echo feedback", R (0.0f, 0.78f, 0.01f), k0.echo.fb, fa (fmtPct)));
    { R r (800.0f, 12000.0f, 1.0f); r.setSkewForCentre (3500.0f);
      p.push_back (std::make_unique<F> (P { "echoTone", 1 }, "Echo tone", r, k0.echo.tone, fa (fmtHz))); }
    p.push_back (std::make_unique<F> (P { "echoReturn", 1 }, "Echo return", R (0.0f, 1.0f, 0.01f), k0.echo.ret, fa (fmtPct)));
    p.push_back (std::make_unique<F> (P { "colorTape", 1 }, "Tape", R (0.0f, 1.0f, 0.01f), k0.color.tape, fa (fmtPct)));
    p.push_back (std::make_unique<F> (P { "colorDust", 1 }, "Dust", R (0.0f, 1.0f, 0.01f), k0.color.dust, fa (fmtPct)));
    p.push_back (std::make_unique<F> (P { "colorCrush", 1 }, "Crush", R (0.0f, 1.0f, 0.01f), k0.color.crush, fa (fmtPct)));
    for (int i = 0; i < kNumPads; ++i)
    {
        const juce::String n = "Pad " + juce::String (i + 1) + " ";
        const ChannelDef& c = k0.ch[i];
        p.push_back (std::make_unique<F> (P { padId (i, "level"), 1 }, n + "level", R (-40.0f, 6.0f, 0.1f), c.levelDb, fa (fmtDb)));
        p.push_back (std::make_unique<F> (P { padId (i, "tune"), 1 }, n + "tune", R (-12.0f, 12.0f, 1.0f), 0.0f, fa ([] (float v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; })));
        p.push_back (std::make_unique<F> (P { padId (i, "decay"), 1 }, n + "decay", R (0.3f, 2.0f, 0.01f), 1.0f, fa ([] (float v) { return juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (v, 2); })));
        p.push_back (std::make_unique<F> (P { padId (i, "pan"), 1 }, n + "pan", R (-1.0f, 1.0f, 0.01f), c.pan, fa (fmtPan)));
        p.push_back (std::make_unique<F> (P { padId (i, "space"), 1 }, n + "space", R (0.0f, 1.0f, 0.01f), c.rev, fa (fmtPct)));
        p.push_back (std::make_unique<F> (P { padId (i, "echo"), 1 }, n + "echo", R (0.0f, 1.0f, 0.01f), c.dly, fa (fmtPct)));
        p.push_back (std::make_unique<B> (P { padId (i, "mute"), 1 }, n + "mute", false));
        p.push_back (std::make_unique<B> (P { padId (i, "solo"), 1 }, n + "solo", false));
    }
    return { p.begin(), p.end() };
}
} // namespace ru8::params
