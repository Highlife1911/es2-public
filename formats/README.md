# ES2 formats library

Portable C17 readers for the formats described by the repository's
[VOLN](../docs/formats/vol.md), [DPL](../docs/formats/dpl.md),
[DBM/DBA/HBA](../docs/formats/dbm.md),
[MSN](../docs/formats/mission.md), [DTS](../docs/formats/dts.md),
[DAT](../docs/formats/dat.md) and
[COL/DMG/GL/PDG](../docs/formats/collision.md)
documents. The packed string files and the configuration files have their own
libraries `//formats:str` ([STR](../docs/formats/str.md)) and `//formats:cfg`
([prefs.cfg, sound.cfg, keyjoy.cfg](../docs/formats/cfg.md)). Those documents
are the implementation specification; the headers in
`include/es2/formats/` carry each function's contract. No original asset bytes
are included in the library or tests. The [BND](../docs/formats/bnd.md) API is
opaque inspection only: its original reader and grammar are unresolved.

## Targets

| Target (`//formats:...`) | What it is |
|---|---|
| `formats` (`//formats`) | the library: `common`, `vol`, `dpl`, `dbm`, `msn`, `dts`, `dat`, `vfs`, `collision`, `bnd` |
| `str`, `cfg` | string files; configuration files |
| `script` | prepared script framing ([below](#prepared-scripts)) |
| `player_mec`, `player_mec_tool` | `player.mec` reader/writer; its CLI ([below](#playermec-templates)) |
| `sav` | SAV framing, writer, edit and compose ([mission.md](../docs/formats/mission.md#complete-sav-framing)) |
| `dfn` | DFN/HFN fonts ([dfn.md](../docs/formats/dfn.md)) |
| `sha256` | SHA-256 for pins and manifests |
| `es2fmt` | the corpus CLI (the only target with file I/O) |

Tests: `formats_test` (synthetic), `str_cfg_test`, `script_test`,
`player_mec_test`, `sav_test`, `vfs_dbsim_test`, `dfn_test`, `sha256_test`
(synthetic); `str_cfg_real_test`, `script_real_test`, `player_mec_real_test`,
`sav_real_test`, `vfs_dbsim_real_test`, `dfn_real_test`, `dbm_real_test`
(`ES2_DATA_ROOT`, read only; they skip without it). Every test also has a
`_wasm` variant.

## Build and test

From the repository root, call the pinned Bazel executable by its full path.
The default target platform is Windows x86; the registered custom toolchain
uses the existing LLVM-MinGW SDK, without Visual Studio auto-configuration.
The library and the CLI build with `-std=c17 -Wall -Wextra -Werror -O2`
([build system](../docs/build_system.md)).

```powershell
& bazel build //formats/...
& bazel test //formats/...

# Windows x86_64, using the same source targets:
& bazel test //formats/... --platforms=//bazel/platforms:windows_x86_64

# The corpus check:
& bazel run //formats:es2fmt -- --real 'build\extracted\cd'
& "$(bazel info bazel-bin)\formats\es2fmt.exe" --real 'build\extracted\cd'
```

### The `es2fmt` CLI

With no arguments, `es2fmt` uses a nonempty `ES2_DATA_ROOT` or prints an explicit
skip and exits successfully; `--help` lists the options. It prints only
statistics and failure diagnostics, never asset contents or decoded files. It
recursively scans `.VOL` files and loose `DATA` files, checks the expected
archive identities and member totals, aggregates extensions and format results,
and compares the documented reference counts. Exit codes: 0 for a matching
corpus (including the two documented failures) or an explicit skip, 1 for
mismatches or read/parse failures, 2 for invalid arguments. This is a
statistics comparison, not hash-based authentication of the CD.

The CLI retains archive buffers and loose DATA bytes until VFS reporting
finishes (about 84 MB for the reference corpus), uses `fseek`/`ftell` (file-size
limit `LONG_MAX` on this toolchain) and limits directory recursion to 64 levels.
These are CLI limits, separate from the memory-span API.

## API conventions

Include headers from `formats/include`, for example
`#include "es2/formats/vol.h"`, and link `libes2formats.a` after caller object
files. All library entry points operate on memory supplied by the caller.
The library has no file I/O, Win32 or SDL dependency, mutable global state,
heap allocation, or allocator callback: its storage model is **zero allocation**
(borrowed spans, small value descriptors, optional caller arrays, a bounded DTS
callback traversal).

- `es2_span` is a byte pointer and a `size_t` length. A null pointer is valid
  only for a zero-length span. Input memory must contain the declared number of
  bytes and stay alive and unchanged while views are used.
- Integers are read explicitly in little-endian order, never through struct
  casts or `fread` into structures.
- Functions return `es2_status`; an optional `es2_error *` receives a code,
  offset and static message, to be consulted only on failure. Reader errors are
  sticky; failed reads preserve the cursor and destination.
- Parse outputs are valid only after `ES2_OK`: the top-level parsers assign
  their output descriptor only on success, except DTS, which returns partial
  counts on failure (discard them and any tree built by earlier callbacks).
  Caller arrays and decode buffers can be partially filled on error.
- Error offsets are diagnostic locations: input-relative for MSN/DTS and most
  top-level parsing, span-relative for readers over nested spans, the requested
  index for invalid-index errors.
- Counts do not imply semantic validity of cross-references unless a header
  says so.

## Contracts the headers do not spell out

- **VOLN.** Opening validates the complete directory and member tables, raw
  kind-2 blocks and their repeated-byte suffixes, nonoverlap and complete
  coverage of the archive body (pairwise checks, quadratic in the member count;
  lookup is linear). Duplicate paths after ASCII folding are rejected; unsorted
  member records and unused directory records are allowed; header flag values
  are preserved, not imposed. `es2_vol_find` folds ASCII case and treats `/` and
  `\` alike by API policy; it implements no archive precedence or patch
  selection.
- **DPL, DBM, DBA.** DPL colour records are RGB and flags, not RGBA, and no
  colour conversion is performed. The audited CD images all use raw storage;
  RLE is tested with synthetic streams only. `CORNERS.DBA` fails strict parsing
  (below); `es2_dba_parse_prefix` counts its 24 declared images and exposes the
  606-byte trailer, which is never added as image children.
- **MSN, DTS.** MSN returns the 17 lists in file order; the eighth list (index 7)
  has stride zero and keeps complete variable route records (ten-byte prefix,
  counted two-byte entries); objectives, triggers and scripts are not
  interpreted. DTS supports all 22 documented object classes, accepts any known
  root class (the narrower root selection seen in game assets is not enforced)
  and calls the visitor in **postorder**; copy a descriptor if it is needed after
  the callback. The depth cap of 256 is an implementation limit.
- **DAT.** There is no shared signature: select an explicit `es2_dat_kind` or
  classify a full path (`dat/WEAPONS.DAT` and `GAM/WEAPONS.DAT` are different
  grammars; the basename selects neither; the loose `DATA/MAT0.DAT` and
  `DATA/MFORMS.DAT` are recognised). Vehicle values stay serialized values. Four
  support levels: `ES2_DAT_READER_FRAMING` (the original reader's framing),
  `ES2_DAT_DATA_FIT_ONLY` (`THEATER`, `MAP_INFO`, `MAP_LOCS`, `TEX`, `STYLES`
  and the `TRN_HERC` sequence: exact byte accounting, no original consumer
  established), `ES2_DAT_EXTERNAL_COUNT` (`BASECOL` needs the parsed `BASES`
  count, `FIRE` a payload-byte count from outside the DAT; pass them via
  `external_count`, never inferred from a header value) and
  `ES2_DAT_PREFIX_WITH_SUFFIX` (`MAT0`: counted eight-byte records, the rest in
  `suffix`). A seven-slot loadout can contain slot ID 7: the record count is not
  the bound of its ID domain. Debris uses the nested group grammar even for the
  small files that also fit a flat arrangement. FIRE's check with external count
  four is conditional (the original source of the count is unverified); parsing
  a candidate or conditional layout is not proof of original behaviour.
- **VFS.** `es2_vfs_open` is "first source wins" in caller order: API policy, not
  game behaviour. The original policy is the opt-in
  `es2_vfs_open_dbsim_order` ([vol.md](../docs/formats/vol.md#original-registration-and-lookup-order)):
  carried displacement, not a stable priority sort (`A5,B5,C10` becomes
  `C10,B5,A5`), mask `1` for DBSIM and `0x100` for VSHELL, at most 30 accepted
  archives; the caller expands `vol\*.vol` over the original two root passes,
  suppresses registered filenames and omits failed opens. All `source_index`
  values refer to the input array. Original archive lookup requires `\`.
- **Collision.** No extension-only autodetection: PDG has two explicit schemas
  (vehicle views/rectangles/weapon-display slots, and the WEAPONS pair table).
  COL rejects sphere counts above 4095 (the original narrows `8*S` to a signed
  16-bit stream-read length). `es2_col_validate_regions`,
  `es2_gl_validate_regions` and `es2_dmg_validate_references` need companion
  counts from the caller; `es2_gl_validate_capacities` checks the second DMG
  mapping used during weapon augmentation. GL display slots are sparse relative
  to PDG (additional GL slots are legal); `es2_gl_find_display_slot` matches in
  the original direction (visible PDG ordinal to GL).
- **BND.** `es2_bnd_inspect` success means only that an opaque view was
  returned: no minimum length, tag, timestamp or count is required.
- **SAV.** VSHELL rewrites a SAV without truncating it, so bytes after
  `reader_end` are a stale tail of an older, longer file
  ([mission.md](../docs/formats/mission.md#sav-write-sequence-and-stale-tail)).
  `es2_sav_write_over` and `es2_sav_overlay` model the original's file update
  (the edited image followed by the previous file's bytes beyond it). Writers
  reject output overlap and insufficient capacity before the first byte;
  replacement records are validated against the loader-safe domain in `sav.h`
  ([mission.md](../docs/formats/mission.md#sav-editing-and-building)). This is
  framing support, not a semantic campaign editor.
- **DFN/HFN.** One `GLFont` member (tag `05 00 28 00`) with per-byte advance,
  glyph bitmap and text width as VSHELL's `getWidth` `0x4010fd` and string
  width `0x4011af` compute them; codes without a glyph are `ES2_ERR_NOT_FOUND`.

## Prepared scripts

[`script.h`](include/es2/formats/script.h) is the library `//formats:script`: the
[prepared script framing](../docs/formats/mission.md#prepared-script-exact-framing) of
`script.dat` and `script<n>.dat`. It reads the ten header words and the 13 signed-count
lists with their strides, refuses negative counts, truncation and route references outside
the coordinate count, and reports the bytes after the last list as `trailing` (a stale tail of
VSHELL's non-truncating rewrite). `script_real_test` reads every
`../../installs/*/SAV/script<n>.dat` beside the CD (`ES2_SCRIPT_INSTALLS` overrides),
checks the counts of the two documented examples and their exact end, and prints each distinct file.

## player.mec templates

[`player_mec.h`](include/es2/formats/player_mec.h) is the library
`//formats:player_mec`, depending on the common/DAT readers. It reads
the [documented writer grammar](../docs/formats/mission.md#playermec), rejects
negative/truncated counts and trailing bytes, and preserves the header, pilot
words, 68-byte record tails and all 33 suffix flags. `es2_mec_write` supports a
size query and selected record edits: unedited records round-trip exactly,
edits keep secondary values by slot ordinal, use an explicit caller-supplied
value for new slots and preserve opaque tails; record count and order cannot
change; output must not overlap borrowed input.

`//formats:player_mec_tool` builds the standalone CLI:

```text
player_mec_tool show FILE [CD_ROOT]
player_mec_tool diff BEFORE AFTER
player_mec_tool roundtrip FILE [CD_ROOT]
player_mec_tool set TEMPLATE NEW_OUTPUT CD_ROOT --record INDEX VEHICLE ID,ID,... [--record ...]
```

Indices are zero-based; `none` is an empty item list. `show` prints each
vehicle, K, item IDs and secondary words; `roundtrip` compares in memory and
writes no file. `set` refuses an existing output and uses 5 for new secondary
words (the VSHELL absent-slot default; their meaning remains a
hypothesis). The generator reads `VOL/SHELL0.VOL` at runtime (HERC_INF, all nine
INI loadouts, shell `GAM/WEAPONS.DAT`; no shell tables are embedded) and
validates the result before opening output. Missing IDs, zero team records and K
above the shell's ten-slot storage are errors; K differing from HERC_INF +6 and
nonzero item/slot pairs absent from the INI examples produce hypothesis
warnings, not demonstrated mount restrictions. The caller reviews condition data
and mission team compatibility in game.

`diff` compares by record ordinal and also reports added/removed records and
header, pilot, opaque-tail and suffix-flag differences, without interpreting
them. Exit status 0 for byte identity, 1 for differences, 2 for
invalid/unreadable inputs.

`player_mec_real_test` reads the four sibling
`../../installs/timing-r111/SAV/{player0.mec,player1.mec,player10.mec,player11.mec}`
examples, validates them against the CD tables and round-trips in memory
(`ES2_MEC_EXAMPLES` overrides the directory). `DATA/player.mec` is not read: a
live regression replaces the clone's `DATA/` while it runs, and between runs
that file is byte-identical to `SAV/player10.mec` (215 bytes; the pin is in
`tools/reference-game/shell-invitro/reference_pins.py`). The test never
executes, exports or writes original data. Keep generated MEC files only in
ignored scratch or `es2-data/oracle/launch`, never git. Full workflow:
[oracle_suites.md](../docs/reference-game/oracle_suites.md).

## What the checks prove

| Check | Proves |
|---|---|
| `formats_test` (synthetic) | every parser against fixtures built in C: valid records, every shorter prefix of representative files, length and count mutations, offset/overlap errors, bad tags and names, capacity, unsupported encodings, RLE edge cases, callback cancellation, traversal limits, DAT and VFS behaviour, the collision grammars, BND's opaque boundary |
| `es2fmt --real` | the library against the reference CD: archive identities and member totals, every documented DTS tag count, DAT family totals, COL/DMG/GL/PDG framing of every member with their cross-file index checks, BND as opaque only |
| `vfs_dbsim_test` | ties in both orders, carried displacement, unsigned priorities, mask bits, loose overrides, filtering and provenance, case and separators, missing entries, capacity and unchanged failure outputs |
| `vfs_dbsim_real_test` | all 13 VOLs, their duplicate totals, the equal-priority SHELL0/SIMVOL0 `dfn\font.dfn` pair and all ten differing paths in both process views under 26 enumeration orders |
| `sav_real_test` | the four saves of a snapshot copy of the SAV directory (`ES2_SAV_DIR`; never an installation) round-trip exactly in memory, including GAME_R's ten-byte tail; the edit/compose identity rewrites and the overwrite identities. No save is written |
| `dfn_test`, `dfn_real_test` | synthetic fonts; the CD's 36 DFN and 26 HFN members, the shell fonts resolved with VSHELL's VOL order, metrics, ink indices and the `ESTEXT.BIN` byte coverage. No glyph bytes are kept |
| `str_cfg_real_test` | the CD's string and configuration files |

Known corpus exceptions, recognised explicitly and kept in the format totals:
`DEMO2.MSN` fails at byte 8,168 of 8,184 (the last required 58-byte record has
only 16 bytes) and `CORNERS.DBA` fails exact consumption at byte 1,724 of 2,330.
A matching corpus exits zero while reporting BND as unresolved; zero is not a
claim that every extension has a recovered grammar.

VFS duplicates over the 13 `VOL/` archives (sorted lexicographically, the three
alternative `VER31` voice archives excluded): 567 duplicate paths, 557 with all
copies identical and 10 differing. All ten differing paths pair a priority-`0a`
archive with priority-`05` archives
([vol.md](../docs/formats/vol.md#duplicate-paths-across-archives)), so the
default caller order is not the explicit original-order policy. The unique-path
counts of the combined loose-first views are measurements under those caller
policies, not original-loader results; both loose DAT files compare
byte-for-byte equal to their archive counterparts.
