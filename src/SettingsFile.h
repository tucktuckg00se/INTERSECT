#pragma once
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

/** One settings.yaml value: a scalar ("key: value") or a list ("key:" then "  - item" lines). */
struct SettingsValue
{
    bool isList = false;
    juce::StringArray items;   // a scalar keeps its text in items[0]

    bool operator== (const SettingsValue& other) const { return isList == other.isList && items == other.items; }
    bool operator!= (const SettingsValue& other) const { return ! operator== (other); }
};

/** The keys of settings.yaml in file order. */
class SettingsValues
{
public:
    void set (const juce::String& key, const juce::String& value);
    /** An empty list removes the key (empty lists are not written). */
    void setList (const juce::String& key, const juce::StringArray& items);
    void setValue (const juce::String& key, const SettingsValue& value);
    void remove (const juce::String& key);

    bool contains (const juce::String& key) const { return find (key) != nullptr; }
    const SettingsValue* find (const juce::String& key) const;
    /** A scalar's trimmed text, or fallback when the key is missing or is a list. */
    juce::String getString (const juce::String& key, const juce::String& fallback = {}) const;
    /** A list's items (empty when missing or a scalar). */
    juce::StringArray getList (const juce::String& key) const;

    const std::vector<std::pair<juce::String, SettingsValue>>& getEntries() const noexcept { return entries; }

    bool operator== (const SettingsValues& other) const { return entries == other.entries; }
    bool operator!= (const SettingsValues& other) const { return ! operator== (other); }

    static SettingsValues parse (const juce::String& text);
    juce::String serialize() const;

    /** Per key: mine where it differs from base (this editor changed it since it last loaded or
        saved), otherwise theirs (the file as it is now). Added and removed keys count as changes.
        Order: mine's keys, then keys only theirs has. */
    static SettingsValues merge (const SettingsValues& base, const SettingsValues& mine, const SettingsValues& theirs);

private:
    std::vector<std::pair<juce::String, SettingsValue>> entries;
};

/**
    settings.yaml, shared by every plugin instance and host process on the machine.

    Editors used to rewrite the whole file from their own copy, so whichever closed last overwrote
    everyone else's changes (last folder, bookmarks...) with stale values. Now each editor keeps the
    values it last loaded or saved as a base, and save() writes only the keys it changed since then
    on top of what is in the file at that moment. Message thread only.
*/
class SettingsFile
{
public:
    explicit SettingsFile (juce::File fileToUse) : file (std::move (fileToUse)) {}

    /** Reads the file (empty if missing) and remembers it as the base for the next save. */
    SettingsValues load();
    /** Merges mine into the current file and writes it if anything changed. False if it couldn't
        be written (sandboxed host, read-only home); the in-memory settings still apply. */
    bool save (const SettingsValues& mine);
    /** One scalar read fresh from disk, e.g. a value another instance may have just written. */
    juce::String readValue (const juce::String& key) const;

    const juce::File& getFile() const noexcept { return file; }

private:
    SettingsValues readFile() const;

    juce::File file;
    SettingsValues base;
};
