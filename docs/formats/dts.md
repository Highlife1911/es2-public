# EarthSiege II DTS shapes

Status: **all 55 CD DTS members parsed to EOF; original readers checked
statically**. Both leading signatures are class tags in the same
object grammar. This is not the A-10 demo DT2/DML layout.

**Confirmed (data)** means measured CD bytes or the bounded corpus audit.
**Confirmed (static)** means instructions in the reference executable support
the claim. Neither means a runtime or visual test. **Hypothesis** identifies
an interpretation still needing an oracle check. Addresses below are VAs at
image base `0x00400000`; offsets are DTS-payload-relative unless qualified.
All integers are little-endian. `W` denotes two bytes where the read span is
confirmed but signedness is not needed to describe the file.

## Container

**Confirmed (data):** all 55 resources are in `VOL/SIMVOL0.VOL`, total
**2,508,462 bytes**. Thirty-three start `08 00 14 00`, twenty-two start
`03 00 1e 00`. There are **479 concatenated root objects**, not 55: 327
`TSShape` and 152 `ANShape`. Each member uses one root class throughout.
The outer VOL block and its extra suffix are separate; see [vol.md](vol.md).

**Confirmed (static and data):** an object at offset `B` consists of:

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| `B` | 4 | Complete class tag, used as one `u32` lookup key |
| `B+4` | 4 | Body byte length `L`, excluding this eight-byte header |
| `B+8` | `L` | Class-specific body, including any nested object headers |
| `B+8+L` | 0 or 1 | Alignment to the next even stream position |

There is no independent file-version field in these members. In particular,
the high words `0x14` and `0x1e` must not be interpreted as DTS versions 20
and 30. `0x00140008` selects `TSShape`; `0x001e0003` selects `ANShape`.
All observed object lengths are even, so no DTS alignment byte is present
in this corpus. Odd-length handling is confirmed only by the original code.

**Confirmed (static), DBSIM loader chain:**

- `0x474cd8` opens a multi-object resource, peeks successive headers with
  `0x47a308`, checks the requested root tag, and loads roots with `0x47a144`.
  The loop at `0x474d74-0x474d86` continues to EOF. `0x474de4` selects one
  indexed root. These are distinct from the generic single-object wrapper
  at `0x474b30`.
- `0x47a144` calls `0x47a6b4` for the header. The eight-byte read is at
  `0x47a6db`; `0x47a6fe-0x47a712` computes the endpoint as
  `(position_after_header + L + 1) & ~1`.
- `0x47a4a0` compares the complete tag against registry entries, advancing
  by 12 bytes. Each entry contains tag, factory pointer and class-name
  pointer. The factory constructs an object and invokes its read method;
  e.g. `TSGroup`, `0x48e8b0-0x48e90b`, uses vtable slot `+0x0c`.
- `0x47a83c` drains bytes up to the saved endpoint after the handler returns.
  Thus the original's successful load alone does **not** prove the handler
  consumed every body byte. The scratch parser separately requires exact
  consumption before advancing to the next object.

The TS registry is at `0x4a63b0` (18 entries), the AN registry at
`0x4a687c` (four); registration calls are at `0x48dd5e` and `0x4913e2`.
Their original class-name strings establish the names used below.

**Confirmed (static), VSHELL:** generic object loader `0x4041ab` calls
header reader `0x404818`, registry lookup `0x4045b4`, and endpoint consumer
`0x404a68`. Corresponding registries are `0x47f2f4` and `0x47f7ec`, with
registration calls at `0x45fd98` and `0x463a52`. The shape readers are linked
and registered in VSHELL. **Hypothesis / unproved:** that the Herc preview
actually calls them; linked engine code is not evidence of that UI path.

## Objects per tag

**Confirmed (static and data)** for rows with a nonzero count. A zero count
means **static only**, with no CD DTS example. Counts include roots and
nested objects. Reader addresses are method entries, not constructors.

| Tag | Original class | Count | DBSIM reader | VSHELL reader |
| --- | --- | ---: | --- | --- |
| `00140001` | TSPoly | 1,947 | `48e02c` | `460034` |
| `00140002` | TSSolidPoly | 1,517 | `48e258` | `46025c` |
| `00140003` | TSShadePoly | 31,782 | `48e258` | `46025c` |
| `00140005` | TSBasePart | 0 | `48f9a0` | `46195c` |
| `00140007` | TSPartList | 400 | `48fea0` | `461e5c` |
| `00140008` | TSShape | 327 | `490e68` | `462dc4` |
| `00140009` | TSGouraudPoly | 765 | `48e588` | `460580` |
| `0014000a` | TSBSPGroup | 0 | `48ef20` | `460ef8` |
| `0014000b` | TSCellAnimPart | 2,158 | `4900dc` | `4620a0` |
| `0014000c` | TSDetailPart | 92 | `490288` | `462248` |
| `0014000e` | TSNullObj | 0 | `48df54` | `45ff60` |
| `0014000f` | TSTexture4Poly | 3,806 | `48e258` | `46025c` |
| `00140010` | TSAliasSolidPoly | 0 | `48e258` | `46025c` |
| `00140011` | TSAliasShadedPoly | 0 | `48e258` | `46025c` |
| `00140012` | TSAliasGouraudPoly | 0 | `48e588` | `460580` |
| `00140013` | TSBitmapPart | 624 | `48fce0` | `461c98` |
| `00140014` | TSGroup | 8,379 | `48e9f0` | `4609dc` |
| `00140015` | TSBSPPart | 433 | `490488` | `462440` |
| `001e0001` | ANSequence | 147 | `4914f0` | `463b5c` |
| `001e0002` | ANAnimList | 152 | `491bb0` | `464208` |
| `001e0003` | ANShape | 152 | `492244` | `46488c` |
| `001e0004` | ANCyclicSequence | 285 | `4914f0` | `463b5c` |

### Part bodies and nesting

**Confirmed (static and data):** the common part prefix, called `B12` here,
is 12 bytes: transform index `i16`, part ID `W`, radius `i16`, and center
`3*i16`. `-1` means no part-transform lookup. The reader `0x48f9a0` reads
three words followed by six bytes. Original debug labels at `0x4a6154`
identify transform, ID, radius and center. Observed transform indices are
`-1..19`, IDs `0..99`; IDs are identifiers, not necessarily dense array
positions.

The following describes **body contents only**; inherited fields are inline,
without a second header. Each occurrence of `object` has its own header.

| Class | Body grammar |
| --- | --- |
| TSBasePart | `B12` |
| TSNullObj | Empty |
| TSPartList | `B12`, `u16 N`, then `N` objects |
| TSGroup | `B12`, four `u16` counts `I,P,C,F`; `I` word indices; `P` signed coordinate triples; `C` four-byte surface words; `F` polygon objects |
| TSCellAnimPart | TSPartList body, then one word selecting a cell-animation slot |
| TSDetailPart | TSPartList body, then **N signed word thresholds**, using the already-read child count |
| TSBSPPart | TSPartList body; `u16 K`; `K` 14-byte planes/links; `K` signed transform-index words |
| TSBSPGroup | TSGroup body; `u16 K`; `K` 10-byte plane/links records; static only |
| TSBitmapPart | `B12`, bitmap index `u16`, first offset `i8`, second offset `u8`; 16 bytes total |
| TSShape | TSPartList body; `u16 T`, `u16 S`; `S` words named `seqLenList`; `T` 32-byte transforms |
| ANShape | TSShape body followed by one ANAnimList object |

**Confirmed read-size details:** `0x48ea0b-0x48eb08` reads the group's four
counts and arrays in that order. `0x4902a9` obtains the detail-list length
from the existing child count: there is **no extra count word**.
`0x4904bf` supplies stride `0x0e` for BSPPart nodes, followed by the separate
word array at `0x490517`. BSPGroup's multiplication is by ten at
`0x48ef4e-0x48ef73`. `0x490f13-0x490f1e` reads `T << 5` transform bytes;
all 479 CD roots have `T=0`, so nonempty serialized matrices are static-only
evidence. `0x492255-0x492260` reads the inherited shape and then one object.

**Confirmed (static and data), BSPPart:** each node is `3*i16 normal`,
`i32 coefficient`, `i16 front`, `i16 back`, followed elsewhere by its
transform word. Traversal `0x476b28` uses node zero as the root; a negative
link is absent (only `-1` occurs). Bit `0x4000` marks a child-part reference
whose index is `link & 0x3fff`; otherwise the link is a node index.
`0x476b7c-0x476b8f` uses the transform word to index 32-byte runtime matrices.
All 433 node graphs are acyclic with in-range links. The transform-index
exceptions are listed under verification. BSPGroup's static-only record is
`i32 coefficient`, polygon-index word, front/back words; its traversal at
`0x475b2c` also distinguishes terminal polygon links with bit `0x4000`.
The coefficient's independent physical units remain unproved.

### Detail selection

**Confirmed (static):** `TSDetailPart` renders one child. At
`0x4769c8-0x476a92`, it computes a projected-size measure from radius,
approximate distance, projection shift and a global adjustment, then compares
it with its signed thresholds. The loop stops on `measure <= threshold`,
subject to the original detail offset and final-child clamp. The 92 objects
have 446 thresholds in `4..255`. These are not metre distances.

Vehicle root arrays have a **separate** LOD mechanism. `0x42024f` loads all
ANShape roots and `0x42025b` installs the seven-dword threshold table at
`0x49a344` (range `6..75`, descending). `0x4033e4-0x4034e9` selects a root
from projected size, renders it through the existing instance, then restores
root zero. `0x420090` matches part IDs from the vehicle DAT part list and
`0x41ff88` remaps alternate roots' transform indices. A viewer may expose
roots independently for inspection; faithful actor rendering must reproduce
this remapping and shared instance state.

## Meshes

**Confirmed (static and data):** there is no standalone array of vertices
followed by a separate normal array. A group's `P` six-byte signed triples
are shared by vertex positions, polygon centers and normals. Its `I` word
indices refer into that point array. Polygon fields refer into these arrays,
not into another object's mesh. Body layouts:

| Polygon class | Fields in body | Bytes |
| --- | --- | ---: |
| TSPoly | normal point index, center point index, corner count, first index-list entry: four `u16` | 8 |
| TSSolidPoly / TSShadePoly / TSTexture4Poly | TSPoly fields, then surface-word index `u16` | 10 |
| TSGouraudPoly | Previous fields, then first normal-index-list entry `u16` | 12 |

The aliases have the corresponding solid/shade/Gouraud read layouts but no
corpus examples. Reader `0x48e02c` and the derived readers in the tag table
prove the spans. Labels at `0x4a5d54-0x4a5d9e` and renderers
`0x474ec0`, `0x474fa8`, `0x475538` establish the array roles.

**Confirmed (data):** 8,379 groups contain 110,460 shared points, 156,207
indices and 39,817 polygon objects. Group ranges are 3-84 points, 1-168
indices and 1-43 polygons. Corner counts range **1-8**; do not reject every
record with fewer than three corners as a malformed triangle. Textured
records have three or four corners despite the class name `Texture4`.
**Confirmed (static):** TSPoly's draw method `0x474eb8` immediately returns;
the 1,947 base polygon records are not all visible filled surfaces. Their
additional gameplay/attachment uses are unproved here.
All point, corner-span, center, face-normal and Gouraud normal-span/index
checks pass. Vertex components range `-3843..5200` across the corpus.

**Confirmed (data and static), numeric representation:** geometry coordinates
are signed integer words. Nonzero face-normal squared lengths are
`4,187,592..4,195,533`, close to `2048^2`; 337 face-normal references are zero.
Lighting at `0x48c048-0x48c07e` uses the dot product and `0x400000` scale,
and the point-light path shifts by 11 at `0x48c144`. This supports a normal
unit of 2048 (Q11), not a metric position or a float.

**Confirmed (static):** the 32-byte matrix contains nine signed Q14 words
at offsets `0..0x10`, a dispatch byte at `0x12`, another byte at `0x13`
whose meaning is open, and three signed dword translations at
`0x14,0x18,0x1c`. It is not nine conventional row-major floats. In
`0x4801d4`, output X uses matrix word offsets `0,4,0x0c`, Y uses
`2,6,0x0e`, Z uses `8,0x0a,0x10`. Each full rotation dot product adds
`0x2000`, shifts arithmetically by 14, then adds the unscaled translation.
Identity writes `0x4000` at offsets `0,6,0x10` (`0x47ebec-0x47ec12`).
The dispatch byte selects translation-only, XY rotation, or full rotation.

**Confirmed (static link to world units):** actor drawing constructs the
rotation at actor `+0x12` from angles at `+0x0c` (`0x401fe4-0x402010`);
that matrix's translation is actor `+0x26`, the calibrated world position.
Part matrices are composed at `0x4761af-0x4761d3`. The perspective vertex
path takes the group's unscaled signed triples at `0x4912e5-0x491319` and
calls the same `0x4801d4` transform before projection. Animation translation
words are sign-extended directly into matrix translations at
`0x478c29-0x478c51`. Thus, on this traced path, a local geometry or pose
translation unit has the same scale as a world-position unit: **6 game mm**,
using the distance-display proof in
[player_state.md](../reverse-engineering/player_state.md). This does not
give metric units to normals, BSP coefficients, LOD thresholds or ticks.

## Materials

**Confirmed (static and data):** groups contain 44,932 four-byte surface
words. Every coloured/textured polygon's surface index is a multiple of four
and has four in-range words available. For solid/shaded polygons these are
front fill, front edge, back fill, back edge; selection is in
`0x474ec0-0x474f25` and `0x475538-0x4755a7`. The view-side test uses the
normal and center (`0x48c72c`). A high byte of `0x14` has a skip role when
both selected fill/edge words carry it. **Hypothesis:** a general transparency
flag interpretation beyond those tested branches; do not treat this dword
as RGBA.

**Confirmed (static):** solid colours pass through the palette-index remap
at `0x468158`. Shaded polygons first obtain light intensity from `0x48bfe8`
and call `0x430e34`: the low byte selects a ramp in the active DPL at
`0x49b020`, with bounds/clamping and an intensity-dependent entry selection.
That result then passes through the remap. See [dpl.md](dpl.md) for the
serialized ramp and RGB formats. The shade selector is not intrinsically
the final displayed palette index.
Gouraud rendering instead evaluates `0x48bfe8` for each indexed vertex normal
and position, negating the normal on the back side, then supplies an intensity
list to rasterization (`0x47576b-0x4758a4`).

**Confirmed (static), palette binding:** `0x42e042-0x42e093` builds
`world` plus decimal `2*W(0x4cfcc4)+W(0x4cfcd6)`, resolves it with the `dpl`
extension, and calls `0x430394`. That function loads a palette through
`0x465f7c` and installs it at `0x49b020`. `WORLD0..WORLD9.DPL` are present
in SIMVOL0. The two selector fields' gameplay meanings and temporary palette
switches are not fully traced here. **Hypothesis:** using WORLD0 as the
default preview palette; it is a selectable preview choice, not a palette
name embedded in OUTLAW.DTS.

**Confirmed (static), textured faces:** `0x474fa8` uses the first or third
surface dword as a bitmap index in the currently installed texture bank;
`0xffffffff` is the unavailable-side sentinel. The renderer uses the whole
dword, not its low byte. `0x47507e-0x47513c` indexes either the loaded bitmap
pointer array or a 20-byte-per-image atlas rectangle array. The two edge
words are not used as texture IDs on this path. This is why `0x00000c0c`
cannot silently be reduced to texture 12 in a purported faithful parser.

There are **no serialized UV records** in these polygon bodies. The software
path creates the four texture corners at `0x475289-0x4752d0`, starting at
`(3,3)` and using bitmap width/height minus three for the far edges. The atlas
path reads rectangle coordinates at `0x4753d2-0x47542e`, with back-side
reordering afterward. Texture projection, clipping and rasterization parity
still need a visual oracle test. Bitmap parts use their separate bank index
and two byte offsets (`0x4763f4` renderer): the first is sign-extended at
`0x476669`, while the second is zero-extended at `0x476646-0x476648`.
See [dbm.md](dbm.md) for DBA/DBM.

### External bitmap banks

**Confirmed (static and data):** DTS contains numeric material references,
not bitmap filenames or a DML list. Vehicle DAT word `+0x94` selects one of
seven bank descriptors. `0x41fe8a-0x41fedd` loads the names from `0x49a360`,
resolves `.dba`, and creates atlas rectangles. `0x4203d6-0x4203ee` attaches
the selected descriptor to each shape; `0x490c80-0x490cd0` installs/restores
it around rendering. Vehicle debris inherits that bank at
`0x420467-0x420483`. Archive entry counts are read from DBA offset `+8`.

| DAT bank ID | DBA basename | Bitmap count |
| ---: | --- | ---: |
| 0 | LIGHT | 24 |
| 1 | MEDIUM | 36 |
| 2 | HEAVY | 41 |
| 3 | ENEMY | 45 |
| 4 | APOCATEX | 53 |
| 5 | RAZORTEX | 22 |
| 6 | NEWHERCS | 44 |

**Confirmed example:** OUTLAW's 90 non-sentinel texture-side references
range `0..23`, all inside LIGHT's 24 entries. `dba\OUTLAW.DBA` also exists,
but the traced shape binding is LIGHT. LIGHT payload is at SIMVOL0
`0x6a9222`, length 44,916, SHA-256
`044bf8d4608145231f3d8ebb08a53a73bb036bad549f6423f9528a41a801f2bb`.

**Confirmed additional bindings:**

| DTS | Bank / original evidence |
| --- | --- |
| SKIMMER | ENEMY, `0x422f1c` and descriptor `0x4a9d8a` |
| MECHWPNS, MECHWPN2 | WPNTEX, 23 images; `0x40fab0-0x40fb00`, `0x40fc8c-0x40fd24` |
| SMOKE | SMOKE, 36; `0x40907e-0x4090b0` |
| METEOR | IMPACT, 25; `0x409af7-0x409b29` |
| BULLETS | BULLETS, 8; `0x40af11-0x40af4d` |
| FLAT2 | FLAT2, 1; name choice `0x4097cf`, bank attachment `0x40985a` |
| BASES_AN | BASETEX (66) or VEHTEX (63), chosen by the base record; `0x405f1e-0x405f35`, loads `0x406713-0x406790` |
| FIRE / FIRE2 | FIRE0/FIRE1 (24/28), or FIRE0S/FIRE1S (12/14); per-root selector bytes from FIRE.DAT `+4`, `0x46b31f-0x46b338`, `0x46b392-0x46b40d` |
| EXPLOS / EXPLOS2 | EXPLO family; per-root bank word from the first DAT table at `0x407e14-0x407e2e` |

For EXPLOS, the loader constructs EXPLO0 through EXPLO14. For EXPLOS2 it
constructs EXPLO0 through EXPLO12: although `0x407c33` appends `s` to the
initial `explo666`, `_itoa` at `0x407c44` overwrites from character five,
including the terminating NUL. Thus this inspected path does not preserve
the `s` suffix. The import names are confirmed through IAT slots `0x6cc338`
(`_strcat`) and `0x6cc35c` (`_itoa`). This is a static observation, not a
claim about which graphics-mode branch was exercised in a mission.

## Animation

**Confirmed (static and data):** all 152 ANShapes contain one ANAnimList.
The animation body has the following order; counts are two-byte fields.
Readers are in the tag table. Original labels at `0x4a662c`, `0x4a6654`,
`0x4a671f` and `0x4a6777` support the field names.

| ANAnimList component | Serialized contents |
| --- | --- |
| Sequences | Count `S`, then `S` ANSequence/ANCyclicSequence objects |
| Transitions | Count `T`, then `T` eight-byte records: tick, destination sequence, destination frame, ground-movement word |
| Poses | Count `X`, then `X` twelve-byte records: three rotation words, three signed translation words |
| Default poses | Count `D`, then `D` signed pose-index words |
| Relations | Count `R`, then `R` pairs of signed words: parent transform slot, destination transform slot; parent `-1` is a root |

Each sequence body is tick, priority, ground-movement word, frame count `F`;
then `F` six-byte records `(tick, first transition, transition count)`;
then part count `P`, `P` signed part-slot words, and `F*P` signed pose-index
words. `0x478f19` addresses that last array in **frame-major** order and
indexes twelve-byte poses. A zero transition count does not require its
otherwise unused start index to be a valid table entry.

**Confirmed (static):** defaults initialize runtime pose slots. The instance
allocation at `0x478890-0x4788de` uses the default-list count, distinct from
the pose-pool count. `0x478c64-0x478d16` processes relations in file order,
composing a destination's local pose with its parent's matrix, or constructing
a root matrix for parent `-1`. The default/sequence pose index is not itself
a part ID or a matrix index. All 152 relation lists have unique destinations,
are acyclic and place each non-root parent earlier in the list.
After this initial evaluation, `0x47892b-0x478947` explicitly resets matrix
slot zero to identity; a default-pose preview should preserve that step.

Rotation words are wrapping angle bit patterns, consumed by `0x47ebb8`:
the Z-only branch uses a quarter-turn offset `0x4000`, table lookup and Q14
matrix output (`0x47ec1e-0x47ecaf`). Translation words are sign-extended
without a fractional shift. This establishes 65,536 angle units per turn;
the full three-axis composition order and interpolation rounding remain to
be specified before a faithful animation implementation.

**Confirmed (data):** 432 sequences, 2,446 frames, 19,056 frame/part pose
indices, 13,291 poses, 989 transitions, 1,598 defaults and 1,485 relation
pairs. Sequence tick fields
range `100..1200`, frame tick fields `50..100`, priorities `0..96`, and
ground-movement words are 0 or 1. Pose translations range `-2807..5000`.
No conversion of these tick fields to milliseconds is established here.

**Confirmed (static):** ordinary sequence frame navigation clamps at the
ends (`0x478760`, `0x47877c`); cyclic navigation wraps (`0x4787e4`,
`0x478804`). The instance has ten active-sequence slots (`0x4789b0`,
`0x478a3c`) and sorts by priority at `0x4789d0`. Scheduling, interpolation,
transition triggering and ground-motion integration are not fully verified.

**Confirmed separate mechanism:** TSCellAnimPart does not directly index
the ANSequence array. `0x4768f0` reads a cell slot from global `0x6b7b6c`
and selects `slot_value % child_count`. The TSShape `seqLenList` belongs
with this cell state; its count can differ from the AN sequence count.
**Hypothesis:** particular cells/sequences represent damage, destroyed parts,
walking or weapon motion. Those names must come from gameplay consumers or
runtime evidence, not from ordinal guesses. Multiple vehicle roots are
confirmed LOD alternatives, and separate `*_DEB.DTS` resources are confirmed
debris bindings; neither establishes every damage-state transition.

## Resource map

**Confirmed (data and static):** `nam\MECHS.NAM` contains the 21 basenames
below in this order. `0x42049c` loads the name table; `0x4201c1-0x42024f`
uses the selected basename for both a 216-byte DAT and its ANShape DTS.
DAT display labels supply the names where spelling differs. Each debris
basename is the corresponding DAT's NUL-terminated slot at `+0xcc`.
All names below are SIMVOL0 `dts\<basename>.DTS`.

| Name-table ID | Vehicle DTS / label if different | Roots | Bank | Debris DTS |
| ---: | --- | ---: | --- | --- |
| 0 | OUTLAW | 7 | LIGHT | OUTL_DEB |
| 1 | RAPTOR2 / Raptor II | 7 | NEWHERCS | RAPT_DEB |
| 2 | TOMAHAWK | 7 | MEDIUM | TOMA_DEB |
| 3 | SAMSON | 7 | HEAVY | SAMS_DEB |
| 4 | COLOSSUS | 7 | HEAVY | COLO_DEB |
| 5 | APOCA / Apocalypse | 7 | APOCATEX | APOC_DEB |
| 6 | OGRE | 7 | NEWHERCS | OGRE_DEB |
| 7 | MAVERICK | 7 | NEWHERCS | MAVE_DEB |
| 8 | RAZOR | 6 | RAZORTEX | Empty slot |
| 9 | MONGOOSE | 7 | ENEMY | MONG_DEB |
| 10 | STINGRAY | 7 | ENEMY | STIN_DEB |
| 11 | MIRIMAC | 7 | ENEMY | MIRI_DEB |
| 12 | RAMSES | 7 | ENEMY | RAMS_DEB |
| 13 | ACHILLES | 7 | ENEMY | ACHI_DEB |
| 14 | HYPERION | 7 | ENEMY | HYPE_DEB |
| 15 | PITBULL | 7 | ENEMY | PITB_DEB |
| 16 | SPIDER / Arachnitron | 5 | ENEMY | Empty slot |
| 17 | DIABLO | 7 | ENEMY | DIAB_DEB |
| 18 | HEADHUNT / Headhunter | 7 | ENEMY | HEAD_DEB |
| 19 | SCARAB | 7 | ENEMY | SCAR_DEB |
| 20 | CERBERUS | 7 | ENEMY | CERB_DEB |

The 19 listed debris files have seven TSShape roots each, except PITB_DEB
with eight. These 40 resources plus the following 15 enumerate all 55.
Roles in the table are confirmed by named original load sites; individual
root-to-gameplay labels are not implied.

| Other DTS | Roots | Confirmed reference / role |
| --- | ---: | --- |
| BASE_DEB | 33 | Generic debris loader `0x40874c`, caller `0x4067d5-0x4067e1`; matching DAT |
| DEF_DEB | 7 | Same loader, name reference `0x4088fe`; matching DAT |
| EXPLOS | 20 | Effect shape array, `0x407cb4-0x407cd7`; matching DAT |
| EXPLOS2 | 18 | Alternate effect array, `0x407cad`; matching DAT |
| SMOKE | 8 | `0x409054-0x409074` |
| METEOR | 2 | `0x409acd-0x409aed` |
| ROCKETS | 2 | `0x40a916-0x40a937`; matching DAT |
| BULLETS | 9 | `0x40aeed-0x40af07`; DAT record's leading word indexes the root array at `0x40af81-0x40af94` |
| MECHWPN2 | 25 | Weapon shape array, `0x40fce5-0x40fd09` |
| FIRE | 4 | `0x46b246-0x46b2b3`; FIRE.DAT has four per-root selector bytes |
| FIRE2 | 4 | Conditional suffix at `0x46b278-0x46b28b`; same FIRE.DAT |
| FLAT2 | 4 | Conditional name at `0x4097d6`; counterpart name `flat` has no DTS member on this CD |
| BASES_AN | 8 | Animated base root selected by base record at `0x405ed7-0x405f14` |
| SKIMMER | 1 | Separate flyer loader `0x422ed0-0x422f1c`; 46-byte DAT |
| MECHWPNS | 56 | Weapon loader `0x40f998` caches each serialized TSShape; `0x40fab0` later reads selected roots from memory |

**Confirmed mission connection:** the 134-byte mission actor-class record
is read at `0x425644-0x425654`; its word at record offset `0x28` is passed
to vehicle constructor `0x415bb0` at `0x425698-0x4256a4`. The constructor's
vehicle/config route ultimately supplies the shape array to `0x4033a4`.
The alternate construction branch uses its remapped runtime table instead.
This confirms an actor-type-to-name-table-to-DAT/DTS route, not equality of
mission record ordinal and vehicle ID; see [mission.md](mission.md).

**Confirmed name resolution:** `0x492bec` constructs the extension directory
and filename, e.g. `dts\outlaw.dts`, from basename and extension. The strings
`.` and `\` are at `0x4a6939` and `0x4a693b`. Case folding and loose-file/VOL
precedence are not covered here. `FLYERS.NAM` also names
HOVTANK and DROPSHIP, but neither has a DTS member in the scanned archives.
Nonanimated bases are loaded separately as `bases.dgs` with tag `0x02bc0001`
at `0x405f48-0x405f6d`; the DTS corpus is not a complete building inventory.

## Verification

**Confirmed input identity:** all 13 `VOL/*.VOL` archive hashes, both EXE
hashes, all 55 DTS-member hashes and the 33 bitmap banks used in bounds checks
match [iso_file_hashes.sha256](../../data/iso_file_hashes.sha256).

| Input | SHA-256 |
| --- | --- |
| VER95/DBSIM.EXE | `8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad` |
| VER95/VSHELL.EXE | `1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e` |
| VOL/SIMVOL0.VOL | `f352762f0b21d10d0fdcd1a770654ae6f8fa2de78214ed01ef51636c47582026` |
| dts\OUTLAW.DTS | `dd1ed24e5de6b7506911d5e31773dff939416ef1472ad009ed5d01a273380880` |
| dts\BASES_AN.DTS | `3d1d658984bf9d581b91cce5a51b2c5100495feaa561a1ac20d7bb520a47e672` |
| dts\APOCA.DTS | `cd2b6484e3be1a051e6d5a7be93ee01e29d6602818bd9ed251a9deed9214213b` |
| dts\TOMAHAWK.DTS | `907e682bdbfa6ed06ace15ee9758dab413edaa00b707be3e9751c8eb1f102115` |

**Confirmed audit results:**

- All **52,966 object bodies** consume exactly their declared lengths; all
  55 members end exactly at the final root boundary. No unknown tags,
  padding, trailing bytes or opaque skipped bodies occur. Maximum depth is
  five, counting a root as zero; body sizes are 8-45,824 bytes.
- All mesh-local counts, index spans and point references pass, including
  the four-word surface spans. All child/cell-slot checks pass.
- All 433 BSP link graphs pass range and cycle checks. All AN default-pose,
  sequence-pose, sequence-part, relation and transition-destination indices
  pass. One frame-to-transition span fails, below.
- All 624 bitmap-part indices fit their statically bound banks. Of 3,876
  non-sentinel texture-side references, nine fail bank bounds. BASES_AN
  references were checked against **both** candidate banks; its precise
  base-record-to-bank/root map remains open. There are no texture references
  omitted merely because the bank was unknown.
- Eighteen BSP transform words in ten BASES_AN objects exceed their root's
  raw default-list domain. All other base-part/BSP transform checks pass.

These checks do not prove correct animation timing, face winding, visual
output, original memory safety or runtime reachability of anomalous cells.

### OUTLAW example

**Confirmed (data):** VOL block `0xdd6280`, payload `0xdd6289`, length
73,222 (`0x11e06`). First header bytes are
`03 00 1e 00 46 44 00 00`: ANShape with a 17,478-byte body. Its first
part prefix begins `ff ff 00 00 64 06`: transform `-1`, ID 0, radius 1636.

| Root | DTS offset | Body bytes | Next root / EOF |
| ---: | --- | ---: | --- |
| 0 | `00000` | 17,478 | `0444e` |
| 1 | `0444e` | 11,902 | `072d4` |
| 2 | `072d4` | 16,634 | `0b3d6` |
| 3 | `0b3d6` | 10,910 | `0de7c` |
| 4 | `0de7c` | 8,736 | `100a4` |
| 5 | `100a4` | 4,758 | `11342` |
| 6 | `11342` | 2,748 | `11e06` |

All seven roots are ANShapes and all checks pass. Aggregate: 1,405 objects,
241 groups, 1,053 polygons, 3,060 shared points, 4,189 indices, 1,180 surface
words; vertex components `-760..1180`. There are 21 sequences (14 cyclic),
122 frames, 678 poses, 48 transitions, 77 defaults and 72 relations.
Root zero's animation object is at `0x2fb8`, body 5,262 bytes: eight
sequences, 24 transitions, 293 poses, 12 defaults, 12 relations. Its ten
TSShape cell slots are a different count from its eight AN sequences.

### Exceptions

**Confirmed (data), BASES_AN:** the following BSP transform-array fields
are outside `[-1,D-1]`, where `D` is that root's default-pose count.
Values and offsets are raw-file evidence, not inferred repairs.

| Root | BSP object | Offending field offsets | Values / D |
| --- | --- | --- | --- |
| `0` | `3dc` | `7a0,7a2,7a4,7a6,7a8` | 2 / 2 |
| `0` | `1b8` | `3da` | 3 / 2 |
| `0` | `2c` | `1b6` | 4 / 2 |
| `9906` | `9d82` | `ab42` | 3 / 3 |
| `9906` | `9932` | `9d7c` | 4 / 3 |
| `eb8c` | `ef68` | `f32c,f32e,f330,f332,f334` | 2 / 2 |
| `eb8c` | `ed44` | `ef66` | 3 / 2 |
| `eb8c` | `ebb8` | `ed42` | 4 / 2 |
| `1864a` | `18ac6` | `19886` | 3 / 3 |
| `1864a` | `18676` | `18ac0` | 4 / 3 |

The corresponding word bytes are `02 00`, `03 00` or `04 00`.
**Hypothesis:** a consumer provides additional transform state or never
visits these branches; neither explanation is established.

**Confirmed (data), APOCA transition:** root `0x1737a`, sequence object
`0x17e7c`, frame index 1 at `0x17e92` contains `64 00 00 00 02 00`:
tick 100, transition start 0, count 2, while its ANAnimList transition count
is zero. No safe interpretation that creates two serialized transitions is
supported by the exact body boundaries.

**Confirmed (data and static), texture bounds:** all offending words are
`0c 0c 00 00` (3084); the original renderer's dword indexing was described
above. Shared surface words account for nine references in five faces:

| Resource / root | Face object offsets | Surface field offsets | Invalid references |
| --- | --- | --- | ---: |
| TOMAHAWK / `0` | `2386,2398` | `2376,237e` | Four, both sides of both faces; MEDIUM has 36 images |
| TOMAHAWK / `0` | `250e,2520` | `24fe,2506` | Four, both sides of both faces; MEDIUM has 36 images |
| APOCA / `a532` | `cb2e` | `c992` | One front side; APOCATEX has 53 images |

**Hypothesis:** dormant cells, runtime replacement or an asset defect could
explain these references. Do not claim an original crash, truncate the ID
to eight bits, or silently rewrite the file without further evidence.

### Evidence and reproduction

Evidence is in
`<es2-data>\logs\codex-dts-20261006`: `probe.py`, `audit.py`,
`summary.py`, `verify.py`, `inventory.json`, `audit.json`, `summary.json`,
`verification.json`, `research-notes.md`, and the two full disassemblies
(`*-disassembly.txt`, written as `build/codex/scratch-dts/*-disasm.txt`).
The JSON files contain counts, ranges, offsets and hashes, not decoded
vertex, face or keyframe lists. Original payloads were not extracted to
files; no geometry exports or rendered/decoded images were produced.

The scripts expect their original location `build/codex/scratch-dts/`.
From the repository root, use the supplied Python executable with `-B`:

```powershell
$dtsPython = '<es2-dependencies>/python/python.exe'
& $dtsPython -B build/codex/scratch-dts/probe.py
& $dtsPython -B build/codex/scratch-dts/audit.py
& $dtsPython -B build/codex/scratch-dts/summary.py
& $dtsPython -B build/codex/scratch-dts/verify.py
```

The grammar above is checked against the original readers and bounded CD
parsing: the detail reader uses the existing child count, the shape reader
consumes 32 bytes per matrix, and the BSPGroup reader consumes ten bytes per
node.

## Independent check

An own VOLN reader and an own object walker written from the grammar table above
(`independent-check.py` and its output in the evidence directory) confirm: 55 DTS
members, 2,508,462 bytes; 479 roots (327 `TSShape`, 152 `ANShape`);
52,966 objects with exactly the per-tag counts of the tag table; every body
consumed exactly and every member ending at its last root. OUTLAW SHA-256 and
its seven root offsets match. The DBSIM registries at `0x4a63b0` (18 entries)
and `0x4a687c` (4) hold the listed tags with the original class-name strings.
The LOD table `0x49a344` is 75, 60, 45, 25, 18, 12, 6 and the bank names at
`0x49a360` are light, medium, heavy, enemy, apocatex, razortex, newhercs.
Instructions checked: `0x47a704-0x47a712` computes the object end as
`(position + L + 1) & ~1`; `0x4902a9` sizes the detail thresholds from the
child count at `+0x10` without a further count word. Not rechecked: materials,
lighting, animation semantics and the exception lists. No runtime or visual
check has been made.

## Open questions

- **Hypothesis / unverified:** exact runtime explanations for the three
  exception classes; additional base transform state and cell reachability.
- **Unverified:** full interpolation, Euler composition order, tick units,
  transition rules, ground movement and mapping of animation/cell slots to
  gait, damage, destruction and weapon state.
- **Unverified:** individual BASES_AN roots' building names and exact bank
  choices; weapon names per MECHWPNS/MECHWPN2 root and GL attachment mapping.
- **Unverified:** VSHELL preview reachability, nonempty serialized shape
  matrices and the six registered classes absent from CD DTS resources.
- **Unverified:** all surface flag meanings, exact clipping/shading/texture
  sampling parity and final displayed colours after palette remapping.
