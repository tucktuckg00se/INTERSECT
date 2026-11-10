#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <optional>

/**
    Runs a helper program (yt-dlp, ffmpeg, qjs) with an argument list, never through a shell, and
    reads its merged stdout + stderr line by line.

    Unlike juce::ChildProcess it stops everything it started: on POSIX the program gets its own
    process group and terminate() signals the whole group (SIGTERM, then SIGKILL); on Windows it
    runs in a job object, so terminate(), or INTERSECT closing or crashing, ends the program and
    whatever it launched. The program inherits none of the host's open files or sockets, reads
    stdin from /dev/null (NUL), opens no console window, and runs with PYTHON* variables removed
    and PYTHONIOENCODING=utf-8 (yt-dlp is a Python program).
*/
class ToolProcess
{
public:
    ToolProcess();
    ~ToolProcess();   // terminates a program that is still running

    /** False with error set when the program can't be started. */
    bool start (const juce::File& executable, const juce::StringArray& args, juce::String& error);
    bool isStarted() const noexcept;

    enum class ReadResult
    {
        line,       // `line` holds the next line of output (no line ending)
        timeout,    // nothing new within timeoutMs
        finished,   // the program closed its output and every line has been returned
    };
    ReadResult readLine (juce::String& line, int timeoutMs);

    /** The exit code once the program has ended (waits up to timeoutMs), or nullopt while it runs.
        -1 when the code can't be known (the host ignores SIGCHLD, so the OS reaped it). */
    std::optional<int> waitForExit (int timeoutMs);

    /** Stops the program and everything it started. Safe to call more than once. */
    void terminate();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE (ToolProcess)
};
