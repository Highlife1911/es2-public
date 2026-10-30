#ifndef ES2_CORE_CONFIG_H
#define ES2_CORE_CONFIG_H
#include "es2/core/weapons.h"
/* DBSIM's configuration readers and the tape header writer/reader
 * as own code. Exact contract: docs/reverse-engineering/file_io.md,
 * section "configuration and tape files as own code".
 *
 * Every game and runtime call goes through g->call with the original's
 * EAX/EDX/ECX at the call (observer key: the callee VA; the prefs
 * callbacks use ES2_CONFIG_CALLBACK, a not-reproduced residue case
 * ES2_CONFIG_RESIDUE and an index or length the own code does not model
 * ES2_CONFIG_UNMODELED). The file I/O stays below the calls: the CW3220
 * stream functions (fopen 496802, fread 49684a, fwrite 496832, fclose
 * 496838, fseek 49682c, ftell 4967de, rewind 496772), the string
 * imports (_stricmp 496868, _atol 496880, _memset 49681a), KERNEL32's
 * GetPrivateProfileStringA (thunk 496910, stdcall), the game's path
 * builder 45ee00, its read and write stream classes (47ad40, 47af98,
 * 47ad78; 47b064, 47b2b8, 47b09c) and the runtime frame setup 49630c.
 *
 * The original's frame lives in g->scratch: ES2_CONFIG_SCRATCH bytes, the
 * synthetic EBP f = scratch + ES2_CONFIG_FRAME (all locals at the
 * original's EBP offsets: buffers, stream objects, the 49630c record).
 * The scratch must be zero-filled by the host for the documented residue
 * behaviour (the original reads its stack residue there instead).
 *
 * Results: EDX:EAX exact; *ecx (may be NULL) the original's exit ECX
 * (for prefs_video the dword at f-4, the original's [ebp-4]). For
 * prefs_load and prefs_store EAX is the record's saved FS:0 head, which
 * the caller writes back to FS:0 as the original's epilogue does. */
#define ES2_CONFIG_SCRATCH 0x200u
#define ES2_CONFIG_FRAME 0x1f8u
#define ES2_CONFIG_CALLBACK 0x4597f1u /* observer key: the indirect call of a prefs callback */
#define ES2_CONFIG_RESIDUE 1u         /* observer key: the original would use stack residue here */
#define ES2_CONFIG_UNMODELED 2u       /* observer key: an index or length the own code does not model */

/* 0x45e510(mode): the early video read (7 bytes of data\prefs.cfg unless
 * the done byte 49f254 is set) and the display-mode globals. */
uint64_t es2_prefs_video_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t mode,const uint32_t entry[3],uint32_t *ecx);
/* 0x459770(flag): the 54-byte prefs block from the prefs path 49e81c
 * through the read stream, the callbacks 4d1fdc, the committed and
 * previous copies. */
uint64_t es2_prefs_load_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t flag,const uint32_t entry[3],uint32_t *ecx);
/* 0x459b94(count, list): read-modify-write of the listed prefs bytes. */
uint64_t es2_prefs_store_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t count,uint32_t list,const uint32_t entry[3],uint32_t *ecx);
/* 0x45b7a8(): the four [Keyjoy] flags 49ea90..49ea9c. */
uint64_t es2_keyjoy_load_registers(const es2_guest_memory *,const es2_weapon_graph *,
    const uint32_t entry[3],uint32_t *ecx);
/* 0x46379c(object): the sound configuration constructor (data\sound.cfg). */
uint64_t es2_sound_config_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t object,const uint32_t entry[3],uint32_t *ecx);
/* 0x45cca4(tape, path): one tape header record (u32 length, the bytes in
 * pieces of at most 500). */
uint64_t es2_tape_save_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t tape,uint32_t path,const uint32_t entry[3],uint32_t *ecx);
/* 0x45cd5c(tape, path): one tape header record restored to path (NULL:
 * the bytes are skipped). */
uint64_t es2_tape_restore_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t tape,uint32_t path,const uint32_t entry[3],uint32_t *ecx);
#endif
