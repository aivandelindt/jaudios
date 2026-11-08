#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace jaudios::gui
{

/** Drags exported files out of the app, into a DAW or the Finder (export-and-drag-drop step 3).
    Call from a mouseDrag on `source`; the files should live in a DragSession folder (DAWs copy
    what is dropped on them). Returns false when the platform refuses to start the drag. */
bool dragFilesOut (const juce::StringArray& files, juce::Component& source, std::function<void()> onFinished = {});

} // namespace jaudios::gui
