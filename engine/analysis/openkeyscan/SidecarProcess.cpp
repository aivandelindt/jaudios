#include "SidecarProcess.h"

#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
 #include <cerrno>
 #include <csignal>
 #include <cstring>
 #include <fcntl.h>
 #include <sys/socket.h>
 #include <sys/wait.h>
 #include <unistd.h>
extern char** environ;
 #define JAUDIOS_POSIX_SIDECAR 1
#else
 #define JAUDIOS_POSIX_SIDECAR 0
#endif

#include <vector>

namespace jaudios::analysis::openkeyscan
{

SidecarProcess::~SidecarProcess() { stop(); }

#if JAUDIOS_POSIX_SIDECAR

namespace
{
void closeFd (int& fd)
{
    if (fd >= 0)
        ::close (fd);
    fd = -1;
}

void setCloexec (int fd) { ::fcntl (fd, F_SETFD, ::fcntl (fd, F_GETFD) | FD_CLOEXEC); }
} // namespace

bool SidecarProcess::start (const juce::File& executable, const juce::StringArray& arguments,
                            const std::map<juce::String, juce::String>& extraEnvironment, juce::String& error)
{
    stop();

    if (! executable.existsAsFile())
    {
        error = "executable not found: " + executable.getFullPathName();
        return false;
    }

    // argv / envp prepared before fork (no allocation in the child)
    std::vector<std::string> argStrings { executable.getFullPathName().toStdString() };
    for (const auto& a : arguments)
        argStrings.push_back (a.toStdString());

    std::vector<std::string> envStrings;
    for (char** e = environ; e != nullptr && *e != nullptr; ++e)
    {
        const std::string entry (*e);
        const auto name = entry.substr (0, entry.find ('='));
        if (extraEnvironment.find (juce::String (name)) == extraEnvironment.end())
            envStrings.push_back (entry);
    }
    for (const auto& [k, v] : extraEnvironment)
        envStrings.push_back ((k + "=" + v).toStdString());

    std::vector<char*> argv, envp;
    for (auto& s : argStrings) argv.push_back (s.data());
    argv.push_back (nullptr);
    for (auto& s : envStrings) envp.push_back (s.data());
    envp.push_back (nullptr);

    int inPair[2], outPipe[2], errPipe[2], execStatus[2];
    if (::socketpair (AF_UNIX, SOCK_STREAM, 0, inPair) != 0 || ::pipe (outPipe) != 0 || ::pipe (errPipe) != 0
        || ::pipe (execStatus) != 0)
    {
        error = juce::String ("pipe creation failed: ") + juce::String (std::strerror (errno));
        return false;
    }
    setCloexec (execStatus[1]);

   #if JUCE_MAC
    int one = 1;
    ::setsockopt (inPair[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof (one));
   #endif

    const pid_t child = ::fork();
    if (child < 0)
    {
        error = juce::String ("fork failed: ") + juce::String (std::strerror (errno));
        for (int fd : { inPair[0], inPair[1], outPipe[0], outPipe[1], errPipe[0], errPipe[1], execStatus[0], execStatus[1] })
            ::close (fd);
        return false;
    }

    if (child == 0)
    {
        // child: async-signal-safe calls only
        ::dup2 (inPair[1], STDIN_FILENO);
        ::dup2 (outPipe[1], STDOUT_FILENO);
        ::dup2 (errPipe[1], STDERR_FILENO);
        for (int fd : { inPair[0], inPair[1], outPipe[0], outPipe[1], errPipe[0], errPipe[1], execStatus[0] })
            ::close (fd);
        ::setpgid (0, 0); // own process group: stop() can take down helper processes too
        ::execve (argv[0], argv.data(), envp.data());
        const int err = errno;
        [[maybe_unused]] auto n = ::write (execStatus[1], &err, sizeof (err));
        ::_exit (127);
    }

    ::close (inPair[1]);
    ::close (outPipe[1]);
    ::close (errPipe[1]);
    ::close (execStatus[1]);

    int execErrno = 0;
    const auto n = ::read (execStatus[0], &execErrno, sizeof (execErrno)); // EOF (0) = exec succeeded
    ::close (execStatus[0]);
    if (n > 0)
    {
        ::waitpid (child, nullptr, 0);
        ::close (inPair[0]);
        ::close (outPipe[0]);
        ::close (errPipe[0]);
        error = "cannot execute " + executable.getFullPathName() + ": " + juce::String (std::strerror (execErrno));
        return false;
    }

    pid = child;
    stdinFd = inPair[0];
    stdoutFd = outPipe[0];
    stderrFd = errPipe[0];
    setCloexec (stdinFd);
    setCloexec (stdoutFd);
    setCloexec (stderrFd);
    stdoutClosed = false;

    stdoutReader = std::thread ([this] { readerLoop (stdoutFd, true); });
    stderrReader = std::thread ([this] { readerLoop (stderrFd, false); });
    return true;
}

void SidecarProcess::readerLoop (int fd, bool isStdout)
{
    std::string pending;
    char buf[4096];
    for (;;)
    {
        const auto n = ::read (fd, buf, sizeof (buf));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;

        pending.append (buf, (size_t) n);
        size_t newline;
        while ((newline = pending.find ('\n')) != std::string::npos)
        {
            const auto line = juce::String::fromUTF8 (pending.data(), (int) newline).trimEnd();
            pending.erase (0, newline + 1);
            const auto& callback = isStdout ? onStdoutLine : onStderrLine;
            if (callback)
                callback (line);
        }
    }

    if (isStdout)
    {
        stdoutClosed = true;
        if (onStdoutClosed)
            onStdoutClosed();
    }
}

bool SidecarProcess::writeLine (const juce::String& line)
{
    if (stdinFd < 0)
        return false;

    const std::string data = line.toStdString() + "\n";
    size_t written = 0;
    while (written < data.size())
    {
       #if JUCE_MAC
        const auto n = ::send (stdinFd, data.data() + written, data.size() - written, 0);
       #else
        const auto n = ::send (stdinFd, data.data() + written, data.size() - written, MSG_NOSIGNAL);
       #endif
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        written += (size_t) n;
    }
    return true;
}

void SidecarProcess::stop (int graceMs)
{
    if (pid > 0)
    {
        closeFd (stdinFd); // EOF on stdin: a well-behaved server exits on its own
        ::kill (-pid, SIGTERM);

        int status = 0;
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) graceMs;
        bool reaped = false;
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            if (::waitpid (pid, &status, WNOHANG) == pid)
            {
                reaped = true;
                break;
            }
            juce::Thread::sleep (10);
        }
        if (! reaped)
        {
            ::kill (-pid, SIGKILL);
            ::waitpid (pid, &status, 0);
        }
        pid = -1;
    }

    if (stdoutReader.joinable()) stdoutReader.join();
    if (stderrReader.joinable()) stderrReader.join();
    closeFd (stdinFd);
    closeFd (stdoutFd);
    closeFd (stderrFd);
}

#else // Windows: not implemented yet

bool SidecarProcess::start (const juce::File&, const juce::StringArray&, const std::map<juce::String, juce::String>&,
                            juce::String& error)
{
    error = "sidecar process launching is not implemented on this platform yet";
    return false;
}

void SidecarProcess::readerLoop (int, bool) {}
bool SidecarProcess::writeLine (const juce::String&) { return false; }
void SidecarProcess::stop (int) {}

#endif

} // namespace jaudios::analysis::openkeyscan
