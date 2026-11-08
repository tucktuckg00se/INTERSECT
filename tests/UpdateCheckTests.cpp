#include "UpdateCheck.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

void versionsCompare()
{
    using UpdateCheck::isNewer;
    require (isNewer ("0.19.1", "0.19.0"), "patch bump");
    require (isNewer ("0.20.0", "0.19.9"), "minor bump");
    require (isNewer ("1.0.0", "0.99.9"), "major bump");
    require (isNewer ("0.19.10", "0.19.9"), "numeric, not text, comparison");
    require (! isNewer ("0.19.0", "0.19.0"), "equal is not newer");
    require (! isNewer ("0.18.2", "0.19.0"), "older is not newer");
    require (isNewer ("v0.19.1", "0.19.0"), "leading v");
    require (! isNewer ("0.19.0-rc.1", "0.19.0"), "suffix ignored");
    require (isNewer ("0.19.0", "0.0.0"), "a build with no version sees any release as newer");
    require (isNewer ("0.19", "0.18.2"), "missing patch counts as 0");
}

void latestReleaseParses()
{
    const auto release = UpdateCheck::parseLatestRelease (R"({
        "tag_name": "v0.19.1",
        "name": "v0.19.1",
        "html_url": "https://github.com/tucktuckg00se/INTERSECT/releases/tag/v0.19.1",
        "draft": false,
        "prerelease": false
    })");
    require (release.has_value(), "valid release");
    require (release->version == "0.19.1", "version without the v");
    require (release->pageUrl == "https://github.com/tucktuckg00se/INTERSECT/releases/tag/v0.19.1", "page url");
}

void unusableReleasesAreRejected()
{
    require (! UpdateCheck::parseLatestRelease (R"({"tag_name":"v0.20.0","draft":true})").has_value(), "draft");
    require (! UpdateCheck::parseLatestRelease (R"({"tag_name":"v0.20.0","prerelease":true})").has_value(), "pre-release");
    require (! UpdateCheck::parseLatestRelease (R"({"message":"API rate limit exceeded"})").has_value(), "API error body");
    require (! UpdateCheck::parseLatestRelease ("not json").has_value(), "junk");
    require (! UpdateCheck::parseLatestRelease ("").has_value(), "empty");

    const auto noUrl = UpdateCheck::parseLatestRelease (R"({"tag_name":"v0.19.1"})");
    require (noUrl.has_value() && noUrl->pageUrl == UpdateCheck::kReleasesPageUrl, "missing page url falls back");
}
} // namespace

int main()
{
    int failed = 0;
    int passed = 0;
    const auto run = [&] (const char* name, auto test)
    {
        try
        {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

    run ("versions compare numerically", versionsCompare);
    run ("latest release parses", latestReleaseParses);
    run ("drafts, pre-releases and errors are rejected", unusableReleasesAreRejected);

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
