# Terrain

The terrain grid of DBSIM: its load (zone file, materials, cells), the base
flattening after the actor load, the terrain pass of every rendered frame
(visibility, object binning, cell walk, cell draw, cell objects), the four
pure geometry helpers the walk uses, and the own code that implements them.

Addresses are VAs of the reference `VER95/DBSIM.EXE` (image base
`0x400000`, see [reference_version.md](../reference_version.md)). Every fact is read from the
entry-aligned `llvm-objdump -d -M intel` listing of that binary unless noted;
the switch table of `0x472bcc`, the exception descriptors `0x49ab2a` and
`0x4a0c14`, the strings and the import thunks were read from the file. Live
counts are the entry probes of the [DBSIM map](../reference-game/dbsim_map.md)
(suite order a60/c1s/c1c/c3_09/c4_08/c3_10, saturated at 1000).

G = the terrain grid `[0x4a0be0]` (= `[0x49aac8]`), cam = the camera
`[0x4d24ea]`, P = the player `[0x4d24e6]`.

Own code:

| Part | Kind | Code |
|---|---|---|
| zone load, grid and cache constructors, materials, cells, mark write | hooks, group `terrain_load` | `core/src/terrain_load.c`, `tools/reference-game/proxy/terrain_load_hooks.h` |
| base flattening, floods, base-mark raster, mark read | hooks, group `base_flatten` | `core/src/terrain_load.c`, `tools/reference-game/proxy/base_flatten_hooks.h` |
| `poly_clip`, `poly_spans`, `view_rect`, `cell_uv` | hooks, group `world` | `core/src/world.c` (`es2/core/world.h`), `tools/reference-game/proxy/world_hooks.h` |
| terrain set-up, walk, walkers, callbacks, cell draw, its leaves | service rows | `sim/src/terrain_draw.c` (`sim/dispatch/services/terrain_draw.h`) |
| binning, cell callback, sort tree, scene finish | service rows | `sim/src/scene.c` (`sim/dispatch/services/scene.h`) |
| view frustum library | service rows | `sim/src/frustum.c` (`sim/dispatch/services/frustum.h`) |

`terrain_load.c` is a module of its own: `terrain.c` holds the pure runtime
grid queries on `es2_guest_memory` (height, face, line/plane) without the
weapons bridge types, while the load-time functions use the weapons guest
bridge like the other mission-load modules.

## The grid object

`new(129h)`. Vtable `+0`; `+28h`..`+0C7h` twenty 8-byte elements constructed
in place by `0x49667c(G+28h, 8, 14h, 1, 0x49545b)` (the element constructor
returns its argument and writes nothing); the height cache `+0D0h` (vtable
`0x4a6a40`, `+4` = 100, `+8` the height offset, `+0Ch` the size 100h, `+10h`
a byte array of that size, `+14h` 100h vectors of 12 bytes, `+18h` 8
vectors); cells `[+0ECh]` (16 bytes each: height byte `+0`, cell normal
words `+1..+0Ch`, shade bytes `+0Dh`/`+0Eh`, material and flags byte
`+0Fh`: bits 0..1 the diagonal flags `terrain_height` reads, bits 2..7 the
material index), marks `[+0F0h]` (one byte per cell); origin
`+0F4h`..`+0FCh`; log2 columns `+100h`, log2 rows `+104h`, cell shift
`+108h`, view radius `+10Ch`, height offset `+110h`, maximum height (then
scaled) `+114h`, height scale `+118h`, material count `+11Dh` and table
`+121h` (unaligned dwords; 8-byte records {texture, level}), the cell
callback `+125h` (set by `0x461608`). `[0x49aac8]` and the terrain pointer
`[0x4a0be0]` both hold G.

Per frame the terrain set-up also writes the view-space cell basis (column
step `+4`, row step `+10h`, origin `+1Ch`, three dwords each), the view
polygon (`+0C8h` count, `+0CCh` points, the points at `+28h`) and the
visibility byte `+11Ch` ([Terrain visibility](#terrain-visibility-0x46cb9c)).

## The terrain zone load

The zone load and its five callees are own code (hooks, group
`terrain_load`). Validated in vitro and live (all suites identical,
after-load snapshot identical; load-side view in
[mission_load.md](mission_load.md#24-terrain-zone-load)).

| Hook | VA | Instr. | Role | Callers |
|---|---|---:|---|---|
| `zone_load` | `0x42789c` | 185 | the zone loader of `terrain.cpp` (the assertion string names the file) | the world load `0x424308` (`0x4252f7`) |
| `grid_ctor` | `0x46befc` | 34 | the grid object constructor (vtable `0x4a0c74`) | `zone_load` (`0x4279ce`; `0x427ac2` is dead code) |
| `height_cache_ctor` | `0x495268` | 42 | the height-cache base constructor (vtable `0x4a6a40`) | `grid_ctor` (`0x46bf3f`) |
| `terrain_materials` | `0x46c754` | 317 | the materials and the grid source (DBA bitmap or text) | `zone_load` (`0x427a8a`; `0x427b32` is dead code) |
| `terrain_cells` | `0x46c4c4` | 208 | the cells from a bitmap, with the material draws | `terrain_materials` (`0x46c86f`) |
| `cell_mark_set` | `0x470d74` | 11 | one mark byte by cell pointer | `terrain_cells` (`0x46c5e5`), the base marks `0x47136c` (`0x47146c`), `0x470e20` (`0x470e9c`) |

No dword of the file holds the address of any of the six, and a byte scan
of the code section for `E8`/`E9` rel32 finds exactly the call sites above.
`0x470e20` (a span fill of marks over the table `0x6b5068`) has no caller
and no reference: dead. Every callee stays game code behind the guest
bridge with its own mode: the runtime record `0x49630c`, `_itoa`
`0x496820`, the path builder `0x492bec`, the stream constructor `0x472f14`
and `stream_destroy` `0x472f64`, the assertion `0x479c78`, the stream reads
(slot `+18h` of the stream object; `stream_read` `0x4735b8` directly), new
`0x47749c`, new[] `0x4774f0`, the array constructor `0x49667c` (element
constructors `0x49545b`, `0x4953c4`, `0x4715e1`), `euler_matrix`
`0x47ebb8`, `_strchr` `0x4967a2`, `_stricmp` `0x496868`, `dts_load`
`0x47a144`, `0x496406` (a count from the object's type record), the
bitmap's slot `+18h` (deleting destructor), `_fopen` `0x496802`, `_fscanf`
`0x4967b4`, `_fclose` `0x496838`, `rng_next` `0x492ee0` on stream A
`0x4d2599`, the height-cache setter `0x4952e8` and reset `0x4952fc` (H3,
[keep list](#keep-list-for-an-own-h3-terrain-renderer) item 2), and the six
among themselves.

### Contracts of the zone load

All six are cdecl; EDX:EAX is reproduced exactly; every game call gets the
original's EAX/EDX/ECX (the core follows them step by step; the in-vitro
logs compare every one). "Count +n" is `[[0x6cbee4]] += n`, the pointer
reread each time.

**`zone_load` 0x42789c (Z)**, frame `add esp,-94h`: `0x49630c` (EAX = the
descriptor `0x49ab2a`, [+4] = −58h: the record at EBP−58h, context word
EBP−48h; the entry EDX/ECX). The first-use byte `[0x49aaf0]` is set to 1
when 0. `[0x4aaaa8]` = the byte `[0x4d1f40]` (the terrain detail of the
prefs), `[0x4aaaac]` = 1, `[0x49aad0]` = 20000 (EDX = that byte). Only the
low word of Z counts (`TEST BX,BX`, `MOVSX`). Negative: word `[0x4aaa7c]`,
`[0x4aaa74]`, `[0x4aaa78]` = 0 (the object table), EAX = 0, EDX = the byte,
ECX = the saved FS:0 head. Otherwise: the 9 bytes "zoneXXXX" of
`0x49aaf1` are copied to EBP−64h and `_itoa(sx Z, EBP−60h, 10)` (EAX = sx Z,
EDX = `[0x49aaf5]` with the low byte `[0x49aaf9]`, ECX = EBP−60h) writes the
digits over "XXXX"; `0x492bec(EBP−64h, [0x4a09f0])` (EDX = the extension
pointer, "dat") builds the path; context 8; `0x472f14(EBP−30h, path)` (EAX
= EBP−30h); a nonzero status [EBP−2Ch] reports through `0x479c78(0x6c56c8,
5, 0, "terrain.cpp", 49h, path)` (EAX = the status, EDX = 0). Two reads
of 4 bytes (slot `+18h`, the vtable reread each time) into EBP−70h and
EBP−6Ch (not used further: `grid_ctor` ignores its second argument), then
`new(129h)` (POP ECX = 129h) into EBP−34h; non-null: context 20h,
`grid_ctor(G, EBP−70h)` (EAX = G, EDX = EBP−70h, ECX = G reread), count −4,
context 14h. `[0x49aac8]` = [EBP−34h]; the third read into EBP−74h (EAX =
the vtable, EDX = G, ECX = EBP−30h); `[[0x49aac8]+108h]` = the cell shift
(the pointer reread); the fourth read into EBP−68h; `[G+118h]` = the scale;
`[G+10Ch]` = 10 SAR ((shift − 14) & 31) when the shift is above 14
(signed), else 10. The origin `+0FCh`, `+0F4h`, `+0F8h` = 0 through three
zero locals (EBP−88h..−80h). `0x492bec(EBP−64h, "dba")` (EAX = EBP−64h,
EDX = ECX = 0), `terrain_materials(G, path, 0)` (EAX = G). The transform:
angle words EBP−7Ch..−78h = 0, `[0x4aaaa0]` = z, `[0x4aaa98]` = x,
`[0x4aaa9c]` = y (G+0F4h reread), `euler_matrix(EBP−7Ch, 0x4aaa84)` (EAX =
y, EDX = EBP−7Ch, ECX = x); `[0x4a0be0]` = `[0x49aac8]`;
`0x472f64(EBP−30h, 2)` (EAX = G, EDX = EBP−30h). EDX:EAX = `0x472f64`'s,
ECX = the saved head from the record (the epilogue stores it to FS:0). A
null grid would store at `[0+108h]` (`0x427a09`) and fault.

**`grid_ctor` 0x46befc (G, unused)**: [G] = `0x4a69f0`, `0x4a6a14`, count
+1 (EAX the pointer, EDX = G+28h), [G] = `0x4a0c74`; `0x49667c(G+28h, 8,
14h, 1, 0x49545b)` (ECX the entry's); `height_cache_ctor(G+0D0h, 100h)`
(ECX = G+0D0h); origin `+0FCh`, `+0F8h`, `+0F4h` = 0; `+108h` = 10, `+10Ch`
= 8; count +1. EAX = G, EDX = the count pointer, ECX = `height_cache_ctor`'s.

**`height_cache_ctor` 0x495268 (C, n)**: [C] = `0x4a69f0`, `0x4a6a14`,
count +1, [C] = `0x4a6a40`; `+0Ch` = n, `+8` = 0, `+4` = 100; `new[]([C+0Ch])`
(EAX = [C+0Ch], EDX = n, ECX = 0; POP ECX = the size) into `+10h`;
`0x49667c(0, 0Ch, [C+0Ch] reread, 1, 0x4953c4)` into `+14h`; `0x49667c(0,
0Ch, 8, 1, 0x4953c4)` into `+18h`; count +1. EAX = C, EDX = the second
`0x49667c`'s, ECX = the count pointer.

**`terrain_materials` 0x46c754 (G, name, b)**, frame `add esp,-0C8h`:
`0x49630c` (descriptor `0x4a0c14`, [+4] = −84h, context word EBP−74h).
The name is copied to EBP−0C8h with `REPNE SCASB` and `REP MOVSD/MOVSB`
(no bound; EAX = EBP−0C8h, EDX = length + 1, ECX = 0); context 8;
`0x492bec("mat0", [0x4a09f0])`; `0x472f14(EBP−30h, path)` (EDX = EBP−30h);
read 4 bytes into G+11Dh (EAX = G+11Dh, EDX = EBP−30h, ECX = the vtable);
`new[](8 × [G+11Dh])` (POP ECX = the size) into G+121h; for i while i <
[G+11Dh] (signed, reread): `stream_read(EBP−30h, 8, [G+121h] + 8i)`.
`_strchr(EBP−0C8h, '.')` (ECX = EBP−0C8h); when a dot is found,
`_stricmp(dot, ".dba")`. Zero (the first dot starts ".dba", any case):
context 14h, `0x472f14(EBP−60h, EBP−0C8h)`, `dts_load(EBP−60h, 0)` (ECX =
EBP−60h), B = [[result+8]] (the first bitmap), `terrain_cells(G, B, the
dword [result+8] with the low byte b)`, [EBP−64h] = B; B ≠ 0: `0x496406(B,
0)` (EDX = B), count += its EAX, context 2Ch, B's slot `+18h`(B, 3)
(EAX = B, EDX = [B], ECX = the count pointer), context 20h;
`0x472f64(EBP−60h, 2)`. Otherwise the text grid: s = 15 − [G+108h],
`_fopen(EBP−0C8h, "r")`, `_fscanf(F, "%d %d %d %d", EBP−9Ch, EBP−98h,
EBP−8Ch, EBP−88h)` (columns, rows, top, bottom; EAX = F, EDX = EBP−98h,
ECX = EBP−9Ch), top and bottom shifted by s (left by −s when s < 0, else
SAR), [G+100h], [G+104h] = columns, rows, the cells by `0x49667c(0, 10h,
2^c × 2^r, 1, 0x4715e1)`, offset = bottom << 3 into G+110h, scale = (top
<< 3 − bottom << 3) SAR 8 into G+118h; per cell i while i < low32(2^c ×
2^r) (signed `IMUL`, reread): `_fscanf(F, "%d", EBP−0A0h)`, the value shifted
by s, height byte = its low byte, mark i = 0 (into `[G+0F0h]`, which this
path does not allocate), byte +0Fh &= 2, then for k = 1 while k < [G+11Dh]
(signed, reread) one draw each until (draw & 0FFFh) < 4CEh (sign-extended
word): byte +0Fh = (byte & 2) | k << 2. `_fclose(F)`, `0x4952e8(G+0D0h,
[G+110h], [G+118h])`, `0x4952fc(G+0D0h)`. Both paths end with
`0x472f64(EBP−30h, 2)`. EAX = its EAX, EDX = the saved head (the epilogue
stores it to FS:0), ECX = its ECX.

**`terrain_cells` 0x46c4c4 (G, B, b)**: columns c = the smallest k with
2^k ≥ the signed word [B+6] (0 for words below 2, at most 15), rows r
likewise from [B+4]; [G+100h], [G+104h] = c, r; n = 2^c × 2^r;
`0x49667c(0, 10h, n, 1, 0x4715e1)` (EAX = 2^r, EDX = ECX = r) into G+0ECh;
`new[](n)` into G+0F0h; [G+110h] = FFh, [G+114h] = 0. Bitmap row y (pixels
`[B+0Eh] + (y << c)`, reread per row) fills grid row 2^r − 1 − y, cells x =
0 .. 2^c − 1 in order: h = pixel − b (low bytes); h > [G+114h] (signed)
stores the maximum; byte +0 = h; `cell_mark_set(G, cell, 0)` (EAX = 2^c
with the low byte h, EDX = h, ECX = c); then m = (8 − [[G+121h]+4] + 0Dh −
[G+108h]) & 31 (the level of material record 0 and the cell shift, reread
per cell) and k = 2^m − 1: when y & k or x & k, byte +0Fh = (byte & 2) |
(byte +0Fh of the cell (x & ~k, y & ~k)) & 0FCh; else byte +0Fh &= 2 and
one draw `rng_next(0x4d2599)` (EAX = EDX = k, ECX = ~k): (draw & 0FFFh) <
4CEh gives byte +0Fh = (byte & 2) | 4 (material 1), else it stays. Exactly
one draw per block whose first cell is in bitmap row and column multiples
of 2^m. Then [G+110h] = 0, [G+114h] ×= [G+118h], `0x4952e8(G+0D0h, 0,
[G+118h])`, `0x4952fc(G+0D0h)`. EDX:EAX = `0x4952fc`'s, ECX = G+0D0h.

**`cell_mark_set` 0x470d74 (G, cell, v)**: byte [[G+0F0h] + ((cell −
[G+0ECh]) >> 4)] = v (unsigned shift, the two reads before the write). EAX
= [G+0F0h], EDX = the index, ECX = the entry ECX with the low byte v.

**Registers after the calls (ECX).** The framework returns the entry ECX;
the originals' ECX is dead at every call site: `zone_load` → the world load
calls `new[](0C8h)` (`0x4774f0` reads no register) and `POP ECX`;
`grid_ctor` → `LEA ECX` at `0x4279f2`; `height_cache_ctor` → returned by
`grid_ctor`, dead there; `terrain_materials` → `MOV ECX,[EAX]` at
`0x427b5f`; `terrain_cells` → `0x496406` (reads its slots only), then `MOV
ECX` at `0x46c88e`, or `LEA ECX` at `0x46c8b2`; `cell_mark_set` → `MOV
ECX,[ESI+121h]` at `0x46c5ed`, in `0x47136c` `MOV ECX` at `0x4713db`/
`0x4713eb` or the return to `base_marks`, whose ECX is dead
([mission_load.md](mission_load.md#17-l5-constructors) section 17). EAX
and EDX are reproduced.

**Frames, residue, shims.** `zone_load` and `terrain_materials` run with
the synthetic EBP of a zeroed scratch (`ES2_ZONE_LOAD_FRAME`,
`ES2_TERRAIN_MATERIALS_FRAME`; `0x49630c` through
`weapons_impact_guest_call`), their locals and stream objects at the
original offsets; the adapters write the saved head back to FS:0
(`zone_load` from ECX, `terrain_materials` from EDX). Uninitialized reads:
the four zone reads and the `_fscanf` targets are read after the call
that should fill them; a short read or a failed conversion would leave the
original's stack residue where the replacement has zeros. The CD's zone
files are 16 bytes and the text grid is never used (below), so no suite
reaches it. The name copy has no bound in the original; the replacement
clamps it at its scratch (504 bytes), a length that already overwrites the
original's frame. No caller reads a register or residue the six leave
(above); no entry or exit shim.

**Dead code.** `0x427a97`..`0x427b39` in `zone_load` (a default grid: shift
14, radius 10, "grid.dat" through `terrain_materials`) has no branch or
reference into it; the byte store `0x46c5dc` in `terrain_cells` follows a
`JMP`. The control-flow walk finds 34 branch sites in the reachable code.

**Compare:** refused for all six (allocation, the stream and file reads,
the runtime record, the RNG stream, game calls that write the grid;
`cell_mark_set` writes one byte at a runtime-indexed address).

### Which path runs, from the data

`0x492bec(name, ext)` builds "ext\name.ext", so `terrain_materials`
receives "dba\zone<N>.dba": its first dot starts ".dba" and the bitmap path
runs in every suite; the text grid needs a name without that extension.
CD members (`ZONES.VOL`, `SIMVOL0.VOL`): every `DAT\ZONE*.DAT` is 16 bytes;
`dat\MAT0.DAT` (also `DATA\MAT0.DAT`, identical) has 13 records, record 0
= {0, 6}. The zones of the suites (script header word 1 of each launch set;
C1_01 as installed from the setup chain's data backup):

| Zone (suites) | DAT shift, scale | DBA | Cells | m | Draws |
|---|---|---|---:|---:|---:|
| 123 (the C1_01 suites, c1_02, c1_06, c1_08) | 14, 256 | 256 × 256 | 65,536 | 1 | 16,384 |
| 22 (c2_02), 39 (c3_09), 3333 (c3_10) | 13, 256 | 128 × 128 | 16,384 | 2 | 1,024 |
| 405 (c4_05), 408 (c4_08, c4_10), 501 (c5_05), 111 (train1) | 14; 130, 256, 90, 200 | 128 × 128 | 16,384 | 1 | 4,096 |

The draws agree with the load-time `rng_next` deltas: exactly 1,024 (c3_09, c3_10), 4,096 (c4_08,
c5_05) and 16,384 (c1_08) in suites whose initial damage draws nothing, and
16,410 / 16,436 in the C1_01 and c1_02 suites (16,384 plus their 26 / 52
damage draws).

### In vitro (group `terrain_load`)

`invitro/terrain_load_cases.h`, quick tier: per hook 100,000 direct cases
against logging stubs for every game callee (28 stub ids; the two slot
`+18h` callees through arena vtables), the original called with six slots,
EAX/EDX/ECX, EBX/ESI/EDI and a stack filled with a case pattern. Compared:
the pair, ECX, FS:0, the ordered calls with argument slots (and the
bridge's slot count), entry registers (stack addresses masked) and the
record's context word, the strings and words passed in the frame by
content (`492bec`, `472f14`, `479c78`, `_strchr`, `_stricmp`, `_fopen`,
the angles), the 24 KiB arena and the image globals (`0x49aac8..0x49aaf9`,
`0x4aaa74..0x4aaaaf`, `0x4a0be0`, `0x4a09f0`, `0x4d1f40`, `0x6cbee4`,
stream A). The stubs change what the originals reread (`[0x49aac8]` after
a read, including a grid after a null `new`; the stream's and the bitmap's
vtables; the count pointer; the origin; the material count and table; the
level, the cell shift, the maximum, the scale; the cell, mark and pixel
arrays); odd cases also take the negative zone, a nonzero stream status,
names without or with other extensions, long names, no bitmap, large and
wrapped text-grid exponents. In even cases (the real callers' inputs) the
adapter is compared too, with the callee counter deltas. 256 installed
31/a7 cases per hook. Real-callee passes (4,096 cases per hook except
`cell_mark_set`, which has no callee): `zone_load` over the real `0x49630c`,
`grid_ctor`, `height_cache_ctor`, `0x49667c` with its element constructors,
`terrain_materials`, `terrain_cells`, `cell_mark_set`, `euler_matrix`,
`rng_next` (stream A), `0x4952e8`, `0x4952fc` (seven nested hooks in all
original/replaced combinations); `terrain_materials` over the real
`terrain_cells`, `cell_mark_set`, `rng_next`, `0x49667c`, `0x4952e8`,
`0x4952fc` (both paths); `terrain_cells` over the real `cell_mark_set`,
`rng_next`, `0x49667c`, `0x4952e8`, `0x4952fc`; `grid_ctor` over the real
`height_cache_ctor` and `0x49667c`; `height_cache_ctor` over the real
`0x49667c`; new and new[] a bump allocator, the file, string and path
functions stubs (their imports are traps in the harness); the hook under
test original or replaced. 34 branch sites, 23 both ways, 11 with a PROVED
edge: in `terrain_cells` the row and block-row index checks
`0x46c57a`/`0x46c591`/`0x46c643`/`0x46c65a` (exponents ≤ 15 keep the index
in range) and the draw loop's back edge `0x46c6d1` (one draw per anchor);
in `terrain_materials`' text grid the cell index checks
`0x46ca4c`/`0x46ca63`/`0x46ca88`/`0x46ca9f`/`0x46cae0`/`0x46caf7` (the
index counts up from 0 below a bound that equals the limit whenever the
loop runs). One caught mutation per hook (`MUTATIONS.md`). Core unit test
`//core:terrain_load_test` (216 checks worked out by hand: a 4 × 2 bitmap
with its draws, flags and heights, a one-cell grid, the bitmap path, the
text grid with three materials, the zone load's sequence, a negative zone,
a nonzero status with a null grid, the cache and grid constructors, the
mark with a wrapped index).

## Base flattening and the base-mark raster

The flattening, its floods, the base-mark raster and the mark read are own
code (hooks, group `base_flatten`, core in `core/src/terrain_load.c`, the
same grid object and bridge as the zone load). Validated in vitro; live
identical (load-side view in
[mission_load.md](mission_load.md#27-base-flattening-and-the-base-mark-raster)).

| Hook | VA | Instr. | Role | Callers |
|---|---|---:|---|---|
| `base_flatten` | `0x47129c` | 79 | after the actor load: every marked cell region gets the mean of its heights; then the shading pass | the actor load `0x4253d8` (`0x4263d9`) |
| `flood_sum` | `0x470fe8` | 119 | recursive sum of the heights of one region into `0x6b5470`, count `0x6b5474`, visit bit 1 | `0x47129c` (`0x471325`), itself (8 sites) |
| `flood_fill` | `0x471128` | 146 | recursive store of the mean into the region and its write-through neighbours, visit bit 2 | `0x47129c` (`0x47133f`), itself (8 sites) |
| `mark_raster` | `0x47136c` | 108 | the base layout's mark bytes into the cells around a building (material bits, `cell_mark_set`) | `base_marks` `0x405db0` (`0x405de9`) |
| `cell_flag_get` | `0x470e00` | 11 | one mark byte by (x, y) | `0x470fe8` (`0x47106d`), `0x471128` (`0x4711ad`), `0x47129c` (`0x471306`), the H3 cell-object draw `0x46e5a4` (`0x46e5fd`, per frame) |

A byte scan of the code section for `E8`/`E9` rel32 finds exactly these
call sites, and no dword of the file holds any of the five addresses.
Their game callees: `cell_flag_set`
`0x470ddc` (hook, group `group_joins`,
[mission_load.md](mission_load.md#23-group-joins-and-the-building-footprint)
section 23), `cell_mark_set` `0x470d74` ([above](#the-terrain-zone-load)),
the shading pass `0x46c2e0` (H3;
[mission_load.md](mission_load.md#34-the-roots-last-callees-the-terrain-shading-pass)
section 34) and the five among themselves; all stay calls through the guest
bridge with their own modes.

### Contracts of the flattening

All five are cdecl; EDX:EAX is reproduced exactly; every game call gets the
original's EAX/EDX/ECX (the core follows them step by step). n = `[G+100h]`
(log2 columns), m = `[G+104h]` (log2 rows); every shift counts CL's low
five bits. **The cell of index i** (computed inline in four of the five,
`[G+100h]` reread each time): i < 0 (signed) gives 0 with ECX = n; else ECX
= n + m (both reread), and i < 1 << (n + m) (signed) gives `[G+0ECh]` + 16
i, otherwise 0.

**`cell_flag_get` 0x470e00 (G, x, y)**: ECX = n, EDX = (y << n) + x, EAX =
the zero-extended byte `[[G+0F0h] + EDX]` (n read before the mark
pointer). 32 bytes, long enough for a hook (prologue `55 8b ec 8b 45 08`
relocated), so a hook and not computed inside its callers.

**`flood_sum` 0x470fe8 (G, x, y)**, frame `add esp,-0Ch` (the cell and two
copies of x, y; no callee sees them):

1. EAX = 1 << n (ECX = n); x ≤ −1, 1 << n ≤ x, y ≤ −1 or 1 << n ≤ y
   (signed): return, EDX = the entry EDX.
2. The cell C of i = (y << n) + x (above); `cell_flag_get(G, x, y)` with
   EAX = i, EDX = C, ECX as the cell computation left it. Mark bit 0 clear
   or bit 1 set: return with its registers.
3. `cell_flag_set(G, x, y, mark | 2)` (EAX = mark | 2, EDX/ECX
   `cell_flag_get`'s); `[6B5474h]` += 1; ECX = the height byte `[C]`;
   `[6B5470h]` += ECX (in that order; C is the pointer of step 2 even if a
   callee changed `[G+0ECh]`).
4. The eight neighbours, each a recursive call: (x−1, y+1), (x, y+1),
   (x+1, y+1), (x−1, y), (x+1, y), (x−1, y−1), (x, y−1), (x+1, y−1). The
   registers each call site sets (the rest are the previous call's): EAX =
   y+1, EDX = x−1 (ECX = the height byte); ECX = y+1; EAX = y+1, EDX = x+1;
   ECX = x−1; EAX = x+1; EDX = y−1, ECX = x−1; EAX = y−1; none. EDX:EAX
   (and ECX) = the last neighbour's.

**`flood_fill` 0x471128 (G, x, y, h, w)**: the same frame and steps with
bit 2. After `cell_flag_get`, when the dword w is not 0, the height byte
`[C]` = h (low byte; CL = h, EDX = C), before the mark test. A visited
cell gets `cell_flag_set(G, x, y, mark | 4)`, then ECX = C, AL = h, `[C]`
= h. The neighbours in the same order with w = 0, 1, 1, 0, 1, 0, 0, 0 and
the registers EAX = h, EDX = y+1, ECX = x−1; EAX = h, EDX = y+1; EAX = y+1,
EDX = x+1, ECX = h; EAX = x−1, ECX = h; EDX = h, ECX = x+1; EAX = h, EDX =
y−1, ECX = x−1; EAX = h, EDX = y−1; ECX = h. So the fill writes the mean
into every visited cell and, through w = 1, into the existing cells (x,
y+1), (x+1, y+1) and (x+1, y) of every visited cell, marked or not: one
row and one column beyond the region (a cell's height byte is its corner
vertex).

**`base_flatten` 0x47129c (G)**, frame `add esp,-8` (S and a row pointer
that reaches no callee): S = 1 << n once ([EBP−4]); for y = 0 .. S−1 the
row's cell (EDX = y, EAX = y << n, n reread; the pointer itself is dead),
then for x = 0 .. S−1 (signed loops): `cell_flag_get(G, x, y)` with the
registers of the previous step. Mark bit 0 set and bit 1 clear starts a
region: EAX = 0, `[6B5470h]` = `[6B5474h]` = 0, `flood_sum(G, x, y)`, EAX
= `[6B5470h]`, CDQ, IDIV `[6B5474h]` (q; the remainder in EDX),
`flood_fill(G, x, y, q, 0)` (ECX = `flood_sum`'s). Afterwards
`0x46c2e0(G)` with the registers of the last step; EDX:EAX = its, ECX = S
(the last POP ECX takes [EBP−4]). Both loops run over the columns'
exponent n, also for the rows (the CD's zones are square,
[Which path runs](#which-path-runs-from-the-data)). The count of a region
is at least 1 (its first cell is marked and unvisited); a zero count or
80000000h / −1 faults at `0x471333` (#DE): the core stops there with the
operands and the adapter executes the IDIV.

**`mark_raster` 0x47136c (G, P, k, rows, count)**, frame `add esp,-20h`
(all locals reach no callee): reads `[G+121h]` (the material table, an
unaligned dword), s = `[G+108h]` − 13, t = `[[G+121h] + 8k + 4]` (the level
of material record k), x0 = `[P]`; bx = (x0 SAR (21−t)) << (8−t−s); by =
(((`[P+4]` SAR (21−t)) + 1) << (8−t−s)) − 1. EAX = rows, EDX = 0, ECX =
8−t−s; count ≤ 0 (signed): return. Row j = 0 .. count−1 (the row pointer
`[rows + 4j]` read per row), column i = 0 .. count−1: the cell of (bx + (i
SAR s), by − (j SAR s)) (EAX = the cell); when the word `[4D2680h]` (option
`-l`, low memory) is 0: ECX = k, DL = byte `[cell+0Fh]`, ECX <<= 2, byte
`[cell+0Fh]` = (DL & 2) | CL (the material bits 2..7 become k, bit 0 is
cleared, bit 1 kept); when the row byte is not 0: `cell_mark_set(G, cell,
1)`. EAX = j after each row (count at the end); EDX/ECX those of the last
cell. With `-l` clear, a cell outside the grid (the null cell) faults at
`[0Fh]`; the game's layouts lie inside the grid (live). The
material byte is presentation data that `terrain_height` also reads (bits
0..1); it is written exactly as the original does.

### The recursion

The floods recurse once per visited cell: 44 bytes of the game stack per
level for the sum (12 argument bytes, return, EBP, 12 locals, EBX/ESI/EDI)
and 52 for the fill. The replacement does not recurse: the core runs the
recursion as a loop with an explicit stack of one byte per level (the
direction from the parent; the parent's x, y are the child's minus that
direction, exact under 32-bit wrap-around) in a static buffer of 65,536
entries per flood in the adapter. Every `cell_flag_get`/`cell_flag_set`
call, every height read and write and the sum/count updates come in the
original's order with the original's entry registers; a frame's result is
its last neighbour's, which is the last call completed when the frame
ends. The game stack only holds the adapter's frame and one bridge call at
a time. Depth: a path visits each cell at most once, so it is shorter than
the zone's cell count (65,536 for zone 123, 16,384 otherwise), within the
buffer. In the suites whose map count is not capped, the entries of
`0x470fe8` are T + 8V (T regions, V visited cells, each calling eight
neighbours): 82, 423, 741, 563, 66, 277, 276, 372, 130, so V ≤ 92 and the
original's deepest path is at most 93 levels (4.8 KB of stack); in the
C1_01, c1_02 and c1_06 suites the probe saturates (1,000) and the depth is
not known, but the original survives it. A path longer than the buffer
(more cells than the largest zone has; the original's 1 MiB stack ends
near 23,831 levels) makes the adapter raise `EXCEPTION_STACK_OVERFLOW`
(`es2_machine_stack_overflow`: the exception in the proxy, the end with 71 in
es2sim), as the original would fault. The hook counters count the top-level entries
only: `hk_flood_sum_calls` = T, and T + 8 × `hk_flood_sum_call_470ddc` =
the map's entries of `0x470fe8` (the same for `0x471128`).

**Callers' registers.** The framework returns the entry ECX; the
originals' ECX is dead at every call site. `cell_flag_get` → in `0x470fe8`
the mark test, then `cell_flag_set` (reads no register) and `XOR ECX,ECX`,
or the return to a caller that writes ECX (`LEA ECX`) or calls
`0x470fe8`/`0x471128` (both load ECX from `[G+100h]` before any read); in
`0x471128` `MOV CL,[EBP+14h]` replaces CL only and the upper bits reach
only the same dead returns; in `0x47129c` the next `cell_flag_get`, `MOV
ECX` at `0x4712c5`, or `0x46c2e0`, whose `PUSH ECX` stores into its local
[EBP−4], overwritten at `0x46c2f4` before any read; in `0x46e5a4` `POP ECX`
or the cell callback `[G+125h]` = `0x428c60`, which writes ECX at
`0x428c75` before reading it. `flood_sum`, `flood_fill` → the dead returns
above (`0x47129c` continues with CDQ/IDIV and `flood_fill`, resp. the
loop). `base_flatten` → `POP ECX` at `0x4263de` (EDX cleared next, EAX
reloaded at `0x42643f`). `mark_raster` → `base_marks`, whose ECX is dead
(mission_load.md section 17); after `cell_mark_set` inside it `MOV ECX` at
`0x4713eb`/`0x4713db` or the return. EAX and EDX are reproduced. No
function reads a local it did not write. The replaced flattening leaves
other bytes below the actor load's frame than the original's recursion;
the actor load's later callees (`terrain_height`, `subject_resolve`,
`0x47ad78`) and `0x44f6f4` read no such residue (the residue scan of
[mission_load.md](mission_load.md#21-l6-actor-load) section 21). No entry
or exit shim.

**Compare:** `cell_flag_get` accepts it (a pure read, no game calls; no
spans or regions, the pair is compared; installed compare probes in
vitro). The other four refuse it: game calls through the bridge that
write the marks and heights of whole regions (runtime-indexed),
`base_flatten` also the shading pass.

### In vitro (group `base_flatten`)

`invitro/base_flatten_cases.h`, quick tier: per hook 100,000 direct cases
against logging stubs for every game callee (`cell_flag_get`,
`cell_flag_set`, `flood_sum`, `flood_fill`, `0x46c2e0`, `cell_mark_set`
patched in the private image), the original called with five slots and
EAX/EDX/ECX over a stack filled with a case pattern. The floods' own
recursion stays original code in the original run, so the stubs play the
marks for real (`0x470e00` answers the mark byte of an existing cell, 0
outside the grid; `0x470ddc` stores), which ends the recursion as in the
game; grids of 1 × 1 up to 32 × 32 cells (2^n × 2^m, n + m ≤ 10; the sum
also with fewer rows than columns), mark densities 1/8 to 7/8, preset visit
bits. Compared: the pair, ECX, the ordered calls with argument slots (and
the bridge's slot count) and entry registers, the 64 KiB arena (grid, two
cell arrays, marks, material records, rows) and the globals `0x6b5470`,
`0x6b5474`, `0x4d2680`. Odd cases: the stubs change heights, the cell array
pointer, the sum or count, add marks and visit bits, add noise above the
mark byte (it reaches `cell_flag_set`'s argument); for the scan the shift
words (negative and out-of-range row cells) and the cell pointer; for the
raster `[G+100h]`, `[G+104h]`, `[G+0ECh]`, row bytes ahead and the
read-once fields (`[G+108h]`, `[G+121h]`, the level), with `-l` set; wild
shifts give the floods' negative and out-of-range cell indices (the stub
answering "unmarked"). In even cases (game-like inputs) the adapter is
compared too, with the callee counter deltas. `base_flatten`'s IDIV faults:
32 cases with a zero count or 80000000h / −1, the original faulting at
`0x471333`, the core stopping with the operands after the same calls and
stores, the adapter faulting itself. 256 installed 31/a7 cases per hook.
Real-callee passes (4,096 cases each): the floods over the real
`cell_flag_get` and `cell_flag_set`; `base_flatten` over the real
`cell_flag_get`, `flood_sum`, `flood_fill` and `cell_flag_set` (`0x46c2e0`
a stub); `mark_raster` over the real `cell_mark_set`; the nested hooks in
all original/replaced combinations, the hook under test original or
replaced. `cell_flag_get`: 512 installed compare cases (exact 0 mismatches,
skewed 256). 33 branch sites, 31 both ways, 2 PROVED (the column loops'
first tests `0x471301` and `0x4713e5` follow the row loops' tests of the
same bound). One caught mutation per hook and five ad-hoc register-flow
mutants caught (MUTATIONS.md). Core unit test `//core:terrain_load_test`
(cases worked out by hand on a 4 × 4 grid: a three-cell region's sum with
its 28 calls in order, the fill's write-through cells, a one-cell grid's
EDX, starts outside the grid and on a visited cell, the path buffer's
overflow; the flattening of two regions end to end with the floods as
callees, the zero-count fault and n = 31; the raster on an 8 × 8 grid with
and without `-l`, a zero count and a cell outside the grid).

## The terrain pass in the frame

The terrain and scene modules in the order the render root calls them; the
frame overview, the coupling check and the module split are in
[rasterizer.md](rasterizer.md#the-renderer-line-r3d) (whose render-root step
numbers 6, 8, 12, 14, 15 are used below).

1. **Draw, not simulation.** The chain hangs under the rendering root
   `0x45fca8` (and the MFD camera view `0x43fe44`, never live); no path
   from the logic frame `0x45f570` or the camera `0x4011a0` reaches it.
   `0x46d448` draws one terrain cell (one quad or two triangles, textured
   or flat) and then the objects binned into that cell; the callbacks
   `0x46dddc`/`0x46dfa8` run it along one row or one column of the walk.
2. **No compared state changes.** Everything the cell draw subtree writes
   is draw-private: static vertex arrays, the shade record, raster and
   haze state, the per-frame height cache and the per-frame cell marks.
   No simulation reader of these is live in the suites
   ([table](#what-the-terrain-pass-writes-and-who-reads-it)). The direct
   reach (81 functions from `0x46d448`; 161 from the cell-object callback
   `0x428c60` and the actor draw slots) has no RNG, clock, frame-counter or
   sound reference. One idempotent cache fill reaches actor memory (keep
   list item 3), and the binning writes two cell words into actors.
3. **The marks never carry over.** The binning clears every cell mark
   first, so no cell keeps a mark from one frame to the next.
4. **Objects reach the screen in three places:** with their cell, right
   after that cell's terrain (`0x46e5a4` → `0x428c60` → the sort tree);
   the objects outside every cell from the far list in `0x42883c`; and,
   for a flyer chassis, the player's own cell once more in `0x42883c`.

Method: writes and their readers were found by scanning every absolute
operand of a full listing and attributing it to the map's function
entries; reach is a direct-call walk with watch addresses (RNG streams
`0x4d2599`/`0x4d260b`, clock `0x467828`, sound API, frame counter
`0x4d257b`).

### Call chain of the terrain pass

| Step | Function | Instr. | Calls per suite | What it does here |
|---|---|---:|---|---|
| 1 | `0x45fca8` render root | 128 | 1000×5/937 | calls in this order `0x46cb9c` (`0x45fd41`), `0x42841c` (`0x45fd55`), `0x46cb9c` again (`0x45fd8f`, after the second camera set-up), `0x42e700` (`0x45fda3`), `0x42883c` (`0x45fdb3`) |
| 2 | `0x46cb9c` | 398 | 1000×6 | eye position `0x6b4f44..0x6b4f4c`, view rectangle (`view_rect`), reset of the height cache of the grid G (`0x4952e8`/`0x4952fc` at `0x46ce77`/`0x46ce86` and `0x46d18f`/`0x46d198`) |
| 3 | `0x42841c` | 288 | 1000×5/937 | bins objects into cells: through `0x4283b4` and `0x42837c` → `0x46e62c`, then `0x46e8c4` → `0x470db8` increments the cell mark byte `[[G+0xf0] + cell]` |
| 4 | `0x42e700` → `0x428140` → `0x46d1a8(G, camera)` | 38, 26, 126 | 1000×5/937 | G = `[0x49aac8]`, the same object as the simulation's terrain grid `[0x4a0be0]` (`0x42789c` copies it at `0x427b7d`/`0x427b82`); eye cell `0x6b4f50`/`0x6b4f54` = camera `+4`/`+8` >> `[G+0x108]`; eye-cell flag `0x6b4f5c` = its mark (`0x470d94`); callback `0x46dfa8` with walker `0x472504` when the camera word `+0x14` (heading, hypothesis) has 0x2000 < \|h\| < 0x6000, else `0x46dddc` with `0x471f44`; afterwards, if the flag is still set, `0x46e5a4` for the eye cell |
| 5 | `0x46dddc` / `0x46dfa8` | 156 / 158 | 1000×6 | per span of cells ([callbacks](#the-line-callbacks-0x46dddc-0x46dfa8)) |
| 6 | `0x46d448` | 684 | 1000×6 | per cell ([cell draw](#the-cell-draw-0x46d448)) |
| 7 | `0x46e5a4(G, x, y)` | 56 | 1000×6 | if the cell's mark ≠ 0 (`0x470e00`): `[G+0x125](x, y, mark)` = `0x428c60` (installed at `0x461982`), then mark ← 0 (`0x470ddc`) |
| 8 | `0x428c60` | 132 | 1000×6 | draws the cell bucket `0x4aab00` + 1000 · `0x4281b0(x, y)`: camera cull `0x401078`, sort records at `[0x4cf884]`, actor slots `+0x04`, `+0x10`, `+0x24`, `+0x00` (draw), `0x429620` |

`0x46cb9c` runs twice per rendered frame (both calls with the third
argument 0; the map counts it 1000×6 against the root's 1000×5/937) and
otherwise only from `0x43fe44`. `0x42883c`, the last call of the root,
calls `0x46e5a4` for the player's cell
([Far and eye-cell objects](#far-and-eye-cell-objects-0x42883c)).

### Terrain visibility `0x46cb9c`

`0x46cb9c(G, cam, Q)` (render-root steps 6 and 12) computes, for the
current camera, the eye, the far distance, the per-kind draw distances, the
view footprint on the grid and the view-space cell basis; it draws nothing.
Step 6 runs before the binning, which needs the eye; step 12 repeats it
after the second camera set-up.

1. Eye `0x6b4f44..0x6b4f4c` ← cam `+4..+0xc` (`0x46cbbf` ff.; stored in the
   order +0xc, +4, +8).
2. Far distance `G+0x10c` ← signed word `0x4a0bb4[byte 0x4d1f3f]` (detail
   byte), shifted right (SAR) by `[G+0x108]` − 14 when `[G+0x108]` > 14
   (signed).
3. `0x428bc0([G+0x10c] << [G+0x108])`: the five draw distances
   `0x4cf988[k]` = `multiply_q10(far, word 0x49abb0[k])` (read by
   `0x428c08`).
4. `0x48c444(0)` (view transform of the current camera).
5. Q ≠ 0 (only the MFD camera view): `G+0xcc`/`G+0xc8` ← Q's points/count,
   each point clamped (signed) to 0 .. ((1 << w) − 2) << shift and 0 ..
   ((1 << h) − 2) << shift; visible = `[pts] < [pts+0x10]` and `[pts+4] <
   [pts+0xc]` (signed). **Q = 0 (both render-root calls):** the view
   footprint from the frustum record (`0x4949a4`, `0x4949e4`; a work record
   and two stack vectors, `0x49667c` with ctor `0x4953c4`, 4 and 20 points
   of 12 bytes), `view_rect` (hook), the clip `0x494e58(rec, rect,
   [G+0x110], [G+0x114], &count)`, whose AL is the visible byte; when
   visible `G+0xcc` ← `G+0x28`, `G+0xc8` ← the point count. Both paths:
   `G+0x11c` ← visible, EAX ← visible; when visible the points (x and y of
   12-byte points) SAR `[G+0x108]` (cell units), then the basis.
6. The view-space cell basis: `transform_point(0, 0x6c5fb4, G+0x1c)`, the
   view matrix copied (10 words) twice for `rotate_point((1 << shift, 0,
   0), copy, G+4)` and `rotate_point((0, 1 << shift, 0), copy, G+0x10)`,
   then the height-cache reset `0x4952e8(G+0xd0, [G+0x110], [G+0x118])`,
   `0x4952fc(G+0xd0)`, whose EAX is returned. Invisible: EAX = 0.

Inputs: G (`+0x100`, `+0x104`, `+0x108`, `+0x110..+0x118`), cam, the view
transform globals. No heap allocation (its `0x49667c` calls construct stack
arrays). Hooks it calls: `view_rect`, `transform_point`, `rotate_point`,
`multiply_q10`, `matrix_compose`, `matrix_transpose`, `euler_matrix`,
`dist3d_forward`, `line_plane`.

### Object binning `0x42841c`

`0x42841c()` (render-root step 8): `0x4cf874` ← 0 (bucket count); the cell
marks cleared by `0x46e944` (`memset([G+0xf0], 0,
2^([G+0x100]+[G+0x104]))`, called at `0x42842f`); then, in this order, each
pool walked with `pool_prev` (hook). Pool names from
[main_loop.md](main_loop.md#object-removal-flush-478644-pool-free-471bc8-append-478628)
and mission_load.md section 4.

| # | Pool | Condition | Size argument | Bin |
|---:|---|---|---|---|
| 1 | flat objects `[0x4a968d]` (type word 9; `flush_attached`) | `dist_gate(obj+0x26, cam+4)` < 30,000 | slot `+0x10` | `0x4282d8` |
| 2 | buildings `[0x4a95a0]` (then `0x4cf92c` ← 0) | group dword `[[obj+0x45]+0x14]` = 0 (no pending activation) | slot `+0x5c` | `0x42837c`; `+0x1e8`/`+0x1ea` ← the record's column/row, or −1 |
| 3 | MECH `[0x4a9b7a]` | the same group test | slot `+0x5c` | linked object `[obj+0x2b0]` ≠ 0: `0x4283b4` into the link's cell (`link+0x1e8`); else `0x42837c`; `+0x1e8`/`+0x1ea` as above |
| 4 | FLYER `[0x4a9db9]` | the same group test | slot `+0x5c` | `0x42837c`; `+0x1e8`/`+0x1ea` |
| 5 | projectiles `[0x4a96c2]` | — | slot `+0x10` | `0x4282d8` |
| 6 | explosions `[0x4a961e]` | `0x408240` skips only an explosion whose parent `+0x57` is the object the camera is attached to; the type bytes `+0x41` = 2 and `0xb..0xe` are never skipped | — | parent ≠ 0: `0x4283b4` into the parent's cell (`0x408228`); else `0x4282d8` |
| 7 | debris `[0x4a9642]` | — | slot `+0x10` | `0x4282d8` |
| 8 | smoke balls `[0x4a966e]` | — | slot `+0x10` | `0x4282d8` |
| 9 | fires `[0x6b4f30]` | — | — | `0x4283b4` into the parent's cell (`[obj+0x4a]+0x1e8`, `0x46b850`) |
| 10 | meteors `[0x4a96aa]` | — | slot `+0x10` | `0x4282d8` |

The explosion test is
[rasterizer.md "The explosion tests"](rasterizer.md#the-explosion-tests).
The smoke emitters `[0x4a9684]` are not binned. `0x4282d8(obj)` =
`0x42837c(obj, slot +0x10)`; `0x42837c(obj, size)` = `0x46e62c(G, slot +4
position, size)` → a cell record or 0, then `0x4282f8`; `0x4283b4(obj,
rec)` = `0x46e8c4(G, rec)`, then `0x4282f8`. `0x46e8c4` returns the static
record `0x6b5058` {column byte, row byte, count byte} after incrementing
the cell's mark (`0x470db8`), or 0 outside the grid. `0x46e62c`'s rule for
an object near a cell edge and the grid leaves are in
[rasterizer.md "Renderer leftovers"](rasterizer.md#renderer-leftovers).
`0x4282f8(obj, rec)`: rec = 0 → far list `0x4cf88c[[0x4cf92c]++]`; else
bucket index `0x4281b0(column, row)` (a linear search of the 6-byte table
`0x4cf4f0` {column, row, index}, count `0x4cf874`; a new cell takes the
next index) and `0x4aab00 + 1000·index + 4·(count − 1)` ← obj. Limits,
each with the fatal report `0x479c78`: 150 cells with objects
(`0x4281e4`), a mark count of `0xfa` or more (`0x42830a`); both assert and
then store anyway ([as own code](#binning-the-cell-callback-and-the-sort-tree-as-own-code)).

The cell words: for every building, MECH and FLYER it bins, the binning
writes the column and row of its cell record at `+0x1e8`/`+0x1ea`, or
`0xffff`/`0xffff` when the object lies in no cell. Readers (listing scan
for `+0x1e8`/`+0x1ea`, grep of `core/`): only the renderer (`0x42883c` for
the player, `0x408228` and `0x46b850` for explosions and fires attached to
a parent, the MECH link `+0x2b0`). Not compared.

Heap: none in the frame. The buckets `0x4aab00` are BSS (cleared once,
`memset` of `0x249f0` bytes); the sort records `[0x4cf884]` (`0x200` ×
`0x36` bytes) and the arrays `[0x4cf878]`, `[0x4cf87c]`, `[0x4cf888]` are
allocated once by the stage-2 callback `0x428230`, which the static
initializer `0x428de8` registers.

### The cell walk

`0x428140(cam)` (inside `0x42e700`, render-root step 14):
`0x48c444(0x4aaa84)`; when the context `[0x6c5f70]` has `+4` = 1 and
`[[ctx+8]]` = 0 it sets `ctx+0x208` ← 1 for the pass (read by the GL
polygon fill `0x483eb8`; role not traced) and restores it after
`0x46d1a8([0x49aac8], cam)`.

`0x46d1a8(G, cam)`: `G+0x11c` = 0 → nothing (EAX the entry EAX). Else
`0x48c444(0)`; `[ctx+0x22c]` (ctx `[0x6c5f70]`) saved and pointed at a
frame dword; eye cell `0x6b4f50/54` ← cam `+4`/`+8` SAR shift; flag
`0x6b4f5c` ← the eye cell's mark (`0x470d94`; 0 outside the grid); heading
word cam `+0x14`: 0x8000 → 0x7fff, negative → negated; 0x2000 < h < 0x6000
(signed) → column order with `0x46dfa8` and the walker `0x472504`, else row
order with `0x46dddc` and `0x471f44` (record R at −0x38: `+0x14` the
callback, `+0x18` G, as `0x471f30` writes them); afterwards `0x46e5a4` for
the eye cell when the flag is still set; `[ctx+0x22c]` restored, EAX ← the
saved value. The callback is called through `[ESI+14h]` (e.g. `0x4720dd`);
R holds `+0xc`/`+0x10` the column/row step ±1.

**The walkers** `0x471f44` (rows) and `0x472504` (columns), (R, eye, poly):
both build, in a frame at the original's offsets, three 20-point buffers
(`0x49667c`, 8-byte elements, 60 in all, ctor `0x49545b`), the polygons
{count, points} −0x10 (one half), −0x8 and −0x18 (its two quadrants) and two
span headers {min, max, count, spans}. `R+0/+4` ← the eye cell. The clips
are `poly_clip(keep, axis, line, in, out)`: the half beyond the eye `(1, 0,
eye.y + 1)` with the quadrants `(1, 1, eye.x + 1)` (−0x8) and `(0, 1,
eye.x)` (−0x18); the half before it `(0, 0, eye.y)` with `(0, 1, eye.x −
1)` and `(1, 1, eye.x)`. Spans: `poly_spans(poly, buffer, header)`.

- **Main order** (`[0x49f248]` = 0 and `[0x49aad4]` ≠ 0): row step ← −1,
  the far half, rows from the larger max down to the smaller min; per row
  the quadrant −0x8 (R quadrant 0, column step −1, its spans from the last
  backwards, b first) then −0x18 (quadrant 1, step 1); then row step ← 1,
  the near half, rows upwards, −0x8 (quadrant 2, step 1) then −0x18
  (quadrant 3, step −1). A quadrant takes part only when its count is
  nonzero and the row lies in its span range.
- **Simple order** (otherwise, row walker only): the four quadrants one
  after the other through −0x8 alone, as (quadrant, row step, column step)
  (2, −1, −1), (3, −1, 1), (0, 1, 1), (1, 1, −1).
- **Column walker**: the polygon's points turned a quarter about the eye
  (`0x4724b8`), the main order with the roles exchanged (column step ← 1
  first; the callback gets the turned row back as a column and the span
  ends as rows), the points turned back (`0x4724e0`) at the end.

`0x471f30(R, cb, G)`: `R+0x14` ← cb, `R+0x18` ← G. `0x4724b8(px, py, c)`:
x′ = y − c.y + c.x, then y′ = c.x − x + c.y; `0x4724e0`: x′ = c.y − y +
c.x, then y′ = x − c.x + c.y. `0x47297c` (after the column walker) is not
referenced.

### The line callbacks `0x46dddc`, `0x46dfa8`

cdecl `(R, line, a, b)`, called by the walkers through `[esi+0x14]`; R =
the walker record (`+0xc`/`+0x10` column/row step ±1, `+0x18` = G, set by
`0x471f30`). From G's view-space cell basis (column step at `G+0x4`, row
step at `G+0x10`, origin at `G+0x1c`, three dwords each) they build the
four corners (x, y), (x, y+1), (x+1, y+1), (x+1, y) of each cell in the
static array `0x6b4fd4` (four 12-byte points) and call `0x46d448(x, y, G,
0x6b4fd4)` for x (`0x46dddc`, line = row y, step `R+0xc`) or y
(`0x46dfa8`, line = column x, step `R+0x10`) from a to b, **b inclusive**
(the loop runs until x = b + step), in the record's step direction.
Afterwards they clear the eye-cell flag `0x6b4f5c` when the eye cell lay in
the span. **Quirk:** in `0x46dfa8` the second bound test compares the eye
**column** `[0x6b4f50]` with the row bounds (`0x46e146` and `0x46e165`; the
first tests and `0x46dddc` use the right coordinate): the flag test uses
the eye's row against a and its column against the end (in
vitro), so the flag can be cleared or kept wrongly. Then `0x46d1a8` calls
`0x46e5a4` for the eye cell once more (harmless: its mark is already 0) or
not at all. Presentation only. 10 conditional sites each.

### The cell draw `0x46d448`

`0x46d448(x, y, G, Q)`: cdecl, four slots, locals `0xb0`; no entry register
is read; EAX/EDX at the return are those of `0x46e5a4`, which both
callbacks ignore. 684 instructions, 40 conditional sites, no x87, no
indirect jump.

1. Corner cells c0..c3 = (x, y), (x, y+1), (x+1, y+1), (x+1, y) of the
   16-byte cell array `[G+0xec]` (index `(y << [G+0x100]) + x`; pointer 0
   outside `0 .. 2^([G+0x100]+[G+0x104]) − 1`). A zero pointer is
   dereferenced without a test (the height reads at `0x46d56a`,
   `0x46d594`, `0x46d5cb`, `0x46d603` for corners 0..3); the walk keeps the
   cells inside.
2. Heights: per corner `0x49534c(G+0xd0, height byte)` returns the cached
   view-space vector of `byte · [G+0x118] + [G+0x110]` (computed once per
   frame and height value with `47e08d` against the rotated up vector of
   `0x4952fc`); it is **added into Q** (per corner in the order +8, +0,
   +4), so the static corners become the cell's view-space vertices.
3. Depth d = minimum of the corners' second coordinates Q+4, Q+0x10,
   Q+0x1c, Q+0x28 after the heights were added. Diagonal from the third
   coordinates, s = (z0 + z2) − (z1 + z3): s = 0 → one quad (`0x6b5004`,
   four points); s > 0 → triangles (0, 1, 3) and (1, 2, 3); s < 0 → (0, 1,
   2) and (0, 2, 3) (second triangle at `0x6b5034`).
4. Texture coordinates `cell_uv([G+0x121] + 8 · (c0 byte +0xf >> 2), x,
   y, 0x6b4f7c)` (hook), copied in polygon order to `0x6b4f9c`/`0x6b4fbc`
   when `[0x4aaaa8]` = 1 (textured terrain; `[0x4aaaa8]` = byte
   `[0x4d1f40]`, set by `0x42789c`).
5. `0x4680e0([G+0x10c] << [G+0x108])` sets the far distance `[0x4a08ac]`,
   `0x4680f0(d)` the haze remap `[0x4a08b0]`; `[0x49f250]` ← (d > 25000),
   read only by `0x468760` (`0x468da1`, `0x468fd5`).
6. First polygon if front-facing: `0x46be9c(c0 + 1, world corner of c0)`
   = dot(cell normal words `+1..+6`, eye − corner) > 0. The fill mode
   `[0x4aaaa8]` decides:
   - 0 (untextured): the half's first shade word ← cell byte `+0xd` when
     the shade mode `[0x4aaaac]` is below 2 (unsigned), then
     `0x46bc44(polygon, n, &word)` (never live);
   - 1 (textured): the first word ← cell byte `+0xd` when the shade mode
     is 1; the four frame words copied zero-extended to the shade record
     `0x6b4f6c`; then `0x468760` (the 3D raster with the perspective
     helpers, [rasterizer.md](rasterizer.md)) when 2 · `[0x49aad0]` > d
     (`[0x49aad0]` = 20000 from `0x42789c`), else `0x46817c(polygon, uv,
     n, texture, 0x6b4f6c, shade mode, 1, 0)`;
   - any other value: no polygon (the far, haze and event calls still
     run).

   The three unwritten shade words are frame residue (frame words
   `EBP−0x4c..` for the first polygon, `EBP−0x54..` for the second
   triangle; residue row in `sim/src/residue.c`).
7. Second triangle, when split: normal words `+7..+0xc`, shade byte
   `+0xe`, the same way. Its reference point for the facing test
   (`0x46da57..0x46db4e`) is the corner (x + 1, y + 1) of c2 **with the
   grid origin `G+0xf4`/`+0xf8` added twice**; harmless while the zone load
   sets the origin to 0.
8. `0x46e5a4(G, x, y)`: the objects of the cell (call chain step 7).

The dword at `EBP−0x34` (`[0x49aacc]`) is stored and never read.

### Callees of the cell draw

| Callee | Instr. | Calls per suite | Role | Writes |
|---|---:|---|---|---|
| `0x49534c` | 52 | 1000×6 | height-vector cache (only caller `0x46d448`) | entries and valid bytes of the cache object at `G+0xd0` |
| `0x46bdf8` `cell_uv` | | hook | texture coordinates | `0x6b4f7c..0x6b4f9b` |
| `0x4680e0`, `0x4680f0` | 6, 35 | 1000×6 | far distance, haze remap (`0x4680f0` also from `0x42876c`, `0x42fa18`) | `0x4a08ac`, `0x4a08b0` |
| `0x46be9c` | 32 | 1000×6 | back-face test against the eye `0x6b4f44` (also used by the binning `0x46e62c`) | none |
| `0x468760`, `0x46817c` | 693, 229 | 1000×6 | raster (H3, [rasterizer.md](rasterizer.md)) | raster state, surface, shade record |
| `0x46bc44` | | 0 | flat variant | raster |
| `0x46e5a4` | 56 | 1000×6 | cell objects | the cell mark; through `0x428c60` the draw state below |

### The objects of one cell `0x428c60`

`0x46e5a4(G, x, y)`: no cell → 0; r = `cell_flag_get(G, x, y)`; r ≠ 0 →
`[G+0x125](x, y, r)` (= `0x428c60`, installed by the mission load), then
`cell_flag_set(G, x, y, 0)`, whose EAX is returned; r = 0 → 0.

`0x428c60(x, y, count)`: for each of the `count` entries of the cell's
bucket (object O):

1. Type word `O+4` = 9 (flat object): `[0x4a5b04]` ← 0, O's slot `+0`,
   `[0x4a5b04]` ← `[0x4a5b08]`; not sorted.
2. Otherwise `0x401078(cam, O)` ≠ 0 (the camera is attached, mode word
   `+0x36` = 2, to O) → skipped: the own vehicle in the cockpit view.
3. Else a sort record R (`0x36` bytes, the next of `[0x4cf884]`): `R+0x32`
   ← O, `R+0x16` ← 0, `R+4..+0xf` ← O's position (slot `+4`), `R+0x10` ←
   slot `+0x10` (shape word); for type 5 and 7 `R+0xc` += the height `+0x1c`
   of slot `+0x24`'s node (500 without one); `R+0x12` ← `dist_gate(R+4,
   cam+4)`; `0x428c08(O, d)` drops the record when d exceeds the draw
   distance of its kind (`0x4cf988[k]`: type 0 → k = 1, 8 → 2, 5 → 3 or 4
   by the shape word against 7,000, others 0); kept records go to the list
   `[0x4cf888]`. `0x4cf984` ← byte `0x4d24c5` (B+9).
4. `0x428eec(cam)` (`0x49ac3c` ← cam), then `0x429620(list, n)`: the sort
   tree (pass 1 the items of kind 1/2, pass 2 the others, ordered by
   `0x429460`; the in-order walk `0x4295f0` calls each record's slot `+0` =
   `SortObj` `0x42876c`).

`0x42876c(R)`: `0x407098([0x4a9608], {R+4, R+0x10})` (the lights,
[rasterizer.md "Couplings and headless reach"](rasterizer.md#couplings-and-headless-reach)
item 2), the ground marks `0x42e8e8(O, R+0x12, byte 0x4cf984)`, the haze
`0x4680f0(R+0x12)`, O's slot `+0` (the actor draw,
[rasterizer.md](rasterizer.md#actor-draw-slots-and-explosion-binning)), the
scene-light re-add `0x407878` when word `0x4cf984` ≠ 0 (map 0 in all
suites), the dead pick
([rasterizer.md "Renderer-line quirks"](rasterizer.md#renderer-line-quirks)
item 1).

### Far and eye-cell objects `0x42883c`

`0x42883c(cam, flag)` (render-root step 15): slot `+0` of each far-list
object `0x4cf88c[0 .. [0x4cf92c])`; then, when the player's chassis is a
flyer (word `[[P+0x1f2]+0x50]` ≠ 0) and the camera is not attached to the
player, `0x46e5a4(G, P+0x1e8, P+0x1ea)` when both words are ≥ 0 (the
player's cell once more; its mark is usually 0 by now, because the walk
drew and cleared it); the pick branch (dead); `0x428eec(cam)`; `0x428b38`
only with byte `0x49abbc` (set only by the dead `0x428960`). The flag
argument (byte B+9) is not read.

### What the terrain pass writes and who reads it

| Memory | Written by | Readers | Headless |
|---|---|---|---|
| `0x6b4f6c..0x6b5057` (shade record, uv, corner and polygon arrays) | `0x46d448`, the callbacks | the static constructor `0x47136c` (array construction, before arming) and the raster through the passed pointers; the nearest simulation datum `0x6b505c` (contact point of `0x470798`) lies outside | may be skipped |
| `0x6b4f44..0x6b505a` (eye, eye cell, the arrays above), G `+0x4..+0x27`, `+0xc8`, `+0xcc`, `+0x10c`, `+0x11c`, `0x4cf988` | `0x46cb9c`, the cell draw | the renderer (`0x46be9c`, `0x470a1c`, the walkers, the draws) | may be skipped |
| `0x6b4f5c` eye-cell flag | `0x46d1a8`, the callbacks | `0x46d1a8` | may be skipped |
| `0x49f250`, `0x4a08ac`, `0x4a08b0` | `0x46d448`, `0x4680e0`, `0x4680f0` | the raster (`0x468760`, `0x468158`, `0x46817c`, `0x468414`) and `0x4680f0` | may be skipped |
| height cache at `G+0xd0` | `0x49534c`; reset by `0x4952e8`/`0x4952fc` (`0x46cb9c`; load time `0x46c4c4`, `0x46c754`) | `0x49534c` only | may be skipped |
| cell marks `[G+0xf0]`, one byte per cell | per frame `0x46e944` (clear), `0x470db8` (count, binning), `0x470ddc` (clear per drawn cell in `0x46e5a4`); at load `0x470d74`, `0x470ed4`, `0x470fe8`, `0x471128`, `0x47129c` (mission load `0x4253d8`, `0x405c3c`) | after the load only the renderer (`0x470d94` in `0x46d1a8`, `0x470e00` in `0x46e5a4`); the load-time readers `flood_sum`, `flood_fill`, `base_flatten`, `0x46c3f0` run before the first frame and use the same array as cell flags | may stay as the load leaves them |
| actor `+0x1e8`/`+0x1ea` | `0x42841c` | `0x42883c`, `0x408228`, `0x46b850`, the MECH link | may be skipped |
| buckets `0x4aab00`, `0x4cf4f0`, `0x4cf874`, far list `0x4cf88c`/`0x4cf92c`, sort records `[0x4cf884]`… and list, `0x4cf984`, `0x4a5b04` | the binning, `0x428c60`, `0x4281b0` | the scene modules and draw code (`0x474fa8`, `0x42876c`); the world loader `0x42e010` writes `0x4a5b04`/`0x4a5b08` | may be skipped |
| actor pose matrix `obj+0x12..` and its valid word `obj+0x32` | `0x401fe4` under the draw slot `0x4033e4` ([vtables.md](vtables.md) 2.2) | simulation (each update rebuilds the matrix when the word is 0) | keep list item 3 |

### Keep list for an own (H3) terrain renderer

1. **Cell marks:** the binning clears them at the start of every frame,
   and `0x46e5a4` clears the mark of every cell whose objects are drawn;
   an own renderer only has to keep "bin, then draw" inside a frame.
   Skipping the binning leaves nothing that a later frame or a simulation
   reader sees.
2. **Height cache** at `G+0xd0`: write it as the original does, or never
   read it; nothing else reads it.
3. **Actor matrix cache fill:** the cell-object draw reaches the actor
   draw slot `+0x00`, whose `0x401fe4` computes `euler_matrix` into the
   actor's pose matrix when its valid word is 0 and sets the word. The
   value equals what the simulation computes from the same angles
   (`euler_matrix` does not write the reserved byte 19, see flyer.md,
   "Flight model stack residue"), so the simulation cannot tell who filled
   it; a renderer that skips the fill only moves `euler_matrix` calls to
   the simulation (call counters differ, compared state does not).
4. **Shared view scratch:** `0x46d1a8` and `0x428140` rebuild the view
   transform `0x6c5fb4..` through `0x48c444`; positional sounds rebuild it
   themselves before use ([audio.md](audio.md)), so no simulation reader
   depends on the draw's copy.

### Open

- Whether the three residue words of the shade record reach pixels
  (depends on how the raster reads the record in shade mode 1); no reader
  of the unwritten words is named.
- The meaning of the object type words 5, 7, 8, 9 at `O+4` (9 is the flat
  object of `meteor_update`'s arrival,
  [main_loop.md](main_loop.md#meteor_update-0x409d2c-cdecl-o)), read here
  only as numbers.
- The role of the context word `ctx+0x208` that `0x428140` sets for the
  pass.

## The four drawing helpers

`poly_clip` `0x472bcc`, `poly_spans` `0x472b14`, `view_rect` `0x46d324` and
`cell_uv` `0x46bdf8` are hooks (group `world`). Callers and callees, read
from the bytes:

| VA | Callers | Callees | Touches |
|---|---|---|---|
| `0x472bcc` | `0x471f44`, `0x472504` (17 call sites) | none | its two polygon arguments, its own frame |
| `0x472b14` | `0x471f44`, `0x472504` (14 call sites) | `0x4930c1` | the polygon (in place), the span buffer, the extent record |
| `0x46d324` | `0x46cb9c` (one site, `0x46cf3a`) | none | the map fields, the globals `0x6b4f44`/`0x6b4f48`, the output polygon |
| `0x46bdf8` | `0x46d448` (one site, `0x46d6ca`) | none | the texture table `[0x6b4f40]`, `[[0x4a0be0]+0x108]`, the output quad |

The walkers `0x471f44` and `0x472504` are called only from `0x46d1a8`
(`0x46d2e9`, `0x46d2cf`; the callback choice at `0x46d2a1`/`0x46d2a8`),
itself called from `0x428140`; `0x46cb9c` (returning at `0x46d1a4`) is
called from the rendering root `0x45fca8` (`0x45fd41`, `0x45fd8f`) and from
`0x43fe44` (`0x4400d4`) and builds the view rectangle with `0x46d324`
([call chain](#call-chain-of-the-terrain-pass)). Both callbacks reach
`0x46d448`, which takes the cell's texture coordinates from `0x46bdf8`. So
the four functions belong to the terrain draw pass (H3) and have no
simulation caller.

None of them draws or touches DirectDraw, the palette or any device state:
they are integer geometry on caller memory, two read-only globals and a
read-only table. A pure helper may be replaced when the result is
bit-identical for every caller, so all four are hooks. Their outputs only
feed the drawing, so the live oracle (which compares simulation state)
cannot see a deviation; the in-vitro comparison against the original
machine code is the evidence for them. In the own process they are the
registry hooks of `sim/src/adapters.c`; they run as original code only
inside the in-vitro harnesses.

### Common shapes

A **polygon** is a record `{count, points}` of two dwords: a signed point
count and a pointer to `count` points. `0x472bcc` and `0x472b14` use 2D
points (two signed dwords x, y, 8 bytes each); `0x46d324` writes 3D points
(x, y, z, 12 bytes each).

### 0x472bcc `poly_clip`: clip a 2D polygon at an axis line

cdecl `(keep, axis, line, in, out)`, five slots; `keep` and `axis` are used
as their low bytes. One Sutherland–Hodgman step against the line
`x = line` (axis byte nonzero) or `y = line` (axis byte zero).

1. If `in.count ≤ 0` (signed): `out.count = 0`; EAX = 0, EDX = 0.
2. S = the last input point `points + 8·count − 8` (32-bit address
   arithmetic). The side of a point is the sign (−1, 0, +1) of the 32-bit
   wrapped difference `p.x − line` or `p.y − line`. With `t = +1` when the
   keep byte is nonzero and `t = −1` when it is zero, the kept half is
   `t · side ≥ 0`.
3. For every input point P in order (P is read before anything is written
   for it; S and P are copied into the frame, so output that overlaps the
   input is read back as the original does):
   - a = t·side(S), b = t·side(P).
   - b ≥ 0: if a < 0 and b > 0 (entering), emit I, then P; otherwise emit P.
   - b < 0: if a > 0 (leaving), emit I; otherwise nothing.
   - S = P.
   The original encodes this as a byte table at `0x472ce9` (19 entries,
   index `(keep ? 0 : 10) + 3·side(S) + side(P) + 4`) into a five-entry jump
   table at `0x472cfc` (codes 0 and 1 emit nothing, 2 emits P, 3 emits I and
   P, 4 emits I). Index 9 and code 0 never occur.
4. The intersection I on the line: for the X line, I.x = line is written
   first, then I.y = q + S.y with q = (low dword of (line − S.x)·(P.y − S.y))
   / (P.x − S.x), a signed 32-bit division truncating toward zero (CDQ, so
   only the low product dword is the dividend). For the Y line, I.y = line
   first, then I.x = (low dword of (line − S.y)·(P.x − S.x)) / (P.y − S.y)
   + S.x. Emitted points go to `out.points` (read once before the loop)
   consecutively, x before y.
5. `out.count` = number of emitted points. EAX = the high 24 bits of the
   last P.y with the low byte of the count in AL; EDX = `out`.

**Divisor.** An intersection is only computed when the two sides are
strictly opposite, so the 32-bit difference of the coordinates is nonzero;
it can be −1 only when the wrapped differences are +2^31−1 and −2^31, and
then a dividend of `INT_MIN` faults (#DE) in the original. Callers use
screen-sized grid coordinates; the in-vitro generator excludes that one
fault.

**Branches.** 17 conditional sites. Two edges are unreachable: the taken
edge of `0x472c68` (the count tested there is the value already found
positive at `0x472be6`; nothing but the frame is written in between) and
the taken edge of `0x472cd6` (the table index is at most 18).

### 0x472b14 `poly_spans`: span list of a polygon

cdecl `(polygon, spans, extent)`, three slots.

1. minX, minY = the first point's coordinates; for the remaining
   `count − 1` points (signed loop, none when count ≤ 1) each coordinate
   that is smaller (signed) replaces the minimum.
2. `count` and `points` are read again; for `count > 0` (signed) points every
   point is translated in place: x −= minX, then y −= minY. The polygon
   stays translated.
3. `count` and `points` are read a third time and `0x4930c1(count, points,
   spans)` runs (original code, see below) with EAX = points, EDX = count,
   ECX = minY (count > 0) or the first point's x (count ≤ 0).
4. With y0 = `[spans] + minY`: `extent+0 = y0`, then `extent+4 = [spans+4] +
   y0 − 1`, `extent+8 = [spans+4]`, `extent+12 = spans + 8` (each read after
   the previous write).
5. minX is added to each of the `2·[spans+4]` dwords at `spans + 8` (signed
   count of the doubled value; none when ≤ 0).
6. EAX = the address after the last adjusted dword (`spans + 8` when none),
   EDX = 0 after the loop or the doubled count when the loop did not run.

**0x4930c1** is a hand-written scan converter (it starts with `ENTER`, so
the DBSIM map could not probe it; 4 functions and about 830 instructions
with `0x493141` and `0x493192`). From the bytes: it returns immediately for
count 0; for more than two points it computes the polygon's orientation
from the 64-bit cross products and, when the orientation is reversed, copies
the points in reverse order into its frame first; `0x493192` writes the
first row y at `spans+0`, the row count at `spans+4` and per row a pair of
x bounds. It reads only its arguments and memory, and its EAX/EDX/ECX are
dead in `0x472b14`. For the hook it stays original and is called through
the guest bridge with the original entry registers; in the own process it
is the row `gfx_scan` (`sim/dispatch/services/gfx_primitives.h`,
[rasterizer.md](rasterizer.md#graphics-library-primitives)).

**Branches.** 8 conditional sites, all feasible both ways.

### 0x46d324 `view_rect`: view rectangle on the terrain grid

cdecl `(map, polygon)`, two slots. `map` is the terrain grid of
[player_movement.md](player_movement.md#terrain-height-46e180)
with four more dwords: origin X `+0xf4`, origin Y `+0xf8`, height `+0xfc`,
view radius in cells `+0x10c`. Shift counts use their low five bits.

1. s = `[map+0x108]`, r = `[map+0x10c] << s`; E = the eye position
   (`[0x6b4f44]`, `[0x6b4f48]`).
2. x0 = max(E.x − r, `[map+0xf4]`), y0 = max(E.y − r, `[map+0xf8]`)
   (signed).
3. x1 = min(E.x + r, ((1 << `[map+0x100]`) − 2) << s + `[map+0xf4]`),
   y1 = min(E.y + r, ((1 << `[map+0x104]`) − 2) << s + `[map+0xf8]`)
   (signed). All sums wrap at 32 bits.
4. If x1 < x0 or y1 < y0 (signed): `polygon.count = 0`, EAX = polygon,
   EDX = 0.
5. Otherwise `polygon.count = 4`, then `polygon.points` is read and the four
   points (x0, y0, z), (x0, y1, z), (x1, y1, z), (x1, y0, z) are written in
   the order x0, y0, x1, y1 (third point), x0, y1 (second), x1, y0 (fourth),
   and only then z = `[map+0xfc]` is read and stored into the fourth, third,
   second and first point. EAX = polygon, EDX = z.

**Branches.** 6 conditional sites, all feasible both ways.

### 0x46bdf8 `cell_uv`: texture coordinates of a terrain cell

cdecl `(cell, u, v, out)`, four slots. `cell` points at two dwords
`{texture index, level}`; the texture table `[0x6b4f40]` has 20-byte
entries (four dwords U0, V0, U1, V1 and a word at +10h).

1. e = `[0x6b4f40]` + 20·index (32-bit).
2. level = 0: U0, V0, U1, V1 = the entry's four dwords.
3. level ≠ 0: k = `[[0x4a0be0]+0x108]` + level − 13 (low five bits used),
   size = 1 << k; U0 = (u << k) & FFh, w = (v << k) & FFh; U1 = size + U0,
   V0 = −w, V1 = −(size + w).
4. The quad (U0, V0), (U0, V1), (U1, V1), (U1, V0) goes to `out` (eight
   dwords) in the order out+8, +0 (U0), +1Ch, +4 (V0), +18h, +10h (U1),
   +14h, +0Ch (V1).
5. EAX = the sign-extended word at e+10h (read after the writes),
   EDX = e.

**Branches.** 1 conditional site (`0x46be19`), both ways.

### Registers, stack and callers

No function reads EAX, EDX or ECX at entry, and no local is read before it
is written (in `0x46bdf8` the dword at EBP−8 is unused on the level-0 path;
it is popped into ECX and overwritten by the second pop), so there are no
entry shims. No x87.

ECX at return (the hooks cannot return it): `0x472bcc` leaves the count,
`0x472b14` minY, `0x46d324` x1, y1 or the point pointer, `0x46bdf8` U1. The
bounded interprocedural forward walk with byte-lane tracking
(over a full listing) from the four return
instructions finds ECX written before any read on every path, except the
indirect calls `[ESI+14h]` after `0x472bcc`/`0x472b14` in `0x471f44`; both
possible targets (`0x46dfa8`, `0x46dddc`) write ECX before they read it
(same walk from their entries). ECX is therefore dead after every caller.

### Validation (in vitro)

`tools/reference-game/invitro/world_cases.h`, 1,001,024 direct cases per
hook plus installed 31/a7 poison probes, and a real-callee pass of
`poly_spans` over the original scan converter; see the
[in-vitro README](../../tools/reference-game/invitro/README.md)
and [hooks.md](../reference-game/hooks.md#registry).

## The terrain renderer as own code

Service rows of the own `dbsim.exe` (`sim/src/terrain_draw.c`,
`sim/include/es2/sim/terrain_draw.h`, 14 rows in
`sim/dispatch/services/terrain_draw.h`): the terrain set-up `0x46cb9c`, the walk
`0x46d1a8` with its walkers, the line callbacks, the cell draw with the
texture selection and their leaves. REA's pseudocode of `0x46cb9c`,
`0x46d1a8`, `0x46d448` and the walkers was read as a hint only; every
register, width, sign extension and read/write order was taken from the
listing and tested in vitro. The contracts are the sections above; the
rows table adds the registers each row leaves.

| VA | Row | Slots | Registers left | Contract |
|---|---|---:|---|---|
| `0x46cb9c` | `terrain_draw` | 3 | EAX | [Terrain visibility](#terrain-visibility-0x46cb9c); EAX the visible byte path's `0x4952fc` EAX, 0 when invisible |
| `0x46d1a8` | `terrain_walk` | 2 | EAX | [The cell walk](#the-cell-walk); EAX the saved `[ctx+0x22c]`, or the entry EAX when `G+0x11c` = 0 |
| `0x471f30` | `terrain_walk_setup` | 3 | EAX R, EDX cb, ECX G | (R, cb, G): `R+0x14` ← cb, `R+0x18` ← G |
| `0x471f44` | `terrain_row_walk` | 3 | none | (R, eye, poly); [the walkers](#the-cell-walk) |
| `0x472504` | `terrain_column_walk` | 3 | none | (R, eye, poly); [the walkers](#the-cell-walk) |
| `0x4724b8` | `terrain_turn` | 3 | EAX c, EDX new y, ECX py | (px, py, c): x′ = y − c.y + c.x, then y′ = c.x − x + c.y |
| `0x4724e0` | `terrain_turn_back` | 3 | EAX c, EDX px, ECX py | x′ = c.y − y + c.x, then y′ = x − c.x + c.y |
| `0x46dddc` | `terrain_row_cells` | 4 | none | (R, y, a, b): the cells (x, y), x = a .. b **inclusive** in the step `R+0xc` |
| `0x46dfa8` | `terrain_column_cells` | 4 | none | (R, x, a, b): the cells (x, y), y = a .. b inclusive in the step `R+0x10` |
| `0x46d448` | `terrain_cell_draw` | 4 | EAX | (x, y, G, Q): [the cell draw](#the-cell-draw-0x46d448); EAX of `0x46e5a4` |
| `0x46e5a4` | `terrain_cell_event` | 3 | EAX | (G, x, y): no cell → 0; r = `cell_flag_get(G, x, y)`; r ≠ 0 → `[G+0x125](x, y, r)`, then `cell_flag_set(G, x, y, 0)`, whose EAX is returned; r = 0 → 0 |
| `0x49534c` | `height_cache_get` | 2 | EAX | (H, h): flag byte `[H+0x10]+h` set → the entry `[H+0x14] + 12h`; else v = h·`[H+4]` + `[H+8]`, the entry ← `[H+0x18]`'s three dwords each `(int64 · v) >> 16` (low dword), the flag ← 1 |
| `0x470d94` | `terrain_cell_mark` | 2 | EAX | (G, cell): the mark byte `[G+0xf0] + ((cell − [G+0xec]) >> 4)`, zero-extended |
| `0x46be9c` | `terrain_facing` | 2 | EAX | (n, P): 1 when the wrapping dot product of the three sign-extended words at n with eye − P (`0x6b4f44`) is above 0 (signed) |

`0x46be9c` lies in the shape draw's slot range `0x46be9c..0x46f000` but is
the cell draw's own callee, so its row is here.

Every callee of these rows is a row or a registry hook: the frustum library
([below](#the-frustum-library-as-own-code)), the scene's `0x428bc0`, the
fills `0x4680e0`, `0x4680f0`, `0x468760`, `0x46817c` and `0x46bc44`, and the
shading pass `0x46c2e0` that computes the cell normals (`+1..+0xc`) and
shades (`+0xd`, `+0xe`) at load
([mission_load.md](mission_load.md#34-the-roots-last-callees-the-terrain-shading-pass)
section 34). `0x46c49f..0x46c4b4` (in the per-cell shading `0x46c3f0`
under the shading pass: mark bit 2 clears the two shade bytes) is dead
code: no jump or call names it (listing scan), and the `jmp 0x46c4b5`
before it skips it.

A corner cell outside the grid: the original reads its height at NULL
(faults at `0x46d56a`, `0x46d594`, `0x46d5cb`, `0x46d603` for corners
0..3); the own row refuses with that address. Not comparable in vitro.

### In vitro (`differential_terrain_draw_test`, `--terrain_draw`)

`tools/reference-game/invitro/terrain_draw_cases.h`: synthetic grids (2..4
bits per side, shift 0..16, random cells, marks, normals, texture bytes,
height cache and coordinate records), the view matrices and a camera with
a view context; the original's machine code (side 0) against the own rows
through a dispatch of the real services (side 1). The fills, `0x428bc0`
and the cell object callback are recorders on both sides (a jump patched
over the original entry; a row at the same VA): their arguments, the
polygon and coordinate buffers and the shade word or dword where it is
written go into a hash and a count, so walk order, cell selection, split,
texture and the fill calls are compared call by call. The hooks
(`poly_clip`, `poly_spans`, `view_rect`, `cell_uv`, the cell flags,
`rotate_point`, `transform_point`, `line_plane`) run as the original's
code on both sides; the frustum library is the own rows of
`sim/src/frustum.c`. Compared: the recorder hash and count, EAX (EDX/ECX
for the turns and the walk set-up), the world buffer and DATA plus heap
(`su_take`); masked: the shade dwords `0x6b4f6c..0x6b4f7b`.

Parts: leaves 3000, walk 1500, draw 1500 cases (seed `0x48c06e21`), all
identical. Walks by rows, by columns and in the simple order, both eye-flag
outcomes, given, frustum and invisible polygons and every fill path occur;
a walk whose polygon has a point outside the grid (where the original
faults) would be left out and counted, and none is. Mutation: the second
triangle's coordinate order (1, 2, 3) → (1, 3, 2) is caught in leaves case
11 (the recorder hash).

## Binning, the cell callback and the sort tree as own code

`sim/src/scene.c` (service rows in `sim/dispatch/services/scene.h`)
implements the scene modules from the listings:

- **Cell table and binning:** the cell table `0x4281b0`, the stores
  `0x4282f8`/`0x42837c`/`0x4283b4`/`0x4282d8` and the binning `0x42841c`.
- **Draw distances and cull:** `0x428bc0` and `0x428c08`.
- **Cell callback:** `0x428c60`.
- **Sort tree:** `0x429460`, `0x4294d4`, `0x429524`, `0x42958c`,
  `0x4295f0` and `0x429620`.
- **Per-object draw:** `0x42876c`.
- **Scene finish:** `0x42883c`.
- **Pick save/restore:** `0x428ae0` and `0x428b38`.
- **Accessors:** `0x4282c0`, `0x4282c8` and `0x428eec`.

Facts the listings settle:

- **Cell table full.** From the 0x96th new cell on, `0x4281b0` calls the
  assertion `0x479c78` and then keeps writing. Entry 0x96 lands on the
  count `0x4cf874`, which becomes `(row << 16 | column) + 1`. The own code
  does the same.
- **Bad slot byte.** A slot byte ≥ `0xfa` in `0x4282f8` asserts and then
  stores anyway.
- **`0x4282f8` registers.** Without a cell record, it leaves EAX and ECX
  unchanged and EDX holds the old loose count. With a record, EAX is the
  slot, EDX is `125 * index` and ECX is the bucket.
- **Binning order.** The flats come first: pool `0x4a968d`, gated by
  `0x49288c` below 30000, and binned before the loose count is reset. Then
  the vehicle pools follow (buildings, mechs, flyers), skipped by
  `[[O+0x45]+0x14]`. A MECH with a link `+0x2b0` uses the link's cell words
  `+0x1e8`. Each vehicle gets its cell words written. The pools that follow
  are 5, 6 (`0x408240` hidden, `0x408228` cell words or by position), 7,
  8, 9 (`0x46b850` cell words) and 10.
- **Sort.** Kinds 1 and 2 are placed in the first pass and the rest in the
  second.
  - Comparison: when both kinds are 1/2 it uses `0x42937c`; when both are
    0 it compares the distances `+0x12` (signed); otherwise it uses
    `0x42900c(a, b, 1)`.
  - Insert: a zero low word goes left.
  - Walk: in order, through the record's slot 0.
  - A zero word count draws nothing.
- **Pick.** The "still current" else branch of the device block in
  `0x42883c`/`0x428b38` cannot be reached unless the driver's palette slot
  changes `[0x6c5ed0]`.

The comparators `0x42900c` and `0x42937c` (with `0x428f38` and `0x42933c`)
have no rows: the own sort calls them through the dispatch, which refuses
them.

Tests: `//sim:scene_test` (synthetic image) and
`//tools/reference-game/invitro:differential_scene_test` (`--scene`). The
binning, cells, sort and pick parts run 1000 cases each. Every stand-in
callee (the grid, the pools, the comparators, the draws, the driver slots)
records its arguments. The call order, DATA, the heap and the world all
match the original's machine code, and so do the contract registers.

## The frustum library as own code

The view frustum record the terrain set-up `0x46cb9c` builds when it gets
no polygon (constructor `0x4949a4`, placement `0x4949e4`, polygon clip
`0x494e58`) and its callees, as service rows of the own `dbsim.exe`
(`sim/src/frustum.c`, `sim/include/es2/sim/frustum.h`, 12 rows in
`sim/dispatch/services/frustum.h`).

| VA | Row | Slots | Registers left | Contract |
|---|---|---:|---|---|
| `0x4949a4` | `frustum_new` | 3 | EAX F, EDX `0x49667c`'s, ECX d | (F, d, e), listing `..0x4949e2`: the eye's empty constructor, the six planes `0x49667c(F+0x18, 0x10, 6, 1, 0x49520e)`, then F+4 ← d, F+0 ← e, the plane mask F+8 ← 0xffffffff |
| `0x4949e4` | `frustum_place` | 4 | EAX, EDX of the last `0x47e450`; ECX a dead stack address | (F, cam, d, e), listing `..0x494cb0`, frame 0x80: F+4 ← d, F+0 ← e, the eye F+0xc ← the camera position (`0x495157`). From the camera's view context v (`0x49516d`) and shift s (`0x495162`): W = v[0x218] − v[0x210], H = v[0x21c] − v[0x214]; the rectangle at depth d: l = −v[0x220]·d SAR s, r = (v[0x220] + W)·d SAR s, t = −v[0x224]·d SAR s, u = (v[0x224] + H)·d SAR s (low dwords). The camera matrix (`0x495132`, `0x495178`) turns (0, 0, e), (e, 0, 0), (0, −e, 0) into p1, p2, p3 and each normal (`0x4954e4`); planes by `0x47e450`: (0, t, −d) → +0x38 through p1, p1 negated (`0x4951d6`), (0, u, d) → +0x48 through −p1; (−d, r, 0) → +0x68 through p2, p2 negated, (d, l, 0) → +0x58 through −p2; (0, d, 0) → +0x18 through p3; −n → +0x28 through n (n copied, then negated) |
| `0x494e58` | `frustum_clip` | 5 | EAX out's count ≠ 0, EDX `0x4950ec`'s, ECX out | (F, poly, a, b, out), listing `..0x4950a1`, frame 0x258: two local 20-point polygons and six local planes (`0x49667c`); each plane of F copied (+0xc, +8, +0, +4) and, unless vertical (`0x495236`), shifted (`0x495254`) by −b when its z > 0 (signed), else by a; out ← poly − eye (`0x4950a4`); for the mask bits of F+8 in the order 0x20, 0x40, 0x08, 0x10, 0x04, 0x02 (planes 4, 5, 2, 3, 1, 0) the current polygon is split (`0x47e73c`) into the other (front) and a scratch polygon (back), the two swap (`0x495221`); out ← current + eye (`0x4950ec`) |
| `0x4950a4` / `0x4950ec` | `poly_offset_sub` / `_add` | 3 | EAX/EDX past the last point, ECX the eye | (in, eye, out): out's count ← in's, each point minus / plus the eye (x, y, z); pointers and count read before the count is stored |
| `0x49520e` | `plane_ctor` | 1 | EAX = ECX = e, EDX unchanged | `0x4953c4(e)` |
| `0x495221` | `swap_dwords` | 2 | EAX a, EDX b, ECX old *a | swaps *a and *b |
| `0x495236` | `plane_vertical` | 1 | EAX = EDX = (EDX & ~0xff) \| r | r = 1 when x or y of the normal ≠ 0 and z = 0 |
| `0x495254` | `plane_shift` | 2 | EAX P, EDX the product | P+0xc −= k·P[2] (low dword) |
| `0x47e450` | `plane_from_normal` | 3 | EAX P2·o2, EDX the distance term, ECX P | (out, P, N): N scaled to length 0x800. Length: `0x492a7d(N)` when a component is > 15000 or < −15000 (signed, x, y, z in turn), else the square root `0x47df9c` (own `es2_logic_root`) of the low dword of x² + y² + z²; each component the 64-bit N[i]·0x800 IDIV length (`0x47eaa1`, `0x47eabe` inline); out+0xc ← −(P·o) (low dwords). Zero length or a quotient beyond 32 bits is the original's divide error at `0x47de3c`: refused |
| `0x47e73c` | `poly_plane_clip` | 4 | EAX front ≠ 0 and back ≠ 0, EDX front's count, ECX back's | (P, in, front, back), jump table `0x47e827`, frame 0x44: below three points both counts 0 (EAX 0, EDX back, ECX 0). Edge from the last point; case prev·3 + side + 4: 0/3 → back (back's flag cleared); 1/4/7 → both; 5/8 → front (front's flag cleared); 2 and 6 cross: when `line_plane 0x47e610` cuts, the cut to both, the point to front (2) or back (6). Points copied +8, +0, +4. A side below three points gets count 0 and its flag set; both flags set → front a copy of in. Counts stored front then back |
| `0x47eaf8` | `plane_side` | 2 | EAX = ECX the side, EDX −P[3] | (P, pt): dot `0x48d97a` against −P[3]: 1 above, −1 below, 0 equal (signed) |

Called through the dispatch, not own code here: the registry hooks
`matrix_ctor 0x495132`, `rotate_point` (through `0x4954e4`), `line_plane
0x47e610`, and the rows `0x49667c`, `0x495157`, `0x495162`, `0x49516d`,
`0x495178`, `0x4951d6`, `0x492a7d`, `0x48d97a`. `0x494cb4` (after the
placement) is a clip variant no jump or call names (listing scan); not a
row.

### In vitro (`differential_frustum_test`, `--frustum`)

`tools/reference-game/invitro/frustum_cases.h`: a 0xa00-byte world with
random planes, polygons, cameras with view contexts and records; random
entry registers. Leaves: every row above alone; frustum: new → place →
clip chains and random records + clip. The hooks run as the original's
code on both sides (the harness fails if a passed VA has a service row).
Compared: EAX/EDX/ECX (except `0x4949e4`'s ECX, the address of a stack
local of the original's frame, which differs between the sides by
construction and is never read), the world and DATA plus heap
(`su_take`). Coverage is enforced (every split case, misses, short inputs,
kept input, long and short normals, vertical and both shifts, empty and
non-empty offsets and outputs, all three sides). Parts: leaves 3000,
frustum 1500 cases (seed `0x48c06e21`), all identical. Mutation: the
long-normal test `> 15000` → `>= 15000` in `0x47e450` is caught in leaves
case 80 (the world).
