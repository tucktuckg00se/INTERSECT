#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace Browser
{
struct DirEntry
{
    juce::String name;
    bool isDirectory = false;   // a symlink to a folder lists as a folder
    bool isSymlink = false;
};

/** The entries of dir (not recursive), typed from the directory listing itself where the OS
    provides it: readdir's d_type on Linux / macOS / BSD, FindNextFile's attributes on Windows.
    Only entries the listing can't type (symlinks, some network / FUSE filesystems) cost one stat,
    instead of the two per entry that findChildFiles + isDirectory() cost. Skips "." and "..".
    Empty if dir can't be read. Any thread. */
std::vector<DirEntry> listDirectory (const juce::File& dir);
} // namespace Browser
