/* (--cockpit-rest; target :differential_cockpit_rest_test): two parts on
 * cockpit_cases.h's world, dispatch and stand-ins (included right after it;
 * prefix jr_), with the stand-ins of jr_stubs added (JB_STUBS + i).
 *
 * Part drop: the mission-start drop sequence 0x45d85c (the row
 * drop_sequence, src/cockpit.c) against the original's machine code. Run
 * as code on both sides: the jolt start, step and end 0x42d2f8/0x42d4a8/
 * 0x42d624 with cockpit_cases.h's stand-ins; on the original's side the
 * frame record 0x49630c. Stand-ins: the path 0x492bec ('D'), the load
 * 0x45cdf4 ('L', the record JB_RECORD), sfx_play/sfx_stop ('P'/'Q'),
 * camera_update ('C'), view_setup ('V'), object_select ('O'), the
 * projection 0x48c5fc ('J', logs the 3D point) and 0x48c6d0 ('K', writes a
 * scripted screen y), render_root ('H'), the pump ('U'), the blit 0x45c8bc
 * ('I', may move the point it is handed), the fade step 0x430b34 ('Z',
 * scripted, 0 ends the fade), the present 0x45fba4 ('Y', may make JB_GC2
 * current), delete[] ('X'), the RTTI count 0x496406 ('n', scripted), the
 * HUD's slot +8 ('h') and the palette objects' slot +0x18 ('v'). The clock
 * never stalls (jb_clock_step), so the landing loop ends; one case of 16
 * starts the clock so that the end time wraps past 2^32 and the next read
 * does not (one pass).
 *
 * Part panel: the cockpit panel constructor 0x431bf8 (the row panel_ctor,
 * src/hud_cockpit.c). Run as code on both sides: the layout 0x431778, the
 * canvas 0x430ea0, the widget root 0x4526ec, the graphics context
 * 0x480c34 (with its callees on the original's side) and operator new
 * (the original's through _calloc on the harness heap). Stand-ins: the
 * path ('D'), the VOL stream 0x472f14 ('a', no error), 0x49667c ('e'),
 * stream_read 0x4735b8 ('g', the scripted layout jr_layout), stream_destroy
 * 0x472f64 ('k'), 0x434368 ('m'), the port constructors 0x436244/0x435bf0/
 * 0x4369a4 ('q'/'u'/'y', scripted EAX), the port texts 0x435970 ('t'), the
 * factories 0x432340..0x432634 ('1'..'7', the elements JB_GAUGES),
 * 0x442ea0/0x442e60/0x442ef4 ('8'/'9'/'0'), 0x410644 ('z') and the
 * power-on start 0x438ddc ('M', which may replace the slots naming its
 * element, so the re-read of the slot shows).
 *
 * Not reached: the refusals (a NULL record or list in 0x45d85c, operator
 * new returning NULL, the VOL stream error with the fatal report) - the
 * original reads through NULL or exits there. */
#pragma once
/* The headers this suite builds on (sim_main.c). */
#include "init_records_cases.h"
#include "startup_cases.h"
#include "loop_cases.h"
#include "cockpit_cases.h"
static uint32_t jr_layout[0x6a4u / 4u];
static int32_t jr_y[16];
static uint32_t jr_fade[8], jr_rtti[2], jr_port_ret, jr_power_ret;
static unsigned jr_pass, jr_draw, jr_fills_at_pump, jr_present, jr_gc_switch, jr_blit_move, jr_fade_i, jr_reslot, jr_power_calls;
static int32_t jr_bottom;
enum {
    DK_PASSES, DK_NO_PASSES, DK_CLIP_LOW, DK_CLIP_IN, DK_CLIP_HIGH, DK_FILL, DK_BLIT_FILL, DK_BLIT, DK_BLIT_MOVE,
    DK_FADE, DK_FADE_END, DK_GC_SWITCH, DK_LAND_WRAP, DK_PAL_FIRST, DK_PAL_SECOND, DK_NO_PAL, DK_ARRAY, DK_NO_ARRAY,
    PK_PORT_A, PK_PORT_B, PK_OFFSET, PK_NO_OFFSET, PK_RECTS, PK_NO_RECTS, PK_POWER, PK_NO_POWER, PK_NULL_SLOT, PK_RESLOT,
    PK_PANEL_IS_H, JR_KEYS
};
static const char *const jr_key_names[JR_KEYS] = {
    "drop:passes", "drop:no-passes", "drop:clip-top", "drop:clip-y", "drop:clip-bottom", "drop:fill", "drop:blit+fill",
    "drop:blit", "drop:blit-moves", "drop:fade", "drop:fade-ends", "drop:gc-switched", "drop:land-wrap", "drop:palette-1",
    "drop:palette-2", "drop:no-palette", "drop:array", "drop:no-array",
    "panel:port-4ef", "panel:port-4df", "panel:offset", "panel:no-offset", "panel:rects", "panel:no-rects", "panel:power-on",
    "panel:no-power-on", "panel:null-slot", "panel:reslot", "panel:H-is-[49b088]",
};
static unsigned jr_keys[JR_KEYS];
static void jr_key(unsigned k) { if (!jb_side) ++jr_keys[k]; }
static void jr_setup_key(unsigned k) { ++jr_keys[k]; }   /* set while building a case, before either side runs */
static void jr_side_reset(void) {
    jr_pass = jr_draw = jr_fills_at_pump = jr_present = jr_fade_i = jr_power_calls = 0;
}
static uint32_t jr_gauge(unsigned i) { return jb_at(JB_GAUGES + 0x20u * i); }
static void jr_reslot_all(uint32_t g) {
    uint32_t h = jb_at(JB_H);
    static const uint16_t singles[3] = {0x1e9, 0x1f5, 0x1ed};
    for (unsigned i = 0; i < 10u; ++i)
        if (jb_get(h + 0x70u + 4u * i) == g) jb_put(h + 0x70u + 4u * i, jr_gauge(11u));
    for (unsigned i = 0; i < 3u; ++i)
        if (jb_get(h + singles[i]) == g) jb_put(h + singles[i], jr_gauge(11u));
}
static uint32_t jr_act(char l, const uint32_t *a) {
    switch (l) {
    /* the drop */
    case 'D': jb_note('D', a[0], a[1], 0); return 0x7a700000u | (a[1] & 0xffffu);
    case 'L': jb_note('L', a[0], 0, 0); return jb_at(JB_RECORD);
    case 'P': case 'Q': case 'C': case 'V': case 'O': case 'X': case 'm': case '8': case '9': case '0': case 'z':
        jb_note(l, a[0], 0, 0);
        return 0;
    case 'H': jb_note('H', 0, 0, 0); return 0;
    case 'U':
        jb_note('U', 0, 0, 0);
        jr_draw = 1;
        jr_fills_at_pump = jr_fills;
        return 0;
    case 'J':
        jb_note('J', jb_get(a[0]), jb_get(a[0] + 4u), jb_get(a[0] + 8u));
        jb_put(a[1], 0x1234u + jr_pass);
        jb_put(a[1] + 4u, 0xffff0000u | jr_pass);
        return 0;
    case 'K': {
        int32_t y = jr_y[jr_pass & 15u];
        jb_note('K', jb_get(a[0]), jb_get(a[0] + 4u), a[0] == a[1]);
        jb_put(a[1], jb_get(a[0]) ^ 0x55u);
        jb_put(a[1] + 4u, (uint32_t)y);
        ++jr_pass;
        jr_key(y <= 0 ? DK_CLIP_LOW : y < jr_bottom ? DK_CLIP_IN : DK_CLIP_HIGH);
        return 0;
    }
    case 'I':
        jb_note('I', a[0], a[1], 0);
        jb_note('i', jb_get(a[2]), jb_get(a[2] + 4u), 0);
        if (jr_draw == 1u) jr_draw = 2u;
        if (jr_blit_move && !(jr_pass & 3u)) {
            jb_put(a[2], jb_get(a[2]) + 7u);
            jb_put(a[2] + 4u, jb_get(a[2] + 4u) - 13u);
            jr_key(DK_BLIT_MOVE);
        }
        return 0;
    case 'h':
        jb_note('h', a[0], 0, 0);
        if (jr_draw == 1u && jr_fills != jr_fills_at_pump) jr_key(DK_FILL);
        else if (jr_draw == 2u) jr_key(jr_fills != jr_fills_at_pump ? DK_BLIT_FILL : DK_BLIT);
        jr_draw = 0;
        return 0x11u;
    case 'Z': {
        uint32_t v = jr_fade[jr_fade_i++ & 7u];
        jb_note('Z', v, 0, 0);
        jr_key(v ? DK_FADE : DK_FADE_END);
        return v;
    }
    case 'Y':
        jb_note('Y', jb_get(0x006c5f70u), 0, 0);
        if (++jr_present == jr_gc_switch) { jb_put(0x006c5f70u, jb_at(JB_GC2)); jr_key(DK_GC_SWITCH); }
        return 0;
    case 'n': jb_note('n', a[0], a[1], 0); return jr_rtti[a[0] == jb_at(JB_PALOBJ) ? 0 : 1];
    case 'v': jb_note('v', a[0], a[1], jb_get(a[0] + 4u) ^ jb_get(a[0] + 0xcu)); return 0xbe000000u ^ a[0] ^ a[1];
    /* the panel */
    case 'a': jb_note('a', a[1], 0, 0); jb_put(a[0] + 4u, 0); return a[0];
    case 'e': jb_note('e', a[0], a[1] | a[2] << 8 | a[3] << 16, a[4]); return a[0];
    case 'g': {
        uint32_t n = a[1] < sizeof(jr_layout) ? a[1] : (uint32_t)sizeof(jr_layout);
        jb_note('g', a[1], a[2], 0);
        memcpy((void *)(uintptr_t)a[2], jr_layout, n);
        return a[1];
    }
    case 'k': jb_note('k', a[1], 0, 0); return 0;
    case 'q': case 'u': case 'y': jb_note(l, a[0], jb_get(a[1]), jb_get(a[1] + 4u)); return jr_port_ret;
    case 't': jb_note('t', a[0], a[1] ^ a[3] << 16, a[2]); return 0;
    case '1': case '2': case '3': case '4': case '5': case '6': case '7':
        jb_note(l, a[0], a[1], 0);
        return jr_gauge((unsigned)(l - '1'));
    case 'M':
        jb_note('M', a[0], 0, 0);
        if (jr_reslot) jr_reslot_all(a[0]);
        return jr_power_ret + ++jr_power_calls;
    default: jb_note('?', 0, 0, 0); return 0;
    }
}
JB_O(path, 'D', 2) JB_O(load, 'L', 1) JB_O(sfx_play, 'P', 1) JB_O(sfx_stop, 'Q', 1) JB_O(camera, 'C', 1)
JB_O(view_setup, 'V', 1) JB_O(object_select, 'O', 1) JB_O(project, 'J', 2) JB_O(to_screen, 'K', 2) JB_O(render, 'H', 0)
JB_O(pump, 'U', 0) JB_O(blit, 'I', 3) JB_O(fade, 'Z', 0) JB_O(present, 'Y', 0) JB_O(delete_array, 'X', 1)
JB_O(rtti, 'n', 2) JB_O(hud, 'h', 1) JB_O(palette, 'v', 2)
JB_O(vol_stream, 'a', 2) JB_O(stream_read, 'g', 3) JB_O(stream_destroy, 'k', 2) JB_O(buttons, 'm', 1)
JB_O(port_a, 'q', 2) JB_O(port_b, 'u', 2) JB_O(port_event, 'y', 2) JB_O(port_texts, 't', 4)
JB_O(factory1, '1', 2) JB_O(factory2, '2', 2) JB_O(factory3, '3', 2) JB_O(factory4, '4', 2) JB_O(factory5, '5', 2)
JB_O(factory6, '6', 2) JB_O(factory7, '7', 2) JB_O(scale_a, '8', 1) JB_O(scale_b, '9', 1) JB_O(scale_c, '0', 1)
JB_O(vehicle, 'z', 1) JB_O(power_on, 'M', 1)
static uint32_t __cdecl jr_o_vector(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    uint32_t a[6] = {a0, a1, a2, a3, a4, 0};
    return jb_act('e', a);
}
static const jb_stub_t jr_stubs[] = {
    {0x00492becu, (void *)jb_o_path, 'D', 2}, {0x0045cdf4u, (void *)jb_o_load, 'L', 1},
    {0x00462830u, (void *)jb_o_sfx_play, 'P', 1}, {0x00462ac4u, (void *)jb_o_sfx_stop, 'Q', 1},
    {0x004011a0u, (void *)jb_o_camera, 'C', 1}, {0x0048c2e4u, (void *)jb_o_view_setup, 'V', 1},
    {0x0048c444u, (void *)jb_o_object_select, 'O', 1}, {0x0048c5fcu, (void *)jb_o_project, 'J', 2},
    {0x0048c6d0u, (void *)jb_o_to_screen, 'K', 2}, {0x0045fca8u, (void *)jb_o_render, 'H', 0},
    {0x00464c78u, (void *)jb_o_pump, 'U', 0}, {0x0045c8bcu, (void *)jb_o_blit, 'I', 3},
    {0x00430b34u, (void *)jb_o_fade, 'Z', 0}, {0x0045fba4u, (void *)jb_o_present, 'Y', 0},
    {0x0047758cu, (void *)jb_o_delete_array, 'X', 1}, {0x00496406u, (void *)jb_o_rtti, 'n', 2},
    {0x00472f14u, (void *)jb_o_vol_stream, 'a', 2}, {0x0049667cu, (void *)jr_o_vector, 'e', 5},
    {0x004735b8u, (void *)jb_o_stream_read, 'g', 3}, {0x00472f64u, (void *)jb_o_stream_destroy, 'k', 2},
    {0x00434368u, (void *)jb_o_buttons, 'm', 1}, {0x00436244u, (void *)jb_o_port_a, 'q', 2},
    {0x00435bf0u, (void *)jb_o_port_b, 'u', 2}, {0x004369a4u, (void *)jb_o_port_event, 'y', 2},
    {0x00435970u, (void *)jb_o_port_texts, 't', 4},
    {0x00432340u, (void *)jb_o_factory1, '1', 2}, {0x004323e0u, (void *)jb_o_factory2, '2', 2},
    {0x00432454u, (void *)jb_o_factory3, '3', 2}, {0x004324c8u, (void *)jb_o_factory4, '4', 2},
    {0x0043254cu, (void *)jb_o_factory5, '5', 2}, {0x004325c0u, (void *)jb_o_factory6, '6', 2},
    {0x00432634u, (void *)jb_o_factory7, '7', 2},
    {0x00442ea0u, (void *)jb_o_scale_a, '8', 1}, {0x00442e60u, (void *)jb_o_scale_b, '9', 1},
    {0x00442ef4u, (void *)jb_o_scale_c, '0', 1}, {0x00410644u, (void *)jb_o_vehicle, 'z', 1},
    {0x00438ddcu, (void *)jb_o_power_on, 'M', 1},
    /* method-table slots: no VA of the original */
    {0, (void *)jb_o_hud, 'h', 1}, {0, (void *)jb_o_palette, 'v', 2},
};
#define JR_STUBS (sizeof(jr_stubs) / sizeof(jr_stubs[0]))

/* 0x45d85c() on a random world. */
static int jr_drop_case(unsigned k) {
    jb_begin();
    jb_clock_step = 1u + jb_rand(12u);
    /* The wrap case: the end time (first clock + 0x1e) wraps past 2^32 while the
     * second clock read does not, so the unsigned test ends the loop after one
     * pass. (A clock that wraps inside the 30 ms keeps the original looping until
     * it reaches the end time again, about 2^32 ticks; not run.) */
    if (!jb_rand(16u)) { jb_clock[0] = 0xffffffe2u + jb_rand(18u); jr_setup_key(DK_LAND_WRAP); }
    /* The graphics context's clip rectangle (GC+0x210..+0x21c) and a second context. */
    jr_bottom = jb_rand(8u) ? (int32_t)(100u + jb_rand(380u)) : (int32_t)jb_signed(50u);
    jb_w32(JB_GC + 0x210u, jb_rand(100u));
    jb_w32(JB_GC + 0x214u, jb_signed(80u));
    jb_w32(JB_GC + 0x218u, 200u + jb_rand(440u));
    jb_w32(JB_GC + 0x21cu, (uint32_t)jr_bottom);
    random_bytes(jb_world_start + JB_GC2, 0x240u);
    jb_w32(JB_GC2 + 0x22cu, jb_at(JB_GC2 + 0x230u));   /* the fill stand-in reads the colour record */
    /* The player (its position at +0x26), the camera, the bitmap and its record. */
    random_bytes(jb_world_start + JB_PLAYER, 0x40u);
    uint32_t z = jb_rand(8u) ? jb_signed(200000u) : 0x80000000u + jb_rand(4000u);
    jb_w32(JB_PLAYER + 0x2eu, z);
    jr_setup_key((int32_t)(z - 4000u) < (int32_t)z ? DK_PASSES : DK_NO_PASSES);
    random_bytes(jb_world_start + JB_CAMERA, 0x20u);
    random_bytes(jb_world_start + JB_BITMAP, 0x20u);
    jb_w16(JB_BITMAP + 4u, jb_rand(4u) ? jb_rand(200u) : random32());
    random_bytes(jb_world_start + JB_RECORD, 0x10u);
    jb_w32(JB_RECORD + 8u, jb_at(JB_LIST));
    jb_w32(JB_LIST, jb_at(JB_BITMAP));
    jb_w32(JB_HUD, jb_at(JB_HUD_VT));
    jb_w32(JB_HUD_VT + 8u, jb_fn((void *)jb_o_hud));
    jb_w32(JB_P + 0x1f5u, jb_at(JB_HUD));
    for (unsigned i = 0; i < 2u; ++i) {
        random_bytes(jb_world_start + JB_PALOBJ + 0x20u * i, 0x20u);
        jb_w32(JB_PALOBJ + 0x20u * i, jb_at(JB_PAL_VT));
    }
    random_bytes(jb_world_start + JB_PAL_VT, 0x20u);
    jb_w32(JB_PAL_VT + 0x18u, jb_fn((void *)jb_o_palette));
    /* DATA. */
    jb_set32(0x004a0a10u, random32());
    jb_set32(0x004d24e6u, jb_at(JB_PLAYER));
    jb_set32(0x004d24eau, jb_at(JB_CAMERA));
    jb_set32(0x0049b088u, jb_at(JB_P));
    jb_set32(0x0049f138u, jb_rand(3u) ? 1u + jb_rand(0xffffu) : 0u);
    for (unsigned i = 0; i < 2u; ++i) {
        uint32_t x = jb_rand(3u) ? random32() : 0u;
        jb_set32(0x004d274cu + 4u * i, x);
        jr_setup_key(x ? DK_ARRAY : DK_NO_ARRAY);
        uint32_t o = jb_rand(3u) ? jb_at(JB_PALOBJ + 0x20u * i) : 0u;
        jb_set32(0x004d2744u + 4u * i, o);
        if (o) jr_setup_key(i ? DK_PAL_SECOND : DK_PAL_FIRST);
        else if (i) jr_setup_key(DK_NO_PAL);
    }
    jb_set8(0x0049aef4u, random32());
    /* The scripts. */
    for (unsigned i = 0; i < 16u; ++i) {
        switch (jb_rand(4u)) {
        case 0: jr_y[i] = -(int32_t)jb_rand(400u); break;
        case 1: jr_y[i] = 1 + (int32_t)jb_rand(jr_bottom > 1 ? (uint32_t)jr_bottom : 2u); break;
        case 2: jr_y[i] = jr_bottom + (int32_t)jb_rand(200u); break;
        default: jr_y[i] = (int32_t)jb_signed(600u); break;
        }
    }
    for (unsigned i = 0; i < 8u; ++i) jr_fade[i] = jb_rand(6u) ? 1u + jb_rand(0x100u) : 0u;
    for (unsigned i = 0; i < 2u; ++i) jr_rtti[i] = jb_signed(6u);
    jr_gc_switch = jb_rand(4u) ? 0u : 1u + jb_rand(120u);
    jr_blit_move = !jb_rand(3u);
    jb_op_add(0x0045d85cu, JB_EAX, 0, 0);
    return jb_run("drop", k);
}

/* 0x431bf8(H, name) on a random world and a random layout file. */
static int jr_panel_case(unsigned k) {
    jb_begin();
    random_bytes(jb_world_start + JB_CLIP, 0x230u);
    jb_w32(JB_V + 0x40u, jb_at(JB_CLIP));
    random_bytes(jb_world_start + JB_H, 0x260u);
    random_bytes(jb_world_start + JB_GAUGES, 0x300u);
    random_bytes(jb_world_start + JB_VEHICLE, 0x210u);
    random_bytes(jb_world_start + JB_VREC, 0x60u);
    jb_w32(JB_VEHICLE + 0x1f2u, jb_at(JB_VREC));
    uint32_t power = jb_rand(2u) ? 0u : 1u + jb_rand(0xffffu);
    jb_w16(JB_VREC + 0x50u, power);
    jr_setup_key(power ? PK_POWER : PK_NO_POWER);
    jb_w32(JB_H + 0x203u, jb_at(JB_VEHICLE));
    jb_w32(JB_P + 0x203u, jb_at(JB_VEHICLE));
    for (unsigned i = 0; i < 10u; ++i) {
        uint32_t g = jb_rand(3u) ? jr_gauge(jb_rand(12u)) : 0u;
        jb_w32(JB_H + 0x70u + 4u * i, g);
        if (!g) jr_setup_key(PK_NULL_SLOT);
    }
    unsigned is_h = !jb_rand(3u);
    jb_set32(0x0049b088u, jb_at(is_h ? JB_H : JB_P));
    if (is_h) jr_setup_key(PK_PANEL_IS_H);
    jb_set32(0x004a0a1cu, random32());
    jb_set32(0x004d254eu, jb_rand(4u) ? 320u << jb_rand(2u) : random32());
    jb_set32(0x004d2552u, jb_rand(4u) ? 200u << jb_rand(2u) : random32());
    static const uint16_t shifts[8] = {0, 0, 1, 1, 2, 3, 0x21, 0xffff};
    jb_set16(0x004d253au, shifts[jb_rand(8u)]);
    uint32_t port = jb_rand(2u) ? 0u : (jb_rand(4u) ? 1u + jb_rand(0xffffu) : 0x100u);
    jb_set16(0x004aa728u, port);
    jr_setup_key(port ? PK_PORT_A : PK_PORT_B);
    /* The layout file: offsets (L+0, L+4), the rectangle count L+0x10, the
     * port rectangles (L+0x680, L+0x684, L+0x694). */
    for (unsigned i = 0; i < sizeof(jr_layout) / 4u; ++i) jr_layout[i] = jb_rand(4u) ? jb_signed(700u) : random32();
    unsigned offset = jb_rand(3u) != 0u;
    jr_layout[0] = offset && jb_rand(4u) ? jb_signed(60u) : 0u;
    jr_layout[1] = offset && jb_rand(4u) ? jb_signed(60u) : 0u;
    jr_setup_key(jr_layout[0] || jr_layout[1] ? PK_OFFSET : PK_NO_OFFSET);
    jr_layout[4] = jb_rand(6u) ? jb_rand(11u) : 0u - (1u + jb_rand(3u));
    jr_setup_key((int32_t)jr_layout[4] > 0 ? PK_RECTS : PK_NO_RECTS);
    jr_layout[0x680u / 4u] = jb_rand(64u);
    jr_port_ret = random32();
    jr_power_ret = random32();
    jr_reslot = !jb_rand(3u);
    if (jr_reslot && power) jr_setup_key(PK_RESLOT);
    jb_op_add(0x00431bf8u, JB_EAX, 2, jb_at(JB_H));
    jb_ops[jb_op_count - 1u].b = random32();
    return jb_run("panel", k);
}

static int cockpit_rest_cases(void) {
    current_function = "cockpit-rest";
    es2_sim_identity_memory(&ir_memory);
    es2_sim_rtl_memory(&ir_memory, &su_rtl);
    su_data_pristine = malloc(SU_DATA_SIZE);
    su_snap[0] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    su_snap[1] = malloc(SU_DATA_SIZE + IR_REGION_SIZE + 0x1000u);
    ir_region = ww_alloc(IR_REGION_SIZE);
    lc_stack = VirtualAlloc(NULL, 0x10000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    lc_bits = ww_alloc(LC_BITS_SIZE);
    jb_world = VirtualAlloc(NULL, JB_WORLD_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    jb_world_start = malloc(2u * JB_WORLD_SIZE);
    if (!su_data_pristine || !su_snap[0] || !su_snap[1] || !ir_region || !lc_stack || !lc_bits || !jb_world || !jb_world_start)
        return fail("cockpit-rest: memory");
    if (get32(mapped + (SU_COUNTER - 0x400000u))) return fail("cockpit-rest: the counter word is not free");
    su_pristine_take();
    if (!ir_patch_import("cw3220.dll", "_calloc", (void *)ir_calloc) || !ir_patch_import("cw3220.dll", "_free", (void *)ir_free))
        return fail("cockpit-rest: import slot not found");
    jb_extra = jr_stubs;
    jb_extra_count = JR_STUBS;
    if (jb_dispatch_setup() || jb_patch_all(1)) return 1;
    int bad = 0;
    static const struct { const char *name; int (*run)(unsigned); unsigned cases; } parts[] = {
        {"drop", jr_drop_case, 600}, {"panel", jr_panel_case, 3000},
    };
    for (unsigned p = 0; !bad && p < sizeof(parts) / sizeof(parts[0]); ++p) {
        for (unsigned k = 0; !bad && k < parts[p].cases; ++k) {
            current_case = k; current_case_valid = 1;
            bad = parts[p].run(k);
        }
        if (!bad) printf("PASS: cockpit-rest %s: %u cases identical\n", parts[p].name, parts[p].cases);
    }
    if (!bad) {
        printf("COCKPIT-REST coverage:");
        for (unsigned i = 0; i < JR_KEYS; ++i) {
            printf(" %s=%u", jr_key_names[i], jr_keys[i]);
            if (!jr_keys[i]) bad = fail("cockpit-rest: a branch was not reached");
        }
        printf("\nCOCKPIT-REST stand-in calls (original):");
        static const char letters[] = "DLPQCVOJKHUIZYXnhvRTNaegkmquyt1234567890zM";
        for (const char *l = letters; *l; ++l) printf(" %c=%u", *l, jb_letters[(unsigned char)*l]);
        printf("\n");
    }
    current_case_valid = 0;
    if (jb_patch_all(0)) bad = 1;
    put32(mapped + 0x2cbee4u, 0);
    memcpy(mapped + SU_DATA_RVA, su_data_pristine, SU_DATA_SIZE);
    return bad;
}
