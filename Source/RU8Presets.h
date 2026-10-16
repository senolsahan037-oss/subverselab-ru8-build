// RU-8 factory presets (P4, owner's decision 2026-10-15: sound + groove, 28 = two per kit, "Style - Kit", ASCII).
//
// A preset loads, in this order: the kit (its sounds and every value a kit sets: channels, glue, SPACE, ECHO, Color), the set
// with its factory steps (edits to that set are replaced) and the set's swing and echo time, HUMAN 12 %, VOLUME -2 dB, then the
// preset's own values below. Never in a preset: mute / solo, SET / MIDI source, section, auto, auto start bar, tempo in a host.
// Plain data, no JUCE: the plug-in and the tests read the same table.
#pragma once
#include "RU8KitData.h"
#include <string>

namespace ru8 {

struct PresetValue { const char* param; float value; };     // a parameter id and its plain value
struct PresetDef
{
    const char* name;
    const char* kit;          // KitDef::id
    const char* set;          // SetDef::id
    PresetValue values[4];    // the preset's own values on top of the kit and the set (param nullptr = unused)
};

constexpr int kNumPresets = 28;

// Index 0 is what a new RU-8 is: the Analog kit with the House set, every other value at its default.
constexpr PresetDef kPresets[kNumPresets] = {
    { "House - Analog",        "analog",   "house",      {} },
    { "Techno - Analog",       "analog",   "techno",     {} },
    { "Disco - Punch",         "punch",    "disco",      {} },
    { "Funk - Punch",          "punch",    "funk",       {} },
    { "Afrobeat - Boom",       "boom",     "afro",       {} },
    { "Boom bap - Boom",       "boom",     "boombap",    {} },
    { "Funk - Dry",            "dry",      "funk",       {} },
    { "Rock - Dry",            "dry",      "rock",       {} },
    { "Drum & bass - Deep",    "deep",     "dnb",        {} },
    { "House - Deep",          "deep",     "house",      {} },
    { "Shuffle - Tape",        "tape",     "shuffle",    {} },
    { "Lo-fi - Tape",          "tape",     "lofi",       {} },
    { "Rock - Industrial",     "steel",    "rock",       {} },
    { "Techno - Industrial",   "steel",    "techno",     {} },
    { "Ciftetelli - Anatolia", "anatolia", "ciftetelli", {} },
    { "House - Anatolia",      "anatolia", "house",      {} },
    { "Trap - Trap",           "trap",     "trap",       {} },
    { "Dembow - Trap",         "trap",     "dembow",     {} },
    { "Jazz brush - Brush",    "brush",    "jazz",       {} },
    { "Bossa nova - Brush",    "brush",    "bossa",      {} },
    { "Electro - Electro",     "electro",  "electro",    {} },
    { "Breakbeat - Electro",   "electro",  "breaks",     {} },
    { "Bossa nova - Glass",    "glass",    "bossa",      {} },
    { "Afrobeat - Glass",      "glass",    "afro",       {} },
    { "Boom bap - Lo-fi",      "lofi",     "boombap",    {} },
    { "Trap - Lo-fi",          "lofi",     "trap",       {} },
    { "Breakbeat - Breaks",    "breaks",   "breaks",     {} },
    { "Drum & bass - Breaks",  "breaks",   "dnb",        {} },
};

inline int presetKit (int i)
{
    for (int k = 0; k < kNumKits; ++k) if (std::string (kKits[k].id) == kPresets[i].kit) return k;
    return 0;
}
inline int presetSet (int i)
{
    for (int s = 0; s < kNumSets; ++s) if (std::string (kSets[s].id) == kPresets[i].set) return s;
    return 0;
}
} // namespace ru8
