# EarthSiege II VOL containers

Status: **confirmed on the reference CD corpus**. This describes
the `VOLN` files, not every format named `.VOL`. Unknown meanings and runtime
behavior are explicitly hypotheses. All offsets below are file offsets, not
executable addresses. Reference media: [reference_version.md](../reference_version.md).

## Evidence and scope

**Confirmed:** recursive inspection of `build\extracted\cd` found
24 `.VOL` files: 16 `VOLN` archives, seven `VOL ` archives from the A-10 demo,
and one 16-byte filename list. All 865 CD files, including these 24, matched
their SHA-256 entries in [iso_file_hashes.sha256](../../data/iso_file_hashes.sha256).
The 16 `VOLN` files contain 3,802 entries and occupy 83,746,191 bytes.

The independent standard-library audit is
`<es2-data>\logs\codex-vol-20261005\inventory.py`; its per-entry offsets, lengths, hashes,
name checks and file listings are in `inventory.json` beside it.
`payload_checks.py` and `payload-checks.json` record additional payload checks.
These are archived evidence, not project tools.

## Layout verified on all 16 VOLN files

All multi-byte integers below are **little-endian**. Let `D` be the directory
string byte count, `N` the member count, `T = 18 + D` the member-table start,
and `P = T + 18*N` the first member block. All ranges use an exclusive end.
The tables distinguish confirmed representation from unconfirmed semantics.

| File offset | Width | Confirmed representation / interpretation |
| --- | --- | --- |
| `0x00` | 4 | Signature `56 4f 4c 4e` (`VOLN`). |
| `0x04` | 1 | Values `00` or `01`. Simulation mask byte; bit 0 selects DBSIM (see original-order contract). |
| `0x05` | 1 | Values `00` or `01`. Shell mask byte; bit 0 selects VSHELL (see original-order contract). |
| `0x06` | 2 | Always `00 00`; upper two bytes of the 32-bit archive use mask. |
| `0x08` | 1 | `0a` in `SHELL1` and `SIMPATCH`, `05` in all others. Unsigned load priority; higher values searched first. |
| `0x09` | 1 | Number of directory strings (1–41 here). |
| `0x0a` | 2 | `D`, bytes occupied by those strings, including backslashes and NULs. |
| `0x0c` | `D` | Consecutive NUL-terminated directory strings. |
| `12 + D` | 2 | `N`, member count (4–1,300 here). |
| `14 + D` | 4 | Member-table length, exactly `18*N` in every file. |
| `T` | `18*N` | Fixed-width member records described below. |
| `P` | remainder | Member blocks, with no gap before the first block. |

**Confirmed raw header grouping:** `LANG0`, `SHELL0`, `SHELL1`, `SHLSOUND`
have bytes `00 01` at `0x04`; `ZONES` has `01 01`; every other `VOLN` has
`01 00`. The original loaders use them as mask bits (below).

**Confirmed strings:** each directory ends in `5c 00` (backslash, NUL).
Directory names are ASCII, 2–8 characters excluding that suffix, with mixed
case across the corpus. There is no separate member-filename string table:
filenames are inline in the member records. The list has exactly the declared
number of strings and consumes exactly `D` bytes. Directory indices are
zero-based; every index resolves. Unused directory entries are allowed by
the observed files (`SIMPATCH` declares 41 directories for only 10 members).

For a member record at `R = T + 18*i`:

| Relative offset | Width | Confirmed representation |
| --- | --- | --- |
| `R + 0` | 13 | Filename slot; read through the first NUL, at most 12 name bytes in this corpus. |
| `R + 13` | 1 | Index into the directory list. |
| `R + 14` | 4 | Absolute file offset `B` of the member block, **not** its payload. |

**Confirmed:** all 3,802 names are nonempty uppercase ASCII, all terminate
inside their 13-byte slots, and no name contains a path separator. Bytes after
the first NUL are not part of the name: 858 slots contain nonzero trailing
bytes. For example, `SHELL1` at record offset `0x17` begins
`4d 41 50 2e 44 46 4e 00 01 9d 00 00 a6`; the name is `MAP.DFN`.
Treating byte 12 as a separate required-zero field would reject valid files.

| Block-relative offset | Width | Confirmed representation / interpretation |
| --- | --- | --- |
| `B + 0` | 1 | Always `02` in this corpus; stored/raw member kind. |
| `B + 1` | 4 | Payload byte length `L`. |
| `B + 5` | 4 | Varying metadata bytes; meaning unknown. |
| `B + 9` | `L` | Payload. |
| `B + 9 + L` | 1 | Extra byte, equal to the last payload byte in **every** observed member. |

**Confirmed:** all payloads are nonempty. Each block occupies `L + 10` bytes,
including the extra byte. The next block starts at `B + L + 10`, and the last
block also has that extra byte before EOF. This is a 9-byte prefix plus a
1-byte suffix, not a 10-byte prefix. The suffix's purpose is **unknown**;
an archive-writer off-by-one error is only a hypothesis. The length field
excludes the suffix. Do not use the next offset as the payload end.

## Compression, alignment and lookup

**Confirmed on this corpus:** every outer member kind is `02`, and raw reads
at `B + 9` expose ordinary payload headers. All 1,371 `.WAV` members start
with `RIFF` and have `WAVE` at payload offset 8; the `fmt ` headers report
PCM (code 1), mono, 11,025 or 22,050 Hz, with 8 or 16 bits per sample.
DBA/HBA, DBM, DFN/HFN and DPL members consistently start with the respective
bytes `01 00 28 00`, `0e 00 28 00`, `05 00 28 00`, `0f 00 28 00`.
No VOL-level compressed member was observed. This does not establish whether
an individual image or other inner format uses its own compression.

**Confirmed length caveat:** WAV RIFF size plus 8 equals the VOL payload size
for 1,334 members; 33 have one further byte and four have 470 further bytes
inside the declared VOL payload. Preserve the VOL length instead of deriving
it from RIFF. These bytes are separate from the extra VOL suffix.

**Confirmed alignment:** member offsets include all four residues modulo 4
across the corpus, and odd addresses occur. There is no 2- or 4-byte block
alignment rule here. Packed header/table fields are also unaligned. All
member records happen to be in ascending physical-offset order in these
16 files; this is an observation, not a requirement established for the loader.

**Confirmed case evidence:** lowercase `stf\`/`stg\` occur in `SIMLANG`,
lowercase resource directories in `SIMVOL0`, and uppercase variants in other
archives. Every resolved path is unique within its own archive even after
ASCII case folding. Across the 13 `VOL/` archives, case folding finds 567
paths present in more than one archive; 10 of those paths have differing
payload hashes. `DFN\MAP.DFN`, for example, has length 5,917 at block
`SHELL0:0x842bbe` and length 5,941 at `SHELL1:0x5f`.

**Confirmed static:** ASCII case-insensitive archive lookup,
backslash-only archive splitting, priority/carry registration and loose-first
stream opening are described below. DBSIM's live root, enumeration order and
final slot order are in [Live observation](#live-observation); VSHELL's and
the actual resource winners on an installation are unmeasured. Preserve original name spelling
and directory identity in future tools; matching by basename alone is unsafe.

## Verification results

**Confirmed for every row below:** exact directory-string count and byte
consumption; `table_bytes = 18*N`; all names terminate; all directory indices
resolve; all blocks/payloads are in bounds and nonoverlapping; first block
equals `P`; every inter-block gap and final suffix is exactly one byte,
equal to the preceding payload's last byte. There are **no mismatching VOLN
files** in the reference CD tree. Sum check for each file:

`file_size = 18 + D + 18*N + 9*N + sum(L) + N`.

| CD path | Dirs / string bytes | Entries | First block | Payload bytes | File bytes |
| --- | --- | --- | --- | --- | --- |
| `VER31/SIMVOICE.VOL` | 1 / 10 | 213 | `0xf16` | 5,220,630 | 5,226,622 |
| `VER31/SIMVOICF.VOL` | 1 / 10 | 213 | `0xf16` | 5,370,479 | 5,376,471 |
| `VER31/SIMVOICG.VOL` | 1 / 10 | 213 | `0xf16` | 5,044,741 | 5,050,733 |
| `VOL/LANG0.VOL` | 3 / 15 | 21 | `0x19b` | 53,783 | 54,404 |
| `VOL/SHELL0.VOL` | 6 / 30 | 170 | `0xc24` | 8,822,154 | 8,826,962 |
| `VOL/SHELL1.VOL` | 1 / 5 | 4 | `0x5f` | 69,210 | 69,345 |
| `VOL/SHLSOUND.VOL` | 1 / 5 | 5 | `0x71` | 2,124,485 | 2,124,648 |
| `VOL/SIMALERT.VOL` | 7 / 35 | 23 | `0x1d3` | 747,607 | 748,304 |
| `VOL/SIMLANG.VOL` | 2 / 10 | 26 | `0x1f0` | 40,701 | 41,457 |
| `VOL/SIMPATCH.VOL` | 41 / 203 | 10 | `0x191` | 71,936 | 72,437 |
| `VOL/SIMSOUND.VOL` | 3 / 15 | 641 | `0x2d33` | 2,273,365 | 2,291,346 |
| `VOL/SIMVOICE.VOL` | 1 / 10 | 213 | `0xf16` | 7,036,415 | 7,042,407 |
| `VOL/SIMVOICF.VOL` | 1 / 10 | 213 | `0xf16` | 7,255,575 | 7,261,567 |
| `VOL/SIMVOICG.VOL` | 1 / 10 | 213 | `0xf16` | 6,604,102 | 6,610,094 |
| `VOL/SIMVOL0.VOL` | 39 / 193 | 1,300 | `0x5c3b` | 30,780,454 | 30,817,065 |
| `VOL/ZONES.VOL` | 3 / 15 | 324 | `0x16e9` | 2,123,224 | 2,132,329 |

The sum of payload lengths is **83,638,861 bytes**; the remaining **107,330
bytes** are fully accounted for by the headers, directory/member tables,
member prefixes and suffixes.

Concrete boundary example: the first 12 bytes of `VOL/SHELL0.VOL` are
`56 4f 4c 4e 00 01 00 00 05 06 1e 00`; its 30-byte directory table
ends at `0x2a`. At `0x2a`, `aa 00 f4 0b 00 00` means 170 records and
3,060 table bytes. First record `DBA\APOC_OUT.DBA` points to `0xc24`,
whose prefix is `02 50 d9 00 00 6a 1f e3 74`. Its 55,632-byte payload
starts at `0xc2d`; the next block is `0xe57e`, exactly `0xc24 + 9 + 55632 + 1`.

## Duplicate paths across archives

**Confirmed (data; `formats/` VFS enumeration, rechecked with an own script):**
over the 13 `VOL/*.VOL` archives, 3,163 member copies form 2,595 unique full
paths (ASCII case and separator folded); 567 paths occur in more than one
archive, 557 of them with byte-identical payloads. The ten paths with
**different** payloads each include an archive whose header byte `0x08` is `0a`
and archives whose byte is `05`. The three-copy FONT.DFN group also contains
one unequal pair at priority `05` (see [Corpus recheck and process split](#corpus-recheck-and-process-split)):

| Path | Priority `0a` copy | Priority `05` copies |
| --- | --- | --- |
| `dfn\map.dfn`, `dfn\black.dfn`, `dfn\font2.dfn` | SHELL1 | SHELL0 |
| `dfn\font.dfn` | SHELL1 | SHELL0, SIMVOL0 |
| `dfn\cpylw.dfn`, `dfn\green6x8.dfn`, `hfn\cpylw.hfn`, `hfn\green6x8.hfn`, `pdg\spider.pdg`, `bnd\results.bnd` | SIMPATCH | SIMVOL0 |

**Confirmed static:** DBSIM reads byte `0x08` into archive
runtime `+0x1c` (`0x47ccda..0x47cce9`), inserts archives in descending
unsigned priority order (`0x467fc8..0x467fd6`), and returns the first path
match (`0x468064..0x46808a`). Thus SIMPATCH precedes SIMVOL0 when both are
registered. Actual opening of RESULTS.BND remains unconfirmed. See
[collision.md](collision.md#selection-and-opening) for the loader chain.
The default `formats/` VFS leaves source order to the caller; the CLI
ASCII-name order is a demonstration policy. The explicit original-order
policy and registration conditions are specified below.

## Original registration and lookup order

**Confirmed static contract:** the
following is derived directly from the reference DBSIM and VSHELL executable
bytes with LLVM objdump and independent PE-offset reads. Binary SHA-256s are
`8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad`
(DBSIM) and
`1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e`
(VSHELL).
Addresses below are executable VAs; this is static evidence, not live parity.

### Registration, filtering and ties

DBSIM `0x473260(mask, name, alternate_root, alternate_first)` owns the global
manager at `0x4a0d18`. It constructs a 30-slot manager through `0x467b9c`
or updates it through `0x467cd8`. The sole direct startup call at `0x45f2a8`
passes mask **1**. VSHELL's separate equivalents are `0x40356f`, `0x403b68`
and `0x403caf`; its startup call at `0x401b3c` passes mask **0x100**.

`0x467cd8` builds **root + "vol\" + "*" + name**. Both startup
callers pass name `.vol` (DBSIM literal `0x49f467`, VSHELL `0x46c5cd`),
so both passes enumerate `vol\*.vol`. The argument order is confirmed by
`0x467d24..0x467d38` and the concatenation order in `0x47d2c4`.
It enumerates two passes with Borland `_findfirst` /
`_findnext` (DBSIM calls `0x467d4c/0x467e0f` and
`0x467e5d/0x467f20`). A false alternate-first argument visits the current
working directory first and the supplied root second; true reverses those
passes (`0x467d14..0x467d21`, `0x467e25..0x467e32`). Every result is
registered immediately before requesting the next result. No alphabetical
sort exists in this code. Both passes use the same name pattern. An absent
first pass does not prevent the second pass; an absent second pass returns.

The existing-name check strips a leading drive designator, then compares
`vol\` plus the returned filename **case-sensitively** (DBSIM
`0x467da1..0x467dd3`, `0x467eb2..0x467ee4`; VSHELL calls `strcmp` at
`0x403db7/0x403eba`). Thus a matching second-root spelling is skipped and
the first registered archive wins that archive-name collision. This is a
separate operation from case-insensitive member lookup. A rooted alternate
path beyond the drive prefix is not removed. The caller must reproduce these
passes and this duplicate suppression before supplying VFS sources.

The archive reader `0x47cab0` reads the four bytes at header offsets 4–7
as a little-endian mask, accepts only a nonzero intersection with the requested
mask (`0x47cbee..0x47cbf4`), stores that mask at archive `+0x20`, and reads
header byte 8 into unsigned priority byte `+0x1c`
(`0x47ccda..0x47cce9`). Consequently byte 4 bit 0 enables simulation and
byte 5 bit 0 enables shell; these are bits of one 32-bit mask, not Boolean
fields checked for equality to 1. VSHELL does the same at
`0x402a05..0x402a1c` and `0x402b42..0x402b4b`.

**Exact tie rule:** archive insertion at `0x467f94` scans occupied slots in
lookup order, carrying the new archive. A slot is replaced only when the
carried priority is **strictly greater** than its occupant; the displaced
occupant becomes the carried archive for later slots. Equal or greater
occupants remain in place. Append the final carried archive. The comparison
at `0x467fcd` is unsigned, and the equal branch at `0x467fd0` skips the
exchange at `0x467fd2..0x467fd6`. A new equal-priority archive therefore goes
after existing equals, but this is **not stable sorting**: registering
`A(5), B(5), C(10)` produces `C, B, A`, because displaced A passes B.
A later higher-priority archive can change the winner between older equals.
VSHELL independently confirms the same carry rule, with reversed comparison
operands and an unsigned less-or-equal skip at `0x404030..0x404047`.

The manager initializer clears 30 pointer slots and its byte count. An
already-full manager returns from DBSIM enumeration (`0x467d0a`), but there
is no capacity recheck inside its result loops. A safe replacement rejects
more than 30 accepted archives rather than reproducing a potential overwrite.
On refresh, mask-disjoint archives are removed (`0x467ced..0x467d08`);
the loop advances after removal, so a consecutive shifted entry is not
rechecked. The portable policy below constructs a fresh view, not that
refresh/removal bug. These limits do not affect the 13-archive CD corpus.

Missing/unopenable archives and mask-disjoint archives yield no registration:
`0x47cb15..0x47cb5d`, `0x47cbf4..0x47cc3a`, and the null check at
`0x467fb5`. A filename that disappears between enumeration and opening
therefore adds no placeholder. No synthetic empty archive represents it.
The loader checks the first three signature bytes; VSHELL additionally
checks the fourth byte at `0x40292a`. Existing strict `es2_vol_open`
validation intentionally rejects malformed archives rather than emulating
original error/diagnostic paths.

### Lookup and loose files

DBSIM stream opener `0x4730e8` first calls Borland `open(path, 0x8000)` at
`0x473116..0x47312c`. If the signed 16-bit handle is not -1, it succeeds
without searching archives. Only a failed loose open calls manager lookup
`0x468064` at `0x473141`; that walks slots and returns on the first successful
member lookup (`0x468073..0x46808a`). Failure of both sets stream error 1
and returns false (`0x47314d..0x47316f`). VSHELL's opener `0x4032b1`
uses the same loose-first rule (`0x4032df..0x4032f5`) and fallback through
`0x4040e9` at `0x4033c9`, with additional diagnostic checks.

Member lookup splits the requested path (`0x47cdf8` calls `0x47d198`),
then uses Borland `stricmp` separately for the directory and filename
(`0x47ce58`, `0x47cec6`). The splitter searches for the last **backslash**
only (`0x47d202..0x47d205`); it never converts `/` to `\`. Thus ASCII
letter case is ignored, but an archive request `dir/file` does not match
`dir\file`. The path builder `0x47d2c4` concatenates components without
separator conversion. VSHELL has the same split/compare structure in
`0x402d2b` (`0x450aec`, `stricmp` calls `0x402d8c/0x402e02`). Non-ASCII
Borland locale behavior remains outside the printable-ASCII VFS domain.
Loose path case/separator handling is delegated to the OS/runtime, not to
this archive comparison; the portable view accepts the usual Windows ASCII
case and slash aliases for supplied successfully opened loose files.

The loose-first rule applies to these stream openers. Direct C `fopen`
users, including some launch/configuration readers, consult loose files only;
there is no global interception that makes every file operation consult VOLs.
`DATA` launch files are therefore supplied as loose entries, not presumed
archive members. An unreadable loose file must be omitted by the caller so
archive fallback remains possible.

### Corpus recheck and process split

**Confirmed data:** the independent read-only audit
`build/codex/scratch-h4-weapons3/vol/audit.py` and its metadata-only
`audit.json` rechecked all 13 VOL archives and all **865** loose CD paths.
The archive totals remain 3,163 copies, 2,595 paths, 567 duplicate paths and
10 paths with differing payloads. No loose CD-relative path overlaps an
archive path, including the loose `DATA` files. This is the CD tree, not a
claim about generated launch files or installation overrides.

The complete list of equal-priority, different-payload archive pairs is:

| Path | First copy | Second copy | Priority | SHA-256 (first / second) |
|---|---|---|---:|---|
| `dfn\font.dfn` | SHELL0, 18,249 bytes | SIMVOL0, 7,749 bytes | 5 | `bc0fb913c568fcda8212234b3b195757aaff8a57c3acd07084711de6bae34c32` / `9f04a6ff1b22d1fed0ea3384d8c8a272d7e2c78fdebc27150ab0fd83adfea333` |

SHELL0's use mask is `0x100`, SIMVOL0's is `1`; they do **not** conflict
inside either normal process view. The 9 accepted simulation archives contain
2,963 copies / 2,401 paths / 562 duplicate paths, of which six differ. The
5 accepted shell archives contain 524 copies / 520 paths / four duplicates,
all differing. ZONES (mask `0x101`) belongs to both views.

| Paths from the ten-path inventory | DBSIM mask 1 winner | VSHELL mask 0x100 winner |
|---|---|---|
| `dfn\map.dfn`, `dfn\black.dfn`, `dfn\font2.dfn` | Not found in archives | SHELL1 |
| `dfn\font.dfn` | SIMVOL0 | SHELL1 |
| `dfn\cpylw.dfn`, `dfn\green6x8.dfn`, `hfn\cpylw.hfn`, `hfn\green6x8.hfn`, `pdg\spider.pdg`, `bnd\results.bnd` | SIMPATCH | Not found in archives |

These winners are independent of enumeration order for the reference corpus.
The opt-in real-data test checks all ten paths in both process views under
26 supplied enumeration orders; it compares full payloads in memory and
exports nothing. Synthetic tests separately cover the order-sensitive tie
case that the normal corpus cannot exercise.

**Second reading:** read independently in Intel operand notation, DBSIM compares
the occupied slot's byte against the carried byte and skips exchange on
unsigned greater-or-equal (`0x467fcd..0x467fd6`). VSHELL compares the carried
byte against the occupied byte and skips on unsigned less-or-equal
(`0x404030..0x404047`). Both imply append-after-equals with carried displacement,
in agreement with the AT&T reading. The `A5,B5,C10 -> C10,B5,A5`
and `A5,B5,C10,D10 -> C10,D10,A5,B5` synthetic cases guard the subtlety.

### Portable policy and live questions

`es2_vfs_open_dbsim_order` is specified as a fresh view over caller-supplied
sources in actual registration/enumeration order: keep loose sources first,
filter archive masks, then apply the carry insertion above. The original
caller-order `es2_vfs_open` remains unchanged. File-system I/O, name-pattern
expansion, duplicate archive-name suppression and read failures belong to the
caller. Source provenance retains the caller's source indices.

**Confirmed host observation:** Python `os.scandir` returned
`LANG0,SHELL0,SHELL1,SHLSOUND,SIMALERT,SIMLANG,SIMPATCH,SIMSOUND,SIMVOICE,SIMVOICF,SIMVOICG,SIMVOL0,ZONES`
(with `.VOL` suffixes) from both the extracted directory on D: (NTFS) and
`E:\VOL` (mounted CDFS). Every mounted archive's SHA-256 matched its extracted
copy. **Hypothesis:** Borland's enumeration exposes the same order on this
host. Neither observation proves a guaranteed NTFS/ISO order or the original
process's root/call sequence. The caller must capture its actual enumeration
order, never replace it with a sorted listing. Startup root/name arguments, runtime reopens,
and actual winners on the installation remain to be captured. Entry counts
alone cannot prove the path arguments or their order.

Entry-probe lists (separate processes):

- DBSIM: `0x473260,0x467b9c,0x467cd8,0x467f94,0x47cab0,0x4730e8,0x468064,0x47cdf8`.
- VSHELL: `0x40356f,0x403b68,0x403caf,0x403fe8,0x4027b2,0x4032b1,0x4040e9,0x402d2b`.

| DBSIM VA | VSHELL VA | Question for the ordered argument/return probe |
|---|---|---|
| `0x473260` | `0x40356f` | Which mask, suffix, alternate root and pass-order flag does startup request? |
| `0x467b9c` | `0x403b68` | Is a fresh manager created, and with which registration arguments? |
| `0x467cd8` | `0x403caf` | Are there later registration/refresh passes or changed root/mask arguments? |
| `0x467f94` | `0x403fe8` | In what order do enumerated archive filenames reach insertion? |
| `0x47cab0` | `0x4027b2` | Which opens return null, and what masks/priorities do accepted archives carry? |
| `0x4730e8` | `0x4032b1` | Which requested loose paths succeed without archive fallback? |
| `0x468064` | `0x4040e9` | Which loose failures reach archive lookup and which lookup attempts fail? |
| `0x47cdf8` | `0x402d2b` | Which archive is visited for each member path, and which returns the first success? |

**Live counts (`live-coverage-20261008T055641Z`, all hooks
original):** the registration entries `0x473260`, `0x467b9c`, `0x467cd8`,
`0x467f94` and `0x47cab0` show 0 in all three suites. This is a measurement
limit, not evidence: the proxy arms hooks, coverage and probes at the first
DirectDraw/MCI call, after the startup registration at `0x45f2a8`. During the
measured missions every stream open falls back to the archives: `0x4730e8`
and `0x468064` both count 24 / 72 / 34 (acceptance-60s / c1_01-success /
c1_01-collision), and the member lookup `0x47cdf8` 96 / 307 / 148, i.e. about
four archives visited per resolved path. Startup registration needs probes
armed from `DllMain` or an argument trace. The proxy has such probes, off
by default; design and live results in
[Registration probes from DllMain](#registration-probes-from-dllmain).

Capture arguments and ordered return observations, not just counters. Startup registration probes must be armed before startup
in all three suites (`acceptance-60s`, `c1_01-success`, `c1_01-collision`);
stream and archive lookups should be nonzero during resource loading. No
claim is made that registration must occur during the measured mission ticks.

## Files that do not match VOLN

**Confirmed:** all eight exceptions are under `DEMOS/A10/`; none is an
EarthSiege II `VOL/` or `VER31/` archive.

- `640.VOL`, `CMP1.VOL`, `SHAPES.VOL`, `SIM.VOL`, `SOUNDS.VOL`, `VOICE.VOL`
  and `CMP1/TED.VOL` start with `56 4f 4c 20` (`VOL `). They contain
  `volh`, `vols`, `voli` and `VBLK` structures. This is a different layout [O1,O3].
- `CMP1/LOWTED.VOL` consists entirely of
  `73 61 30 2e 74 65 64 00 73 61 30 2e 64 6d 6c 00`, two NUL-terminated
  names (`sa0.ted`, `sa0.dml`). It has no archive directory or embedded members.
  Its role as an external-resource list is a **hypothesis**.

For inventory only, the seven `VOL ` files were independently checked as follows:
`volh` is at offset 8 with a zero 24-bit length; `vols` is at 16, its reserved
length is at 20 and its used string-byte count at 24. The used names start at
28. `voli` follows the reserved string region; its reserved size covers
14-byte records. Each active record has a matching string-relative name offset
at `+0`, a `VBLK` file offset at `+4`, a length at `+8`, and bytes `00 01`
at `+12`. Each pointed-to block has an 8-byte header and an in-bounds payload;
its 24-bit stored length equals the record length for all 688 members.
Active names and payload ranges are unique/nonoverlapping per file. Reserved
table capacity exceeds active entry count and contains nonzero unused bytes;
it must not be counted as additional members. This is **confirmed inventory
evidence**, not a complete specification of that variant or its compression.
Counts and extensions are in [data_inventory.md](../data_inventory.md).

## Remaining work

The layout above comes from real CD bytes and the executables. Real CD bytes
show a one-byte kind and a 9-byte member prefix; the extension
`.VOL` covers multiple incompatible layouts, and only the inventory subset
above was verified for the demo variant's tagged tables, 24-bit lengths and
record stride.

Open: VSHELL's live enumeration, root and winners; meaning of block metadata
and repeated suffix; compressed or malformed inputs. The static contract
covers both executables' mask, priority/carry, case and stream lookup
behavior.

## CD archive hashes

**Confirmed SHA-256**, computed from the original CD files. Paths are relative
to `build\extracted\cd`; these identify the exact corpus used above.

| CD path | SHA-256 |
| --- | --- |
| `DEMOS/A10/640.VOL` | `8ea97de17c5f5f524d872278434cfb44f40fc3e5e3758e6bb02806a7624176bb` |
| `DEMOS/A10/CMP1/LOWTED.VOL` | `e16f824427fd134cfca9394e36fd4c972f6afde72266da28f26d2203f7a7db81` |
| `DEMOS/A10/CMP1/TED.VOL` | `7831203ac10b80bcee874e17cf2625d68cedc72c47c59ed5c4e9b49db58dbe1d` |
| `DEMOS/A10/CMP1.VOL` | `4c4ff566ed348c51e313b286a41de4a7f0d9ecdd4f16f95709f11b398cf8f149` |
| `DEMOS/A10/SHAPES.VOL` | `e6a1995fab495327203d8a2000bbfa5faeef0f167bbda5676a0fed32b62bd394` |
| `DEMOS/A10/SIM.VOL` | `b81cd3aaef277cca51419bdf6982a7a9f29fb3adbe19fe2db726c6bbd605392c` |
| `DEMOS/A10/SOUNDS.VOL` | `1288d0923386ed1f7f61d89410662ff526e9c19551b5d40c2cd1667aa7450300` |
| `DEMOS/A10/VOICE.VOL` | `5c42e8aaae39acfc9bf2dc7eb4421ed77c48edb476ae639cde2a8e2e0be4e125` |
| `VER31/SIMVOICE.VOL` | `ccee7bf1ee4e6ffc56ba735d2795b0905afb5f3493550ec0d42a5350bde046cc` |
| `VER31/SIMVOICF.VOL` | `e590b30ee551013f8dfaaaa405547d3d169eee0f5381bb19bc9923ff74f930e6` |
| `VER31/SIMVOICG.VOL` | `1cf6dc42f6ddf0178c55209992ccc70c3df11da034c91dfd6e5c092fbb04a178` |
| `VOL/LANG0.VOL` | `a6a513c31997f95a65a208ee7e24c1aa033cbe55e580cc4eaa23c3ffb8ce17a8` |
| `VOL/SHELL0.VOL` | `fba8bda4dd2c7e43b625e5b188c87ce3505503c93fc92d1d48059a5193db16aa` |
| `VOL/SHELL1.VOL` | `df565a42ac045297716e101150363cd02e5694c2cae2fd6c2d1ef889778c32e5` |
| `VOL/SHLSOUND.VOL` | `1e8fbcc4ae50f257b60fbb126185df1e08e46729e9f3579d6f89be3005c1154b` |
| `VOL/SIMALERT.VOL` | `8e75a0dc765da0eb6cc257ae2e212fa6d453c26338403a15f512b1f99cf67b5c` |
| `VOL/SIMLANG.VOL` | `928b9227dbd8fd4e6b0267710038d1f5bbd85bfee7843b0ffe1839494bdedda3` |
| `VOL/SIMPATCH.VOL` | `c00ff6e0993bb2ffeb4ed5e78aa4bcae21d40e488f75eb51f6c4e5e5d3171e1b` |
| `VOL/SIMSOUND.VOL` | `d78e8366cae221d2391256ca60dc8a7ecdfdf9e81985e8fe3e3907783d531676` |
| `VOL/SIMVOICE.VOL` | `7f8cc292c7cbf8e43eace7cbc56de38e1bb79c3ae6e24ad8f421567ec9d12a37` |
| `VOL/SIMVOICF.VOL` | `65c76111139008ba75391f55eb02951c724e013614722a3e9728921f669ad664` |
| `VOL/SIMVOICG.VOL` | `fe13273201fd974668937785ca13934e674d59a489f40a3d1c381d7f155ba20d` |
| `VOL/SIMVOL0.VOL` | `f352762f0b21d10d0fdcd1a770654ae6f8fa2de78214ed01ef51636c47582026` |
| `VOL/ZONES.VOL` | `ce28cbba4596918b86fd173e1ecdbca2dbc0fd6ad7652940f3a8854391b83c7b` |

## Registration probes from DllMain

Status: **implemented and fixture-tested**; the live results are in
[Live observation](#live-observation). The expected values in the live check
are derived from the static contract above. Code: `tools/reference-game/proxy/vol_probes.h`,
called from `media_proxy.c` (`DllMain`, `initialize_forwarding`, process
detach) and from the tick boundary in `tick_log.h`.

### Why at process attach, and why that is safe

All other proxy instrumentation arms at the first DirectDraw/MCI call. DBSIM
registers its archives before that call (`0x45f2a8`), so the registration
entries always counted 0 (live counts above). The proxy is an import of
DBSIM, so its `DLL_PROCESS_ATTACH` runs after DBSIM is mapped at
`0x400000` and before DBSIM's entry point: the earliest point at which the
image exists and none of its code has run. The attach path, which runs only
when `ES2_LIVE_COVERAGE_CONFIG` is set, uses only kernel32 memory and file
calls: `GetModuleFileNameW`, `GetEnvironmentVariableW`,
`GetPrivateProfileStringW` (its own `[vol_probes]` section), `CreateFileW`
(reserving the report with `CREATE_NEW`), `VirtualQuery`, `VirtualProtect`,
`FlushInstructionCache` and `AddVectoredExceptionHandler`. It does not call
`LoadLibrary`, create threads, wait, or use user32 or advapi32. Patching
pages of the already mapped DBSIM image with `VirtualProtect` is the
technique `install_virtual_time` and `install_input_journal` already use in
`DllMain`. The module file SHA-256 needs CryptoAPI, which may load a
provider DLL, so it is not computed under the loader lock. In `DllMain` the
image is identified by its PE base and size (`es2_hook_image_valid`), the
7-byte anchor `83 3d 18 0d 4a 00 00` at `0x47327c` (the test of the global
manager pointer `0x4a0d18`) and a 12-byte signature at every probed entry.
At the first DirectDraw/MCI call `vol_probes_forwarding` checks the file
hash with `rng_hook_reference_file`. On a mismatch it restores every byte and
publishes the report with `module_verified:false` and error 10, so the records
are discarded. The fixture `vol_probes_dllmain_test` loads a DLL whose
`DllMain` arms the probes under the real loader lock; FreeLibrary's detach
then restores the bytes and removes the handler.

Probe kind: as in the entry probes, an INT3 replaces the one-byte entry
`push ebp` (`55`; all eleven entries are `push ebp`, all are instruction
starts in the [probe inventory](../reference-game/dbsim_map.md#probe-inventory)). The vectored handler records the event and
then emulates the PUSH in the saved context. It never removes the INT3 to
single-step. Excluded at arming, regardless of the later modes: every
registered hook relocation range and every coverage branch site. The
reference fixture checks that no probed entry overlaps either. If a
`[probes] entries` list names one of these VAs while `[vol_probes]` is
enabled, the entry probes refuse their whole set because the byte is already
`cc` (`entry_probes_sites_refused`). They do not fail silently.

### Probed entries (contract from the listing)

Arguments are cdecl stack arguments at entry (`[esp+4]`, `[esp+8]`, ...),
read from the `llvm-objdump` listing of the reference DBSIM. "Text" names
the arguments that are copied as C strings (at most 95 bytes, with a checked
read).

| VA | Role | Arguments at entry | Text | Slot snapshot |
|---|---|---|---|---|
| `0x473260` | `register` | mask, name, root, alternate_first | name, root | global manager `[0x4a0d18]` |
| `0x467b9c` | `manager_create` | new manager, mask, name, root, alternate_first | name, root | none (slots not yet cleared) |
| `0x467cd8` | `manager_update` | manager, mask, name, root, alternate_first | name, root | argument 0 |
| `0x467f40` | `slot_remove` | manager, slot index | | argument 0 |
| `0x467f94` | `insert` | manager, path, mask | path | argument 0 (state before this insertion) |
| `0x467fec` | `add_by_name` | manager, name | name | argument 0 |
| `0x467be8` | `manager_destroy` | manager, flags | | argument 0 |
| `0x47cab0` | `archive_open` | path, mask, buffer size | path | none |
| `0x4730e8` | `stream_open` (lookup set) | stream, path | path | none |
| `0x468064` | `lookup` (lookup set) | manager, path, out | path | none |
| `0x47cdf8` | `member_lookup` (lookup set) | archive, path, out | path | none; the archive enters the archive table |

Facts from the bytes that the records rely on. The startup call at
`0x45f2a8` pushes `mask = 1`, `name = 0x49f467` (`.vol`), `root =
[ebp-0x3c]` and `alternate_first = (short)[ebp-0x3e]`. The flag is set to 0
at `0x45f18f`. The root is the first `%s` token read from `data\drive.cfg`
(`0x45f19a` opens it with mode `rt`) followed by `\` (`0x45f1f0`), or the
empty string `0x49f462` if that read fails. Thus, statically, pass 1
enumerates `vol\*.vol` in the working directory and pass 2 enumerates
`<drive.cfg token>\vol\*.vol`. `0x467cd8` calls `0x467f94(manager, path,
mask)` once for every enumerated name that the case-sensitive name check does
not skip. `0x467f94` calls `0x47cab0(path, mask, [0x4a088c])`, where
`0x467b9c` set `[0x4a088c]` to `0x400`. The manager is `0x79` bytes long: 30
slot pointers and the slot count byte at `+0x78`. The archive object (`0x30`
bytes, allocated at `0x47cc7b`) holds two object pointers at `+0` and `+4`
(the opened file and the object built at `0x47cc5c`), a `strcpy` of the
path at `+8`, priority at `+0x1c`, directory count at `+0x1d` and the use
mask at `+0x20`. The mask is stored at `0x47ccb6` **before** the `strcpy`
at `0x47ccc0..0x47cce4`; the priority and the directory count are read
after it. A path of 20 to 23 characters therefore runs into `+0x1c`/`+0x1d`,
which are rewritten afterwards, and a path of 24 or more characters
(with a 12-character file name: a root of 8 or more characters) also overwrites the mask
field. The probe reads the first 20 name bytes and the fields as they are in
memory. `vol\SIMVOICE.VOL` (16 characters) and `E:\vol\SIMVOICE.VOL` (19)
fit.

### Configuration and record format

```ini
[vol_probes]
enabled=1
lookups=1
report=<run directory>\vol-probes.json
frame_limit=<suite frame limit, 0 for whole-mission suites>
report_every_ticks=32
```

The proxy reads this section only from the INI named by
`ES2_LIVE_COVERAGE_CONFIG`. Unlike the other sections it has no fallback to
`es2-oracle.ini`, because the attach path must stay inert without the
variable. `lookups=0` arms only the eight registration entries.
`run-live-coverage.ps1 -VolProbes` writes the section for each suite next to
`[coverage]`/`[probes]` (`-VolProbesRegistrationOnly` gives `lookups=0`).
Afterwards it writes `vol-probes-manifest.json` and `vol-probes-summary.json`,
which lists for each suite the registration-phase events with their first
text, `forwarding_event`, and the final slots as `name@priority`.

The report is schema-1 JSON, published atomically like `probes.json`. It is
first written at the first DirectDraw/MCI call (`reason:"forwarding"`). After
that it is rewritten at tick boundaries every `report_every_ticks` frames
when new events arrived. The final write happens at the frame limit
(`frame_limit`: disarm, restore, `complete:true`) or at process detach
(`detach`). A process that the runner stops with TerminateProcess keeps its
last publication (`complete:false`); the startup records are already in it.
Fields:

- `armed`, `refusal` (`config`, `config_path`, `report_exists`, `image`,
  `sites`, `install`), `module_verified`, `error` (2-5 restore, 6 second
  executing thread, 7 write, 8 handler removal, 10 hash mismatch),
  `forwarding_event` (number of events recorded before the first
  DirectDraw/MCI call, that is, the old arming point), `events_total`,
  `events_dropped` (capacity 8,192), `first_thread`/`other_thread`.
- `sites[]`: `va`, `role`, `count`, `restored`.
- `events[]` in arrival order: `seq`, `va`, `role`, `thread`, `frame`
  (the last tick frame seen, -1 before the first tick), `args[]` (hex),
  `text[]` with `text_state[]` (`ok`, `null`, `unreadable`, `truncated`,
  `none`), and `snapshot` (an index into `snapshots[]`, or null).
- `snapshots[]`: `event`, `manager`, `state` (`copied`, `null_manager`,
  `unreadable`), `count` (the raw byte at `+0x78`), `slots[]` (archive
  pointers in lookup order).
- `final`: the same snapshot of the global manager, taken at publication.
- `archives[]`: every distinct archive pointer seen in a snapshot or as a
  `member_lookup` argument, with `name` (20 bytes at `+8`), `priority`,
  `dirs`, `mask` and `first_event`.

Returns are not observed: the supported probe kind is an entry PUSH only.
Acceptance of an archive is the next snapshot's count. Member lookup success
is the last `member_lookup` before the next non-member event, unless the
visits cover every slot; in that case it is ambiguous whether the last slot
won or every slot missed.

**Off by default, how verified.** Without the option the proxy behaves as
before, and every output (tick log, coverage, probes, launch reports) stays
byte-identical. The fixture `vol_probes_off_test` checks five cases: variable
unset, INI without the section, `enabled=0`, another executable, and the
variable removed after an enabled INI was written. In every case it checks
that the configuration is never read past the gate, that no byte is patched,
that no handler is installed, and that the forwarding, tick and detach entry
points record nothing and create no file. In code, every entry point returns
at its first statement while `vp_state` is `VP_OFF`. Without the variable the
`DllMain` path ends after one `GetEnvironmentVariableW`. `vol_probes_tick`
runs after the tick record has been flushed, like `live_coverage_tick`.
`-VolProbes` absent from `run-live-coverage.ps1` writes no section.

**Fixtures** (`//tools/reference-game/proxy:vol_probes_{startup,off,restore,reference,dllmain}_test`):
the synthetic image has own stubs at its own RVAs. `startup` arms through the
attach path, then runs a synthetic startup (`register`, `manager_update`, and
`insert` plus `archive_open` for A(5), B(5) and C(10), with the carry rule
applied to an own manager), the forwarding call, two ticks and lookups. It
checks 11 events in order with arguments, strings and frames (-1 before the
first tick), `forwarding_event` 8, five snapshots (null global, 0, 0, `[A]`,
`[A,B]`), final `[C,B,A]`, the archive table, and that the frame limit
restored all six entries so later calls record nothing. `restore` checks that
refusals for configuration, signature, exclusion range, anchor and an
existing report patch nothing and never overwrite the old report. It also
checks the restore after a hash mismatch and at detach, `lookups=0`, and that
a second executing thread leaves the INT3s and the handler in place
(error 6). `reference` checks all eleven signatures and the anchor against
the hash-verified DBSIM (read only), the probe inventory opcodes, and that no
entry overlaps a registered hook or coverage site.

### Live check

All hooks `original`, probes only, every suite, one clone:

```powershell
Set-Location <es2>
& .\tools\reference-game\setup\run-live-coverage.ps1 -InstallName timing-r111 -ProbesOnly -VolProbes `
    -Suite acceptance-60s,c1_01-success,c1_01-collision,c3_09-loadout-b,c4_08-razor-plasma,c3_10-loadout-hercs
```

The minimum is `acceptance-60s` and `c1_01-success`; the three launch-set
suites add their own `DATA` sets. The wrapper still requires an identical
oracle check per suite. If one differs, for example from exception-stack
residue on a lookup entry, rerun with `-VolProbesRegistrationOnly`. Before
the run, record `type <clone>\data\drive.cfg` and the listings of `<clone>\vol`
and of the root that drive.cfg names, if it exists. Expected counts:
`stream_open` and `lookup` at least 28 / 74 / 36 / 137 / 45 / 57 and
`member_lookup` at least 118 / 321 / 162 / 683 / 189 / 273 (the DBSIM map,
counted after the old arming point, for a60 / c1s / c1c / c3_09 / c4_08 /
c3_10). The registration entries should be nonzero before `forwarding_event`.

Records that decide the hypotheses (per suite):

| Hypothesis | Confirmed by | Refuted by |
|---|---|---|
| R1: one startup registration, before the old arming point | exactly one `register` (seq 0, frame -1, seq < `forwarding_event`) with args `0x1`, text `.vol`; snapshot `null_manager`; then `manager_create` and one `manager_update` | a second `register`/`manager_update`, or any `add_by_name`, `slot_remove` or `manager_destroy` after `forwarding_event` (later registration or refresh) |
| R2: root and pass order | `register` text[1] = the drive.cfg token plus `\` (or empty), args[3] = 0; the first `insert` paths are `vol\X.VOL` (working directory), and later ones carry the root prefix | args[3] ≠ 0, or the root-prefixed paths come first |
| R3: duplicate-name suppression | no root-pass `insert` for a name already inserted from the working directory; root-pass inserts only for names the clone's `vol\` lacks | a second `insert` for the same file name |
| E: enumeration order | `insert` text order of each pass equals the host directory order (CD corpus: `LANG0, SHELL0, SHELL1, SHLSOUND, SIMALERT, SIMLANG, SIMPATCH, SIMSOUND, SIMVOICE, SIMVOICF, SIMVOICG, SIMVOL0, ZONES`, if the clone's `vol\` holds those 13) | any other order: record it; it is the order the VFS caller must supply |
| M: mask filter | after the `insert` of each shell-only archive (`LANG0`, `SHELL0`, `SHELL1`, `SHLSOUND`, mask `0x100`) the next snapshot's `count` is unchanged; `archives[]` holds only mask-1 archives; final count 9 for the CD set | a shell-only archive in a slot |
| P: precedence (carry rule) | `final.slots` equals the carry insertion of the accepted sequence. For E as above: `SIMPATCH@10, SIMLANG@5, SIMALERT@5, SIMSOUND@5, SIMVOICE@5, SIMVOICF@5, SIMVOICG@5, SIMVOL0@5, ZONES@5` (SIMALERT is displaced behind SIMLANG by SIMPATCH, not stably sorted) | any other final order given the observed insert sequence: the static tie rule is wrong |
| L: lookup order and first success | after each `lookup`, the `member_lookup` archives follow `final.slots` from slot 0. `bnd\results.bnd`, `pdg\spider.pdg` and the other SIMPATCH paths stop at SIMPATCH (one visit); a SIMVOL0-only path visits slots up to SIMVOL0 | visits out of slot order, or continuing after a slot that holds the path |
| F: loose first | a `stream_open` directly followed by a `lookup` with the same path means the loose open failed. A `stream_open` without one means a loose file served it (record which) | |

The answer to the open precedence question is `final.slots` together with
the insert sequence and pass roots. The ten-path winners in "Corpus recheck
and process split" are order-independent for the CD corpus; the record shows
whether the clone's actual set (including any root-pass archives from the
CD) changes that.

### Live observation

Run on `timing-r111`, all hooks `original`, probes only,
the six base suites (`<es2-data>\logs\v-02-live\run`; the clone's
`DATA\DRIVE.CFG` and `VOL\` listing recorded beside it in `drive-cfg.json`
and `vol-listing.json`). All six reports are complete, error 0, no dropped
events, no other thread, and give the same record: 37 registration events,
all before `forwarding_event` (37), frame -1. The lookup entries are `vfs` hooks
and were not armed, so L and F are answered by the vfs hook
counters, not here.

| Hypothesis | Observed | Verdict |
|---|---|---|
| R1 | one `register` (seq 0, args[0] 1, text `.vol`, snapshot `null_manager`), one `manager_create`, one `manager_update`; nothing after `forwarding_event` | confirmed |
| R2 | `register` text[1] `E:\` (the first `DRIVE.CFG` token is `E:`), args[3] 0; the 13 `vol\X.VOL` inserts come first, then the `E:\vol\` ones | confirmed |
| R3 | the root pass inserts `E:\vol\LANG0`, `SHELL0`, `SHELL1`, `SHLSOUND` (names the clone's `VOL\` holds) and none of the nine accepted names | refined: suppression is against the accepted slots, not against names already inserted; the four shell-only names are tried again from the root and rejected again by the mask |
| E | working-directory inserts in the order `LANG0, SHELL0, SHELL1, SHLSOUND, SIMALERT, SIMLANG, SIMPATCH, SIMSOUND, SIMVOICE, SIMVOICF, SIMVOICG, SIMVOL0, ZONES` (the clone's listing) | confirmed |
| M | the slot count stays 0 over the four shell-only inserts and 9 over the four root-pass inserts; every slot archive has mask 1 | confirmed |
| P | final slots `SIMPATCH@10, SIMLANG@5, SIMALERT@5, SIMSOUND@5, SIMVOICE@5, SIMVOICF@5, SIMVOICG@5, SIMVOL0@5, ZONES@5` (SIMALERT displaced behind SIMLANG by SIMPATCH) | confirmed |

### The lookup set and the vfs hooks

The three lookup-set entries (`0x4730e8`, `0x468064`, `0x47cdf8`) are hooks
of the group `vfs` ([file_io.md](../reverse-engineering/file_io.md#the-vol-aware-read-stream-as-own-code)).
`vp_arm` excludes registered hook relocation ranges at process attach; a
lookup-set entry inside such a range is left out (not armed, no
`sites[]` row) instead of refusing the whole set, while any other excluded
entry still refuses it. With `lookups=1` the report therefore holds the
eight registration entries only; the lookup counts come from the hook
counters (`hk_stream_open_calls`, `hk_vol_lookup_calls`,
`hk_member_lookup_calls`) when the hooks are not original, or from the
DBSIM map. The fixtures check both (`vol_probes_restore_test`: a lookup
exclusion leaves out the two lookup stubs and arms the four others; `vol_probes_reference_test`: hook
overlap only at the three lookup entries).
