/* The tick record's line formatting; see tick_record.h. The proxy's tick_log.h
 * writes its lines through it. */
#include "sim/oracle/tick_record.h"
#include <stdio.h>
#include <string.h>

static uint32_t tr_u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t tr_u16(const uint8_t *p) { return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8); }

int es2_tick_hex(char *out, const uint8_t *bytes, size_t size) {
    static const char digits[] = "0123456789abcdef";
    out[0] = '"';
    for (size_t i = 0; i < size; ++i) {
        out[1 + 2 * i] = digits[bytes[i] >> 4];
        out[2 + 2 * i] = digits[bytes[i] & 15];
    }
    out[1 + 2 * size] = '"';
    return (int)(2 + 2 * size);
}

int es2_tick_record_start(char *out, size_t cap, const es2_tick_start *s) {
    return snprintf(out, cap,
        "{\"event\":\"start\",\"schema\":%u,\"mode\":\"%s\",\"pid\":%lu,\"tid\":%lu,"
        "\"image_base\":\"0x%08lx\",\"boundary\":\"0x%08lx\",\"phase\":\"before_frame_counter_increment\","
        "\"qpc_frequency\":%lld,\"record_limit\":%lu,\"actor_raw\":%s,\"watches\":%u}\r\n",
        s->schema, s->mode, s->pid, s->tid, (unsigned long)s->image_base, (unsigned long)s->boundary,
        s->qpc_frequency, s->record_limit, s->actor_raw ? "true" : "false", s->watches);
}

int es2_tick_record_head(char *out, size_t cap, int compact, unsigned long sequence, long long qpc,
                         uint32_t ebx, const uint8_t *context, const uint8_t *clock) {
    int n = snprintf(out, cap,
        "{\"event\":\"%s\",\"sequence\":%lu,\"qpc\":%lld,\"ebx\":\"0x%08lx\",\"frame_counter_before_increment\":%lu,"
        "\"completed_clock_ms\":%lu,\"limiter_bypassed\":%s,\"loop_pause_flag\":%u,\"clock_raw_hex\":",
        compact ? "tick_compact" : "tick", sequence, qpc, (unsigned long)ebx,
        (unsigned long)tr_u32(context + 0xbf), (unsigned long)tr_u32(clock + 0x10),
        tr_u16(context + 0x1a) ? "true" : "false", (unsigned)context[0xb6]);
    n += es2_tick_hex(out + n, clock, ES2_TICK_CLOCK_SIZE);
    return n;
}

int es2_tick_record_rng(char *out, size_t cap, unsigned bank, const uint8_t *bytes, int compact, int saved,
                        uint8_t previous[ES2_TICK_RNG_SIZE]) {
    char name = bank ? 'b' : 'a';
    if (compact && saved && !memcmp(previous, bytes, ES2_TICK_RNG_SIZE))
        return snprintf(out, cap, ",\"rng_%c_same\":true", name);
    int n = snprintf(out, cap, ",\"rng_%c_hex\":", name);
    n += es2_tick_hex(out + n, bytes, ES2_TICK_RNG_SIZE);
    if (compact) memcpy(previous, bytes, ES2_TICK_RNG_SIZE);
    return n;
}

int es2_tick_record_actor(char *out, size_t cap, uint32_t address, const uint8_t *actor, int actor_raw) {
    int n = snprintf(out, cap, ",\"player_actor\":\"0x%08lx\"", (unsigned long)address);
    if (!actor) return n;
    n += snprintf(out + n, cap - (size_t)n,
        ",\"position_raw_i32\":[%ld,%ld,%ld],\"angles_raw_u16\":[%u,%u,%u],\"speed_raw_i16\":%d,\"throttle_raw_q10\":%d",
        (long)(int32_t)tr_u32(actor + 0x26), (long)(int32_t)tr_u32(actor + 0x2a),
        (long)(int32_t)tr_u32(actor + 0x2e), (unsigned)tr_u16(actor + 0x0c),
        (unsigned)tr_u16(actor + 0x0e), (unsigned)tr_u16(actor + 0x10),
        (int)(int16_t)tr_u16(actor + 0x28e), (int)(int16_t)tr_u16(actor + 0x290));
    if (actor_raw) {
        n += snprintf(out + n, cap - (size_t)n, ",\"actor_raw_hex\":");
        n += es2_tick_hex(out + n, actor, ES2_TICK_ACTOR_SIZE);
    }
    return n;
}

int es2_tick_record_vt(char *out, size_t cap, int intra, unsigned long limiter_polls, unsigned long stall_advances,
                       unsigned long late_polls, unsigned long intra_advances) {
    int n = snprintf(out, cap, ",\"vt_limiter_polls\":%lu,\"vt_stall_advances\":%lu,\"vt_late_polls\":%lu",
                     limiter_polls, stall_advances, late_polls);
    if (intra) n += snprintf(out + n, cap - (size_t)n, ",\"vt_intra_advances\":%lu", intra_advances);
    return n;
}

int es2_tick_record_input(char *out, size_t cap, int replay, unsigned long recorded, unsigned long injected,
                          unsigned long dropped, unsigned long fallbacks, unsigned long focus_dropped) {
    return snprintf(out, cap,
        ",\"in_mode\":\"%s\",\"in_recorded\":%lu,\"in_injected\":%lu,\"in_dropped\":%lu,\"in_fallbacks\":%lu,\"in_focus_dropped\":%lu",
        replay ? "replay" : "record", recorded, injected, dropped, fallbacks, focus_dropped);
}
