#ifndef ES2_CORE_TYPE_LOADS_H
#define ES2_CORE_TYPE_LOADS_H
#include "es2/core/weapons.h"
/* The MECH and FLYER type loads of the world load's pass 1
 * (docs/reverse-engineering/mission_load.md, section "MECH and FLYER type
 * loads"). All fifteen are cdecl; each returns EDX:EAX exactly as the original
 * leaves them; entry[] = EAX/EDX/ECX at entry; *ecx (may be NULL) receives the
 * original's ECX at return (dead after every call site, the framework returns
 * its own). Every game call goes through g->call with the original's
 * EAX/EDX/ECX, observed by its reference VA (virtual calls by their slot, slot
 * +0 keyed 100h): the runtime record 0x49630c, new[] 0x4774f0, the array
 * constructor 0x49667c, memset 0x49681a, the file layer (0x492d00, 0x492bec,
 * 0x492f54, the stream constructor 0x472f14, stream_read 0x4735b8,
 * stream_destroy 0x472f64, the report 0x479c78), the shape loads
 * 0x474cd8/0x474c38, the H3 displays 0x4378d8/0x4379cc, the mount entry
 * 0x40fea0, the debris set 0x40874c, multiply_q10 0x47e0b0, dmg_load 0x40d160,
 * collision_load 0x40cd88, the shape virtuals +20h/+24h, the node virtual +0
 * and the fifteen among themselves, so nested hooks keep their modes. Computed
 * here: the type-index leaves 0x41ff00 (MECH, byte table 0x4a9d08) and 0x422e80
 * (FLYER, 0x4a9dcc) and the Q16 helpers 0x47e068 ((a << 16) / b; a zero divisor
 * or a quotient outside 32 bits faults in the original and is outside the
 * contract) and 0x47e08d (bits 16..47 of a * b). sx = sign extension of a word.
 * */

/* Scratch frames (the synthetic EBP of 0x49630c and the locals the
 * callees receive, at the original's offsets): g->scratch needs the
 * _SCRATCH bytes, EBP = g->scratch + _FRAME. */
#define ES2_MECH_TYPES_SCRATCH 0x28u
#define ES2_MECH_TYPES_FRAME 0x24u
#define ES2_MECH_TYPE_LOAD_SCRATCH 0x64u
#define ES2_MECH_TYPE_LOAD_FRAME 0x60u
#define ES2_GUN_LIST_LOAD_SCRATCH 0x54u
#define ES2_GUN_LIST_LOAD_FRAME 0x50u
#define ES2_LOD_REMAP_SCRATCH 0x1a4u
#define ES2_LOD_REMAP_FRAME 0x1a0u

/* mech_types 0x42049c (): runtime record (descriptor 0x49a3e0) at f-24h;
 * [0x4a9d60] = 0x492d00("mechs", 55h, 8) (the 85 type names); new[](21Eh x
 * sx(count 0x4a9d06) + 4) into f-4 and, when not null, context 14h,
 * 0x49667c(block, 21Eh, sx(count), 211h, 0x420817), context 8; [0x4a9d64]
 * = the array (or 0); 0x4378d8(count word); then for each type t = 0..54h
 * whose index byte is not FFh, 0x4201a8(t). EAX = the record's saved FS:0
 * head (the adapter stores it to FS:0, as the epilogue does). */
uint64_t es2_mech_types_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],uint32_t *ecx);

/* mech_type_load 0x4201a8 (t): one MECH type into the record rec =
 * [0x4a9d64] + 21Eh x index (the index byte of t). Record (descriptor
 * 0x49a3b0) at f-24h; the name [[0x4a9d60] + 4 sx(t)]; the OVL record
 * 0x492f54(rec+2, D8h, 1, name.dat); words rec+0 = index, rec+6Ch = t; the
 * shape list S = rec+DEh: 0x474cd8(S, name.dts, 1E0003h, 0) gives the count
 * word S+0Ch, S+4 = 0x49a344, S+8 = 0x49667c(new[](6 x count + 4), 6,
 * count, 211h, 0x4207db) or 0; 0x420090(S, rec+30h); R+0C6h = R+4 (R =
 * rec+2); when word R+4Eh is 0: q = 0x47e068(400 x sx(R+0C4h), sx(R+0C2h)),
 * R+0C0h = multiply_q10(13Bh, sx(R+4)), R+2, R+4, R+2Ch, R+6Ch scaled by
 * 0x47e08d(word, q); the gun list E = 0x40fee8(name.gl, index + word
 * 0x4a9d70), n = 0x40fc50(E, f-60h); per shape k: 0x40304c(6k + [S+8],
 * shape k, n, f-60h) and shape k +26h = 0x4a9d72 + 8 sx(rec+96h); rec+0DAh =
 * 0x4379cc(name, index); when word rec+50h is not 0: 0x492f54(rec+1DCh, 36h,
 * 1, name.fm) and +0Eh of it = 0x47e068([+22h] - [+26h], [+2Ah] - [+2Eh]);
 * when the byte rec+0CEh is not 0: 0x40874c(rec+212h, rec+0CEh, 0x4a9d72 +
 * 8 sx(rec+96h)). EAX = the saved FS:0 head (stored by the adapter). */
uint64_t es2_mech_type_load_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx);

/* lod_remap 0x420090 (S, B): for a shape list S with count word S+0Ch
 * above 1: memset(rows f-1A0h, FFh, 190h); for each part byte B[i] (i <
 * 50, up to the first negative byte): node0 = shape 0's virtual +20h(B[i]),
 * and for each other shape k with a node: row k [word +4 of that node] =
 * the low byte of node0's word +4 (row k at f-1D2h + 50k); then for each
 * shape k = 1 .. count-1: 0x41ff88(shape k, row k, word +10h of [shape 0
 * +2Ah]). f = g->scratch + ES2_LOD_REMAP_FRAME; a row store outside the
 * frame window (f-1A0h .. f-1) is skipped: the original would write into
 * its saved registers or its caller's frame (outside the contract; with the
 * game's LOD counts and transform indices only an index -1 in shape 1
 * leaves the window, and the caller faults unless that store changes
 * nothing). */
uint64_t es2_lod_remap_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t shapes,uint32_t parts,
    const uint32_t entry[3],uint32_t *ecx);

/* node_remap 0x41ff88 (N, table, limit): by the class id of the node's
 * virtual +0: TSGroup 140014h remaps the word N+4 (0x41ff68); TSCellAnimPart
 * 14000Bh remaps N+4 and walks the children; TSBSPPart 140015h remaps each
 * word w of [N+1Ch] (count N+16h) with 0 < w < limit and table[w] >= 0, then
 * walks the children; 140007h, 140008h, 14000Ch and ANShape 1E0003h walk the
 * children; every other class ends. The children walk calls 0x41ff88 for
 * each entry of [N+12h] (count N+10h, reread). */
uint64_t es2_node_remap_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t node,uint32_t table,
    uint32_t limit,const uint32_t entry[3],uint32_t *ecx);

/* remap_word 0x41ff68 (P, table): a word w >= 0 at P becomes the
 * sign-extended byte table[w]. EAX = P. */
uint64_t es2_remap_word_registers(const es2_guest_memory *,uint32_t word,uint32_t table,const uint32_t entry[3],uint32_t *ecx);

/* mech_type_select 0x41ff10 (t): a type without an index byte gets the next
 * index (count word 0x4a9d06, incremented) and a gun-list slot
 * (0x40feb4(1)), the first one's slot base into the word 0x4a9d70; every
 * call then reserves one collision and one damage slot (0x40cbd4(1),
 * 0x40d200(1)). */
uint64_t es2_mech_type_select_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx);

/* flyer_type_select 0x422e90 (t): a type without an index byte (0x4a9dcc)
 * gets the next index (count 0x4a9dca) and one damage and one collision
 * slot (0x40d200(1), 0x40cbd4(1)). */
uint64_t es2_flyer_type_select_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx);

/* gun_list_load 0x40fee8 (name, type): record (descriptor 0x498fb1) at
 * f-50h, context word f-40h = 8, the stream object f-30h built by
 * 0x472f14(f-30h, name); E = 0x40fea0(type) (the pushed dword carries the
 * caller's EBX high word: `ebx`); a stream status other than 0 reports
 * through 0x479c78; stream_read(f-30h, 2, E), E+2 = 0x49667c(0, 1Ah,
 * sx(count), 1, 0x4117a5), stream_read(f-30h, 1Ah x sx(count), E+2's block),
 * stream_destroy(f-30h, 2). EAX = E, EDX = the saved FS:0 head (stored by
 * the adapter). */
uint64_t es2_gun_list_load_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t name,uint32_t type,
    uint32_t ebx,const uint32_t entry[3],uint32_t *ecx);

/* gun_classes 0x40fc50 (L, out): for each 26-byte entry of [L+2] while i <
 * the count word L (reread): out[i] = the entry's word +0 when its signed
 * byte +6 is below 4, else FFFFh. AX = the count word (reread). */
uint64_t es2_gun_classes_registers(const es2_guest_memory *,uint32_t list,uint32_t out,const uint32_t entry[3],uint32_t *ecx);

/* node_list 0x40304c (L, S, n, classes): word L+4 = n, [L] = new[](4 x
 * sx(n)); then for the counter word L+4 (reread once) down to 0: entry =
 * the shape's virtual +24h(class) for a class word >= 0, else 0 (a null
 * block ends the list). ECX at return = n << 16 | the counter word. */
uint64_t es2_node_list_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t list,uint32_t shape,
    uint32_t n,uint32_t classes,const uint32_t entry[3],uint32_t *ecx);

/* gun_slot_base 0x40feb4, col_slot_base 0x40cbd4, dmg_slot_base 0x40d200
 * (n): AX = the running slot word (0x4a98e8, 0x4987dc, 0x498856), DX = n,
 * the word += n. `image` is the image base. */
uint64_t es2_gun_slot_base_registers(const es2_guest_memory *,uint32_t image,uint32_t n,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_col_slot_base_registers(const es2_guest_memory *,uint32_t image,uint32_t n,const uint32_t entry[3],uint32_t *ecx);
uint64_t es2_dmg_slot_base_registers(const es2_guest_memory *,uint32_t image,uint32_t n,const uint32_t entry[3],uint32_t *ecx);

/* flyer_types 0x422fb4 (): nothing when the count word 0x4a9dca is 0;
 * otherwise [0x4a9df0] = 0x492d00("flyers", 22h, 4), [0x4a9df4] =
 * 0x49667c(0, 70h, sx(count), 1, 0x423098), then 0x422ed0(t) for each type
 * t = 0..21h whose index byte is not FFh. */
uint64_t es2_flyer_types_registers(const es2_guest_memory *,const es2_weapon_graph *,const uint32_t entry[3],uint32_t *ecx);

/* flyer_type_load 0x422ed0 (t): one FLYER type into rec = [0x4a9df4] + 70h
 * x index: 0x492f54(rec, 2Eh, 1, name.dat); rec+36h = 0x474c38(name.dts)
 * and its +26h = 0x4a9d8a; rec+2Eh = dmg_load(name.dmg); rec+32h =
 * collision_load(name.col); 0x492f54(rec+3Ah, 36h, 1, name.fm) and its +0Eh
 * = 0x47e068([+22h] - [+26h], [+2Ah] - [+2Eh]) (EDX:EAX its quotient and
 * remainder). */
uint64_t es2_flyer_type_load_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t type,
    const uint32_t entry[3],uint32_t *ecx);
#endif
