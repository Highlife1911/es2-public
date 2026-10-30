#ifndef ES2_SHELL_ENGINE_IO_H
#define ES2_SHELL_ENGINE_IO_H

/* The thin file layer of the own shell over es2/shell/engine.h, and
 * the optional evidence log (docs/reverse-engineering/launcher.md, "Shell
 * state engine"). Files are relative to the game directory (the
 * process's current directory, as VSHELL's): DATA\ and SAV\; the archives
 * come from cd_root (VOL\ZONES.VOL, SHELL0.VOL, LANG0.VOL), VSHELL's tables
 * from the span store (es2/shell/vshell_store.h; VER95\VSHELL.EXE is not
 * opened).
 *
 * The evidence log is a directory of campaign records in the schema of
 * shell/tools/es2chain.py (campaign-NNN\chain.json, chain.log, the SAV after
 * every step, each mission's four files and DBSIM's two result files, and
 * `current`). It is written when a log directory is set and never read to
 * decide anything the shell does, except which record a step is appended
 * to. */
#include "es2/shell/engine.h"
#include "es2/formats/vol.h"
#include <stdint.h>

#define EIO_PATH 520

typedef struct eio {
    /* configuration (set by the caller before eio_start) */
    char cd_root[EIO_PATH], game[EIO_PATH], core[EIO_PATH], log_dir[EIO_PATH];
    char language[4];
    int lowest_free;            /* menu.ini hangar = lowest-free (the default) */
    int state;                  /* the -X value of this start */
    /* engine and the CD (opened at the first use) */
    es2_shell_engine engine;
    es2_shell_engine_tables tables;
    es2_shell_hardpoints hardpoints;
    uint8_t *vols[3];
    es2_vol vol[3];
    es2_vfs_source sources[3];
    es2_vfs vfs;
    int opened, failed;
    uint16_t draws;
    char error[200];            /* the last failure, for the screen */
    /* the evidence log: the campaign record steps are appended to */
    void *chain;                /* parsed chain.json, or NULL */
    char campaign[EIO_PATH];
} eio;

/* Start of the process: the draw count and mode word, then the CD is opened
 * and the engine started (es2_shell_engine_start). Without a CD every later
 * operation fails with x->error; slot loads (es2_shell_engine_load) work. */
void eio_start(eio *x, uint16_t draws, int16_t mode);
int eio_open(eio *x);           /* 1 when the CD tables and archives are ready */
void eio_close(eio *x);

/* Files of the game directory. */
uint8_t *eio_read(const char *path, size_t *size);
int eio_write(const char *path, const uint8_t *data, size_t size);
/* The preparation's writers 0x41b278/0x418014: rewritten from offset 0
 * without truncation (file_io.md). */
int eio_write_over(const char *path, const uint8_t *data, size_t size);

/* New career (Registration ACCEPT) and the Use Default preparation that
 * follows it; the four files go to DATA. With `defer` (-@, the DEBUG
 * dialog waits) only the new game; the preparation follows through
 * eio_prepare_next or eio_prepare_mission. 1 on success. */
int eio_new_game(eio *x, const char *name, uint16_t rank, uint32_t *salvage, int defer);
/* After a slot load (memory state and final word already the slot's, the
 * companions copied): a slot with final word 2 whose companions were missing
 * is prepared from its CAREER position (the own menu's deviation for slots the
 * shell did not write); `slot_path` and `companions` describe the slot for
 * the log. 1 on success. */
int eio_loaded(eio *x, const char *slot_path, const uint8_t *file, size_t size, int companions);
/* Rock & Roll: launch checks; on success DATA\player.mec and mission.var of
 * 0x40f68c. Returns 1 launched, 0 refused or failed; *warning gets the box
 * index (ES2_SHELL_LAUNCH_OK when none). */
int eio_launch(eio *x, int *warning);
/* -X3/-X4 after the slot load: the results path over DATA\mission.var and
 * DATA\results.dat, the debrief's MISSION.STR, and with return 2 the next
 * preparation. *out (zeroed) keeps the results; results/size receive a copy
 * of DATA\results.dat (caller frees). Returns 1, or 0 with x->error set. */
int eio_results(eio *x, const uint8_t *slot, size_t slot_size, uint8_t repair, uint8_t restock,
    es2_shell_results_output *out, uint8_t **results, size_t *results_size);
int eio_prepare_next(eio *x);
/* The DEBUG dialog's ACCEPT in mode 1 (0x44dc4d): msn\<mission>.msn. */
int eio_prepare_mission(eio *x, const char *mission);
/* Service bay operations on the memory state (logged). */
int eio_bay(eio *x, es2_shell_bay_op *op);
/* A start without results (-X1, -X6, ...) after a launch: logged only;
 * 1 when the log had a mission with DBSIM. */
int eio_abort(eio *x);
/* The Replay box's Yes after the slot-10 load (logged only). */
int eio_replay(eio *x);
/* CAREER mission name of the memory state ("-" without one). */
void eio_mission_name(eio *x, char *out, size_t capacity);

#endif
