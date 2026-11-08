#include "Timeline.h"

#include <algorithm>

namespace jaudios::gui
{

void TimelineModel::setDuration (double seconds)
{
    total = std::max (minLength, seconds);
    clampAndNotify (0.0, total);
}

void TimelineModel::setWidth (int w)
{
    if (w != pixels && w > 0)
    {
        pixels = w;
        sendChangeMessage();
    }
}

float TimelineModel::toX (double seconds) const noexcept
{
    return (float) ((seconds - viewStart) / viewLength * pixels);
}

double TimelineModel::toSeconds (float x) const noexcept
{
    return viewStart + (double) x / std::max (1, pixels) * viewLength;
}

void TimelineModel::zoom (double factor, float anchorX)
{
    if (factor <= 0.0)
        return;
    const double anchor = toSeconds (anchorX);
    const double newLength = std::clamp (viewLength / factor, std::min (minLength, total), total);
    clampAndNotify (anchor - (double) anchorX / std::max (1, pixels) * newLength, newLength);
}

void TimelineModel::scrollBy (double seconds) { clampAndNotify (viewStart + seconds, viewLength); }
void TimelineModel::showAll() { clampAndNotify (0.0, total); }

void TimelineModel::reveal (double seconds)
{
    if (seconds < viewStart)
        clampAndNotify (seconds, viewLength);
    else if (seconds > viewStart + viewLength)
        clampAndNotify (seconds - viewLength * 0.1, viewLength); // page on, keeping a little context
}

void TimelineModel::clampAndNotify (double newStart, double newLength)
{
    newLength = std::clamp (newLength, std::min (minLength, total), total);
    newStart = std::clamp (newStart, 0.0, total - newLength);
    if (newStart != viewStart || newLength != viewLength)
    {
        viewStart = newStart;
        viewLength = newLength;
        sendChangeMessage();
    }
}

} // namespace jaudios::gui
