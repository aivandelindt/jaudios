#include "ExternalDrag.h"

namespace jaudios::gui
{

bool dragFilesOut (const juce::StringArray& files, juce::Component& source, std::function<void()> onFinished)
{
    if (files.isEmpty())
        return false;
    return juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false /* copy, never move */, &source, std::move (onFinished));
}

} // namespace jaudios::gui
