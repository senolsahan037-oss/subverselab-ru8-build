#if defined (__APPLE__)
 #include <CoreFoundation/CoreFoundation.h>   // before JuceHeader: its using-directive would make Point ambiguous
#endif
#include "UiCommon.h"
#include "GlyphData.h"
#include "../RU8KitData.h"
#include <map>

using namespace ru8;

namespace ru8ui
{
const int kDisplayOrder[16] = { 12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3 };

Faces::Faces()
{
    bold = juce::Typeface::createSystemTypefaceFor (BinaryData::texgyreherosbold_otf, (size_t) BinaryData::texgyreherosbold_otfSize);
    regular = juce::Typeface::createSystemTypefaceFor (BinaryData::texgyreherosregular_otf, (size_t) BinaryData::texgyreherosregular_otfSize);
    italic = juce::Typeface::createSystemTypefaceFor (BinaryData::texgyreherositalic_otf, (size_t) BinaryData::texgyreherositalic_otfSize);
    mono = juce::Typeface::createSystemTypefaceFor (BinaryData::DejaVuSansMonoBold_ttf, (size_t) BinaryData::DejaVuSansMonoBold_ttfSize);
}

juce::Font heros (float px, bool bold)
{
    juce::SharedResourcePointer<Faces> f;
    return juce::Font (juce::FontOptions (bold ? f->bold : f->regular).withPointHeight (px));
}

juce::Font display (float px, float hscale)
{
    juce::SharedResourcePointer<Faces> f;
    return juce::Font (juce::FontOptions (f->bold).withPointHeight (px)).withHorizontalScale (hscale);
}

float trackedWidth (const juce::String& s, juce::Font f, float trackEm)
{
    const float track = trackEm * f.getHeight();
    if (track != 0.0f) f = f.withExtraKerningFactor (trackEm);
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, s, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth() + 0.0f;
}

void drawTracked (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area, juce::Font f, float trackEm, juce::Justification j, bool clip)
{
    if (trackEm != 0.0f) f = f.withExtraKerningFactor (trackEm);
    g.setFont (f);
    const float shift = trackEm * f.getHeight();
    // a trailing letter space would push centred and right-aligned text off the box
    if (j.testFlags (juce::Justification::horizontallyCentred)) area = area.translated (shift * 0.5f, 0.0f);
    else if (j.testFlags (juce::Justification::right)) area = area.translated (shift, 0.0f);
    g.drawText (s, area, j, clip);
}

// ---- glyphs
namespace
{
const GlyphDef* findDef (const GlyphDef* list, size_t n, const juce::String& name)
{
    for (size_t i = 0; i < n; ++i)
        if (name == list[i].name) return &list[i];
    return nullptr;
}

const juce::Drawable* glyphDrawable (GlyphSet set, const juce::String& name, juce::Colour c, float sw)
{
    static std::map<juce::String, std::unique_ptr<juce::Drawable>> cache;
    const auto key = juce::String ((int) set) + ":" + name + ":" + juce::String::toHexString ((int) (c.getARGB() & 0xffffff)) + ":" + juce::String (juce::roundToInt (sw * 10.0f));
    auto it = cache.find (key);
    if (it != cache.end()) return it->second.get();

    const GlyphDef* def = set == GlyphSet::voice ? findDef (kVoiceGlyphs, sizeof kVoiceGlyphs / sizeof kVoiceGlyphs[0], name)
                        : set == GlyphSet::type  ? findDef (kTypeGlyphs, sizeof kTypeGlyphs / sizeof kTypeGlyphs[0], name)
                                                 : findDef (kIcons, sizeof kIcons / sizeof kIcons[0], name);
    std::unique_ptr<juce::Drawable> d;
    if (def != nullptr)
    {
        const int vb = set == GlyphSet::icon ? 24 : 32;
        const juce::String xml = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + juce::String (vb) + " " + juce::String (vb)
                               + "\" width=\"" + juce::String (vb) + "\" height=\"" + juce::String (vb) + "\"><g fill=\"none\" stroke=\"#"
                               + c.withAlpha (1.0f).toDisplayString (false) + "\" stroke-width=\"" + juce::String (sw) + "\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                               + juce::String::fromUTF8 (def->svg) + "</g></svg>";
        d = juce::Drawable::createFromSVGString (xml);
    }
    auto* raw = d.get();
    cache[key] = std::move (d);
    return raw;
}
} // namespace

void drawGlyph (juce::Graphics& g, GlyphSet set, const juce::String& name, juce::Rectangle<float> area, juce::Colour colour, float sw)
{
    if (auto* d = glyphDrawable (set, name, colour, sw))
        d->drawWithin (g, area, juce::RectanglePlacement::centred, colour.getFloatAlpha());
}

std::pair<GlyphSet, juce::String> padGlyph (int pad, int kit)
{
    if (pad < 8) return { GlyphSet::voice, kPadIds[pad] };
    const auto& a = kKits[juce::jlimit (0, kNumKits - 1, kit)].aux[pad - 8];
    const juce::String n (a.name);
    // the prototype names a glyph on a few voices; every other aux voice shows its type's glyph
    static const std::pair<const char*, const char*> byName[] = {
        { "Bendir", "frame" }, { "Brush", "shaker" }, { "Brush slap", "brush" }, { "Brush sweep", "brush" }, { "Chime", "bell" }, { "Davul", "davul" },
        { "Def", "frame" }, { "Doum", "darbuka" }, { "Glass bell", "bell" }, { "Gong", "gong" }, { "Keys", "bell" }, { "Pluck", "bell" },
        { "Ride", "ride" }, { "Ride bell", "bell" }, { "Ride tick", "ride" }, { "Spoons", "spoon" }, { "Tek", "darbuka" }, { "Zil", "zil" } };
    for (const auto& e : byName) if (n == e.first) return { GlyphSet::type, e.second };
    switch (a.type)
    {
        case AuxType::conga: return { GlyphSet::type, "conga" };
        case AuxType::zap: return { GlyphSet::type, "zap" };
        case AuxType::sub: return { GlyphSet::type, "sub" };
        case AuxType::shaker: return { GlyphSet::type, "shaker" };
        case AuxType::tamb: return { GlyphSet::type, "tamb" };
        case AuxType::snap: return { GlyphSet::type, "snap" };
        case AuxType::noise: return { GlyphSet::type, "noise" };
        case AuxType::cowbell: return { GlyphSet::type, "cowbell" };
        case AuxType::clave: return { GlyphSet::type, "clave" };
        case AuxType::metal: return { GlyphSet::type, "metal" };
        case AuxType::hand: return { GlyphSet::type, "darbuka" };
        case AuxType::blip: return { GlyphSet::type, "blip" };
    }
    return { GlyphSet::type, "blip" };
}

juce::String padName (int pad, int kit)
{
    static const char* main[8] = { "Kick", "Snare", "Clap", "Rim", "Closed hat", "Open hat", "Low tom", "High tom" };
    if (pad < 8) return main[pad];
    return kKits[juce::jlimit (0, kNumKits - 1, kit)].aux[pad - 8].name;
}

juce::String padTag (int pad, int kit)
{
    if (pad < 8) { juce::String t (kPadIds[pad]); return t.toUpperCase(); }
    return kKits[juce::jlimit (0, kNumKits - 1, kit)].aux[pad - 8].tag;
}

const char* padKey (int pad)
{
    static const char* k[16] = { "Z", "X", "C", "V", "A", "S", "D", "F", "Q", "W", "E", "R", "1", "2", "3", "4" };
    return k[pad];
}

int padFlashMs (int pad, int kit)
{
    static const int main[8] = { 320, 260, 280, 160, 140, 420, 320, 280 };
    if (pad < 8) return main[pad];
    return juce::jlimit (140, 900, juce::roundToInt (kKits[juce::jlimit (0, kNumKits - 1, kit)].aux[pad - 8].dec * 900.0f));
}

// ---- raised keys
void drawKeyShadow (juce::Graphics& g, const Palette& p, juce::Rectangle<float> face, float radius, bool pressed, bool soft)
{
    // 0 3px 0 lift-edge, 0 4px 2px -1px lift, 0 10px 14px -7px lift   (pressed: 0 1px 0, 0 2px 3px -2px)
    if (soft)
    {
        g.setColour (p.lift.withMultipliedAlpha (0.55f));
        g.fillRoundedRectangle (face.translated (0.0f, pressed ? 2.0f : 6.0f).reduced (1.5f, 0.0f), radius);
    }
    g.setColour (p.liftEdge);
    g.fillRoundedRectangle (face.translated (0.0f, pressed ? 1.0f : 3.0f), radius);
}

void drawSoftShadow (juce::Graphics& g, juce::Rectangle<float> body, float radius, juce::Colour c, float dy, float spread)
{
    for (int i = 0; i < 4; ++i)
    {
        const float k = (float) i / 3.0f;
        g.setColour (c.withMultipliedAlpha (0.20f * (1.0f - 0.55f * k)));
        g.fillRoundedRectangle (body.translated (0.0f, dy).expanded (spread * k, spread * k * 0.8f), radius + spread * k);
    }
}

namespace act
{
void setParam (RU8AudioProcessor& p, const juce::String& id, float plain)
{
    if (auto* prm = p.apvts.getParameter (id)) svl::setParamAsGesture (prm, plain);
}

void stepKit (Ctx& c, int d)
{
    const int n = ru8::kNumKits;
    setParam (c.proc, "kit", (float) (((c.kit() + d) % n + n) % n));
}

void stepSet (Ctx& c, int d)
{
    const int n = ru8::kNumSlots, cur = c.proc.editSlot();
    setParam (c.proc, "set", (float) (((cur + d) % n + n) % n));
}

void sectionKey (Ctx& c, int section)
{
    if (c.proc.apvts.getRawParameterValue ("auto")->load() > 0.5f) setParam (c.proc, "auto", 0.0f);
    c.fillQueuedUi = false;
    setParam (c.proc, "section", (float) section);
}

void fillKey (Ctx& c)
{
    if (c.proc.apvts.getRawParameterValue ("auto")->load() > 0.5f) setParam (c.proc, "auto", 0.0f);
    c.proc.queueFill();
    c.fillQueuedUi = true; c.fillQueueBar = -1000;
}

void autoKey (Ctx& c)
{
    setParam (c.proc, "auto", c.proc.apvts.getRawParameterValue ("auto")->load() > 0.5f ? 0.0f : 1.0f);
    c.fillQueuedUi = false;
}

void selectPad (Ctx& c, int pad) { c.selectedPad = juce::jlimit (0, 15, pad); }

void cyclePadStep (Ctx& c, int pad, int step, int& outPad, float& outVel)
{
    auto& ch = c.proc.song().slot[(size_t) c.proc.editSlot()].steps[c.viewSection()][pad][step];
    const char next = ch == '.' ? 'x' : ch == 'x' ? 'X' : ch == 'X' ? 'g' : '.';   // 'r' (roll) goes to '.'
    ch = next;
    outPad = next != '.' ? pad : -1;
    outVel = next == 'X' ? ru8::kVel.X : next == 'x' ? ru8::kVel.x : next == 'g' ? ru8::kVel.g : 0.0f;
}
} // namespace act

juce::String noteName (int n)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 2);
}

juce::Image tintedImage (const juce::Image& src, juce::Colour c)
{
    juce::Image out (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
    for (int y = 0; y < src.getHeight(); ++y)
        for (int x = 0; x < src.getWidth(); ++x)
            out.setPixelAt (x, y, c.withAlpha (src.getPixelAt (x, y).getFloatAlpha() * c.getFloatAlpha()));
    return out;
}

bool systemReducedMotion()
{
#if JUCE_MAC
    bool on = false;
    if (auto v = CFPreferencesCopyAppValue (CFSTR ("reduceMotion"), CFSTR ("com.apple.universalaccess")))
    {
        if (CFGetTypeID (v) == CFBooleanGetTypeID()) on = CFBooleanGetValue ((CFBooleanRef) v);
        else if (CFGetTypeID (v) == CFNumberGetTypeID()) { int n = 0; CFNumberGetValue ((CFNumberRef) v, kCFNumberIntType, &n); on = n != 0; }
        CFRelease (v);
    }
    return on;
#else
    return false;
#endif
}

namespace
{
std::unique_ptr<juce::PropertiesFile> settingsFile()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "RU-8"; o.folderName = "SubverseLab"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    return std::make_unique<juce::PropertiesFile> (o);
}
}

bool userReducedMotion()
{
    auto f = settingsFile();
    return f != nullptr && f->getBoolValue ("reducedMotion", false);
}

void setUserReducedMotion (bool on)
{
    if (auto f = settingsFile()) { f->setValue ("reducedMotion", on); f->saveIfNeeded(); }
}

bool chainVisible (const juce::Component* c)
{
    for (; c != nullptr; c = c->getParentComponent())
        if (! c->isVisible()) return false;
    return true;
}
} // namespace ru8ui
