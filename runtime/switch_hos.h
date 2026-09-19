/* GPL-2.0-or-later */
/* Switch HOS storage for the kisaku runtime.
 *
 * A direct-install NSP keeps the read-only game data in the title RomFS and
 * writable state in HOS-managed SaveData.  switch_hos_init() mounts both and
 * reports the two directory prefixes; the runtime then reads game data from
 * data_dir and keeps every save / flag / history file under save_dir.
 *
 * When neither mount is available (NRO launched from hbmenu) the caller keeps
 * its SD-card layout, so the same binary still works as a homebrew NRO.
 */
#pragma once
#include <stddef.h>

/* Non-zero when the title RomFS was mounted as "romfs:". */
extern int switch_romfs_active;

/* Non-zero when HOS SaveData was mounted as "save:".  When zero (NRO, or the
 * mount failed) saves stay wherever the caller pointed save_dir. */
extern int switch_save_active;

/* Where saves ended up, for diagnostics: "save:", the SD fallback path used
 * when an installed title could not mount SaveData, or "" for the NRO layout. */
extern const char *switch_save_reason;

/* Mount RomFS (when this process is an installed title) and HOS SaveData.
 * data_dir keeps its caller-provided value when no RomFS exists.  When RomFS
 * exists but SaveData does not mount, save_dir is pointed at a writable
 * SD-card directory instead of the read-only game-data directory. */
void switch_hos_init(char *data_dir, size_t data_dir_size, char *save_dir, size_t save_dir_size);

/* Flush pending SaveData writes.  Cheap and idempotent; safe to call when no
 * SaveData is mounted. */
void switch_hos_commit(void);
