#pragma once

#include <juce_core/juce_core.h>

/** Minimal app log. Threshold from JAUDIOS_LOG_LEVEL = error | warning | info | debug (default info).
    Never call from the audio thread. */
namespace jaudios::log
{

enum class Level { error = 0, warning, info, debug };

bool enabled (Level) noexcept;
void write (Level, const juce::String& message);

inline void debug (const juce::String& m) { if (enabled (Level::debug)) write (Level::debug, m); }
inline void info (const juce::String& m) { write (Level::info, m); }
inline void warning (const juce::String& m) { write (Level::warning, m); }

} // namespace jaudios::log
