# ES2 formats library

Portable C17 readers for the formats described by the repository's
[VOLN](../docs/formats/vol.md), [DPL](../docs/formats/dpl.md),
[DBM/DBA/HBA](../docs/formats/dbm.md),
[MSN](../docs/formats/mission.md), [DTS](../docs/formats/dts.md), and
[DAT](../docs/formats/dat.md), and
[COL/DMG/GL/PDG](../docs/formats/collision.md)
documents. The packed string files and the configuration files have their own
libraries `//formats:str` ([STR](../docs/formats/str.md)) and `//formats:cfg`
([prefs.cfg, sound.cfg, keyjoy.cfg](../docs/formats/cfg.md)); tests
`//formats:str_cfg_test` (synthetic) and `//formats:str_cfg_real_test`
(`ES2_DATA_ROOT`, read only). Those documents are the implementation
specification. No original asset bytes are included in the library or tests.
The [BND](../docs/formats/bnd.md) API is opaque inspection only: its original
reader and grammar are unresolved.

### player.mec templates

[`player_mec.h`](include/es2/formats/player_mec.h) is a separate C17 library
target `//formats:player_mec`, depending on the common/DAT readers. It reads
the [documented writer grammar](../docs/formats/mission.md#playermec), rejects
negative/truncated counts and trailing bytes, and preserves the header, pilot
words, 68-byte record tails and all 33 suffix flags. It allocates nothing.
`es2_mec_write` supports a size query and selected record edits. Unedited
records round-trip exactly; edits keep secondary values by slot ordinal,
use an explicit caller-supplied value for new slots, and preserve opaque tails.
Record count/order cannot be changed. Output must not overlap borrowed input.

`//formats:player_mec_tool` builds the standalone CLI:

```text
player_mec_tool show FILE [CD_ROOT]
player_mec_tool diff BEFORE AFTER
player_mec_tool roundtrip FILE [CD_ROOT]
player_mec_tool set TEMPLATE NEW_OUTPUT CD_ROOT --record INDEX VEHICLE ID,ID,... [--record ...]
```

Indices are zero-based. `none` is an empty item list. `show` prints each
vehicle, K, item IDs and secondary words. `roundtrip` compares in memory and
writes no file. `set` refuses an existing output and uses 5 for new secondary
words (the confirmed VSHELL absent-slot default); their meaning remains a
hypothesis. The generator reads `VOL/SHELL0.VOL` at runtime, parses HERC_INF,
all nine INI loadouts and shell `GAM/WEAPONS.DAT`, and validates the result
before opening output. Missing IDs, zero team records and K above the shell's
ten-slot storage are errors. K differing from HERC_INF +6 and nonzero item/slot
pairs absent from the INI examples produce explicit hypothesis warnings;
neither is a demonstrated mount restriction. No shell tables are embedded.
The caller must review condition data and mission team compatibility in game.

`diff` compares by record ordinal, showing before/after vehicle, K, items and
secondary words. Added/removed records are explicit; header, pilot, opaque-tail
and suffix-flag differences are also reported (without interpreting them).
Exit status is 0 for byte identity, 1 for differences, 2 for invalid/unreadable
inputs. Use it before sealing a generated launch and compare the displayed
loadout with what is actually observed in VSHELL/the mission.

`player_mec_test` uses synthetic data only. `player_mec_real_test` skips when
`ES2_DATA_ROOT` is unset; with the CD root it reads the four sibling
`../../installs/timing-r111/SAV/{player0.mec,player1.mec,player10.mec,player11.mec}`
examples, validates them against the CD tables and round-trips in memory.
`DATA/player.mec` is not read: a live regression replaces the clone's `DATA/`
while it runs, and between runs that file is byte-identical to
`SAV/player10.mec` (215 bytes, SHA-256
`f669d1250486b6e1e3ff9ed18370f0bc14cdc60793fed8caaf4b49cb78174984`, the pin in
`tools/reference-game/shell-invitro/reference_pins.py`).
`ES2_MEC_EXAMPLES` can override that measurement-clone directory. The test
never executes, exports or writes original data. Keep generated MEC files only
in ignored scratch or `es2-data/oracle/launch`, never git. Full workflow:
[oracle_suites.md](../docs/reference-game/oracle_suites.md).

## Build and test

### Bazel

From the repository root, call the pinned Bazel executable by its full path.
The default target platform is Windows x86; the registered custom toolchain
uses the existing LLVM-MinGW SDK, without Visual Studio auto-configuration.

```powershell
& bazel build //formats/...
& bazel test //formats/...

# Windows x86_64, using the same source targets:
& bazel build //formats/... --platforms=//bazel/platforms:windows_x86_64
& bazel test //formats/... --platforms=//bazel/platforms:windows_x86_64
```

The targets are `//formats` (the library), `//formats:es2fmt` (the CLI), and
`//formats:formats_test` (synthetic tests). Both build with
`-std=c17 -Wall -Wextra -Werror -O2`. `formats_test` runs synthetic fixtures
only; the corpus client is the separate `es2fmt` executable.
`player_mec_real_test` and the other real-data tests are gated by
`ES2_DATA_ROOT` and otherwise skip.

To run the corpus check explicitly:

```powershell
& bazel run //formats:es2fmt -- --real
& bazel run //formats:es2fmt -- --real 'build\extracted\cd'
& bazel run //formats:es2fmt --platforms=//bazel/platforms:windows_x86_64 -- --real 'build\extracted\cd'
```

Pinned Bzlmod dependencies are fetched on demand. Bazel's server, caches and
build outputs stay under `build\bazel`, as configured in
`.bazelrc`. Workspace output symlinks are disabled by `--symlink_prefix=/`.
Locate the actual binary directory with:

```powershell
& bazel info bazel-bin
& bazel info bazel-bin --platforms=//bazel/platforms:windows_x86_64
```

The toolchain configuration is described in
[the build decision](../docs/decisions/0001-build-system.md).

### The `es2fmt` CLI

```powershell
$bin = & bazel info bazel-bin
& "$bin\formats\es2fmt.exe" --real 'build\extracted\cd'
```

With no arguments, `es2fmt` uses a nonempty `ES2_DATA_ROOT` or prints an explicit
skip and exits successfully. `--help` explains these options. It prints only
statistics and failure diagnostics, never asset contents or decoded files.
It recursively scans `.VOL` files and loose `DATA` files, checks the expected
archive identities and member totals, aggregates extensions and format results,
and compares the documented reference counts. A different or incomplete corpus
reports mismatches. Exit codes are 0 for a matching corpus (including the two
documented failures) or an explicit skip, 1 for mismatches/read/parse failures,
and 2 for invalid arguments. This is a statistics comparison, not hash-based
authentication of the CD.

The CLI retains archive buffers and loose DATA bytes until VFS reporting
finishes (about 84 MB for the reference corpus), uses `fseek`/`ftell` (file-size
limit `LONG_MAX` on this toolchain) and limits directory recursion to 64 levels.
These are CLI limits, separate from the memory-span API.

## API conventions

Include headers from `formats/include`, for example
`#include "es2/formats/vol.h"`, and link `libes2formats.a` after caller object
files. All library entry points operate on memory supplied by the caller.
The library has no file I/O, Win32 or SDL dependency, mutable global state,
heap allocation, or allocator callback. Its storage model is **zero allocation**:
borrowed spans, small value descriptors, optional caller arrays, and a bounded
DTS callback traversal. The CLI alone uses file/directory I/O and allocation.

- `es2_span` is a byte pointer and a `size_t` length. A null pointer is valid
  only for a zero-length span. Input memory must really contain the declared
  number of bytes and remain alive and unchanged while views are used.
- Integers are read explicitly in little-endian order, without struct casts,
  alignment assumptions, or `fread` into structures. Signed reads handle the
  full 16-bit and 32-bit ranges without unsigned-to-signed overflow conversion.
- Functions return `es2_status`. Optional `es2_error *` arguments receive a
  code, offset, and static short message; the caller must not free the message.
  Check the return status, and consult the error only on failure. Some successful
  accessors leave an earlier error untouched.
- Reader errors are sticky. Failed reads preserve the cursor and destination.
  `es2_reader_end` rejects remaining bytes. `es2_subspan` bounds a byte range
  without copying it.
- Parse outputs are valid only after `ES2_OK`. Top-level VOLN, DPL, DBM/DBA,
  MSN, DAT, and VFS parsers assign their output descriptor only on success. DTS
  instead returns partial counts on failure; discard them and any tree built by
  earlier callbacks. Caller arrays and decode buffers can be partially filled
  on error.
- Error offsets in MSN/DTS and most top-level structural parsing refer to the
  supplied input. Common readers and accessors over nested spans can report a
  span-relative offset; invalid-index errors can report the requested index.
  They are diagnostic locations, not a universal absolute file-offset contract.
- Use descriptors returned by successful parsing. Their public fields expose
  information for callers, not an alternative unchecked construction interface.
  Counts do not imply semantic validity of cross-references unless stated below.

## Modules

| Module | Main operations | Validation and returned data |
|---|---|---|
| `common` | Reader initialization, integer/span reads, skip, end | Bounds, sticky errors, little-endian conversion; no allocation |
| `vol` | `es2_vol_open`, `directory_at`, `member_at`, `find`, `payload`, `read_tables` | Directory/member tables, names, offsets, payload spans, block kind/metadata |
| `dpl` | `es2_dpl_parse`, `colour_at`, `ramp_at` | Colour records and variable ramps, including palette-index bounds |
| `dbm` | `es2_dbm_parse`, `es2_dba_parse`, `es2_dba_image_at`, `es2_dbm_decode` | Image headers, stored pixels/colours, array children, optional caller-buffer decode |
| `msn` | `es2_msn_parse` | Version 5 and all 17 ordered list descriptors |
| `dts` | `es2_dts_walk`, `tag_at`, `tag_name` | Object body/encoded spans, child runs, parent/root/depth information and tag counts |
| `dat` | Explicit schema selection, `es2_dat_parse`, record access and typed getters | Separate simulator/shell tables, nested counts, confirmed fields and opaque spans |
| `vfs` | Caller-order or explicit original DBSIM-order memory sources, lookup and enumeration | Unique paths, every duplicate copy, source provenance and byte equality |
| `collision` | COL/DMG/GL/PDG parsers and typed record accessors | Nested counts, exact EOF, internal damage/color indices; optional companion-region validation |
| `bnd` | `es2_bnd_inspect` | Whole opaque span and optional four-byte prefix candidate; **no grammar validation** |
| `sav` | `es2_sav_parse`, accessors, `es2_sav_write`, `es2_sav_edit`, `es2_sav_compose` | SAV framing, see [SAV](#sav-framing-and-round-trips) |
| `dfn` | `es2_dfn_*` | DFN/HFN fonts, see [DFN/HFN fonts](#dfnhfn-fonts) |

### VOLN

Opening an archive validates the complete directory and member tables, exact
table lengths, directory/name termination, directory indices, bounded raw
kind-2 blocks, their repeated-byte suffixes, nonoverlap, and complete coverage
of the archive body. Duplicate paths after ASCII folding are rejected. Unsorted
member records and unused directory records are allowed. Observed header flag
values are preserved rather than imposed as a required constant. Bytes after
the first NUL in a filename slot are ignored.

`es2_vol_find` compares a full directory-plus-filename path using ASCII case
folding; `/` and `\` are equivalent by API policy. It does not implement original
VFS archive precedence or patch selection. Returned names exclude the NUL;
directory spans retain the trailing backslash. `es2_vol_read_tables` materializes
descriptors into caller arrays sized from `directory_count` and `member_count`.
Opening uses pairwise overlap/duplicate checks with quadratic member-count cost;
lookup is linear and directory access scans the small borrowed string table.

### DPL and bitmaps

DPL colour records contain RGB and flags, not RGBA. No colour conversion is
performed. Ramp indices are borrowed little-endian words; use the common reader
to access them. Exact consumption and every index bound are checked. Counts and
reserved fields are retained without imposing the corpus's observed values.

DBA and HBA share the documented array grammar. Strict DBM/array parsing checks
tags, declared sizes, child counts, exact consumption, and zero alignment bytes.
Known raw 8-bit/layout-0 images must contain exactly `width * height` pixel bytes.
Other image encodings may be structurally parsed as bounded stored spans, but
that does not claim decoded-pixel validity. Compressed spans require the four-byte
uncompressed-size prefix.

`es2_dbm_decode` supports raw 8-bit/layout-0 pixels and the fully documented
kind-1 RLE grammar. Kind 3 (LZH), other compression kinds, other bit depths and
other layouts return `ES2_ERR_UNSUPPORTED`. Decoding writes into caller storage
that must not overlap the stored bytes. On success, `written` and
`stored_consumed` expose the result; for RLE, the latter includes the size prefix
and can reveal unused trailing stream bytes. A final run can be partially consumed
when the requested output is complete. On failure both counts are zero, but the
buffer may contain partial output and must be discarded. RLE is tested with
synthetic streams; the audited CD images all use raw storage.

`es2_dba_parse_prefix` is an explicit diagnostic alternative to strict parsing.
It returns the declared array and a separate `trailing` span. For `CORNERS.DBA`,
the CLI reports the strict failure, then uses the prefix operation to count the
24 declared images and expose the 606-byte trailer. It never silently adds that
trailer as image children.

### MSN and DTS

MSN parsing returns the 17 lists in file order, excluding version/count fields
from each list's record span. Fixed lists expose their documented stride;
zero-based index 7 (the eighth list) uses stride zero and retains complete
variable route records, including the ten-byte prefix and the counted two-byte
entries. Negative counts, truncated records, unsupported versions, and trailing
bytes fail. Objectives, triggers, scripts, and references are not interpreted
by this framing API.

DTS walking supports all 22 documented object classes, including classes absent
from the audited corpus. Each declared body and nested child run must be consumed
exactly. Multiple roots are supported. The generic API accepts any known root
class; it does not enforce the narrower root selection observed in game assets.
Unsupported tags fail. Object alignment is checked against the stream boundary;
no DTS pad-byte value is imposed.

The visitor receives temporary descriptors in **postorder**: children before
their parent. Copy a descriptor if needed after the callback; all its spans still
borrow the input. A null visitor collects statistics only. A callback's nonzero
status becomes `ES2_ERR_CALLBACK`. `max_depth` counts roots as depth zero and must
not exceed the implementation cap of 256 (an implementation limit, not a claim
about the file format). `children[2]` can distinguish ANShape's part run from its
final animation-list child. Geometry, material, animation and transform
references are not semantically validated.

### DAT tables

DAT has no shared signature or universal record layout. Select an explicit
`es2_dat_kind`, or use `es2_dat_classify` on a full resource path. In particular,
`dat/WEAPONS.DAT` and `GAM/WEAPONS.DAT` select different grammars; the basename
alone selects neither. The classifier also recognizes the two loose
`DATA/MAT0.DAT` and `DATA/MFORMS.DAT` paths. This selects a parser, not a VFS alias.

`es2_dat_parse` checks counts, nested boundaries and exact consumption, returning
borrowed primary/secondary record spans, counts and support-level metadata.
`es2_dat_record_at` finds fixed or variable records without allocating a table.
Typed getters expose confirmed scalar fields. Unknown meanings use neutral
offset names, raw bit patterns or opaque spans. Vehicle values remain serialized
values: parsing does not apply the simulator's speed conversion or overwrite
file fields with runtime state.

The support levels separate four cases:

- `ES2_DAT_READER_FRAMING`: framing supported by the documented original reader.
- `ES2_DAT_DATA_FIT_ONLY`: the five candidate layouts (`THEATER`, `MAP_INFO`,
  `MAP_LOCS`, `TEX`, `STYLES`) and the `TRN_HERC` record sequence. Exact byte
  accounting does not establish their original consumers or field semantics.
- `ES2_DAT_EXTERNAL_COUNT`: `BASECOL` needs the previously parsed `BASES` record
  count; `FIRE` needs a payload-byte count from outside the DAT. Pass these via
  `external_count`. It is not inferred from a convenient header value.
- `ES2_DAT_PREFIX_WITH_SUFFIX`: `MAT0` exposes the counted eight-byte records and
  all remaining bytes in `suffix`. The suffix is not counted as more records.

Counts normally come from the file; observed corpus counts are checked by the
CLI, not imposed as general format limits. The fixed 33-item DAMAGE consumer and
nine-group hotspot contract are enforced. Bounds and exact consumption reject
negative/oversized counts and unexpected tails, except for MAT0's explicitly
exposed suffix. A seven-slot loadout can contain slot ID 7: the number of
records is not the upper bound of its ID domain. Debris uses the nested group
grammar even for the small files that also fit a flat arrangement.

The CLI parses every archive DAT member and the two loose DAT files once. It
derives BASECOL's external count from parsed BASES. FIRE's check with external
count four is conditional: the original source of that count is unverified. It
reports both MAT0 suffixes and compares their 136-byte lengths, without
interpreting or printing their contents. Parsing a candidate or conditional
layout successfully is not proof of original behavior.

### Read-only VFS

`es2_vfs_open` borrows an ordered array of `es2_vfs_source` values. Each source
is either a successfully opened VOLN archive or a caller-provided array of
`es2_vfs_file` path/span pairs. **First source wins** is the default policy.
Changing source order changes precedence; loose files receive no implicit
preference, and archive flags/priority bytes are not interpreted. The explicit
original policy below is opt-in.

`es2_vfs_find` compares full relative paths with ASCII case folding and slash
equivalence. It never falls back to a basename. API paths must contain at least
one separator and nonempty components. Absolute paths, colons, repeated or
trailing separators, `.`/`..` components and non-printable/non-ASCII bytes are
rejected rather than normalized. Duplicate folded paths within one loose source
are rejected; copies in different sources remain visible. Empty source lists
and zero-byte loose files are supported.

`es2_vfs_open_dbsim_order(sources, count, mask, order, capacity, &vfs, &error)`
adds the [original registration policy](../docs/formats/vol.md#original-registration-and-lookup-order).
Supply sources in the actual directory enumeration/registration order and
caller-owned `size_t order[count]` storage. The helper keeps loose sources
first, tests the complete little-endian header use mask (normally `1` for
DBSIM, `0x100` for VSHELL), and inserts accepted archives by unsigned priority.
It uses carried displacement: equal slots stay in place, but a later higher
priority can rotate older equal-priority archives. For example, `A5,B5,C10`
becomes `C10,B5,A5`, not a stable priority sort. All returned `source_index`
values still refer to the original input array, including filtered gaps.

The caller expands `vol\*.vol` across the original two root passes, suppresses
already registered archive filenames as documented, and omits failed opens.
The helper performs no I/O and borrows all storage; it rejects more than 30
accepted archives and leaves `out` unchanged on failure (order scratch may be
partial). It builds a fresh view, not the original manager's refresh/removal
behavior. Original-order archive requests fold ASCII case but require `\`;
`/` can resolve supplied loose files, while the default API keeps slash aliases
for all sources. Enumeration groups canonical folded paths; its winning copy
is the winner for the corresponding backslash query.

`es2_vfs_enumerate` fills caller-provided entry and copy arrays. Allocating each
to `vfs.file_count` entries is sufficient; the unique-entry array can be smaller
when its exact required size is known. Unique paths follow first occurrence,
and copies follow source order then member/list order, so enumeration is
deterministic for an unchanged ordered input. Each entry identifies its winning
copy, its copy count and a chain of every copy with source/file indices.
`all_identical` compares complete lengths and payload bytes across the chain.
`es2_vfs_duplicate_indices` fills a caller array of duplicate-entry indices;
NULL/zero storage queries the required count, with `ES2_ERR_CAPACITY` when nonzero.
Enumeration can leave partial arrays on failure; statistics are assigned only
on success. Arrays must not overlap each other or any borrowed input.

Sources, loose path strings, archive descriptors, source lists and payload bytes
must remain alive and unchanged. Returned paths borrow prefix/name spans and
retain original spelling. No path or payload copy is allocated. Lookup scans
sources and files; enumeration compares with prior unique entries, with quadratic
worst-case path-comparison cost. The source list has no fixed archive-count cap;
representable caller-array sizes are checked before enumeration.

### Collision formats

`collision.h` exposes four explicit grammars. COL groups contain shapes and
signed sphere coordinates/radii; DMG regions contain a threshold, debris/cell/
parent references, flags and weighted capacity links. GL is a counted 26-byte list.
PDG schema selection is explicit: vehicle views/rectangles/weapon-display
slots, or the different WEAPONS pair table. No extension-only autodetection
is offered. Opaque fields borrow input, with record offsets and span lengths;
generated bounds and runtime pointers are never serialized.

All parsers reject negative/oversized counts, truncated fields and trailing
bytes, and leave output unchanged on failure. DMG checks every capacity link;
PDG checks its COLORS.DAT references and the original three-view capacity.
DMG also validates parent regions; `es2_dmg_validate_references` checks
external DTS cell and combined default/vehicle debris counts supplied by the caller.
COL rejects sphere counts above 4095 because the original narrows `8*S` to
a signed 16-bit stream-read length. `es2_col_validate_regions` and
`es2_gl_validate_regions` need the companion DMG count. The caller resolves
DTS part IDs. `es2_gl_validate_capacities` checks the second DMG mapping
used during weapon augmentation. GL display slots are sparse relative to PDG:
additional GL slots are legal, and `es2_gl_find_display_slot` performs the
original direction of matching (visible PDG ordinal to GL).

`es2_bnd_inspect` accepts any bounded memory span. Its success means only that
an opaque view was returned, not that a BND file is valid: there is no minimum
length, tag, timestamp or count requirement. The prefix candidate is optional;
all bytes remain in the opaque range.

### SAV framing and round trips

`//formats:sav` adds `es2_sav_parse`, roster/loadout accessors and
`es2_sav_write`. It accounts for all variable sections documented in
[mission.md](../docs/formats/mission.md#complete-sav-framing-confirmed-static-layout-confirmed-data-accounting),
retains borrowed opaque spans and preserves any trailing bytes. The writer
rebuilds counts/string framing, optionally edits the 1,000 variable words and
campaign position, and preserves all other bytes. It rejects output overlap and
insufficient capacity before writing. This is framing support, not a semantic
campaign editor.

`es2_sav_edit` can also replace the 33 availability flags, all 36 roster
records, the roster selection, the team words, the player record, the complete
hangar list and the nine-word bank, and `es2_sav_compose` serializes a complete
`es2_sav_image` without an input file. Replacement records are validated
against the loader-safe domain described in `sav.h` and
[mission.md](../docs/formats/mission.md#sav-editing-and-building) before the
first output byte; unchanged sections are copied as before.

VSHELL rewrites a SAV without truncating it, so bytes after `reader_end` are a
stale tail of an older, longer file
([mission.md](../docs/formats/mission.md#sav-write-sequence-and-stale-tail)).
`es2_sav_write_over` models the original's file update (edited image followed
by the previous file's bytes beyond it) and `es2_sav_overlay` does the same for
a composed image.

### DFN/HFN fonts

`//formats:dfn` ([`dfn.h`](include/es2/formats/dfn.h),
[dfn.md](../docs/formats/dfn.md)) parses one `GLFont` member (tag
`05 00 28 00`) without allocating: header words, colour records, glyph pixel
data, offsets, widths and the (unused) character map as borrowed spans, plus
per-byte advance, glyph bitmap and text width as VSHELL's `getWidth`
`0x4010fd` and string width `0x4011af` compute them. It refuses what the
original does not check (bounds, negative counts, trailing bytes); codes
without a glyph are `ES2_ERR_NOT_FOUND`.

## What the checks prove

| Check | Proves |
|---|---|
| `formats_test` (synthetic) | every parser against fixtures built in C: valid records, every shorter prefix of representative files, nested-body length mutations, signed/count extremes, offset/overlap errors, bad tags/names, odd sizes/padding, insufficient capacity, unsupported encodings, RLE edge cases, callback cancellation, traversal limits, DAT and VFS behaviour, the collision grammars (both PDG schemas, sparse GL mapping, negative part IDs), BND's opaque boundary. No original assets are loaded |
| `es2fmt --real` | the library against the reference CD: archive identities and member totals, every documented DTS tag count, DAT family totals (every family's file and byte total and the documented record and nested-count totals), COL/DMG/GL/PDG framing of every member with their cross-file index checks (COL/GL against same-stem DTS root 0 and DMG, visible PDG slots against GL), BND as opaque only |
| `vfs_dbsim_test` | ties in both orders, carried displacement, unsigned priorities, full mask bits, loose overrides, filtering/provenance, case/separators, missing entries, capacity and unchanged failure outputs |
| `vfs_dbsim_real_test` (`ES2_DATA_ROOT`) | all 13 VOLs, their duplicate totals, the equal-priority SHELL0/SIMVOL0 `dfn\font.dfn` pair and all ten differing paths in both process views under 26 enumeration orders; three shell-only paths correctly return not-found in DBSIM, its FONT.DFN comes from SIMVOL0, the remaining six from SIMPATCH |
| `sav_real_test` (`ES2_DATA_ROOT`) | the four saves of the snapshot copy `es2-data/logs/snapshots/save-load-crash-20261005T054444359Z/SAV` (override `ES2_SAV_DIR`; never an installation) round-trip exactly in memory, including GAME_R's ten-byte tail; the three identity rewrites of the edit/compose API; the overwrite identities and a GAME_T-over-GAME_0 overwrite. No save is written |
| `dfn_test`, `dfn_real_test` | synthetic fonts; the CD's 36 DFN and 26 HFN members, the shell fonts resolved with VSHELL's VOL order, metrics, ink indices and the `ESTEXT.BIN` byte coverage. No glyph bytes are kept |
| `str_cfg_test`, `str_cfg_real_test`, `player_mec_test`, `player_mec_real_test` | as named above |

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
