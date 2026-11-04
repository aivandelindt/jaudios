#include "Log.h"

namespace jaudios::log
{

static Level threshold() noexcept
{
    static const Level level = []
    {
        const auto v = juce::SystemStats::getEnvironmentVariable ("JAUDIOS_LOG_LEVEL", "info").toLowerCase();
        if (v == "debug") return Level::debug;
        if (v == "warning") return Level::warning;
        if (v == "error") return Level::error;
        return Level::info;
    }();
    return level;
}

bool enabled (Level l) noexcept { return (int) l <= (int) threshold(); }

void write (Level l, const juce::String& message)
{
    if (! enabled (l))
        return;
    static const char* names[] = { "error", "warning", "info", "debug" };
    juce::Logger::outputDebugString ("[JAudios][" + juce::String (names[(int) l]) + "] " + message);
}

} // namespace jaudios::log
