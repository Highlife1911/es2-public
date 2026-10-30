/* The tick record's line formatting (tick_log.md schema 2 and 3; own_process.md
 * section 9), shared by the proxy (tools/reference-game/proxy/tick_log.h) and
 * the own process (sim/). Pure C17: every value is passed in; the caller reads
 * guest memory (the proxy from the game's image, the own process through its
 * guest view) and owns the I/O. Each function writes at `out` (capacity
 * `cap`, which the caller sizes as the proxy does: TICK_LOG_LINE_MAX) and
 * returns the number of characters written, like snprintf. The text is byte
 * for byte the proxy's; the proxy's tick_log_* and normalization fixtures
 * guard it. */
#ifndef ES2_SIM_ORACLE_TICK_RECORD_H
#define ES2_SIM_ORACLE_TICK_RECORD_H
#include <stddef.h>
#include <stdint.h>

#define ES2_TICK_RNG_SIZE 0x72u      /* one RNG stream, 0x4d2599 and 0x4d260b */
#define ES2_TICK_CLOCK_SIZE 24u      /* the timing block 0x4d3b64 */
#define ES2_TICK_CONTEXT_SIZE 0xc3u  /* the globals object 0x4d24bc (frame counter at +0xbf) */
#define ES2_TICK_ACTOR_SIZE 0x292u   /* the player actor's bytes read by the record */

/* `"` + 2 lowercase hex digits per byte + `"`. */
int es2_tick_hex(char *out, const uint8_t *bytes, size_t size);

/* The start line's fields after the record kind. */
typedef struct es2_tick_start {
    unsigned schema;                 /* 2, or 3 with compact RNG fields */
    const char *mode;                /* "in_process_boundary_hook" (proxy) */
    unsigned long pid, tid;
    uint32_t image_base, boundary;   /* boundary: the frame-counter increment 0x45f4ad */
    long long qpc_frequency;
    unsigned long record_limit;
    int actor_raw;
    unsigned watches;
} es2_tick_start;
/* The whole start line including CR LF. */
int es2_tick_record_start(char *out, size_t cap, const es2_tick_start *start);

/* The head of a tick line: event, sequence, qpc, ebx, frame counter,
 * completed clock, limiter flag, pause flag and the timing block, from the
 * globals object `context` (ES2_TICK_CONTEXT_SIZE bytes) and the timing block
 * `clock` (ES2_TICK_CLOCK_SIZE bytes). `compact` selects the schema 3 event. */
int es2_tick_record_head(char *out, size_t cap, int compact, unsigned long sequence, long long qpc,
                         uint32_t ebx, const uint8_t *context, const uint8_t *clock);

/* One RNG field (bank 0 = stream A, 1 = B). With `compact`, a stream equal to
 * `previous` while `saved` is set is written as "rng_<x>_same"; otherwise its
 * hex, and with `compact` the bytes are copied to `previous`. */
int es2_tick_record_rng(char *out, size_t cap, unsigned bank, const uint8_t *bytes, int compact, int saved,
                        uint8_t previous[ES2_TICK_RNG_SIZE]);

/* The player actor: its address, and when `actor` is not NULL (the caller
 * found ES2_TICK_ACTOR_SIZE readable bytes there) position, angles, speed and
 * throttle, plus the raw block with `actor_raw`. */
int es2_tick_record_actor(char *out, size_t cap, uint32_t address, const uint8_t *actor, int actor_raw);

/* The virtual-time counters (written only when virtual time is enabled). */
int es2_tick_record_vt(char *out, size_t cap, int intra, unsigned long limiter_polls, unsigned long stall_advances,
                       unsigned long late_polls, unsigned long intra_advances);

/* The input journal's counters (written only when the journal records or
 * replays). */
int es2_tick_record_input(char *out, size_t cap, int replay, unsigned long recorded, unsigned long injected,
                          unsigned long dropped, unsigned long fallbacks, unsigned long focus_dropped);

#endif
