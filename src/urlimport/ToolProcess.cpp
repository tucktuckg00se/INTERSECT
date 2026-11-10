#include "ToolProcess.h"
#include <string>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <cerrno>
 #include <cstring>
 #include <fcntl.h>
 #include <poll.h>
 #include <signal.h>
 #include <spawn.h>
 #include <sys/wait.h>
 #include <unistd.h>
 #if JUCE_MAC
  #include <crt_externs.h>   // plugins are bundles, which can't link `environ` directly
 #else
  extern char** environ;
 #endif
#endif

namespace
{
/** Pops one '\n'-terminated line off buffer (dropping a trailing '\r'). */
bool takeLine (std::string& buffer, juce::String& line)
{
    const auto newline = buffer.find ('\n');
    if (newline == std::string::npos)
        return false;

    auto length = newline;
    if (length > 0 && buffer[length - 1] == '\r')
        --length;
    line = juce::String::fromUTF8 (buffer.data(), (int) length);
    buffer.erase (0, newline + 1);
    return true;
}

bool isPythonVariable (const juce::String& entry)
{
    return entry.startsWithIgnoreCase ("PYTHON");
}

constexpr const char* kUtf8Output = "PYTHONIOENCODING=utf-8";
} // namespace

#if JUCE_WINDOWS
//==============================================================================
namespace
{
/** One argument quoted the way CommandLineToArgvW and the MSVC runtime split it again. */
std::wstring quoteArgument (const juce::String& argument)
{
    const std::wstring arg (argument.toWideCharPointer());
    if (! arg.empty() && arg.find_first_of (L" \t\n\v\"") == std::wstring::npos)
        return arg;

    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (const auto c : arg)
    {
        if (c == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (c == L'"')
            out.append (backslashes * 2 + 1, L'\\');   // escape the backslashes and the quote
        else
            out.append (backslashes, L'\\');
        out.push_back (c);
        backslashes = 0;
    }
    out.append (backslashes * 2, L'\\');   // before the closing quote, backslashes must be doubled
    out.push_back (L'"');
    return out;
}

/** The current environment without PYTHON* variables, plus UTF-8 output, as a CreateProcess block. */
std::vector<wchar_t> makeEnvironmentBlock()
{
    std::vector<wchar_t> block;
    if (auto* strings = GetEnvironmentStringsW())
    {
        for (const wchar_t* entry = strings; *entry != 0; entry += wcslen (entry) + 1)
        {
            if (isPythonVariable (juce::String (entry)))
                continue;
            block.insert (block.end(), entry, entry + wcslen (entry) + 1);
        }
        FreeEnvironmentStringsW (strings);
    }
    const std::wstring utf8 (juce::String (kUtf8Output).toWideCharPointer());
    block.insert (block.end(), utf8.c_str(), utf8.c_str() + utf8.size() + 1);
    block.push_back (0);
    return block;
}

juce::String describeWindowsError (DWORD code)
{
    if (code == 225 /* ERROR_VIRUS_INFECTED */ || code == 226 /* ERROR_VIRUS_DELETED */)
        return "Windows Security blocked it as a threat";

    wchar_t* text = nullptr;
    FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, code, 0, (LPWSTR) &text, 0, nullptr);
    juce::String message = text != nullptr ? juce::String (text).trim() : juce::String();
    if (text != nullptr)
        LocalFree (text);
    return message.isNotEmpty() ? message + " (" + juce::String ((int) code) + ")" : "error " + juce::String ((int) code);
}
} // namespace

struct ToolProcess::Impl
{
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    HANDLE readPipe = nullptr;
    bool exited = false;
    int exitCode = -1;
    bool eof = false;
    std::string buffer;

    ~Impl()
    {
        if (process != nullptr && ! exited)
            terminate();
        for (auto* handle : { process, job, readPipe })
            if (handle != nullptr)
                CloseHandle (handle);   // closing the job kills anything still in it
    }

    void terminate()
    {
        if (process == nullptr || exited)
            return;
        if (job != nullptr)
            TerminateJobObject (job, 1);
        else
            TerminateProcess (process, 1);
        if (WaitForSingleObject (process, 5000) == WAIT_OBJECT_0)
        {
            DWORD code = 1;
            GetExitCodeProcess (process, &code);
            exited = true;
            exitCode = (int) code;
        }
    }
};

ToolProcess::ToolProcess() : impl (std::make_unique<Impl>()) {}
ToolProcess::~ToolProcess() = default;

bool ToolProcess::isStarted() const noexcept { return impl->process != nullptr; }

bool ToolProcess::start (const juce::File& executable, const juce::StringArray& args, juce::String& error)
{
    if (isStarted())
    {
        error = "already started";
        return false;
    }

    SECURITY_ATTRIBUTES inheritable { sizeof (SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE writePipe = nullptr;
    if (! CreatePipe (&impl->readPipe, &writePipe, &inheritable, 0))
    {
        error = "couldn't create a pipe: " + describeWindowsError (GetLastError());
        return false;
    }
    SetHandleInformation (impl->readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul = CreateFileW (L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                              OPEN_EXISTING, 0, nullptr);

    // Only the pipe and NUL are inherited, not every inheritable handle the host has open.
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList (nullptr, 1, 0, &attributeSize);
    juce::HeapBlock<char> attributeStorage (attributeSize);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST> (attributeStorage.getData());
    InitializeProcThreadAttributeList (attributes, 1, 0, &attributeSize);
    HANDLE inherited[2] = { writePipe, nul };
    UpdateProcThreadAttribute (attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                               nul != INVALID_HANDLE_VALUE ? sizeof (inherited) : sizeof (HANDLE), nullptr, nullptr);

    STARTUPINFOEXW startup {};
    startup.StartupInfo.cb = sizeof (startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : nullptr;
    startup.StartupInfo.hStdOutput = writePipe;
    startup.StartupInfo.hStdError = writePipe;
    startup.lpAttributeList = attributes;

    std::wstring commandLine = quoteArgument (executable.getFullPathName());
    for (const auto& arg : args)
        commandLine += L" " + quoteArgument (arg);
    std::vector<wchar_t> commandBuffer (commandLine.begin(), commandLine.end());
    commandBuffer.push_back (0);
    auto environment = makeEnvironmentBlock();

    PROCESS_INFORMATION info {};
    const BOOL created = CreateProcessW (executable.getFullPathName().toWideCharPointer(), commandBuffer.data(),
                                         nullptr, nullptr, TRUE,
                                         CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT
                                             | CREATE_UNICODE_ENVIRONMENT,
                                         environment.data(), nullptr, &startup.StartupInfo, &info);
    const DWORD createError = created ? 0 : GetLastError();

    DeleteProcThreadAttributeList (attributes);
    CloseHandle (writePipe);   // the child has its copy; EOF arrives when it (and its children) close theirs
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle (nul);

    if (! created)
    {
        CloseHandle (impl->readPipe);
        impl->readPipe = nullptr;
        error = describeWindowsError (createError);
        return false;
    }

    // Started suspended so it is inside the job before it can launch anything.
    impl->job = CreateJobObjectW (nullptr, nullptr);
    if (impl->job != nullptr)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (! SetInformationJobObject (impl->job, JobObjectExtendedLimitInformation, &limits, sizeof (limits))
            || ! AssignProcessToJobObject (impl->job, info.hProcess))
        {
            CloseHandle (impl->job);   // fall back to terminating just the process
            impl->job = nullptr;
        }
    }

    ResumeThread (info.hThread);
    CloseHandle (info.hThread);
    impl->process = info.hProcess;
    return true;
}

ToolProcess::ReadResult ToolProcess::readLine (juce::String& line, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);
    for (;;)
    {
        if (takeLine (impl->buffer, line))
            return ReadResult::line;

        if (impl->eof || impl->readPipe == nullptr)
        {
            if (! impl->buffer.empty())
            {
                line = juce::String::fromUTF8 (impl->buffer.data(), (int) impl->buffer.size()).trimEnd();
                impl->buffer.clear();
                return ReadResult::line;
            }
            return ReadResult::finished;
        }

        DWORD available = 0;
        if (! PeekNamedPipe (impl->readPipe, nullptr, 0, nullptr, &available, nullptr))
        {
            impl->eof = true;   // ERROR_BROKEN_PIPE: every writer has closed
            continue;
        }

        if (available > 0)
        {
            char chunk[4096];
            DWORD bytesRead = 0;
            if (ReadFile (impl->readPipe, chunk, juce::jmin<DWORD> (available, (DWORD) sizeof (chunk)), &bytesRead, nullptr)
                && bytesRead > 0)
                impl->buffer.append (chunk, bytesRead);
            else
                impl->eof = true;
            continue;
        }

        if (juce::Time::getMillisecondCounter() >= deadline)
            return ReadResult::timeout;
        Sleep (15);
    }
}

std::optional<int> ToolProcess::waitForExit (int timeoutMs)
{
    if (impl->process == nullptr)
        return std::nullopt;
    if (! impl->exited && WaitForSingleObject (impl->process, (DWORD) juce::jmax (0, timeoutMs)) == WAIT_OBJECT_0)
    {
        DWORD code = 1;
        GetExitCodeProcess (impl->process, &code);
        impl->exited = true;
        impl->exitCode = (int) code;
    }
    return impl->exited ? std::optional<int> (impl->exitCode) : std::nullopt;
}

void ToolProcess::terminate()
{
    impl->terminate();
}

#else
//==============================================================================
struct ToolProcess::Impl
{
    pid_t pid = -1;
    int fd = -1;
    bool exited = false;
    int exitCode = -1;
    bool eof = false;
    std::string buffer;

    ~Impl()
    {
        if (pid > 0 && ! exited)
            terminate();
        if (fd >= 0)
            ::close (fd);
    }

    /** Reaps the child without blocking; true once it has ended. */
    bool poll()
    {
        if (exited || pid <= 0)
            return exited;

        int status = 0;
        const auto result = ::waitpid (pid, &status, WNOHANG);
        if (result == pid)
        {
            exited = true;
            exitCode = WIFEXITED (status) ? WEXITSTATUS (status)
                     : WIFSIGNALED (status) ? 128 + WTERMSIG (status) : -1;
        }
        else if (result < 0 && errno == ECHILD)
        {
            exited = true;   // the host ignores SIGCHLD, so the OS reaped it and the code is lost
            exitCode = -1;
        }
        return exited;
    }

    bool waitFor (int timeoutMs)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);
        while (! poll())
        {
            if (juce::Time::getMillisecondCounter() >= deadline)
                return false;
            juce::Thread::sleep (10);
        }
        return true;
    }

    void terminate()
    {
        if (pid <= 0)
            return;

        // The group id is the child's pid (POSIX_SPAWN_SETPGROUP with 0). Ask nicely, then insist;
        // the final SIGKILL also catches anything the program started that outlived it.
        if (! exited)
        {
            ::kill (-pid, SIGTERM);
            if (! waitFor (2000))
            {
                ::kill (-pid, SIGKILL);
                waitFor (2000);
            }
        }
        ::kill (-pid, SIGKILL);
    }
};

ToolProcess::ToolProcess() : impl (std::make_unique<Impl>()) {}
ToolProcess::~ToolProcess() = default;

bool ToolProcess::isStarted() const noexcept { return impl->pid > 0; }

bool ToolProcess::start (const juce::File& executable, const juce::StringArray& args, juce::String& error)
{
    if (isStarted())
    {
        error = "already started";
        return false;
    }

    int fds[2] = { -1, -1 };
   #if JUCE_LINUX
    if (::pipe2 (fds, O_CLOEXEC) != 0)
   #else
    if (::pipe (fds) != 0 || ::fcntl (fds[0], F_SETFD, FD_CLOEXEC) != 0 || ::fcntl (fds[1], F_SETFD, FD_CLOEXEC) != 0)
   #endif
    {
        error = "couldn't create a pipe: " + juce::String (std::strerror (errno));
        return false;
    }

    // argv and envp as UTF-8 C strings that stay alive across posix_spawn.
    std::vector<std::string> argStorage;
    argStorage.push_back (executable.getFullPathName().toStdString());
    for (const auto& arg : args)
        argStorage.push_back (arg.toStdString());
    std::vector<char*> argv;
    for (auto& arg : argStorage)
        argv.push_back (arg.data());
    argv.push_back (nullptr);

   #if JUCE_MAC
    char** const currentEnvironment = *_NSGetEnviron();
   #else
    char** const currentEnvironment = environ;
   #endif
    std::vector<std::string> envStorage;
    for (char** entry = currentEnvironment; entry != nullptr && *entry != nullptr; ++entry)
        if (! isPythonVariable (juce::String::fromUTF8 (*entry)))
            envStorage.emplace_back (*entry);
    envStorage.emplace_back (kUtf8Output);
    std::vector<char*> envp;
    for (auto& entry : envStorage)
        envp.push_back (entry.data());
    envp.push_back (nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init (&actions);
    posix_spawn_file_actions_addopen (&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2 (&actions, fds[1], 1);   // dup2 clears close-on-exec on 1 and 2
    posix_spawn_file_actions_adddup2 (&actions, fds[1], 2);
   #if defined (__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    posix_spawn_file_actions_addclosefrom_np (&actions, 3);   // nothing else from the host
   #endif

    posix_spawnattr_t attributes;
    posix_spawnattr_init (&attributes);
    short flags = POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
   #if JUCE_MAC
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;   // macOS: close every descriptor the actions don't name
   #endif
    posix_spawnattr_setflags (&attributes, flags);
    posix_spawnattr_setpgroup (&attributes, 0);   // its own process group, so terminate() reaches its children
    sigset_t noSignals;
    sigemptyset (&noSignals);
    posix_spawnattr_setsigmask (&attributes, &noSignals);   // the host thread may block signals
    sigset_t defaults;
    sigemptyset (&defaults);
    for (const int sig : { SIGPIPE, SIGTERM, SIGINT, SIGHUP, SIGCHLD })
        sigaddset (&defaults, sig);   // hosts often ignore SIGPIPE; the child must not inherit that
    posix_spawnattr_setsigdefault (&attributes, &defaults);

    pid_t pid = -1;
    const int result = ::posix_spawn (&pid, argStorage.front().c_str(), &actions, &attributes, argv.data(), envp.data());

    posix_spawn_file_actions_destroy (&actions);
    posix_spawnattr_destroy (&attributes);
    ::close (fds[1]);

    if (result != 0)
    {
        ::close (fds[0]);
        error = juce::String (std::strerror (result));
        return false;
    }

    impl->pid = pid;
    impl->fd = fds[0];
    return true;
}

ToolProcess::ReadResult ToolProcess::readLine (juce::String& line, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);
    for (;;)
    {
        if (takeLine (impl->buffer, line))
            return ReadResult::line;

        if (impl->eof || impl->fd < 0)
        {
            if (! impl->buffer.empty())
            {
                line = juce::String::fromUTF8 (impl->buffer.data(), (int) impl->buffer.size()).trimEnd();
                impl->buffer.clear();
                return ReadResult::line;
            }
            return ReadResult::finished;
        }

        const auto now = juce::Time::getMillisecondCounter();
        const int remaining = now >= deadline ? 0 : (int) (deadline - now);
        pollfd waiting { impl->fd, POLLIN, 0 };
        const int ready = ::poll (&waiting, 1, remaining);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready == 0)
            return ReadResult::timeout;

        char chunk[4096];
        const auto bytesRead = ::read (impl->fd, chunk, sizeof (chunk));
        if (bytesRead > 0)
            impl->buffer.append (chunk, (size_t) bytesRead);
        else if (bytesRead < 0 && errno == EINTR)
            continue;
        else
            impl->eof = true;   // 0: every writer closed; < 0: nothing more will come
    }
}

std::optional<int> ToolProcess::waitForExit (int timeoutMs)
{
    if (impl->pid <= 0)
        return std::nullopt;
    impl->waitFor (timeoutMs);
    return impl->exited ? std::optional<int> (impl->exitCode) : std::nullopt;
}

void ToolProcess::terminate()
{
    impl->terminate();
}
#endif
