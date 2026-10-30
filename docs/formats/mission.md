# EarthSiege II mission data, execution and shell handoff

Static analysis. **Hypothesis** means an interpretation or behavior not
established by original bytes, bounded file parsing, or original executable
instructions. **Open** means unresolved. Recommendations describe future work,
not completed behavior. Unknown meanings are deliberately retained as opaque
fields.

All integers are little-endian. `i16` is a signed two-byte value, `u16` an
unsigned one, `W` a two-byte field without established signedness/meaning,
and `D` a four-byte field. Record offsets are decimal unless written `0x...`.
Executable addresses are VAs at image base **`0x00400000`** in the named
reference binary. A runtime pointer/stride is never a serialized pointer/stride.

## Answer and limits

**Conclusion:** mission-specific setup and conditions live in
`ZONES.VOL` `.MSN` records and localized companions. VSHELL evaluates
ordered conditions, random branches, overlays and references to prepare
`DATA/script.dat` and its companions. DBSIM reads typed lists, resolves
references, and runs compiled handlers selected by numeric record types.
Objectives, spatial regions, route references, trigger actions, timers
and delayed group activation are data-configurable. The meanings of their
predicates/actions, update order, completion policy and handoff are code in
DBSIM/VSHELL. This is a declarative mission language with small operation
tables, not evidence that each mission is implemented as a separate function.

**Hypothesis / bounded negative finding:** no general instruction-stream VM
with an instruction pointer, calls and a stack was found in the covered
preparation, loading, update and outcome paths. The filename `script.dat`
alone does not establish bytecode. This does not prove that no other path
in either executable interprets another format. Opcode-like selectors and
their dispatch addresses are documented below instead of inventing a VM.

**Concrete anchor:** Q dispatches a compiled forced-status check.
An unmet required route objective selects its data-supplied message for the
`EINSATZALARM` dialog. The German waypoint warning is therefore the meeting
point of authored objective/text data and generic compiled policy.

**Scope:** inputs are `build/extracted/cd` and the
prepared `build/installs/timing-r111/{DATA,SAV}` examples. No
original assets are exported or committed. This document refines the
mission hypotheses in [data_inventory.md](../data_inventory.md); it does
not declare the terrain semantics complete.

## Resource inventory and complete-file checks

The audit checks all 13 archives under CD `VOL/`,
all 3,163 of their member hashes, and ES/VSHELL/DBSIM against
[iso_file_hashes.sha256](../../data/iso_file_hashes.sha256). All match.
The three extra VER31 voice archives are outside this mission sweep;
their broader inventory remains in [vol.md](vol.md).

The resource inventory identifies **389**
direct mission/environment/campaign/alert candidates by archive, exact
member name, payload offset and size. The table below gives every
family in that inventory. Generic dependent actor models, textures, sounds
and fonts retain their separate format work; this is not a claim to have
closed the transitive dependency graph of every mission.

| Family | Members / bytes | Original reader or established scope | Verification |
| --- | --- | --- | --- |
| ZONES `MSN\*.MSN` | 62 / 760,684 | VSHELL `0x41818b`; alternate debrief reader below | 61 exact; `DEMO2` is 42 bytes short |
| ZONES `MSN\*.ENG/.FRE/.GER` | 62/61/61; 119,560/127,463/130,527 | VSHELL `0x417cb0` | All 184 exact |
| ZONES `DAT\ZONE*.DAT` | 39 / 624 | DBSIM `0x42789c` | All exact; one dimension discrepancy with DBA |
| ZONES `DBA\ZONE*.DBA` | 39 / 984,366 | DBSIM `0x46c754` → `0x46c4c4` | All exact, one raw bitmap each |
| SIMVOL0 `wld\WORLD0..9.WLD` | 10 / 3,116 | DBSIM `0x42e010` | All exact |
| SIMVOL0 `rmp\WORLD0..9.RMP` | 10 / 983,120 | DBSIM `0x42e621..0x42e67f` | All exact |
| SIMVOL0 `dpl\WORLD0..9.DPL`, `IMPACT0..9.DPL` | 20 | World loader; DPL reader `0x4821cc`, [dpl.md](dpl.md) | All 20 rechecked to exact EOF |
| LANG0 `{ENG,FRE,GER}\MISSIONS.BIN` | 3 / 2,985 | VSHELL `0x40883a`, career caller `0x412f4b` | All exact and identical |
| SHELL0 `GAM\CAREER.DAT` | 1 / 148 | VSHELL `0x412f2a` → `0x412e88`, [dat.md](dat.md) | Exact; all 61 name indices valid |
| SHELL0 `GAM\THEATER/MAP_LOCS/MAP_INFO.DAT` | 3 / 58, 1,108, 148 | Reader gaps retained from [dat.md](dat.md) | Exact candidate fits; schemas remain **hypotheses** |
| LANG0 `{ENG,FRE,GER}\CAMPAIGN.STR`, SHELL0 `GAM\CAMPAIGN.STR` | 4 / 3,415, 3,700, 3,564, 3,415 | VSHELL campaign text open `0x40fde3`; packed groups | Each exact, one five-string group |
| SIMALERT `{STR,STF,STG}\GNL_ALRT.STR` | 3 / 2,431, 2,543, 2,494 | DBSIM `0x455950` → STR reader `0x453878` | Each exact, groups 20/40/80 |
| SIMALERT `{STR,STF,STG}\OBJ_ALRT.STR` | 3 / 32, 32, 27 | Packed-STR data framing; specific UI consumer not traced here | Each exact, groups 1/1 |
| SIMALERT `{DBA,HBA}\{GNL_ALRT,OBJ_ALRT}` images | 4 | Shared reader grammar in [dbm.md](dbm.md) | All rechecked to exact EOF; no image export |
| SIMVOL0 `bnd\GNL_ALRT.BND`, `OBJ_ALRT.BND`, `RESULTS.BND`; SIMPATCH `bnd\RESULTS.BND` | 4 / 44, 38, 28, 28 | **Open:** associated UI candidates, not proved mission opcodes | Metadata/hashes only; not claimed parsed |

The 62 mission stems are the 50 `C1_01..C5_10`
campaign-shaped names, `TRAIN1..TRAIN8`, `DEMO`, `DEMO2`, `DEMO_01`,
`DEMO_02`. `DEMO2` alone lacks FRE/GER companions. The 39 zone IDs are
`15,22,39,111,123,222,234,333,401..410,444,501..510,555,666,777,888,`
`1400,1700,1800,3333,3334,7000`. The count/range labels describe the corpus,
not legal maximums or guaranteed runtime reachability.

All five prepared/saved scripts, five MEC files,
five mission STR files, mission.var and the paired results file have bounded
parses. Four SAV examples and GAMEFILE.STR are accounted separately below.
Exceptions are kept visible rather than normalized away:

| Input | Exception | Consequence |
| --- | --- | --- |
| ZONES `DEMO2.MSN` | Final objective needs 58 bytes; only 16 remain | Preserve short-read case; do not pad or read archive suffix |
| `ZONE7000.DAT` + `.DBA` | DAT exponents 7/7 versus 256×256 bitmap | Original bitmap reader derives/overwrites dimensions |
| All five `player*.mec` | Proven writer has 35-byte suffix; inspected DBSIM initializer ignores it | Distinguish writer EOF from consumer stop |
| `SAV/GAMEFILE.STR` | Two consumed groups end at 314; length 318 | Four opaque trailing bytes remain |
| `SAV/GAME_R.SAV` | Reader-defined data ends at 4,753; length 4,763 | Ten opaque trailing bytes remain |

## Authored MSN and prepared script tables

The following schema and transformations come from original static
readers/writers and corpus bounds checks, except meanings explicitly marked
unknown/hypothesis.

Class names "mech", "flyer" and "base/object" below are **hypothesized pool
labels** supported by construction paths; numeric selectors and serialized
widths are independent of those names.

### Complete MSN framing

VSHELL `0x41818b–0x41b277` reads the mission.
The caller `0x41cd61` prepares a mission, calling `0x41818b` at `0x41cd7f`
then the script writer `0x41b278` at `0x41ce20`. VSHELL `0x44dc4d`
constructs `msn\%s.msn` at `0x44dc6c`, based on a selected shell record.

The MSN begins with an `i16` version at offset zero, required to equal 5
by `0x418205`. Starting at offset 2, the following **17 lists occur in
this exact order**. Every list begins with signed `i16 N`; negative counts
are rejected by our audit. Except list 8, each is followed by exactly
`N * stride` bytes. Counts and spans are independent; there is no global
directory or bytecode-length field. Unknown fields are retained as opaque
spans; bulk reads do not prove all word/dword meanings.

| List | Meaning / confidence | Disk stride | N min–max | Total declared records | Count read; record read in VSHELL |
| --- | --- | ---: | ---: | ---: | --- |
| 1 | Preparation conditions | 14 | 2–81 | 2,160 | `0x418243`; `0x4182c3` |
| 2 | Conditional environment/header overlays | 82 | 2–3 | 145 | `0x418733`; `0x418748` |
| 3 | Conditional auxiliary selectors | 8 | 0–9 | 196 | `0x41879a`; `0x418818` |
| 4 | Conditional pre-mission text selections | 144 | 0–1 | 60 | `0x4188b5`; `0x418941` |
| 5 | Post-mission text selections; startup skips | 64 | 0–1 | 54 | `0x418b61`; seek `0x418b8e`; alternate reader `0x41d7ac` |
| 6 | Coordinates / coordinate expressions | 22 | 7–151 | 2,661 | `0x418ba4`; `0x418c23` |
| 7 | Angle candidates, later output as words | 10 | 0–3 | 105 | `0x418ee8`; `0x418f70` |
| 8 | Lists of coordinate references; route meaning corroborated by DBSIM | variable | 1–34 | 470 | `0x4190a4`; `0x41912d–0x4192af` |
| 9 | Spatial-region candidates | 12 | 0–18 | 335 | `0x41964d`; `0x4196d7` |
| 10 | Trigger declarations | 82 | 0–21 | 338 | `0x419803`; `0x419883` |
| 11 | Timed-trigger declarations | 30 | 0–19 | 72 | `0x4199db`; `0x419a5b` |
| 12 | Actor table A (mech hypothesis) | 144 | 2–53 | 1,683 | `0x419bb8`; `0x419c45` |
| 13 | Actor table B (flyer hypothesis) | 102 | 0–8 | 124 | `0x41a05d`; `0x41a0dd` |
| 14 | Actor table C (base/object hypothesis) | 62 | 0–89 | 1,945 | `0x41a3c5`; `0x41a445` |
| 15 | Order declarations | 22 | 1–27 | 637 | `0x41a761`; `0x41a7e1` |
| 16 | Actor-group declarations | 164 | 3–44 | 1,247 | `0x41ab2a`; `0x41abb3` |
| 17 | Objectives | 58 | 1–5 | 128 declared, 127 complete | `0x41afea`; `0x41b06a` |

List 8 records start with five separately
read words: `+0,+2,+4,+6,+8`; the last is signed child count `K`, followed
by `K` two-byte coordinate IDs. Disk length is `10+2*K`. VSHELL's retained route-record stride is
18, with allocated child pointers; it is not the disk stride. DBSIM's
separately constructed runtime route record is six bytes. The 470
records contain **1,488 references**, with child counts 0–9. The old
tentative description of a flat 16-byte family is not a general grammar.

**Exception:** 61 MSN members consume exactly. `MSN\DEMO2.MSN`
has length **8,184**, payload archive offset `0x981c4`.
All lists through 16 fit. Its final count at payload offset `0x1fe6`
(8,166) is 1, so the original reader requests a 58-byte objective at
`0x1fe8` (8,168), but only **16 bytes remain**. It would require an end at
8,226, **42 bytes beyond the payload**. We do not silently pad this record,
read the VOL suffix or call the corpus anomaly-free. Runtime reachability
and original short-read handling remain open. The audit preserves
this as `exact_consumption:false`, not a successful parse.

### VSHELL executes data-driven preparation rules

List-1 records contain an ID at `+0`, dependency ID
at `+2`, operation at `+4`, and operands at `+6/+8/+10`; type-3 `+12`
stores the latest copy-selection draw. `0x417c34` accepts dependency -1, or an ID found
among the already accepted list-1 records. The reader compacts accepted
records in place, so authored order matters. The operation dispatch begins
at `0x4182da`.

| Operation | Static behavior |
| ---: | --- |
| 0 | Require dependency accepted; compare signed variable `0x482bdc + 2*i16(+6)` with signed literal `+10`; `+8` selects comparison. Only true records are retained. |
| 1 | Require dependency accepted; call `0x465d2c` with state `0x482409` and word `+6`, store result in `+8`, retain. Bounded-random interpretation follows the helper; no runtime distribution claim. |
| 2 | Look up dependency with `0x415ff4`: previous type-1 returns its stored result; previous type-3 returns -99; missing returns -1. Accept -99, otherwise require result between **zero-extended** `+6` and `+8` inclusive. |
| 3 | Retain if dependency accepted. |

Comparison dispatch `0x418348`, table `0x41834f`, covers numeric selectors
281 through 286 in order `==`, `!=`, signed `<`, `<=`, `>`, `>=`; branch
targets `0x418367`, `0x4183b9`, `0x41840b`, `0x41845d`, `0x4184af`,
`0x4184fe`. Default sets false. **Corpus totals:** type 0 1,021;
type 1 33; type 2 849; type 3 257. Comparator281 266;283 45;284 3;285 705;
286 2; **282 is implemented but unused by this corpus**.

Most subsequent declaration lists use record `+2`
as their preparation dependency; environment, pre-/post-mission text
selection and objective lists use `+0`.
Failed dependencies skip retention, though their serialized spans are still
read. Duplicate IDs can replace/merge an earlier retained declaration,
and references are resolved to compact indices. These rules explain why
an MSN is not a byte-identical runtime script. The complete per-list merge
sentinels, random template selection, compact remapping, variable-bank setup,
retained reset selectors and shell-mode adjustments are documented in
[VSHELL preparation](../reverse-engineering/vshell_preparation.md).
The independent C17 implementation reproduces C1_01 and DEMO's captured
four-file preparations exactly; broader state/seed parity remains to be
compared with the original in-vitro harness.

**Environment overlay:** list-2 is 82 bytes: condition at `+0`,
ten parameter words `+2..+20`, then 30 variable-ID words `+22..+80`.
`0x41699d–0x416ac8` overlays parameter values conditionally (the first
has special sentinel 1, the others special sentinel 0); non--1 entries
in the last 30 slots replace corresponding variable slots. `0x417c7d`
then clears the selected variables in the 1,000-word mission bank.
World/zone meaning comes from the final script consumer, not raw first
environment values alone.

**Coordinates:** list-6 fields are ID `+0`, dependency `+2`,
random copy selector `+4`, unresolved `+6`, expression flag `+8`, and three
four-byte values at `+10,+14,+18`. A non--1 selector uses type-3/type-2
conditions to choose an earlier coordinate by its dependency, not its ID
(`0x4165e7–0x416798`, `0x418c62–0x418d18`). If expression flag is nonzero,
after any copy, the low
words of values `+10/+14` select two prior points, and their three dwords
are added component-wise (`0x418d28–0x418e72`). Otherwise these are direct
coordinate dwords. The writer `0x41b430–0x41b49b` emits the resolved triple
unchanged. Exact axis labels/units inherit the separate consumer evidence;
this does not reinterpret every MSN dword as a physical coordinate.

### Prepared script exact framing

`build/installs/timing-r111/DATA/script.dat` is 12,286 bytes.
There is **no MSN version word**: the first 20 bytes are ten parameter
words. DBSIM copies them at `0x4246b3` to `0x4a9e4e`, clears `+4`, and
calls world loading at `0x4246c9`. World-resource selection is described below.
The selector is the wrapped word result `signed16(2*word 0+word 9)`;
this arithmetic detail is at `0x42e050–0x42e075`.
Zone selection uses header word 1 (`+2`): `0x4252ee` loads it and
`0x4252f7` passes it as the first argument of `0x42789c`; the loader
formats that signed word as decimal at `0x427915–0x427931`. Negative
values take the no-terrain branch. VSHELL `0x41b2ec` writes literal 1 to
header `+4`, which DBSIM clears on load. Across the five prepared examples,
header word ranges at offsets0,2,...,18 are respectively
0–1,123–7000,1,0,0,0–1,0–1,0–1,0,0. These are measured examples,
not legal-range claims for all mission configurations.

From offset 20, 13 signed-count lists occur. An audit checks all
bounds, all route references against the coordinate count and exact EOF.
Names identify observed consumers; unknown field semantics remain open.

| List | Count offset / N | Payload stride | End (exclusive) | DBSIM count / record reader |
| --- | --- | ---: | ---: | --- |
| Coordinates | 20 / 84 | 12 | 1,030 | `0x4246dd` / `0x42474b` |
| Angles | 1,030 / 1 | 2 | 1,034 | `0x4247f2` / `0x424831` |
| Routes | 1,034 / 9 | `2+2*K` | 1,136 | `0x424871` / `0x4248c2,0x4248f6` |
| Regions | 1,136 / 8 | 6 | 1,186 | `0x424948` / `0x42498f` |
| Triggers | 1,186 / 8 | 74 | 1,780 | `0x4249da` / `0x424a30–0x424ad2` |
| Timed triggers | 1,780 / 0 | 24 | 1,782 | `0x424b62` / `0x424baa` |
| Mechs | 1,782 / 10 | 134 | 3,124 | `0x424c8b` / `0x424cb6` |
| Flyers | 3,124 / 4 | 92 | 3,494 | `0x424cdf` / `0x424d07` |
| Base/objects | 3,494 / 50 | 52 | 6,096 | `0x424d30` / `0x424d58` |
| Orders | 6,096 / 14 | 14 | 6,294 | `0x424d81` / `0x424da3` |
| Actor groups | 6,294 / 38 | 156 | 12,224 | `0x424dd1` / `0x424e15,0x424e32` |
| Objectives | 12,224 / 1 | 54 | 12,280 | `0x424ec4` / `0x424ee6` |
| Mission-string indices | 12,280 / 2 | 2 | **12,286** | `0x424f03` / `0x424f58` |

Routes hold nine child counts totaling 41 references; all are valid compact
coordinate indices. Angle words are multiplied by 182 by `0x424836–0x424852`,
supporting authored degrees, but degree naming is a hypothesis without
independent calibration.

**All prepared examples:** the same parser consumes
all five `DATA/script.dat` and `SAV/script*.dat` files exactly, with no
negative counts, out-of-bounds spans or invalid route coordinate indices.
`SAV/script0.dat` and `script10.dat` are byte-identical to the
12,286-byte DATA file. `script1.dat` has the same
length and list counts but differs in content.
Its 14 differing bytes are at header offset 14 and in the mech declaration
span (offsets2496–3036); this identifies different prepared state, not a
framing exception.

**A script copy can carry a stale tail.** VSHELL
rewrites `data\script.dat` without truncation (`0x41b278`,
[file_io.md](../reverse-engineering/file_io.md)) and copies it whole to
`sav\script%d.dat`. `native-r111/SAV/script10.dat` (12,286 bytes)
has lists ending at 12,108 (counts
55,1,5,5,6,1,16,2,50,15,36,2,2, 13 route references); its last 178 bytes equal
those of the 12,286-byte file above. `formats/src/script.c` (`//formats:script`)
parses this framing, reports bytes after the last list as `trailing`, and
`//formats:script_real_test` checks every installation's `SAV/script<n>.dat`
(31 files, six distinct; the two documented examples with exact EOF).
`SAV/script11.dat` is 4,514
bytes.
Its ordered list counts are 29,1,7,1,4,0,8,3,9,10,11,1,2; its seven
routes contain 36 valid coordinate references. All five per-list offsets
and exact-consumption results check.
The final list's entries are also checked against the companion
`DATA/mission.str` or `SAV/missn*.str` string count; all five files pass. The
label `variables` is incorrect: original writer and consumer prove
that these words select mission text, not mission-variable cells.

#### Conditional presentation records and source-ID remapping

**8-byte list 3:** `+0` is an authored ID, `+2` a
preparation condition. `+4` remains semantically unresolved. `+6` is an
auxiliary value later substituted into the last word of the pre- or
post-mission text-selection record. At `0x418854`, helper `0x416107`
searches the already accepted list 3 prefix for the current authored ID.
If found, `0x418884` replaces that earlier entire 8-byte record; the
accepted count is unchanged. Otherwise the accepted count increments.
Rejected conditions do not increment the count. This is ordered
conditional replacement, not interpretation of an 8-byte instruction.

**144-byte list 4:** condition `+0`, ten string IDs
`+2..+20`, thirty string IDs `+22..+80`, thirty string IDs `+82..+140`,
and auxiliary selector ID `+142`. Every non--1 string ID is remapped
through `0x4160a0` at `0x4189be`, `0x418a31`, `0x418aa4` to the compact
retained language table. The selector is resolved through list 3 by
`0x416107` at `0x418b0d`, then replaced by that record's `+6` word.
`0x41ce64–0x41ce89` passes the first retained record to `0x4134f2`,
which installs its three text arrays and selector in shell state.
Exact UI names for each of the three arrays remain a hypothesis.

**Final script list:** `0x41c79f–0x41c810` counts
non--1 words among the first ten string indices in the first retained
list 4 record and writes that count followed by the **first N words**.
It does not gather sparse slots. The DBSIM presentation consumer at
`0x45779d–0x4577e3` indexes the mission-string pointer table `0x4aa660`
with these words. They therefore belong to presentation data, not the
mission-variable bank. Contiguous-prefix authoring is required by this
writer; whether every conditional language combination preserves that
property remains untested.
All 60 raw list 4 records have a contiguous prefix
of non--1 values in that first ten-slot array; this alone does not prove
that subsequent conditional string remapping never removes one.

**64-byte list 5:** startup reader skips the span,
but post-mission reader `0x41d072` parses it: count `0x41d72e`, records
`0x41d7ac`; condition `+0`; thirty string IDs `+2..+60` remapped through
`0x4160a0` at `0x41d81e`; selector ID `+62` remapped by `0x416107` at
`0x41d879` and replaced with list 3 `+6`. Its 144-byte sibling is skipped
by this alternate reader at `0x41d718`. Thus neither skipped family is
unconditionally irrelevant: startup and return use different sections.

**Second replacement example:** coordinate list 6
first resolves its copy/expression fields, then searches its accepted
prefix by authored ID via `0x416168` at `0x418e89`. A match replaces
all 22 bytes at the existing slot (`0x418eb7`); a new ID increments the
accepted count (`0x418ec1`). Helpers `0x416107`, `0x416168`, and string
lookup `0x4160a0` have a mode byte: mode 0 counts tombstones (records
whose ID is -1) while searching; mode 1 excludes them from the returned
index. This is the concrete distinction between authored IDs, retained
array positions and compact serialized indices. Raw MSN reference
numbers must never be assumed to be direct script/runtime indices.
Counting repeated nonnegative authored IDs in raw
records gives 86 repeated IDs across 45 members for list 3, and 509 across
24 members for coordinates. This counts possible conditional replacement
sites, not the number of replacements under one campaign state.

All five prepared
scripts pass **812 nonnegative known index/span checks**: 189 each for DATA,
script0, script1 and script10, and 56 for script11. This covers consumed region,
order, group and objective references (including three-string message spans),
presentation indices, known variable-bank slots, and the mapped actor-table-A
point/angle/event references. Negative handling follows each original consumer's
exact -1, any-negative or first-negative rule; all accepted negative values in
these examples are -1. No out-of-range exception occurs in this checked subset.

**Open coverage limits:** all five timer lists are empty and their trigger types
are only 0/1, so timer and typed-trigger-subject checks are unexercised. The first
group's serialized actor slots are replaced by the player roster and are not
treated as consumed references. Actor pool remapping/membership is not simulated;
flyer/object direct references and variable fields remain partly unverified.
Trigger event selectors are not assumed to index mission strings. This bounded
reference audit does not certify every serialized word or every authored MSN ID.

#### Trigger serialization is different from runtime memory

MSN list 10 has stride 82; script trigger stride 74; runtime
stride 58. The writer `0x41b750–0x41b8b2` maps the retained MSN record:

| MSN record offset | Script record offset | Length / operation |
| --- | --- | --- |
| `+6,+8` | `+0,+2` | two words (trigger type and activation mode) |
| `+10` | `+4` | 16 bytes, eight region indices |
| `+28+4*i` | `+20+2*i` | ten variable-index words |
| `+30+4*i` | `+40+2*i` | ten operation words |
| `+68` | `+60` | ten bytes |
| `+78,+80` | `+70,+72` | two words |

This drops prefix/unused words, resolves IDs before serialization and
transposes the ten pairs into two parallel arrays. The corresponding
DBSIM reader maps script `+0,+2` directly to runtime `+0,+2`; counts valid
region IDs until a negative sentinel or eight entries, builds region
pointers at runtime `+6`; initializes runtime `+10` to zero; copies the
two ten-word arrays to runtime `+12,+32`; reads the ten-byte script span
at `+60` into a temporary; copies script `+70` to runtime `+52` and
decrements it; sign-extends script `+72` to runtime dword `+54`.
Runtime operation semantics are described below.
**Corpus statistics:** trigger predicate types are 0:142,
1:170,2:3,3:15,4:4,7:3,9:1. All 3,380 action slots are either
sentinel -1 (3,314) or action6 (66); this is disk `MSN+30+4*i`,
not the paired variable IDs at `MSN+28+4*i`.
Trigger activation-mode counts at `MSN+8` are -1:1,0:66,1:172,2:63,3:36;
modes4/5, which the logic trace finds implemented, are unused by this corpus.

#### Objective serialization

MSN final stride 58 becomes script stride 54. At
`0x41c5d3–0x41c78b`, MSN words `+2,+4,...,+14` become the first seven
script words `+0..+12`; ten variable-index words at `MSN+18+4*i`
become script `+14+2*i`; their paired operation words at `MSN+20+4*i`
become script `+34+2*i`. MSN condition `+0` and word `+16` are omitted.
The bulk54 reader at `0x4262b5` later realizes runtime objective objects.
Script `+0` is the required
marker, `+2` is the predicate type, `+4` subject scope (0 group; 1/2/3 actor pools,
provisionally mech/flyer/base), and `+6` subject index. Thus MSN predicate type is `+4`,
not `+2`. Dispatches are at `0x4132cf` for groups and `0x4133d7` for
actors. **Corpus statistics**, excluding the incomplete
DEMO2 objective: required markers0:36,1:91; predicate types0:19,1:67,
2:27,3:5,4:3,6:5,7:1. Action slots contain -1:1,236,6:24,7:10.
All counts come from the corrected disk offsets.

#### Order and actor-group wire mapping

The first script pass merely skips 14-byte order
records; the second reader at `0x425ee1–0x425fa7` creates 22-byte runtime
orders. Seven disk words map as follows: `+0` type to runtime0, `+2`
unresolved word to runtime2, `+4` coordinate index to runtime point
pointer4, `+6` route index to runtime pointer8, `+8` subject scope to
runtime word 12, `+10` subject index resolved with that scope through
`0x425348` to runtime pointer14, `+12` trigger index to runtime pointer18.
The point/route/trigger cases test the exact -1 sentinel here.

The 156-byte group record read at `0x42600c`
contains actor pool type at `+40`, another word at `+42`, optional
coordinate index at `+44`, optional angle index at `+46`, twenty actor
IDs at `+50..+88`, ten order IDs at `+90..+108`, a byte consumed at
`+110`, and gate-trigger index at `+112`. The two ten-word arrays at
`+114..+132` and `+134..+152` are copied to runtime arrays at `+28` and
`+48`; their action meaning is not assigned in this resource report.
`0x4260ba–0x4260e4` resolves order references and `0x423b34` installs
the resulting ten pointers at runtime `+68..+104`, clears the ten
completion bytes at `+112..+121`, sets current order index at `+108`
to zero, installs the gate pointer at `+20`, and attaches actors from
the pool selected by group type 0/1/2. The first group is special: its
actor slots are replaced with the player's roster at `0x426014–0x426051`.
Consequently a portable loader cannot instantiate all disk group slots
uniformly without preserving that handoff behavior.

**Independent cross-check:** it agrees with the original
trigger ten-slot variable/action pairing, the required-vs-failure marker,
and timed-trigger24-to-runtime49 layout. Objective predicate5 jumps to
the common branch at `0x4134e2` without a fresh ESI result in both tables;
it is unused in the complete corpus objectives. The first unmet required
objective stores the **address of its four-entry string-pointer array**
(`LEA [objective+18]` at `0x4134f7`) in `0x4d1e98`; status 5 uses that
array in the alert. It does not store a direct character-string pointer.

## World, terrain and localized resources

### World resource selection

DBSIM 0x4246a8 reads the first 20 script.dat bytes into 0x4a9e4e, resets the
word at +4 to zero (0x4246bb), then calls 0x42e010 at 0x4246c9. That function
copies these 20 bytes to 0x4cfcc4 and constructs `world<signed16(2*A+B)>` where A is the
signed word at script offset 0 and B is the signed word at offset 0x12
(0x42e042..0x42e075). Biome/day-night names are hypotheses. The observed resource
set is WORLD0..WORLD9 in each of wld, rmp, and dpl; impact0..impact9 palettes
are named in the WLD strings. 0x42e07d reads DPL via resource construction
0x492bec then 0x430394. 0x42e09f constructs WLD; literal `wld` VA 0x49af8e.
0x42e529 optionally opens same-stem `cyc` (VA 0x49af92); no CYC members occur
in the 13 installed-family archives. It then loads `shades` (0x42e5d6), RMP
(literal at 0x49af9d; open 0x42e621) and material/texture resources.

### WORLD*.WLD grammar

Ten members, total 3116 bytes, sizes310..313; all exact EOF. No tag or magic.
Fourteen individually read two-byte words at file0x00..0x1b
(0x42e0f8..0x42e256). First eight read 0x42e0f8..0x42e182, further six
0x42e1f6..0x42e256. Some values are sign-extended into runtime fields;
remaining field meanings unresolved. Two tables follow:

- Four-byte stored count A at 0x1c, followed by 4*A bytes. The reader actually
  stores/uses signed low16 of that count (0x42e280..0x42e2c1). A=16 and upper
  word 0 in all ten. Second four-byte count B and 4*B bytes follow; same low16
  rule at 0x42e2d5..0x42e316; B=16. Do not generalize to a full u32 count.
- At 0xa4: signed word D=16; at 0xa6: signed word C=11, followed by 4*C bytes
  at 0xa8. At 0xd4: another word, value 11, then a second 4*C-byte array at 0xd6.
  The second array length explicitly reuses C, not that second stored word
  (0x42e318..0x42e378). D is used for generated output rows by 0x430d08.
- At 0x102: two four-byte values (0x42e3c9..0x42e3ea); at 0x10a two words
  (0x42e426..0x42e444); at 0x10e two four-byte values (0x42e44a..0x42e464).
- At 0x116: five sequential NUL-terminated resource strings, byte-loop reads
  at 0x42e470,0x42e48b,0x42e4ad,0x42e4f2,0x42e50d. Lengths excluding NUL:
  7,7,7,3..6,3. One short example is the last `tex`. Third is passed to
  palette loading; fourth reaches 0x46bd9c (0x42e695). Complete resource-role
  semantics remain open; first two names are read into the same temporary
  without a consumer in this inspected path.

The fixed prefix reaches 278 bytes and five terminated strings consume the
remainder. This is environment/render configuration, not an observed event
instruction stream. Exact environmental meaning per numeric field remains
hypothesis.

### WORLD*.RMP

DBSIM0x42e629..0x42e64f reads two four-byte values A,B, multiplies them at
0x42e655..0x42e65b and reads256*A*B bytes at 0x42e676..0x42e67f via the ordinary
stream helper 0x4735b8. All ten have A=32,B=12 and length 98312=8+32*12*256,
total 983120. No trailer. The byte block is passed into rendering state
(0x42e6a2..0x42e6cf); palette/shade-remap interpretation is a hypothesis
until individual lookup use is traced. It is not shown to encode objectives.

### ZONE DAT and DBA

All 39 DATs are 16 bytes, four separate four-byte reads at 0x427988..0x427a19.
Filename construction `zone` plus signed decimal ID:0x42790c..0x427944.
Third field goes to terrain object+0x108, fourth to+0x118. First two are
passed to constructor 0x46befc but not used by that inspected constructor.
Later the same stem with `.dba` is loaded (0x427a71..0x427a8a) through
0x46c754. That function selects the first bitmap from the array at 0x46c864,
then calls 0x46c4c4 at 0x46c86f.

All 39 DBA members have one raw8-bit bitmap (32 at 128x128;7 at 256x256),
exact DBM framing and EOF, total 984366 bytes including tags/alignment. They
are functional terrain inputs, not merely briefing pictures:0x46c4dc..0x46c50b
derives grid exponents from bitmap width/height;0x46c56a..0x46c5df reverses
row order and copies each source byte into byte 0 of 16-byte grid cells.
0x46c70b..0x46c740 combines those bytes with terrain+0x118. Calling the
bytes heights is strongly supported by this terrain use; exact physical
height interpretation still requires the interpolation/accessor chain.
This path also draws RNG streamA at 0x46c69d for cell metadata, so mission
initialization includes terrain-dependent RNG consumption.

Exception: first two DAT fields match log2 bitmap width/height in 38/39 pairs.
ZONE7000.DAT contains 7,7 but ZONE7000.DBA has 256x256. Do not reject it or
size the actual grid exclusively from those DAT fields; original bitmap
loader overwrites runtime dimensions from image dimensions. All files are
byte-complete; this is a semantic cross-file mismatch, not truncated data.

### Localized .ENG/.FRE/.GER mission records

184 members in ZONES:62 ENG/119560 bytes,61 FRE/127463,61 GER/130527.
All pass bounds and EOF. Reader VSHELL0x417cb0 chooses suffix by language
at 0x417cf2..0x417d2f, reads signed count at 0x417d7a..0x417d88. Per record:
three two-byte fields at+0,+2,+4 and signed two-byte length L at+6, then
L bytes at+8. Reads0x417e18..0x417e92; text read 0x417f62..0x417f7e or
skip0x417fb6..0x417fcb. In every member L includes exactly one final NUL,
no interior NUL; 3..49 records/member. Totals1122/1117/1117 records.
Field+0 is a referenced ID (0x417f92..0x417f97); +2 participates in shell
condition filtering0x417e95..0x417eb7 through 0x417c34; +4 is -1 or -99
in this corpus, meaning unresolved. Text encoding beyond the original byte
values is not established by this framing. The ENG-only extra stem is `DEMO2`; its runtime reachability is unresolved.

These records are selected before VSHELL writes DATA/MISSION.STR at 0x418014;
the latter strips IDs/conditions and has a different packed string format.
0x418083 writes a temporary dword9999, writes signed count and each
length/string/additional zero (0x418099..0x41812e), then seeks back and
replaces first dword with bytes written minus 4 (0x418130..0x41815d).
The language records therefore are not raw STR files, and their conditions
can affect which runtime text index corresponds to a source ID.
The [preparation contract](../reverse-engineering/vshell_preparation.md#language-and-serialization)
specifies duplicate string replacement, compact references and the
per-string extra zero, with byte-exact acceptance for both captured cases.

### LANG0 MISSIONS.BIN

Three members ENG/FRE/GER\MISSIONS.BIN, each 995 bytes. Two four-byte fields:
count N=61 at 0; pool size S=865 at 4; N unsigned16 offsets at 8; S bytes of
NUL-terminated string pool at 8+2*N. Exact995=8+122+865. All offsets resolve
and ordered strings consume the pool exactly. All three copies are identical.
This is an indexed name list, not executable logic.

VSHELL CAREER loader0x412f4b pushes `missions.bin`, calls 0x408bb5; that
selects language prefix and invokes0x40883a at 0x408cb4. Reader0x408845..0x40889a
proves4,4,2*N,S framing. Index resolver0x40882b..0x408834 zero-extends each
16-bit offset and adds the pool pointer; CAREER reader 0x412e88 uses it
per dat.md.

## ES, VSHELL and DBSIM file handoff

### Process chain and arguments

The shorthand `VSHELL -> DBSIM` hides an important process boundary: **ES.EXE actually starts both executables** through imported CW3220 `_spawnv`. ES thunk `0x4024f4`, IAT `0x404250`; VSHELL call `0x40156b`, DBSIM call `0x40164f`. Each receives mode argument zero; the returned value is stored into state global `0x403070`. The jump table at `0x4014d3` selects:

| Returned state | Next branch |
|---|---|
| 0 | stop |
| 1, 3, 4, 6 | VSHELL at `0x4014ef` |
| 2 | DBSIM at `0x401582` |
| 5 | DBSIM via `0x40157d`, additionally `-D` |

DBSIM arguments are built from `-eggplant` (`0x40158c`), `-X` plus the current state (`0x401594..0x4015b2`), and `-R` plus an initially zero, post-incremented counter (`0x4015b6..0x4015ce`; initialization `0x401385..0x4013a9`). Therefore ordinary state 2/first launch yields `-eggplant -X2 -R0`. ES reads one byte from `data/language.cfg` into `0x403075` (`0x4012e8..0x401305`); prefix `-` is at `0x403074`. English `E` is cleared; any nonzero remaining language character is passed (`0x401631..0x401640`). German `G` therefore supplies `-G`. Other option branches exist; this is not an assertion that all invocations have precisely four options. Prepared language file: one byte `G`.

VSHELL mission start paths call `0x41cd61` (e.g. `0x44db91`), then prepare the player handoff (`0x40f68c`, e.g. `0x44dbd5`) and set state 2 through `0x408d1a` (`0x44dbda..0x44dbe2`). The latter stores its signed-word argument to `0x46e348`; it is not a direct process-spawn API.

DBSIM return function `0x461ff0` passes the known active player pointer `[0x4d24e6]` to result writer `0x42412c` at `0x462029`. The following code sets exit state `0x4d27b8` to 0, 3, 4 or 6 depending on state (`0x46202f..0x462080`). Full semantic labels for those branches remain hypotheses. On VSHELL initialization, launch states 3 and 4 reach `0x4018a1`: load save index 10 via `0x40eaaa` at `0x4018af`, then read mission results via `0x40f09f` at `0x4018b7`. This is a restart-and-file-return protocol, not DBSIM reading its own results back.

Binary identities (hashes in `data/reference_hashes.txt`):

| Binary | Bytes |
|---|---:|
| ES.EXE | 11,808 |
| VSHELL.EXE | 566,304 |
| DBSIM.EXE | 724,512 |

### The four launch files

| File | VSHELL producer / DBSIM reader |
|---|---|
| `DATA/script.dat` | `0x41cd61` loads selected MSN via `0x41818b` (`0x41cd7f`), adjusts setup words by shell mode (`0x41cd85..0x41ce12`), then calls writer `0x41b278` at `0x41ce20`. DBSIM `0x424308`, open `0x42464d`, first read `0x4246a8`. The complete script framing is documented above. |
| `DATA/MISSION.STR` | Writer `0x418014`; DBSIM opens at `0x4245db`, calls `0x453878`, then `0x4539a0` to obtain strings. |
| `DATA/player.mec` | Writer `0x40f68c`, individual equipment helper `0x410cdb`, appended item flags `0x412877`. DBSIM opens `0x42447c`, reads `0x4244c0..0x4245b3`, closes `0x4245bc`. |
| `DATA/mission.var` | Writer `0x40ef83`, called first by `0x40f68c` at `0x40f69f`. DBSIM read `0x424428..0x424436`. Updated by simulator and written back by `0x42412c`. |

#### mission.var

Exactly 2,000 bytes, no header/count/string. VSHELL writes/reads the block at `0x482bdc` using explicit size `0x7d0` (`0x40efde..0x40efef`, `0x40f06c..0x40f07d`). DBSIM maps it to `0x4a9e70` (`0x424428..0x424436`) and writes the same range at `0x4242d0..0x4242de`. Indexed two-byte variable operations elsewhere establish a 1,000-word variable bank; field meanings depend on the mission/system logic.

The initial read is not an exact runtime-state restore: immediately afterwards DBSIM zeros words at offsets `0x28` and `0x14` (`0x42443b`,`0x424447`) and the 22 words at offsets `0x2a..0x54` inclusive (`0x424450..0x424466`). Other loaded words are retained by this inspected block. The runtime section below identifies variable action and condition consumers.

Prepared example: 2,000 bytes, four nonzero words. This is one read-only snapshot, not evidence of an unmodified initial game state.

#### player.mec

Own C17 reader/writer and template editor:
[`formats/player_mec`](../../formats/README.md#playermec-templates).
It preserves opaque blocks and the writer's suffix; it does not reproduce
the original initializer's unchecked reads or infer condition semantics.

The independent `shell/` preparer also creates MEC from
CD defaults and restored SAV state, including mode-zero authored equipment.
The [default-state contract](../reverse-engineering/vshell_preparation.md#default-state-and-mission-equipment)
records byte offsets, sparse-slot behavior, pilot activation order and runtime
table sources. The [differential sweep](../reference-game/shell_invitro.md#differential-sweep)
compares all four launch files and returned RNG/reset state with the original;
matched original assertions are reported separately from successful parity.

All multi-byte quantities are little-endian. `W` below is a two-byte field whose full semantic interpretation is unresolved. A signed loop/count is denoted `i16`.

| File/record offset | Serialized width and operation |
|---|---|
| File 0 | W, emitted as zero by inspected VSHELL writer |
| File 2 | i16 N, number of player/team records |
| Record R+0 | W; pilot/character identity is a hypothesis |
| R+2 | W; pilot skill is a hypothesis |
| R+4 | i16 vehicle ID; writer takes the same shell vehicle/loadout ID described in dat.md; simulator tests first record against 8 for a branch at `0x4245cd` |
| R+6 | i16 K, slot count |
| R+8 | K two-byte item IDs; writer takes object `+0` or writes zero for absent slots |
| R+8+2*K | K two-byte secondary values; writer takes item `+8`, default 5 for absent slots; ammunition variant is a hypothesis. VSHELL side: the WEAPONS guidance buttons store the missile guidance mode there (SARH 0, ARH 1, ARM 2, EO 3; `0x43e346`), a mount stores 1 for racks 13..15 and 5 otherwise ([vshell_ui.md §10](../reverse-engineering/vshell_ui.md#10-service-bay-and-weapons)); what DBSIM does with it is not traced |
| R+8+4*K | W, emitted zero by inspected writer |
| R+10+4*K | 26-byte block (13 words) |
| R+36+4*K | 20-byte block (10 words) |
| R+56+4*K | 20-byte block (10 words) |
| After N records | i16 A=33, then A single-byte flags from shell per-item table |

Each record is `76+4*K` bytes. The runtime record has stride 126 (`0x4244e9..0x4244ee`, `0x4245a9`) and fixed array locations; never use that as the disk stride. The final three blocks map to equipment condition data used by the result reader; health/armour/ammunition category labels remain hypotheses.

**Reader exception:** DBSIM closes after the N records, leaving the 35-byte item-flag suffix unread in this initializer (`0x4245b9..0x4245bc`). This is not unexplained garbage: VSHELL `0x412877` explicitly writes count 33 and 33 one-byte entries; call is `0x40f8ae`. Another DBSIM early setup reader `0x46154c..0x4615ee` only reads the first five words and tests the vehicle ID; neither inspected reader consumes the suffix. Runtime use by a different path remains open.

All five prepared/saved examples parse to exact EOF with the proven writer grammar: DATA215B and SAV player10 215B have N2, K3/3; player0 and player1 are 127B, N1/K3; player11 is 151B, N1/K9. Flags contain only 0/1. DATA equals player10 byte-for-byte.

#### MISSION.STR

Writer grammar: u32 body byte size at 0; i16 N at 4; N repetitions of i16 L, L bytes including terminating NUL, one zero byte. No separate string-index array. Writer first reserves size with temporary 9999, then backpatches actual bytes written minus 4 (`0x418130..0x41815d`). It uses `strlen+1` (`0x4180c7..0x4180d4`). DBSIM `0x4538d0` loads body; iterator `0x453918` reads count/length and advances over the extra byte. `0x4539a0` iterates the group; caller strips a final LF from in-memory strings (`0x424622..0x42463e`). Strings are original byte sequences; a specific text encoding is not proved here.

All five launch/save examples exactly consume the file: DATA and missn0/1/10 have 11 strings and 939 bytes; missn11 has 8 and 882 bytes. No text is dumped. The first four are identical. Localized archive source grammar and condition filtering are documented above.

### Return results.dat

Writer DBSIM `0x42412c` uses the active player and its team. The file contains:

| Offset/sequence | Layout |
|---|---|
| 0 | Two-byte Boolean: whether objective evaluator `0x413280(player)` returns 9 (`0x42414b..0x424159`). The runtime section establishes the exact status 9 gate. |
| 2 | Four-byte additive result tally; VSHELL adds it to global `0x482bd8` at `0x40f12d..0x40f13a`. Score/currency label is a hypothesis. |
| 6 | i16 P, pair count from `0x4a9e6a` |
| 8 | P records of two words from `0x4a9e6c`, writer `0x424230..0x42426a`; shell `0x40f16b..0x40f1b8` reads them and campaign mode 1 passes them to `0x4128c1` (adds per-item inventory objects). Salvaged equipment is a hypothesis. |
| 8+4*P | Team actor result records, **implicit count/order** from launch state; each 72 bytes. |

Each actor result is 13+10+10 two-byte quantities plus 6 bytes. Writer `0x423d68` obtains actor condition data via `0x41b4ec`; each of the 33 words is transformed as `((256 - signed_word)*100) >> 8` with signed arithmetic (`0x423da4..0x423e54`), followed by 6 bytes at actor+0x2a4 (`0x423e56..0x423e64`). The meaning of each condition component remains unresolved. The shell consumes the 66-byte condition span via `0x411d44`, removes equipment whose condition is zero, and consumes the final three words via `0x410632` into per-pilot counters and accumulators. There is no actor ID or actor count encoded between these blocks.

Prepared results 152 bytes exactly parse as 8-byte prefix, P0, two 72-byte actor records, matching N2 in the paired prepared player.mec. This pairing is data consistency evidence; it does not establish which historical play produced the result.

VSHELL result routine `0x40f09f` reads mission.var first (`0x40f0b2`), then results.dat (`0x40f0bd..0x40f0c6`). At `0x40f13f..0x40f149` it replaces variable-bank word 0 with the result Boolean. Subsequent campaign progression through `0x4133eb` uses the result (`0x40f52c..0x40f53d`). The covered campaign advancement and debrief paths are described below; wider shell policy remains open.

### SAV relationship, scope and exception

The save operation `0x40e933` includes helper `0x413095` at `0x40e9e1`. This writes mission selection metadata and copies the three launch companions using exact-byte copy helper `0x40da8d`: DATA script/mission/player to `SAV/script%d.dat`, `SAV/missn%d.str`, `SAV/player%d.mec` at `0x41316b..0x4131da`. Restore helper `0x4131e3` reverses those copies at `0x413281..0x4132f0`; caller `0x40eaaa` at `0x40eb5a`.

The variable bank is embedded in the SAV itself: after earlier variable-size sections and a four-byte tally, writer `0x40ea3b..0x40ea4c` stores 2,000 bytes from 0x482bdc, then a word and 20-byte block; restore reads that same sequence at `0x40eb98..0x40ebe5`. There is no numbered `.var` companion in the inspected paths. The full framing, including one prepared-file trailing-byte exception, is accounted below; many semantic field names remain hypotheses.

#### Complete SAV framing

The full writer sequence is `0x40e9d2..0x40ea81`; its reverse reader sequence is `0x40eb47..0x40ebe5`. Starting at file offset zero:

| Sequence | Serialized grammar and original evidence |
|---|---|
| 33 inventory categories | Each: one byte flag, i16 N, N records of five words (10 bytes). Outer writer/reader `0x4127f3`/`0x412781`; category writer/reader `0x412488`/`0x4123df`; item writer/reader `0x412123`/`0x4120ca`. |
| Five-entry bank | One word, then exactly five `(i16 index,word value)` pairs: 22 bytes. Both loops fixed at five, irrespective of first word (`0x41281d..0x41286f`, `0x4127ae..0x4127ec`). Observed indices 0..4. |
| Mission selection state | 152 bytes in widths `2,2,2,20,2,60,2,60,2`. Writer `0x413095..0x413168`, reader `0x4131e3..0x41327e`. In order, globals `0x46fc58`, `0x46fc5a`, `0x4840cc`, `0x4840ce`, `0x4840e2`, `0x4840e4`, `0x484120`, `0x484122`, `0x48419c`. Their complete semantic labels are unresolved. |
| Roster | Exactly 36 pilot-shaped records (three groups of 12), then two six-byte selection arrays. Outer `0x41023a`/`0x41029b`; per-record `0x410383`/`0x410520`. |
| Player/team | Two words, then one more pilot-shaped record. `0x410791`/`0x4107dc`. |
| Loadouts | i16 N, N records each prefixed by i16 index into eight-slot loadout table. `0x410c7c` writes occupied slots; `0x410e2e` reads N records. |
| Nine-word bank |18 bytes, writer `0x411f78`, reader `0x411fad`. |
| Result tally |Four bytes from `0x482bd8`. |
| Persistent variable bank |2000 bytes from `0x482bdc`. |
| Final fields |Two bytes from `0x4826f2`, then 20 bytes from `0x4833ac`. |

A pilot-shaped record has two words, i16 L, L name bytes including NUL, then 25 bytes in the exact widths `2,1,2*11`: total `31+L`. The fixed suffix corresponds to memory offsets +0x22 (word),+0x24 (byte), then words +0x25..+0x39. These bytes include the per-pilot quantities updated by results; their complete semantics are unresolved. No names are emitted by the verifier.

A loadout record, excluding its preceding table index, has 78 fixed bytes then 12 bytes per occupied item: two words, 66-byte condition block, two words, i16 capacity K, i16 occupied count M, then M `(i16 slot_index,10-byte item_record)` records. Writer `0x411862..0x41194b`; reader `0x4116e6..0x411861` explicitly loops on M, not K. The writer scans K slots and emits each non-null item. All inspected slot indices are unique and in range; item semantics are not assumed merely from their position.

The save audit bounds-checks every span and count, validates names terminate at their declared lengths, validates five-bank and loadout slot indices, and reports offsets without names or original state dumps. All four prepared saves contain39 inventory item records. Results:

| File | Bytes | Loadouts | Variable bank offset | Reader consumption | Exception |
|---|---:|---:|---:|---:|---|
|GAME_0.SAV|4753|6|2731|4753|None|
|GAME_1.SAV|4673|5|2651|4673|None|
|GAME_R.SAV|4763|6|2731|4753|10 trailing bytes (stale tail, see below)|
|GAME_T.SAV|4322|1|2300|4322|None|

The GAME_R trailer is not reader-defined data; every byte is accounted as
either reader-defined data or this suffix. Its origin is explained by the
write sequence below: the original never truncates a SAV, so the suffix can
only be bytes of an older, longer file at the same path.

The independent `formats/sav.h` reader/writer implements this framing,
preserves opaque fields and trailing bytes, and supports variable-bank and
campaign-position edits. All four examples round-trip byte for byte in memory;
see [the preparation audit](../reverse-engineering/vshell_preparation.md#sav-reader-and-writer).
It does not infer the remaining field meanings. How it models the stale tail
is described at the end of the next subsection.

#### SAV write sequence and stale tail

The save
operation `0x40e933(slot, label)` runs these steps, in this order:

1. Slot 10 becomes 11 when `0x4826f0` is zero (`0x40e962..0x40e974`).
2. `0x40e6cd` stores the label as the display name of the slot (`_strcpy`
   thunk `0x46b9fa` into `0x482701 + 0x5e*slot`, only for slots below 10),
   sets the slot flag `0x482751 + 0x5e*slot` to 1 and rewrites
   `sav\gamefile.str` through `0x40e503` (see the GAMEFILE paragraph at the
   end of this section).
3. The read/write stream (ctor `0x44e9d8`, open `0x44eafc`) opens
   `sav\` + the slot's file name (`0x40e708`) with `_open(path, 0x8102,
   0x180)`. With the CW3220 `_open` flags
   ([file_io.md](../reverse-engineering/file_io.md)), 0x8102 is binary,
   read/write, `OPEN_ALWAYS`, no append and no truncation. The file pointer
   therefore starts at 0 and an existing file keeps its length.
4. A failed open sets stream status 1 (`0x44eb2a`); the caller only reports
   it through `0x44e560` with `src\GAME.CPP` line 0xc2 (`0x40e9a4..0x40e9ca`)
   and continues.
5. The sections of the framing table are written in table order:
   `0x4127f3` (inventory, via `0x412488` and `0x412123`), `0x413095`
   (selection block; afterwards it copies the three launch companions into
   their own files with separate read and truncating write streams through
   `0x40da8d`), `0x41023a` (roster via `0x410383`), `0x410791` (team and
   player), `0x410c7c` (hangar via `0x411862`), `0x411f78` (nine-word bank),
   then directly in `0x40e933`: 4 bytes tally, 2,000 bytes variable bank,
   2 bytes `0x4826f2`, 20 bytes `0x4833ac` (`0x40ea1f..0x40ea81`).
6. `0x44eabc` closes the descriptor with `_close`; `0x44ea10` destroys the
   stream.

Every write goes through vtable slot +0x14 of the stream (vtable `0x47a708`,
method `0x44ec1c`), which is one `_write(fd, buffer, n)` at the current
position. A call walk over the whole writer tree (the ten functions named in
step 5) finds no direct call besides those listed (`_sprintf`, `_strlen`
for the name length word, the companion copy) and no indirect call other
than slot +0x14 on the stream passed in: no tell (+0x04, `0x44eb48`), seek
(+0x08, `0x44eb64`), file-length query (`0x44eba8`) or reopen. The image is
therefore written contiguously from offset 0, and its length is exactly the
framed length of the state, which `es2_sav_parse` reports as `reader_end`.
Nothing truncates afterwards: VSHELL imports neither `_chsize` nor
`SetEndOfFile`, and inside CW3220 the only call of `SetEndOfFile`
(`0x409c20`) is in `_chsize` (`0x409ba0`, reached only through its export
thunks), not in `_open`, `_write` or `_close`.

Consequences:

- A save whose image is shorter than the file already at that path leaves the
  old bytes from the image length on. The file afterwards is
  `image || old[len(image)..]`. A longer or equal image leaves no tail.
- The tail is never read back: the loader `0x40eaaa` reads the framing
  sequentially (stream slot +0x18 only, last read the 20-byte block at
  `0x40ebe5`), and the slot scanner `0x4309f0` reads and skips forward with
  tell/seek relative to the current position. Neither asks for the file
  length, so a stale tail does not change any loaded state.
- Stream errors latch: after a failed open or a failed `_write` (status set by
  `0x44ebf8`), every later write of the save is skipped (`0x44ec27`). Such a
  save leaves a partial new prefix over the old content; that case is not a
  plain image plus tail.

GAME_R (4,763 bytes) is reader-defined up to 4,753 and its last ten bytes are
zero. The writer image of its state is exactly those 4,753 bytes (in vitro
below: the original writers reproduce them without the ten bytes). So the
ten bytes were not written by the save that produced the current state; they
are older content of the file (or an external edit). **Hypothesis
(consistent, history not recorded):** an earlier GAME_R save ten bytes
longer, for example with one more 10-byte inventory item record. Its last ten
bytes would be the end of its 20-byte block from `0x4833ac`, which is zero
in all four snapshot saves.

**Model in `formats/sav.h`.** `reader_end` is the writer image length and
`trailing` the stale tail. `es2_sav_write` keeps the parsed tail after the
(possibly resized) image, so an unedited save round-trips byte for byte.
`es2_sav_write_over(save, edit, previous, ...)` models the original's file
update: the edited image followed by `previous[image length..]`; with an
empty `previous` it is the bare writer image, with the input itself and no
edit it is the input. `es2_sav_overlay` does the same for an image built by
`es2_sav_compose`. The synthetic test covers shorter, longer and equal images
over shorter, longer and non-SAV previous files, the parsed-tail case and the
size/capacity/argument/overlap contract. The real test (`sav_real_test`)
reads the four saves from a snapshot copy kept in `es2-data`
(`ES2_SAV_DIR` overrides it): for each, writing the state over the file
reproduces the file and writing it with no previous file gives the first
`reader_end` bytes; GAME_T's state written over GAME_0's file gives the
GAME_T image plus GAME_0's last 431 bytes, which parses with `reader_end`
4,322 and `trailing` 431 and rewrites unchanged.

#### SAV field values

The record writers called by the
sequence above emit these runtime fields. Offsets are serialized offsets inside
each record; "runtime" names the object field written there.

| Record | Serialized content (writer, evidence) |
| --- | --- |
| Inventory category | Flag byte = category +0x16, i16 count = +0x17, then the linked list from +0x19 in list order (`0x412488`). Every item record of category *i* carries ID *i*: items enter the list of their own ID (`0x412521`, which prepends). |
| Item (10 bytes) | Five words of the item object (`0x412123`): +0 ID, +2 the ID's position in the executable's thirty-entry list at `0x46f9a8` or -1 (`0x411fd8`), +4 condition (moved into the slot condition on mounting, see below), +6 second condition word, +8 secondary word (the MEC secondary). A fresh item object is -1,-1,100,100,5 (`0x411ffc`). |
| Five-entry bank | Word `0x46fa14`, then for i=0..4 the word i and `0x46fa16[i]` (`0x41281d..0x41286f`); new careers set 5 and five zeros (`0x412761`). |
| Selection | Words 0/1 are CAREER group/position (`0x46fc58/5a`); the remaining 148 bytes are the briefing selection globals listed above. |
| Pilot | Prefix words +0/+2 (identity, variant), name, suffix `+0x22` bay i16, `+0x24` active byte, `+0x25` rank, `+0x27` order i16, `+0x29` companion, `+0x2b..+0x39` seven words (`0x410383`). Roster reset `0x41033b` sets bay -1, active 0, order -1, +0x2b 100, the other six 0. |
| Roster selection | Three selected indices (`0x483c2c`) then three next indices (`0x483c32`), one per group of twelve. |
| Team | Capacity and active count words, then the player pilot record (`0x410791`). |
| Hangar | Word +0x20 of the hangar (record count), then for bays 0..7 that are present: bay index and the record (`0x410c7c`). |
| Hangar record (78 + 12n bytes) | +0 vehicle ID, +2 vehicle table position (`0x411354`), 66 bytes of 33 condition words (object +8), +70 object +0x4a, +72 object +0x78, +74 capacity (+0x4c), +76 occupied count (+0x4e), then for slots 0..capacity-1 that hold an item: slot word and item record (`0x411862`). Slots therefore appear in increasing order. |
| Nine-word bank | Word +14 of each of the nine HERC_INF runtime records (`0x411f78`, `0x483c46 + 16*i`). |

Hangar record +70 and +72: `0x41163d` starts a Herc under construction with
+0x4a = 0 and +0x78 = HERC_INF +12 (a countdown); `0x4116aa` decrements the
countdown and sets +0x4a = (total - remaining) * 100 / total. A ready Herc has
100/0; HERCS.DAT bay 4 holds vehicle 8 with 0/3. **Hypothesis:** the labels
"build progress percent" and "missions left".

Nine-word bank: `0x411ee9` sets the word of a vehicle not yet marked to one
when its variable holds the expected value (vehicle 1: variable 60, vehicle 7:
61, vehicle 6: 62, each 2; vehicle 8: variable 63 = 1), then clears that
variable. With the hangar UI reads at `0x433e95`, `0x446ece` and
`0x4471a8` this marks which vehicles are unlocked; that label is a hypothesis.

The private VSHELL worker can call the original
writer sequence after restoring a SAV with the original readers
(`es2shellprep --emit-save`, [shell_invitro.md](../reference-game/shell_invitro.md#sav-emission-and-loadout-sweep)).
GAME_0, GAME_1 and GAME_T are reproduced byte for byte; GAME_R is reproduced
without its ten opaque trailing bytes. Reading and writing are therefore exact
inverses on these files, and the trailer is not part of the saved state.

#### SAV editing and building

`es2_sav_write` keeps every section of a parsed SAV unless the edit replaces
it: variables, CAREER position, the 33 availability flags (item lists kept),
all 36 roster records, roster selection, team words, player record, the whole
hangar list and the nine-word bank. `es2_sav_compose` serializes a complete
image without an input file. Replacement records must lie in the loader-safe
domain that every SAV written by the original satisfies: names 1..30 bytes
with one final NUL (runtime name field +4..+0x21), at most eight hangar records
with unique indices 0..7 (eight bay pointers), capacity at most 10 (ten slot
pointers at +0x50), occupied count not above capacity, unique slots below
capacity, item IDs 0..32 (category domain) and, for composed inventories,
item ID equal to its category. Validation precedes the first output byte;
errors leave output and the required size untouched. Synthetic tests cover each
replacement, all malformed cases and size/overlap refusals; the real test
rebuilds the four timing-r111 saves through a zero edit, an edit re-supplying
every parsed record, and a complete image, each byte for byte.

The CD-default career SAV built by
`es2_shell_default_save` equals the SAV the original writers produce right
after the original new-career loaders (`0x4125e8`, seed-zero roster `0x410055`,
player `0x41072b`, HERCS hangar `0x410b11`) for the same player name, rank and
CAREER position: 192 of 192 combinations of ENG/FRE/GER, four names, ranks
0/1/3/65535 and four positions. The rules it uses are in
[vshell_preparation.md](../reverse-engineering/vshell_preparation.md#loadout-sav-contract).

Save-name table `SAV/GAMEFILE.STR` maps indices 0..9 to GAME_0..GAME_9.SAV,10 to GAME_R.SAV,11 to GAME_T.SAV. The names are decoded from the table bytes, and shell filename resolver `0x40e708` uses index-selected names. Both save/load functions redirect requested index 10 to 11 if mode 0 (`0x40e962..0x40e974`,`0x40eabe..0x40ead0`); assigning campaign/training labels to the mode values is a hypothesis beyond the observed branches.

GAMEFILE has a 4-byte size field (314 in the example), then two groups of 12 strings: filename group at 4, display-name/availability group at 174. Each entry is i16 L, L bytes including NUL, one tag byte; the second group's tag is read as a slot flag (`0x40e4b3..0x40e4c9`). The reader `0x40e380` reads the size field into a local and never uses it.

**The four bytes after the groups are a stale tail, and
the size field includes it.** The only writer, `0x40e503`, opens
`sav\gamefile.str` with the same non-truncating read/write stream (`0x8102`)
and writes from offset 0: a u32 placeholder 0, i16 12, twelve entries
(i16 `strlen+1`, the file name from `0x4826f4 + 0x5e*i`, tag byte 0), i16 12,
twelve entries (i16 `strlen+1`, the display name from `0x482701 + 0x5e*i`,
the slot flag `0x482751 + 0x5e*i`). It then seeks to 0 (`0x44eb64`), takes
the **file length** from `0x44eba8` (`_lseek(fd, 0, SEEK_END)`, position
restored), subtracts 4, writes that as the size field
(`0x40e66d..0x40e69f`) and closes. Writer and reader grammars are the same,
so the last write produced exactly the 314 reader-defined bytes; bytes
314..317 (`00 01 00 01`) predate it. Because the size comes from the file
length, not from the bytes written, it counts the stale tail: 314 = 318 - 4.
The CD copy (278 bytes) has size field 274 = 278 - 4
and no tail. Which earlier file left the four bytes is not reconstructed
(no older copy of this file was kept). Contrast: the MISSION.STR writer backpatches
the **current position** minus 4 (`0x418130..0x41815d`, tell `0x44eb48`),
so its size field counts only the bytes written. The STR grammar is implemented in
[`formats/src/str.c`](str.md); an independent check confirms it.

**In vitro: the tail is reconstructed.** The reader `0x40e380`
appends ESTEXT entry 0x21 ("LEER" in German) to every text whose fifth byte
is zero, so the CD table is held as " 1. LEER" … "10. LEER" and its first
save writes 318 bytes. VSHELL's own save run on the CD table (German, slot
mode of the memory worker): slots 10 and 11 (either order; 318 bytes each,
both flags set), then " 1. S1" into slot 0 (316) and " 2. Z1" into slot 1
(314). The 316- and 318-byte images each leave their last two bytes (the
TRAINING NUL and its flag 1) after the shorter ones, and the result equals
the 318-byte example above byte for byte. Details:
[vshell_ui.md §9.1](../reverse-engineering/vshell_ui.md#91-slot-table).

### Campaign order, debrief and exit-state policy

#### Data selects ordered mission names

VSHELL `0x412f2a` loads `missions.bin` (literal 0x46fd1d, call 0x412f50) and `gam\career.dat` (literal 0x46fd2a, open 0x412f64). CAREER has six groups, each an ID word, signed count, and signed two-byte string indices. `0x412e88` reads each group, and `0x412f09` resolves each index via 0x4087f0 into the MISSIONS string catalogue. Runtime group records are eight bytes (ID,count,pointer), not the disk stride. Global 0x46fc50 is group count; 0x46fc54 points to groups; 0x46fc58 is current group index; 0x46fc5a is current index within that group. These last two words are the first four bytes of the 152-byte mission-selection SAV block.

An independent audit consumes the 148-byte `SHELL0.VOL/GAM\CAREER.DAT` exactly; group counts 11,10,10,10,10,10 total 61; all 61 catalogue indices are unique and within 0..60. All three language MISSIONS.BIN copies consume 995 bytes each and are identical; no catalogue prose or full mission-name list is reproduced.

Initialization 0x413053 sets mode 0 to group 0 and a chosen subindex; mode 1 to group 1/subindex 0. Campaign/training labels are still **hypotheses** if based on these mode numbers alone.

#### Results, variables and debrief selection

Results Boolean is stored at 0x482bcd and copied into persistent variable-bank word 0 (0x40f13f..0x40f149). Caller0x40f52c..0x40f539 passes the Boolean to campaign advancement0x4133eb. Before advancing, this routine gets the just-completed mission name from current group/subindex and calls 0x41d8e7 at 0x413427.

0x41d8e7 calls alternate MSN reader 0x41d072 (0x41d8f2). That reader reevaluates original MSN condition records using the current persistent variable bank0x482bdc; type 0 conditions at 0x41d21b..0x41d431 compare the signed bank value indexed by record+6 with the signed literal at record+10. Operators281..286 dispatch through table 0x41d235 to 0x41d24d,0x41d29f,0x41d2f1,0x41d343,0x41d395,0x41d3e4, implementing `==`, `!=`, signed `<`, `<=`, `>`, `>=`. This parallels the preparation condition mechanism; no separate arbitrary instruction stream is needed to explain it.

The alternate reader consumes **MSN list 5, the 64-byte debrief family** that main reader 0x41818b skips. It is not an extra final family. Count read 0x41d72e; record read 0x41d7ac; record+0 is condition, record+2..60 are 30 string references, record+62 resolves through the 8-byte auxiliary selector family. The MSN section above provides the full list and remapping evidence. 0x41d8e7 copies first selected 64-byte record from 0x470784 and calls 0x4139aa (0x41d928..0x41d94c). 0x4139aa copies up to 30 non--1 string references into 0x484160/count 0x48415e and final word to 0x48419e. 0x4139f6 opens `data\mission.str` (literal 0x46fe79), looks up those string references and combines the selected text. Thus returned variables and success affect debrief selection through data conditions, while the basic next-mission ordering is separate.

#### Hardcoded progression around the data order

0x41342d increments current subindex unconditionally. At the end of its group's count,0x413452 resets subindex 0 and 0x41345b increments group. The input success Boolean then controls these return rules:

| Predicate after advancing | Return from 0x4133eb | Caller effect |
|---|---:|---|
|Success and new group>=group count|1|0x40f554 sets 0x4826f2=1 and saves index 10; subsequent end-flow UI follows.|
|Failure and (new subindex=0 or new group>4)|0|0x40f603 sets 0x4826f2=0 and calls 0x44d0e7(0).|
|Otherwise|2|Builds debrief, then 0x40f62d..0x40f647 sets UI state 4 and calls 0x413305 to prepare next mission state.|

Labeling returns 1/0 as final campaign win/loss is a **hypothesis** pending UI/oracle confirmation. The code does not choose an arbitrary next mission ID here; it advances along the CAREER order, with hardcoded terminal rules.

Two specific progression hooks are hardcoded: newgroup 1/subindex 3 calls 0x40ec80(4), which sets word 0x482b82=4; newgroup 1/subindex 6 calls 0x40ed85(8), which passes vehicle/loadout ID8 to 0x4111e2 and clears an associated pilot assignment if found. High-level promotion/reward interpretations remain **hypotheses**. A reimplementation must provide these code rules in addition to reading CAREER/MSN condition tables.

The [removal hook](../reverse-engineering/vshell_preparation.md#removal-hook-0x40ed85) contract
also confirms the removal hook's condition-dependent return of equipment to
inventory and its no-match return-value quirk. These static findings are not
yet a portable campaign-advancement implementation or runtime parity result.

#### DBSIM exit states are distinct from objective success

Following result output at 0x462029, exit selection0x46202f..0x462080 follows this priority:

1. Word[0x4d2530]!=0 =>6.
2. Otherwise word[0x4d24fe]!=0 =>0.
3. Otherwise active-player byte+0x99!=0 and word[0x4a9e52]!=0 =>4.
4. Otherwise =>3.

The flags' complete semantics are unresolved; do not equate exit 3 with failure or exit 4 with success. Success already resides in results.dat Boolean. The word 0x4a9e52 also corresponds to script header+4, explicitly zeroed during normal initialization0x4246bb; other mutations require further tracing.

The uppercase-D command-line dispatch at 0x45e796/0x45e79c maps through index 20 to 0x45e944. That path selects a `tap` file, sets word 0x4d2530=1 at 0x45e955, opens the file for reading, and clears the flag if selection/open fails at 0x45e9a6. Combined with input paths setting0x4d2532 when that flag is set, demo/tape playback is a strongly supported **hypothesis** for exit 6. VSHELL startup state 6 reaches 0x4018c6 and bypasses restore/results readback; states 3/4 both take0x4018a1 and perform restore/readback. ES dispatch routes3,4,6 to VSHELL in all cases.


## Runtime mission logic and operation selectors

### Trigger table and actions

Each 74-byte serialized trigger becomes a 58-byte runtime record. Offsets in the following table are decimal.

| Serialized offset | Width | Runtime destination / use |
|---:|---:|---|
| 0 | 2 | +0 predicate type; dispatch 0..10 |
| 2 | 2 | +2 group activation mode, used when this trigger gates a group |
| 4 | 16 | Up to eight signed region indices, stopping at first negative; mapped to allocated pointers at runtime +6, count +4 |
| 20 | 20 | Ten signed variable IDs at runtime +12 |
| 40 | 20 | Ten signed action IDs at runtime +32 |
| 60 | 10 | Read into a temporary; no retained runtime field identified here |
| 70 | 2 | Runtime +52 = serialized value minus one; message/event selector |
| 72 | 2 | Runtime +54 initially sign-extended ID, later resolved by trigger type |

Runtime latch at +10 is initialized to zero. The resolver `0x426470..0x4264f3` maps types 7/8/9/10 to subject scopes 1/2/3/0 through `0x425348`, and clears the subject pointer for types 0..6. Scope 0 selects a group in the 122-byte group array; 1/2/3 use separate object pools through remapping byte arrays. **Hypothesis:** those three pools represent mechs, flyers and bases respectively; construction paths support these labels but the report keeps raw scope IDs where essential.

`0x426b70` iterates the table at pointer `0x4a9e28`, count word `0x4a9e24`; switch at `0x426b94`, jump table `0x426b9b`, eleven four-byte targets:

| Type | Target | Predicate tested |
|---:|---|---|
| 0 | `0x426bc7` | Player position (`player+0x26`) against trigger regions via `0x4234b8` |
| 1 | `0x426bdf` | Player's group (`player+0x45`) via `0x423508` |
| 2 | `0x426bf7` | Any actor in groups with group byte +18 == 0 and dword +20 == 0 |
| 3 | `0x426c64` | Same, group byte +18 == 1 |
| 4 | `0x426cd2` | Same, group word +0 == 0 |
| 5 | `0x426d48` | Same, group word +0 == 1 |
| 6 | `0x426dbe` | Same, group word +0 == 2 |
| 7 | `0x426e2d` | Resolved scope-1 actor position |
| 8 | `0x426e44` | Resolved scope-2 actor position |
| 9 | `0x426e5b` | Resolved scope-3 actor position |
| 10 | `0x426e72` | Resolved scope-0 group |

`0x4234b8` returns true immediately for an already latched trigger. Otherwise it visits the trigger's regions and fires the trigger on the first true spatial predicate. `0x423508` applies this to each actor in a group. `0x426e89` also calls the action handler when a predicate returns nonzero; the latch prevents duplicate variable actions.

`0x423358` converts a serialized 6-byte region: signed word type, signed point index, signed second index/scalar. Type 0 resolves the last word as a second point and `0x4233a4` tests strict XY interior between coordinate minima and maxima (edges excluded). Other types use the last signed word times ten as threshold and compare the result of `0x4928d0` strictly below it. This metric is `max(abs(dx),abs(dy)) + floor(min(abs(dx),abs(dy))/2)` for ordinary representable coordinate differences: `0x4928d0 -> 0x492a45 -> 0x47de4c` (`distance*.asm`). It is an octagonal distance approximation, so substituting a Euclidean circle changes boundary behavior. Extreme overflow cases retain original 32-bit arithmetic quirks and are not normalized here.

If latch +10 is nonzero, return; otherwise set it to one. Iterate ten slots; for each variable ID >= 0, action 5 clears the corresponding signed-word cell at `0x4a9e70 + 2*ID`, action 6 increments it; other codes do not mutate the cell on this path. If message/event selector +52 >= 0, create and dispatch the event in this same per-valid-variable loop. Thus a faithful implementation must not silently move message emission outside the loop. Whether multiple valid slots intentionally produce multiple messages needs an oracle case.

### Timers and update order

**Own implementation, validated live** (distance hooks bit-identical in the oracle regression, [hooks.md](../reference-game/hooks.md)): portable
`core/geometry` and `core/timing` plus opt-in proxy hooks implement the
spatial/waypoint XY metric, the player's **3-D** gate metric (`0x49288c`), and
the word/dword countdown and arm helpers. Static rereading confirms countdown
subtraction wraps before the sign test, including overflow cases. Neither
temporary initializer `0x49545b`/`0x4953c4` writes memory or globals. See
[exact arithmetic and hook contracts](../reference-game/hooks.md#distance-and-timing).

A 24-byte serialized timer contains a signed gate-trigger index (+0), signed duration (+2), and ten signed target-trigger indices (+4..+22). Negative references become null. Runtime stride is 49: gate pointer +0, ten target pointers +4..+43, five-byte timer object +44. Constructor `0x423104` calls `0x467ac4`, storing signed duration << 11 at timer+1.

`0x4230a4` runs when gate is null or gate latch +10 is set. It decrements using `0x467aa8`, which subtracts the signed word at `0x4d3b64` from the signed dword timer+1 and clamps negative results to zero. At zero it fires each non-null target with `0x423430`, then re-arms with parameter 30000. This is a large re-arm interval, not deletion. `0x426b48` visits every timer.

The principal update handles pending group activation / existing group orders at `0x45f7ec` / `0x45f7f5`, timers at `0x45f860`, spatial triggers at `0x45f86c`, then periodic outcome evaluation later at `0x45f992` via `0x4131ac`. Timer/trigger calls are conditional on the main update flag being zero. A spatial trigger newly latched in this sequence gates its timer on a later iteration; a timer firing a trigger occurs before the spatial scan but after the group activation pass.

Initialization `0x467840` derives timing reference 125 from 1000 divided by 8. Limiter `0x4678c0` obtains elapsed milliseconds from GetTickCount; `0x467894` sign-extends its low 16 bits, multiplies by 256 and signed-divides by the reference. It compares the quotient's low signed word against [64,450] and clamps it. Thus `duration<<11` corresponds nominally to duration seconds before per-update integer truncation and scale clamping. Example derived arithmetically, not a new oracle experiment: at a constant 47 ms interval and reference 125 the scale is 96; a duration 60 countdown starts 122880 and expires after 1280 decrements, equivalent to 60.16seconds of those intervals. Large elapsed intervals and initialization must preserve the exact original signed-word behavior; simply scheduling a wall-clock deadline would differ.

### Spawns, deferred activation and group orders

The loader constructs selected actors in pools before mission simulation. For the 134-byte actor class table, allocation is at `0x42567f` and constructor `0x415bb0` at `0x4256a4`; positional references, orientation and event references are installed afterward. Other actor tables have separate construction paths. Selected-index remapping is present before group/objective references resolve; archived IDs cannot be assumed to be runtime pool indices.

The 156-byte group record contains class selector +40, secondary word +42, point index +44, angle index +46, twenty actor references +50..+88, ten order references +90..+108, category byte taken from word +110 (allegiance meaning remains a hypothesis), activation-trigger index +112, ten variable IDs +114..+132, ten action codes +134..+152. Constructor `0x423b34` places the resolved activation-trigger pointer at runtime group+20; null means active. Group class selector 0/1/2 selects the three object pools. Runtime group completion bytes are explicitly zeroed at `0x423b8d..0x423b92`.

`0x4236c4` checks the pending trigger's latch +10; nothing occurs until it is set. It then selects hardcoded placement behavior from trigger word +2 (serialized trigger+2):

| Mode | Compiled branch |
|---:|---|
| 2 | `0x423717`: random stream-A word, mask 0x7fff, angle0x4000-random; placement helper `0x42354c` at distance 150000, mode 0; allocate pool `0x4a96aa` object and call `0x409b44` |
| 3 | `0x423731`: mask 0x1fff, angle0x1000-random; same placement/allocation path |
| 4 | `0x4237c3`: mask 0x1fff, angle0x1000-random+0x8000; placement distance90000, mode 1 |
| 5 | `0x4237e5`: mask0x3fff, angle0x2000-random; placement distance 150000, mode 1 |
| Other | `0x42388b`: clear the pending pointer directly |

Modes2/3 set group byte+19 before allocation and return with the activation pointer still non-null; subsequent calls do not repeat allocation. The arrival object's `0x409d2c` update eventually positions the group's first actor and clears group+20 at `0x409da3`, after an animation-state condition. Its constructor `0x409b44` consumes resource pointer `0x4a96b4`, loaded from original resource name `meteor` (string `0x49829c`, loader `0x409ad3..0x409aed`; `meteor-resource.asm`). Modes4/5 reposition the existing first actor, orient it, use virtual method+0x78 to position subsequent actors, then clear group+20 immediately at `0x42388d`. Pending groups are skipped by actor update and spatial trigger scans. **Hypothesis:** the exact user-visible mode names and differences should be checked against an oracle; the meteor resource gives a stronger anchor than a guessed dropship label. These are deferred activation paths, not evidence that all actors are allocated only at spawn time.

Trigger activation mode counts across all 62 MSN members: -1:1, 0:66, 1:172, 2:63, 3:36; implemented modes4/5 do not occur in this corpus.

`0x42354c` computes placement relative to the player's position and orientation. It increases a candidate distance by 2000 on retries and rejects candidates via actor-distance, `0x404ae4`, and terrain helper `0x46ff90` checks. The exact gameplay interpretation of all rejection tests remains **hypothesis**.

Each 14-byte serialized order becomes22runtime bytes: +0 type ->runtime+0; +2 word ->+2; +4 pointindex ->pointer+4; +6 routeindex ->pointer+8; +8 scope ->+12; +10 subjectindex resolved with scope ->pointer+14; +12 triggerindex ->pointer+18. Groups refer to up to ten such orders. This is another typed state table rather than a free-form instruction stream.

### Objectives and success/failure

Each 54-byte objective becomes 76 bytes at pointer `0x4a9e40`, count word `0x4a9e3c`.

| Serialized offset | Width | Runtime destination / use |
|---:|---:|---|
| 0 | 2 | +0 required marker; exactly 1 means required |
| 2 | 2 | +2 predicate type 0..10 |
| 4 | 2 | +4 subject scope 0..3 |
| 6 | 2 | +6 resolved subject pointer via `0x425348` |
| 8 | 2 | +10 optional point pointer |
| 10 | 2 | +14 optional route pointer |
| 12 | 2 | Three successive mission-string pointers at +18,+22,+26; fourth pointer +30 is an empty string |
| 14 | 20 | +36 ten signed variable IDs |
| 34 | 20 | +56 ten signed action IDs |

`0x413280` evaluates objectives with separate predicate switches for groups (scope 0, dispatch `0x4132cf`, table `0x4132d6`) and actors (scopes 1..3, dispatch `0x4133d7`, table `0x4133de`). The stable semantic subset is:

| Type | Group subject | Actor subject |
|---:|---|---|
| 0 | `0x41324c(group, route)` reads the completion byte corresponding to that route | Same helper on actor's group |
| 1 | `0x413920` compares a group status helper with threshold 3 or 4 | Actor flag +0x99 or +0xa4 nonzero |
| 2 | `0x413950`: group status acceptable and all applicable actors pass `0x4137b4` | `0x4137b4(actor)` |
| 3,4 | Player flag +0xa0 | Player flag +0xa0 |
| 5 | Common branch without assigning a fresh result; do not assign a mnemonic | Same caution |
| 6 | `0x412d90`: any group actor flag +0x9e | Actor flag +0x9e |
| 7 | `0x412c58`: every actor flag +0xa5 | Actor flag +0xa5 |
| 8 | Inverse of type 6 | Inverse of type 6 |
| 9,10 | Inverse player flag +0xa0 | Inverse player flag +0xa0 |


The objective switch targets below were
also independently decoded as data, avoiding objdump's interpretation of embedded
tables as instructions. Operand fields and predicate behavior are in the preceding
tables; these are type selectors, not variable-length bytecode instructions.

| Type | Group target (`0x4132d6`) | Actor target (`0x4133de`) |
| ---: | --- | --- |
| 0 | `0x00413302` | `0x0041340a` |
| 1 | `0x00413317` | `0x00413424` |
| 2 | `0x00413325` | `0x0041344d` |
| 3 | `0x00413334` | `0x0041345c` |
| 4 | `0x0041334a` | `0x0041346f` |
| 5 | `0x004134e2` | `0x004134e2` |
| 6 | `0x00413360` | `0x00413482` |
| 7 | `0x0041336f` | `0x00413493` |
| 8 | `0x0041337e` | `0x004134a4` |
| 9 | `0x00413395` | `0x004134b8` |
| 10 | `0x004133ae` | `0x004134ce` |

**Hypothesis:** type 1 is destruction/incapacitation; type 2 includes a safety/combat-proximity condition. Rather than conflate these with specific game terms, retain exact flag/helper evidence until the state fields are established.

For marker == 1 all required predicates must hold; the first false objective supplies the address of its mission-string pointer array (runtime +18, three strings plus an empty terminal entry) to global `0x4d1e98` via `LEA` at `0x4134f7`. For any marker other than 1, a true predicate sets the failure aggregate. On first true evaluation for each objective (runtime latch +34), ten variable actions run: code 4 = set 1; 5 = clear; 6 = increment; 7 = decrement at the same 1000-word mission variable bank. Slots require nonnegative variable and action IDs. The latch starts zero because allocation at `0x42629d` calls `0x4774f0`: its pool branch clears memory at `0x47752c`; its fallback `0x47747c` calls imported `_calloc` at `0x477485` through thunk `0x49686e`, IAT `0x6cc390` in `cw3220.DLL` (`find_import_callers.py`; `allocation*.asm`).

`0x411bc8` processes actor variable/action arrays at +0x1ba/+0x1ce; code 1 clears, code 2 increments, codes 13..16 store 1..4. Before the actor actions it invokes `0x423f30(group,actor)`, which applies the same code family to group arrays +28/+48 only when every other group actor has flag+0x99 or+0xa4 set. Call sites include `0x404eda`, `0x418276`, `0x418526`, and `0x421c41`; associated trigger pointer actor+0x1b6 is fired at those sites. Separate pointer actor+0x1b2 fires from proximity/interaction paths such as `0x4127c2`, `0x412aa2` and `0x42673a`. **Hypothesis:** the former event family is destruction/incapacitation and the latter detection/engagement. Their action code numbering differs from objective and trigger actions and must not be unified blindly.

After testing `0x4137b4(player)`, return statuses are: checked in this priority order at `0x4135a8..0x4135dd`: any failure => 6 if helper true, 10 otherwise; else all required complete => 9 if helper true, 10 otherwise; else required incomplete => 5 if helper true, 4 otherwise. The result serializer `0x42412c` computes its outcome word as `(0x413280(player) == 9)` (independently checked against the handoff writer). The exit caller at `0x462029` passes player pointer `[0x4d24bc+0x2a]`. Thus status 9 is the success result accepted by the save handoff. This is a fresh objective evaluation, not a copy of stored status `0x4a9e4c`; first-true objective variable actions may run during this evaluation. This cross-check agrees with the handoff report and does not prove that the prepared result files and current script are from one uninterrupted run.

`0x4135e8` first checks player flags +0x99 and +0xa4 (statuses 2 and 3); ordinary periodic mode also tests map bounds (statuses 8,7), then calls objectives. Forced evaluation skips the timers and boundary warnings. `0x413180(player,force)` stores the forced result in word `0x4a9e4c`. The periodic wrapper `0x4131ac` implements additional delayed outcome policy; it must not be replaced by merely calling the raw objective test each frame.

### Waypoint completion

Groups contain ten order pointers at +0x44, current order index +0x6c, and ten completion bytes +0x70. `0x41324c` finds an order whose route pointer at order+8 matches the objective's route and returns the matching completion byte. `0x423a74` updates the current order: a true `0x4239fc` predicate sets the matching completion byte at `0x423aa2`, then may advance to the next non-null order and clears each actor's state countdown dword +0x52 (within state object +0x4d).

`0x4239fc` dispatches on order type; types 3,5,6 consider the route complete when `0x423b0c(group+4,currentPoint+1)` returns null. Type 0 uses `0x412e58`, type 2 checks an order pointer and uses `0x412e58/0x412e74`, other types return false. `0x423b0c` returns a route point pointer only when a route exists and the signed index is below its signed count; the original does not also check a negative index here. Group construction initializes current point index to zero and its route pointer from the first order at `0x423bde..0x423be7`.

Player callback `0x41c194` first requires that its actor be the first member of the group (`0x41c1a0..0x41c1a8`). It fetches point `current+1`, then tests the actor's position +0x26 against that point using `0x4928d0`. The threshold at `0x41c1e0` is strictly less than 10,000 internal XY distance units. The metric is the same integer approximation documented above: `max(abs(dx),abs(dy)) + floor(min(abs(dx),abs(dy))/2)`; Z is ignored. If the next point is not a loop-closing point, it emits event 0x1d through `0x420a68` at `0x41c1fb` and advances at `0x41c208 -> 0x42313c`. Hence a player's required route progresses in order, through proximity to the next point, rather than an unordered set of visited points.

`0x423170` recognizes a closing point when next index equals count-1 and the next point pointer equals the first point pointer. The player callback suppresses advance in this case. The general advance helper `0x42313c` increments the word index and wraps it to zero when count>1, the new index is count-1, and the same pointer equality holds. This is pointer/point-ID equality, not coordinate equality. The separate AI routine `0x41fb60` calls the advance helper after `0x41fac4(actor,nextPoint)` succeeds (`route-ai.asm`); its complete movement policy remains outside this bounded trace.

Constructor `0x415c60` installs actor vtable `0x49a282`, whose +0x14/+0x18/+0x1c entries are `0x415afc`, `0x415b38`, `0x415b74`. Group update invokes `0x411cec` per actor. That routine invokes +0x1c when state timer +0x52 is zero, invokes +0x14, and normally invokes +0x18 (suppressed once by actor flag +0xaf). Wrapper `0x415b38` calls the current state's method pointer at +0x18. Initializer `0x413ed4` installs callback `0x41c194` into each of states `0x4993e2` and `0x499420`, from original pointer constants `0x49991c` and `0x499940`. Selector `0x41eb34` chooses those states when actor flag +0xa3 is set, choosing between them according to actor template word +0x50. Reader `0x4256d5..0x4256e7` sets that flag on the selected player actor and records it in global `0x4a9b84`. The constructor also contains the same state selection at `0x415f11..0x415f59`.

For the ordinary non-player group-order branch of `0x41eb34`, `0x41ecf6..0x41ed1b` reads current order type and jumps via seven-entry table `0x41ed22`. The selected compiled state addresses are:

| Order type | Branch VA | State selected |
|---:|---|---|
| 0 | 0x41ed68 | 0x49968c |
| 1 | 0x41ed3e | 0x4997c2 |
| 2 | 0x41ed6f | 0x499746 |
| 3 | 0x41ed61 | 0x499594 |
| 4 | 0x41ed76 | 0x4996ca |
| 5 | 0x41ed45 | 0x49964e if signed template word +0x22 > 32000; otherwise 0x4995d2 |
| 6 | 0x41ed7d | 0x499610 |

**Hypothesis / limitation:** these are compiled AI states selected by data. Names such as patrol, escort, attack, or flee for every type would require further tracing; the route-completion behavior of types 3,5,6 is proved independently above. Player wingman command handling is a separate preceding branch and must not be collapsed into this table.

Timing `script.dat` has one required objective at offset 12226: type 0, scope 1, subject ID 3, route ID 1, message-base ID 0. Group 0 at 6296 names actor ID 3 first and order ID 8 first; order 8 at 6210 has type 3 and route ID 1. Route 1 at 1044 contains five point references and its first and last IDs differ. This records only a short relational example, not coordinates or full mission contents. **Hypothesis:** this matches the journal's incomplete-waypoint scenario; static evidence proves the linkage and check, but does not replay the journal state.

### Q: EINSATZALARM and incomplete waypoints

`0x477bec` handles key input and `0x477c32` indexes dword table `0x4a10ec` using the Windows virtual-key code. The VK_Q (0x51) entry is internal command 0x10. Main command handler `0x45feb8` dispatches through table `0x45ff50`; command 0x10 points to `0x4605ea`. That branch calls `0x413180(player,1)`, constructs the general alert at `0x455950` with the returned status, invokes its modal method at `0x460649`, then compares the chosen button against status-indexed table `0x49f5c0` at `0x460650`.

The alert loader reads `gnl_alrt` using resolver `0x45ef1c` and STR reader `0x453878`. It has 20 titles, 40 button strings, and 80 body strings. The generic body selection uses status*16. Status 5 specifically replaces that generic body with the pointer in `0x4d1e98` at `0x455df1..0x455df7`: the first unmet required objective's string-pointer array.

`SIMALERT.VOL` member `STG\GNL_ALRT.STR`, archive payload offset `0x0b5c00`, local offset `0x2a`, contains the title `EINSATZALARM`. `ZONES.VOL` member `MSN\C1_01.GER`, payload offset `0x0e91f5`, local offset `0x35`, contains `Sie haben nicht alle Wegmarkierungen angesteuert.` The prepared timing DATA/MISSION.STR contains the corresponding incomplete-objective text. Thus this observed Q behavior has both hardcoded policy/dispatch and data-selected objective/text; the mission-specific wording is not evidence of a per-mission hardcoded check.

## Conclusion: division of data and compiled behavior

The classifications below are **conclusions supported by the paths
above**. They do not assign semantics to unresolved fields or claim live parity.

| Aspect | Authored / saved data | Compiled behavior that must be provided |
| --- | --- | --- |
| Mission preparation | MSN condition IDs, comparisons, random ranges, conditional records, coordinate expressions | VSHELL ordered evaluation, random draw order, filtering, override/merge and ID remapping (`0x41818b`, `0x41b278`) |
| Terrain/environment | Header selectors, WLD tables/names, ZONE words/bitmap, RMP/DPL tables | Resource selection, grid construction, row reversal, scaling and terrain RNG draws (`0x42e010`, `0x42789c`, `0x46c4c4`) |
| Actors and arrivals | Typed actor records, groups, placement/order references and trigger mode | Constructors/pools and class-specific behavior; delayed activation/arrival state machine (`0x42567f`, `0x4236c4`, `0x409d2c`) |
| Spatial triggers | Region geometry, subject selector, variable/action slots | Fixed predicate switch, exact distance approximation, one-shot latch and action/message ordering (`0x426b70`, `0x423430`) |
| Timers | Gate, duration and ten target indices | Elapsed-scale integer countdown, gate policy, re-arm and update ordering (`0x4230a4`, `0x426b48`) |
| Routes and waypoints | Point lists, orders, objective route references | Current route/order state, waypoint advance/completion and objective lookup (`0x423a74`, `0x41324c`) |
| Objectives | Required marker, typed subject/predicate, text and variable actions | Fixed group/actor predicate tables, failure aggregation, one-shot side effects and extra player-status gate (`0x413280`) |
| Q / mission exit | Objective text, general dialog strings | Key/command dispatch, forced versus periodic outcome policy, dialog/button handling (`0x4605ea`, `0x413180`, `0x4131ac`) |
| Campaign and saves | CAREER/MISSIONS order, persistent variables, conditional debrief text, loadouts and companions | Progression policy, special branches, save restoration and result application (`0x4133eb`, `0x40eaaa`, `0x40f09f`) |

**Traps for own code:** the evaluators mutate latches/variables and may emit
events, so a compare mode must copy all affected state and suppress duplicate
external effects; a pure-looking `0x413280` call is not side-effect-free.
Replacing the processes also needs the ES exit-state/argument protocol, saved
companion restoration, matching team order for uncounted results, and the
original variable reset rules. The simulation needs the original integer
elapsed-time scaling and deterministic initialization/update/RNG ordering; an
unqualified fixed tick would contradict [README.md](../../README.md#architecture)
(timing). Mission behavior is a typed-record language with compiled primitive
semantics, not per-mission special cases or a general-purpose VM. Three
variable-action namespaces (trigger, objective, actor) stay distinct; equal
numbers do not imply equal operations.

**Open limits:** static exact consumption proves framing, not that every field
meaning is correct, every authored reference is reachable, or every mission is
playable. Filtered preparation references have an independently implemented
static contract and two exact snapshot checks; broader oracle coverage and
other campaign/variable/RNG states remain. Open semantics: actor flags, order
types, proximity/interaction events and group actions; remaining WLD numeric
meanings, terrain units/interpolation, RMP lookup use, BND consumers and
save/result field meanings; runtime handling of the truncated DEMO2. Objective
type 5 is unsafe to name from the common-branch fallthrough alone. For C1_01,
completion status 9, exact wording/body selection and the return files are not
observed live. No search result here establishes a universal absence of other
interpreters.

## Independent check

An own parser and byte reads confirm: all 62 MSN members have version 5; the 17-list grammar (strides
14, 82, 8, 144, 64, 22, 10, variable `10+2K`, 12, 82, 30, 144, 102, 62, 22,
164, 58) consumes 61 members exactly and leaves `DEMO2.MSN` exactly 42 bytes
short; companions ENG/FRE/GER 62/61/61. VSHELL comparison table `0x41834f`
holds the six targets listed above. DBSIM key table `0x4a10ec` maps VK_Q to
command `0x10`, command table `0x45ff50` maps `0x10` to `0x4605ea`; trigger
table `0x426b9b` holds the eleven listed targets; `0x41c1e0` is
`cmp eax, 0x2710` (10,000); objective group table `0x4132d6` starts at
`0x413302` and ends at `0x4133ae`.

**Runtime check (C1_01 capture):** in prepared C1_01 the player route advances in order in the first frame with octagonal distance below 10,000 (Euclidean distance below 10,000 one frame earlier did not advance); the completion byte follows; the objective's first-true latch is set at the next periodic evaluation (countdown `0x4a9e63`, every 105 frames at 47 ms). Forced `Q` statuses observed: 5 (start), 4 (incomplete after contact), 10 (complete after contact), i.e. the player gate `0x4137b4` decides between 5/4 and 9/10. Status 9, button handling and return files are not observed.

## Evidence, references and reproduction

**References:** original CD bytes and original-executable disassembly are the
primary evidence. Every grammar here is checked against original
instructions and/or original bytes at the stated confidence. Shared
VOL/DBM/DPL/DAT facts cite this repository's verified documents. The audits
are small explicit little-endian/bounds analyses, not engine code; no
original mission payload, bitmap or decoded text corpus is saved.

**Reproduction:** each audit reads original data and writes only its own
scratch report. No build/test dependency is required.

The disassembler is `llvm-objdump` from `es2-dependencies`; imports were
inspected with a byte scan for their callers. Tool-generated
symbol labels such as `MainWndProc` covering distant unnamed addresses are not
treated as recovered function names. Dispatch-table bytes are decoded as data.
