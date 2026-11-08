#pragma once
#include <juce_core/juce_core.h>
#include <optional>

/** Is there a newer INTERSECT release? One request to GitHub's "latest release" endpoint, which
    already leaves drafts and pre-releases out. */
namespace UpdateCheck
{
constexpr auto kLatestReleaseApiUrl = "https://api.github.com/repos/tucktuckg00se/INTERSECT/releases/latest";
constexpr auto kReleasesPageUrl = "https://github.com/tucktuckg00se/INTERSECT/releases/latest";

struct Release
{
    juce::String version;   // "0.19.1" (no leading v)
    juce::String pageUrl;   // the release's page on GitHub
};

/** Numeric major.minor.patch comparison. Accepts a leading "v"; a "-suffix" (e.g. -rc.1) is ignored. */
bool isNewer (const juce::String& candidate, const juce::String& current);

/** tag_name + html_url from a releases/latest response; nothing for drafts, pre-releases or junk. */
std::optional<Release> parseLatestRelease (const juce::String& json);
} // namespace UpdateCheck

/**
    Background update check, shared by every instance in a host (held through a
    juce::SharedResourcePointer by the processor), so one look at GitHub serves them all.
*/
class UpdateChecker : private juce::Thread
{
public:
    enum class State
    {
        idle,
        checking,
        upToDate,
        updateAvailable,
        failed,
    };

    struct Status
    {
        State state = State::idle;
        juce::String currentVersion;
        juce::String latestVersion;
        juce::String pageUrl;
    };

    UpdateChecker();
    ~UpdateChecker() override;

    /** Starts a check unless one is running or the last successful one is under 6 hours old
        (force skips that wait). Message thread. */
    void checkIfStale (const juce::String& currentVersion, bool force);
    /** A copy, safe from any thread. */
    Status getStatus() const;

    static constexpr int kTimeoutMs = 5000;
    static constexpr juce::int64 kRecheckAfterMs = 6 * 60 * 60 * 1000;

private:
    void run() override;

    mutable juce::CriticalSection lock;
    Status status;
    juce::Time lastSuccess;
};
