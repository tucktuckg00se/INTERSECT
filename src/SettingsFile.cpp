#include "SettingsFile.h"
#include <algorithm>

void SettingsValues::set (const juce::String& key, const juce::String& value)
{
    setValue (key, { false, juce::StringArray (value) });
}

void SettingsValues::setList (const juce::String& key, const juce::StringArray& items)
{
    if (items.isEmpty())
        remove (key);
    else
        setValue (key, { true, items });
}

void SettingsValues::setValue (const juce::String& key, const SettingsValue& value)
{
    for (auto& entry : entries)
    {
        if (entry.first == key)
        {
            entry.second = value;
            return;
        }
    }
    entries.emplace_back (key, value);
}

void SettingsValues::remove (const juce::String& key)
{
    entries.erase (std::remove_if (entries.begin(), entries.end(),
                                   [&key] (const auto& entry) { return entry.first == key; }),
                   entries.end());
}

const SettingsValue* SettingsValues::find (const juce::String& key) const
{
    for (const auto& entry : entries)
        if (entry.first == key)
            return &entry.second;
    return nullptr;
}

juce::String SettingsValues::getString (const juce::String& key, const juce::String& fallback) const
{
    const auto* value = find (key);
    if (value == nullptr || value->isList || value->items.isEmpty())
        return fallback;
    return value->items[0].trim();
}

juce::StringArray SettingsValues::getList (const juce::String& key) const
{
    const auto* value = find (key);
    return value != nullptr && value->isList ? value->items : juce::StringArray();
}

SettingsValues SettingsValues::parse (const juce::String& text)
{
    SettingsValues values;
    juce::String listKey;   // the list whose "  - item" lines are being read
    juce::StringArray listItems;

    auto finishList = [&]
    {
        if (listKey.isNotEmpty())
            values.setValue (listKey, { true, listItems });
        listKey.clear();
        listItems.clear();
    };

    for (const auto& rawLine : juce::StringArray::fromLines (text))
    {
        const auto line = rawLine.trim();
        const bool indented = rawLine.startsWithChar (' ') || rawLine.startsWithChar ('\t');

        if (listKey.isNotEmpty())
        {
            if (line.startsWithChar ('-'))
            {
                const auto item = line.substring (1).trim();
                if (item.isNotEmpty())
                    listItems.addIfNotAlreadyThere (item);
                continue;
            }
            if (indented || line.isEmpty())
                continue;
            finishList();
        }

        if (line.isEmpty() || line.startsWithChar ('#') || ! line.containsChar (':'))
            continue;

        // The key ends at the first colon, so values may contain colons (C:\..., URLs).
        const auto key = line.upToFirstOccurrenceOf (":", false, false).trim();
        const auto value = line.fromFirstOccurrenceOf (":", false, false).trim();
        if (key.isEmpty())
            continue;

        if (value.isEmpty())
            listKey = key;   // "key:" starts a list
        else
            values.set (key, value);
    }

    finishList();
    return values;
}

juce::String SettingsValues::serialize() const
{
    juce::String text;
    for (const auto& [key, value] : entries)
    {
        if (value.isList)
        {
            text << key << ":\n";
            for (const auto& item : value.items)
                text << "  - " << item << "\n";
        }
        else
        {
            text << key << ": " << value.items[0] << "\n";
        }
    }
    return text;
}

SettingsValues SettingsValues::merge (const SettingsValues& base, const SettingsValues& mine, const SettingsValues& theirs)
{
    SettingsValues result;

    auto decide = [&] (const juce::String& key)
    {
        const auto* mineValue = mine.find (key);
        const auto* baseValue = base.find (key);
        const bool changedHere = (mineValue == nullptr) != (baseValue == nullptr)
                              || (mineValue != nullptr && *mineValue != *baseValue);
        const auto* chosen = changedHere ? mineValue : theirs.find (key);
        if (chosen != nullptr)
            result.setValue (key, *chosen);
    };

    for (const auto& entry : mine.entries)
        decide (entry.first);
    for (const auto& entry : theirs.entries)
        if (! mine.contains (entry.first))
            decide (entry.first);

    return result;
}

//==============================================================================

SettingsValues SettingsFile::readFile() const
{
    return file.existsAsFile() ? SettingsValues::parse (file.loadFileAsString()) : SettingsValues();
}

SettingsValues SettingsFile::load()
{
    base = readFile();
    return base;
}

bool SettingsFile::save (const SettingsValues& mine)
{
    const auto theirs = readFile();
    const auto merged = SettingsValues::merge (base, mine, theirs);
    base = mine;

    if (merged == theirs)
        return true;   // nothing new (most navigation saves in an unchanged folder)

    if (! file.getParentDirectory().createDirectory())
        return false;
    return file.replaceWithText (merged.serialize());   // via a temp file: readers never see half a file
}

juce::String SettingsFile::readValue (const juce::String& key) const
{
    return readFile().getString (key);
}
