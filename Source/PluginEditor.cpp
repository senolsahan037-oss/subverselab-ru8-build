#include "PluginEditor.h"

using namespace ru8ui;

//==============================================================================
RU8LookAndFeel::RU8LookAndFeel()
{
    // Menus and tooltips too: nothing on the panel asks the system for a font.
    setDefaultSansSerifTypeface (faces->regular);
    useMood (Palette::get ("cream"));
}

void RU8LookAndFeel::useMood (const Palette& p)
{
    setColour (juce::PopupMenu::backgroundColourId, p.cream);
    setColour (juce::PopupMenu::textColourId, p.ink);
    setColour (juce::PopupMenu::headerTextColourId, p.goldDeep);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.ink);
    setColour (juce::PopupMenu::highlightedTextColourId, p.cream);
    setColour (juce::TooltipWindow::backgroundColourId, p.ink);
    setColour (juce::TooltipWindow::textColourId, p.cream);
    setColour (juce::TooltipWindow::outlineColourId, p.gold);
}

//==============================================================================
RU8Editor::RU8Editor (RU8AudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), ctx (p, p.isStandalone() || previewAsStandalone),
      front (ctx, [this] { showRear (true); }, [this] { cycleMood(); }, [this] { toggleHelp(); }, [this] { togglePresets(); }, [this] { return presets.isOpen(); }),
      rear (ctx, [this] { showRear (false); }, [this] { setReducedMotion (userReducedMotion() || systemReducedMotion()); }),
      help (ctx, [this] { if (ctx.standalone) grabKeyboardFocus(); }),
      presets (ctx, [this] { if (ctx.standalone) grabKeyboardFocus(); })
{
    // The width the last session kept (resized() writes the live width back into the processor, so read it first).
    const int savedWidth = juce::jlimit (kW, kW * 2, proc.editorWidth.load());
    ctx.reducedMotion = userReducedMotion() || systemReducedMotion();
    lnf.useMood (ctx.p());
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (ctx.standalone);   // in a host every key belongs to the host (DAW integration rule 4)
    addMouseListener (this, true);
    addAndMakeVisible (front);
    addChildComponent (rear);
    addChildComponent (help);
    addChildComponent (presets);
    proc.beforePresetLoad = [this] { ctx.armGlide(); };   // a preset writes values: the knobs glide to them
#if RU8_LICENSE_REQUIRED
    addAndMakeVisible (license);
#endif
    setResizable (true, true);
    updateSizeLimits();
    const int w = juce::jmin (savedWidth, getConstrainer()->getMaximumWidth());
    setSize (w, juce::roundToInt ((float) w * (float) kH / (float) kW));
    for (const char* id : { "kit", "set", "spaceType" }) proc.apvts.addParameterListener (id, this);
    timerCallback();
    startTimerHz (30);
}

RU8Editor::~RU8Editor()
{
    stopTimer();
    proc.beforePresetLoad = nullptr;
    for (const char* id : { "kit", "set", "spaceType" }) proc.apvts.removeParameterListener (id, this);
    setLookAndFeel (nullptr);
}

int RU8Editor::maxWidthFor (juce::Rectangle<int> userArea)
{
    if (userArea.isEmpty()) return kW * 2;
    const int byWidth = userArea.getWidth();
    const int byHeight = (int) std::floor ((double) (userArea.getHeight() - kTitleBarRoom) * (double) kW / (double) kH);
    return juce::jlimit (kW, kW * 2, juce::jmin (byWidth, byHeight));
}

void RU8Editor::updateSizeLimits()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* d = isShowing() ? displays.getDisplayForRect (getScreenBounds()) : displays.getPrimaryDisplay();
    const int maxW = maxWidthFor (d != nullptr ? d->userArea : juce::Rectangle<int>());
    auto* c = getConstrainer();
    if (c->getMaximumWidth() == maxW && c->getMinimumWidth() == kW) return;
    setResizeLimits (kW, kH, maxW, juce::roundToInt ((float) maxW * (float) kH / (float) kW));
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    if (getWidth() > maxW) setSize (maxW, juce::roundToInt ((float) maxW * (float) kH / (float) kW));
}

void RU8Editor::paint (juce::Graphics& g) { g.fillAll (ctx.p().desk); }

void RU8Editor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    std::initializer_list<juce::Component*> layers = {
        &front, &rear, &help, &presets
#if RU8_LICENSE_REQUIRED
        , &license
#endif
    };
    for (auto* c : layers)
    {
        c->setBounds (0, 0, kW, kH);
        c->setTransform (juce::AffineTransform::scale (s));
    }
    proc.editorWidth = getWidth();
}

void RU8Editor::mouseDown (const juce::MouseEvent& e) { if (ctx.standalone) svl::takeFocusOnClick (*this, e); }

void RU8Editor::showRear (bool r)
{
    if (help.isOpen()) help.close();
    if (presets.isOpen()) presets.close();
    rear.setVisible (r);
    front.setVisible (! r);     // the front's controls are inert while the rear panel shows
    if (ctx.standalone) grabKeyboardFocus();
}

void RU8Editor::setMood (const juce::String& m)
{
    ctx.setMood (m);
    lnf.useMood (ctx.p());
    front.moodChanged(); rear.moodChanged();
    help.repaint(); presets.repaint();
    repaint();
}

void RU8Editor::cycleMood() { setMood (Palette::nextMood (proc.mood)); }

void RU8Editor::toggleHelp()
{
    if (rear.isVisible()) return;
    if (presets.isOpen()) presets.close();
    if (help.isOpen()) help.close(); else help.open (0);
}

void RU8Editor::togglePresets()
{
    if (rear.isVisible()) return;
    if (help.isOpen()) help.close();
    if (presets.isOpen()) presets.close(); else presets.open();
}

void RU8Editor::setReducedMotion (bool on) { ctx.reducedMotion = on; front.water.repaint(); }

void RU8Editor::timerCallback()
{
    if (++limitTicks >= 30) { limitTicks = 0; if (isShowing()) updateSizeLimits(); }   // once a second: the window may have moved to another display
    if (rear.isVisible()) rear.tick();
    front.tick();
    if (presets.isOpen())
    {   // the list shows the loaded preset and its modified dot (a host may change the program while it is open)
        const int key = proc.currentPreset() * 2 + (proc.presetModified() ? 1 : 0);
        if (key != presetListKey) { presetListKey = key; presets.repaint(); }
    }
#if RU8_LICENSE_REQUIRED
    license.tick();
#endif
}

//==============================================================================
int RU8Editor::padForKey (const juce::KeyPress& key) const
{
    static const char order[] = "ZXCVASDFQWER1234";
    static const char shifted[] = "!@#$";   // Shift + 1 2 3 4 on a US layout
    const auto up = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    for (int i = 0; i < 16; ++i) if ((juce::juce_wchar) order[i] == up) return i;
    const auto tc = key.getTextCharacter();
    for (int i = 0; i < 4; ++i) if ((juce::juce_wchar) shifted[i] == tc) return 12 + i;
    return -1;
}

bool RU8Editor::keyPressed (const juce::KeyPress& key)
{
    if (! ctx.standalone) return false;   // a host's keys are the host's: RU-8 takes none (not even Cmd+Z, Escape or Space)
    if (svl::handleUndoKey (proc.undo, key)) return true;
    const auto mods = key.getModifiers();
    const auto up = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());

    if (key == juce::KeyPress::escapeKey)
    {
        if (presets.isOpen()) { presets.close(); return true; }
        if (help.isOpen()) { help.close(); return true; }
        return false;
    }
    if (help.isOpen())
    {
        if (key == juce::KeyPress::rightKey) { help.step (1); return true; }
        if (key == juce::KeyPress::leftKey) { help.step (-1); return true; }
    }
    if (mods.isCommandDown())
    {
        if (rear.isVisible()) return false;
        if (! mods.isAltDown() && ! mods.isShiftDown() && up == 'C') { front.copyPadSteps(); return true; }
        if (! mods.isAltDown() && ! mods.isShiftDown() && up == 'V') { front.pastePadSteps(); return true; }
        return false;
    }
    if (mods.isAltDown() || mods.isCtrlDown()) return false;

    if (up == 'B') { showRear (! rear.isVisible()); return true; }
    if (up == 'M') { cycleMood(); return true; }
    if (rear.isVisible()) return false;      // the rest is the front's
    if (up == 'H') { toggleHelp(); return true; }
    if (const int pad = padForKey (key); pad >= 0)
    {
        const int code = key.getKeyCode();
        if (heldKeys.count (code) != 0) return true;       // auto-repeat
        heldKeys.insert (code);
        front.bed.padAt (pad).setHeld (true);
        front.playPad (pad, mods.isShiftDown() ? 0.5f : 1.0f);
        return true;
    }
    if (up == '5') { act::sectionKey (ctx, 1); return true; }
    if (up == '6') { act::sectionKey (ctx, 2); return true; }
    if (up == '7') { act::fillKey (ctx); return true; }
    if (up == '8') { act::autoKey (ctx); return true; }
    if (up == 'T') { front.tap(); return true; }
    if (key == juce::KeyPress::spaceKey) { if (ctx.standalone) { front.startStop(); return true; } return false; }
    return false;
}

bool RU8Editor::keyStateChanged (bool)
{
    if (! ctx.standalone) return false;
    bool changed = false;
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (*it))
        {
            const int pad = padForKey (juce::KeyPress (*it));
            if (pad >= 0) front.bed.padAt (pad).setHeld (false);
            it = heldKeys.erase (it);
            changed = true;
        }
        else ++it;
    }
    return changed;
}
