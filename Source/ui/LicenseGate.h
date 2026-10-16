// RU-8 licence card (only compiled with RU8_LICENSE_REQUIRED=1; RU-8 has no store product id yet, so the flag is off in this build).
// The same plugin-common card KASET shows: it covers the panel until the product is activated; LICENSE reopens it.
// The audio pass-through of an unlicensed product belongs to the processor (KASET owns its svl::License there); this card only
// shows the state: the release build adds the processor side when the product ids exist.
#pragma once
#include "UiCommon.h"
#include "../../../subverselab-plugin-common/Licensing/LicenseOverlay.h"

namespace ru8ui
{
inline svl::LicenseConfig ru8LicenseConfig()
{
    svl::LicenseConfig c;
    c.product = "RU-8";
    c.buyUrl = "https://subverselab.com/launch";
    c.storeId = 0;                // TODO release build: the Lemon Squeezy store id (0 fails closed: every key is refused)
    return c;                     // TODO release build: productIds and svlProductIds
}

inline svl::License& ru8License()
{
    static svl::License lic { ru8LicenseConfig() };
    return lic;
}

class LicenseCard : public juce::Component
{
public:
    LicenseCard()
        : overlay (ru8License(), style())
    {
        addChildComponent (overlay);
        button.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        button.onClick = [this] { overlay.refresh(); overlay.setVisible (true); overlay.toFront (true); };
        addAndMakeVisible (button);
        overlay.setVisible (! ru8License().isLicensed());
    }
    void resized() override { overlay.setBounds (getLocalBounds()); button.setBounds (kW / 2 + 170, kH - 44, 70, 20); }
    bool hitTest (int x, int y) override { return overlay.isVisible() || button.getBounds().contains (x, y); }
    void tick()
    {
        if (! ru8License().isLicensed() && ! overlay.isVisible()) { overlay.refresh(); overlay.setVisible (true); overlay.toFront (true); }
    }
    svl::LicenseOverlay overlay;
    juce::TextButton button { "LICENSE" };

private:
    static svl::LicenseOverlay::Style style()
    {
        svl::LicenseOverlay::Style s;
        s.legend = [] (float size) { return display (size); };
        s.fine = [] (float size) { return heros (size); };
        s.mono = [] (float size) { return juce::Font (juce::FontOptions (juce::SharedResourcePointer<Faces>()->mono).withPointHeight (size * 0.88f)); };
        return s;
    }
};
} // namespace ru8ui
