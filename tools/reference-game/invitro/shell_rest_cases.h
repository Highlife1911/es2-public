/* The shell leftovers, original machine code against the own rows of
 * sim/src/loop.c (--shell-rest; target :differential_shell_rest_test). Built on
 * cockpit_cases.h's helpers (the stand-in log jb_note, the jump patch jb_patch)
 * and on loop_cases.h's loop set-up (lc_reset); the world, the dispatch and the
 * stand-ins are this suite's (prefix sr_).
 *
 * Under test, as random sequences of up to 6 calls with random entry
 * registers: the capture push 0x461250 and pop 0x461274, the HUD
 * sub-object 0x4610ad, the language path 0x45ef1c, the voice archive name
 * 0x45ef84, the cursor store 0x464c68, the empty 0x4650cc and the timer
 * kill 0x4650d0 (without a timer: with one the original calls KillTimer,
 * which the own row refuses); and, as single calls, the subject label
 * 0x45e208 (the y clamp, the first-label byte, the player / a wingman
 * without index / with an index, an empty text and its box, the value
 * text's two choices). Run as code on both sides: 0x42db10 (inside
 * 0x45e208 on the original's side) and _strcat (the import on the original's
 * side through the rtl_cases.h model as on the own side).
 *
 * Recording stand-ins (a `jmp rel32` over the callee on the original's
 * side, a service at the VA on the own side, swapped in after the seal
 * where a row exists): the capture set 0x45b68c ('k', scripted EAX), the
 * fill 0x486704 ('R', the rectangle and the colour record GC+0x22c points
 * at), the string draw 0x482634 ('T', the font, x, y and a hash of the
 * text; scripted EAX), the wingman index 0x433134 ('w', scripted EAX) and
 * the path builder 0x492b90 ('x', writes a random name into the world).
 *
 * Compared after each case: the stand-in logs (order and arguments; stack
 * addresses as a tag), DATA and the heap, the world (GC+0x22c, which
 * 0x45e208 leaves pointing at its dead frame, as "written or not"), and per
 * call the registers the contract names: EAX, EDX and ECX for 0x4610ad,
 * 0x45ef84, 0x464c68, 0x4650cc and 0x4650d0; EAX and ECX for
 * 0x461250/0x461274 (EDX is the stand-in's); EAX for 0x45e208 and 0x45ef1c
 * (ECX and EDX are _strcat's: a harness stand-in on the original's side).
 *
 * Not reached: a timer at 0x4650d0 (KillTimer), and label texts longer
 * than 0x43 bytes (the original overwrites its saved registers; the own
 * row refuses). */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "cockpit_cases.h"

#define SR_WORLD_SIZE 0x1000u
#define SR_GC 0x000u        /* the graphics context (0x240) */
#define SR_FONT 0x240u      /* the label font (+0xa the height) */
#define SR_HOLDER 0x260u    /* [0x4d24ea]: +0x32 the subject */
#define SR_COCKPIT 0x2a0u   /* [0x49b088]: +0x1e1 the sub-object */
#define SR_STRINGS 0x500u   /* 6 strings of 0x60: the player, label, value, value 2, value 3, the path name */
#define SR_NAMES 0x780u     /* the name records: 4 x 334, +0x264 the name pointer */
#define SR_NAME_TEXT 0xd00u /* the wingman names, 4 x 0x40 */
static BYTE *sr_world, *sr_world_start;   /* start + the kept side-0 result */
static es2_sim_entry sr_entries[4096];
static es2_sim_dispatch sr_dispatch;
static int sr_bad;
static uint32_t sr_at(uint32_t offset) { return (uint32_t)(uintptr_t)sr_world + offset; }
static void sr_w32(uint32_t offset, uint32_t v) { put32(sr_world_start + offset, v); }
static void sr_w16(uint32_t offset, uint32_t v) { put16(sr_world_start + offset, (uint16_t)v); }
static uint32_t sr_rand(uint32_t n) { return random32() % n; }
static uint32_t sr_norm(uint32_t v) {
    if (v - (uint32_t)(uintptr_t)sr_world < SR_WORLD_SIZE || v - 0x00400000u < 0x00400000u) return v;
    return 0x57ac0000u;
}
static uint32_t sr_hash(uint32_t s) {
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < 0x100u; ++i) {
        BYTE b = *(const BYTE *)(uintptr_t)(s + i);
        h ^= b; h *= 16777619u;
        if (!b) break;
    }
    return h;
}

/* ---- the scripted answers and the stand-ins */
static uint32_t sr_capture_ret[8], sr_text_ret[4], sr_wingman_ret, sr_path_len;
static unsigned sr_capture_i, sr_text_i;
static uint32_t sr_act(char l, const uint32_t *a) {
    switch (l) {
    case 'k': jb_note('k', a[0], 0, 0); return sr_capture_ret[sr_capture_i++ & 7u];
    case 'R': {
        uint32_t rec = get32((const BYTE *)(uintptr_t)(get32(mapped + (0x006c5f70u - 0x400000u)) + 0x22cu));
        const BYTE *r = (const BYTE *)(uintptr_t)a[0];
        jb_note('R', sr_norm(a[0]), get32(r), get32(r + 4));
        jb_note('r', get32(r + 8), get32(r + 12), 0);
        jb_note('c', get32((const BYTE *)(uintptr_t)rec), get32((const BYTE *)(uintptr_t)rec + 4), 0);
        return 0x6bu;
    }
    case 'T': {
        const BYTE *p = (const BYTE *)(uintptr_t)a[1];
        jb_note('T', a[0], get32(p), get32(p + 4));
        jb_note('t', sr_norm(a[2]), sr_hash(a[2]), 0);
        return sr_text_ret[sr_text_i++ & 3u];
    }
    case 'w': jb_note('w', a[0], 0, 0); return sr_wingman_ret;
    case 'x': {
        jb_note('x', a[0], a[1], 0);
        BYTE *d = sr_world + SR_STRINGS + 5u * 0x60u;
        for (uint32_t i = 0; i < sr_path_len; ++i) d[i] = (BYTE)('a' + (a[0] + 7u * i) % 26u);
        d[sr_path_len] = 0;
        return sr_at(SR_STRINGS + 5u * 0x60u);
    }
    default: return 0;
    }
}
#define SR_O(name, letter, n)                                                                                   \
    static uint32_t __cdecl sr_o_##name(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {                     \
        uint32_t a[4] = {a0, a1, a2, a3};                                                                       \
        for (unsigned i = (n); i < 4u; ++i) a[i] = 0;                                                           \
        return sr_act(letter, a);                                                                               \
    }
SR_O(capture, 'k', 1) SR_O(fill, 'R', 1) SR_O(text, 'T', 3) SR_O(wingman, 'w', 1) SR_O(path, 'x', 2)
static const jb_stub_t sr_stubs[] = {
    {0x0045b68cu, (void *)sr_o_capture, 'k', 1}, {0x00486704u, (void *)sr_o_fill, 'R', 1},
    {0x00482634u, (void *)sr_o_text, 'T', 3},    {0x00433134u, (void *)sr_o_wingman, 'w', 1},
    {0x00492b90u, (void *)sr_o_path, 'x', 2},
};
#define SR_STUBS (sizeof(sr_stubs) / sizeof(sr_stubs[0]))
static BYTE sr_saved[SR_STUBS][5];
static int sr_patch_all(int install) {
    for (unsigned i = 0; i < SR_STUBS; ++i)
        if (jb_patch(sr_stubs[i].va, sr_stubs[i].fn, sr_saved[i], install)) return 1;
    return 0;
}
static void sr_s_stub(es2_sim_service_call *call, void *user) {
    unsigned i = (unsigned)(uintptr_t)user;
    uint32_t a[4] = {0, 0, 0, 0};
    for (unsigned k = 0; k < sr_stubs[i].slots; ++k) a[k] = call->slots[k];
    call->eax = sr_act(sr_stubs[i].letter, a);
}
static void sr_refuse(const es2_sim_refusal *r, void *user) {
    (void)user;
    fprintf(stderr, "FAIL: own shell-rest: refused call to 0x%08lx (%s): %s\n", (unsigned long)r->va, r->name ? r->name : "?",
            r->reason ? r->reason : "");
    sr_bad = 1;
}
static int sr_dispatch_setup(void) {
    es2_sim_dispatch_init(&sr_dispatch, sr_entries, 4096, sr_refuse, NULL);
    static es2_sim_services s;
    s.memory = &ir_memory;
    s.heap = &ir_heap;
    s.vt = &lc_vt;
    s.fs0 = NULL;
    if (es2_sim_register_services(&sr_dispatch, &s)) return fail("shell-rest: dispatch");
    for (unsigned i = 0; i < SR_STUBS; ++i)
        if (!jb_is_replaced(sr_stubs[i].va) &&
            es2_sim_dispatch_add_service(&sr_dispatch, sr_stubs[i].va, "shell-rest stub", sr_s_stub, (void *)(uintptr_t)i, 0,
                                         (uint8_t)sr_stubs[i].slots))
            return fail("shell-rest: dispatch (stub)");
    const char *error = NULL;
    uint32_t bad = 0;
    if (es2_sim_dispatch_seal(&sr_dispatch, &error, &bad)) return fail("shell-rest: dispatch seal");
    for (unsigned i = 0; i < SR_STUBS; ++i) {
        if (!jb_is_replaced(sr_stubs[i].va)) continue;
        es2_sim_entry *e = (es2_sim_entry *)es2_sim_dispatch_find(&sr_dispatch, sr_stubs[i].va);
        if (!e || e->kind != ES2_SIM_SERVICE) return fail("shell-rest: service missing");
        e->service = sr_s_stub;
        e->service_user = (void *)(uintptr_t)i;
    }
    return 0;
}

/* ---- the case script */
static jb_preset sr_presets[128];
static unsigned sr_preset_count;
static void sr_set32(uint32_t va, uint32_t v) { sr_presets[sr_preset_count++] = (jb_preset){va, v, 4}; }
static void sr_set16(uint32_t va, uint32_t v) { sr_presets[sr_preset_count++] = (jb_preset){va, v, 2}; }
static void sr_set8(uint32_t va, uint32_t v) { sr_presets[sr_preset_count++] = (jb_preset){va, v, 1}; }
#define SR_EAX 1u
#define SR_EDX 2u
#define SR_ECX 4u
typedef struct sr_op { uint32_t va, a[2], in[3]; unsigned n, mask; } sr_op;
static sr_op sr_ops[8];
static unsigned sr_op_count;
static uint32_t sr_res[2][8][3];
static void sr_op_add(uint32_t va, unsigned mask, unsigned n, uint32_t a0, uint32_t a1) {
    sr_op *o = &sr_ops[sr_op_count++];
    o->va = va; o->mask = mask; o->n = n; o->a[0] = a0; o->a[1] = a1;
    for (unsigned i = 0; i < 3u; ++i) o->in[i] = random32();
}
/* A random string of `n` printable bytes at a world offset. */
static void sr_string(uint32_t offset, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) sr_world_start[offset + i] = (BYTE)(0x20u + sr_rand(0x5fu));
    sr_world_start[offset + n] = 0;
}
static uint32_t sr_len(void) { return sr_rand(4u) ? sr_rand(0x20u) : 0u; }

enum {
    SK_PUSH, SK_POP, SK_SUB, SK_PATH, SK_VOICE, SK_VOICE_R, SK_CURSOR, SK_NOTHING, SK_TIMER,
    SK_ONCE_SET, SK_ONCE_KEPT, SK_CLAMP, SK_NO_CLAMP, SK_PLAYER, SK_NO_INDEX, SK_INDEX, SK_TEXT, SK_BOX, SK_MODE_0, SK_MODE_1,
    SR_KEYS
};
static const char *const sr_key_names[SR_KEYS] = {
    "push", "pop", "sub-object", "language-path", "voice", "voice-r", "cursor", "nothing", "timer-0",
    "label:once-set", "label:once-kept", "label:clamp", "label:no-clamp", "label:player", "label:no-index", "label:index",
    "label:text", "label:box", "label:mode-0", "label:mode-1",
};
static unsigned sr_keys[SR_KEYS];

static void sr_begin(void) {
    memset(sr_world_start, 0, SR_WORLD_SIZE);
    sr_preset_count = 0;
    sr_op_count = 0;
    random_bytes(sr_world_start + SR_GC, 0x240u);
    random_bytes(sr_world_start + SR_COCKPIT, 0x200u);
    for (unsigned i = 0; i < 8u; ++i) sr_capture_ret[i] = random32();
    for (unsigned i = 0; i < 4u; ++i) sr_text_ret[i] = random32();
    sr_path_len = sr_rand(20u);
    sr_set32(0x006c5f70u, sr_at(SR_GC));
    sr_set32(0x0049b088u, sr_at(SR_COCKPIT));
}
static void sr_side_begin(unsigned side) {
    lc_reset(side, 47, 0x01000000u, 1);
    lc_loop.dispatch = &sr_dispatch;
    memcpy(sr_world, sr_world_start, SR_WORLD_SIZE);
    for (unsigned i = 0; i < sr_preset_count; ++i) {
        BYTE *p = mapped + (sr_presets[i].va - 0x400000u);
        if (sr_presets[i].width == 4) put32(p, sr_presets[i].value);
        else if (sr_presets[i].width == 2) put16(p, (uint16_t)sr_presets[i].value);
        else *p = (BYTE)sr_presets[i].value;
    }
    jb_side = side;
    jb_hash[side] = 0xcbf29ce484222325ull;
    jb_count[side] = 0;
    jb_len[side] = 0;
    jb_text[side][0] = 0;
    sr_capture_i = sr_text_i = 0;
}
static void sr_show(const char *what, unsigned k) {
    for (unsigned i = 0; i < sr_op_count; ++i)
        fprintf(stderr, "  op %u: 0x%08lx(0x%08lx, 0x%08lx) in %08lx %08lx %08lx\n", i, (unsigned long)sr_ops[i].va,
                (unsigned long)sr_ops[i].a[0], (unsigned long)sr_ops[i].a[1], (unsigned long)sr_ops[i].in[0],
                (unsigned long)sr_ops[i].in[1], (unsigned long)sr_ops[i].in[2]);
    fprintf(stderr, "  world at 0x%08lx; %s case %u\n", (unsigned long)(uintptr_t)sr_world, what, k);
}
static int sr_run(const char *what, unsigned k) {
    uint32_t gc_record = get32(sr_world_start + SR_GC + 0x22cu);
    for (unsigned side = 0; side < 2u; ++side) {
        sr_side_begin(side);
        for (unsigned i = 0; i < sr_op_count; ++i) {
            sr_op *o = &sr_ops[i];
            uint32_t *out = sr_res[side][i];
            jb_note('#', i, 0, 0);
            if (!side) au_call((uint32_t)(uintptr_t)(mapped + (o->va - 0x400000u)), o->a, o->n, o->in, out);
            else {
                es2_sim_result r;
                memset(&r, 0, sizeof(r));
                if (es2_sim_call(&sr_dispatch, o->va, o->a, o->n, o->in, &r)) sr_bad = 1;
                out[0] = r.eax; out[1] = r.edx; out[2] = r.ecx;
            }
            if (sr_bad) {
                fprintf(stderr, "FAIL: shell-rest %s case %u op %u (0x%08lx): a call failed\n", what, k, i, (unsigned long)o->va);
                sr_show(what, k);
                return 1;
            }
        }
        if (get32(sr_world + SR_GC + 0x22cu) != gc_record) put32(sr_world + SR_GC + 0x22cu, 0xdead022cu);
        if (!side) memcpy(sr_world_start + SR_WORLD_SIZE, sr_world, SR_WORLD_SIZE);
        su_take(side);
    }
    if (jb_hash[0] != jb_hash[1] || jb_count[0] != jb_count[1]) {
        fprintf(stderr, "  original (%u): %s\n  own      (%u): %s\n", jb_count[0], jb_text[0], jb_count[1], jb_text[1]);
        sr_show(what, k);
        fprintf(stderr, "FAIL: shell-rest %s case %u: the stand-in calls differ\n", what, k);
        return 1;
    }
    if (su_compare(what, k)) { sr_show(what, k); return 1; }
    if (memcmp(sr_world, sr_world_start + SR_WORLD_SIZE, SR_WORLD_SIZE)) {
        for (unsigned i = 0, shown = 0; i < SR_WORLD_SIZE && shown < 16u; ++i)
            if (sr_world[i] != sr_world_start[SR_WORLD_SIZE + i]) {
                fprintf(stderr, "  world +0x%04x original %02x own %02x\n", i, sr_world_start[SR_WORLD_SIZE + i], sr_world[i]);
                ++shown;
            }
        sr_show(what, k);
        fprintf(stderr, "FAIL: shell-rest %s case %u: the world differs\n", what, k);
        return 1;
    }
    static const char *const regs[3] = {"EAX", "EDX", "ECX"};
    for (unsigned i = 0; i < sr_op_count; ++i)
        for (unsigned r = 0; r < 3u; ++r) {
            if (!(sr_ops[i].mask & (1u << r)) || sr_res[0][i][r] == sr_res[1][i][r]) continue;
            fprintf(stderr, "FAIL: shell-rest %s case %u op %u (0x%08lx): %s original 0x%08lx own 0x%08lx\n", what, k, i,
                    (unsigned long)sr_ops[i].va, regs[r], (unsigned long)sr_res[0][i][r], (unsigned long)sr_res[1][i][r]);
            sr_show(what, k);
            return 1;
        }
    return 0;
}

/* Part calls: random sequences of the small rows. */
static int sr_calls_case(unsigned k) {
    sr_begin();
    static const BYTE letters[8] = {'e', 'g', 'f', 'r', 'r', 'i', 's', 0xe4};
    sr_set8(0x004d2536u, sr_rand(4u) ? letters[sr_rand(8u)] : sr_rand(256u));
    sr_set8(0x0049eaa5u, sr_rand(3u) ? sr_rand(2u) : sr_rand(256u));
    sr_set8(0x0049f704u, sr_rand(8u) ? 2u + sr_rand(8u) : sr_rand(256u));
    for (unsigned i = 0; i < 16u; ++i) sr_set8(0x004d2a80u + i, sr_rand(2u) ? sr_rand(2u) : sr_rand(256u));
    sr_set32(0x004d3720u, random32());
    sr_set32(0x004d3608u, 0);
    unsigned n = 1u + sr_rand(6u);
    for (unsigned i = 0; i < n; ++i) {
        switch (sr_rand(8u)) {
        case 0: sr_op_add(0x00461250u, SR_EAX | SR_ECX, 0, 0, 0); ++sr_keys[SK_PUSH]; break;
        case 1: sr_op_add(0x00461274u, SR_EAX | SR_ECX, 0, 0, 0); ++sr_keys[SK_POP]; break;
        case 2: sr_op_add(0x004610adu, SR_EAX | SR_EDX | SR_ECX, 0, 0, 0); ++sr_keys[SK_SUB]; break;
        case 3: sr_op_add(0x0045ef1cu, SR_EAX, 2, random32(), random32()); ++sr_keys[SK_PATH]; break;
        case 4: {
            sr_op_add(0x0045ef84u, SR_EAX | SR_EDX | SR_ECX, 0, 0, 0);
            BYTE letter = 0;
            for (unsigned p = 0; p < sr_preset_count; ++p)
                if (sr_presets[p].va == 0x004d2536u) letter = (BYTE)sr_presets[p].value;
            ++sr_keys[letter == 'r' ? SK_VOICE_R : SK_VOICE];
            break;
        }
        case 5: sr_op_add(0x00464c68u, SR_EAX | SR_EDX | SR_ECX, 1, random32(), 0); ++sr_keys[SK_CURSOR]; break;
        case 6: sr_op_add(0x004650ccu, SR_EAX | SR_EDX | SR_ECX, 0, 0, 0); ++sr_keys[SK_NOTHING]; break;
        default: sr_op_add(0x004650d0u, SR_EAX | SR_EDX | SR_ECX, 0, 0, 0); ++sr_keys[SK_TIMER]; break;
        }
    }
    return sr_run("calls", k);
}

/* Part label: one call of 0x45e208. */
static int sr_label_case(unsigned k) {
    sr_begin();
    uint32_t once = sr_rand(2u);
    sr_set8(0x0049f211u, once ? 1u + sr_rand(255u) : 0u);
    ++sr_keys[once ? SK_ONCE_KEPT : SK_ONCE_SET];
    /* The font, the view, the rectangle's top. */
    int32_t fh = sr_rand(8u) ? (int32_t)sr_rand(24u) : (int32_t)(int16_t)random32();
    sr_w16(SR_FONT + 0xau, (uint32_t)fh);
    random_bytes(sr_world_start + SR_FONT, 0xau);
    sr_set32(0x004d1e2cu, sr_at(SR_FONT));
    static const uint32_t widths[4] = {320, 640, 320, 512}, heights[4] = {200, 240, 400, 480};
    uint32_t w = sr_rand(8u) ? widths[sr_rand(4u)] : random32(), h = sr_rand(8u) ? heights[sr_rand(4u)] : random32() & 0xffffu;
    sr_set32(0x004d253eu, w);
    sr_set32(0x004d2542u, h);
    int32_t top = sr_rand(8u) ? (int32_t)sr_rand(h + 1u) : (int32_t)random32();
    if (!sr_rand(4u)) top = (int32_t)h - (int32_t)sr_rand(4u);   /* near the bottom: the clamp */
    sr_set32(0x004cfa4cu, (uint32_t)top);
    {
        uint32_t h1 = h - 1u, half = (uint32_t)((int32_t)(h1 - (uint32_t)top) >> 1);
        uint16_t y16 = (uint16_t)((uint16_t)half + (uint16_t)top);
        y16 = (uint16_t)(y16 - (uint16_t)(fh >> 1));
        ++sr_keys[(int32_t)(h1 - 1u - (uint32_t)fh) < (int32_t)(int16_t)y16 ? SK_CLAMP : SK_NO_CLAMP];
    }
    sr_set16(0x004d3ba2u, random32());
    sr_set16(0x0049f02eu, sr_rand(4u) ? sr_rand(320u) : random32());
    sr_set16(0x0049f030u, sr_rand(4u) ? sr_rand(320u) : random32());
    static const uint16_t shifts[8] = {0, 0, 1, 1, 2, 3, 0x21, 0xffff};
    sr_set16(0x004d253au, shifts[sr_rand(8u)]);
    /* The subject: the player, or a wingman without / with an index. */
    uint32_t subject = sr_rand(4u) ? sr_at(0x800u + sr_rand(0x100u)) : random32();
    sr_w32(SR_HOLDER + 0x32u, subject);
    sr_set32(0x004d24eau, sr_at(SR_HOLDER));
    unsigned kind = sr_rand(3u);
    sr_set32(0x004d24e6u, kind == 0u ? subject : subject ^ (1u + sr_rand(0xffffu)));
    uint32_t label_len = sr_len(), name_len = sr_rand(4u) ? sr_rand(0x20u) : 0u;
    if (label_len + name_len > 0x43u) name_len = 0x43u - label_len;
    sr_string(SR_STRINGS + 0x60u, label_len);
    sr_set32(0x004d16a0u, sr_at(SR_STRINGS + 0x60u));
    uint32_t name_shown = name_len;
    if (kind == 0u) {
        sr_string(SR_STRINGS, name_len);
        sr_set32(0x004d14dcu, sr_at(SR_STRINGS));
        ++sr_keys[SK_PLAYER];
    } else if (kind == 1u) {
        sr_wingman_ret = (random32() & 0xffff0000u) | 0xffffu;
        name_shown = 0;   /* 0x49f240 is "" in the image */
        ++sr_keys[SK_NO_INDEX];
    } else {
        uint32_t index = sr_rand(4u);
        sr_wingman_ret = (random32() & 0xffff0000u) | index;
        sr_set32(0x0049d230u, sr_at(SR_NAMES));
        for (unsigned i = 0; i < 4u; ++i) {
            sr_string(SR_NAME_TEXT + 0x40u * i, i == index ? name_len : sr_rand(0x20u));
            sr_w32(SR_NAMES + 334u * i + 0x264u, sr_at(SR_NAME_TEXT + 0x40u * i));
        }
        ++sr_keys[SK_INDEX];
    }
    ++sr_keys[label_len + name_shown ? SK_TEXT : SK_BOX];
    /* The value text: [0x4d16a4] + ([0x4d24f0] ? [0x4d16a8] : [0x4d16ac]). */
    uint32_t v = sr_rand(0x20u), v2 = sr_rand(0x20u), v3 = sr_rand(0x20u);
    sr_string(SR_STRINGS + 0xc0u, v);
    sr_string(SR_STRINGS + 0x120u, v2);
    sr_string(SR_STRINGS + 0x180u, v3);
    sr_set32(0x004d16a4u, sr_at(SR_STRINGS + 0xc0u));
    sr_set32(0x004d16a8u, sr_at(SR_STRINGS + 0x120u));
    sr_set32(0x004d16acu, sr_at(SR_STRINGS + 0x180u));
    uint32_t mode = sr_rand(2u) ? 0u : (sr_rand(2u) ? 1u : random32() | 1u);
    sr_set16(0x004d24f0u, mode);
    ++sr_keys[(mode & 0xffffu) ? SK_MODE_1 : SK_MODE_0];
    sr_op_add(0x0045e208u, SR_EAX, 0, 0, 0);
    return sr_run("label", k);
}

static int shell_rest_cases(void) {
    current_function = "shell-rest";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    sr_world = VirtualAlloc(NULL, SR_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    sr_world_start = malloc(2u * SR_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !sr_world || !sr_world_start)
        return fail("shell-rest: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("shell-rest: the counter word is not free");
    su_pristine_take();
    if (!ir_patch_import("cw3220.dll", "_strcat", (void *)su_strcat)) return fail("shell-rest: import slot not found");
    if (sr_dispatch_setup() || sr_patch_all(1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"calls", sr_calls_case, 2000}, {"label", sr_label_case, 2000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: shell-rest %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) {
        printf("SHELL-REST coverage:");
        for (unsigned i = 0; i < SR_KEYS; ++i) {
            printf(" %s=%u", sr_key_names[i], sr_keys[i]);
            if (!sr_keys[i]) bad = fail("shell-rest: a branch was not reached");
        }
        printf("\nSHELL-REST stand-in calls (original):");
        static const char letters[] = "kRTwx";
        for (const char *l = letters; *l; ++l) printf(" %c=%u", *l, jb_letters[(unsigned char)*l]);
        printf("\n");
    }
    current_case_valid = 0;
    if (sr_patch_all(0)) bad = 1;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
