# VSHELL mission preparation

Mission preparation, the SAV fields it reads, the results path with campaign
advancement and debrief selection, the shell options and VSHELL's RNG use.
Addresses refer to the reference VSHELL.EXE (SHA-256
`1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e`) at image
base `0x00400000`; DBSIM.EXE is `8561aa47…c4ad`. **Confirmed (static)** means
inspected original instructions (`llvm-objdump` from the dependency tree),
**confirmed (data)** a bounded read or byte comparison, **confirmed (in
vitro)** equality with the original code run in the mapped executable
([shell_invitro.md](../reference-game/shell_invitro.md)). None of these is a
live gameplay observation. **Hypothesis** marks unresolved behaviour; raw
fields without a confirmed label are exposed as raw controls.

Runtime tables (the RNG table, vehicle, item-list, armory-list and ARM-name
tables, the results tables) are read from the hash-checked executable at run
time and never embedded in source; names come from the CD catalogues. Inputs
on which the original faults, asserts or reads uninitialized memory are
rejected by our API as policy; no equivalence with undefined original
execution is claimed.

## Boundary

Inputs are the VFS, mission/campaign selection, 1,000 variable words, explicit
RNG state and player/team loadout. Outputs are the four buffers documented in
[mission.md](../formats/mission.md#the-four-launch-files), plus resulting RNG
state. Original files remain read-only. Preparation starts at `0x41cd61`,
reads MSN at `0x41818b`, and serializes at `0x41b278`.

## Conditions and random state

**Confirmed (static):** list 1 is processed in file order. Dependency -1 is
accepted; otherwise an already retained condition with the same ID must exist
(`0x417c34`, `66 83 fa ff` at `0x417c3f`). Type 0 compares a signed bank word
against a signed literal with selectors 281..286 (equal, unequal, less,
less-or-equal, greater, greater-or-equal; dispatch `0x418348`). Type 1 draws
only when its dependency passes. Type 2 searches previous type 1/3 records;
type 3 gives -99 (unconditional range acceptance), type 1 supplies its result,
missing gives -1 (`0x415ff4..0x41609f`). Type 3 tests dependency alone.

**Confirmed (static):** `0x465ce8` adds two uint16 cells modulo 65536, stores
and returns the cell at the second cursor, then advances both cursors modulo
56. The 56 words precede two cursor bytes at offsets 112/113. Bounded draw
`0x465d2c` masks the result with 32767 (`25 ff 7f 00 00`, `0x465d3d`) and
takes the signed division remainder with the signed-word bound (`f7 f9`,
`0x465d49`). Bound zero faults in the original and is rejected by our API.
Negative bounds yield nonnegative remainders. Initializer `0x465c90` sets
cursors 24/55 and copies 112 bytes from VA `0x47f850`; the table is read from
the user-supplied executable. Seeded initializer `0x465cbc` then discards
draws with signed-word decrement/test: for the public uint16 seed, 0..32768
discard that many draws; 32769..65535 discard none (`4e 66 85 f6 7d f3`,
`0x465cde`). The preparation stream is the state object `0x482409`. Start-up
uses `GetTickCount() & 127` (`0x407b42..0x407b51`, thunk `ff 25 bc e3 5d 00`
at `0x46baf0`, IAT `0x5de3bc` resolves to KERNEL32.dll/GetTickCount); see
[RNG use in the menus and the start-up seed](#rng-use-in-the-menus-and-the-start-up-seed).
SAV framing contains neither this RNG object nor the intervening draw history.

## Environment and shell mode

**Confirmed (static):** later accepted list-2 overlays preserve header word 0
when incoming value is 1 and other header words when incoming value is zero
(`0x41699d..0x416a98`). Thirty variable selectors preserve the previous value
when incoming value is -1 (`0x416aa5`, `66 83 f9 ff`). `0x417c7d` clears each
selected bank word (`0x417c9e`), after conditions have been evaluated.

**Confirmed (static):** mode 1 writes header words 5/6 as zero, word 7 from
`0x482b85` (the player rank), and word 0 from the current CAREER group ID
(`0x41cd97..0x41cdcf`). Mode 0 writes words 5/6/7 from unsigned bytes
`0x4825c1..0x4825c3`, word 8 as zero, word 9 from byte `0x4825c5`, and word 0
as one (`0x41cdd1..0x41ce12`). Other modes leave these values alone. The
script writer forces header word 2 to one.

The mode is the word `0x4826f0`, copied from prefs byte +0x2a at start-up:
mode 1 is the campaign (CAREER), mode 0 the single missions (practice and
instant action); see [Which controls write them](#which-controls-write-them).
The five mode-0 controls c1..c5 are prefs bytes +0x25..+0x29 (`0x4825c1..0x4825c5`):
ammunition, damage, mission difficulty, Herc type and time of day of the
practice screen ([vshell_ui.md §4.4](vshell_ui.md#44-practice-missions-0x44b310-frames-0x48dbfc-0x48dc00-0x48dc08)).

## Identity, copying and compact references

**Confirmed (static):** list 1 appends accepted conditions, including duplicate
IDs. List 2 overlays one environment. Lists 4 and 17 append all passing records;
their first word is a condition, not an identity. Start-up skips list 5; the
alternate debrief reader appends passing list-5 records (`0x41d72e..0x41d8e7`).
Lists 3 and 6..16 search the earlier retained records for the incoming ID;
ID -1 never matches (`0x416107..0x4165e6`, `66 83 fa ff`). Thus -1 records
remain available as templates but are omitted from script lists 6..16. A
replacement occupies the original slot. References use compact indices that
exclude these -1 slots; missing references become -1. Physical lookup is used
for template data and auxiliary values. Other negative IDs are not sentinels.

**Confirmed (static):** the word at +4 in lists 6,7,8,12,13,14,15 is a
*random selector*, not a direct source ID. `0x4165e7..0x416743` scans retained
type-3 conditions for matching +6, draws with bound +10, then finds the first
type-2 condition depending on that condition and containing the result in its
unsigned inclusive +6/+8 range. Copy helpers `0x416744..0x41699c` select the
first earlier same-family record whose +2 dependency equals that selected
condition's ID. Each use draws again. Missing templates cause unsafe/assertion
paths in the original; our API rejects them. A selected template may have ID -1.

| List | Duplicate action after dependency/remapping | Copy fields |
|---|---|---|
| 3 (8-byte auxiliary) | Replace all 8 bytes (`0x418854..0x418884`) | None |
| 6 (coordinates) | Replace all 22 (lookup `0x418e89`, replacement `0x418eb7`, length `6a 16` at `0x418e99`) | +10..21 |
| 7 (angles) | Replace all 10 (`0x418f00..0x4190c0`) | +8 word |
| 8 (routes) | Replace prefix and owned child sequence (`0x41910c..0x41942a`) | Count and children |
| 9 (regions) | Replace all 12 (`0x419430..0x419630`) | None |
| 10 (triggers) | Sparse merge `0x416b45..0x416d8b` | None |
| 11 (timers) | Sparse merge `0x416d8c..0x416e87` | None |
| 12 (actor A) | Sparse merge `0x416e88..0x41723a` | +8..73, +76..143 |
| 13 (actor B) | Sparse merge `0x41723b..0x41749d` | +8..53, +56..101 |
| 14 (actor C) | Sparse merge `0x41749e..0x41770b` | +8..13, +16..61 |
| 15 (orders) | Sparse merge `0x41770c..0x4178a9` | +8..21 |
| 16 (groups) | Sparse merge `0x4178aa..0x417c33` | None |

**Confirmed (static):** sparse merges preserve the earlier prefix and any
unlisted field. All offsets below are source-record byte offsets; ranges step
by two. Copy incoming words unless they equal the indicated sentinel:

| List | Sentinel -1 | Other sentinel fields |
|---|---|---|
| 10 | 6,8,10..24,28..76,80 | 78:0 |
| 11 | 6,10..28 | 8:0 |
| 12 | 48..72,76..114,138,140 | 8..46:0; 116..134:5; 136:2; 142:100 |
| 13 | 48..52,56..98 | 8..46:0; 100:100 |
| 14 | 8..12,16..58 | 60:100 |
| 15 | 8..20 | None |
| 16 | 46..114,116,118,122..162 | 6..44:0 |

The helpers compare individual words (including names), with `66 83 ... ff`,
`66 83 ... 05`, `66 83 ... 02`, and `66 83 ... 64` branches. In particular a
missing incoming reference remaps to -1 *before* merge and preserves an earlier
value. Copy branches reuse already remapped template fields, rather than
remapping the compact indices again.

**Confirmed (static):** list-6 relative flag +8 is tested after template copy.
Low words of +10 and +14 then identify two previously retained coordinates;
their three 32-bit components add modulo 2^32 (`0x418c62..0x418e76`;
the addition `0x418d1c..0x418e76`). No signed overflow or floating-point
conversion belongs in our implementation.

| Consumer | Fields remapped to compact indices |
|---|---|
| 4 / 5 | 70 / 30 string words from +2; last word resolves auxiliary +6 |
| 8 | Child coordinate IDs, before dependency test |
| 9 | +8 coordinate; +10 coordinate only when +6=0 |
| 10 | +10..24 regions; +68..76 strings; late +80 when +6=7/8/9/10 to A/B/C/group |
| 11 | +6 and +10..28 triggers |
| 12 | +70 coordinate, +72 angle, +138/+140 triggers |
| 13 | +48 coordinate, +50 angle, +96/+98 triggers |
| 14 | +10 coordinate, +12 angle, +56/+58 triggers |
| 15 | +12 coordinate, +14 route, +20 trigger; +18 subject selected by +16 (1 A,2 B,3 C,0 group late) |
| 16 | +50 coordinate,+52 angle,+54 route,+118 trigger; +56..94 actors by +46 (0 A,1 B,2 C); +96..114 orders |
| 17 | +10 coordinate,+12 route,+14 string; +8 subject by +6 (0 group,1 A,2 B,3 C) |

These calls occur in `0x418a10..0x41b22f`; compact lookup is mode 1, while
auxiliary lookup is mode 0. List-15 group and list-10 typed-subject references
are fixed only after list 16. Reference -1 remains -1.

## Language and serialization

**Confirmed (static):** localized records are condition-filtered at
`0x417eaf` against the retained conditions, not re-evaluated after bank clearing.
An accepted duplicate string ID replaces only its text pointer (`0x416ac9`);
its earlier slot and metadata survive. ID -1 never matches and is still
serialized. `0x418014` writes u32 body size, signed count, then each strlen+1
word and NUL-terminated text, and one additional zero byte **per string**
(`6a 01` at `0x418114`, inside loop ending `0x41812e`). Embedded text after
the first NUL is not emitted. This is distinct from the source language framing.

**Confirmed (static):** writer `0x41b278..0x41c861` strips authoring fields and
transposes ten interleaved variable/operator pairs in triggers, actors, groups
and objectives into two ten-word arrays. Exact output strides are in
[mission.md](../formats/mission.md#prepared-script-exact-framing). It emits
only the first accepted list-4 record: counts non--1 entries among its first ten
references, then emits that many *leading* references (does not gather sparse
entries). Empty list 4 emits zero. Undefined original cases such as a nonempty
source list 4 with no accepted record are rejected rather than emulated using
uninitialized memory.

## Variable and retained state

**Confirmed (static):** caller `0x40ef06`, invoked before the MSN reader, writes
bank[1/2] from CAREER indices in mode 1; mode 0 clears all 2,000 bank bytes
(`0x40eef1`, `68 d0 07 00 00`). All modes write bank[3] from `0x4826ea` and
bank[4..6] from three bound-12 draws (`0x40ef43..0x40ef7c`). No code writes
`0x4826ea` (BSS), so bank[3] is always 0 in VSHELL
([vshell_ui.md §17.6](vshell_ui.md#176-the-difficulty-word-0x4826ea)); our
preparation takes it as the `difficulty` input, 0 by default. Type-0
conditions read bank indices authored at +6. Environment reset selectors
clear up to 30 indices after condition selection. No other bank reads/writes
occur in this preparation boundary. The complete bank is serialized little
endian.

**Confirmed (static):** the thirty reset selectors at `0x48553e` are not reset
by the MSN reader; only the ten header words are. They start at zero in every
process (BSS) and carry between preparations of one process; our state carries
them explicitly. The preparation RNG is reset once per process
(`0x407b42..0x407b51`, low seven bits of GetTickCount after the static
initializer `0x407d4f`). Seed values do not reconstruct intervening draws,
so the full 114-byte state is an explicit input/output.

**Confirmed (data):** in all 61 well-framed CD MSN files, type-0 conditions
reference 175 distinct variable indices, from 0 to 828. Every authored reset
selector is -1. Consequently the initial retained selectors matter even when
the CD contains no explicit clear target. VA `0x48553e` lies in the zero-filled
part of PE DATA (VA `0x46c000`, raw size 86528, virtual size 1515520).
The zero initial state therefore clears word 0 after condition selection.

## Default state and mission equipment

**Confirmed (static).** Numeric fields are LE; signed comparisons are
explicitly identified. Only the projection reaching preparation/MEC is
modelled, not menu objects or allocator bookkeeping.

| Routine | Inputs, branches and state reaching the launch files |
| --- | --- |
| `0x410b11(hangar,1)` / `0x410aaf` | Clear eight bay pointers and three count/selection words. Only argument 1 opens GAM/HERCS.DAT (`0x410b41`). Its signed count is followed by signed bay index and a DAT loadout: vehicle i16, two raw words, occupied i16, then occupied `(slot i16, item i16, two words)` records. `0x41149d` obtains capacity through vehicle-ID table `0x46f868` and capacity table `0x46f87a` (nine words each), rather than the occupied count. Slots are indexed, not packed. |
| `0x410055` | Localized ESNAMES.BIN supplies names. Three roster groups contain twelve 59-byte records each at `0x4833e0`. For each group, `0x40ff80` independently permutes four identities and three variants by drawing an empty ordinal and placing the next value there; then twelve bound-100 draws select the first strictly greater signed threshold from four words at `0x46f72c` (fallback index zero). The companion table at `0x46f734` supplies another pilot word. Runtime +0/+2 are identity/variant words, +4 is a 30-byte name, +0x25 rank and +0x29 companion word. Selections `0x483c2c/32` begin zero. |
| `0x41072b` | Clears team capacity/count; creates player with identity zero, variant from one bound-11 draw, supplied name and supplied rank, companion zero. Player bay and order become zero and active byte one; count becomes one. Selects roster entry zero in each group through `0x410173`, advancing each next-selection index to one. Names do not reach MEC; variant and rank do. The roster and player draws come from the preparation stream `0x482409` of the new career `0x40e8a5` ([launcher.md](launcher.md#new-career-0x40e8a5)); our default SAV starts them from a seed-zero stream by convention. |
| `0x4125e8` / `0x412877` | Localized WEAPONS.BIN supplies labels. GAM/WEAPONS.DAT has signed category count; each category has signed ID, length-prefixed name, raw cost word, availability u8, and final word. `0x41237b` stores availability at category +0x16; category stride is 29. Following item records are placed into per-ID inventory lists; five-entry bank clears. MEC receives exactly word 33 and availability bytes in ID order (`0x41289d`), not inventory quantities, labels or costs. |
| `0x410923` | Authored capacity starts at one and scans the first retained group at +0x38, stopping at the first -1 from slot one (`0x41cea6..0x41cec3`). For orders 1,2,3, find the first selected pilot with that order. Activate iff order is signed-less than capacity and pilot bay != -1, else deactivate. Active changes update count. Missing orders do nothing. |
| `0x41cbb1` | Mode zero only. If signed preset index `0x479c54` >3 AND flag `0x473784` ==0, c4 indexes the nine filename pointers at `0x47083c` and the DAT loadout reader runs. Otherwise group actor reference +0x38 is resolved from compact to physical list-12 index by `0x41c862`. Actor vehicle is i16 +0x30; items are ten words at +0x32; secondary words at +0x74. Capacity follows the executable vehicle table. Only nonzero item words construct objects. Conditions initialize to 100. Result occupies bay zero and selects player bay zero. |
| `0x41cf08..0x41d038` | Mode-zero team repeats the actor construction for consecutive first-group references starting +0x3a. `0x4086b7` allocates; `0x41139e` initializes conditions; `0x411602` selects vehicle/capacity; `0x411bea` inserts items; `0x41118c` inserts bay i+1. `0x40ec8f(i,i+1)` assigns selected pilot i's bay and reevaluates order i BEFORE `0x4108e2` assigns order i+1. This order is preserved; its effect on active counts must be compared, not corrected by intent. |
| `0x40f68c` | Calls `0x40ef83` for the full variable bank. MEC writes zero, active count, player variant/rank and bay, then orders 1..<capacity: first selected matching pilot, only if active. `0x410cdb` emits vehicle, capacity, indexed item words (empty=0), secondary (empty=5), zero, 33 condition words. An absent bay writes no loadout bytes, yielding malformed output; safe API rejects this state. Availability suffix follows. |

`0x41cc3d..0x41cd26` holds `66 83 f8 03; 7e 3e` (signed index <=3 skips the
preset), then a dword test of `0x473784` and `75 34` (nonzero skips). c4 is
loaded as an unsigned byte; the actor loop limit is capacity +0x4c. In
`0x4125e8..0x4126b0` the category count and IDs are signed reads, the
category stride is 29 bytes, `0x41237b` receives the availability byte, the
final word is stored separately at `0x484086`, and the following count
controls the inventory object lists. `0x412877..0x4128ce` emits 33 ID-ordered
flags, not list quantities.

Language changes labels/text but not the numeric default rules. c1,c2,c3,c5
are unsigned header controls; c4 matters only in the preset branch above.
The preset flag is a dword; its exposed zero/nonzero control is widened from a
byte. The index is an i16. Pilot bay/order are i16 at runtime +0x22/+0x27;
active is u8 +0x24, rank and companion are raw words +0x25/+0x29. Localized BIN
catalogues have u32 count/payload length, u16 string offsets, and NUL-terminated
strings, bounded by the member extent. DAT slot +0x06 is the secondary word;
the other raw slot word does not reach MEC. The existing DAT and SAV readers
cover the disk framing.

### Equipment quirks of the original

**Confirmed (static, in vitro):** `0x41ccd7..0x41ccda` tests item words for
zero, not positivity; authored -1 survives in TRAIN3's equipment. The first
fresh mode-zero team construction with capacity four has three active
actors: setting the next pilot's bay reevaluates the preceding order before
setting the new order (`0x40ec8f`, `0x4108e2`). A repeated preparation can
activate the fourth. Synthetic tests also cover missing/duplicate orders and
absent bays.

**Confirmed (in vitro):** mode 1 requires an accepted pretext. TRAIN1/TRAIN2
at zero bank/seed zero assert at return site `0x41ce61` (condition zero,
`src/msn_gen.cpp:2005`), in every language and all three state sources. They
do not yield four files. TRAIN3 can reach the same assertion at seed 127,
including retained runs. Our bounded API returns an error without changing
caller state.

### Implementation

`shell/src/equipment.c` implements this projection with owned values,
explicit LE reads and the DAT/SAV readers. The same MEC writer handles CD
defaults, restored SAV state, frozen templates and explicit actor arrays. The
public state retains roster, selected pilots, bays and flags between
preparations. Names, inventory item lists and menu allocations are outside
this projection.

## Preparation acceptance

### Captured preparations

**Confirmed (data):** C1_01 and DEMO compare byte for byte in memory, using CD
archives, saved state and our C17 writer. No original launch file is an input
to the generation step; the reference reads happen only after generation. The
DEMO variable reference is the SAV bank because no numbered `.var` companion
exists.

| Case / reference | Size | SHA-256 (our output equals reference) |
|---|---:|---|
| C1_01 DATA/script.dat | 12286 | `61efab9bedce8e1a45b484f6fd5157ba0e5d94629ad5881e7ac0720d24a135bb` |
| C1_01 DATA/MISSION.STR | 939 | `4c359b61fd35bbf9cd3d2cac21e79f39a23bd48ec167ff6049e19d3de14584f9` |
| C1_01 DATA/mission.var | 2000 | `20c8eb568dc09fb2f9f3a64a231264b70d31dd1c225709b146b829993d74dbfc` |
| C1_01 DATA/player.mec | 215 | `f669d1250486b6e1e3ff9ed18370f0bc14cdc60793fed8caaf4b49cb78174984` |
| DEMO SAV/script11.dat | 4514 | `dd28822315105ec3054aa54609c13cada1aaef5871f475163b98ee439641cff9` |
| DEMO SAV/missn11.str | 882 | `12a693e44050085648cd7f256e2ad48b5bff67193bcb2fcfc57e4b08283ba6fb` |
| DEMO GAME_T.SAV bank | 2000 | `13cedfdea94b642bca3976ee8798f5bb038d19fda9e706302c62912a91fa62c4` |
| DEMO SAV/player11.mec | 151 | `ec5b87a6cdaa1e0d9bf87510c9d99a8db114eac78874b213fc064ce02d30ce68` |

**Confirmed (data):** GAME_R.SAV SHA-256
`ae19c8e622adf64173d895b31d4ebcacbadbf7667a57fa9d793deacd97b9b2cb`
has bank offset 2731, reader end 4753 and ten opaque trailing bytes. Its
CAREER position is (1,0), resolving to C1_01. GAME_T.SAV SHA-256
`1e0cfc3705127b504fafd3a60b92246e4de916113ea24bd0e82ddcec8bf8de0f`
has bank offset 2300 and exact end 4322; position (0,8) resolves to DEMO.

**Confirmed (static and data):** SAV pilot suffix +0 supplies loadout slot,
+2 active byte, +3 the second MEC pilot word, and +5 team order. MEC's first
pilot word is SAV pilot prefix +2. Selected pilots use the first three-word
selection array, one index into each twelve-record roster group
(`0x410813..0x41083e`). The writer scans order 1..<team capacity, selecting
the first matching pilot and testing active (`0x4108fa..0x410920`,
`0x40f793..0x40f8a4`). Equipment comes from SAV indexed loadouts: capacity
+74, occupied count +76, then `(slot, five item words)`; absent slots emit
item 0/secondary 5. Secondary is saved item +8; the 66 condition bytes are
saved loadout +4..69. `0x410cdb..0x410e27` gives the MEC layout, and
`0x412877..0x4128bf` emits the 33 saved inventory flags. Both reconstructed
MEC buffers match exactly without a MEC template.

Supplied controls: mode 1 for C1_01 and mode 0 for DEMO, difficulty zero,
fresh reset selectors zero, German text. C1_01 header word 7 comes from the
saved player suffix; DEMO's four mode-0 controls `(1,1,0,0)` come from
captured script header offsets 10,12,14,18, not a decoded SAV field.

RNG witnesses are initial-table states after 71 draws (C1_01, "GAME_R + 71
draws") and 103 draws (DEMO). Searching offsets 0..32767 for the saved
bank[4..6] triple gave 26 and 14 candidates respectively. Only the first
C1_01 candidate reproduced its script; all fourteen DEMO candidates reproduced
its script. These are sufficient inputs, **not recovered historical start-up
seeds**. Input RNG hashes are respectively
`1681332a8128a761cdba0650c9e7ee31edfd65500446355f9004b0da3862c336` and
`c20e29bbc94b4f20d384198c2a2814f9e69925e467ce871dd4859a0636cfcbb0`.

Two rules were derived twice from independent instruction dumps: the RNG
signed remainder and signed-word seed loop (`0x465cbc..0x465d4f`) and the
coordinate copy before relative addition (`0x418c62..0x418e76`). The STR
writer's extra zero is inside the per-string loop, not once at EOF; the
synthetic framing test covers that distinction. `signature_audit.py` checks
the byte anchors quoted in the sections above.

### Differential sweep

**Confirmed (in vitro):** all 61 CD-default missions match all four files plus
returned RNG/reset selectors with mode zero for group zero and mode one
otherwise, seed zero, zero bank, German. The baseline is 61 missions ×
3 languages × 2 modes × 3 sources (CD, GAME_R, GAME_T): 1,080 exact matches and
18 matched original assertions, zero mismatches. Fixed mode zero matches 61/61
per language/source; fixed mode one matches 59/61, with the two assertions of
[Equipment quirks of the original](#equipment-quirks-of-the-original).
C1_01/GAME_R and DEMO/GAME_T match the captured preparations. Direct-original
seed initialization (1,188 cases) and the C1_01/DEMO cross-product (18,972
cases) match all six results. Combined reports contain 94,557 cases: 94,521
exact, 36 matched original assertions, zero mismatches (repeated baseline
combinations included). Reports contain per-mission, per-file hashes, never
original output buffers. Matrix and limits:
[the harness report](../reference-game/shell_invitro.md#differential-sweep).

A corpus run prepares all 61 CAREER entries with independent seed-0 states,
zero bank/reset selectors, mode 1 and the CLI's diagnostic loadout
(`campaign-report.json`: success flag, four sizes/hashes and returned-state
hashes per mission).

### Limits of the preparation contract

- UI labels for the raw rank and header fields remain hypotheses.
- Start-up GetTickCount and intervening draws cannot be recovered from SAVs;
  repeated preparations exchange the full RNG/reset state.
- Conflicting duplicate archive precedence, arbitrary SAVs, malformed-original
  behaviour, complete cross-products and longer retained sequences are not
  covered. Comparison-boundary banks do not prove every authored branch ran:
  reserved words are overwritten and parent conditions can suppress a record.
  No claim of exhaustive shell equivalence follows from the sweep.

## SAV reader and writer

The `formats:sav` library (`formats/src/sav.c`) parses the framing of
[mission.md](../formats/mission.md#sav-field-values), exposes borrowed
inventory/roster/loadout/variable views, edits every section the preparation
reads and composes complete images. Original opaque bytes and GAME_R's
ten-byte trailer survive a round trip. `sav_test` covers the framing with
synthetic bytes; `sav_real_test` round-trips GAME_0 (4753 bytes), GAME_1
(4673), GAME_R (4763, reader end 4753) and GAME_T (4322) in memory without
writing a SAV to disk.

## Loadout SAV contract

**Confirmed (static, in vitro):** the mapped original restores each SAV with
its own readers, writes it again with its own writers (`--emit-save`) and
prepares it. The SAV record layout and writer addresses are in
[mission.md](../formats/mission.md#sav-field-values). Implementation:
`formats/src/sav.c` (framing edits), `shell/src/save.c` (field values),
`shell/tools/es2loadout.py` (front end).

### Fields a mode-1 preparation reads

**Confirmed (static and in vitro):** only these SAV fields reach the four
launch files in mode 1 (CAREER groups 1..5). The loadout sweep varies most of
the others (three real bases, CD defaults, kept or zeroed banks).

| SAV field | Use in the preparation |
| --- | --- |
| Selection words 0/1 | CAREER group/position, hence the mission; bank[1]/bank[2] (`0x40ef06`). |
| Variable bank | Condition bank; words 1..6 are then overwritten as in [Variable and retained state](#variable-and-retained-state). |
| Player prefix +2 (variant), suffix +3 (rank) | MEC record 0 pilot words 0/1; the rank is also script header word 7 (`0x41cd97..0x41cdcf`). |
| Player suffix +0 (bay) | Hangar index of MEC record 0. |
| Roster selection words 0..2 | The one pilot of each roster group that can serve (`0x410813..0x41083e`). |
| Selected pilot suffix +0 bay, +2 active, +5 order; prefix +2, suffix +3 | Team orders 1..3, activation, MEC pilot words. |
| Team word 1 (active count) | MEC record count. `0x410923` adjusts it only when an order changes its active state, so it must equal 1 + the active ordered pilots. Word 0 is overwritten with the authored capacity. |
| Hangar records | Vehicle: MEC vehicle; capacity word (+74, restored as saved, not recomputed): MEC K; 33 condition words; item ID and secondary word +8: MEC item/secondary; empty slot: 0/5. |
| 33 inventory flags | MEC availability suffix. |

Activation (mode 1, `0x410923`): the capacity is the number of actor references
in the first retained group up to the first -1 (at most four). For orders 1..3
the first selected pilot with that order is active iff `order < capacity` and
its bay is not -1. The effective team is therefore `min(team, capacity - 1)`.
Measured by preparing the missions: C1_01 admits one member,
C3_02/C3_09/C3_10 three, C1_04, C1_05, C1_06, C4_08, C5_02 and C5_07 none.

### Hangar rules of the VSHELL UI

**Confirmed (static, then in vitro;
[vshell_ui.md §10](vshell_ui.md#10-service-bay-and-weapons)):** these rules
decide which weapons the hangar lets the player mount. `es2loadout.py`
enforces them unless a loadout asks for `"ui_rules": "report"`.

- The weapon list offers the 27 IDs stored at `0x476a4c`: 1..18, 22..25,
  29..32 and 0 (empty). IDs 19, 20, 21, 26, 27 and 28 never appear in it.
- The list refresh `0x44025b..0x4403eb` disables an entry whose category
  availability byte (+0x16) is zero and disables an entry that `0x41501f`
  refuses for the selected slot. `0x41501f` requires the availability byte
  and calls `0x4142f0(table, item, slot + 2)` at `0x415069`: the vehicle's
  ARM table group whose key is the item ID must hold a record whose first
  word is the slot plus two (in vitro: 2,277 answers for every vehicle, slot
  and item). The ARM loader `0x41508f` (file names through the pointer table
  at `0x47006c`) stores one count and one record pointer per group key 0..32
  for each vehicle (stride `0xc6` from `0x484702`). Group 0 lists every slot
  of the vehicle.
- The click handler `0x43fdac` mounts when the item's inventory list is not
  empty (`0x43fe66`, category +0x17), when the slot already holds that item,
  or when the slot item's word +8 (5 for an empty slot) equals the clicked
  ID, which can empty a slot for an ID whose list is empty.
- Mounting (`0x411b10`) returns the slot's old item to its inventory list,
  takes the head of the new item's list, copies item +4 into slot condition
  word `23 + slot`, increments the occupied count and sets item +8 to 1 for
  IDs 13, 14 and 15 and to 5 for every other ID (`0x411bb6..0x411bde`). A
  VSHELL-made save therefore has secondary 1 on the 6x/8x/10x missile racks and
  5 elsewhere, while unmounted inventory items keep their WEAPONS.DAT variant
  (0 for the racks).
- The secondary word is the missile guidance mode (SARH 0, ARH 1, ARM 2,
  EO 3; 5 none), which the WEAPONS guidance buttons store through
  `0x43e346`.
- The UI writes no availability flag: `es2loadout`'s flag edits for mounted
  IDs are a loadout-builder policy outside the UI path; the own service bay
  never writes flags.

Facts derived from the CD data (ARM files read at run time by the front end):
plasma cannon 25 fits Ogre slots 0/2/4, Razor 4/5, Colossus 7/8,
Apocalypse 5/6, Samson 7, Tomahawk 4, Raptor II 1/3, Maverick 0/2 and
Outlaw 2; the Razor missile rack 16 fits only the Razor (slots 2..5).

### CD-default career SAV

**Confirmed (static, then in vitro 192/192):** `es2_shell_default_save`
builds the SAV the original writes after its new-career loaders:

- inventory: availability bytes from WEAPONS.DAT; the 39 item entries become
  item objects (ID, list position, 100, DAT condition, DAT variant), each
  prepended to its ID's list, so a list holds its DAT entries in reverse order;
- five-entry bank 5, then (0,0) .. (4,0) (`0x412761`);
- selection: the requested CAREER position, then 148 zero bytes;
- roster: the roster of [Default state and mission equipment](#default-state-and-mission-equipment)
  from a seed-zero stream; name ESNAMES[variant] (`0x4100ff`); suffix from the
  reset `0x41033b` with rank and companion;
- roster selection 0,0,0 / 1,1,1; team capacity 0, count 1;
- player: identity 0, variant from the same stream, supplied name and rank,
  bay 0, active, order 0, companion 0, +0x2b 100;
- hangar: HERCS.DAT records in file order with DAT words +2/+4 at hangar
  +70/+72, conditions 100, items (ID, list position, 100, DAT condition,
  DAT variant) in slot order;
- nine-word bank: HERC_INF word +14 of each record; tally, bank and final
  fields zero.

The player name is an input (the real game asks for it); the harness takes the
same name through `--player-name`/`--player-rank`. The seed-zero roster stream
is the harness convention; VSHELL's new career `0x40e8a5` draws the roster,
the player variant and the starting salvage from the process stream
([launcher.md](launcher.md#new-career-0x40e8a5), `es2_shell_new_game`).

### Applying a loadout (`es2_shell_plan_save`)

Policy of our builder; every field is written the way VSHELL's own code writes
it:

- The hangar is replaced: player Herc in bay 0, team member k in bay k
  (k = 1..3). Each is a ready Herc (100/0) with vehicle table position,
  capacity from the executable table, 33 conditions of 100 and items
  (ID, list position, 100, 100, secondary). The secondary follows the mount
  rule unless the loadout gives one explicitly. Slots are written ascending.
- Every roster pilot is unassigned (bay -1, inactive, order -1); member k gets
  bay k, order k and active, and becomes the selected pilot of its roster
  group. Next indices, names, variants and ranks are kept.
- Player bay 0, active, order 0; name, variant and rank kept. Team words
  1 + n and 1 + n.
- Availability: base flags with every mounted ID set to 1, or 33 explicit bytes.
- CAREER position set; variable bank kept or zeroed; the opaque tail after the
  reader end is dropped (the original writer never emits it).
- Untouched: inventory lists, five-entry bank, briefing selection, nine-word
  bank, tally, final fields.

**Confirmed (in vitro):** for every generated SAV of the loadout sweep (6,048)
and of the five published suites, the original readers followed by the
original writers return our SAV byte for byte, and both preparations agree on
all four files and the returned RNG/reset state
([shell_invitro.md](../reference-game/shell_invitro.md#sav-emission-and-loadout-sweep)).

### Loadout limits

- The UI operations that change these fields (CREW's bay/order binding,
  BUILD, ARMORY, REPAIR, scrap) are in [vshell_ui.md §12](vshell_ui.md#12-service-bay-editors-repair-build-herc-scrap)
  and [§17](vshell_ui.md#17-service-bay-editors-crew-armory-weapons-scrap);
  the builder's k/k binding is one of the combinations CREW reaches.
  Inventory counts are not derived from the UI: mounted items are new
  objects and the base inventory is kept.
- Labels: item words +4/+6 (two condition words), hangar +70/+72, nine-word
  bank ("unlocked").
- DBSIM: preparation parity says nothing about the simulator. Item 26 in a MEC
  record stopped DBSIM before tick 0 in the owner's probes
  (`c3_09-omicron-test`). Items 19..21, 27 and 28 are not mountable in the
  hangar and appear only in the explicitly unchecked suite. What the
  guidance word does in DBSIM is a hypothesis.
- Mode 0 (CAREER group 0) builds equipment from the mission, so a SAV loadout
  does not apply there; the front end refuses it.

## Results path

**Confirmed (static)** marks inspected instructions; everything else is a
hypothesis. The labels *repair*, *restock*, *promotion*, *salvage* and *money*
are **hypotheses**; the arithmetic is confirmed.

### Results path boundary

VSHELL is restarted by ES after DBSIM. Launch states 3/4 reach `0x4018a1`:
load save index 10 (`0x40eaaa`, GAME_R, or GAME_T in mode 0), then
`0x40f09f`. The shell mode word `0x4826f0` is copied from option byte
`0x4825c6` at start-up (`0x40e753`); it is not in the SAV.

Inputs: the restored SAV state; `DATA/mission.var` (2,000 bytes, read by
`0x40f011`); `DATA/results.dat`; mode; two option bytes that are not in the
SAV, `0x4825c8` (*repair option*) and `0x4825c9` (*weapons-building option*,
*restock* below) from [Shell options](#shell-options); the 114-byte RNG state
at `0x482409` (only the debrief reader draws); CD tables (`GAM/HERC_INF.DAT`,
`GAM/DAMAGE.DAT`, `GAM/WEAPONS.DAT`, `GAM/CAREER.DAT`, MSN and language
files); executable tables listed below. Outputs: the state as the SAV writers
would serialize it, the CAREER position, the advancement return value, final
word `0x4826f2`, the [debrief selection](#debrief-selection), the rewritten
`DATA/MISSION.STR`, the composed debrief text and the RNG state.

UI and disk side effects called directly by `0x40f09f`, never by the state
code: `0x40e933` (save to disk), `0x40f904` (statistics widgets), `0x44d0e7`,
`0x41f93a`, `0x41e8c0`, `0x41e98c`, `0x407b62`, `0x407c1b`, `0x402266`,
`0x413305`, `0x4077ee`. The static closure shows no state write below them
that reaches the SAV writers; they are outside the boundary.

### Runtime tables

From the executable (read at run time from the hash-checked file, never
embedded): six rows of four signed words at `0x46f9e4`, each a list of
condition-word indices ended by -1 (*part groups*); six thresholds at
`0x46fec4` and six levels at `0x46feb8` (*repair levels*); 33 expected values
at `0x46fb80` and 33 variable indices at `0x46fbc2` (item unlocks); 29 item IDs
at `0x46fa20` for variables 21..49 (only 21..42 hold item IDs; the words for
43..49 are unrelated data); the 30-entry item index list at `0x46f9a8`.

From the CD, as VSHELL's loaders compute them:

- Category *c* (ID read from WEAPONS.DAT, `0x4125e8`): cost
  `(u16)(raw * 1000)` (`0x4123b6..0x4123c0`) and a priority word stored at
  `0x484086 + 2c`. Availability bytes and item lists come from the SAV.
- `0x413c4c` (`gam\damage.dat`): with `f(a,b) = (int32)((int64)a*b >> 10)`
  (`0x451114`, `f7 6d 0c` / `0f ac d0 0a`), for HERC_INF record *r* and its
  word +8 *v*: `A[r][i] = (i16)f(common, f(c6[i], v*1000))` for *i* < 6 and
  `A[r][13+i] = (i16)f(common, f(c9[i], v*1000))` for *i* < 9; all other
  words of the 33-word row are zero. Item values
  `V[i] = (i16)f(second, item[i])`, *i* < 33.
- HERC_INF record *r*: +0 vehicle ID, +8 *v*, +12 build divisor, +14 the
  saved nine-word bank entry.

### Condition helpers

A loadout's 33 condition words *w* (SAV hangar +4..+69) are addressed in three
families by `0x41232a`: family 0 index *i* is the signed average of the words
listed in part group *i* (16-bit wrapping sum, signed division by the list
length, `0x41224d`); family 1 index *i* is *w*[13+*i*]; family 2 index *i* is
*w*[23+*i*] (slot *i*). `0x4121f8`, the *overall* value, is the 16-bit
wrapping sum of *w*[0..12], *w*[13..21] and *w*[23..22+capacity], signed and
divided by capacity+22.

### Sequence of `0x40f09f`

**Confirmed (static).**

1. Bank := `mission.var` (`0x40f06c..0x40f07d`).
2. results.dat: outcome word → `0x482bcd`, tally dword → `0x482bcf`; the
   persistent total `0x482bd8` += tally (32-bit wrap); bank word 0 := outcome
   (`0x40f12d..0x40f146`).
3. Signed count *P*; *P* pairs (a,b) are always read, and only in mode 1
   `0x4128c1` prepends item (a, index(a), b, 100, 5) to category a.
4. Player: bay *k* = player +0x22. If hangar[*k*] exists, `0x411d44` reads 66
   bytes over its condition words, then for slot pointers 0..9 removes every
   item whose *w*[23+slot] is 0: free, clear, *w*[23+slot] := 100, occupied
   count − 1. Player +0x2b := *w*[22] (or 100 without a loadout).
   `0x410632` reads three words into +0x2d, +0x31, +0x2f (that order), adds
   them to +0x33, +0x37, +0x35 and increments +0x39 (16-bit wrap).
   Total += `0x4112a0(k)`: overall value > 29 (signed) sets *w*[22] := 100 and
   yields 0. Otherwise the Herc is lost: `0x414174` value, then `0x411db9`
   returns items with *w*[23+slot] >= 80 to their category (prepend; slots
   ascending) and frees the rest (each visited slot reset to 100), the loadout
   is destroyed, hangar count − 1, hangar +0x24 + 1; the value is the yield.
   Without a loadout `0x4112a0` writes through a null pointer.
5. Orders 1..count−1 (team word 1, `0x482b5e`): the first selected pilot with
   that order (`0x4108fa`, groups 0..2); skipped if none or inactive; then
   exactly step 4 for that pilot.
6. `0x4109de`: lost := 0. Player +0x2b = 0 → lost + 1, else `0x41068a(player,1)`.
   Orders 1..capacity−1 (team word 0): the selected active pilot with +0x2b = 0
   counts as lost and is replaced through `0x410173` (selected := next,
   next := (next+1) mod 12, the new pilot reset by `0x41033b`, team count − 1;
   the old record keeps its fields); others get `0x41068a(pilot,0)`.
   `0x482bd3` and bank word 9 := lost.
7. Player +0x2b = 0: `0x4826f2` := 3, UI `0x44d0e7(3)`, end (`0x40f475..0x40f4a9`).
8. Bank word 8 := hangar +0x24 (Hercs lost this run; zero after restore,
   `0x410aaf`). Mode ≠ 1: UI, end.
9. Mode 1, in this order: `0x412a4c` (each nonzero entry *id* of the
   five-entry bank prepends item (*id*, index, 100, 100, 1 for 13..16 else 5)
   and costs cost[*id*]; total −= sum; the bank is kept); `0x40edbc` with budget
   `total / count` (signed 32-bit; count 0 faults); `0x41104f`; `0x411ee9`;
   `0x412ce2`; `0x412a37`; UI `0x40f904`; `0x4133eb(outcome)`.

### Helpers of the sequence

`0x41068a(p, player)`: only if active. Divisors (*m*,*n*) are (10,15) for order
≠ 0 and (20,10) for order 0. If rank +0x25 < 3, the flag is 0 and
(+0x33 + +0x35) mod *m* = 0 (C remainder of sign-extended words), rank + 1.
If +0x29 < 3 and +0x39 mod *n* = 0, +0x29 + 1.

`0x40edbc` (option `0x4825c8` = *R*): if *R* = 0 the player's loadout, and
unless *R* = 2 the active selected pilots of orders 1..capacity−1, are passed
to `0x41194c` with the same budget; total −= each result. `0x41194c`: level
index *j* = 0, cost := `0x41401b(L, level[j])`; while cost > budget (unsigned)
and *j* < thr(overall) (`0x413e40`: first *i* with value >= threshold[*i*],
else 0): *j* + 1, recompute. If cost < budget (unsigned, strict), `0x4119d3`
sets *w*[0..12] and *w*[13..21] to level[*j*], each slot below capacity to
level[*j*] when it holds a nonzero item ID, else 100, and the cost is returned;
otherwise 0. `0x41401b(L, x)` with table row *t* = hangar +2: sum of
`g(A[t][i], family-0 i)` (*i* < 6), `g(A[t][13+i], w[13+i])` (*i* < 9) and,
for slots below capacity with nonzero item ID and nonzero *w*[23+s],
`g(V[id], w[23+s])`, where `g(weight, c) = c >= x ? 0 :
(level[thr(x)] − c) * weight / 100` (`0x413f4e`, signed 32-bit).

`0x414174(L)` (lost Herc value), vehicle *id* = hangar +0: if +70 (build
progress) < 100, `((100 − progress) / 100) * HERC_INF[id].v * 1000`;
otherwise the sum over *i* < 6 of family-0 *i* · A[*id*][*i*] / 100, over
*i* < 9 of *w*[13+*i*] · A[*id*][13+*i*] / 100, and over slots below
capacity with *w*[23+s] < 80 of *w*[23+s] · V[item or 0] / 100. Note the two
row indices: `0x41401b` uses hangar +2, `0x414174` uses hangar +0.

`0x41104f`: every present bay with progress ≠ 100 (`0x411088`) decrements
+72 and sets progress := (div − countdown) · 100 / div, with *div* =
HERC_INF[hangar +2] +12 (`0x4116aa`; zero faults).

`0x411ee9`: for HERC_INF records 0..8 whose nine-bank word is 0, the vehicle
ID selects variable 60 (ID 1), 61 (ID 7), 62 (ID 6) or 63 (ID 8); others are
skipped. The expected value starts at 2 and becomes 1 when ID 8 is reached
and stays 1 for later records (`b9 01 00 00 00` at `0x411f37`, never reset).
Equal → nine-bank word := 1; the variable is cleared either way.

`0x412ce2`: for categories 0..32 whose unlock variable is not -1 and whose
availability is 0: variable = expected value → availability := 1; the
variable is cleared either way. Then for variables 21..49, *n* = bank value
(signed), *n* items (ID from the table, index, 100, 100, 5) are prepended;
these variables are not cleared. The count of added items is returned.

`0x412a37` (option `0x4825c9` = *S*): *S* = 0 → `0x412965`: the five-entry
bank becomes 5 free entries of 0, spent := 0; for *j* = 0..32 while free
entries remain: category *c* = first category whose priority word is ≠ 99 and
= *j*, else -1; if availability[*c*], list count[*c*] < 2 (signed) and
cost[*c*] + spent < total (unsigned): spent += cost, *c* goes into the first
zero entry and the free count − 1. *S* ≠ 0 → `0x4129de`: if the sum of costs
of nonzero entries exceeds total (unsigned), entries 0..4 are cleared in order
(free count + 1 each) until the remaining sum <= total. With the CD data,
priorities 29..32 have no category; *c* = -1 then reads bytes of HERC_INF
record 8 as a category (availability = high byte of its word +8, zero on the
CD). Our API rejects the case where that byte is nonzero.

### Advancement `0x4133eb(outcome)`

**Confirmed (static).**

1. [Debrief selection](#debrief-selection) for the current CAREER entry
   (`0x41d8e7`).
2. Position + 1 (`0x41342d`, `66 ff 05 5a fc 46 00`, before success is
   tested); if group count <= position (signed, `0x41344d..0x413450`):
   position 0, group + 1.
3. Outcome ≠ 0 and group >= group count → return 1 (no hooks, no text).
4. (position = 0 or group > 4) and outcome = 0 → return 0.
5. Hooks on the **new** position (the predicates of
   [mission.md](../formats/mission.md#hardcoded-progression-around-the-data-order)):
   (1,3) → player bay := 4 (`0x40ec80`); (1,6) → the
   [removal hook](#removal-hook-0x40ed85) `0x40ed85(8)`.
6. `0x4139f6` composes the debrief text; return 2.

Caller: 1 → `0x4826f2` := 1 and save index 10; 0 → `0x4826f2` := 0, UI
`0x44d0e7(0)`; 2 → UI state 4 and `0x413305` (window messages only).

### Removal hook `0x40ed85`

**Confirmed (static, in vitro).**

- `0x40ed85(8)` calls `0x4111e2`, which searches the eight bays by vehicle
  ID. A match first calls `0x411db9` (`e8 af 0b 00 00` at `0x411205`),
  destroys the loadout, clears its pointer and decrements the hangar's
  record count +0x20 (`66 ff 4e 20` at `0x41123a`, `0x411237..0x41123e`). Its
  return is the removed bay index.
- `0x411db9` visits occupied equipment slots in increasing order. Type-2
  condition lookup `0x41235f` reads condition word `23 + slot`; signed compare
  `66 83 f8 50` at `0x411de2` retains values >=80. Lower-condition items are
  freed; retained items go to their item-ID inventory bucket (`0x411dff..0x411e0c`).
  `0x412521` prepends a node to that bucket's linked list and increments its
  count (`89 73 19 66 ff 43 17` at `0x412593`). Each visited loadout slot is
  cleared and its condition reset to 100. Inventory order therefore matters.
- A no-match search does not explicitly return -1: `0x411242..0x41124d`
  preserves the last `0x411164` lookup result in AX, i.e. the vehicle ID of
  bay 7, or -1 if empty (`66 8b 00` at `0x411182`, `83 c8 ff` at
  `0x411187`).
- The caller passes this word into the pilot lookup `0x410844` at `0x40eda6`:
  the player if its bay equals the word, else the first selected pilot with
  that bay. Only its assignment word +0x22 becomes -1 (`0x40edb2`); the
  active byte and the team count are not changed. -1 matches nobody.

### Safe API policy

Not emulation: the portable code rejects, before producing output, inputs on
which the original faults, asserts or reads outside an object: a consumed
pilot whose bay has no loadout (step 4), bay outside 0..7, item IDs outside
0..32, count 0 for the budget, zero build divisor, a reachable category -1
with nonzero availability, variables 43..49 with positive counts, more than
32,767 items in a list, a missing debrief record, string references outside
the table, and a `results.dat` or `mission.var` shorter than the bytes the
path reads (`mission.var` must have exactly 2,000 bytes). Trailing results
bytes are ignored, as by the original.

## Debrief selection

**Confirmed (static).** `0x41d8e7(name)` calls the alternate MSN reader
`0x41d072` for the mission just played, before the position changes:

- List 1 (14-byte conditions) is evaluated as in preparation: types 0..3, type
  1 draws from the RNG at `0x482409` with bound +6, failing records are
  overwritten (only passing ones are retained).
- List 2 is skipped (`0x52` bytes per record), the language file is read by
  `0x417cb0` exactly as in preparation, list 3 (8-byte auxiliary records)
  is merged as in preparation, list 4 is skipped (`0x90` per record).
- List 5 (64-byte records): a record passes when its +0 condition is retained.
  Its 30 string references (+2..+61) are remapped to compact string indices
  (`0x4160a0`, missing → -1). Its +62 auxiliary reference is looked up with
  `0x416107` in **compact** mode (third argument 1) and the result is used as
  a **physical** index into list 3; the record word +6 found there replaces
  +62. A failing record is overwritten by the next one.
- `0x418014` rewrites `DATA/MISSION.STR` from the retained strings.

`0x41d8e7` then takes the record in slot 0: the first passing record, or, when
none passes, the last record read, unremapped. An empty list 5 leaves the
pointer null and asserts (`0x41d920`). `0x4139aa` copies the 30 references to
`0x484160`, counts those ≠ -1 into `0x48415e` and stores +62 in `0x48419e`
(the debrief movie word, [vshell_ui.md §14.3](vshell_ui.md#143-page-4-in-0x4448734-and-the-debrief-movie)).
None of these three is in the SAV.

`0x4139f6` (only for return 2) reloads `DATA/MISSION.STR`, and the debrief
text is the concatenation of the strings of every reference ≠ -1 in reference
order (`strcpy` for reference 0, `strcat` for 1..29).

### Debrief inputs and statistics widgets

The debrief screen follows from: the outcome word (bank word 0, set in step 2),
the bank after steps 6..9 (words 8/9 and cleared unlock variables included),
the RNG state and the MSN/language data. The next mission is the CAREER entry
at the advanced position; there is no data-driven branch. The statistics
widgets of `0x40f904` receive the outcome word, the tally, lost count
(`0x482bd3`) and *P* plus the items added by `0x412ce2`; text IDs and widget
layout are in [vshell_ui.md §14](vshell_ui.md#14-debrief-page-telecomm-panel-and-dropship).
**Hypothesis:** returns 1/0 and final word 3 are campaign won, campaign
failed and player killed.

The selection is fully determined by the static contract once the RNG state
is given. Which RNG state the real shell holds at that moment (start-up seed
`GetTickCount() & 127` plus any draws before `0x40f09f`) is not recoverable
from saves; it stays an input.

## Results path implementation and acceptance

`shell/src/campaign.c` (`es2_shell_results`, header `es2/shell/campaign.h`)
and `es2_shell_debrief_select` in `shell/src/prepare.c`, which reuses the
preparation's condition, auxiliary and text machinery. The output SAV is
composed by the `formats:sav` writer. Front end: `shell/tools/es2campaign.py`;
private transport `es2prep_core --results`. Inventory lists are kept in file
order and written reversed, because the original restore prepends every item
it reads.

**Confirmed (in vitro):** `results_compare.py` runs every case through our core
and through the original `0x40f09f` in the mapped executable
([shell_invitro.md](../reference-game/shell_invitro.md#results-path)),
and compares the original writers' SAV byte for byte, the CAREER position,
return path and value, final word, total, lost-pilot count, debrief record
(references, count, auxiliary word), the rewritten `MISSION.STR`, the
composed text and the RNG state. Main run: **10,707 cases, 10,574 equal, 133
matched rejections, 0 mismatches**; a run with zero team counts: 2,459
cases, 2,343 equal, 116 matched rejections, 0 mismatches.

Probe coverage of the main run: all 61 CAREER positions × both outcome values ×
ENG/FRE/GER (every group boundary, returns 0, 1 and 2, roll-over into group
3/4/5 and the end of group 5); outcome words -32768/-1/2/32767 with extreme
tallies at every group end; 144 hook cases ((1,2)→(1,3) player bay 4;
(1,5)→(1,6) with vehicle 8 in bays 0..7, duplicate vehicle-8 bays, no vehicle
8 with bay 7 empty, and bay 7 holding vehicle *v* with a pilot or the player in
bay *v*, which loses its bay); slot conditions 0/79/80/81/100/±32767 on removed
and lost Hercs; 6,000 random cases (losses, replacement, promotions, repairs
with options 0..3, purchases, restock options 0/1, unlocks, construction, mode
0, inactive members, team capacity/count variations, duplicate bays); 3,507
debrief cases at every authored list-1 comparison boundary; 480 cases on 60
invented MSN/text files (overlaid for both implementations, never CD data)
with condition types 0..3, unknown types/operators, list-3 `-1` records and
duplicates and debrief records that may all fail; 138 repair-budget cases
with budget equal to, one above and one below the measured cost.

Matched rejections (both sides refuse; ours by policy, the original by the
named failure): empty list 5 → assertion at `0x41d925` (TRAIN1..8 in mode 1);
consumed pilot without loadout → write fault at `0x412314`; team count 0 →
divide fault at `0x40f4eb`; a debrief reference outside the text table →
null string pointer in the C string adapters.

**Sensitivity:** each of 11 deliberate rule changes in our code (overall
threshold, sticky expected value, return threshold 80, bay-7 fallback word,
promotion divisors, strict budget comparison, restock free count, rack
secondary IDs 13..16, family-1 repair width, compact/physical auxiliary index,
overwrite of failing debrief records) produced 8 to 296 mismatches in a
reduced sweep (`mutations.json`, `mutations2.json`). Random cases and the CD
missions never reach the budget comparison and the two debrief rules; the
repair-budget and invented-MSN axes do.

**Coverage (traced subset):** an `ES2_SHELL_TRACE=exec` run of 269 German cases
executed 2,649 of the 2,976 instructions of the 54 state functions of the
path. Not executed: assertion and allocation-failure blocks, the
null-loadout branches (`0x40f215`, `0x40f322`, faults), helpers unreachable on
this path (`0x41229c`, the item loop of `0x411408`), the player match in
`0x410844` and the restock drop loop of `0x4129de` (both exercised in the main
run, the latter shown by the restock mutation).

### Results path limits

- Labels: money/total, repair, restock, salvage value, promotion and the
  meaning of returns 0/1 and final word 3 remain hypotheses.
- The RNG state the real shell holds when `0x40f09f` runs is not recovered.
  The UI after the path (statistics widgets, the debrief window, the save to
  disk) is trapped, not run.
- The sweep uses SAVs from our builders plus field edits, not saves written by
  a played campaign, and synthetic `results.dat` bytes; recorded DBSIM output
  is covered by [results.dat corpus and round trip](#resultsdat-corpus-and-round-trip).
- Policy rejections outside the matched sites above (category -1 with nonzero
  availability, positive counts in variables 43..49, lists above 32,767 items,
  a missing auxiliary record, truncated inputs) are not compared with the
  original; the CD data never reach them.

## Shell options

**Confirmed (static)** from VSHELL.EXE and DBSIM.EXE listings; the blocks,
accessors, Preferences controls and the mode store are confirmed in vitro in
[vshell_ui.md §11.4](vshell_ui.md#114-preferences-blocks). UI labels are the
strings the controls are built with (localized `ESTEXT.BIN` in `LANG0.VOL`,
cited by string ID; English and German read).

### The preferences block

The values live in VSHELL's 54-byte preferences block at VA `0x48259c`,
which is the file `data\prefs.cfg` (path strings `0x46f27c`/`0x46f28b`/`0x46f29a`;
all bytes: [cfg.md](../formats/cfg.md#prefscfg)):

| Block offset | Address | Use |
| --- | --- | --- |
| +0x25..+0x29 | `0x4825c1..0x4825c5` | mode-0 controls c1..c5 of [Environment and shell mode](#environment-and-shell-mode) |
| +0x2a | `0x4825c6` | shell mode, copied into the word `0x4826f0` |
| +0x2c | `0x4825c8` | repair option |
| +0x2d | `0x4825c9` | weapons-building option (*restock* in the results path) |

- **Load** `0x40dc5b` (via `0x40dc44` from WinMain `0x406b28`, before the
  start-up routine `0x401aa4`): the block is zeroed (`0x40dc95`), the file is
  read with 54 bytes only when it opens (`0x40dcaf`); a short read leaves the
  rest zero and is not checked. A second copy at `0x4825d2` is the last
  committed state.
- **Accessors:** set `0x40ddfe(index, value, notify)`, toggle `0x40de35`,
  cyclic step up/down `0x40de5b`/`0x40de88`, restore all from the committed
  copy `0x40ddb6`, commit `0x40dd6e`, write all 54 bytes `0x40dd0a`
  (truncating write stream), read-modify-write of single bytes
  (`0x40df90` → `0x40dfa3`: the image is the current block, the file read
  over it, the listed indices taken from the block again, 54 bytes written).
- **Mode word:** `0x40e751..0x40e758` (called from the start-up routine at
  `0x401d44`) loads byte +0x2a zero-extended into `0x4826f0`. The only other
  writer is `0x40ec56`. The start-up routine reaches the entry dispatch
  (`0x401ff8` → `0x40180c`) afterwards, so for entry states 3/4 the results
  path (`0x4018b7`) sees exactly bytes +0x2a, +0x2c, +0x2d of the file as it
  was when VSHELL started.

### Which controls write them

**Mode (+0x2a)**: `0x40ec56(m)` sets the mode word, stores its low byte at
+0x2a without notification and rewrites only that byte in the file. Its five
callers are main menu buttons (string IDs from the menu constructor
`0x431200..0x431680`):

| Entry (ID) | Handler | Mode | Other effects |
| --- | --- | --- | --- |
| INSTANT ACTION (3) | `0x431935` | 0 | sets the preset flag `0x473784` to 1 (never cleared; this skips the preset loadout branch of `0x41cbb1`), prepares at once with the name `TRAINEE` and c3, writes the MEC, exit code 2 |
| START NEW GAME (4) | `0x431a08` | 1 | new-career screen `0x43c29a` |
| CONTINUE GAME (5) | `0x431a73` | 1 | loads save index 10 (`0x40eaaa(10, 1)`) |
| SAVE/RESTORE (6) | `0x431b27` | 1 | save/restore screen `0x43a19c` |
| PRACTICE MISSIONS (7) | `0x431f3a` | 0 | practice screen `0x44c322` |

Mode 1 is therefore the campaign (CAREER), mode 0 the single missions. This
agrees with the preparation (mode 1: CAREER header words and the SAV
equipment; mode 0: mission equipment) and with the results path (mode ≠ 1
ends before advancing). No UI control stores another value.

**Repair (+0x2c)** and **weapons building (+0x2d)**: main menu PREFERENCES
(ID 0xa, handler `0x431b9b`) opens the screen titled ID 0x103. Its panel
`0x48d398` is titled "Repair Options:" (0x107), panel `0x48d39c` "Weapons
Building:" (0x108). Each row is a label and a radio button whose handler
calls the setter (`0x43712c` for +0x2c, `0x4371e0` for +0x2d: store the value
with notification, then redraw the buttons, `0x436fbe`/`0x43716c`):

| Byte | Value | Button handler | Label (ID, English / German) |
| --- | --- | --- | --- |
| +0x2c | 0 | `0x437413` | 0x113 "AutoRepair All Hercs" / "AutoReparatur" |
| +0x2c | 1 | `0x437474` | 0x114 "Manually Repair My Herc" / "Mein Herc manuell reparieren" |
| +0x2c | 2 | `0x4374d5` | 0x115 "Manually Repair All Hercs" / "Alle Hercs manuell reparieren" |
| +0x2d | 0 | `0x437536` | 0x111 "AutoBuild Weapons" / "Waffenbau:Auto" |
| +0x2d | 1 | `0x437696` | 0x112 "Manually Build Weapons" / "Waffenbau:Manuell" |

Accept (ID 0x10c, `0x4372e1`) commits and writes all 54 bytes; Cancel (ID
0x10b, `0x437220`) restores the block from the committed copy and writes
nothing. No other code stores +0x2c or +0x2d (every caller of the accessors
was listed; the whole-block writes by other screens only persist the
committed state). Value 3 of the repair byte is not reachable through the UI;
the results path would treat it like 1.

The labels match the results-path arithmetic: `0x40edbc` repairs the player's
Herc only for 0 and the team members unless 2, so 0 = everything automatic,
1 = the player repairs his own Herc by hand, 2 = nothing automatic. `0x412a37`
fills the five-entry bank by priority only for 0 (*AutoBuild*) and otherwise
only drops entries that exceed the money. Other readers are UI: the repair bay
(`0x433995`) shows "Auto Repair" (0x42) for 0 and "Manual Repair" (0x41) for
1 and sets no mode text for other values; ARMORY (`0x449b87`, `0x449d2f`,
`0x44a06e`) adds, removes and clears build-bank entries only with manual
building ([vshell_ui.md §17.4](vshell_ui.md#174-armory-rows-add-remove-clear)).
The labels describe the UI; they are not difficulty or realism settings.

**Confirmed (in vitro):** `0x40ec56` changes only the working block, not the
committed copy. A Preferences Cancel after a menu entry therefore puts the
previous mode back into byte +0x2a of the working block (the word `0x4826f0`
keeps the new one), and a later whole-block write stores it in the file.

### Where the values end up, and DBSIM

The only file that holds them is `data\prefs.cfg`; they are not in the SAV or
in the four launch files. Their effects reach DBSIM indirectly: the mode
selects the preparation path (header words, bank, equipment source) and so
`script.dat`, `mission.var` and `player.mec`; the repair option changes the
hangar condition words in the SAV, which become the 33 condition words of the
next `player.mec`; the weapons-building option changes the five-entry bank,
the inventory lists and the money in the SAV (the MEC carries only the
availability flags).

DBSIM reads the same file (54 bytes into `0x4d1f38` at `0x459770`, 7 bytes at
`0x45e510`) but never bytes +0x2a, +0x2c or +0x2d:

- absolute references into the block (and its copies `0x4d1f6e`,
  `0x4d1fa4`) name only offsets +0..+0xe and +0x1a;
- the accessor calls `0x459958`, `0x45999c`, `0x4599d4`, `0x459a10`,
  `0x459b20`, `0x459b80`, `0x459c74`, `0x459d3c` pass immediate indices
  0..0x24; `0x459c54` takes indices from the table `0x49e2dc`
  (0..3, 7..11);
- the controller-mapping code indexes s+0..s+11 and s+4+b (button b < 8),
  where s (byte `0x4d2577`) is written only as 13 (`0x45cbd2`) or 25/13
  (`0x4617de`), at most 0x24;
- DBSIM's writer `0x459b94` rewrites only the listed indices (lists 0..3 and
  7..11, s−1..s+11, 0x0c..0x24, and 6), after reading the file.

So the three bytes pass through a DBSIM run unchanged when the file has 54
bytes, and the restarted VSHELL hands the player's last accepted choice to
the results path.

### Practice screen controls

The practice screen steps c1..c5 cyclically: +0x25 and +0x26 modulo 2
(`0x44c697`, `0x44c5da`), +0x27 modulo 4, +0x28 modulo 9 (and set from the
table `0x479c56` at `0x44c53d`), +0x29 modulo 2; INSTANT ACTION steps +0x2e
modulo 3. Labels, handlers and the left/right-click directions:
[vshell_ui.md §15.2](vshell_ui.md#152-practice-screen-0x44b310-shown-by-practice-missions-0x431f3a).

### Shell options implementation

`shell/src/campaign.c` (header `es2/shell/campaign.h`): `es2_shell_prefs_load`
(the start-up load: zero block, then up to 54 file bytes),
`es2_shell_prefs_options` (mode zero-extended from +0x2a, options
+0x2c/+0x2d into `es2_shell_results_input`) and `es2_shell_prefs_choose`
(store the UI values: mode 0/1, repair 0..2, building 0..1; anything else is
rejected before a byte changes). Private transport `es2prep_core - --prefs`
(no CD needed).

`es2campaign.py` options: `--prefs FILE` (read as the restarted VSHELL reads
it), `--menu-entry {instant-action,new-game,continue-game,save-restore,
practice-missions}`, `--repair {auto-all,manual-mine,manual-all}`,
`--weapons-building {auto,manual}`, `--output-prefs FILE` (the 54-byte file
VSHELL would hold after these choices; base `--prefs` or the CD
`DATA/PREFS.CFG`). The raw `--mode`, `--repair-option`, `--restock-option`
stay, without a UI check, and exclude their named counterpart. The JSON
output reports the options and their source.

**Defaults:** mode 1, repair 0, building 0. The CD `DATA/PREFS.CFG` (SHA-256
`01fd1750…ed4b`, the installation default) holds 0/0/0 at +0x2a/+0x2c/+0x2d;
after any campaign entry VSHELL holds 1/0/0. Every recorded suite was
prepared in mode 1, and DBSIM ignores the option bytes.

**Tests:** `campaign_test` (load of missing, short and long files; zero
extension of the mode; UI value sets and rejection; the three repair values
change the total of a small-budget case; mode 0 ends the path).
`campaign_cli_test` (synthetic prefs images through the transport, option
resolution, argument exclusions; with `ES2_DATA_ROOT`: the CD file's hash and
options 0/0/0, the default output file equals the CD file with byte +0x2a = 1
byte for byte, every UI combination changes exactly its bytes, and a
CD-default C1_01 SAV advances identically with the defaults, with the
written file as `--prefs` and with the equivalent named options). Four
deliberate mutations (signed mode byte, repair value 3 accepted, short file
not padded, swapped option bytes) each fail both tests.

## results.dat corpus and round trip

Every `results.dat` and `mission.var` pair the oracle suites wrote, the fields
seen there next to the static writer contract
([mission.md](../formats/mission.md#return-resultsdat),
[mission_runtime.md](mission_runtime.md#results-writer-42412c-and-its-helpers)),
and each pair through our results path and the original one. The files are
game output: they stay in `es2-data` and the test reads them at run time.

### Corpus

`//shell:results_corpus_real_test` (`shell/tests/test_results_corpus.py`,
skipped without `ES2_DATA_ROOT`) walks `oracle/golden` and `logs` of
`es2-data` for directories holding both files (snapshot `DATA` directories of
play sessions excluded), de-duplicates by the SHA-256 of both files and
attributes each pair to the launch set its run started from: the run's
`<stem>-launch.json` next to `<stem>-output` lists the prepared `script.dat`,
`player.mec` and `mission.var` hashes, which are matched against the sealed
launch sets (`oracle/launch/*/manifest.json`) and the C1_01 pins; golden
outputs are attributed by their suite directory. The scan finds 357
directories with 9 distinct pairs, all attributed:

| results / mission.var (SHA-256 prefix) | Bytes | Runs | Launch set | Flag | Tally | P | N | Results path (ours = original; mode 1, options 0/0) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `93e27063` / `7f8e45b9` | 160 | 126, golden | c1_01-success (GAME_R + 71 draws) | 1 | 43,981 | 2 | 2 | return 2 → C1_02 (1,1); debrief references 10, 8, 9, auxiliary word 37, 66-byte text |
| `0e4bc8ca` / `7f8e45b9` | 160 | 4 (regression runs) | c1_01-success | 0 | 48,273 | 2 | 2 | return 2 → C1_02; references 10, 8, 9, auxiliary word 52, 303-byte text |
| `c29e0765` / `029d50e0` | 308 | 83, golden | c3_09-loadout-b | 0 | 191,655 | 3 | 4 | player killed (final word 3), 2 pilots lost |
| `ba0737b9` / `c2552a81` | 296 | 1 (recording, aborted 3) | c3_09-loadout-b | 0 | 0 | 0 | 4 | return 2 → C3_10; auxiliary word 59, 312-byte text |
| `61e64ece` / `ca7fce7d` | 304 | 1 (recording, aborted 4) | c3_09-loadout-b | 0 | 31,164 | 2 | 4 | return 2 → C3_10; auxiliary word 59, 312-byte text |
| `36609524` / `3084be51` | 296 | 68, golden | c3_10-loadout-hercs | 0 | 0 | 0 | 4 | player killed, 3 pilots lost, one Herc lost |
| `be68baf1` / `5d9fefac` | 80 | 72, golden | c4_08-razor-plasma | 0 | 0 | 0 | 1 | player killed |
| `ba0737b9` / `b825950f` | 296 | 1 (authoring, aborted) | c5_05-moon-missiles | 0 | 0 | 0 | 4 | return 0 (outcome 0 in group 5), final word 0 |
| `55126bb2` / `b825950f` | 312 | 1 (authoring, paused) | c5_05-moon-missiles | 0 | 108,327 | 4 | 4 | return 0, final word 0 |

Outside the corpus (not suite output): two snapshot `DATA` directories of the
owner's play sessions, and the CD's prepared `DATA` pair (`dc1cbabd…` /
`20c8eb56…`, the 152-byte example of mission.md) in the chain's data backup
under `oracle/launch`.

### Fields seen live versus the static contract

N is the number of 72-byte records. In every pair the file is consumed
exactly with N = the record count of the launch `player.mec` = the number of
records the VSHELL results path reads from the start SAV (the player plus the
active selected pilots of orders 1..count−1).

| Field | Static contract | Corpus |
| --- | --- | --- |
| +0 flag | word, 1 when `objective_eval` returned 9 | 0 and 1; 1 only in the C1_01 golden |
| +2 tally | u32 = `multiply_q10(2500, score_sum)` + 25,000 × bank word 20 (byte 0x28 of the returned `mission.var`) | 0 .. 191,655; every value decomposes exactly; score sums 0, 12,765, 17,062, 18,015, 19,773, 44,371; word 20 nonzero only in the C3_09 golden (6, i.e. 150,000 of its tally) |
| +6 P | i16 salvage count `[4a9e6a]` | 0 .. 4 |
| pairs (item, percent) | item ID word; `((256 − r)·100) >> 8` with part ratio r < 128, so ≥ 50 | items 1, 3, 10, 15; percent 66 .. 100 |
| record words 0..12 (condition groups) | `((256 − c)·100) >> 8` of the Q8 word, range −12,700 .. 12,900 | 0 .. 100; groups 2 and 3 are 100 in every record |
| record words 13..22 (parts) | same transform | 0 .. 100; word 22 (part 9, the shell's pilot +0x2b) is 0 exactly in the records of the killed player and the lost pilots |
| record words 23..32 (the shell's slot words) | same transform | 0 .. 100 (3, 20, 25, 37, 43, 44, 62, 74, 76, 86 besides 0 and 100) |
| record words 33..35 (actor +0x2a4) | three words, read into +0x2d, +0x31, +0x2f | word 33: 0 .. 4; word 34: 0, 1, 2, 4, 7; word 35: always 0 |
| `mission.var` | 2,000 bytes; DBSIM clears words 10, 20, 21..42 after reading | always 2,000 bytes; word 0 always 0 (the shell overwrites it with the flag); words changed against the launch file: 10, 20, 22, 28, 35 (C3_09 golden), 22, 28 (C3_09 aborted 4), 10 (C3_10), 250 (C1_01), 621, 624, 776, 782 (C4_08); none in C3_09 aborted 3 and the two C5_05 runs |

Untouched team members report 100 in every word, and every killed player or
lost pilot has 0 in part 9: the written word reads as *remaining
condition* (100 = intact), so the Q8 source word DBSIM transforms is 0 for an
intact part (**hypothesis** from the data;
[mission_runtime.md](mission_runtime.md#team-result-records-423d68) gives the
arithmetic).

Never varying in the corpus: condition groups 2 and 3 (always 100), record
word 35 (always 0), returned bank word 0 (always 0). No value is negative or
above 100, no item ID lies outside 1..15, no pair count exceeds 4.

### Round trip through the results path

Each pair starts from its launch set's SAV and the RNG state our preparation
of that launch set leaves; the preparation is checked to equal the four
launch files DBSIM ran, hash by hash. The pair then runs through
`es2campaign.py`'s results path (`es2_shell_results`) and through the
original `0x40f09f` in the mapped VSHELL (`es2shellprep --results-batch`, UI
entries trapped) and is compared with `results_compare.compare`: SAV bytes,
CAREER position, return path and value, final word, total, lost pilots,
debrief record (references, count, auxiliary word), the rewritten
`MISSION.STR`, the composed text and the RNG state. Options: mode 1 with
repair 0/1/2 × restock 0/1, plus mode 0. **63 cases, 63 equal, no rejection
on either side.**

Exercised (from the original's output): player killed (3 pairs), return 2
(4 pairs), return 0 (2 pairs), mode 0 (all), pilot losses and replacement,
promotion, salvage items, construction, one repair (C5_05 paused, repair
option 0), restock option 0 changing the build bank in the two pairs with
salvage left; different repair options give different SAVs on both sides.
Not exercised: return 1 (end of campaign), the (1,3) and (1,6) hooks,
nonzero unlock variables, languages other than GER. The results path drew no
random number in any case (output RNG = input RNG): the debriefs of C1_01,
C3_09 and C5_05 select without a type-1 condition, so the selection does not
depend on the shell's start-up seed.

### Live debrief check

Open: whether the real VSHELL's Mission Report page shows the debrief our
path composes. The C1_01 golden pair is the only corpus pair that reaches
return 2 from a successful mission, and its selection draws no random number,
so the screen is fully predicted.

- Setup: a copy of a clean clone (never `native-r111`), German
  (`DATA\LANGUAGE.CFG` = `G`); `SAV\GAME_R.SAV` with the pinned hash
  `ae19c8e6…b2cb` and its slot-10 companions (the C1_01 files);
  `DATA\PREFS.CFG` byte +0x2a = 1, +0x2c = 0, +0x2d = 0; `DATA\results.dat`
  and `DATA\mission.var` replaced by the golden pair `93e27063…` /
  `7f8e45b9…` from `oracle/golden/c1_01-success/output`.
- Start: `VSHELL.EXE dummy -eggplant -X3 -G`, the command line ES uses after
  DBSIM exit 3. VSHELL loads slot 10, runs the results path and, through the
  DEBUG dialog's automatic Use Default, prepares C1_02 and opens the mission
  screen on page 4 "Mission Report"
  ([vshell_ui.md §4.9](vshell_ui.md#49-after-a-mission-results-path-0x40f09f-entry--x3-x4)).
- Capture: page 4 with the whole debrief text and the statistics widgets, the
  debrief movie, `DATA\` (the C1_02 launch files), then after MAIN MENU and
  QUIT `SAV\GAME_R.SAV` and `SAV\GAMEFILE.STR`, stored under
  `<es2-data>\logs\o-11-vshell-debrief-<yyyymmdd>\` (`screens\`,
  `data-at-report\`, `sav-after-quit\`, `notes.txt`).
- Expected (our path, equal to the original in vitro): next mission C1_02
  (1,1), return 2, debrief references 10, 8, 9 with auxiliary word 37 (the
  debrief movie word), a 66-byte text (SHA-256 prefix `5b319350`), salvage
  total 45,981 (2,000 + 43,981), no pilot lost, two salvage items added,
  final word 2. The slot-10 SAV after the C1_02 preparation also holds that
  preparation's bank words and RNG draws from an unknown start-up seed; it
  is compared outside those fields.

A play-through with the own launcher (`start-own-launcher.ps1`, CONTINUE GAME
→ C1_01 → Rock & Roll) is comparable only when its `results.dat` equals a
corpus pair (both C1_01 pairs share the `mission.var` `7f8e45b9…`), because
VSHELL's C1_02 preparation rewrites `DATA\mission.var` before the report is
shown. The `0e4bc8ca…` pair (flag 0) would show the C1_01 failure debrief
(auxiliary word 52, 303-byte text).

### Corpus limits

- In-vitro parity on recorded inputs: equal outputs of two implementations
  for the same SAV, not a live VSHELL observation; the screen is open
  ([Live debrief check](#live-debrief-check)).
- The starts are our reconstructions (GAME_R for C1_01, `loadout.sav` of the
  sealed launch sets), checked only through the four launch files our
  preparation reproduces; the RNG state is our preparation's output, not one
  read from a running VSHELL.
- The corpus is small: 9 pairs from 5 missions, all GER, all with the suites'
  default options; fields that never vary here (groups 2/3, record word 35)
  are covered only by the static contract.
- The test writes `results-corpus.json` (hashes, parsed fields, per-case
  outcomes; no file bytes) to its undeclared outputs, or to
  `ES2_RESULTS_CORPUS_REPORT`.

## Service-bay editors and the preparation state

What REPAIR, BUILD and the Herc scrap change in the state that a following
preparation, the launch-file writer `0x40f68c` (vshell_ui.md §10.3) and the
Rock & Roll checks (§11.1) read. The operations themselves are in
[vshell_ui.md §12](vshell_ui.md#12-service-bay-editors-repair-build-herc-scrap);
**C** static, **T** in vitro (`//shell:editors_real_test`: the SAV after
each sequence equal to the original writers').

- **Condition words.** REPAIR raises one row to the next band limit (100,
  90, 80, 60, 30 or 1; a part group writes all its words), REPAIR ALL sets
  words 0..21 and the slots below the capacity to 100, CANCEL restores the
  33 words of the snapshot (C, T). These are the words `0x40f68c` copies
  into `player.mec` with the bay's record and the words 13, 14, 18 and 21
  the functional rule `0x411ca5` tests (> 50), so a repair can turn box 0
  ("not functional") into a launch.
- **Salvage total** `0x482bd8` (SAV tally): − the repair costs, − HERC_INF
  +8 · 1000 per Build, + `0x414174` per scrap (C, T). The results path's
  repair budget (`0x40edbc`, total / count) and the rebalance `0x412a37`
  read it; the preparation does not.
- **New record (Build).** Vehicle word +0 = the BUILD row, +2 = its index in
  `0x46f868`, capacity from `0x46f87a`, all 33 words 100, no items,
  progress 0 and countdown HERC_INF[+2] +12 (C, T). Progress 0 makes the bay
  unfinished: the REPAIR screen and the Rock & Roll check `0x411088` skip it
  until the results path's `0x41104f` has counted it to 100; the hangar
  count word grows by one.
- **Scrap.** The record is gone (hangar count − 1), its items with a
  condition of at least 80 go back to the head of their inventory lists, the
  pilot of that bay (the player first) gets bay −1, and the selected pilot
  of the first group with that pilot's order word is deactivated with the
  team count word `0x482b5e` − 1 (C, T). The player without a bay gives
  Rock & Roll's box 2; an inactive pilot is skipped by `0x40f68c` and by the
  results path; the order slot stays with the deactivated pilot until CREW
  ([vshell_ui.md §17.3](vshell_ui.md#173-crew-pilot-lists-assignment-clear))
  changes it.
- **Five-entry build bank.** Leaving REPAIR or BUILD runs `0x412a37` with
  prefs +0x2d (refill by priority or drop what the salvage cannot pay); the
  editors' enable rules use salvage − the bank's cost (`0x412baa`) (C, T).
- Not changed by these editors: the availability flags, the nine-word bank,
  the CAREER position, the variable bank and the roster selection.

## RNG use in the menus and the start-up seed

Source: the llvm-objdump listing of `VER95\VSHELL.EXE` plus a whole-image
search for the four entry addresses as 32-bit literals. All findings are
**static**.

### Entry points

**Confirmed (static).**

| Address | Role |
|---|---|
| `0x465c90(state)` | initializer: bytes +0x70/+0x71 := 0x18/0x37, copy the 112-byte table `0x47f850`, call the empty `0x465d50` |
| `0x465cbc(state, n)` | reset with `0x465c90`, call the empty `0x465d55`, then `0x465ce8` while the decremented word `n` stays >= 0: exactly `n` draws for `n` = 0..32767 |
| `0x465ce8(state)` | one raw draw (the generator core, rng.md) |
| `0x465d2c(state, bound)` | bounded draw: (raw & 0x7fff) mod bound (`0x465d3a..0x465d4b`) |

`0x482409` and `0x48110a` are **state objects** (114 bytes each), not
functions. The image holds no 32-bit pointer to any of the four entry points
(no indirect or virtual draws); every draw is one of the direct calls below.

### Seeding and resets

- Static initializers (pointers in the initializer table at file offsets
  `0x8044e` and `0x80460`): `0x4023be` initializes `0x48110a` at `0x4023d3`;
  `0x407d4f` initializes `0x482409` at `0x407d54`. Both run once per process
  before `0x401aa4`.
- Start-up reseed: `0x40796c` returns at once for argument 0 (`0x401b15`); for
  argument 1 (`0x401d39`, the resource part of start-up,
  [vshell_process.md](vshell_process.md) section 3) the straight-line path
  `0x4079f2..0x407b61` (only forward `jne` skips of error boxes) ends with
  `GetTickCount()` (`0x407b42`), `and ax, 0x7f`, and
  `0x465cbc(0x482409, n)`: **once per VSHELL process, n = 0..127 draws** after
  a full reset, then `0x48247b` := 1. Nothing reseeds either state later.
  Because ES.EXE starts a new VSHELL for every shell step, the preparation
  stream restarts from one of 128 clock-chosen states after each mission.
- `0x48110a` is never advanced at start-up: it begins every process at the
  table state.

### Every draw site and its caller chain

**Confirmed (static).** Stream `0x482409` (bounded draws `0x465d2c`):

| Site | Function | Chain up to a handler | Screen / event | Covered by |
|---|---|---|---|---|
| `0x40ffb3` | `0x40ff80` (roster permutation) | `0x410055` (`0x410094`, `0x4100a2`) ← `0x40e8a5` (`0x40e8c9`) | new game (below) | launcher.md "New career `0x40e8a5`", this doc `0x410055` |
| `0x41001f` | `0x410000` (bound-100 thresholds) | `0x410055` (`0x4100d6`) ← `0x40e8a5` | new game | same |
| `0x410741` | `0x41072b` (player variant, bound 11) | `0x40e8a5` (`0x40e8db`) | new game | same |
| `0x40e90d` | `0x40e8a5` (salvage, bound 11) | Accept `0x43c5ab` / ACCEPT `0x43c78b` (registration), INSTANT ACTION `0x431935` (main menu), Begin Mission `0x44ca26` (practice) | new game | vshell_ui.md §8 |
| `0x40ef4a/5f/74` | `0x40ef06` (bank words 4..6, bound 12) | preparation `0x41cd61` (`0x41cd77`) | DEBUG dialog Use Default `0x44db5c` (`0x44db91`), ACCEPT `0x44dc4d` (`0x44dc96`) | rng.md "Streams across save, load and the VSHELL hand-over", item 4 |
| `0x4185c9` | MSN reader `0x41818b` (type-1 conditions) | `0x41cd61` (`0x41cd7f`) | same | this doc, preparation |
| `0x41664e` | `0x4165e7` (template choice, bound +10) via `0x416744/99`, `0x4167ee`, `0x416843`, `0x41689a`, `0x4168ef`, `0x416948` | `0x41818b` | same | this doc, preparation |
| `0x41d4af` | alternate MSN reader `0x41d072` (type 1, bound +6) | `0x41d8e7` ← `0x4133eb` (`0x413427`) ← results path `0x40f09f` (`0x40f534`) | `-X3`/`-X4` start-up (`0x4018b7`) | this doc, debrief selection |

Stream `0x48110a` (raw draws `0x465ce8`):

| Site | Function | Chain | Screen / event |
|---|---|---|---|
| `0x42959c` | `0x4293af` | `0x429664` HeightGrid (`0x429821`) ← `0x42519c` ← `0x424a2b` ← `0x424597` ← map object `0x40e75f` (`0x40e836`) | map object rebuild: load `0x40eaaa` (`0x40ec14`; CONTINUE `0x431a73`, RESTORE `0x438393`, Replay Yes `0x44d1d4`, `-X3`/`-X4` `0x4018af`) and the DEBUG dialog (`0x44dba3`, `0x44dca9`) |
| `0x429b10` | `0x429664` | same | same |

Both `0x48110a` sites keep `(raw & 0xfff)` and compare it with
`[0x472368] * 41` to pick a per-cell byte at +0x19 of the map grid
(`0x4295a2..0x4295c2`, `0x429b16..0x429b45`). **Hypothesis:** this is a
cosmetic map variant; the grid belongs to the shell map object, and no
literal reference connects `0x48110a` or the grid to a SAV writer.

**Confirmed (static): no other draws.** No screen constructor, widget
handler, the animated main-menu background, the service-bay editors,
Preferences, save/load (apart from the map rebuild on `0x48110a`) or the
pump draws from either stream. Menu navigation therefore never advances
`0x482409`; only new game, preparation and the results path do.

### What a mission transition draws in VSHELL

Per process, `0x482409` = reset + n draws (n = `GetTickCount() & 127`), then:

- results process (`-X3`/`-X4`): the debrief reader's type-1 draws of the
  mission just played, then, with return 2, the next preparation (`0x40ef06`
  three draws, then the MSN reader's type-1 and template draws) — the DEBUG
  dialog's Use Default ([vshell_ui.md §4.11](vshell_ui.md#411-the-debug-mission-file-dialog-0x44dd38));
- new career in the same process: 58 + 1 draws
  ([launcher.md](launcher.md#new-career-0x40e8a5)) before the first
  preparation.

The state is never saved (rng.md "Streams across save, load and the VSHELL
hand-over", item 3), so nothing carries from one mission's process to the next.

### The own chain: a documented deviation

`es2chain.py` seeds once at new/continue and **carries** the 114-byte state
from the results step into the next preparation and on to later missions
([launcher.md](launcher.md#campaign-chain-through-the-own-launcher)). VSHELL
instead restarts from one of 128 reset + n states in every results process.
From the second transition on, the chain's state is therefore generally not a
state VSHELL can hold. The shell state engine of `es2menu`/`es2shell`
restarts the RNG at every process start as VSHELL does
([launcher.md](launcher.md#rng-convention)).

Exact effect on SAV bytes when the state differs (static; from the draw
table above): the variable bank (SAV, 2,000 bytes from `0x482bdc`,
[mission.md](../formats/mission.md)) words 4, 5, 6 = bank bytes +8..+13 (three
bound-12 draws at every preparation), plus every bank word and launch-file
content that the MSN reader's type-1 conditions and template choices decide
(retained conditions/actions, `script.dat`, `MISSION.STR`). The debrief draws
change only the debrief selection, `MISSION.STR` and the debrief text, none
in the SAV, but they shift the stream for the following preparation. Roster,
player variant and the starting salvage depend on the state only at a new
career, where the chain reseeds.

### RNG open points

- The cosmetic reading of the `0x48110a` map byte is a hypothesis.
- None of this section is confirmed in vitro or live; an in-vitro check would
  need a harness entry for `0x40796c(1)` with a fixed tick.

## Evidence

The focused listings and reports below were written to ignored scratch
directories of worktrees that are not kept; the paths name the files, not
evidence that can be opened. The statements above rest on the cited
addresses:

- Preparation contract: `build/codex/scratch-shell-prep/` — `helpers.asm`,
  `preparation.asm`, `vshell.asm`, `rederive-equipment.asm`,
  `rederive-inventory.asm`, `audit.py`, `acceptance_debug.py`, `imports.py`,
  `signature_audit.py`, `acceptance-report.json`, `acceptance-x64.json`,
  `campaign-report.json`.
- Differential sweep: `build/codex/scratch-shell-mec/{baseline-parity,coverage,retained}.json/.md`
  of the worktree `es2-shell-mec` (not preserved).
- Results path and debrief: `build/claude/shell-campaign/` —
  `results-path.asm` (`0x40f09f..0x40f68b`), `results-helpers.asm`,
  `debrief-reader.asm` (`0x41d072`), `results-closure.txt` (static call
  closure of `0x40f09f`), `results-full.json/.md` (SHA-256 `d60b198f…244a` /
  `c5952f60…8776`), `results-count0.json`, `mutations.json`,
  `mutations2.json`, `coverage.json`, `coverage.log`.
- Shell options: `build/claude/s-02/` — `prefs-accessors.asm`,
  `preferences-screen.asm`, `main-menu.asm`, `preferences-layout.txt`
  (control rectangles, string IDs, handlers), `pref_calls.txt` (every
  accessor call in VSHELL), `dbsim-prefs-refs.txt`, `dbsim-prefs-calls.txt`,
  `mutations.log`, and the scripts `va.py`, `callers.py`, `screen.py`,
  `estext.py`, `findptr.py`.
- results.dat corpus: `build/claude/s-05/` (`results-corpus.json`).
