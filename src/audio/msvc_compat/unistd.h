#pragma once
// Stub unistd.h for MSVC — Bungee only uses getpid()/sleep() in BUNGEE_PETRIFY mode
#ifdef _MSC_VER
#  include <io.h>
#  include <process.h>
#  define sleep(x) _sleep((x) * 1000)
#endif
