#include "DirectoryListing.h"

#if JUCE_LINUX || JUCE_MAC || JUCE_BSD
 #include <dirent.h>
 #include <sys/stat.h>
 #define INTERSECT_POSIX_LISTING 1
 // glibc says whether dirent has d_type; macOS and the BSDs always have it.
 #if defined (_DIRENT_HAVE_D_TYPE) || JUCE_MAC || JUCE_BSD
  #define INTERSECT_HAVE_D_TYPE 1
 #endif
#endif

namespace Browser
{
#if INTERSECT_POSIX_LISTING

std::vector<DirEntry> listDirectory (const juce::File& dir)
{
    std::vector<DirEntry> entries;
    const auto path = dir.getFullPathName();
    DIR* handle = opendir (path.toRawUTF8());
    if (handle == nullptr)
        return entries;

    const auto prefix = path.endsWithChar ('/') ? path : path + "/";

    while (const auto* item = readdir (handle))
    {
        const char* rawName = item->d_name;
        if (rawName[0] == '.' && (rawName[1] == 0 || (rawName[1] == '.' && rawName[2] == 0)))
            continue;   // "." and ".."

        DirEntry entry;
        entry.name = juce::String::fromUTF8 (rawName);

        bool typed = false;
       #if INTERSECT_HAVE_D_TYPE
        switch (item->d_type)
        {
            case DT_DIR:     entry.isDirectory = true; typed = true; break;
            case DT_LNK:     entry.isSymlink = true; break;   // the target's type needs a stat
            case DT_UNKNOWN: break;                           // filesystem doesn't say: stat below
            default:         typed = true; break;             // regular file, fifo, device...: not a folder
        }
       #endif

        if (! typed)
        {
            const auto fullPath = prefix + entry.name;
            struct stat info {};
            if (! entry.isSymlink && lstat (fullPath.toRawUTF8(), &info) == 0)
            {
                entry.isSymlink = S_ISLNK (info.st_mode);
                entry.isDirectory = S_ISDIR (info.st_mode);
            }
            if (entry.isSymlink && stat (fullPath.toRawUTF8(), &info) == 0)
                entry.isDirectory = S_ISDIR (info.st_mode);   // a link to a folder lists as a folder
        }

        entries.push_back (std::move (entry));
    }

    closedir (handle);
    return entries;
}

#else

// Windows: JUCE's iterator reads the attributes FindNextFile already returned, so isDirectory()
// is free here; what we drop is the second, separate isDirectory() call per entry.
std::vector<DirEntry> listDirectory (const juce::File& dir)
{
    std::vector<DirEntry> entries;
    for (const auto& item : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories,
                                                          juce::File::FollowSymlinks::no))
        entries.push_back ({ item.getFile().getFileName(), item.isDirectory(), false });
    return entries;
}

#endif
} // namespace Browser
