// RU-8 panel: the factory preset list. The PRESET key in the tools row opens it as a popover over the pad bed (below the tools
// row), grouped by kit; a click on a preset loads it and closes the list; a click anywhere else, the
// PRESET key included (or Escape in the standalone), closes it. The window never changes size: the list is drawn over the panel.
#pragma once
#include "Widgets.h"

namespace ru8ui
{
class PresetList : public juce::Component
{
public:
    PresetList (Ctx&, std::function<void()> onClosed);

    void open();
    void close();
    bool isOpen() const noexcept { return opened; }
    juce::Rectangle<int> listBounds() const noexcept { return listRect; }
    juce::Rectangle<int> itemBounds (int preset) const;     // in the panel's coordinates
    void choose (int preset);                               // as a click on that preset (tests)

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int itemAt (juce::Point<int>) const;
    void setHover (int i) { if (hover != i) { hover = i; repaint(); } }

    Ctx& ctx;
    std::function<void()> closedCb;
    bool opened = false, downInside = false;
    int hover = -1;
    juce::Rectangle<int> listRect;
};
} // namespace ru8ui
