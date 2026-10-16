// RU-8 panel colours: the Water edition's CSS tokens (:root and [data-mood=...]) as one struct per mood.
// Moods change colour only. Cream is the SubverseLab face and the default.
#pragma once
#include <JuceHeader.h>

namespace ru8ui
{
struct Palette
{
    juce::Colour cream, bed, desk, ink, inkSoft, inkQuiet, inkGround, rdLine, rdDim, gold, goldDeep, rule, rdStep, rdBeat,
                 glassFg, glassEdge, hover, hitHover, padAux, padHeld, stHover, chipActive, veil, shadow, tipAccent, runFg, onGold, glow,
                 capHi, capLo, cast, lift, liftEdge, bevelHi, bevelLo, well, sheen, sheenLo,
                 waterTop, waterBot, waterSurf, waterBubble, waterPost, waterRing;
    juce::Colour shape[4];
    bool waterGlow = false;
    float waterAlpha = 0.32f;
    bool dark = false;

    static juce::StringArray moods() { return { "cream", "night", "powder", "pink" }; }
    static juce::String moodName (const juce::String& m) { return m == "night" ? "Night" : m == "powder" ? "Powder" : m == "pink" ? "Pink" : "Cream"; }
    static juce::String normalise (const juce::String& m) { return moods().contains (m) ? m : juce::String ("cream"); }
    static juce::String nextMood (const juce::String& m) { const auto l = moods(); return l[(l.indexOf (normalise (m)) + 1) % l.size()]; }

    static const Palette& get (const juce::String& mood)
    {
        static const Palette cream = make ("cream"), night = make ("night"), powder = make ("powder"), pink = make ("pink");
        const auto m = normalise (mood);
        return m == "night" ? night : m == "powder" ? powder : m == "pink" ? pink : cream;
    }

private:
    static juce::Colour C (juce::uint32 rgb) { return juce::Colour (0xff000000u | rgb); }
    static juce::Colour A (int r, int g, int b, float a) { return juce::Colour::fromFloatRGBA ((float) r / 255.0f, (float) g / 255.0f, (float) b / 255.0f, a); }

    static Palette make (const juce::String& m)
    {
        Palette p;
        // :root (cream)
        p.cream = C (0xF2EFE7); p.bed = C (0xEAE5D8); p.desk = C (0xE8E4D9); p.ink = C (0x033733); p.inkSoft = C (0x496C68);
        p.inkQuiet = A (3, 55, 51, .42f); p.inkGround = C (0x032825); p.rdLine = C (0x3E605C); p.rdDim = C (0x8FA8A4);
        p.gold = C (0xC5A059); p.goldDeep = C (0xA9853F); p.rule = C (0xCECAC0); p.rdStep = C (0x4F726E); p.rdBeat = C (0x6F8E8A);
        p.glassFg = C (0xF2EFE7); p.glassEdge = C (0x033733); p.hover = C (0xEAE6DB); p.hitHover = A (3, 55, 51, .07f);
        p.padAux = C (0xF6F3EC); p.padHeld = C (0xE7E1D2); p.stHover = C (0xE6E1D5); p.chipActive = C (0xE2DCCD);
        p.veil = A (242, 239, 231, .35f); p.shadow = A (3, 40, 37, .25f); p.tipAccent = C (0xC5A059); p.runFg = C (0xC5A059);
        p.onGold = C (0x033733); p.glow = C (0xC5A059);
        p.capHi = C (0xFDFBF6); p.capLo = C (0xE3DDCF); p.cast = A (3, 40, 37, .38f); p.lift = A (3, 40, 37, .24f); p.liftEdge = A (3, 40, 37, .34f);
        p.bevelHi = A (255, 255, 255, .60f); p.bevelLo = A (3, 40, 37, .10f); p.well = A (3, 40, 37, .16f);
        p.sheen = A (255, 255, 255, .42f); p.sheenLo = A (3, 40, 37, .05f);
        p.waterTop = C (0xF1F0E9); p.waterBot = C (0xDCE7E1); p.waterSurf = A (3, 55, 51, .16f); p.waterBubble = A (3, 55, 51, .22f);
        p.waterPost = C (0x496C68); p.waterRing = C (0xC5A059); p.waterGlow = false; p.waterAlpha = .32f;
        p.shape[0] = C (0x496C68); p.shape[1] = C (0xC5A059); p.shape[2] = C (0x8FA8A4); p.shape[3] = C (0xA9853F);

        if (m == "night")
        {
            p.dark = true;
            p.cream = C (0x14211F); p.bed = C (0x0F1A18); p.desk = C (0x0A1211);
            p.ink = C (0xE8E1CF); p.inkSoft = C (0xA7B6B0); p.inkQuiet = A (232, 225, 207, .42f); p.inkGround = C (0x050B0A);
            p.rdLine = C (0x26403C); p.rdDim = C (0x86A09A); p.gold = C (0xD4B06A); p.goldDeep = C (0xD9B873); p.rule = C (0x2B3B38);
            p.rdStep = C (0x3C5B56); p.rdBeat = C (0x557570); p.glassFg = C (0xEFE8D6); p.glassEdge = C (0x2B3B38);
            p.hover = C (0x1C2B28); p.hitHover = A (232, 225, 207, .08f); p.padAux = C (0x182623); p.padHeld = C (0x24332F);
            p.stHover = C (0x203029); p.chipActive = C (0x203029);
            p.veil = A (5, 11, 10, .40f); p.shadow = A (0, 0, 0, .55f); p.tipAccent = C (0x7E5F22); p.runFg = C (0x7E5F22);
            p.onGold = C (0x14211F); p.glow = C (0x7E5F22);
            p.capHi = C (0x2E413C); p.capLo = C (0x101A18); p.cast = A (0, 0, 0, .75f); p.lift = A (0, 0, 0, .6f); p.liftEdge = A (0, 0, 0, .85f);
            p.bevelHi = A (255, 255, 255, .08f); p.bevelLo = A (0, 0, 0, .40f); p.well = A (0, 0, 0, .55f);
            p.sheen = A (255, 255, 255, .06f); p.sheenLo = A (0, 0, 0, .25f);
            p.waterTop = C (0x14211F); p.waterBot = C (0x06100E); p.waterSurf = A (212, 176, 106, .22f); p.waterBubble = A (212, 176, 106, .32f);
            p.waterPost = C (0x86A09A); p.waterRing = C (0xD4B06A); p.waterGlow = true; p.waterAlpha = .38f;
            p.shape[0] = C (0xD4B06A); p.shape[1] = C (0xEFE8D6); p.shape[2] = C (0x6FB0A4); p.shape[3] = C (0xF0D08F);
        }
        else if (m == "powder")
        {
            p.cream = C (0xF9EFEC); p.bed = C (0xF2E3DF); p.desk = C (0xEFE1DD);
            p.ink = C (0x4A1D3A); p.inkSoft = C (0x7A5069); p.inkQuiet = A (74, 29, 58, .42f); p.inkGround = C (0x36142C);
            p.rdLine = C (0x5C3250); p.rdDim = C (0xCDA9BF); p.gold = C (0xD49A8A); p.goldDeep = C (0x9A5547); p.rule = C (0xE5D0CC);
            p.rdStep = C (0x6E4362); p.rdBeat = C (0x8E6683); p.glassFg = C (0xFBEFEF); p.glassEdge = C (0x4A1D3A);
            p.hover = C (0xF2E1DD); p.hitHover = A (74, 29, 58, .07f); p.padAux = C (0xFCF5F3); p.padHeld = C (0xEFDBD6);
            p.stHover = C (0xF1DFDB); p.chipActive = C (0xECD7D2);
            p.veil = A (249, 239, 236, .35f); p.shadow = A (54, 20, 44, .25f); p.tipAccent = C (0xE3AE9F); p.runFg = C (0xE3AE9F);
            p.onGold = C (0x4A1D3A); p.glow = C (0xD49A8A);
            p.capHi = C (0xFFFAF8); p.capLo = C (0xEDD9D4); p.cast = A (54, 20, 44, .38f); p.lift = A (54, 20, 44, .22f); p.liftEdge = A (54, 20, 44, .32f);
            p.bevelHi = A (255, 255, 255, .65f); p.bevelLo = A (54, 20, 44, .10f); p.well = A (54, 20, 44, .15f);
            p.sheen = A (255, 255, 255, .45f); p.sheenLo = A (54, 20, 44, .05f);
            p.waterTop = C (0xF9EFEC); p.waterBot = C (0xEBD8E2); p.waterSurf = A (74, 29, 58, .14f); p.waterBubble = A (74, 29, 58, .20f);
            p.waterPost = C (0x7A5069); p.waterRing = C (0xD49A8A); p.waterGlow = false; p.waterAlpha = .36f;
            p.shape[0] = C (0xC98FA0); p.shape[1] = C (0x9E7FB8); p.shape[2] = C (0xD49A8A); p.shape[3] = C (0x7FB5A5);
        }
        else if (m == "pink")
        {
            p.cream = C (0xFFE6F0); p.bed = C (0xFAD4E4); p.desk = C (0xF6CFE0);
            p.ink = C (0x5A0F3A); p.inkSoft = C (0x8A3A66); p.inkQuiet = A (90, 15, 58, .42f); p.inkGround = C (0x3E0828);
            p.rdLine = C (0x6A1E4A); p.rdDim = C (0xF0A8C8); p.gold = C (0xF2558F); p.goldDeep = C (0xA81E5A); p.rule = C (0xEDBFD3);
            p.rdStep = C (0x7A2A5A); p.rdBeat = C (0x9A4A7A); p.glassFg = C (0xFFF0F6); p.glassEdge = C (0x5A0F3A);
            p.hover = C (0xFADAE8); p.hitHover = A (90, 15, 58, .07f); p.padAux = C (0xFFEEF5); p.padHeld = C (0xF7CDE0);
            p.stHover = C (0xF8D6E5); p.chipActive = C (0xF5CCDD);
            p.veil = A (255, 230, 240, .35f); p.shadow = A (62, 8, 40, .25f); p.tipAccent = C (0xFF9CC4); p.runFg = C (0xFF9CC4);
            p.onGold = C (0x3E0828); p.glow = C (0xF77DAA);
            p.capHi = C (0xFFF7FA); p.capLo = C (0xF4C9DC); p.cast = A (90, 15, 58, .36f); p.lift = A (90, 15, 58, .22f); p.liftEdge = A (90, 15, 58, .32f);
            p.bevelHi = A (255, 255, 255, .65f); p.bevelLo = A (90, 15, 58, .10f); p.well = A (90, 15, 58, .15f);
            p.sheen = A (255, 255, 255, .45f); p.sheenLo = A (90, 15, 58, .05f);
            p.waterTop = C (0xFFE6F0); p.waterBot = C (0xF8C6DC); p.waterSurf = A (90, 15, 58, .16f); p.waterBubble = A (168, 30, 90, .24f);
            p.waterPost = C (0x8A3A66); p.waterRing = C (0xF2558F); p.waterGlow = false; p.waterAlpha = .38f;
            p.shape[0] = C (0xF2558F); p.shape[1] = C (0xA81E5A); p.shape[2] = C (0xFF9CC4); p.shape[3] = C (0x8A3FA0);
        }
        return p;
    }
};
} // namespace ru8ui
