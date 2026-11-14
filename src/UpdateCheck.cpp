#include "UpdateCheck.h"
#include "HttpFetch.h"
#include <array>

namespace
{
std::array<int, 3> versionParts (juce::String text)
{
    text = text.trim();
    if (text.startsWithIgnoreCase ("v"))
        text = text.substring (1);
    text = text.upToFirstOccurrenceOf ("-", false, false);

    std::array<int, 3> parts { 0, 0, 0 };
    const auto tokens = juce::StringArray::fromTokens (text, ".", {});
    for (int i = 0; i < (int) parts.size() && i < tokens.size(); ++i)
        parts[(size_t) i] = tokens[i].getIntValue();
    return parts;
}
} // namespace

namespace UpdateCheck
{
bool isNewer (const juce::String& candidate, const juce::String& current)
{
    return versionParts (candidate) > versionParts (current);
}

std::optional<Release> parseLatestRelease (const juce::String& json)
{
    const auto parsed = juce::JSON::parse (json);
    const auto* release = parsed.getDynamicObject();
    if (release == nullptr)
        return std::nullopt;

    if ((bool) release->getProperty ("draft") || (bool) release->getProperty ("prerelease"))
        return std::nullopt;

    auto tag = release->getProperty ("tag_name").toString().trim();
    if (tag.startsWithIgnoreCase ("v"))
        tag = tag.substring (1);
    if (tag.isEmpty() || ! juce::CharacterFunctions::isDigit (tag[0]))
        return std::nullopt;

    const auto page = release->getProperty ("html_url").toString().trim();
    return Release { tag, page.startsWith ("https://") ? page : juce::String (kReleasesPageUrl) };
}

juce::String makeIssueUrl (const IssueEnvironment& environment)
{
    auto url = juce::URL (kNewIssueUrl).withParameter ("template", "bug_report.yml");

    auto version = environment.version.trim();
    if (version.isNotEmpty() && ! version.startsWithIgnoreCase ("v"))
        version = "v" + version;

    const std::pair<const char*, juce::String> fields[] {
        { "version", version },
        { "os", environment.os.trim() },
        { "format", environment.format.trim() },
        { "architecture", environment.architecture.trim() },
        { "daw", environment.daw.trim() },
    };
    for (const auto& [id, value] : fields)
        if (value.isNotEmpty())
            url = url.withParameter (id, value);

    return url.toString (true);
}

juce::String currentOsLabel()
{
    const auto type = juce::SystemStats::getOperatingSystemType();
    if ((type & juce::SystemStats::Windows) != 0)
        return "Windows";
    if ((type & juce::SystemStats::MacOSX) != 0)
        return "macOS";
    if ((type & juce::SystemStats::Linux) != 0)
        return "Linux";
    return {};
}

juce::String currentArchitectureLabel()
{
   #if JUCE_ARM
    return "ARM/Apple Silicon";
   #elif JUCE_INTEL
    return "Intel/AMD";
   #else
    return {};
   #endif
}
} // namespace UpdateCheck

//==============================================================================

UpdateChecker::UpdateChecker()
    : juce::Thread ("UpdateCheck")
{
}

UpdateChecker::~UpdateChecker()
{
    stopThread (UpdateChecker::kTimeoutMs + 1000);
}

void UpdateChecker::checkIfStale (const juce::String& currentVersion, bool force)
{
    {
        const juce::ScopedLock sl (lock);
        if (status.state == State::checking || isThreadRunning())
            return;

        const bool recentEnough = lastSuccess != juce::Time()
            && (juce::Time::getCurrentTime() - lastSuccess).inMilliseconds() < kRecheckAfterMs
            && status.currentVersion == currentVersion;
        if (recentEnough && ! force)
            return;

        status.state = State::checking;
        status.currentVersion = currentVersion;
    }

    startThread();
}

UpdateChecker::Status UpdateChecker::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

void UpdateChecker::run()
{
    // GitHub's API rejects requests without a User-Agent.
    const auto json = fetchText (juce::URL (UpdateCheck::kLatestReleaseApiUrl), *this,
                                 "User-Agent: INTERSECT\r\nAccept: application/vnd.github+json", kTimeoutMs);
    if (threadShouldExit())
        return;

    const auto release = json.isNotEmpty() ? UpdateCheck::parseLatestRelease (json) : std::nullopt;

    const juce::ScopedLock sl (lock);
    if (! release.has_value())
    {
        status.state = State::failed;
        return;
    }

    lastSuccess = juce::Time::getCurrentTime();
    status.latestVersion = release->version;
    status.pageUrl = release->pageUrl;
    status.state = UpdateCheck::isNewer (release->version, status.currentVersion) ? State::updateAvailable
                                                                                   : State::upToDate;
}
