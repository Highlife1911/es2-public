# Shape and bitmap loading

Six functions of DBSIM's persistent-object library are hooks of the adapter
group `assets`, served from own code: the generic loader `dts_load`
`0x47a144`, the `GLBitmap` factory `bitmap_new` `0x481cd4` and read
`bitmap_read` `0x481d8c`, the `GLBitmapArray` factory `bitmap_array_new`
`0x482d88` and read `bitmap_array_read` `0x482e7c`, and the VOL-aware
stream's destructor `stream_destroy` `0x472f64`. They load read-only
resources (shapes, bitmaps, palettes, fonts) and have the verdict
"platform" in [function_verdicts.md](../reference-game/function_verdicts.md); they
stand on the stream model of
[file_io.md](file_io.md#the-vol-aware-read-stream-as-own-code).

The contract below was read from the `llvm-objdump -d -M intel` listing of
the reference `VER95/DBSIM.EXE`
([reference_version.md](../reference_version.md); image base `0x00400000`).
Record layouts are those of [dbm.md](../formats/dbm.md) and
[dts.md](../formats/dts.md).

| Part | Where |
| --- | --- |
| contracts over guest memory (module `asset_load`, depends on `vfs_stream`) | `core/src/asset_load.c` |
| member read without its copy, `es2_vfs_stream_fetch` (the VOL-aware read hook calls the same member path) | `core/src/vfs_stream.c` |
| hook adapter | `tools/reference-game/proxy/assets_hooks.h` |
| shape, TS/AN class and memory-stream services of the own process | `sim/src/load.c`, `sim/dispatch/services/mission_load.h` |
| in-vitro cases | `tools/reference-game/invitro/assets_cases.h` |
| unit test | `//core:asset_load_test` |

The bytes of a member stream come from the archive file that the VFS
adapter identified and mapped (`formats/` `es2_vol`): the loaders read
through the VFS, not through the game's archive buffer.

## Objects and globals

### Class registry

`0x6c5834` holds `[0x4a1690]` pairs (entry table, count); an entry is 12
bytes: tag (the object header's first dword), factory, class-name pointer.
Four registration routines call `0x47a0b8`, which appends one pair and
increments `[0x4a1690]`:

| Routine | Table | Entries |
| --- | --- | --- |
| `0x4277f0` (init record) | grid `0x49aa7c` | 1: GridShape `02bc0001` |
| `0x480ef4` | GL `0x4a2eb8` (count at `0x4a2f24`) | 9: Old GLBitmap `00280000`, GLBitmap `0028000e` → `0x481cd4`, GLBitmapArray `00280001` → `0x482d88`, GLPalette `0028000f`, Old GLFont `00280003`, GLFont `00280005`, GLRectangleRegion `0028000c`, GLEdgeTableRegion `0028000d`, GLCursor `0028000b` → `0x483374` |
| `0x48dd08` | TS `0x4a63b0` (count `[0x4a6488]`) | 18 |
| `0x4913dc` | AN `0x4a687c` (count `[0x4a68ac]`) | 4 |

The grid table is the only one registered by an init record, so pair 0 is
always the grid table; the GL table is registered from start-up
(`0x464d5c`, `0x480be0`), TS and AN by the mission load
(`0x4617ac`/`0x4617b1`; [own_process.md](../reference-game/own_process.md)).

A second ("fallback") registry at `0x6c58d4` with count `[0x4a1694]` is
filled only by `0x47a0e8`, which nothing calls or references (no call, no
jump, no stored pointer; `[0x4a1694]` is 0 in the image): it stays empty.

The factory field of an entry, not a class vtable, holds the factory:
`0x481cd4` is entry 1's field `0x4a2ec8`, likewise `0x482d88` and
`0x483374`.

### Loader frame

`[0x6c5964]` points to the innermost load's frame (0x1c bytes on its
stack): `+0` tag, `+4` stream position before the header, `+8` end of the
object (signed dword), byte `+0xc` "length even", `+0x10`/`+0x14` the header
(tag, length), `+0x18` the registry entry. Only `0x47a6b4`, `0x47a83c`,
`0x47a144` and the save path (`0x47a1ac`, `0x47a638`, `0x47a798`) use it.

### Arenas

`[0x6c5830]` is the current arena record (base, limit, used; 12 bytes in
the table `0x6c5968`, index `[0x4a16c0]`, initially -1 and `[0x6c5830]` 0).
The shape loader pushes one (`0x47a3fc`, `0x47a430`, `0x47a474`); bitmaps
loaded by name use none. `0x47a288(n, A)` and `0x47a2c8(n, A)` (the same
code at two addresses): with A non-null and `used + n` (32-bit) at or below
the limit, `used` += n and the result is `base + old used`; otherwise new
`0x4774f0(n)`.

### Bitmap objects and the stream

`GLBitmap` (0x1a bytes, vtable `0x4a306c`; read slot `+0xc` = `0x481d8c`):

| Offset | Meaning |
| --- | --- |
| `+4` | height (word) |
| `+6` | width (word) |
| `+8` | depth (byte) |
| `+9` | flags (byte; bits 0..4 from the file, 5..7 kept) |
| `+0xa` | stored pixel bytes P |
| `+0xe` | pixel pointer |
| `+0x12` | compression (word, zero-extended byte) |
| `+0x14` | colour count C (word, used signed) |
| `+0x16` | colour pointer |

`GLBitmapArray` (0x0c bytes, vtable `0x4a32ec`; read slot `0x482e7c`): `+4`
count, `+8` pointer array. The stream S is the VOL-aware stream (0x2f bytes,
vtable `0x4a0e10`, [file_io.md](file_io.md#objects)).

### Memory stream

Vtable `0x4a1788`; base constructor `0x47a888` (vtable `0x4a16e8`, status
`+4` = 0, construction counter +1). `0x47b36c(S, size, buffer)` sets `+8` =
buffer, `+0xc` = buffer + size, `+0x10` = buffer, the vtable `0x4a1788` and
counts the counter up once more.

| Offset | Meaning |
| --- | --- |
| `+4` | status (base class) |
| `+8` | start of the buffer |
| `+0xc` | end of the buffer (start + size) |
| `+0x10` | cursor |

- **tell `0x47b400` (slot `+4`):** EAX = cursor − start.
- **read `0x47b4b4` (slot `+0x18`; S, size, buf):**
  - Status not 0 (`0x47b4c7`): EAX 0, nothing else.
  - Otherwise avail = end − cursor. If avail ≥ size, compared signed
    (`0x47b4d7`): `memcpy(buf, cursor, size)` (thunk `0x49680e`; CW3220's
    memcpy returns the destination), then AL ← 1 and cursor += size. EAX is
    the buffer with AL 1.
  - Else: `memcpy(buf, cursor, avail)`, cursor += avail, then
    `0x47b3a0(S, 0)`: the cursor equals the end (`0x47b3ac`) and the flag
    is 0 (`0x47b3b2`), so status ← 2. EAX 0.
- Other slots: `+0` destructor `0x481aea`, `+8` seek `0x47b410`, `+0x14`
  write `0x47b45c` (`0x47b3a0(S, 1)` at the end: status 1). `dts_load` and
  the readers it reproduces use only tell and read.

The only user that reaches `dts_load` is the weapon shape cache
([Weapon shape cache](#weapon-shape-cache)). The classes derived from the
base (`0x47a888` callers) are `0x4a0e10`, `0x4a1710`, `0x4a1738`,
`0x4a1760`, `0x4a1788`, `0x4a17b0`, `0x4a1808`, `0x4a1ab0` and `0x4a1ad8`;
only `0x4a0e10` and `0x4a1788` reach `0x47a144`.

## Loader functions

All are cdecl. The construction counter is the dword `[[0x6cbee4]]`.

### dts_load 0x47a144

`dts_load(S, arg)`. `[0x6c5964]` is saved and set to the frame F.

- **Header** `0x47a6b4(S, F+0x10)`: F+4 ← S.tell (virtual `+4`); the header
  ← (-1, 0); S.read(8, F+0x10) (virtual `+0x18`); if lookup(tag)
  (`0x47a4a0`) finds an entry: F+0 ← tag, F+8 ← (S.tell + length + 1) AND -2
  (`0x47a70d..0x47a712`), byte F+0xc ← (length AND 1) = 0; else, with the
  fallback registry empty, nothing more.
- **Factory.** If the tag is not -1 (`0x47a171`), F+0x18 ← lookup(tag) again
  and, when non-null (`0x47a182`), result ← the entry's factory `[e+4]`
  called with (S, arg) (EAX = ECX = e, EDX = arg).
- **Drain** `0x47a83c(S)`: while S's status (`0x4930ab`: the dword S+4) is 0
  and S.tell < `[[0x6c5964]+8]` (signed), S.read(1) into its own local.
- `[0x6c5964]` ← the saved value; EAX = the result (0 when nothing was
  built).

A tag the registry lacks leaves F+8 unset: the drain then reads stack
garbage unless S's status is already nonzero.

### bitmap_new 0x481cd4

`bitmap_new(S, arg)`. Exception record (`0x49630c`, descriptor `0x4a309c`;
FS:0 restored at exit, EDX = its saved head = FS:0 at entry). B ←
`0x47a2c8(0x1a, [0x6c5830])`; non-null: constructor `0x48156c` (vtables
`0x4a69f0`, `0x4a3368`, counter +1, `0x4a3040`, counter +1, `0x4a306c`;
+0xa, +0x16, +0xe ← 0, word +0x14 ← 0; counter +1), then counter -3
(`0x481d13`). Then B's virtual `+0xc` (B, S, arg) (EAX = arg, EDX = S,
ECX = the vtable). EAX = B. A null B faults at `0x481d2d`.

### bitmap_read 0x481d8c

`bitmap_read(B, S, arg)`. The pushed ECX is the local byte L (`[ebp-1]` =
ECX's top byte; a read that delivers nothing keeps it).

- S.read(2, B+4), S.read(2, B+6), S.read(1, B+8), S.read(1, L); B+9 ←
  (B+9 AND E0h) OR (L AND 1Fh) (two stores, `0x481dde`, `0x481df7`);
  S.read(1, L); word B+0x12 ← L; B+0xe ← 0; B+0x16 ← 0; S.read(4, B+0xa);
  S.read(2, B+0x14).
- P = B+0xa non-zero (`0x481e36`): B+0xe ← `0x47a288(P, [0x6c5830])`,
  S.read(B+0xa, B+0xe).
- Word B+0x14 non-zero (`0x481e62`): n = sign-extended C × 4, B+0x16 ←
  `0x47a288(n, [0x6c5830])`, S.read(n, B+0x16) (EAX = n at this call).
- EAX = the last read's EAX. `arg` is not read. The reads go through helpers
  (`0x492430`, `0x490874`, `0x481f3d`, `0x482f21`, `0x482bd7`: EAX = the
  vtable, EDX = the buffer) or directly; none tests a result.

### bitmap_array_new 0x482d88

`bitmap_array_new(S, arg)`. As `bitmap_new` with descriptor `0x4a31d8`, 0x0c
bytes and the constructor `0x482f38` (vtables `0x4a69f0`, `0x4a32ec`;
`0x482f80(A, 0, 0)`: +4, +8 ← 0; counter +1), counter -1 (`0x482dc7`),
virtual `+0xc` = `0x482e7c`. A null object faults at `0x482de0`.

### bitmap_array_read 0x482e7c

`bitmap_array_read(A, S, arg)`. S.read(4, A+4); A+8 ←
`0x47a288(A+4 << 2, [0x6c5830])`; for i = 0 while i < A+4 (signed,
reread): `[A+8]+4i` ← `0x47a144(S, 0)`. EAX = the last load's result, or the
allocation when the count is not positive.

### stream_destroy 0x472f64

`stream_destroy(S, flag)`. Exception record (descriptor `0x4a0d3c`).
Counter -1.

- S null (`0x472f82`): EAX = FS:0 at entry (left by `0x49630c`), EDX = the
  counter pointer.
- Otherwise [S] ← `0x4a0e10`; for each decoder object d = S+0x27, S+0x2b
  that is non-null: counter += `0x496406(d, 0)`, d's virtual `+0` (d, 3);
  close `0x47307c(S)`; the base destructor `0x47a8a4(S, 0)`: counter -1,
  [S] ← `0x4a16e8`, its slot `+0x10` (`0x47a904`: AL ← 1; EAX keeps the
  counter pointer in bits 8..31); flag bit 0 (`0x473022`): delete
  `0x477548(S)`.
- EAX/EDX: the base destructor's (counter pointer with AL 1; `0x4a16e8`) or
  the delete's. The second tests of each decoder (`0x472f9c`, `0x472fdb`)
  re-read the value just found non-zero and cannot branch.

### Registers after the calls

A scan of every direct call site of the six functions (21 of `0x47a144`, 62
of `0x472f64`; the factories and reads are reached only through `0x47a144`
and the factories' virtual call) finds no read of ECX or EDX before a write
(`reguse.py` in the evidence directory). The virtual destructor sites
(`[vtable+0]` with flag 3) were not enumerated.

## Callers and reach

`0x47a144` is called by the load-by-name wrappers `0x45cdf4` (DBA/DBM by
name, e.g. the loading screen, `lo_curs.dba`, the HUD bitmaps) and
`0x430fb0` (the dialog code's cursor), the shape loaders `0x474b30` and
`0x474cd8` (TS/AN roots; they push an arena), the terrain grid `0x46c754`,
`0x429c2c`, `0x453630`, `0x453684`, `0x465f7c`, `0x474c10` (the memory
stream, below), and recursively by the TS/AN readers and `0x482e7c`. The
function is reached only by its 21 direct calls; no pointer to it is stored.
Every caller except `0x474c10` builds a VOL-aware stream (`0x472f14`) on its
frame; the TS/AN readers and `0x482e7c` pass their own S on.

The DBSIM map (counted after the mission load, late arming,
a60/c1s/c1c/c3_09/c4_08/c3_10): `0x47a144` 0/26/0/5/5/5 = 8/1 arrays
(`0x45cdf4` 0/8/0/1/1/1) + 17/3 bitmaps (`0x481cd4`, `0x481d8c`
0/17/0/3/3/3) + 1 cursor (`0x483374`, `0x430fb0` 0/1/0/1/1/1); `0x482d88`,
`0x482e7c` 0/8/0/1/1/1; `0x472f64` 28/74/36/137/45/57 (= the stream opens).
With early arming (L0, the default,
[mission_load.md](mission_load.md#13-l0-arming-before-the-load-and-the-after-load-snapshot))
the hooks also see the whole mission load: the shapes (TS/AN through
`dts_load` and the original TS/AN factories), palettes, fonts, grids and the
loading-screen arrays; the map cannot count these.

### Weapon shape cache

- **The cache.** `0x40f998` (mission load, called at `0x425604`; see
  [mission_load.md](mission_load.md), "weapon shape cache") opens
  `mechwpns.dts` (`0x4991e0` and the extension `[0x4a09f8]`) as a VOL-aware
  stream. For each root it peeks the header (`0x47a308`, the tag must be
  `0x140008`), allocates `0x47a540(header)` bytes (`(length + 9) AND -2`:
  header, body, pad) with new `0x4774f0`, and reads the root whole into that
  block (`0x4735b8`). Block pointers and sizes go to `0x4a9904` and
  `0x4a99f4`, the count to `[0x4a9ae4]`; `0x40fa88` frees them.
- **The loads.** For each weapon mount, `0x40df30` (hardpoint byte at most
  3) calls `0x40fab0`. It builds a memory stream on its frame with
  `0x47b36c(S, size, block)`, loads the root with `0x474c10` (`0x47a308`,
  then `0x47a144(S, 0)`) and drops the stream with the base destructor
  `0x47a8a4` (not `0x472f64`).
- **Load count.** The TS/AN readers load their children with `0x47a144` on
  the same stream, so every object of a weapon shape is a `dts_load` call on
  the memory stream: the 56 roots of `MECHWPNS.DTS` make 8,477 loads (24 to
  458 per root). Per load the stream sees 3 tells (header, entry, drain) and
  one read (the header only: the readers consume every body, the drain reads
  nothing).

## Replacement in the hook adapter

- **Stream operations.** A member stream of an identified archive (vtable
  `0x4a0e10`, handle `0xffff`) answers tell from its fields and reads
  through `es2_vfs_stream_fetch`: the member read of the stream model
  (resync, past-end count `length-pos+1` with status 2, the empty window of
  the archive's buffered stream B), with the bytes copied from the mapped
  archive file. A memory stream is served as own code (`mem_read`, `s_tell`
  in `core/src/asset_load.c`): `dts_load`'s header, tells and drain, and the
  reads of `bitmap_read` and `bitmap_array_read` when they get one. The
  read copies the bytes from guest memory, as the original does; EAX is the
  buffer with AL 1, or 0 for a short read or a set status. Two cases are
  left to the stream's own virtual call: a negative count, where the
  original's memcpy would run wild, and overlapping source and destination.
  Any other stream, or a state the models do not serve (B in an error
  state, a range outside the file, a memory cursor past its end), gets the
  stream's own virtual call (`+4`, `+0x18`) through the guest bridge with
  the call site's EAX/EDX. Each operation is decided on its own, so a mixed
  sequence is the original's.
- **Header peek.** For a memory stream the header is read without writing S:
  status, avail ≥ 8, or a partial header with status 2 after it.
- **Calls kept.** The factories of registry entries (so the TS/AN/palette/
  font/cursor/grid classes stay original code and nested hooks keep their
  modes), the class reads through the object's vtable, the nested loads of
  an array (`0x47a144`), new `0x4774f0` when the arena has no room, the
  close `0x47307c`, the decoders' `0x496406` and virtual destructors and the
  delete `0x477548` are guest calls in the original's order with its
  registers. The constructors `0x48156c`, `0x482f38`/`0x482f80`, the arena
  allocations, the registry lookup, the header and the drain are own code.
- **Frame.** Each hook call has its own guest scratch on the adapter's
  stack: the loader frame F (`[0x6c5964]` points to it during the load, with
  the same fields as the original's), the one-byte locals. The original's
  frames (`0x47a144`'s 0x1c bytes, the factories' exception records, the
  reads' local) are not written below the caller's ESP; no caller reads
  them.
- **Results.** EAX as the original; EDX = FS:0 at entry for the two
  factories, the base vtable `0x4a16e8` (or the delete's EDX) for
  `stream_destroy`, 0 for the others. ECX is not reproduced.
- **Buffered stream.** The replaced loaders serve member streams from the
  archive file: the original's archive buffer B stays the stream model's
  empty window (its bytes differ, its tell does not).

### Pass to the original

`dts_load` passes a call to the original (`bypassed`, replaced mode only)
when the stream or header is outside the models. The decision is made before
any write, from a copy of S (the header read on the copy, B untouched).
Each pass reports its reason to the observer; the adapter exports the keys
(`ES2_ASSET_PASS_*`) in the exit record next to the call counters:

| Counter | Key | `dts_load` passes to the original when |
| --- | ---: | --- |
| `hk_dts_load_pass_stream` | 1 | S is neither VOL-aware (vtable `0x4a0e10`) nor a memory stream (`0x4a1788`) |
| `hk_dts_load_pass_loose` | 2 | S is VOL-aware with a file handle (a loose file) |
| `hk_dts_load_pass_unserved` | 3 | the header read is in a state the models do not serve (the member read refuses it; a memory stream's cursor past its end) |
| `hk_dts_load_pass_tag` | 4 | the header's tag is not registered and S stays readable (the original's drain reads its unset frame dword) |
| `hk_dts_load_pass_fallback` | 5 | the tag is not registered and the fallback registry `[0x4a1694]` is in use |

Each pass counts exactly one of them, so `hk_dts_load_bypassed` minus their
sum is the number of calls the framework bypassed (calls inside another
hook's original phase). Compare mode reports nothing (its SKIPs count in
`skipped`). The fields are bounded like every callee counter (the bound case
of `exit_counters_fixture.c` and `path_counters_fixture.c` covers them).
None of the reasons is expected in the suites:

- **Stream class.** Only `0x4a0e10` and `0x4a1788` reach `0x47a144`.
- **Loose.** One loose open per suite (two loose reads, `0x496808`), none a
  shape or bitmap.
- **Unserved.** The VOL-aware `stream_read` hook has `bypassed` 0, and the
  header peek uses the same member path.
- **Tag.** The first object of every DBA/HBA/DB*/HB*/DPL/DFN/HFN/DCI/DTS/DGS
  member of the simulation archives has a registered tag (a scan of the CD
  archives), and the shape corpus walks without an unknown tag
  ([dts.md](../formats/dts.md)).
- **Fallback.** `[0x4a1694]` stays 0 (`PROVED`).

## Own process

The own process (`es2sim`) runs the shape loading as own code in
`sim/src/load.c`, registered in `sim/dispatch/services/mission_load.h`. It calls
`dts_load` and `stream_destroy` as hooks and implements:

| Function | VA | Service |
| --- | --- | --- |
| load by name, palette by name | `0x45cdf4`, `0x465f7c` | `load_by_name` |
| VOL-aware stream open | `0x472f14` | `vol_stream` |
| arena allocator | `0x47a288`, `0x47a2c8` | `arena_alloc` |
| shape-list header peek, class lookup, arena push/pop | `0x47a308`, `0x47a4a0`, `0x47a3fc`, `0x47a474` | inside the shape loaders |
| shape loaders | `0x474b30`, `0x474cd8`, `0x474de4` | `shape_root`, `shape_list`, `shape_at` |
| GLPalette factory and read | `0x4820d4`, `0x4821cc` | `palette_new`, `palette_read` |
| bitmap and array destructors | `0x4815b8`, `0x48349c` | `bitmap_destroy`, `bitmap_array_destroy` |
| TS/AN registration, factories and reads | `0x4913dc`, `0x48dea0`..`0x492164`, `0x48e02c`..`0x492244` | `an_classes`, `class_new_*`, `class_read_*` |
| part and shape reads | `0x48f9a0`, `0x48fea0`, `0x490e68`, `0x476710`, `0x490a34` | `part_read`, `part_list_read`, `shape_read`, `part_list_ctor`, `shape_ctor` |
| GridShape factory and read | `0x427568`, `0x42762c` | `grid_new`, `grid_read` |
| memory stream (constructor, tell, seek, read, write, status, destructor) | `0x47b36c`..`0x47b4b4`, `0x481aea`, `0x47a888`, `0x47a8a4` | `memory_stream_*`, `stream_base_*` |
| weapon shape cache | `0x40f998`, `0x40fa88` | `weapon_shapes`, `weapon_shapes_free` |

The hook adapter keeps the TS/AN/palette/font/cursor/grid classes as
original code (the factories of the registry entries are guest calls).

## Compare mode

All six accept compare. The replacement phase is pure: it writes only the
clones of its spans, the guest scratch, the stream's buffered stream B (a
region, `ARG_INDIRECT` S+0Ah, 0x21 bytes; not compared, the windows differ
by design) and, for `stream_destroy`, the construction counter (a region,
`RVA_INDIRECT [0x6cbee4]`, 4 bytes; also declared for the loads and the
factories, which leave it unchanged).

- `dts_load`, `bitmap_new`, `bitmap_array_new`, `bitmap_array_read` describe
  what the original must have built (`es2_asset_expect`, in load order:
  every bitmap field, its pixel and colour bytes as their source in the
  archive file, every array's count and children, an empty load as a null
  pointer) from S's clone, B's pre-state and the file. The comparators walk
  the original's objects from its EAX (from `[A+8]` for the array read) and
  compare every byte: vtable, fields (`+9` bits 0..4 only: bits 5..7 come
  from the allocator's memory), pixel and colour bytes, pointer arrays.
  Allocation addresses are not compared. S's clone and the counter are
  compared; EDX for the factories.
- `bitmap_read` writes its fields into the object's clone; the comparator
  compares the clone without the two pointers and the bytes the original's
  pointers lead to with the file's, and EAX.
- `stream_destroy`: S's clone, the counter, EAX and EDX.
- SKIP (the comparator accepts, `skipped` counts): a class the core does not
  model (shapes, palettes, fonts, cursors, grids), a stream the stream
  model does not serve, a memory stream (the only memory-stream loads are
  the weapon shapes, whose classes compare skips anyway; the declared
  region `S+0x0a` is not a pointer in this class), a read that delivers less
  than it asks inside a new object (whose pre-state the replacement cannot
  know), more than 1,024 objects, a decoder object, the delete flag or an
  owned handle in `stream_destroy`. The framework itself skips when S+0Ah is
  0.
- Hooks nested in a compared call's original phase are `bypassed`, so each
  compares where it is outermost: with `dts_load=compare` every bitmap load
  is compared at its outermost `dts_load`; with the four bitmap hooks in
  compare mode each compares on its own.

## In vitro

`tools/reference-game/invitro/assets_cases.h` (quick tier 8,000 direct cases
per hook, full tier 40,000), on the stream model's harness: the CD archives
behind DBSIM's file imports, the manager built by the original insert over
the nine simulation archives, the bump heap behind `_calloc`, plus the four
registrations by the original `0x47a0b8`, `_free` and a fake decoder's
destructor as logging stubs, an arena record over a test arena.

Inputs:

- every registered object at the top level of a member and every bitmap
  inside an array (2,890 members, 2,871 objects: 275 arrays, 2,222 bitmaps,
  shapes up to 64 KiB);
- 41 positions inside pixel data that read as a small pixel size and 1..64
  colours (the CD has no bitmap with colours);
- headers across a member end, the status preset, another stream moving B,
  shorter member lengths (bitmaps and arrays only), arenas with little room,
  loose and fresh streams, decoder objects and the delete flag for
  `stream_destroy`;
- one in four cases puts S on a memory stream over the member's bytes, with
  the cursor at the object (end cut short for bitmaps and arrays, status
  preset), and every `MECHWPNS.DTS` root as the cache keeps it. Quick tier:
  2,554 of the 8,000 `dts_load` cases, about 1,950 of 8,000 for the four
  bitmap hooks (full tier: 12,995 and about 10,000 of 40,000).

Compared per case: EAX (EDX for the factories and the destructor), the test
arena, the heap bytes allocated in the case, the stream model's state (B's
tells and status, file streams, file-system handles, counters, decoder log),
`[0x6c5964]`, `[0x6c5830]` and the stub logs.

Further passes:

- `formats/` against the original loader over the whole corpus: all 275
  arrays and 2,222 images (count, every field, pixel and colour bytes equal
  to `es2_dba_parse_prefix`/`es2_dba_image_at`); all 34 shape members, 264
  roots (the stream ends exactly where `es2_dts_walk` frames each root).
- Real-callee for `dts_load` and `bitmap_array_new` (the six hooks with
  `stream_read`, `stream_tell`, `stream_close` in all 512
  original/replaced combinations, 1,024 cases each).
- 256 installed 31/a7 poison cases per hook.
- Compare mode alone: 1,024 exact cases (0 mismatches); 1,024 skewed (a
  byte of S's clone flipped, one mismatch per compared call). The
  memory-stream cases are `skipped`.
- Pass reasons: 64 installed replaced cases per reason. `bypassed` equals
  the reason's counter and the other four stay 0; the original's nested
  loads are included (stream: 350 in the quick, 471 in the full tier).
  Stream (a copy of the VOL-aware vtable) and loose (status preset) cases
  are compared with the original run; unserved (B in an error state), tag
  and fallback reach the original's drain over stack garbage, so only their
  counters are checked.
- Weapon shape cache: all 56 roots through the installed hook against the
  original, equal, `bypassed` 0; 8,477 loads, 25,431 memory tells
  (`hk_dts_load_call_47b400`) and 8,477 memory reads (`_47b4b4`).

Branch sites: the 24 branch sites of the six functions and the helpers they
reproduce (`0x47a6b4`, `0x47a4a0`, `0x47a83c`, `0x47a288`, `0x47a2c8`) are
covered both ways, five proved one-way (the fallback registry, the two null
objects, the two re-tests of the decoders). For the memory stream `0x47b4c7`
and `0x47b4d7` are covered both ways; `0x47b3ac` (the cursor is at the end)
and `0x47b3b2` (the read passes 0) are proved one-way. One caught mutation
per hook (MUTATIONS.md). `//core:asset_load_test` (187 checks) covers the
contract on a synthetic archive, memory-stream loads, reads and passes and
one observer key per reason.

## Live checks

Replaced mode: every hook replaced, early arming and an RTL config with
the 49 CW3220 imports replaced, run with `tools/check.ps1 -Live
-Overlap -RtlConfig <rtl-config.ini>`. Compare runs add a `-HookModes` file
with the lines `dts_load=compare` and `stream_destroy=compare`, or
`bitmap_new=compare`, `bitmap_read=compare`, `bitmap_array_new=compare` and
`bitmap_array_read=compare`.

- **Replaced: all suites identical** to their goldens. No skipped,
  bypassed or mismatch counter of any hook or import is nonzero.
- `hk_dts_load_bypassed` and every `hk_dts_load_pass_*` are 0; the other five
  `assets` hooks have `_bypassed` 0. No loose file reaches `dts_load`.
- The memory stream is served by own code: `hk_dts_load_call_47b4b4` counts
  the weapon-shape loads and `hk_dts_load_call_47b400` is three times that
  count (2,320 and 6,960 in the c1_01 suites; 8,966 and 26,898 in
  c3_09-loadout-b; 5,464 and 16,392 in the c4_08 suites). `hk_dts_load_calls`
  is 20,677 in c1_01-success and 33,693 in c3_09-loadout-b (nested loads
  included); `bitmap_new`/`bitmap_read` 1,061, `bitmap_array_new`/`_read` 70
  and `stream_destroy` 307 in c1_01-success.
  `hk_bitmap_read_call_47b4b4` and `hk_bitmap_array_read_call_47b4b4` are 0
  (no bitmap is read from memory).
- **Compare** (`dts_load`, `stream_destroy`, and the four bitmap hooks): all
  suites identical, 0 mismatches. The nonzero counters are
  `dts_load_skipped` (classes not modelled and the memory-stream loads),
  `stream_destroy_skipped` (owned handles) and the nested hooks' `_bypassed`
  inside the compared calls' original phases.

## Open

- The virtual destructor call sites (`[vtable+0]` with flag 3) are not
  enumerated for register use.
- The cursor class (`0x483374`, registry entry 8) and the other GL/TS/AN
  classes stay original in the hook adapter; `dts_load` calls their
  factories.
- Compressed bitmaps (compression 1/3) and colour tables have no CD example;
  the read stores them unchanged, in vitro only on synthetic positions.
