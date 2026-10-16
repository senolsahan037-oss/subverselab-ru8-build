// controlproof: every RU-8 parameter in every variation through the real processor (SvlControlProof.h, instrument mode:
// the sequencer plays the default set with the host transport; the material only decides when the transport runs).
//   controlproof [--quick] [--checks 1,3] [--param id] ...   (options of SvlControlProof.h)
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "SvlControlProof.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    namespace cp = svl::controlproof;
    using cp::feat::outRmsDb; using cp::feat::outPeakDb;
    cp::Config c;
    c.name = "RU-8";
    c.make = [] { return std::make_unique<RU8AudioProcessor>(); };
    c.instrument = true;
    c.tailSeconds = 12.0;   // SPACE (hall, size 96 %) and ECHO (feedback 78 %) ring for seconds after the transport stops
    c.designedGainDb = [] (const cp::StateView& s) { return (double) juce::jmax (0.0f, s.raw ("volume")) + 6.0; };   // VOLUME up to +3 dB, pad levels up to +6 dB
    // kit, set and SPACE type load other values (on the message thread in a host; here they change the sound / plan only)
    c.latencyNote = 38;   // GM snare: the latency probe plays it with the transport stopped and finds where its sound starts
    c.recallParams = { "kit", "set", "spaceType" };
    c.applyFirst = { "kit", "set", "spaceType" };
    auto D = [] (const char* id, int dir, cp::Feature f, double tol, const char* fname, const char* note, std::vector<std::pair<juce::String, float>> force = {})
    {
        cp::Direction d; d.id = id; d.dir = dir; d.feature = f; d.tol = tol; d.featureName = fname; d.note = note; d.force = force;
        d.force.push_back ({ "section", 1.0f / 3.0f });   // section A (index 1 of Off / A / B / Fill)
        return d;
    };
    c.directions = {
        D ("volume", +1, outRmsDb(), 0.25, "output rms (dB)", "master volume before the limiter"),
        D ("pad1.level", +1, outRmsDb(), 0.25, "output rms (dB)", "the kick plays on every quarter of the default set"),
        D ("pad1.mute", -1, outRmsDb(), 0.25, "output rms (dB)", "muting the kick removes it"),
        D ("section", 0, nullptr, 0, "", "Off / A / B / Fill: what plays, not an ordered scale"),
        D ("kit", 0, nullptr, 0, "", "fourteen kits"),
        D ("set", 0, nullptr, 0, "", "seventeen sets and four empty USER slots"),
        D ("spaceType", 0, nullptr, 0, "", "three characters"),
        D ("echoTime", 0, nullptr, 0, "", "four note values"),
        D ("auto", 0, nullptr, 0, "", "arrangement on / off"),
    };
    return cp::main (c, argc, argv);
}
