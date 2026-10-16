// RU-8 panel: help. The "?" key (or H) lays numbered markers over the panel; a marker opens its popover next to the control.
// The window never changes size: the popover is drawn over the panel. Escape or a click anywhere else closes it.
// Markers: one size (kMarkerD), one glyph style, one place: centred on the top-right corner of the control's outline, moved
// kMarkerDx right and kMarkerDy up (the outline's corner is kept free of labels when the targets are chosen).
// The open popover is painted after every marker, so no marker ever lies over its text; it sits beside its control (never on it,
// never on its marker), inside the panel, with its pointer touching the control near the marker.
#pragma once
#include "Widgets.h"

namespace ru8ui
{
class HelpOverlay : public juce::Component
{
public:
    static constexpr int kMarkerD = 20, kMarkerDx = 2, kMarkerDy = -6;
    HelpOverlay (Ctx&, std::function<void()> onClosed);
    ~HelpOverlay() override;

    void open (int at = 0);
    void close();
    bool isOpen() const noexcept { return opened; }
    int count() const noexcept { return (int) entries.size(); }
    int current() const noexcept { return at; }
    void openTip (int i);                                   // what a click on marker i does
    void step (int d) { openTip (at + d); }
    juce::Rectangle<int> tipBounds() const noexcept { return tipRect; }
    juce::Rectangle<int> markerBounds (int i) const;
    juce::String titleOf (int i) const { return entries[(size_t) i].title; }
    juce::Rectangle<int> targetOf (int i) const { return entries[(size_t) i].target; }
    juce::Point<float> arrowTipPoint() const noexcept { return arrowTip; }
    bool drawMarkersForTest = true;                         // tests: the popover's pixels are the same with and without the markers

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    struct Entry { juce::String title, text; juce::Rectangle<int> target; char side; };
    struct Item { enum Kind { word, icon, key } kind; juce::String text; float w; bool glued = false; };
    void build();
    void layoutTip();
    void paintMarker (juce::Graphics&, int i) const;
    int markerAt (juce::Point<int>) const;

    Ctx& ctx;
    std::function<void()> closedCb;
    std::vector<Entry> entries;
    std::vector<juce::Rectangle<int>> markers;
    bool opened = false;
    int at = 0;
    juce::Rectangle<int> tipRect, prevRect, nextRect;
    juce::Point<float> arrowTip;     // where the popover's pointer touches the control
    char arrowSide = 'b';
    float arrowAt = 0, arrowLen = 7.0f;
    std::vector<std::vector<Item>> lines;
    float titleH = 20.0f, lineH = 18.1f;
};
} // namespace ru8ui
