#ifndef ES2_CORE_TERRAIN_LOAD_H
#define ES2_CORE_TERRAIN_LOAD_H
#include "es2/core/weapons.h"
/* The terrain zone load of terrain.cpp (docs/reverse-engineering/
 * terrain.md, section "the terrain zone load"): the zone loader, the
 * grid object and its height-cache base, the materials, the cell generation
 * with its stream-A draws, and the cell-mark write. All six are cdecl; each
 * returns EDX:EAX exactly as the original leaves them; entry[] = EAX/EDX/ECX
 * at entry; *ecx (may be NULL) receives the original's ECX at return (dead
 * after every call site; the framework returns its own). Every game call
 * goes through g->call with the original's EAX/EDX/ECX, observed by its
 * reference VA (a virtual call by its slot): the runtime record 0x49630c,
 * _itoa 0x496820, the path builder 0x492bec, the stream constructor 0x472f14
 * and stream_destroy 0x472f64, the assertion 0x479c78, the stream reads
 * (slot +18h of the stream object, stream_read 0x4735b8), new 0x47749c and
 * new[] 0x4774f0, the array constructor 0x49667c, euler_matrix 0x47ebb8,
 * _strchr 0x4967a2, _stricmp 0x496868, dts_load 0x47a144, 0x496406, the
 * bitmap's slot +18h, _fopen 0x496802, _fscanf 0x4967b4, _fclose 0x496838,
 * rng_next 0x492ee0 on stream A 0x4d2599, the height-cache setter 0x4952e8
 * and reset 0x4952fc, and these six among themselves, so nested hooks keep
 * their modes.
 *
 * The grid object G (0x129 bytes): cells [G+0ECh] (16 bytes each: height byte
 * +0, material and flag byte +0Fh), marks [G+0F0h] (one byte per cell), origin
 * G+0F4h..G+0FCh, log2 columns +100h, log2 rows +104h, cell shift +108h, view
 * radius +10Ch, height offset +110h, the maximum height (then scaled) +114h,
 * height scale +118h, material count +11Dh and table +121h (unaligned dwords;
 * 8-byte records {texture, level}); the height cache at G+0D0h. */

/* zone_load 0x42789c (zone): frame f; the runtime record (descriptor
 * 0x49ab2a) at f-58h, context word f-48h, the stream object at f-30h, the
 * name "zone"+decimal at f-64h, the reads at f-70h, f-6Ch, f-74h, f-68h, the
 * angle words f-7Ch..f-78h, the origin copy f-88h..f-80h. A negative zone
 * word only clears the object table (word 0x4aaa7c, 0x4aaa74, 0x4aaa78):
 * EAX = 0, EDX = the byte 0x4d1f40. Otherwise EDX:EAX = 0x472f64's. *ecx =
 * the record's saved FS:0 head in both cases (the adapter stores it to FS:0,
 * as the original's epilogue does). f = g->scratch + ES2_ZONE_LOAD_FRAME;
 * g->scratch needs ES2_ZONE_LOAD_SCRATCH bytes, zeroed. */
#define ES2_ZONE_LOAD_SCRATCH 0xa0u
#define ES2_ZONE_LOAD_FRAME 0x98u
uint64_t es2_zone_load_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t zone,
    const uint32_t entry[3],uint32_t *ecx);

/* grid_ctor 0x46befc (G, unused): vtables 0x4a69f0, 0x4a6a14, count +1,
 * vtable 0x4a0c74; 0x49667c(G+28h, 8, 14h, 1, 0x49545b);
 * 0x495268(G+0D0h, 100h); origin 0; cell shift 10, view radius 8; count +1.
 * EAX = G, EDX = the count pointer; *ecx = 0x495268's ECX. */
uint64_t es2_grid_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t grid,
    const uint32_t entry[3],uint32_t *ecx);

/* height_cache_ctor 0x495268 (C, n): vtables 0x4a69f0, 0x4a6a14, count +1,
 * vtable 0x4a6a40; [C+0Ch] = n, [C+8] = 0, [C+4] = 100; [C+10h] =
 * new[]([C+0Ch]); [C+14h] = 0x49667c(0, 0Ch, [C+0Ch], 1, 0x4953c4); [C+18h] =
 * 0x49667c(0, 0Ch, 8, 1, 0x4953c4); count +1. EAX = C, EDX = the second
 * 0x49667c's EDX; *ecx = the count pointer. */
uint64_t es2_height_cache_ctor_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t cache,
    uint32_t size,const uint32_t entry[3],uint32_t *ecx);

/* terrain_materials 0x46c754 (G, name, b): frame f; the record (descriptor
 * 0x4a0c14) at f-84h, context word f-74h, the name copy at f-0C8h (the
 * original copies without a bound; the copy is clamped at the scratch's
 * end, which only names that already overwrite the original's frame reach),
 * the material stream at f-30h, the bitmap stream at f-60h. The materials
 * of "mat0": count to G+11Dh, new[](8 count) to G+121h, the records by
 * stream_read. A name whose first '.' starts ".dba" (any case): the bitmap
 * B = [[dts_load(f-60h, 0)+8]], 0x46c4c4(G, B, b), then B's count and
 * deleting destructor (slot +18h, 3) when B != 0. Otherwise the text grid:
 * _fopen, "%d %d %d %d" (columns, rows, top, bottom), the cells by
 * 0x49667c, one "%d" height per cell and stream-A draws for its material,
 * _fclose, 0x4952e8 and 0x4952fc. EAX = the last 0x472f64's, EDX = the
 * saved FS:0 head (the adapter stores it to FS:0). f = g->scratch +
 * ES2_TERRAIN_MATERIALS_FRAME; g->scratch needs
 * ES2_TERRAIN_MATERIALS_SCRATCH bytes, zeroed. */
#define ES2_TERRAIN_MATERIALS_SCRATCH 0x200u
#define ES2_TERRAIN_MATERIALS_FRAME 0xd0u
uint64_t es2_terrain_materials_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t grid,
    uint32_t name,uint32_t base,const uint32_t entry[3],uint32_t *ecx);

/* terrain_cells 0x46c4c4 (G, B, b): the exponents of B's width (word +6)
 * and height (word +4) to G+100h/G+104h (the smallest k with 2^k not below
 * the signed word), the cells by 0x49667c(0, 10h, n, 1, 0x4715e1) and the
 * marks by new[](n), n = 2^(columns+rows); [G+110h] = FFh, [G+114h] = 0.
 * Bitmap row y (pixels [B+0Eh], stride 2^columns) fills grid row
 * 2^rows-1-y: height byte = pixel - b (low byte; the maximum kept at
 * G+114h, signed compare), 0x470d74(G, cell, 0); then with the block mask
 * k = (1 << ((8 - [[G+121h]+4] + 13 - [G+108h]) & 31)) - 1: a cell whose
 * row and column are both multiples of k+1 draws rng_next(stream A) once
 * and gets material 1 (byte +0Fh = (byte & 2) | 4) when (draw & FFFh) <
 * 4CEh, else keeps (byte & 2); every other cell copies bits 2..7 of the
 * block's first cell. Then [G+110h] = 0, [G+114h] *= [G+118h],
 * 0x4952e8(G+0D0h, 0, [G+118h]) and 0x4952fc(G+0D0h). */
uint64_t es2_terrain_cells_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t grid,
    uint32_t bitmap,uint32_t base,const uint32_t entry[3],uint32_t *ecx);

/* cell_mark_set 0x470d74 (G, cell, v): byte [[G+0F0h] + ((cell - [G+0ECh])
 * >> 4)] = v (unsigned shift). EAX = [G+0F0h], EDX = the index; *ecx = the
 * entry ECX with the low byte v. */
uint64_t es2_cell_mark_set_registers(const es2_guest_memory *,uint32_t grid,uint32_t cell,uint32_t value,
    const uint32_t entry[3],uint32_t *ecx);

/* The base flattening after the actor load and the base-mark
 * raster (terrain.md, section "base flattening and the base-mark
 * raster"). n = [G+100h] (log2 columns), m = [G+104h] (log2 rows); shifts
 * count CL's low five bits. The cell of (x, y): index i = (y << n) + x;
 * i < 0 (signed) or i >= 1 << (n + m) gives the null cell, else
 * [G+0ECh] + 16 i. Game calls as in the zone load (g->call, observer
 * first, the original's EAX/EDX/ECX); *ecx (when not NULL) receives the
 * original's ECX, which is dead at every caller. */

/* cell_flag_get 0x470e00 (G, x, y): EAX = the zero-extended mark byte
 * [[G+0F0h] + i], EDX = i, *ecx = n. No game calls. */
uint64_t es2_cell_flag_get_registers(const es2_guest_memory *,uint32_t grid,uint32_t x,uint32_t y,uint32_t *ecx);

/* The two recursive floods run iteratively: one byte per visited cell on
 * the current path (the direction taken from its parent) in `directions`,
 * which holds `capacity` entries. The original recurses on the game stack
 * (44 bytes per level for the sum, 52 for the fill), so a path longer than
 * 1 MiB / 44 = 23,831 cells overflows the original's stack first; a path
 * longer than `capacity` stops the core with `overflow` = 1 (the adapter
 * then raises the stack overflow). `depth_max` keeps the deepest path. */
typedef struct es2_flood_stack { uint8_t *directions; uint32_t capacity, depth_max; int overflow; } es2_flood_stack;

/* flood_sum 0x470fe8 (G, x, y): with x and y in 0 .. 2^n - 1 (signed) and
 * the cell computed, cell_flag_get(G, x, y) through the bridge; when the
 * mark has bit 0 and not bit 1: cell_flag_set 470ddc(G, x, y, mark | 2),
 * [6B5474h] += 1, [6B5470h] += the height byte of the cell, then the eight
 * neighbours in the order (x-1, y+1), (x, y+1), (x+1, y+1), (x-1, y),
 * (x+1, y), (x-1, y-1), (x, y-1), (x+1, y-1). EDX:EAX: outside the grid
 * 1 << n and the entry EDX; an unmarked or visited cell cell_flag_get's;
 * a visited cell the last neighbour's. */
uint64_t es2_flood_sum_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t grid,uint32_t x,
    uint32_t y,const uint32_t entry[3],es2_flood_stack *stack,uint32_t *ecx);
/* flood_fill 0x471128 (G, x, y, h, w): as flood_sum with bit 2; when the
 * dword w is not 0 the height byte of the cell becomes h (low byte) right
 * after cell_flag_get, before the mark test; a visited cell gets
 * cell_flag_set(G, x, y, mark | 4) and height h, then the same eight
 * neighbours with w = 0, 1, 1, 0, 1, 0, 0, 0. */
uint64_t es2_flood_fill_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t grid,uint32_t x,
    uint32_t y,uint32_t height,uint32_t write,const uint32_t entry[3],es2_flood_stack *stack,uint32_t *ecx);
/* base_flatten 0x47129c (G): S = 1 << n once; for y, then x in 0 .. S - 1
 * (signed): cell_flag_get(G, x, y); a mark with bit 0 and not bit 1 starts
 * a region: [6B5470h] = [6B5474h] = 0, flood_sum 470fe8(G, x, y), q =
 * [6B5470h] / [6B5474h] (IDIV), flood_fill 471128(G, x, y, q, 0). Then the
 * shading pass 46c2e0(G) (H3). EDX:EAX = 46c2e0's, *ecx = S. A zero count
 * or 80000000h / -1 faults in the original: the core stops there with
 * *fault = 1 and divide[] = {sum, count} (the adapter executes the IDIV). */
uint64_t es2_base_flatten_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t grid,
    const uint32_t entry[3],uint32_t divide[2],int *fault,uint32_t *ecx);
/* mark_raster 0x47136c (G, P, k, rows, count): t = [[G+121h] + 8k + 4]
 * (the material record's level), s = [G+108h] - 13, bx = ([P] SAR (21 -
 * t)) << (8 - t - s), by = ((([P+4] SAR (21 - t)) + 1) << (8 - t - s)) -
 * 1; for row j, then column i in 0 .. count - 1 (signed): the cell of (bx
 * + (i SAR s), by - (j SAR s)); unless the word [4D2680h] (option -l) is
 * set, byte [cell+0Fh] = (byte & 2) | k << 2 (low byte); when byte
 * [[rows + 4j] + i] is not 0, cell_mark_set 470d74(G, cell, 1). EAX =
 * count after a row (rows when count <= 0, EDX = 0 then). */
uint64_t es2_mark_raster_registers(const es2_guest_memory *,const es2_weapon_graph *,uint32_t grid,uint32_t point,
    uint32_t type,uint32_t rows,uint32_t count,const uint32_t entry[3],uint32_t *ecx);
#endif
