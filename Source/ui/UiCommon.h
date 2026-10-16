// RU-8 panel: shared drawing helpers (typefaces, glyphs, text with tracking, the raised-key look) and the layout of the front.
#pragma once
#include <JuceHeader.h>
#include "Palette.h"
#include "../PluginProcessor.h"
#include "../Params.h"

namespace ru8ui
{
// ---- the panel is drawn at 1180 x 823 logical units and scaled as one piece (100 % - 200 %)
constexpr int kW = 1180, kH = 823;

namespace layout
{
using R = juce::Rectangle<int>;
// head
inline const R readout { 255, 21, 670, 127 };
inline const R startBtn { 947, 31, 139, 56 }, tapBtn { 1020, 104, 65, 40 }, helpBtn { 1111, 109, 30, 30 }, tempoKnob { 1101, 26, 50, 68 };
inline const R productBox { 21, 21, 212, 127 };
// deck
inline const R bed { 255, 164, 670, 603 };
constexpr int padSize = 122, padPitch = 134, padX0 = 73, padY0 = 62;   // relative to the bed
constexpr int leftX = 23, rightX = 949, railW = 208;
constexpr int leftT1 = 197, leftT2 = 489, rightT1 = 201, rightT2 = 489;
inline const R baseMood { 21, 783, 30, 26 }, baseFlip { 59, 783, 30, 26 }, tank { 536, 783, 120, 26 }, lockup { 1051, 783, 108, 26 };
// readout parts, relative to the readout
inline const R kitPrev { 14, 16, 24, 52 }, kitNext { 536, 16, 24, 52 }, kitFace { 44, 10, 486, 64 };
inline const R setPrev { 14, 91, 22, 26 }, setNext { 286, 91, 22, 26 }, setFace { 92, 91, 188, 26 };
inline const R srcKey { 40, 91, 50, 26 };     // SET / MIDI, beside the set name (where the word SET was printed)
constexpr float setNameRight = 278.0f;      // the set name ends here
constexpr float setNumX = 96.0f;            // the set number (or NEXT) starts here
inline const R keyA { 337, 91, 39, 26 }, keyB { 381, 91, 39, 26 }, keyF { 425, 91, 49, 26 }, keyAuto { 479, 91, 49, 26 };
inline const R bpmBlock { 578, 10, 78, 64 };
} // namespace layout

// ---- what the panel knows about the machine, shared by every part
struct Ctx
{
    explicit Ctx (RU8AudioProcessor& p, bool standaloneLayout) : proc (p), standalone (standaloneLayout)
    {
        setMood (p.mood);
        pKit = p.apvts.getRawParameterValue ("kit"); pSection = p.apvts.getRawParameterValue ("section");
        for (int i = 0; i < 16; ++i)
        {
            padMute[(size_t) i] = p.apvts.getRawParameterValue (ru8::params::padId (i, "mute"));
            padSolo[(size_t) i] = p.apvts.getRawParameterValue (ru8::params::padId (i, "solo"));
            padDecay[(size_t) i] = p.apvts.getRawParameterValue (ru8::params::padId (i, "decay"));
        }
    }
    RU8AudioProcessor& proc;
    std::atomic<float>* pKit = nullptr; std::atomic<float>* pSection = nullptr;
    std::array<std::atomic<float>*, 16> padMute {}, padSolo {}, padDecay {};
    int curStep = -1;                                       // the sequencer's 16th that plays now (-1: stopped)
    bool fillQueuedUi = false; int fillQueueBar = -1000;    // the FILL key was pressed and its bar has not come yet (blinks)
    int kit() const noexcept { return juce::jlimit (0, 13, juce::roundToInt (pKit->load())); }
    int viewSection() const noexcept { return juce::roundToInt (pSection->load()) == 2 ? 1 : 0; }   // 0 = A, 1 = B (A when Off or Fill)
    bool anySolo() const noexcept { for (auto* s : padSolo) if (s->load() > 0.5f) return true; return false; }
    const Palette* pal = nullptr;
    bool standalone = false;
    bool reducedMotion = false;
    int selectedPad = 0;                                    // engine pad index of the CHANNEL rail
    std::atomic<juce::int64> glideArmUntil { 0 };           // the product is about to write values (kit, set, SPACE type): knobs glide
    std::function<void()> moodChanged;

    void setMood (const juce::String& m) { proc.mood = Palette::normalise (m); pal = &Palette::get (proc.mood); }
    const Palette& p() const noexcept { return *pal; }
    bool glideArmed() const noexcept { return juce::Time::getMillisecondCounter() < (juce::uint32) glideArmUntil.load(); }
    void armGlide (int ms = 600) noexcept { glideArmUntil = (juce::int64) juce::Time::getMillisecondCounter() + ms; }
};

// ---- typefaces: TeX Gyre Heros from BinaryData; no system font is ever asked for
struct Faces
{
    juce::Typeface::Ptr bold, regular, italic, mono;
    Faces();
};
juce::Font heros (float px, bool bold = false);                 // body text (Avenir in the prototype)
juce::Font display (float px, float hscale = 0.80f);            // the display face (DIN Condensed / Oswald): Heros bold, horizontally condensed
void drawTracked (juce::Graphics&, const juce::String&, juce::Rectangle<float> area, juce::Font, float trackEm, juce::Justification, bool clip = false);
float trackedWidth (const juce::String&, juce::Font, float trackEm);

// ---- glyphs: the prototype's SVG paths, drawn with JUCE's SVG parser
enum class GlyphSet { voice, type, icon };
void drawGlyph (juce::Graphics&, GlyphSet, const juce::String& name, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth = 2.0f);
// the glyph of an engine pad under the loaded kit (the aux pads follow the kit's aux type)
std::pair<GlyphSet, juce::String> padGlyph (int pad, int kit);
juce::String padName (int pad, int kit);
juce::String padTag (int pad, int kit);
const char* padKey (int pad);
int padFlashMs (int pad, int kit);
// screen order of the 4 x 4 bed, top row first (pad 1 bottom left)
extern const int kDisplayOrder[16];

// ---- the raised key (gbtn / seg): a face with a hard shadow under it, pressed = down 2 px
constexpr int kKeySlack = 5;
void drawKeyShadow (juce::Graphics&, const Palette&, juce::Rectangle<float> face, float radius, bool pressed, bool soft);
// a soft drop under a body (pads), built from a few layers
void drawSoftShadow (juce::Graphics&, juce::Rectangle<float> body, float radius, juce::Colour c, float dy, float spread);

// ---- what a key or a click does, shared by the mouse and the computer keyboard
namespace act
{
void setParam (RU8AudioProcessor&, const juce::String& id, float plain);          // one complete gesture (one undo step)
void stepKit (Ctx&, int delta);                                                    // wraps round
void stepSet (Ctx&, int delta);
void sectionKey (Ctx&, int section);                                               // 1 = A, 2 = B (turns AUTO off, as the prototype)
void fillKey (Ctx&);
void autoKey (Ctx&);
void selectPad (Ctx&, int pad);
void cyclePadStep (Ctx&, int pad, int step, int& outVelPad, float& outVel);        // '.' -> x -> X -> g -> '.'
} // namespace act

juce::String noteName (int note);                               // C-1 ... (note 60 is C3, as the prototype)
juce::Image tintedImage (const juce::Image& alphaImage, juce::Colour c);
bool systemReducedMotion();                                     // macOS "Reduce motion"
bool userReducedMotion();                                       // the rear panel's own switch (kept in the user's settings)
void setUserReducedMotion (bool);
// Is this component and every parent visible (an editor that is not on a screen counts)?
bool chainVisible (const juce::Component*);
} // namespace ru8ui
