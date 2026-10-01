#ifndef BB_SCPCB_SCPCB_H
#define BB_SCPCB_SCPCB_H

#include <bb/blitz/blitz.h>
#include <bb/audio/audio.h>

// Compatibility layer for the third-party Windows DLLs that SCP: Containment
// Breach declares as userlibs (FMOD 3, CPUid, BlitzMovie, zlibwapi, user32,
// kernel32). They are exposed as ordinary built-ins under the same names so the
// game's .bb sources and .decls files can stay untouched.

#include "commands.h"

#endif
