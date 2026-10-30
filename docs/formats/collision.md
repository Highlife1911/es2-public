# Vehicle collision, damage, attachments and damage displays

Static analysis. Addresses use image base `0x00400000`. Binary identities
are in [reference_version.md](../reference_version.md). Audit inputs:
`VER95/DBSIM.EXE`, `VER95/VSHELL.EXE`, `VOL/SIMVOL0.VOL` and
`VOL/SIMPATCH.VOL` (file hashes: `data/reference_hashes.txt`,
`data/iso_file_hashes.sha256`).

All integers are little endian; `i16`/`i32` denote signed values. Counts
must be nonnegative. These four resources have **no class tag or object
length envelope**. Dedicated stream readers consume their count fields;
the class registry at `0x47a4a0` is not on these load paths. Unknown bytes
remain opaque. [bnd.md](bnd.md) separately records the unresolved BND family.
This split keeps BND's weaker evidence separate from reader-proved grammars.

## Selection and opening

`0x4201a8` selects the 21 vehicle basenames through
`MECHS.NAM`, loading the same-stem DAT, DTS, GL and PDG. The 542-byte
runtime configuration receives the 216-byte DAT at `+2` (`0x420210`), its
name-table ID at `+0x6c`, and its PDG pointer at `+0xda` (`0x420401`).
The actor constructor uses that name-table ID at `0x415f73`, loads DMG at
`0x415f98` into actor `+0x1fa`, then COL at `0x415fb5` into `+0x1f6`.
`0x415fd3` binds them into the damage context at actor `+0x206`:
context `+0xc` = DMG, `+0x10` = COL, `+0x14` = owner (`0x40d354`).
SKIMMER uses the FLYERS name table (`0x422fc9`) and the separate 112-byte
configuration path `0x422ed0`: DMG load `0x422f4f` stores `+0x2e`, COL load
`0x422f69` stores `+0x32`. Its actor `+0x1fa` points to that configuration,
not directly to DMG. `0x42171c` binds these resources into actor `+0x200`.

`0x492bec` calls `0x492b90` to replace the basename's
extension and prepends the extension directory: e.g. `col\outlaw.col`.
The extension pointers are `0x4a0a00` GL, `0x4a0a04` DMG, `0x4a0a08` COL;
PDG uses literal `0x49bc48`. These paths use stream constructor `0x472f14`
and opener `0x4730e8`, not localized resolver `0x45ef1c` or STR reader
`0x453878`. The opener tries the loose path first (`0x473116..0x47312c`),
then archive lookup `0x468064`. Every audited member is raw VOLN kind 2.

**Static precedence:** archive header byte 8 is read into runtime
`+0x1c` at `0x47ccda..0x47cce9`. Insertion `0x467f94`, specifically
`0x467fc8..0x467fd6`, orders that byte descending (unsigned); lookup
`0x468064..0x46808a` returns the first matching archive. SIMPATCH priority
10 therefore precedes SIMVOL0 priority 5 when both are registered. Its
`pdg\spider.pdg` overrides the base copy. This also determines lookup of
`bnd\results.bnd`, but an actual BND request has not been established.
Equal priorities retain insertion order. This does not establish all archive
registration conditions. The library VFS continues to use caller ordering.
Discovery `0x467cd8` builds `vol\\*.<extension>`, enumerates two roots,
filters archive flags and inserts accepted archives through `0x467f94`.
The startup call reaches it through `0x45f2a8 -> 0x473260 -> 0x467b9c`.
Both SPIDER.PDG copies are 740 bytes. Six bytes differ, each in a different
rectangle coordinate of view 2 (zero based): file offsets 664, 684, 688,
696, 716 and 724. All counts and framing agree.

**Search result, not proof of absence:** neither a type-specific
VSHELL loader nor relevant COL/DMG/PDG/GL path literal was found in the
reference VSHELL. Shell DAT damage/loadout tables are different resources
([dat.md](dat.md)). Mission actor definitions select vehicle configurations;
the indices inside these four formats are not mission actor or STR IDs.

## COL: collision spheres grouped by model part and damage region

**Grammar:** `i16 G`, followed by `G` variable groups:

| Offset within record | Width | Meaning |
|---|---:|---|
| Group `+0` | i16 | DTS part ID; negative selects the actor transform |
| Group `+2` | i16 | Shape count K |
| Group `+4` | variable | K shapes |
| Shape `+0` | i16 | DMG region index |
| Shape `+2` | i16 | Sphere count S |
| Shape `+4` | 8*S | Sphere records |
| Sphere `+0,+2,+4` | i16 each | Local x, y, z |
| Sphere `+6` | i16 | Radius |

No padding or suffix. File length is `2 + 4*G + 4*sum(K) + 8*sum(S)`.
Reader chain: `0x40cd88` -> `0x40ccf8` -> `0x40cc50` -> `0x40cc14`
-> `0x40c7c4`. The runtime descriptor is 6 bytes (count `+0`, pointer `+2`);
groups allocate 10 bytes each (part `+0`, count `+4`, pointer `+6`), shapes
16 bytes each (count `+0`, sphere pointer `+2`, region `+6`, derived bounding
sphere `+8..+15`). Sphere arrays allocate 8 bytes per element. Group `+2`
is not serialized. `0x40ccc8`/`0x40c5d0` compute the enclosing sphere;
those eight derived bytes are not an extra file field.
The original narrows `8*S` to a signed 16-bit stream-read size at
`0x40c80f`. The bounded reader rejects S > 4095 rather than reproducing
that overflow. No corpus member approaches this limit.
Zero-sphere shapes are legal and common: 166 records in 20 files. Preserve
their region entries. The bounds helper skips the element loop when S=0
(`0x40c696..0x40c699`); its initial opposite extrema produce a zero center
at `0x40c69f..0x40c6df`. Its radius calculation still uses those extrema
(`0x40c6e3..0x40c70c`), so do not substitute an all-zero runtime descriptor.
Whether a particular empty entry is later populated is a loadout/runtime
question; it is not a parse error.

**Consumers:** `0x40c9d4` traverses groups; `0x40c8fc` resolves
nonnegative part IDs via the DTS part-list virtual `+0x20` (`0x40c946`).
It selects a 32-byte transform record by the returned part's `+4` index,
with fallback `0x6c56a8`. `0x40c990..0x40c99b` checks the region's live
damage state. `0x40c524` transforms sphere centers with `0x4801d4` and
passes radius to the collision test (`0x40c58e` -> `0x40c428`). There is
no unit rescaling here: the world-position convention is one raw unit =
6 game mm ([player_state.md](../reverse-engineering/player_state.md)).
The meaning of model axes is inherited from the DTS transform, not inferred
from extrema. Negative part IDs observed: only -1.

Buildings use the same nested COL reader from externally counted
`BASECOL.DAT` (`0x40658e..0x4065ad`), not a same-stem `.COL` file; see
[dat.md](dat.md#auxiliary-simulator-structures). Do not treat the 22 COL
files as the complete static-obstacle collision corpus.

## DMG: damage limits and weighted internal links

**Grammar:** `i16 C`, `i16 capacities[C]`, `i16 R`, then R regions:

| Region offset | Width | Meaning |
|---|---:|---|
| `+0` | i16 | Outer damage threshold, consumed by `0x40da67..0x40da71` |
| `+2` | i8 | Debris selector; -1 generates debris from the part |
| `+3` | i8 | DTS cell-state slot; negative disables the reference |
| `+4` | i8 | Parent damage region; negative has no parent |
| `+5` | u8 | Destruction/effect flags; preserve unnamed bits |
| `+6` | i16 | Link count L |
| `+8` | 4*L | Pairs: i16 weight, i16 capacity index |

No padding or suffix. Length = `4 + 2*C + 8*R + 4*sum(L)`.
`0x40d160` -> `0x40d09c` -> `0x40cff8`: 12-byte descriptor contains region
pointer `+0`, capacity pointer `+4`, region count `+8`, capacity count `+0xa`.
Capacity storage is 2*C; region storage is 18*R. Each region's first eight
bytes are read directly, links allocate 4*L and their pointer occupies
runtime `+8`. Runtime `+0xc` is initialized to -1 and `+0xe` to null
(`0x40d03b..0x40d046`), later used for COL part/center caching. Neither is
serialized. The six-byte file prefix is not three numeric words.

**Cross-references:** `0x40d744` treats debris selector -1 specially;
otherwise `0x40d946..0x40d97e` sends it through `0x4089bc`. That helper first
selects the default DEF_DEB list (`0x4a9650`, count `0x4a9654`); indices at
or above its count select the actor's DAT-specified debris list via
`0x4a964c`, subtracting the default count. DAT's resource-name slot is
`+0xcc`; the selection path is `0x420456..0x420483`. All 610 selectors fit
the combined lists (or are -1).

`0x40d57e..0x40d58e` writes state 2 to the model-instance word array `+8`
at signed byte `+3`'s index when nonnegative. This is a DTS cell-state slot:
`0x490b8c..0x490bbd` allocates that array from TSShape count `+0x24`, read
from the file's S count at `0x490e8e..0x490e93`; `0x490c25..0x490c2d`
publishes it through `0x490d3a` to the cell renderer. Every nonnegative
selector fits same-stem root 0's S count. `0x40d9a9..0x40d9c8` queues each
still-live region whose signed parent byte `+4` equals the region being
destroyed. All nonnegative parents fit R. Names of individual cells, debris
effects and flag bits remain unresolved. The API retains the original
parameter span as well as these typed references.

`0x40da38` first applies the region threshold through
`0x40d3ec`, then calls weighted selection `0x40cf44` with the capacity table.
`0x40d3ec` accumulates damage while nonnegative; reaching the supplied
limit changes state to -1 and returns excess damage. Link indices refer to
the C capacity entries, not DTS parts. `0x40d434` resolves the damaged
region's COL shape through `0x40ca8c` for destruction/effect placement.
Physical damage units, names of all capacities and individual flag semantics
remain **hypotheses**; the API exposes raw values, not HP or joules.

**Runtime augmentation:** `0x41778f` adds weapon COL records via
`0x40cec0`; `0x4177a8` replaces region definitions through `0x40d240`, and
`0x4177c1` updates capacities through `0x40d128`. `0x4177f8` collects COL
region/part/center associations with `0x40cb84`; `0x41781d` calls `0x40d284`,
which stores the part and center pointer in DMG runtime `+0xc/+0xe`
(`0x40d2ae..0x40d2b2`). A runtime dump therefore need not equal the file's
initial region or sphere lists. All 21 vehicle DMGs have 22 capacities and
29 regions; SKIMMER has one of each.

## GL: attachment records

**Grammar:** `i16 N`, exactly N records of 26 bytes, no suffix.
`0x420370..0x420391` builds the path and calls `0x40fee8`. It reads count
at `0x40ff4e`, allocates 26*N and reads that many bytes at `0x40ff83`.
Runtime list is 6 bytes: count `+0`, pointer `+2`.

| Record offset | Width | Use |
|---|---:|---|
| `+0` | i16 | DTS part ID |
| `+2..+22` | 21 bytes | Opaque attachment parameters; byte +6 is a signed category |
| `+23` | i8 | Display/damage slot number |
| `+24..+25` | 2 bytes | Opaque parameters |

`0x40fc50` builds a part-ID list for attaching weapon shapes: category
byte `+6 < 4` uses the part ID, otherwise emits -1. `0x42039e` passes this
list to the shape attachment path. `0x417e47..0x417e64` adds 19 to signed
slot byte `+23` to select a DMG region. `0x437cf6..0x437d08` instead searches
GL for equality with a PDG visible-slot ordinal. Thus GL can contain more
slots than the PDG display. All 121 part IDs resolve in same-stem DTS root 0.
Weapon augmentation also adds 12 to the signed slot byte at
`0x4176ff..0x417707` for a DMG capacity index, written via `0x40d128`.
The other parameters' numeric types and units are **unresolved**, not assumed
from the 26-byte stride.

## PDG: damage-display layout, with a separate WEAPONS schema

Basename selects the schema. Vehicle reader `0x4379cc` is
called by `0x420401`. `WEAPONS.PDG` uses `0x4378d8` instead. Neither starts
with a tag. The vehicle runtime allocation is 92 bytes (`0x4378d8` setup):
three 28-byte view descriptors followed by an 8-byte slot-list descriptor.

Vehicle file: `i32 V`; V views; `i32 W`; W records of 20 bytes. Each view:

| View offset | Width | Meaning |
|---|---:|---|
| `+0,+4` | i32 each | First screen-coordinate pair |
| `+8,+12` | i32 each | Second screen-coordinate pair |
| `+16` | i32 | Rectangle count Q |
| `+20` | 28*Q | Display rectangles |

Rectangle offsets: `+0` i32 display selector (exact mapping unresolved),
`+4,+8,+12,+16` i32 coordinate pairs, `+20` i16 color-table index,
`+22` two ignored bytes, `+24` four opaque bytes. At `0x437b5b..0x437b67`
the color word indexes the 27-word table loaded from COLORS.DAT at
`0x467b64..0x467b81`; the resulting value overwrites all four bytes at
runtime `+20`, so file `+22..23` is ignored. Do not call selector `+0`
a direct DMG index: the display consumer at `0x43a8e3` branches/remaps it.

Each 20-byte weapon-display record has i32 x/y at `+0/+4`, signed bitmap
offset at `+8` (added to weapon-definition image ID at `0x437d43`), two
opaque bytes `+10`, alignment bits at `+12` (`0x437d67..0x437dcd`) and a
display mode at `+16` (`0x437e15`). Exact mode names remain unresolved.

The reader obtains view bitmap pointers from the same-stem DBA/HBA image
array (`0x437a99..0x437aa8`); those pointers are not in PDG. View runtime
offsets are bitmap `+0`, first pair `+4`, second pair `+12`, count `+20`,
rectangle pointer `+24`. Coordinate shifts at `0x437ad0..0x437b9e` and
`0x437c19` adapt display resolution. These are screen coordinates, not
6-mm model/world positions. Tail list is always runtime `+0x54`.
The bounded reader rejects V > 3 to avoid the original fixed allocation's
overrun. All 22 vehicle copies have exactly three views.

**WEAPONS exception:** `i32 N`, then N pairs of i32 screen dimensions/offsets
(8*N bytes), no vehicle views or tail. Reader `0x437959..0x437971` fills
global `0x4d16d4`; the display consumer uses these pairs to align bitmap
placement (`0x437d94..0x437e12`). The observed N is 14; semantic distinction
between nominal size and anchor offset remains **open**.

## Collision virtual methods: dependency boundary

The vehicle actor vtable is `0x49a282`, installed at `0x415c60`.
These mappings hold for that vtable, not every actor subclass.

| Slot | Function | Resource dependency |
|---|---|---|
| `+0x5c` | `0x415518` | Returns config word `+0x70`, i.e. DAT file `+0x6e` |
| `+0x7c` | `0x41552c` | Same DAT radius source |
| `+0x68` | `0x42200c` | Sets actor byte `+0xb1`; no file read |
| `+0x70` | `0x4187d0` | Shields, then 29 live DMG regions; `+0x58` centers, range falloff, `+0x74` damage |
| `+0x74` | `0x417de4` | GL slot-to-region mapping; `0x40da38` on actor damage context; COL for effects |
| `+0x58` | `0x41b60c` | DMG region runtime `+0xc/+0xe` caches part/center; transforms the COL-derived center, or uses actor position |
| Actor `+0x20` | `0x418ba8` | Projectile path, calls COL traversal `0x40c9d4` at `0x418ddf` |

**Distinction:** ground collision `0x418f74` calls the **DTS
part-list's** virtual `+0x20` to resolve DAT-selected parts; that is not the
actor's `+0x20`. Its object broad phase uses the DAT radii above, and its
contacts/slide branch reach DMG through `+0x70/+0x74`. Static buildings use
`0x404ae4`/BASECOL. No direct PDG or GL geometry read was found in ground
collision. BND-like values do occur as compiled globals: see [bnd.md](bnd.md).

The flyer vtable `0x49a5e0`, installed at `0x421692`, differs: `+0x20` is
`0x421c8c`, reaching COL at `0x421d67`; `+0x74` is `0x421bb4`, reaching
DMG application at `0x421bdb`. Its generic radial hit `+0x70 = 0x411b3c`
uses actor position and calls `+0x74` with region zero (`0x411b82`), without
the vehicle's 29-center loop. `+0x5c = 0x411aa4` and `+0x7c = 0x411aac`
return zero, while `+0x68` remains `0x42200c`. Flyer `+0x58 = 0x411a3c`
is an empty method; do not apply the vehicle center-cache contract to it.

## Corpus verification and remaining work

An independent corpus scan covered all 24 CD `.VOL` files;
16 are VOLN, eight are other formats. The three VER31 VOLN archives contain
voice only, hence no extra members of these types. No members were extracted.

| Type | Physical copies / bytes | Complete decoded counts |
|---|---:|---|
| COL | 22 / 7,452 | 135 groups, 389 shapes (166 empty), 664 spheres |
| DMG | 22 / 7,554 | 463 capacities, 610 regions, 415 weighted links |
| GL | 21 / 3,188 | 121 attachments |
| PDG | 23 / 19,996 | 66 views, 573 rectangles, 117 visible slots; 14 WEAPONS pairs |

Every byte is consumed, with no suffix. COL parts (-1 or 0..88) and region
indices (0..28), DMG capacity links and GL part IDs were checked against
their companion data. Sphere coordinates range -1450..1750; radii 40..600.
Capacity values range 0..5000, link weights 1..150. These are observations,
not parser limits. GL entries outside the visible PDG range: MIRIMAC slot
record 3, HYPERION record 6, SCARAB records 3 and 4 (zero based). This is a
display omission, not a malformed GL file; reverse matching is audited.
The expanded checks total 524 COL, 2,245 DMG, 480 GL and 619 PDG references,
with zero failures. DMG includes all links, parent regions, DTS cell slots
and combined debris-list references. PDG includes all color indices and
both DBA/HBA count bounds per physical file.
GL checks both DMG region and capacity mappings in addition to model parts
and the reverse PDG display lookup.

Remaining hypotheses and unassigned semantics are deliberately outside the
reader contract. The following observations would settle them.

| Open question | Required observation |
|---|---|
| Live vehicle/flyer coverage and collision parity | In the c1_01 collision suite, record the vtable, COL part/region choice, transforms and DMG state around `0x418f74`, `+0x70/+0x74` and `0x40da38`; distinguish the two runtime layouts above. |
| Weapon-added COL shapes and cached DMG centers | Capture actor `+0x1f6/+0x1fa/+0x206` before/after `0x41778f..0x41781d`, then observe which cached center is used for a real hit. |
| DMG units, capacity names, cell/debris labels and flag meanings | Correlate threshold/capacity changes and each `0x40d434` effect branch with controlled part destruction, rendered cell changes and debris creation. No HP/joule conversion is asserted. |
| Opaque GL fields and their units | Record the record and its consumers during attachment, aiming and firing, including categories with no DTS part. |
| PDG selectors, opaque fields, modes and WEAPONS pair meanings | Trace `0x43a8e3`, `0x437c8c` and drawing calls for the same damage/loadout at both resolutions; match each selected bitmap and screen rectangle. The bitmap offset's final index also depends on the equipped weapon. |
| Actual archive registration and loose-file precedence | Log accepted roots/flags at `0x467cd8`, priority insertion and the successful open path. Static ordering is proved; a particular installation's registration is not. |
| VSHELL or other dynamically constructed load paths missed by the static search | Record resource-open paths/callers through shell initialization, loadout and mission return; a single run supplies coverage, not proof of universal absence. |
| BND loader, schema, prefix and compiled-default relationship | See [bnd.md](bnd.md#observation-that-would-settle-the-loader-hypothesis). |

## Re-derivation

COL and GL were re-derived independently from the original disassembly and
walked without the first parser: all 43 members have identical
framing/counts and exact EOF. The bounded reader enforces the original COL
signed read-length narrowing (S <= 4095, synthetically tested).
Tests construct synthetic values. Reports contain statistics, identities
and offsets only, no extracted members or converted assets.
