#pragma once
// MSVC compatibility for Bungee sources
#ifdef _MSC_VER
#  ifndef __attribute__
#    define __attribute__(x)
#  endif
// Provide a stub unistd.h — the functions it uses (getpid, sleep)
// are only called in BUNGEE_PETRIFY mode which we don't enable.
#  ifndef _UNISTD_H
#    define _UNISTD_H
#    include <io.h>
#    include <process.h>
#  endif
#endif
