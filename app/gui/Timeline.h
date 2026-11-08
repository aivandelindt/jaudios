#pragma once

#include <juce_events/juce_events.h>

namespace jaudios::gui
{

/** The visible part of the song shared by every lane: [start, start + length) seconds over the
    lanes' width. Zoom keeps the time under the pointer in place (pinch, Cmd+scroll); scroll moves
    the window. Message thread; lanes listen for changes. */
class TimelineModel final : public juce::ChangeBroadcaster
{
public:
    static constexpr double minLength = 0.5;   // seconds across the lanes at most zoom

    void setDuration (double seconds);           // shows everything
    void setWidth (int pixels);
    double duration() const noexcept { return total; }
    double start() const noexcept { return viewStart; }
    double length() const noexcept { return viewLength; }
    int width() const noexcept { return pixels; }

    float toX (double seconds) const noexcept;
    double toSeconds (float x) const noexcept;
    double secondsPerPixel() const noexcept { return viewLength / std::max (1, pixels); }

    /** factor > 1 zooms in. The time at `anchorX` stays at `anchorX`. */
    void zoom (double factor, float anchorX);
    void scrollBy (double seconds);
    void showAll();
    /** Scrolls just enough to keep `seconds` in view (follows the playhead). */
    void reveal (double seconds);

private:
    void clampAndNotify (double newStart, double newLength);

    double total = 1.0, viewStart = 0.0, viewLength = 1.0;
    int pixels = 1;
};

} // namespace jaudios::gui
