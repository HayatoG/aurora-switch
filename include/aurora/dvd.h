#ifndef AURORA_DVD_H
#define AURORA_DVD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <dolphin/dvd.h>
#include <dolphin/types.h>

/**
 * Open a GC/Wii disc image for use by the DVD API.
 * Must be called before DVDInit().
 * Returns true on success, false on failure.
 */
bool aurora_dvd_open(const char* disc_path);

/**
 * Read a GC/Wii disc image ID without changing the active DVD state.
 * Returns true on success, false on failure.
 */
bool aurora_dvd_get_disk_id(const char* disc_path, DVDDiskID* out_id);

/**
 * Close the disc image and free all resources.
 */
void aurora_dvd_close(void);

#ifdef __cplusplus
}
#endif

#endif
