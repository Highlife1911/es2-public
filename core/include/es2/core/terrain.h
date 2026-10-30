#ifndef ES2_CORE_TERRAIN_H
#define ES2_CORE_TERRAIN_H
#include <stdint.h>
#include "es2/core/guest.h"

/* Terrain grid G (guest object): cell array pointer [G+0xec] (16-byte cells:
 * height byte +0, diagonal flags byte +0x0f), log2 column count [G+0x100],
 * log2 row count [G+0x104], log2 cell size [G+0x108], height offset
 * [G+0x110] and height scale [G+0x118] (all 32-bit; shift counts use their
 * low five bits). Point P: 32-bit X at +0, Y at +4.
 *
 * 0x46e180: height of the terrain at P. Outside the grid, or when a corner
 * cell index leaves the cell array, the result is 0. Otherwise the four
 * corner heights (byte * scale + offset) are interpolated linearly inside
 * one of the two triangles of the cell; flags 2 split the cell along one
 * diagonal, flags 0 along the other, flags 1 and 3 always use the first
 * triangle of the flags-0 split. All arithmetic wraps at 32 bits; products
 * are shifted right arithmetically by the cell size. Returns EDX:EAX (EAX =
 * height; EDX is path dependent and reproduced). */
uint64_t es2_terrain_height_registers(const es2_guest_memory *memory, uint32_t terrain, uint32_t point);

/* 0x46e498: face normal of the cell triangle under P, same grid fields as
 * above. Outside the grid EAX = 0 and EDX = the cell column (X >> size).
 * Inside, EAX = cell + 1 (first normal) or cell + 7 (second normal), chosen
 * by the diagonal flags: flags 2 take the second normal when fx > fy, all
 * other flags when size - fx < fy (fx, fy = offsets inside the cell, signed
 * comparisons). EDX = fx for flags 2, size - fx otherwise. The cell index
 * (row << columns) + column is checked against 1 << (columns + rows); a
 * failing check makes the original read the flags at address 0x0f, which
 * this function reproduces (callers keep the grid small, see camera.md). */
uint64_t es2_terrain_face_registers(const es2_guest_memory *memory, uint32_t terrain, uint32_t point);

/* 0x47e610: intersection of the segment S..E (six dwords at `segment`) with
 * the plane n.x, n.y, n.z, c (four dwords at `plane`), the contact helper of
 * the terrain walk (docs/reverse-engineering/weapons.md). No hit:
 * EAX = 0, EDX = n.y * D.y (den = 0) or n.y * S.y (outside [S, E]). Hit:
 * the three dwords at `out` become the point, EAX = (out & ~0xff) | 1 and
 * EDX = frame - 0x28 (the original returns that frame address of its own
 * EBP; pass any stand-in, it is dead at every caller). */
uint64_t es2_line_plane_registers(const es2_guest_memory *memory, uint32_t frame, uint32_t plane, uint32_t segment,
                                  uint32_t out);
#endif
