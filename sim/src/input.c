/* DBSIM's input layer as own code (es2/sim/input.h). Contract: input.md, "the
 * input layer as own code" (every store and call below with its listing address
 * range); "listing" = llvm-objdump -d -M intel of the reference DBSIM.EXE
 * (SHA-256 8561aa47...c4ad). The original's internal callees are own functions
 * here; hooks, other packages' services and the imports are called through the
 * dispatch (es2_sim_loop_call over the bound main loop's call area). Guest
 * state is read and written only through the guest view. */
#include <string.h>
#include "es2/sim/input.h"
#include "es2/sim/memory.h"
#include "es2/sim/services.h"
#include "es2/formats/cfg.h"
#include "es2/sim/image_data.h"

/* ---- guest VAs */
#define VA_COUNTER_POINTER 0x006cbee4u
#define VA_RECORD 0x004d22c6u          /* the control record R (48 bytes) */
#define VA_RECORD_DEVICE 0x004d2390u   /* -> D */
#define VA_ACTIVE 0x004d248cu          /* byte per device id */
#define VA_ID0 0x004d2498u
#define VA_ID_KEYBOARD 0x004d249cu
#define VA_ID_MOUSE 0x004d24a0u
#define VA_ID_JOYSTICK 0x004d24a4u
#define VA_CURRENT 0x006c5eb4u         /* the graphics library's current object per slot (0x480590) */
#define VA_HOTKEYS 0x006bb8b8u         /* 10 entries of 12 bytes: active, list, callback */
#define VA_RING_WRITE 0x006bb8b0u
#define VA_RING_READ 0x006bb8b4u
#define VA_CAPTURE 0x0049eaa5u         /* byte: flight-key capture on */
#define VA_CAPTURE_SKIP 0x0049eb61u    /* byte: no flight-key hot keys */
#define VA_KEYS_DOWN 0x004d2394u       /* three 15-byte key-state arrays */
#define VA_KEYS_UP 0x004d23a3u
#define VA_KEYS_SEEN 0x004d23b2u
#define VA_MOUSE_FIRST 0x0049ec64u     /* byte: the mouse reader ran */
#define VA_MOUSE_CACHE 0x004d241fu     /* the last mouse block (10 bytes) */
#define VA_JOY_INFO 0x006bb310u        /* two JOYINFOEX of 0x34 bytes */
#define VA_JOY_CAPS 0x006bb378u        /* JOYCAPSA buffers at a 0x194 stride, 0x328 bytes each */
#define VA_JOY_RESULT 0x006bb6a0u      /* two MMRESULTs */
#define VA_JOY_PRESENT 0x004a10e8u     /* word: the poll's two device flags (0x0101 in the file) */
#define VA_JOY_OUT 0x004d242cu         /* the joystick poll's 0x1d-byte result */
#define VA_JOY_LAST 0x004d2449u        /* its previous value */
#define VA_JOY_CHANGED 0x004d2466u
#define VA_JOY_VTABLE 0x0049ec74u
#define VA_PREFS_BLOCK 0x004d1f38u     /* 54 bytes */
#define VA_PREFS_PREVIOUS 0x004d1fa4u
#define VA_PREFS_CALLBACKS 0x004d1fdcu /* 54 dwords */
#define VA_PREFS_PATH 0x0049e81cu
#define VA_PREFS_LOADING 0x004d20b4u
#define VA_JOY_MAP 0x004d20b5u         /* 12 bytes (0x45a274) */
#define VA_MOUSE_SCALE_X 0x004a2dccu
#define VA_MOUSE_SCALE_Y 0x004a2dd0u
#define VA_CLIENT_RECT 0x006c5f5au
#define VA_MOUSE_CALLBACKS 0x006c5ed8u /* 10 entries of 12 bytes: active, mask, function */
#define VA_HWND 0x004d3610u
#define VA_WINDOW_RECT 0x004d36ccu     /* x, y, width, height (0x464c10) */
/* Callees (dispatch). */
#define VA_NEW 0x0047749cu
#define VA_NEW_ARRAY 0x004774f0u
#define VA_DELETE 0x00477548u
#define VA_MEMSET 0x0049681au
#define VA_MEMCPY 0x0049680eu
#define VA_MEMCMP 0x004967bau
#define VA_STRCAT 0x004967eau
#define VA_STRNCPY 0x00496760u
#define VA_FOPEN 0x00496802u
#define VA_FCLOSE 0x00496838u
#define VA_OPEN 0x004967c6u
#define VA_CLOSE 0x00496874u
#define VA_READ 0x00496808u
#define VA_GET_CLIENT_RECT 0x00496970u
#define VA_PATH_ROOT 0x0045ee00u
#define VA_HANDLER_ADD 0x00465fecu
#define VA_CLOCK 0x00467828u
#define VA_PREFS_LOAD 0x00459770u      /* hook prefs_load */
#define VA_PREFS_STORE 0x00459b94u     /* hook prefs_store */
#define VA_KEYJOY_LOAD 0x0045b7a8u     /* hook keyjoy_load */
#define VA_MOUSE_QUEUE 0x00453078u     /* hook mouse_queue */
#define VA_CURSOR_FOLLOW 0x00486e7cu
#define VA_VECTOR_NEW 0x0049667cu
#define VA_SURFACE_ATTRIBUTE 0x00481350u
#define VA_JOY_POLL 0x0045c330u
#define VA_STREAM_SLOT_10 0x0047a904u  /* the stream base class's slot +0x10: AL 1 */

static es2_sim_input *bound_input;
void es2_sim_input_bind(es2_sim_input *in) { bound_input = in; }

static const es2_guest_memory *mem(const es2_sim_input *in) { return in->loop->memory; }
static uint32_t r32(const es2_sim_input *in, uint32_t a) { return mem(in)->read32(mem(in)->context, a); }
static uint16_t r16(const es2_sim_input *in, uint32_t a) { return mem(in)->read16(mem(in)->context, a); }
static uint8_t r8(const es2_sim_input *in, uint32_t a) { return es2_sim_read8(mem(in), a); }
static void w32(const es2_sim_input *in, uint32_t a, uint32_t v) { mem(in)->write32(mem(in)->context, a, v); }
static void w16(const es2_sim_input *in, uint32_t a, uint16_t v) { mem(in)->write16(mem(in)->context, a, v); }
static void w8(const es2_sim_input *in, uint32_t a, uint8_t v) { es2_sim_write8(mem(in), a, v); }
static uint32_t sx16(uint16_t v) { return (uint32_t)(int32_t)(int16_t)v; }
static void counter(const es2_sim_input *in, int32_t n) {
    uint32_t p = r32(in, VA_COUNTER_POINTER);
    w32(in, p, r32(in, p) + (uint32_t)n);
}
/* 0x47e08d(a, b): imul, then shrd 16: the low dword of (a * b) >> 16. */
static uint32_t mul16(uint32_t a, uint32_t b) { return (uint32_t)(uint64_t)(((int64_t)(int32_t)a * (int32_t)b) >> 16); }

static int refuse(es2_sim_input *in, uint32_t va, const char *name, const char *reason) {
    in->refused_va = va;
    return es2_sim_loop_refuse(in->loop, va, name, reason);
}
/* The original reads, writes or calls through NULL at va (es2_sim_loop_null_refuse). */
static int null_refuse(es2_sim_input *in, uint32_t va, const char *name, const char *reason) {
    in->refused_va = va;
    return es2_sim_loop_null_refuse(in->loop, va, name, reason);
}
/* D: the original's IDIV of EDX:EAX = dividend by the divisor faults at va
 * (es2_sim_loop_divide_refuse). */
static int divide_refuse(es2_sim_input *in, uint32_t va, uint64_t dividend, uint32_t divisor, const char *name, const char *reason) {
    in->refused_va = va;
    return es2_sim_loop_divide_refuse(in->loop, va, dividend, divisor, 1, name, reason);
}
/* A call through the dispatch with the given entry registers (NULL: zero). */
static int call_regs(es2_sim_input *in, uint32_t va, const uint32_t *slots, unsigned count, const uint32_t regs[3],
                     es2_sim_result *out) {
    es2_sim_loop *l = in->loop;
    uint32_t bytes = 4u * (count ? count : 1u), top = l->stack_hi;
    if (top - l->stack_lo < bytes) return refuse(in, va, NULL, "the own code's call area is exhausted");
    l->stack_hi -= bytes;
    for (unsigned i = 0; i < count; ++i) w32(in, l->stack_hi + 4u * i, slots[i]);
    uint32_t zero[3] = {0, 0, 0};
    es2_sim_result r;
    memset(&r, 0, sizeof(r));
    int refused = es2_sim_call(l->dispatch, va, mem(in)->host(mem(in)->context, l->stack_hi), count, regs ? regs : zero, &r);
    l->stack_hi = top;
    if (refused) {
        in->refused_va = va;
        if (l->outcome == ES2_SIM_LOOP_EXITED) return 1;   /* the process ended inside */
        l->outcome = ES2_SIM_LOOP_REFUSED;
        l->refused_va = va;
        return 1;
    }
    if (out) *out = r;
    return 0;
}
static int call(es2_sim_input *in, uint32_t va, const uint32_t *slots, unsigned count, es2_sim_result *out) {
    return call_regs(in, va, slots, count, NULL, out);
}
/* Locals of the original's frames: zero-filled guest blocks of the call area
 * (own_process.md section 5: where the original reads its own uninitialised
 * locals the modelled value is 0; input.md, "the input layer as own code" names
 * the three readers). */
static uint32_t local_push(es2_sim_input *in, uint32_t bytes) {
    es2_sim_loop *l = in->loop;
    bytes = (bytes + 3u) & ~3u;
    if (l->stack_hi - l->stack_lo < bytes + 0x400u) return 0;
    l->stack_hi -= bytes;
    for (uint32_t i = 0; i < bytes; i += 4u) w32(in, l->stack_hi + i, 0);
    return l->stack_hi;
}
static void local_pop(es2_sim_input *in, uint32_t top) { in->loop->stack_hi = top; }
static uint32_t frame_of(const es2_sim_input *in) { return r32(in, ES2_SIM_VA_FRAME_COUNTER); }
/* A guest address the original would fault on (an object pointer of 0 plus
 * a member offset): refused instead. */
static int bad(uint32_t address) { return address < 0x10000u; }

/* ---- the headless platform: the journal, as the proxy's replay answers
 * (tools/reference-game/proxy/input_journal.h, input_journal.md). */
static es2_journal *journal_of(es2_sim_input *in) { return in->journal ? in->journal : &in->empty; }
static int16_t headless_key(void *user, uint32_t frame, int32_t vk) {
    es2_journal *j = journal_of(user);
    es2_journal_entry *e = es2_journal_take(j, ES2_JOURNAL_KEY, frame, es2_journal_match_first, vk);
    return e ? (int16_t)e->b : es2_journal_key_fallback(j, vk);
}
static uint32_t headless_caps(void *user, uint32_t frame, uint32_t id, uint8_t *caps, uint32_t size) {
    es2_journal *j = journal_of(user);
    es2_journal_entry *e = es2_journal_take(j, ES2_JOURNAL_JOYCAPS, frame, es2_journal_match_first, (int32_t)id);
    if (!e) { ++j->fallbacks; return ES2_JOYERR_PARMS; }
    if (caps && e->blob && e->blob_size == size) memcpy(caps, e->blob, size);
    return (uint32_t)e->b;
}
static uint32_t headless_pos(void *user, uint32_t frame, uint32_t id, uint8_t *info, uint32_t size) {
    es2_journal *j = journal_of(user);
    es2_journal_entry *e = es2_journal_take(j, ES2_JOURNAL_JOYPOS, frame, es2_journal_match_first, (int32_t)id);
    if (!e) { ++j->fallbacks; return ES2_JOYERR_UNPLUGGED; }
    if (info && e->blob && e->blob_size == size) memcpy(info, e->blob, size);
    return (uint32_t)e->b;
}
static uint32_t headless_cursor(void *user, uint32_t frame, int32_t *x, int32_t *y) {
    es2_journal *j = journal_of(user);
    es2_journal_entry *e = es2_journal_take(j, ES2_JOURNAL_CURSOR, frame, NULL, 0);
    if (!e) ++j->fallbacks;
    *x = e ? e->a : j->mouse_x;
    *y = e ? e->b : j->mouse_y;
    return e ? (uint32_t)e->c : 1u;
}
/* The client area the window was made for: the game size [0x4d253e] x
 * [0x4d2542] (launcher.md, "DBSIM's process shell, start-up and exit": the
 * outer size adds the frame and caption; DBSIM has no menu resource) unless the
 * host names another. */
static void headless_client(void *user, int32_t *width, int32_t *height) {
    const es2_sim_input *in = user;
    *width = in->client_width > 0 ? in->client_width : (int32_t)r32(in, 0x004d253eu);
    *height = in->client_height > 0 ? in->client_height : (int32_t)r32(in, 0x004d2542u);
}
void es2_sim_input_headless(es2_sim_input *in, es2_sim_loop *loop, es2_journal *journal, int32_t width, int32_t height) {
    memset(in, 0, sizeof(*in));
    in->loop = loop;
    in->journal = journal;
    es2_journal_init(&in->empty);
    in->client_width = width;
    in->client_height = height;
    in->platform.key_state = headless_key;
    in->platform.joy_caps = headless_caps;
    in->platform.joy_pos = headless_pos;
    in->platform.cursor_pos = headless_cursor;
    in->platform.client_size = headless_client;
    in->platform.user = in;
}

/* ---- the device registry (0x45bda4, 0x45be50, 0x45bee0, 0x45bf04) */
static uint32_t device_slot(const es2_sim_input *in, uint32_t id) { return r32(in, ES2_SIM_VA_DEVICE_SLOTS + 4u * id); }
/* 0x45bee0(rec) (listing 0x45bee0..0x45bef4): the record's active byte and
 * the id's byte cleared. */
static int rec_deactivate(es2_sim_input *in, uint32_t rec) {
    if (bad(rec)) return null_refuse(in, 0x0045bee0u, "device record", "a registered device id without an object (the original faults)");
    w8(in, rec, 0);
    w8(in, VA_ACTIVE + r32(in, rec + 1u), 0);
    return 0;
}
/* The mouse (id word 0x4d24a0, record at object + 0x3a) and the joystick
 * (0x4d24a4, + 0x1f) keep one current device: the previous one's record is
 * deactivated (a slot of 0 gives the address 0 + offset). */
static int deactivate_previous(es2_sim_input *in, uint32_t id_word, uint32_t offset) {
    uint32_t previous = r32(in, id_word);
    if (!previous) return 0;
    uint32_t o = device_slot(in, previous);
    return rec_deactivate(in, o ? o : offset);
}
/* 0x45bda4(rec, id, obj) (listing 0x45bda4..0x45be4d): the record's id, the
 * registry slot, the id's active byte 0; per id the current-device word
 * (id 0 and 1 store the id; 2 and 3 first deactivate the previous record,
 * then store the record's id). */
static int rec_register(es2_sim_input *in, uint32_t rec, uint32_t id, uint32_t obj) {
    w32(in, rec + 1u, id);
    w32(in, ES2_SIM_VA_DEVICE_SLOTS + 4u * id, obj);
    w8(in, VA_ACTIVE + id, 0);
    switch (id) {
    case 0: w32(in, VA_ID0, id); break;
    case 1: w32(in, VA_ID_KEYBOARD, id); break;
    case 2:
        if (deactivate_previous(in, VA_ID_MOUSE, 0x3au)) return 1;
        w32(in, VA_ID_MOUSE, r32(in, rec + 1u));
        break;
    case 3:
        if (deactivate_previous(in, VA_ID_JOYSTICK, 0x1fu)) return 1;
        w32(in, VA_ID_JOYSTICK, r32(in, rec + 1u));
        break;
    default: break;
    }
    return 0;
}
/* 0x45be50(rec) (listing 0x45be50..0x45bedc): for ids 2 and 3 with a current
 * device of that kind, that one's record is deactivated and the word takes
 * this record's id (without one the word stays 0); then the record's active
 * byte and the id's byte are 1. */
static int rec_activate(es2_sim_input *in, uint32_t rec) {
    if (bad(rec)) return null_refuse(in, 0x0045be50u, "device record", "activation of a record of a null object (the original faults)");
    uint32_t id = r32(in, rec + 1u);
    if (id == 2u || id == 3u) {
        uint32_t word = id == 2u ? VA_ID_MOUSE : VA_ID_JOYSTICK;
        if (r32(in, word)) {
            if (deactivate_previous(in, word, id == 2u ? 0x3au : 0x1fu)) return 1;
            w32(in, word, r32(in, rec + 1u));
        }
    }
    w8(in, rec, 1);
    w8(in, VA_ACTIVE + r32(in, rec + 1u), 1);
    return 0;
}
/* 0x480590(obj, k, candidate) (listing 0x480590..0x4805b2): [obj] = 0; the
 * graphics library's slot k takes the candidate when it is free. */
static void object_base(es2_sim_input *in, uint32_t obj, uint32_t k, uint32_t candidate) {
    w32(in, obj, 0);
    if (!r32(in, VA_CURRENT + 4u * k)) w32(in, VA_CURRENT + 4u * k, candidate);
}
/* 0x477b18 / 0x477b7c(obj, callback, list) (identical bodies): the first
 * free hot-key entry. 1 or 0. */
static int hotkey_register(es2_sim_input *in, uint32_t callback, uint32_t list) {
    for (uint32_t k = 0, e = VA_HOTKEYS; k < 10u; ++k, e += 12u) {
        if (r32(in, e)) continue;
        w32(in, e, 1);
        w32(in, e + 8u, callback);
        w32(in, e + 4u, list);
        return 1;
    }
    return 0;
}

/* ---- the keyboard device K (0xe bytes: +0 flags, +4 record, +9 mode, +0xd drain byte) */
/* 0x45c01c(K, mode) with 0x4779a8 (listings 0x45c01c..0x45c054,
 * 0x4779a8..0x477a0e): the base object, the window handler 0x477bec when K
 * is the current keyboard (0x477a4c: handler_add, then [K] & 1, which is 0:
 * [K] was just cleared, so the atexit, abort-notify and hot-key
 * registrations of 0x4779d5..0x4779fe never run), counter + 1; the record
 * (id 1), the mode, the drain byte, counter + 1. */
static int keyboard_ctor(es2_sim_input *in, uint32_t k, uint32_t mode) {
    object_base(in, k, 1, k);
    if (k == r32(in, VA_CURRENT + 4u)) {
        uint32_t a[1] = {0x00477becu};
        if (call(in, VA_HANDLER_ADD, a, 1, NULL)) return 1;
        if (r32(in, k) & 1u)
            return refuse(in, 0x004779d5u, "keyboard device", "the device flag set right after the base object cleared it");
    }
    counter(in, 1);
    if (rec_register(in, k + 4u, 1, k + 4u)) return 1;
    w32(in, k + 9u, mode);
    w8(in, k + 0xdu, 0);
    counter(in, 1);
    return 0;
}
/* 0x477ad4(K) (listing 0x477ad4..0x477b14): one key from the ring (the read
 * index unmasked as the slot, the next index masked to 8 bits); bit 1 of
 * [K] cleared when the ring becomes empty. EAX: 0, or the read index's high
 * word with the key word. */
static uint32_t key_take(es2_sim_input *in, uint32_t k) {
    uint32_t w = r32(in, VA_RING_WRITE), r = r32(in, VA_RING_READ);
    if (w == r) return 0;
    uint32_t slot = r32(in, VA_RING_READ);
    uint32_t key = (slot & 0xffff0000u) | r16(in, ES2_SIM_VA_KEY_RING + 2u * slot);
    uint32_t next = (r + 1u) & 0xffu;
    w32(in, VA_RING_READ, next);
    if (next == w) w32(in, k, r32(in, k) & ~2u);
    return key;
}
/* 0x45c0a0(K) (listing 0x45c0a0..0x45c162): modes 0x10000 and 0x20000 take
 * one key when bit 1 is set and, with the drain byte, the rest; 0x40000 gives
 * 0; any other mode returns the caller's ESI, which 0x45bbc4 loaded with the
 * id 1. */
static uint32_t keyboard_read(es2_sim_input *in, uint32_t k) {
    uint32_t mode = r32(in, k + 9u), v;
    if (mode == 0x10000u || mode == 0x20000u) {
        v = (r32(in, k) & 2u) ? key_take(in, k) : 0;
        if (r8(in, k + 0xdu))
            while (r32(in, k) & 2u) key_take(in, k);
        return v;
    }
    if (mode == 0x40000u) return 0;
    return 1;
}

/* ---- the mouse device M (0x3f bytes; +0xe .. +0x23 the bounds, +0x3a record) */
/* 0x45c1d4(M) with 0x4805e0 (listings 0x45c1d4..0x45c1fd,
 * 0x4805e0..0x480652): the base object (slot 2), five member constructors
 * 0x49545b (no stores), the window handler 0x4809f8 when M is the current
 * mouse (0x480694: handler_add, then the configuration only when [M] & 1,
 * which is 0, so 0x48080c/0x480738/0x480764 and the abort-notify
 * registration 0x480804 never run), counter + 1; the record (id 2), counter
 * + 1. */
static int mouse_ctor(es2_sim_input *in, uint32_t m) {
    object_base(in, m, 2, m);
    if (m == r32(in, VA_CURRENT + 8u)) {
        uint32_t a[1] = {0x004809f8u};
        if (call(in, VA_HANDLER_ADD, a, 1, NULL)) return 1;
        if (r32(in, m) & 1u)
            return refuse(in, 0x004806acu, "mouse device", "the device flag set right after the base object cleared it");
    }
    counter(in, 1);
    if (rec_register(in, m + 0x3au, 2, m + 0x3au)) return 1;
    counter(in, 1);
    return 0;
}
/* 0x45bd8c -> 0x45bf68: the current mouse object (record - 0x3a) or 0. */
static uint32_t current_mouse(const es2_sim_input *in) {
    uint32_t id = r32(in, VA_ID_MOUSE);
    if (!id) return 0;
    uint32_t o = device_slot(in, id);
    return o ? o - 0x3au : 0;
}
/* 0x45bd84 -> 0x45bf40: the current keyboard object (record - 4) or 0. */
static uint32_t current_keyboard(const es2_sim_input *in) {
    uint32_t id = r32(in, VA_ID_KEYBOARD);
    if (!id) return 0;
    uint32_t o = device_slot(in, id);
    return o ? o - 4u : 0;
}

/* ---- the flight-key capture 0x45b68c(on) (listing 0x45b68c..0x45b732) */
static int flight_capture(es2_sim_input *in, uint8_t on) {
    if (on == r8(in, VA_CAPTURE)) return 0;
    uint32_t k = current_keyboard(in);
    if (!k) return 0;
    if (on && !r8(in, VA_CAPTURE_SKIP)) {
        hotkey_register(in, 0x0045a324u, 0x0049eb0bu);   /* flight keys: hook flight_key */
        hotkey_register(in, 0x0045a498u, 0x0049eb35u);   /* command keys: hook command_queue */
    }
    w8(in, VA_CAPTURE, (uint8_t)(r8(in, VA_CAPTURE) ^ 1u));
    if (!r8(in, VA_CAPTURE)) {
        static const uint32_t arrays[3] = {VA_KEYS_DOWN, VA_KEYS_UP, VA_KEYS_SEEN};
        for (unsigned i = 0; i < 3u; ++i) {
            uint32_t a[3] = {arrays[i], 0, 15};
            if (call(in, VA_MEMSET, a, 3, NULL)) return 1;
        }
    }
    return 0;
}

/* ---- the prefs block (0x459958, the callbacks, 0x45972c, 0x45a1b8) */
/* 0x459958(index, value, flag) (listing 0x459958..0x45999a): the previous
 * copy, the byte, then the index's callback with the value slot when the
 * flag byte is set. EAX the index (or the callback's), EDX the index
 * sign-extended (or the callback's), ECX the value. */
int es2_sim_prefs_set(es2_sim_input *in, uint32_t index, uint32_t value, uint32_t flag, es2_sim_result *out) {
    uint32_t i = sx16((uint16_t)index);
    es2_sim_result r = {index, i, value, 0, 0, 0};
    w8(in, VA_PREFS_PREVIOUS + i, r8(in, VA_PREFS_BLOCK + i));
    w8(in, VA_PREFS_BLOCK + i, (uint8_t)value);
    if ((uint8_t)flag && r32(in, VA_PREFS_CALLBACKS + 4u * i)) {
        /* At the call EAX = EDX = the index sign-extended (0x45998a), ECX the callback. */
        uint32_t a[1] = {value}, target = r32(in, VA_PREFS_CALLBACKS + 4u * i), regs[3] = {i, i, target};
        es2_sim_result c;
        if (call_regs(in, target, a, 1, regs, &c)) return 1;
        r.eax = c.eax; r.edx = c.edx; r.ecx = value;
    }
    if (out) *out = r;
    return 0;
}
/* 0x477904 (listing 0x477904..0x47799a): the 8-byte joystick summary from
 * the two devices' results and capabilities. */
int es2_sim_joystick_summary(es2_sim_input *in, es2_sim_result *out) {
    const uint32_t s = ES2_SIM_VA_JOY_SUMMARY;
    uint32_t a[3] = {s, 0, 8};
    es2_sim_result m;
    if (call(in, VA_MEMSET, a, 3, &m)) return 1;
    w16(in, s, r32(in, VA_JOY_RESULT + 4u) ? 1u : 2u);
    uint32_t sum = r32(in, 0x006bb3b4u) + r32(in, 0x006bb548u);
    uint16_t buttons = sum < 8u ? (uint16_t)(r16(in, 0x006bb3b4u) + r16(in, 0x006bb548u)) : 8u;
    w16(in, s + 2u, buttons);
    uint8_t caps = r8(in, 0x006bb3d8u);
    int second_ok = r32(in, VA_JOY_RESULT + 4u) == 0;
    w8(in, s + 4u, (caps & 1u) || second_ok ? 1u : 0u);
    uint8_t rudder = (caps & 2u) || second_ok ? 1u : 0u;
    w8(in, s + 5u, rudder);
    w8(in, s + 6u, (uint8_t)(r8(in, 0x006bb3d8u) & 0x10u));
    if (out) {
        out->eax = s;
        out->edx = rudder;
        out->ecx = (m.ecx & 0xffff0000u) | buttons;
    }
    return 0;
}
/* 0x459d3c(v) (listing 0x459d3c..0x459d5e): the throttle-lever word
 * 0x49a06e = 1 when the summary's byte 4 is set and v's byte is 1, else 0;
 * EAX = EDX = that value. */
static int lever_word(es2_sim_input *in, uint32_t v, es2_sim_result *out) {
    es2_sim_result s;
    if (es2_sim_joystick_summary(in, &s)) return 1;
    uint32_t d = r8(in, s.eax + 4u) && (uint8_t)v == 1u ? 1u : 0u;
    w16(in, 0x0049a06eu, (uint16_t)d);
    if (out) { out->eax = d; out->edx = d; out->ecx = s.ecx; }
    return 0;
}
/* 0x459d68(v): [0x4aaaa8] = v's byte != 0. */
static void flag_4aaaa8(es2_sim_input *in, uint32_t v) { w32(in, 0x004aaaa8u, (uint8_t)v ? 1u : 0u); }
/* 0x45a274(set) (listing 0x45a274..0x45a307): the 12-byte joystick button map
 * 0x4d20b5 for set 0 or 1 (cleared when the summary's word is 0). EAX the map;
 * EDX and ECX _memset's when cleared, else the summary's (row joystick_map). */
static int joystick_map(es2_sim_input *in, uint32_t set, es2_sim_result *out) {
    static const uint8_t maps[2][12] = {{1, 0, 2, 2, 1, 2, 4, 6, 0x12, 0x13, 9, 0xa},
                                        {1, 0, 2, 2, 1, 2, 0xd, 0xe, 0x12, 0x13, 9, 0xa}};
    es2_sim_result s;
    if (es2_sim_joystick_summary(in, &s)) return 1;
    if (!r16(in, s.eax)) {
        /* 0x45a28d: the summary's EAX, EDX and ECX are live at the call. */
        uint32_t a[3] = {VA_JOY_MAP, 0, 12}, regs[3] = {s.eax, s.edx, s.ecx};
        if (call_regs(in, VA_MEMSET, a, 3, regs, &s)) return 1;
    } else if (set < 2u) {
        for (unsigned i = 0; i < 12u; ++i) w8(in, VA_JOY_MAP + i, maps[set][i]);
    }
    if (out) { out->eax = VA_JOY_MAP; out->edx = s.edx; out->ecx = s.ecx; }
    return 0;
}

/* ---- the joystick device J (0x2c bytes: +0 flags, +4 four axes and the
 * button and hat bytes, +0x16 axis bits, +0x18 slot, +0x1f record, +0x24
 * vtable 0x49ec74, +0x28 response mode) */
/* 0x477674(J, k) (listing 0x477674..0x477713): the slot, the axis bits 0;
 * per device 0 and 1 joyGetDevCapsA(i, 0x6bb378 + 0x194 i, 0x328) into the
 * result, the JOYINFOEX size 0x34 and flags 0xccf, and joyGetPosEx(i) into
 * the result when the caps call succeeded; [J] |= 1 when device 0 answered. */
static int joystick_open(es2_sim_input *in, uint32_t j, uint32_t k) {
    w32(in, j + 0x18u, k);
    w16(in, j + 0x16u, 0);
    for (uint32_t i = 0; i < 2u; ++i) {
        uint32_t info = VA_JOY_INFO + 0x34u * i;
        uint32_t a[3] = {i, VA_JOY_CAPS + 0x194u * i, 0x328u};
        es2_sim_result r;
        if (call(in, 0x00496a66u, a, 3, &r)) return 1;          /* joyGetDevCapsA */
        w32(in, VA_JOY_RESULT + 4u * i, r.eax);
        w32(in, info, 0x34u);
        w32(in, info + 4u, 0xccfu);
        if (!r32(in, VA_JOY_RESULT + 4u * i)) {
            uint32_t b[2] = {i, info};
            if (call(in, 0x00496a6cu, b, 2, &r)) return 1;      /* joyGetPosEx */
            w32(in, VA_JOY_RESULT + 4u * i, r.eax);
        }
    }
    if (!r32(in, VA_JOY_RESULT)) w32(in, j, r32(in, j) | 1u);
    return 0;
}
/* 0x45c298(J, id, w, b, mode) with 0x4775dc (listings 0x45c298..0x45c2de,
 * 0x4775dc..0x47762f): the base object (slot id), the device open when J is
 * the current one, the axis bits 7, counter + 1; the record, the vtable, the
 * response mode, counter + 1. The word and byte arguments are passed on to
 * 0x477674, which does not read them. */
static int joystick_ctor(es2_sim_input *in, uint32_t j, uint32_t id, uint32_t mode) {
    object_base(in, j, id, j);
    if (j == r32(in, VA_CURRENT + 4u * id) && joystick_open(in, j, id)) return 1;
    w16(in, j + 0x16u, 7);
    counter(in, 1);
    if (rec_register(in, j + 0x1fu, id, j + 0x1fu)) return 1;
    w32(in, j + 0x24u, VA_JOY_VTABLE);
    w32(in, j + 0x28u, mode);
    counter(in, 1);
    return 0;
}
/* 0x45c2e0(J, flags) with 0x477630 (listings 0x45c2e0..0x45c32d,
 * 0x477630..0x477671): counter - 1, the vtable, the id's byte 0, the current
 * joystick word 0 when it is this id; counter - 1, the slot cleared with
 * [J] &= ~1 (0x477714) when it is set; delete with flag bit 0. */
/* Exit registers in r (entered with the caller's): EAX the counter
 * pointer, EDX the slot k (0x477642); a cleared slot leaves EAX = k, EDX =
 * 0, ECX = J (0x477650..); the delete leaves its EAX and EDX and ECX = J.
 * For J = 0 EDX and ECX stay. */
static int joystick_dtor(es2_sim_input *in, uint32_t j, uint32_t flags, es2_sim_result *r) {
    counter(in, -1);
    r->eax = r32(in, VA_COUNTER_POINTER);
    if (!j) return 0;
    w32(in, j + 0x24u, VA_JOY_VTABLE);
    uint32_t id = r32(in, j + 0x20u);
    w8(in, VA_ACTIVE + id, 0);
    if (id == r32(in, VA_ID_JOYSTICK)) w32(in, VA_ID_JOYSTICK, 0);
    counter(in, -1);
    r->eax = r32(in, VA_COUNTER_POINTER);
    uint32_t k = r->edx = r32(in, j + 0x18u);
    if (r32(in, VA_CURRENT + 4u * k)) {
        w32(in, j, r32(in, j) & ~1u);
        w32(in, VA_CURRENT + 4u * k, 0);
        r->eax = k; r->edx = 0; r->ecx = j;
    }
    if (flags & 1u) {
        uint32_t a[1] = {j};
        es2_sim_result d;
        if (call(in, VA_DELETE, a, 1, &d)) return 1;
        r->eax = d.eax; r->edx = d.edx; r->ecx = j;
    }
    return 0;
}
/* 0x496406(o, 0x24) then the virtual destructor with flags 3 (listing
 * 0x459e72..0x459e99): counter += the type record's dword +0x20 (t = [vt -
 * [vt - 4] - 0xc], read from the image), then slot 0. */
static int joystick_destroy(es2_sim_input *in, uint32_t j) {
    uint32_t vt = r32(in, j + 0x24u);
    uint32_t type = r32(in, vt - r32(in, vt - 4u) - 0xcu), n;
    /* The count from the own RTTI_COUNT runs (CODE is no-access). */
    if (es2_sim_dbsim_rtti_count(type + 0x20u, &n)) n = r32(in, type + 0x20u);
    counter(in, (int32_t)n);
    uint32_t dtor = r32(in, vt);
    uint32_t a[2] = {j, 3};
    return call(in, dtor, a, 2, NULL);
}
/* 0x459df0 (listing 0x459df0..0x45a1b7): prefs byte 0xc 1 or 0 builds the
 * joystick device; without device 0 it is destroyed (with byte 1 the
 * mapping bytes 0xd..0x18 are then cleared through the callbacks); with it,
 * byte 0 sets the maps 0x45a274(0) and (1) into 0xd..0x24 and stores them
 * (prefs_store(0x19, 0x49e9a8)), and both activate the record. The runtime
 * record and its context words are the original's frame only. */
static int joystick_prefs(es2_sim_input *in) {
    uint8_t kind = r8(in, VA_PREFS_BLOCK + 0xcu);
    if (kind > 1u) return 0;
    uint32_t a[1] = {0x2cu};
    es2_sim_result r;
    if (call(in, VA_NEW, a, 1, &r)) return 1;
    uint32_t j = r.eax;
    if (j) {
        if (joystick_ctor(in, j, 3, 1)) return 1;
        counter(in, -2);
    }
    if (!j) return 0;
    if (!(r32(in, j) & 1u)) {
        if (joystick_destroy(in, j)) return 1;
        if (kind == 1u)
            for (uint32_t i = 0xdu; i <= 0x18u; ++i)
                if (es2_sim_prefs_set(in, i, 0, 1, NULL)) return 1;
        return 0;
    }
    if (kind == 0u) {
        if (joystick_map(in, 0, NULL) || es2_sim_prefs_set(in, 0xc, 1, 0, NULL)) return 1;
        for (uint32_t i = 0; i < 12u; ++i)
            if (es2_sim_prefs_set(in, 0xdu + i, r8(in, VA_JOY_MAP + i), 1, NULL)) return 1;
        if (joystick_map(in, 1, NULL)) return 1;
        for (uint32_t i = 0; i < 12u; ++i)
            if (es2_sim_prefs_set(in, 0x19u + i, r8(in, VA_JOY_MAP + i), 1, NULL)) return 1;
        uint32_t b[2] = {0x19u, 0x0049e9a8u};
        if (call(in, VA_PREFS_STORE, b, 2, NULL)) return 1;
    }
    return rec_activate(in, j + 0x1fu);
}
/* 0x459d88 (listing 0x459d88..0x459dec): "vol\" + the voice archive name of
 * 0x45ef84 (byte 7 of "simvoice" becomes the language letter [0x4d2536]
 * unless it is 'r') + ".vol" below the install root, opened "rb": 1 when it
 * exists (closed again), else 0. */
static int voice_archive(es2_sim_input *in, uint8_t *found) {
    uint32_t top = in->loop->stack_hi, b = local_push(in, 0x20u);
    if (!b) return refuse(in, 0x00459d88u, "voice archive test", "the own code's call area is exhausted");
    for (uint32_t i = 0; i < 5u; ++i) w8(in, b + i, r8(in, 0x0049ea82u + i));
    uint8_t language = r8(in, 0x004d2536u);
    if ((int8_t)language != 0x72) w8(in, 0x0049f260u, language);
    uint32_t a[2] = {b, 0x0049f259u};
    es2_sim_result r;
    int failed = call(in, VA_STRCAT, a, 2, NULL);
    a[1] = 0x0049ea87u;
    failed = failed || call(in, VA_STRCAT, a, 2, NULL);
    uint32_t c[1] = {b};
    failed = failed || call(in, VA_PATH_ROOT, c, 1, &r);
    if (!failed) {
        uint32_t d[2] = {r.eax, 0x0049ea8cu};
        failed = call(in, VA_FOPEN, d, 2, &r);
    }
    *found = 0;
    if (!failed && r.eax) {
        uint32_t e[1] = {r.eax};
        failed = call(in, VA_FCLOSE, e, 1, NULL);
        *found = 1;
    }
    local_pop(in, top);
    return failed;
}
/* 0x45972c (listing 0x45972c..0x45976c) and 0x45a1b8 (0x45a1b8..0x45a273):
 * the prefs path once, the 54 callbacks cleared, then with the loading byte
 * set: five callbacks (0x459c74), prefs_load(1), the voice-archive test into
 * 0x49e9a5 (without the archive bytes 2 and 3 are cleared through the
 * callbacks), the joystick device, 0x459d68(byte 8), the lever word when the
 * summary has no lever, 0x49e9a4 = 0xf0, the loading byte 0. */
static int prefs_read(es2_sim_input *in) {
    if (!r8(in, VA_PREFS_PATH)) {
        uint32_t a[1] = {0x0049e994u};
        es2_sim_result r;
        if (call(in, VA_PATH_ROOT, a, 1, &r)) return 1;
        uint32_t b[3] = {VA_PREFS_PATH, r.eax, 0xffu};
        if (call(in, VA_STRNCPY, b, 3, NULL)) return 1;
    }
    for (uint32_t i = 0; i < 0x36u; ++i) w32(in, VA_PREFS_CALLBACKS + 4u * i, 0);
    w8(in, VA_PREFS_LOADING, 1);
    static const struct { uint32_t index, function; } callbacks[5] = {
        {0, 0x00459cb4u}, {1, 0x00459c88u}, {2, 0x00459ce0u}, {0xe, 0x00459d3cu}, {8, 0x00459d68u}};
    for (unsigned i = 0; i < 5u; ++i) w32(in, VA_PREFS_CALLBACKS + 4u * callbacks[i].index, callbacks[i].function);
    uint32_t one[1] = {1};
    if (call(in, VA_PREFS_LOAD, one, 1, NULL)) return 1;
    uint8_t found;
    if (voice_archive(in, &found)) return 1;
    w8(in, 0x0049e9a5u, found);
    if (!found && (es2_sim_prefs_set(in, 2, 0, 1, NULL) || es2_sim_prefs_set(in, 3, 0, 1, NULL))) return 1;
    if (joystick_prefs(in)) return 1;
    flag_4aaaa8(in, r8(in, 0x004d1f40u));
    es2_sim_result s;
    if (es2_sim_joystick_summary(in, &s)) return 1;
    if (!r8(in, s.eax + 4u) && lever_word(in, 0, NULL)) return 1;
    w8(in, 0x0049e9a4u, 0xf0u);
    w8(in, VA_PREFS_LOADING, 0);
    return 0;
}

/* ---- the device poll and its readers */
/* 0x45c524(id) (listing 0x45c524..0x45c541): the joystick object (record -
 * 0x1f) or 0; ECX the id (pop ecx). */
static uint32_t joystick_object(const es2_sim_input *in, uint32_t id) {
    uint32_t o = device_slot(in, id);
    return o ? o - 0x1fu : 0;
}
/* 0x477720(J) (listing 0x477720..0x477859): joyGetPosEx for devices 0 and 1
 * (a failure sets the axes to 0x8000 and POV and buttons to 0, and clears
 * the device's byte of the word 0x4a10e8's copy); J+4..+0x15 cleared; with
 * device 0: x, y, z (or device 1's x without JOYCAPS_HASZ), r (or device 1's
 * y without HASR), the POV direction bit when JOYCAPS_HASPOV, the buttons of
 * both devices (device 1's shifted by the low byte of caps dword 0x6bb3e4). */
static int joystick_read(es2_sim_input *in, uint32_t j) {
    uint16_t present = r16(in, VA_JOY_PRESENT);
    uint8_t ok[2] = {(uint8_t)present, (uint8_t)(present >> 8)};
    for (uint32_t i = 0; i < 2u; ++i) {
        uint32_t info = VA_JOY_INFO + 0x34u * i, a[2] = {i, info};
        es2_sim_result r;
        if (call(in, 0x00496a6cu, a, 2, &r)) return 1;          /* joyGetPosEx */
        if (r.eax) {
            ok[i] = 0;
            w32(in, info + 0x14u, 0x8000u);
            w32(in, info + 0x10u, 0x8000u);
            w32(in, info + 0x0cu, 0x8000u);
            w32(in, info + 0x08u, 0x8000u);
            w32(in, info + 0x28u, 0);
            w32(in, info + 0x20u, 0);
        }
    }
    if (bad(j + 4u)) return null_refuse(in, 0x00477787u, "joystick poll", "a joystick id without an object (the original faults)");
    uint32_t a[3] = {j + 4u, 0, 0x12u};
    if (call(in, VA_MEMSET, a, 3, NULL)) return 1;
    if (!ok[0]) return 0;
    uint32_t shift = r32(in, 0x006bb3e4u);
    w32(in, j + 4u, r32(in, 0x006bb318u));
    w32(in, j + 8u, r32(in, 0x006bb31cu));
    w32(in, j + 0xcu, (r8(in, 0x006bb3d8u) & 1u) ? r32(in, 0x006bb320u) : r32(in, 0x006bb34cu));
    w32(in, j + 0x10u, (r8(in, 0x006bb3d8u) & 2u) ? r32(in, 0x006bb324u) : r32(in, 0x006bb350u));
    if (r8(in, 0x006bb3d8u) & 0x10u) {
        uint32_t pov = r32(in, 0x006bb338u);
        if (pov == 0) w8(in, j + 0x15u, 1);
        else if (pov == 0x4650u) w8(in, j + 0x15u, 2);
        else if (pov == 0x6978u) w8(in, j + 0x15u, 4);
        else if (pov == 0x2328u) w8(in, j + 0x15u, 8);
    }
    w8(in, j + 0x14u, r8(in, 0x006bb330u));
    unsigned count = shift & 0x1fu;   /* shl al, cl: the count masked to 5 bits */
    uint8_t high = count < 8u ? (uint8_t)(r8(in, 0x006bb364u) << count) : 0u;
    w8(in, j + 0x14u, (uint8_t)(r8(in, j + 0x14u) | high));
    return 0;
}
/* 0x47785c(J, out) (listing 0x47785c..0x4778a7): with [J] & 1 each axis
 * dword becomes ((v << 8) / 0xffff) - 0x80 (signed) in place, then the 18
 * bytes from J+4 are copied out; AL 1, else 0. 0x4778a8 (0x4778a8..0x477903)
 * then reduces each axis to its sign and copies again. */
static int axes_scale(es2_sim_input *in, uint32_t j, uint32_t out, int sign) {
    if (!(r32(in, j) & 1u)) return 0;
    for (uint32_t k = 0; k < 4u; ++k) {
        int32_t v = (int32_t)r32(in, j + 4u + 4u * k);
        w32(in, j + 4u + 4u * k, (uint32_t)((int32_t)((uint32_t)v << 8) / 0xffff - 0x80));
    }
    for (uint32_t i = 0; i < 0x12u; ++i) w8(in, out + i, r8(in, j + 4u + i));
    if (!sign) return 1;
    for (uint32_t k = 0; k < 4u; ++k) {
        int32_t v = (int32_t)r32(in, j + 4u + 4u * k);
        w32(in, j + 4u + 4u * k, v > 0 ? 1u : v < 0 ? 0xffffffffu : 0u);
    }
    for (uint32_t i = 0; i < 0x12u; ++i) w8(in, out + i, r8(in, j + 4u + i));
    return 1;
}
/* 0x45c330(J) (listing 0x45c330..0x45c522), the joystick's poll virtual:
 * the axes through 0x47785c (modes 0, 1) or 0x4778a8 (2) into a local (any
 * other mode leaves it unwritten: residue, modelled 0), then per axis the
 * dead zone 25 and scale (1 << (b + 8)) / ((1 << b) - 25) >> 8 except in
 * mode 1, which instead maps |v| > 25 quadratically with 0x47e068(1,
 * ((1 << b) - 25)^2 >> (b + 1)); the 8 buttons and 4 hat bits as bytes;
 * the change byte from _memcmp against the last result, which is updated
 * when it differs. EAX 0x4d242c. */
static int joystick_poll(es2_sim_input *in, uint32_t j, es2_sim_result *regs) {
    uint32_t top = in->loop->stack_hi, buf = local_push(in, 0x30u);
    if (!buf) return refuse(in, VA_JOY_POLL, "joystick poll", "the own code's call area is exhausted");
    uint32_t mode = r32(in, j + 0x28u);
    if (mode < 2u) axes_scale(in, j, buf, 0);
    else if (mode == 2u) axes_scale(in, j, buf, 1);
    uint32_t bits = r16(in, j + 0x16u);
    uint32_t big = 1u << ((bits + 8u) & 0x1fu), small = (1u << (bits & 0x1fu)) - 0x19u;
    if (!small) { local_pop(in, top); return divide_refuse(in, 0x0045c3bdu, 0, 0, "joystick poll", "axis bits giving a zero divisor (the original faults)"); }
    int32_t scale = (int32_t)big / (int32_t)small;
    uint32_t e = (1u << (r16(in, j + 0x16u) & 0x1fu)) - 0x19u;
    int32_t square = (int32_t)(e * e) >> ((r16(in, j + 0x16u) + 1u) & 0x1fu);
    if (!square) { local_pop(in, top); return divide_refuse(in, 0x0045c3e9u, 0, 0, "joystick poll", "axis bits giving a zero divisor (the original faults)"); }
    int32_t curve = (int32_t)(0x10000 / square);   /* 0x47e068(1, square): (1 << 16) / square */
    for (uint32_t k = 0; k < 4u; ++k) {
        uint32_t a = buf + 4u * k, o = VA_JOY_OUT + 4u * k;
        if (r32(in, j + 0x28u) != 1u && scale) {
            int32_t v = (int32_t)r32(in, a);
            v = v > 0x19 ? v - 0x19 : v < -0x19 ? v + 0x19 : 0;
            w32(in, a, (uint32_t)v);
            w32(in, a, (uint32_t)(scale * (int32_t)r32(in, a)));
            w32(in, a, (uint32_t)((int32_t)r32(in, a) >> 8));
        }
        uint32_t m = r32(in, j + 0x28u);
        if (m == 0 || m == 2u) {
            w32(in, o, r32(in, a));
        } else if (m == 1u) {
            int32_t v = (int32_t)sx16(r16(in, a));
            if (v < 0x19 && v > -0x19) {
                w32(in, o, 0);
            } else {
                v = v < 0 ? v + 0x19 : v - 0x19;
                w32(in, o, mul16((uint32_t)(v * v), (uint32_t)curve));
                if (v < 0) w32(in, o, (uint32_t)-(int32_t)r32(in, o));
            }
        }
    }
    for (uint32_t i = 0; i < 8u; ++i) w8(in, 0x004d243cu + i, (r8(in, buf + 0x10u) >> i) & 1u);
    for (uint32_t i = 0; i < 4u; ++i) w8(in, 0x004d2444u + i, (r8(in, buf + 0x11u) >> i) & 1u);
    local_pop(in, top);
    uint32_t c[3] = {VA_JOY_OUT, VA_JOY_LAST, 0x1du};
    es2_sim_result r;
    if (call(in, VA_MEMCMP, c, 3, &r)) return 1;
    w8(in, VA_JOY_CHANGED, (uint8_t)r.eax);
    if ((uint8_t)r.eax) {
        uint32_t d[3] = {VA_JOY_LAST, VA_JOY_OUT, 0x1du};
        if (call(in, VA_MEMCPY, d, 3, &r)) return 1;
    }
    *regs = r;
    regs->eax = VA_JOY_OUT;
    return 0;
}
/* 0x45bc10(buf, id) (listing 0x45bc10..0x45bc70): for ids 3 and 4 with an
 * active record, the object's poll virtual (+4) and, when it answers, its
 * 0x1d bytes into buf. */
static int joystick_reader(es2_sim_input *in, uint32_t buf, uint32_t id) {
    if (id - 3u >= 2u) return 0;
    uint32_t o = device_slot(in, id), j = o ? o - 0x1fu : 0;
    if (bad(j + 0x1fu)) return null_refuse(in, 0x0045bc3eu, "joystick reader", "a joystick id without an object (the original faults)");
    if (!r8(in, j + 0x1fu)) return 0;
    uint32_t poll = r32(in, r32(in, j + 0x24u) + 4u), a[1] = {j};
    es2_sim_result r;
    if (call(in, poll, a, 1, &r)) return 1;
    uint32_t result = r.eax;
    if (!result) return 0;
    uint32_t b[3] = {buf, result, 0x1du};
    return call(in, VA_MEMCPY, b, 3, NULL);
}
/* 0x45bc74(buf, id) (listing 0x45bc74..0x45bd21): the first-call byte; for
 * id 2 with an active record, 0x45c248 -> 0x480834 (the constant 0x6c5f50,
 * never 0: the cached branch 0x45bcfa is unreachable) copied into buf and
 * into the cache 0x4d241f. AL 0 only for an inactive record. */
static int mouse_reader(es2_sim_input *in, uint32_t buf, uint32_t id, int *al, es2_sim_result *regs) {
    if (!r8(in, VA_MOUSE_FIRST)) w8(in, VA_MOUSE_FIRST, (uint8_t)(r8(in, VA_MOUSE_FIRST) + 1u));
    *al = 1;
    if (id != 2u) return 0;
    uint32_t o = device_slot(in, id), m = o ? o - 0x3au : 0;
    if (bad(m + 0x3au)) return null_refuse(in, 0x0045bcb4u, "mouse reader", "a mouse id without an object (the original faults)");
    if (!r8(in, m + 0x3au)) {
        *al = 0;
        regs->eax = 0; regs->edx = m; regs->ecx = m + 0x3au;
        return 0;
    }
    const uint32_t s = ES2_SIM_VA_MOUSE_BLOCK;
    uint16_t buttons = r16(in, s + 8u);
    w16(in, buf + 8u, buttons);
    w32(in, buf, r32(in, s));
    uint32_t y = r32(in, s + 4u);
    w32(in, buf + 4u, y);
    uint16_t dx = r16(in, s + 8u);
    w16(in, VA_MOUSE_CACHE + 8u, dx);
    uint32_t x = r32(in, s);
    w32(in, VA_MOUSE_CACHE, x);
    uint32_t y2 = r32(in, s + 4u);
    w32(in, VA_MOUSE_CACHE + 4u, y2);
    regs->eax = (y2 & 0xffffff00u) | 1u;
    regs->edx = (y & 0xffff0000u) | dx;
    regs->ecx = x;
    return 0;
}
/* 0x45bbc4(p, id) (listing 0x45bbc4..0x45bc0d): for id 1 with an active
 * record the keyboard's next key as the word [p]. */
int es2_sim_key_reader(es2_sim_input *in, uint32_t p, uint32_t id, es2_sim_result *out) {
    es2_sim_result r = {0, 0, 0, 0, 0, 0};
    if (id != 1u) {
        r.eax = ((id - 1u) & 0xffffff00u) | 1u;
        if (out) { out->eax = r.eax; }
        return 0;
    }
    uint32_t o = device_slot(in, 1), k = o ? o - 4u : 0;
    if (bad(k + 4u)) return null_refuse(in, 0x0045bbebu, "key reader", "a keyboard id without an object (the original faults)");
    r.edx = k;
    if (!r8(in, k + 4u)) {
        r.eax = 0; r.ecx = k + 4u;
    } else {
        uint32_t v = keyboard_read(in, k);
        w16(in, p, (uint16_t)v);
        r.eax = (v & 0xffffff00u) | 1u;
        r.ecx = p;
    }
    if (out) { out->eax = r.eax; out->edx = r.edx; out->ecx = r.ecx; }
    return 0;
}
/* 0x45baa8 (listing 0x45baa8..0x45bbc3): D cleared; the key word (bits 12
 * and 13 cleared); with a joystick id the poll 0x45c544 -> 0x477720 and the
 * reader 0x45bc10 into a local copied to D+0xd (its 0x1d bytes are copied
 * whether or not the reader wrote them: residue, modelled 0); with a mouse
 * id the reader 0x45bc74 and, when it answers, x, y and three button bytes.
 * EAX D. */
int es2_sim_device_poll(es2_sim_input *in, es2_sim_result *out) {
    const uint32_t d = ES2_SIM_VA_DEVICE_BLOCK;
    uint32_t top = in->loop->stack_hi, local = local_push(in, 0x4cu);
    if (!local) return refuse(in, ES2_SIM_VA_DEVICE_POLL, "device poll", "the own code's call area is exhausted");
    es2_sim_result regs;
    uint32_t a[3] = {d, 0, 0x29u};
    if (call(in, VA_MEMSET, a, 3, &regs)) return 1;
    uint32_t keyboard = r32(in, VA_ID_KEYBOARD);
    if (keyboard) {
        if (es2_sim_key_reader(in, d, keyboard, &regs)) return 1;
        w16(in, d, (uint16_t)(r16(in, d) & 0xcfffu));
    }
    if (r32(in, VA_ID_JOYSTICK)) {
        uint32_t j = joystick_object(in, 3);
        if (joystick_read(in, j) || joystick_reader(in, local + 0xcu, r32(in, VA_ID_JOYSTICK))) return 1;
        w32(in, d + 0xdu, r32(in, local + 0xcu));
        w32(in, d + 0x11u, r32(in, local + 0x10u));
        w32(in, d + 0x15u, r32(in, local + 0x14u));
        w32(in, d + 0x19u, r32(in, local + 0x18u));
        for (uint32_t i = 0; i < 12u; ++i) w8(in, d + 0x1du + i, r8(in, local + 0x1cu + i));
        regs.edx = (r32(in, local + 0x14u) & 0xffffff00u) | r8(in, local + 0x23u);
        regs.ecx = (r32(in, local + 0x18u) & 0xffffff00u) | r8(in, local + 0x24u);
    }
    uint32_t mouse = r32(in, VA_ID_MOUSE);
    if (mouse) {
        int al;
        es2_sim_result m = regs;
        if (mouse_reader(in, local, mouse, &al, &m)) return 1;
        regs.edx = m.edx; regs.ecx = m.ecx;
        if (al) {
            w32(in, d + 2u, r32(in, local));
            w32(in, d + 6u, r32(in, local + 4u));
            uint16_t buttons = r16(in, local + 8u);
            if (buttons & 1u) w8(in, d + 0xau, 1);
            if (buttons & 2u) w8(in, d + 0xbu, 1);
            if (buttons & 4u) w8(in, d + 0xcu, 1);
            regs.ecx = r32(in, local);
        }
    }
    local_pop(in, top);
    if (out) { out->eax = d; out->edx = regs.edx; out->ecx = regs.ecx; }
    return 0;
}

/* ---- the input set-up 0x45a5dc (listing 0x45a5dc..0x45a80d) */
int es2_sim_input_setup(es2_sim_input *in, es2_sim_result *out) {
    /* 0x49630c: the runtime record (descriptor 0x49eb84) is the original's
     * frame only; the own code links none, so FS:0 is unchanged. */
    uint32_t a[1] = {0xeu};
    es2_sim_result r;
    if (call(in, VA_NEW, a, 1, &r)) return 1;
    uint32_t k = r.eax;
    if (k) {
        if (keyboard_ctor(in, k, 0x10000u)) return 1;
        counter(in, -2);
    }
    if (rec_activate(in, k + 4u)) return 1;
    hotkey_register(in, 0x00477a7cu, 0x0049eabcu);
    if (flight_capture(in, 1)) return 1;
    a[0] = 0x3fu;
    if (call(in, VA_NEW, a, 1, &r)) return 1;
    uint32_t m = r.eax;
    if (m) {
        if (mouse_ctor(in, m)) return 1;
        counter(in, -2);
    }
    if (rec_activate(in, m + 0x3au) || prefs_read(in)) return 1;
    es2_sim_result p;
    if (es2_sim_device_poll(in, &p)) return 1;
    const uint32_t rec = VA_RECORD, d = p.eax;
    w32(in, VA_RECORD_DEVICE, d);
    static const uint8_t targets[12] = {0xe, 0x10, 0x12, 0x14, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d};
    for (unsigned i = 0; i < 12u; ++i) w32(in, 0x004d2310u + 4u * i, rec + targets[i]);
    w32(in, 0x004d2350u, d + 0xdu);
    w32(in, 0x004d2354u, d + 0x11u);
    w32(in, 0x004d2358u, r32(in, VA_RECORD_DEVICE) + 0x15u);
    w32(in, 0x004d235cu, r32(in, VA_RECORD_DEVICE) + 0x19u);
    static const uint32_t defaults[8] = {0x004d23c1u, 0x004d23c5u, 0x004d23c9u, 0x004d23cdu,
                                         0x004d23d1u, 0x004d23d2u, 0x004d23d3u, 0x004d23d4u};
    for (unsigned i = 0; i < 4u; ++i) w32(in, 0x004d2340u + 4u * i, defaults[i]);
    for (unsigned i = 0; i < 4u; ++i) w32(in, 0x004d2360u + 4u * i, defaults[4u + i]);
    for (unsigned i = 0; i < 8u; ++i) w32(in, 0x004d2370u + 4u * i, r32(in, VA_RECORD_DEVICE) + 0x1du + i);
    for (uint32_t i = 0; i < 4u; ++i) w8(in, 0x004d22f9u - i, 0);
    es2_sim_result j;
    if (call(in, VA_KEYJOY_LOAD, NULL, 0, &j)) return 1;
    if (out) { out->edx = j.edx; out->ecx = j.ecx; }
    return 0;
}

/* ---- the mouse: the window handler, the event, the queue */
/* 0x480898 (listing 0x480898..0x4808da): GetClientRect into 0x6c5f5a, then
 * the scales (DIB size << 15) / client size (signed) when the width is not 0. */
static int mouse_scale(es2_sim_input *in) {
    uint32_t hwnd = r32(in, VA_HWND);
    if (!hwnd) return 0;
    uint32_t a[2] = {hwnd, VA_CLIENT_RECT};
    if (call(in, VA_GET_CLIENT_RECT, a, 2, NULL)) return 1;
    int32_t width = (int32_t)r32(in, VA_CLIENT_RECT + 8u);
    if (!width) return 0;
    w32(in, VA_MOUSE_SCALE_X, (uint32_t)((int32_t)(r32(in, 0x004d3040u) << 15) / width));
    int32_t height = (int32_t)r32(in, VA_CLIENT_RECT + 12u);
    if (!height)
        return divide_refuse(in, 0x004808cfu, (uint64_t)(int64_t)(int32_t)(r32(in, 0x004d3044u) << 15), 0, "mouse scale",
                             "a client height of 0 (#DE)");
    w32(in, VA_MOUSE_SCALE_Y, (uint32_t)((int32_t)(r32(in, 0x004d3044u) << 15) / height));
    return 0;
}
/* 0x480948(code, lparam, buttons) (listing 0x480948..0x4809f7): the scales
 * once; the block 0x6c5f50 (x, y of lparam's words, zero-extended, times
 * the scales >> 16; the button word: bit 0 left, bit 1 right); every active
 * entry of 0x6c5ed8 whose mask has bit `code` gets the block (cdecl). */
int es2_sim_mouse_event(es2_sim_input *in, uint32_t code, uint32_t lparam, uint32_t buttons) {
    if (!r32(in, VA_MOUSE_SCALE_X) && mouse_scale(in)) return 1;
    const uint32_t s = ES2_SIM_VA_MOUSE_BLOCK;
    w32(in, s, mul16(lparam & 0xffffu, r32(in, VA_MOUSE_SCALE_X)));
    w32(in, s + 4u, mul16(lparam >> 16, r32(in, VA_MOUSE_SCALE_Y)));
    w16(in, s + 8u, 0);
    if (buttons & 1u) w16(in, s + 8u, (uint16_t)(r16(in, s + 8u) | 1u));
    if (buttons & 2u) w16(in, s + 8u, (uint16_t)(r16(in, s + 8u) | 2u));
    uint32_t bit = 1u << (code & 0x1fu);
    for (uint32_t k = 0, e = VA_MOUSE_CALLBACKS; k < 10u; ++k, e += 12u) {
        if (!r32(in, e) || !(r32(in, e + 4u) & bit)) continue;
        uint32_t a[1] = {s};
        if (call(in, r32(in, e + 8u), a, 1, NULL)) return 1;
    }
    return 0;
}
/* 0x4809f8(hwnd, message, wParam, lParam), stdcall (listing
 * 0x4809f8..0x480ad2): WM_SIZE rescales; the mouse messages become events 0
 * (move), 1/2 (left down/up), 3/4 (right down/up) with the button bits the
 * listing forms; EAX 1 for move and the left button (the window procedure
 * then stops), 0 otherwise. */
int es2_sim_mouse_handler(es2_sim_input *in, uint32_t message, uint32_t wparam, uint32_t lparam, uint32_t *result) {
    *result = 0;
    switch (message) {
    case 0x0005u: return mouse_scale(in);
    case 0x0200u: *result = 1; return es2_sim_mouse_event(in, 0, lparam, (wparam & 1u) + 2u * (wparam & 2u));
    case 0x0201u: *result = 1; return es2_sim_mouse_event(in, 1, lparam, 2u * (wparam & 2u) + 1u);
    case 0x0202u: *result = 1; return es2_sim_mouse_event(in, 2, lparam, 2u * (wparam & 2u));
    case 0x0204u: return es2_sim_mouse_event(in, 3, lparam, (wparam & 1u) + 2u);
    case 0x0205u: return es2_sim_mouse_event(in, 4, lparam, wparam & 1u);
    default: return 0;
    }
}
/* 0x452cdc(E) (listing 0x452cdc..0x452d3e): with the live-mouse byte
 * 0x4d1dd6, the clock; a changed button word is queued (mouse_queue(E,
 * t)); an unchanged one is queued only when the clock advanced (after the
 * cursor follow 0x486e7c when 0x49dbdf is set); the last time is stored. */
static int mouse_button_event(es2_sim_input *in, uint32_t e) {
    if (!r8(in, 0x004d1dd6u)) return 0;
    es2_sim_result c;
    if (call(in, VA_CLOCK, NULL, 0, &c)) return 1;
    uint32_t t = c.eax, buttons = r16(in, e + 8u);
    uint32_t q[2] = {e, t};
    if (buttons != r32(in, 0x004d1e0cu)) {
        w32(in, 0x004d1e0cu, buttons);
        if (call(in, VA_MOUSE_QUEUE, q, 2, NULL)) return 1;
    } else {
        if (r8(in, 0x0049dbdfu)) {
            uint32_t a[1] = {e};
            if (call(in, VA_CURSOR_FOLLOW, a, 1, NULL)) return 1;
        }
        if (t > r32(in, 0x0049dc20u) && call(in, VA_MOUSE_QUEUE, q, 2, NULL)) return 1;
    }
    w32(in, 0x0049dc20u, t);
    return 0;
}
/* 0x452ae4 (listing 0x452ae4..0x452ba2): the button callback 0x452cdc with
 * mask 0x1e into the mouse's table (0x480848: the first free entry, its
 * index or 0xffff), the button and position state, two event lists of 100
 * 14-byte events (0x49667c with the empty constructor 0x4531b5), the list
 * headers, the double-click time 30, no active screen. EAX 0x1e, EDX 0, ECX 0. */
int es2_sim_mouse_queue_setup(es2_sim_input *in, es2_sim_result *out) {
    (void)current_mouse(in);   /* 0x45bd8c: the object argument 0x480848 does not read */
    uint32_t index = 0x006cffffu;
    for (uint32_t k = 0, e = VA_MOUSE_CALLBACKS; k < 10u; ++k, e += 12u) {
        if (r32(in, e)) continue;
        w32(in, e + 4u, 0x1eu);
        w32(in, e + 8u, 0x00452cdcu);
        w32(in, e, 1);
        index = k;
        break;
    }
    w16(in, 0x0049dbd4u, (uint16_t)index);
    w32(in, 0x004d1dd0u, 0);
    w32(in, 0x004d1dccu, 0);
    w8(in, 0x004d1dd5u, 0);
    w8(in, 0x004d1dd4u, 0);
    w32(in, 0x004d1e0cu, 0);
    uint32_t a[5] = {0, 0xeu, 0x64u, 1, 0x004531b5u};
    es2_sim_result r;
    if (call(in, VA_VECTOR_NEW, a, 5, &r)) return 1;
    w32(in, 0x004d1df8u, r.eax);
    if (call(in, VA_VECTOR_NEW, a, 5, &r)) return 1;
    w32(in, 0x004d1e00u, r.eax);
    w32(in, 0x004d1dfcu, 0);
    w32(in, 0x004d1df4u, 0);
    w32(in, 0x004d1e04u, 0x004d1df4u);
    w32(in, 0x004d1e08u, 0x004d1dfcu);
    w32(in, 0x004d1decu, 0x1eu);
    w32(in, 0x0049dbd8u, 0);
    w16(in, 0x0049dbdcu, 0xffffu);
    if (out) { out->eax = 0x1eu; out->edx = 0; out->ecx = 0; }
    return 0;
}
/* 0x45b758 (listing 0x45b758..0x45b7a7): the mouse's bounds (0x480738 out,
 * the current surface's width and height - 1 through 0x481350, 0x480764
 * back). 0x480764 also copies three words from +0x10..+0x15 of the local,
 * which nothing wrote (residue, modelled 0), to M+0x1e..+0x23. */
int es2_sim_mouse_bounds(es2_sim_input *in) {
    uint32_t m = 0, o = device_slot(in, 2);
    m = o ? o - 0x3au : 0;
    if (!m) return 0;
    uint32_t surface = r32(in, 0x006c5ed0u);
    uint32_t top = in->loop->stack_hi, b = local_push(in, 0x18u);
    if (!b) return refuse(in, 0x0045b758u, "mouse bounds", "the own code's call area is exhausted");
    w32(in, b, r32(in, m + 0xeu));
    w32(in, b + 4u, r32(in, m + 0x12u));
    w32(in, b + 8u, r32(in, m + 0x16u));
    w32(in, b + 0xcu, r32(in, m + 0x1au));
    es2_sim_result r;
    uint32_t a[2] = {surface, 0};
    if (call(in, VA_SURFACE_ATTRIBUTE, a, 2, &r)) { local_pop(in, top); return 1; }
    w32(in, b + 8u, r.eax - 1u);
    a[1] = 1;
    if (call(in, VA_SURFACE_ATTRIBUTE, a, 2, &r)) { local_pop(in, top); return 1; }
    w32(in, b + 0xcu, r.eax - 1u);
    w16(in, m + 0x1eu, r16(in, b + 0x10u));
    w16(in, m + 0x20u, r16(in, b + 0x12u));
    w16(in, m + 0x22u, r16(in, b + 0x14u));
    w32(in, m + 0xeu, r32(in, b));
    w32(in, m + 0x12u, r32(in, b + 4u));
    w32(in, m + 0x16u, r32(in, b + 8u));
    w32(in, m + 0x1au, r32(in, b + 0xcu));
    local_pop(in, top);
    return 0;
}

/* ---- service rows (sim/dispatch/services.h) over the bound instance */
static es2_sim_input *serving(es2_sim_service_call *c) {
    es2_sim_input *in = bound_input;
    if (!in || !in->loop) { c->refused = 1; c->reason = "no input layer bound (H5-08)"; }
    return in;
}
static void failed(es2_sim_service_call *c) { c->refused = 1; c->reason = "a call inside the service was refused"; }
static uint32_t fs0_head(const es2_sim_services *s) {
    if (s && s->fs0) return *s->fs0;
#if defined(__i386__)
    uint32_t v;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(v));
    return v;
#else
    return 0;
#endif
}
/* 0x45a5dc: EAX the FS:0 head the frame record saved (the head at entry),
 * EDX/ECX keyjoy_load's. */
void es2_sim_service_input_setup(es2_sim_service_call *c, void *user) {
    es2_sim_input *in = serving(c);
    if (!in) return;
    es2_sim_result r = {0, c->edx, c->ecx, 0, 0, 0};
    uint32_t head = fs0_head(user);
    if (es2_sim_input_setup(in, &r)) { failed(c); return; }
    c->eax = head; c->edx = r.edx; c->ecx = r.ecx;
}
void es2_sim_service_device_poll(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    es2_sim_result r;
    if (es2_sim_device_poll(in, &r)) { failed(c); return; }
    c->eax = r.eax; c->edx = r.edx; c->ecx = r.ecx;
}
/* 0x45bbc4(p, id): for an id other than 1 EDX and ECX stay as at the call. */
void es2_sim_service_key_reader(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    es2_sim_result r = {0, c->edx, c->ecx, 0, 0, 0};
    if (es2_sim_key_reader(in, c->slots[0], c->slots[1], &r)) { failed(c); return; }
    c->eax = r.eax; c->edx = r.edx; c->ecx = r.ecx;
}
/* 0x45c524(id): EAX the object, ECX the id, EDX as at the call. */
void es2_sim_service_joystick_object(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    c->eax = joystick_object(in, c->slots[0]);
    c->ecx = c->slots[0];
}
void es2_sim_service_joystick_summary(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    es2_sim_result r;
    if (es2_sim_joystick_summary(in, &r)) { failed(c); return; }
    c->eax = r.eax; c->edx = r.edx; c->ecx = r.ecx;
}
/* 0x45c2e0(J, flags) and 0x45c330(J), the joystick vtable's slots 0 and 1
 * (input.md, "the joystick virtuals as rows"). */
void es2_sim_service_joystick_dtor_45c2e0(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    es2_sim_result r = {c->eax, c->edx, c->ecx, 0, 0, 0};
    if (joystick_dtor(in, c->slots[0], c->slots[1], &r)) { failed(c); return; }
    c->eax = r.eax; c->edx = r.edx; c->ecx = r.ecx;
}
void es2_sim_service_joystick_poll_45c330(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    es2_sim_result r;
    if (joystick_poll(in, c->slots[0], &r)) { failed(c); return; }
    c->eax = r.eax; c->edx = r.edx; c->ecx = r.ecx;
}
/* 0x4809f8: stdcall, EAX 0/1 (the window procedure reads only EAX). */
void es2_sim_service_mouse_handler(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    uint32_t result = 0;
    if (es2_sim_mouse_handler(in, c->slots[1], c->slots[2], c->slots[3], &result)) { failed(c); return; }
    c->eax = result;
}
void es2_sim_service_mouse_event(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    if (es2_sim_mouse_event(in, c->slots[0], c->slots[1], c->slots[2])) failed(c);
}
void es2_sim_service_mouse_button(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    if (mouse_button_event(in, c->slots[0])) failed(c);
}
void es2_sim_service_mouse_queue_setup(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    es2_sim_result r;
    if (es2_sim_mouse_queue_setup(in, &r)) { failed(c); return; }
    c->eax = r.eax; c->edx = r.edx; c->ecx = r.ecx;
}
void es2_sim_service_mouse_bounds(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    uint32_t m = 0, o = device_slot(in, 2);
    m = o ? o - 0x3au : 0;
    if (es2_sim_mouse_bounds(in)) { failed(c); return; }
    c->eax = m ? m + 0xeu : 0;
    if (m) { c->edx = m + 0x16u; c->ecx = r32(in, m + 0x1au); }
    else c->ecx = 2;   /* 0x45c258's pop ecx */
}
/* 0x477abc(K) (listing 0x477abc..0x477ad2): the read index = the write
 * index, bit 1 of [K] cleared. EAX K, EDX the write index. */
void es2_sim_service_key_flush(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    uint32_t k = c->slots[0], w = r32(in, VA_RING_WRITE);
    w32(in, VA_RING_READ, w);
    w32(in, k, r32(in, k) & ~2u);
    c->eax = k; c->edx = w;
}
/* 0x477a7c(code) (listing 0x477a7c..0x477a82): the empty hot-key callback, EAX 0. */
void es2_sim_service_hotkey_none(es2_sim_service_call *c, void *user) { (void)user; c->eax = 0; }
/* 0x459958(index, value, flag). */
void es2_sim_service_prefs_set(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    es2_sim_result r;
    if (es2_sim_prefs_set(in, c->slots[0], c->slots[1], c->slots[2], &r)) { failed(c); return; }
    c->eax = r.eax; c->edx = r.edx; c->ecx = r.ecx;
}
/* The prefs callbacks the set-up registers (listings 0x459c88..0x459cdc,
 * 0x459ce0..0x459cf3, 0x459d3c..0x459d85): bytes 0 and 1 pass their value to
 * the sound flags 0x462d40 / 0x462d5c and, outside the prefs load, start or
 * stop the sound groups (audio); byte 2 the flag 0x49f966; byte 0xe the lever
 * word; byte 8 the flag 0x4aaaa8. */
static void sound_callback(es2_sim_service_call *c, uint32_t setter, uint32_t on, uint32_t off) {
    es2_sim_input *in = serving(c);
    if (!in) return;
    uint32_t v = c->slots[0], a[1] = {v};
    es2_sim_result r;
    if (call(in, setter, a, 1, &r)) { failed(c); return; }
    /* The group call gets the setter's EAX and EDX and ECX = v (pop ecx). */
    uint32_t regs[3] = {r.eax, r.edx, v};
    if (!r8(in, VA_PREFS_LOADING) && call_regs(in, (uint8_t)v ? on : off, NULL, 0, regs, &r)) { failed(c); return; }
    c->eax = r.eax; c->edx = r.edx; c->ecx = v;
}
void es2_sim_service_prefs_sound_a(es2_sim_service_call *c, void *user) { (void)user; sound_callback(c, 0x00462d40u, 0x00462e58u, 0x00462d78u); }
void es2_sim_service_prefs_sound_b(es2_sim_service_call *c, void *user) { (void)user; sound_callback(c, 0x00462d5cu, 0x00462efcu, 0x00462ddcu); }
void es2_sim_service_prefs_flag_49f966(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    uint32_t v = (uint8_t)c->slots[0] ? 1u : 0u;
    w8(in, 0x0049f966u, (uint8_t)v);
    c->eax = v;
}
void es2_sim_service_prefs_lever(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    es2_sim_result r;
    if (lever_word(in, c->slots[0], &r)) { failed(c); return; }
    c->eax = r.eax; c->edx = r.edx; c->ecx = r.ecx;
}
void es2_sim_service_prefs_flag_4aaaa8(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    flag_4aaaa8(in, c->slots[0]);
    if (!(uint8_t)c->slots[0]) c->eax = 0;
}
/* 0x462d40(v) / 0x462d5c(v) (listings 0x462d40..0x462d59, 0x462d5c..0x462d75):
 * the dword 0x49f8f4 / 0x49f8f8 takes v's byte when it differs. EAX v, EDX
 * the byte. (Audio's flags.) */
static void sound_flag(es2_sim_service_call *c, uint32_t va) {
    es2_sim_input *in = serving(c);
    if (!in) return;
    uint32_t b = (uint8_t)c->slots[0];
    if (b != r32(in, va)) w32(in, va, b);
    c->eax = c->slots[0]; c->edx = b;
}
void es2_sim_service_sound_flag_a(es2_sim_service_call *c, void *user) { (void)user; sound_flag(c, 0x0049f8f4u); }
void es2_sim_service_sound_flag_b(es2_sim_service_call *c, void *user) { (void)user; sound_flag(c, 0x0049f8f8u); }
/* 0x4531b5(p) and 0x49545b(p): constructors without stores, EAX p. */
void es2_sim_service_empty_ctor(es2_sim_service_call *c, void *user) { (void)user; c->eax = c->slots[0]; }
/* 0x481350(surface, k) (listing 0x481350..0x4813f7, jump table 0x481369):
 * the graphics library's surface attribute k (EAX; 0 for k above 16 and
 * for 12..15), EDX k. (The graphics library's.) */
void es2_sim_service_surface_attribute(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    static const int16_t offsets[17] = {8, 0xc, 0x28, 0x2c, 0x10, -1, 0x14, 0x18, -2, 0x1c, 0x20, 0x24, 0, 0, 0, 0, -3};
    uint32_t s = c->slots[0], k = c->slots[1];
    c->edx = k;
    if (k > 0x10u || (k >= 12u && k <= 15u)) { c->eax = 0; return; }
    int16_t o = offsets[k];
    c->eax = o == -1 ? r32(in, 0x004a57a5u) : o == -2 ? r32(in, 0x004a4fa1u) : o == -3 ? r32(in, 0x004a368eu) : r32(in, s + (uint32_t)o);
}
/* 0x49667c(p, size, count, flags, ctor), the runtime's array constructor
 * (listing 0x49667c..0x496745): with p = 0 new[](size * count, + 4 with
 * flag 0x10; NULL: EAX 0); with 0x10 the count before the elements; per
 * element the constructor by flags & 7 (1 cdecl, 2 and 5 stdcall, 3 with
 * the element in EAX); with 0x200 the construction counter restored to its
 * value at entry ([0x6cbee4] is fs:[4] - 4 on the simulation thread, the
 * c0w32 entry's). EAX the array. */
void es2_sim_service_vector_new(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    uint32_t p = c->slots[0], size = c->slots[1], count = c->slots[2], flags = c->slots[3], ctor = c->slots[4];
    uint32_t saved = r32(in, r32(in, VA_COUNTER_POINTER));
    unsigned kind = flags & 7u;
    if (ctor && count && kind != 1u && kind != 2u && kind != 3u && kind != 5u) {
        c->refused = 1; c->reason = "an array constructor kind the runtime reports as an error (0x49676c)"; return;
    }
    es2_sim_result r;
    if (!p) {
        uint32_t a[1] = {size * count + ((flags & 0x10u) ? 4u : 0u)};
        if (call(in, VA_NEW_ARRAY, a, 1, &r)) { failed(c); return; }
        p = r.eax;
        c->ecx = a[0]; c->edx = r.edx;
        if (!p) { c->eax = 0; return; }
    }
    if (flags & 0x10u) { w32(in, p, count); p += 4u; c->edx = count; }
    if (ctor) {
        uint32_t e = p;
        for (uint32_t n = count; n; --n, e += size) {
            uint32_t a[1] = {e}, regs[3] = {e, 0, 0};
            if (call_regs(in, ctor, a, 1, kind == 3u ? regs : NULL, &r)) { failed(c); return; }
            c->ecx = kind == 1u ? e : r.ecx;
        }
        c->edx = 0;
    }
    if (flags & 0x200u) w32(in, r32(in, VA_COUNTER_POINTER), saved);
    c->eax = p;
}

/* ---- the platform imports through their thunks (stdcall) */
static void guest_text(const es2_sim_input *in, uint32_t p, char *out, unsigned capacity) {
    unsigned n = 0;
    for (uint8_t b; p && n + 1u < capacity && (b = r8(in, p + n)) != 0; ++n) out[n] = (char)b;
    out[n] = 0;
}
/* GetKeyState (thunk 0x49695e): the SHORT, sign-extended. */
void es2_sim_service_get_key_state(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    ++in->key_states;
    c->eax = (uint32_t)(int32_t)in->platform.key_state(in->platform.user, frame_of(in), (int32_t)c->slots[0]);
}
/* joyGetDevCapsA (thunk 0x496a66)(id, caps, size). */
void es2_sim_service_joy_caps(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    ++in->joy_caps;
    uint32_t caps = c->slots[1], size = c->slots[2];
    c->eax = in->platform.joy_caps(in->platform.user, frame_of(in), c->slots[0], caps ? mem(in)->host(mem(in)->context, caps) : NULL, size);
}
/* joyGetPosEx (thunk 0x496a6c)(id, info): the buffer's size is its dwSize. */
void es2_sim_service_joy_pos(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    ++in->joy_pos;
    uint32_t info = c->slots[1];
    c->eax = in->platform.joy_pos(in->platform.user, frame_of(in), c->slots[0], info ? mem(in)->host(mem(in)->context, info) : NULL,
                                  info ? r32(in, info) : 0);
}
/* GetCursorPos (thunk 0x49696a)(point). */
void es2_sim_service_cursor_pos(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    ++in->cursor_reads;
    int32_t x = 0, y = 0;
    c->eax = in->platform.cursor_pos(in->platform.user, frame_of(in), &x, &y);
    if (c->slots[0]) { w32(in, c->slots[0], (uint32_t)x); w32(in, c->slots[0] + 4u, (uint32_t)y); }
}
/* GetClientRect (thunk 0x496970)(hwnd, rect): (0, 0, width, height), TRUE. */
void es2_sim_service_client_rect(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    ++in->client_rects;
    int32_t width = 0, height = 0;
    in->platform.client_size(in->platform.user, &width, &height);
    uint32_t r = c->slots[1];
    w32(in, r, 0); w32(in, r + 4u, 0); w32(in, r + 8u, (uint32_t)width); w32(in, r + 12u, (uint32_t)height);
    c->eax = 1;
}
/* GetWindowRect (thunk 0x49694c)(hwnd, rect): the rectangle the window was
 * created with (0x4d36cc: x, y, width, height), TRUE. */
void es2_sim_service_window_rect(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    ++in->window_rects;
    uint32_t x = r32(in, VA_WINDOW_RECT), y = r32(in, VA_WINDOW_RECT + 4u), r = c->slots[1];
    w32(in, r, x); w32(in, r + 4u, y);
    w32(in, r + 8u, x + r32(in, VA_WINDOW_RECT + 8u)); w32(in, r + 12u, y + r32(in, VA_WINDOW_RECT + 12u));
    c->eax = 1;
}
/* GetPrivateProfileStringA (thunk 0x496910)(section, key, default, buffer,
 * size, file): the value from the file (formats/cfg.md's INI subset) or the
 * default, cut to size - 1 characters; EAX the length. NULL section or key
 * (the enumerations) are refused: DBSIM never passes them. */
void es2_sim_service_profile_string(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    ++in->profile_reads;
    uint32_t section = c->slots[0], key = c->slots[1], fallback = c->slots[2], buffer = c->slots[3], size = c->slots[4];
    if (!section || !key) { c->refused = 1; c->reason = "GetPrivateProfileStringA enumeration (NULL section or key)"; return; }
    if (!size) { c->eax = 0; return; }
    char name[260], s[128], k[128], value[256];
    guest_text(in, c->slots[5], name, sizeof(name));
    guest_text(in, section, s, sizeof(s));
    guest_text(in, key, k, sizeof(k));
    static uint8_t file[0x10000];
    uint32_t file_size = 0;
    int found = 0;
    if (in->read_file && !in->read_file(in->read_user, name, file, sizeof(file), &file_size)) {
        es2_span span = {file, file_size};
        es2_error error;
        uint32_t capacity = size < sizeof(value) ? size : (uint32_t)sizeof(value);
        if (es2_cfg_ini_get(span, s, k, value, capacity, &found, &error) != ES2_OK) found = 0;
    }
    if (!found) {
        uint32_t capacity = size < sizeof(value) ? size : (uint32_t)sizeof(value);
        guest_text(in, fallback, value, capacity);
    }
    uint32_t n = (uint32_t)strlen(value);
    for (uint32_t i = 0; i < n; ++i) w8(in, buffer + i, (uint8_t)value[i]);
    w8(in, buffer + n, 0);
    c->eax = n;
}

/* ---- the read and write streams as rows, for the prefs adapters' bridges
 * (prefs_load calls 0x47ad40, 0x47af98, 0x47ad78; prefs_store also 0x47b064,
 * 0x47b2b8, 0x47b09c by VA). The read stream's contract is volumes.c's (in
 * vitro there); here as rows with the registers each leaves: listings
 * 0x47ad40..0x47ad77, 0x47af98..0x47afe7, 0x47ad78..0x47adb1,
 * 0x47ae28..0x47ae67, 0x47ae68..0x47aeac, 0x47af60..0x47af82 (read);
 * 0x47b064..0x47b09b, 0x47b2b8..0x47b2f4, 0x47b09c..0x47b0d5,
 * 0x47b158..0x47b197, 0x47b198..0x47b1e1, 0x47b294..0x47b2b6 (write);
 * 0x47a888..0x47a8d2 (the base class). Both streams are 0xb bytes: +0 vtable,
 * +4 status, +8 handle word, +0xa open flag. */
#define VT_STREAM_BASE 0x004a16e8u
#define VT_STREAM_READ 0x004a1738u
#define VT_STREAM_WRITE 0x004a1760u
/* 0x47af60 / 0x47b294: status 1 when the dword 0x4967ae (bytes of the
 * _errno thunk in CODE, nonzero) is set, else 3; EAX 0. */
static void stream_failed(es2_sim_input *in, uint32_t r) { w32(in, r + 4u, r32(in, 0x004967aeu) ? 1u : 3u); }
/* 0x47ae28 / 0x47b158: close an open handle; AL 1, or 0 when _close failed (status 3). */
static int stream_close(es2_sim_input *in, uint32_t r, int *al) {
    if (r8(in, r + 0xau) && r16(in, r + 8u) != 0xffffu) {
        uint32_t a[1] = {sx16(r16(in, r + 8u))};
        es2_sim_result x;
        if (call(in, VA_CLOSE, a, 1, &x)) return 1;
        if (x.eax == 0xffffffffu) { w32(in, r + 4u, 3); *al = 0; return 0; }
    }
    w16(in, r + 8u, 0xffffu);
    w32(in, r + 4u, 5);
    *al = 1;
    return 0;
}
/* 0x47ad40 / 0x47b064(S, path): the base class (counter + 1), the vtable,
 * no handle; close, _open(path, flags, mode) (read 0x8000; write 0x8301,
 * 0x180), status 1 on failure, else status 0 and the open flag; counter + 1.
 * EAX S, EDX the counter pointer, ECX S after a successful open (0x47b35d's
 * pop), else _open's. */
static void stream_open(es2_sim_service_call *c, uint32_t vtable, uint32_t flags, uint32_t mode) {
    es2_sim_input *in = serving(c);
    if (!in) return;
    uint32_t r = c->slots[0], path = c->slots[1];
    w32(in, r, VT_STREAM_BASE);
    w32(in, r + 4u, 0);
    counter(in, 1);
    w32(in, r, vtable);
    w16(in, r + 8u, 0xffffu);
    w8(in, r + 0xau, 0);
    int al;
    if (stream_close(in, r, &al)) { failed(c); return; }
    uint32_t a[3] = {path, flags, mode};
    es2_sim_result x;
    if (call(in, VA_OPEN, a, 3, &x)) { failed(c); return; }
    w16(in, r + 8u, (uint16_t)x.eax);
    c->ecx = x.ecx;
    if ((uint16_t)x.eax == 0xffffu) {
        w32(in, r + 4u, 1);
    } else {
        w32(in, r + 4u, 0);
        w8(in, r + 0xau, 1);
        c->ecx = r;
    }
    counter(in, 1);
    c->eax = r;
    c->edx = r32(in, VA_COUNTER_POINTER);
}
void es2_sim_service_stream_open(es2_sim_service_call *c, void *user) { (void)user; stream_open(c, VT_STREAM_READ, 0x8000u, 0); }
void es2_sim_service_stream_write_open(es2_sim_service_call *c, void *user) { (void)user; stream_open(c, VT_STREAM_WRITE, 0x8301u, 0x180u); }
/* 0x47af98(R, n, buf): a status gives EAX 0; n = 0 AL 1; _read = -1 the
 * failure status, EAX 0; short: status 2, EAX 0; else AL 1.
 * 0x47b2b8(W, n, buf): the same with _write and no short case. */
static void stream_transfer(es2_sim_service_call *c, uint32_t function, int short_check) {
    es2_sim_input *in = serving(c);
    if (!in) return;
    uint32_t r = c->slots[0], n = c->slots[1], buf = c->slots[2];
    if (r32(in, r + 4u)) { c->eax = 0; return; }
    if (!n) { c->eax = short_check ? (c->eax & 0xffffff00u) | 1u : 1u; return; }
    uint32_t a[3] = {sx16(r16(in, r + 8u)), buf, n};
    es2_sim_result x;
    if (call(in, function, a, 3, &x)) { failed(c); return; }
    c->edx = x.edx; c->ecx = x.ecx;
    if (x.eax == 0xffffffffu) { stream_failed(in, r); c->eax = 0; c->ecx = r; return; }
    if (short_check && x.eax != n) { w32(in, r + 4u, 2); c->eax = 0; return; }
    c->eax = ((x.eax + 1u) & 0xffffff00u) | 1u;
}
void es2_sim_service_stream_read(es2_sim_service_call *c, void *user) { (void)user; stream_transfer(c, VA_READ, 1); }
void es2_sim_service_stream_write(es2_sim_service_call *c, void *user) { (void)user; stream_transfer(c, 0x0049677eu, 0); }
/* 0x47ad78 / 0x47b09c(S, flags): counter - 1, the vtable, the close, the
 * base destructor 0x47a8a4(S, 0) (counter - 1, the base vtable, its slot
 * +0x10), delete with flag bit 0. */
static void stream_destroy(es2_sim_service_call *c, uint32_t vtable) {
    es2_sim_input *in = serving(c);
    if (!in) return;
    uint32_t r = c->slots[0], flags = c->slots[1];
    counter(in, -1);
    if (!r) return;
    w32(in, r, vtable);
    int al;
    if (stream_close(in, r, &al)) { failed(c); return; }
    counter(in, -1);
    w32(in, r, VT_STREAM_BASE);
    uint32_t slot = r32(in, VT_STREAM_BASE + 0x10u);
    if (slot != VA_STREAM_SLOT_10) {
        uint32_t a[1] = {r};
        if (call(in, slot, a, 1, NULL)) { failed(c); return; }
    }
    if (flags & 1u) {
        uint32_t a[1] = {r};
        if (call(in, VA_DELETE, a, 1, NULL)) { failed(c); return; }
    }
    c->eax = 1;
}
void es2_sim_service_stream_destroy(es2_sim_service_call *c, void *user) { (void)user; stream_destroy(c, VT_STREAM_READ); }
void es2_sim_service_stream_write_destroy(es2_sim_service_call *c, void *user) { (void)user; stream_destroy(c, VT_STREAM_WRITE); }

/* ---- the input rest (input.md, "the input rest"). Two rows over
 * the input layer's own code for callers that are not own code yet; the other VAs of the
 * row are covered without a row, dead or inside an own function (the verdict
 * table there). */
/* 0x45a274(set): row joystick_map (caller still original: the control
 * mapping page 0x45866c at 0x458ad7). */
void es2_sim_service_joystick_map(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    es2_sim_result r;
    if (joystick_map(in, c->slots[0], &r)) { failed(c); return; }
    c->eax = r.eax; c->edx = r.edx; c->ecx = r.ecx;
}
/* 0x459b80(index) (listing 0x459b80..0x459b92): prefs_store(1, the address
 * of its own argument slot), so the low byte of the index is the one entry
 * of the list; at the call EAX is that address (lea eax, [ebp + 8]), EDX
 * and ECX as at entry. EAX, EDX and ECX are prefs_store's. Row prefs_write
 * (caller: the frame end at 0x45f504, loop.c). */
void es2_sim_service_prefs_write(es2_sim_service_call *c, void *user) {
    (void)user;
    es2_sim_input *in = serving(c);
    if (!in) return;
    uint32_t a[2] = {1u, c->stack}, regs[3] = {c->stack, c->edx, c->ecx};
    es2_sim_result r;
    if (call_regs(in, VA_PREFS_STORE, a, 2, regs, &r)) { failed(c); return; }
    c->eax = r.eax; c->edx = r.edx; c->ecx = r.ecx;
}
