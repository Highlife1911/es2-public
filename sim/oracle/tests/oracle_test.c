/* Unit test of //sim/oracle: virtual time, the tick-record lines and the input
 * journal on synthetic values (no game data). The proxy's own fixtures check
 * the same code through tools/reference-game/proxy/virtual_time.h and
 * tick_log.h. */
#include <stdio.h>
#include <string.h>
#include "sim/oracle/virtual_time.h"
#include "sim/oracle/tick_record.h"
#include "sim/oracle/journal.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static unsigned paced;
static es2_vt *pace_vt;
static uint32_t pace_clock;
static void pace(void *user) { (void)user; ++paced; pace_clock = pace_vt->clock_ms; }

static int virtual_time(void) {
    es2_vt vt = {0};
    es2_vt_enable(&vt, 1, 39, 1000, 0, 0);
    CHECK(!vt.enabled && vt.stall_limit == ES2_VT_DEFAULT_STALL_LIMIT);   /* below the limiter's 40 ms */
    es2_vt_enable(&vt, 1, 47, 1000, 2, 0);
    CHECK(vt.enabled && vt.clock_ms == 1000 && vt.step_base == 1000);
    /* Reads stand still until the stall guard. */
    CHECK(es2_vt_read(&vt) == 1000 && es2_vt_read(&vt) == 1000);
    CHECK(es2_vt_read(&vt) == 1001 && vt.stall_advances == 1);
    /* A poll gives previous + step, the pace hook sees the new clock first. */
    pace_vt = &vt;
    CHECK(es2_vt_limiter_poll(&vt, 1000, pace, NULL) == 1047 && paced == 1 && pace_clock == 1047);
    CHECK(vt.limiter_polls == 1 && vt.since_limiter == 0 && vt.step_base == 1047);
    /* Never backwards: a previous far behind keeps the clock. */
    vt.clock_ms = 5000;
    CHECK(es2_vt_limiter_poll(&vt, 1000, NULL, NULL) == 5000 && vt.limiter_polls == 2);
    /* Wrap-around keeps "ahead" as signed difference. */
    vt.clock_ms = 0xfffffff0u;
    CHECK(es2_vt_limiter_poll(&vt, 0xfffffff0u, NULL, NULL) == 0x1fu);
    /* Intra-frame advance: the first read stands, then +1 per read up to step - 1. */
    es2_vt iv = {0};
    es2_vt_enable(&iv, 1, 47, 1000, 100000, 1);
    CHECK(es2_vt_limiter_poll(&iv, 953, NULL, NULL) == 1000);
    CHECK(es2_vt_read(&iv) == 1000);
    for (unsigned i = 1; i <= 46; ++i) CHECK(es2_vt_read(&iv) == 1000 + i);
    CHECK(es2_vt_read(&iv) == 1046 && iv.intra_advances == 46 && iv.stall_advances == 0);
    /* Frame boundary: the first only starts the count; a frame without a poll
     * gets the step; a frame with one is untouched. */
    es2_vt fb = {0};
    es2_vt_enable(&fb, 1, 47, 1000, 0, 0);
    es2_vt_frame_boundary(&fb);
    CHECK(fb.bypass_steps == 0 && fb.clock_ms == 1000);
    es2_vt_frame_boundary(&fb);
    CHECK(fb.bypass_steps == 1 && fb.clock_ms == 1047 && fb.step_base == 1047);
    es2_vt_limiter_poll(&fb, 1047, NULL, NULL);
    es2_vt_frame_boundary(&fb);
    CHECK(fb.bypass_steps == 1 && fb.clock_ms == 1094);
    es2_vt off = {0};
    es2_vt_frame_boundary(&off);
    CHECK(!off.boundary_seen);
    return 0;
}

static int tick_lines(void) {
    static char line[262144];
    uint8_t context[ES2_TICK_CONTEXT_SIZE] = {0}, clock[ES2_TICK_CLOCK_SIZE], rng[ES2_TICK_RNG_SIZE], prev[ES2_TICK_RNG_SIZE] = {0};
    uint8_t actor[ES2_TICK_ACTOR_SIZE] = {0};
    for (unsigned i = 0; i < sizeof(clock); ++i) clock[i] = (uint8_t)i;
    for (unsigned i = 0; i < sizeof(rng); ++i) rng[i] = (uint8_t)(0xa0 + i);
    context[0xbf] = 7; context[0x1a] = 1; context[0xb6] = 1;
    int n = es2_tick_record_head(line, sizeof(line), 0, 3, -5, 0x4d24bcu, context, clock);
    const char *head = "{\"event\":\"tick\",\"sequence\":3,\"qpc\":-5,\"ebx\":\"0x004d24bc\",\"frame_counter_before_increment\":7,"
        "\"completed_clock_ms\":319951120,\"limiter_bypassed\":true,\"loop_pause_flag\":1,"
        "\"clock_raw_hex\":\"000102030405060708090a0b0c0d0e0f1011121314151617\"";
    CHECK(n == (int)strlen(head) && !strcmp(line, head));
    CHECK(es2_tick_record_head(line, sizeof(line), 1, 3, 0, 0, context, clock) > 0 && !strncmp(line, "{\"event\":\"tick_compact\"", 23));
    /* RNG: hex, then with compact the cache makes the next equal stream "same". */
    n = es2_tick_record_rng(line, sizeof(line), 1, rng, 0, 0, prev);
    CHECK(n == 13 + 2 + 2 * ES2_TICK_RNG_SIZE && !strncmp(line, ",\"rng_b_hex\":\"a0a1", 18) && prev[0] == 0);
    n = es2_tick_record_rng(line, sizeof(line), 0, rng, 1, 0, prev);
    CHECK(!strncmp(line, ",\"rng_a_hex\":", 13) && prev[0] == 0xa0);
    n = es2_tick_record_rng(line, sizeof(line), 0, rng, 1, 1, prev);
    CHECK(!strcmp(line, ",\"rng_a_same\":true") && n == 18);
    /* Actor: address only when unreadable, fields and raw block otherwise. */
    n = es2_tick_record_actor(line, sizeof(line), 0x01234567u, NULL, 1);
    CHECK(!strcmp(line, ",\"player_actor\":\"0x01234567\"") && n == 28);
    actor[0x26] = 0xff; actor[0x27] = 0xff; actor[0x28] = 0xff; actor[0x29] = 0xff;   /* x = -1 */
    actor[0x0c] = 0x34; actor[0x0d] = 0x12;
    actor[0x28e] = 0xfe; actor[0x28f] = 0xff; actor[0x290] = 0x00; actor[0x291] = 0x04;
    n = es2_tick_record_actor(line, sizeof(line), 0x10u, actor, 0);
    CHECK(!strcmp(line, ",\"player_actor\":\"0x00000010\",\"position_raw_i32\":[-1,0,0],\"angles_raw_u16\":[4660,0,0],"
                        "\"speed_raw_i16\":-2,\"throttle_raw_q10\":1024"));
    n = es2_tick_record_actor(line, sizeof(line), 0x10u, actor, 1);
    CHECK(n == (int)strlen(line) && strstr(line, ",\"actor_raw_hex\":\"") && (size_t)n > 2 * ES2_TICK_ACTOR_SIZE);
    n = es2_tick_record_vt(line, sizeof(line), 0, 1, 2, 3, 4);
    CHECK(!strcmp(line, ",\"vt_limiter_polls\":1,\"vt_stall_advances\":2,\"vt_late_polls\":3"));
    n = es2_tick_record_vt(line, sizeof(line), 1, 1, 2, 3, 4);
    CHECK(!strcmp(line, ",\"vt_limiter_polls\":1,\"vt_stall_advances\":2,\"vt_late_polls\":3,\"vt_intra_advances\":4"));
    n = es2_tick_record_input(line, sizeof(line), 1, 0, 5, 1, 2, 0);
    CHECK(n == (int)strlen(line) && !strcmp(line, ",\"in_mode\":\"replay\",\"in_recorded\":0,\"in_injected\":5,\"in_dropped\":1,"
                                                   "\"in_fallbacks\":2,\"in_focus_dropped\":0"));
    n = es2_tick_record_input(line, sizeof(line), 0, 3, 0, 0, 0, 0);
    CHECK(!strncmp(line, ",\"in_mode\":\"record\",\"in_recorded\":3,", 36));
    es2_tick_start s = {2, "own_process_headless", 11, 12, 0x400000u, 0x45f4adu, 10000000, 99, 0, 0};
    n = es2_tick_record_start(line, sizeof(line), &s);
    CHECK(!strcmp(line, "{\"event\":\"start\",\"schema\":2,\"mode\":\"own_process_headless\",\"pid\":11,\"tid\":12,"
                        "\"image_base\":\"0x00400000\",\"boundary\":\"0x0045f4ad\",\"phase\":\"before_frame_counter_increment\","
                        "\"qpc_frequency\":10000000,\"record_limit\":99,\"actor_raw\":false,\"watches\":0}\r\n"));
    return 0;
}

static unsigned scan_of(unsigned vk) { return vk + 0x100u; }

/* The journal: both line forms, consumption order, the pump key, the virtual
 * keys and cursor. */
static int journal(void) {
    static const char text[] =
        "{\"event\":\"start\",\"schema\":1}\n"
        "{\"f\":3,\"api\":\"msg\",\"a\":256,\"b\":87,\"c\":1114113,\"d\":0}\n"
        "{\"f\":3, \"p\": 2, \"api\": \"keydown\", \"vk\": 13}\n"
        "{\"f\":3,\"api\":\"mousedown\",\"x\":10,\"y\":-1}\n"
        "{\"f\":4,\"api\":\"mousemove\",\"x\":7,\"y\":9}\n"
        "{\"f\":2,\"api\":\"key\",\"a\":16,\"b\":-32768}\n"
        "{\"f\":5,\"api\":\"key\",\"a\":16,\"b\":1}\n"
        "{\"f\":5,\"api\":\"joycaps\",\"a\":0,\"b\":0,\"hex\":\"0aFF\"}\n"
        "{\"f\":5,\"api\":\"joypos\",\"a\":0,\"b\":0,\"hex\":\"0g\"}\n"
        "garbage\n";
    es2_journal j;
    es2_journal_init(&j);
    CHECK(j.mouse_x == 320 && j.mouse_y == 240 && j.pump_frame == 0xffffffffu);
    CHECK(!es2_journal_load_text(&j, text, sizeof(text) - 1u, scan_of));
    CHECK(j.count == 6);   /* the start line, the negative mouse y, the bad hex and the garbage are no entries */
    CHECK(j.entries[1].api == ES2_JOURNAL_MSG && j.entries[1].pump == 2 && j.entries[1].a == 0x100 && j.entries[1].b == 13 &&
          (uint32_t)j.entries[1].c == (1u | (0x10du << 16)));
    CHECK(j.entries[2].a == 0x200 && j.entries[2].c == (7 | 9 << 16));
    CHECK(j.entries[5].blob_size == 2 && j.entries[5].blob[0] == 0x0a && j.entries[5].blob[1] == 0xff);
    /* Frame 3: the first peek counts 0, the keyed entry waits for count 2. */
    es2_journal_count_pump(&j, 3);
    es2_journal_entry *e = es2_journal_take(&j, ES2_JOURNAL_MSG, 3, NULL, 0);
    CHECK(e && e->b == 87 && j.pump_count == 0);
    es2_journal_apply_message(&j, (uint32_t)e->a, (uint32_t)e->b, (uint32_t)e->c);
    CHECK(j.keys[87] == 0x81);
    es2_journal_count_pump(&j, 3);
    CHECK(!es2_journal_take(&j, ES2_JOURNAL_MSG, 3, NULL, 0) && j.pump_count == 1);
    es2_journal_count_pump(&j, 3);
    CHECK((e = es2_journal_take(&j, ES2_JOURNAL_MSG, 3, NULL, 0)) && e->b == 13);
    /* An earlier frame's message is still delivered later; a mouse message moves the cursor. */
    es2_journal_count_pump(&j, 5);
    CHECK(j.pump_count == 0);
    CHECK((e = es2_journal_take(&j, ES2_JOURNAL_MSG, 5, NULL, 0)) && e->frame == 4);
    es2_journal_apply_message(&j, (uint32_t)e->a, (uint32_t)e->b, (uint32_t)e->c);
    CHECK(j.mouse_x == 7 && j.mouse_y == 9);
    CHECK(!es2_journal_take(&j, ES2_JOURNAL_MSG, 5, NULL, 0));
    /* Keys: an older frame's entry is skipped, a mismatch stays. */
    CHECK(!es2_journal_take(&j, ES2_JOURNAL_KEY, 5, es2_journal_match_first, 17));
    CHECK((e = es2_journal_take(&j, ES2_JOURNAL_KEY, 5, es2_journal_match_first, 16)) && e->b == 1);
    CHECK((uint16_t)es2_journal_key_fallback(&j, 87) == 0x8001u && j.fallbacks == 1);
    es2_journal_apply_message(&j, 0x101u, 87u, 0);
    CHECK(es2_journal_key_fallback(&j, 87) == 1 && es2_journal_key_fallback(&j, 300) == 0 && j.fallbacks == 3);
    es2_journal_apply_message(&j, 0x100u, 87u, 0);
    CHECK((uint16_t)es2_journal_key_fallback(&j, 87) == 0x8000u);
    CHECK(es2_journal_is_input_message(0x104) && es2_journal_is_input_message(0x20e) && !es2_journal_is_input_message(0x102));
    es2_journal_free(&j);
    CHECK(!j.entries && !j.count);
    return 0;
}

int main(void) {
    if (virtual_time() || tick_lines() || journal()) return 1;
    puts("PASS: oracle virtual time, tick-record lines and journal");
    return 0;
}
