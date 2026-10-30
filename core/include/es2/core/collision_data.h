#ifndef ES2_CORE_COLLISION_DATA_H
#define ES2_CORE_COLLISION_DATA_H
#include "es2/core/weapons.h"
/* The collision data of collide.cpp (docs/reverse-engineering/ mission_load.md,
 * section "collision data"): the reader chain of a type's COL resource, the
 * bounding spheres, and the part-list scan of the weapon fit. All nine are
 * cdecl; each returns EDX:EAX exactly as the original leaves them; entry[] =
 * EAX/EDX/ECX at entry; *ecx (may be NULL) receives the original's ECX at
 * return (dead after every call site, the framework returns its own). Every
 * game call goes through g->call with the original's EAX/EDX/ECX, observed by
 * its reference VA (the stream read by its slot +18h): the stream reads
 * (virtual +18h of the stream object), new 0x4774f0, new[] 0x49667c, the
 * runtime record 0x49630c, the stream constructor 0x472f14 and destructor
 * 0x472f64, the assertion 0x479c78, dist3d_delta 0x47de72, the volume match
 * 0x40ce28 and the other eight of these nine, so nested hooks keep their modes.
 * A group is 10 bytes (part word +0, volume count +4, volumes +6), a volume 16
 * bytes (sphere count +0, spheres +2, region word +6, centre +8..+0Dh, radius
 * +0Eh), a sphere 8 bytes (x, y, z, radius words). s16 = sign extension of a
 * word. */

/* collision_load 0x40cd88 (name): the COL resource of a type. Runtime
 * record (descriptor 0x4987f0) at f-50h, context word f-40h = 8, the
 * stream object at f-30h built by 0x472f14(f-30h, name); a stream status
 * [f-2Ch] other than 0 reports through 0x479c78. The descriptor slot is
 * [0x4a9824] + 6 x s16(the word 0x4987de), which is incremented; then
 * 0x40ccf8(slot, f-30h) and 0x472f64(f-30h, 2). EAX = the slot, EDX = the
 * record's saved FS:0 head (the adapter stores it to FS:0, as the
 * original's epilogue does). f = g->scratch + ES2_COLLISION_LOAD_FRAME
 * (the synthetic EBP of 0x49630c); g->scratch needs
 * ES2_COLLISION_LOAD_SCRATCH bytes. */
#define ES2_COLLISION_LOAD_SCRATCH 0x60u
#define ES2_COLLISION_LOAD_FRAME 0x50u
uint64_t es2_collision_load_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t name,
    const uint32_t entry[3],uint32_t *ecx);

/* collision_read 0x40ccf8 (D, S): a nonzero status [S+4] reports through
 * 0x479c78; S.read(2, D) (the group count); a nonzero count c: new(10 x
 * s16(c)) into [D+2], then per group i while i < s16([D]) (reread):
 * 0x40cc50(group, S) and 0x40ccc8(group). */
uint64_t es2_collision_read_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t descriptor,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx);

/* collision_entry 0x40cc50 (G, S): S.read(2, G), S.read(2, G+4); a nonzero
 * volume count k: new[](0, 10h, s16(k), 1, 0x40cf04) into [G+6], then
 * 0x40cc14(volume, S) per volume while i < s16([G+4]) (reread). AX = 1
 * (EAX's high word stays). */
uint64_t es2_collision_entry_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t group,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx);

/* collision_volume 0x40cc14 (V, S): S.read(2, V+6); when the result's low
 * byte is not 0, 0x40c7c4(V, S). EAX = that byte. */
uint64_t es2_collision_volume_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t volume,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx);

/* collision_points 0x40c7c4 (V, S): S.read(2, V) (the sphere count c);
 * when the read's low byte and the word 8c (16 bits) are not 0: new[](0,
 * 8, s16(c), 1, 0x40c8be) into [V+2] and S.read(s16(8c), [V+2]). EAX = the
 * last read's low byte. */
uint64_t es2_collision_points_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t volume,
    uint32_t stream,const uint32_t entry[3],uint32_t *ecx);

/* collision_bounds 0x40ccc8 (G): word [V+0Eh] = 0x40c5d0(V, V+8) for the
 * volumes V of G while i < s16([G+4]) (reread). EDX:EAX/ECX: the last
 * call's, or the entry's when there is none. */
uint64_t es2_collision_bounds_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t group,
    const uint32_t entry[3],uint32_t *ecx);

/* collision_radius 0x40c5d0 (V, O): the box of V's spheres (16-bit sums
 * centre +- radius, minima from 10000, maxima from -10000, signed
 * compares), the centre O = (minimum + maximum) SAR 1 per axis (16-bit
 * sums, written as the original does: dword, word, three adds, three
 * shifts), and dist3d_delta(s16(max x - cx), s16(max y - cy), s16(max z -
 * cz)) with EAX/EDX/ECX = O's high word and cy/cz/cx; its registers are
 * returned. */
uint64_t es2_collision_radius_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t volume,
    uint32_t centre,const uint32_t entry[3],uint32_t *ecx);

/* collision_scan 0x40cec0 (T, A, B, n): for the groups g of the table T
 * ([T+2], count [T] reread) 0x40ce78(g, A, B, n's low word); the first
 * match gets 0x40ccc8(g) and ends the scan (its EDX:EAX, ECX = g); without
 * one EAX = s16([T]). */
uint64_t es2_collision_scan_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t table,
    uint32_t a,uint32_t b,uint32_t n,const uint32_t entry[3],uint32_t *ecx);

/* collision_match 0x40ce78 (G, A, B, n): 0x40ce28(V, A, B, n's low word)
 * for the volumes V of G until one returns AX other than 0; EAX = 1 when
 * one did (index below s16([G+4]) reread), else 0; EDX = s16([G+4]). */
uint64_t es2_collision_match_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t group,
    uint32_t a,uint32_t b,uint32_t n,const uint32_t entry[3],uint32_t *ecx);
#endif
