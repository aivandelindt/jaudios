#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace jaudios::gui
{

/** JAudios' own look (ui-shell rule: an original design). Dark slate panels; one hue per part:
    teal for the source and stems, amber for chords, violet for bass, coral for drums. */
namespace colours
{
inline const juce::Colour background { 0xff12151a };
inline const juce::Colour panel { 0xff1a1e25 };
inline const juce::Colour lane { 0xff1f242c };
inline const juce::Colour laneAlt { 0xff232932 };
inline const juce::Colour outline { 0xff2e3540 };
inline const juce::Colour barLine { 0xff3a4250 };
inline const juce::Colour beatLine { 0xff262c35 };
inline const juce::Colour text { 0xffdde2ea };
inline const juce::Colour textDim { 0xff8a93a3 };
inline const juce::Colour source { 0xff3cc8b4 };
inline const juce::Colour chords { 0xfff2b45a };
inline const juce::Colour bass { 0xff9b8cff };
inline const juce::Colour drums { 0xffff7a6b };
inline const juce::Colour playhead { 0xffffffff };
inline const juce::Colour mute { 0xffe0a03c };
inline const juce::Colour solo { 0xff52c46f };
} // namespace colours

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
};

} // namespace jaudios::gui
