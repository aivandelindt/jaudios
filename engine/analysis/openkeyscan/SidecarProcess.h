#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <map>
#include <thread>

namespace jaudios::analysis::openkeyscan
{

/** A child process with stdin/stdout/stderr pipes.

    juce::ChildProcess cannot write to the child's stdin, which the NDJSON protocol needs, so this
    is a small POSIX implementation (fork/exec, socketpair for stdin so a dead child cannot
    SIGPIPE us). Windows is not implemented yet: start() fails and callers fall back to chroma.

    Callbacks run on internal reader threads. Analysis thread only; never the audio thread.
*/
class SidecarProcess
{
public:
    SidecarProcess() = default;
    ~SidecarProcess();

    std::function<void (const juce::String&)> onStdoutLine;
    std::function<void (const juce::String&)> onStderrLine;
    std::function<void()> onStdoutClosed; // child exited or closed stdout

    bool start (const juce::File& executable, const juce::StringArray& arguments,
                const std::map<juce::String, juce::String>& extraEnvironment, juce::String& error);

    /** SIGTERM, then SIGKILL after `graceMs`; joins the reader threads. Safe to call twice. */
    void stop (int graceMs = 500);

    bool isRunning() const noexcept { return pid > 0 && ! stdoutClosed.load(); }

    /** Writes `line` + '\n'. Returns false if the child is gone. */
    bool writeLine (const juce::String& line);

private:
    void readerLoop (int fd, bool isStdout);

    int pid = -1;
    int stdinFd = -1, stdoutFd = -1, stderrFd = -1;
    std::thread stdoutReader, stderrReader;
    std::atomic<bool> stdoutClosed { false };
};

} // namespace jaudios::analysis::openkeyscan
