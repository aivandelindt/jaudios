#include "Theme.h"

namespace jaudios::gui
{

LookAndFeel::LookAndFeel()
    : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme (
          colours::background, colours::panel, colours::lane, colours::outline, colours::text, colours::laneAlt,
          colours::text, colours::source.darker (0.4f), colours::text))
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::TextButton::buttonColourId, colours::laneAlt);
    setColour (juce::TextButton::buttonOnColourId, colours::source.darker (0.3f));
    setColour (juce::ComboBox::backgroundColourId, colours::laneAlt);
    setColour (juce::ComboBox::outlineColourId, colours::outline);
    setColour (juce::Slider::thumbColourId, colours::source);
    setColour (juce::Slider::trackColourId, colours::source.darker (0.6f));
    setColour (juce::Slider::backgroundColourId, colours::barLine);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::source.darker (0.5f));
}

} // namespace jaudios::gui
