#ifndef BB_STUB_OVERLAY_NX_H
#define BB_STUB_OVERLAY_NX_H

// Installed-title mode. When the program runs from an NSP its read-only game files are in the
// title's romfs and its saves belong in device save data. The game opens everything relative to
// the current directory, so one device, "ov:", puts the two together:
//
//   read   save data first, then the romfs          (so a saved options.ini wins over the stock one)
//   write  save data only, parent directories are created on demand
//   list   the union of both
//
// Returns false (and does nothing) when there is no romfs, i.e. when started as an NRO from hbmenu.
// If the save data cannot be mounted the writable layer falls back to fallbackDir on the SD card.
bool nxOverlayInit( const char *fallbackDir );

// One line describing what nxOverlayInit did (romfs, save mount result), for the log.
const char *nxOverlayStatus();

// Flush pending save data writes; also done after every write-closed file and at exit.
void nxOverlayCommit();

#endif
