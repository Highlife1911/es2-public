# VSHELL in vitro

The in-vitro harness for `VSHELL.EXE` is the worker `es2shellprep.exe`
([tools/reference-game/shell-invitro](../../tools/reference-game/shell-invitro/README.md)).
It maps the hash-checked original image privately and calls the original
functions of three areas without starting the game or loading an original DLL:

- mission preparation `0x41cd61` (script, text, variables and player MEC);
- the results path `0x40f09f`;
- the save-slot, new-career and service-bay functions of the menus ("slot mode").

The own `shell/` library (`es2prep_core`, [shell/README.md](../../shell/README.md))
implements the same functions; the sweeps below compare both byte for byte.
Statements are confirmed where an address, hash or measured comparison is
given; hypotheses are collected in [Open points](#open-points).

Reference: `VER95/VSHELL.EXE`, 566,304 bytes, SHA-256
`1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e`.
Addresses use image base `0x00400000`; PE image size `0x1ea000`, relocation
directory RVA `0x1e0000`, size `0x886c`. The contracts of the functions
themselves are in [vshell_preparation.md](../reverse-engineering/vshell_preparation.md)
and [vshell_ui.md](../reverse-engineering/vshell_ui.md).

## Preparation entry points

The default-preparation UI callback is `0x44dbea` (button text at `0x47a52c`
is "Use Default"), installed by the button constructor call at `0x44e112`
(callback argument at `0x44e0fa`). It hides the selection windows through
`0x44e217`, calls `0x44db5c` at `0x44dbff`, then destroys its callback object
through vtable slot +0x18 and restores the exception chain. `0x44db5c`
resolves the mission through `0x46fc54/58/5a` and calls `0x41cd61` at
`0x44db91`. The alternative callback `0x44dc4d`, installed at `0x44e174`,
constructs a mission path from selected UI record `0x48dcf8+0x45`, replaces
character 0x5e with underscore, and calls preparation at `0x44dc96`. The
worker selects a CAREER entry directly and builds neither UI object.

`0x41cd61` produces all four files (script, MISSION.STR, variables, player
MEC). After script/text serialization it calls `0x40f68c` at `0x41d058`, then
`0x41c8b3` to free the preparation tables. The second MEC/variable write at
`0x44dbd5` belongs to the non-mode-1 outer path, after preparation has
returned; no byte-exact result needs it.

After preparation, mode 1 runs `0x40e75f`, `0x4135bb`, `0x430ec4(10)`,
`0x43b7f2(8)`, `0x43b758`, `0x43aee7([0x481108],1)` to construct and show the
briefing flow. The other branch rewrites MEC/variables, calls `0x408d1a(2)`
and sets `0x46c268=1`. These presentation and exit operations lie outside the
four-file boundary and the worker never calls them.

Immediate calls of `0x41cd61`:

| Call site | Callee | Effect |
| --- | --- | --- |
| `0x41cd72` | `0x46b574` | Borland exception registration |
| `0x41cd77` | `0x40ef06` | Mode/difficulty variables and three RNG draws |
| `0x41cd7f` | `0x41818b` | MSN reader, conditions, remapping, localized text |
| `0x41ce20` | `0x41b278` | Script writer, including `0x418014` text writer |
| `0x41ce5c`, `0x41cf62` | `0x44e560` | Assertion boundary (fatal in the worker) |
| `0x41ce89` | `0x4134f2` | First retained pretext record into briefing globals |
| `0x41ced1` | `0x410923` | Adjust selected pilots to authored team capacity |
| `0x41cee9` | `0x41cbb1` | Mode-zero player equipment construction |
| `0x41cf08` | `0x41c862` | Actor lookup for mode-zero team construction |
| `0x41cf12` | `0x4086b7` | Allocate loadout object |
| `0x41cf28` | `0x41139e` | Initialize loadout |
| `0x41cf81` | `0x411602` | Initialize loadout slot storage |
| `0x41cfe7` | `0x411bea` | Insert selected equipment |
| `0x41d007` | `0x41118c` | Insert loadout into hangar |
| `0x41d01a` | `0x40ec8f` | Assign roster selection |
| `0x41d038` | `0x4108e2` | Associate selected pilot and bay |
| `0x41d058` | `0x40f68c` | Write variables and player/team MEC |
| `0x41d05d` | `0x41c8b3` | Release retained mission tables |

`0x40f68c` first calls variable writer `0x40ef83`, then serializes selected
player/team data through `0x410cdb` and availability suffix `0x412877`.
`0x41b278` serializes the retained lists and calls `0x418014`. The data-driven
rules stay entirely original instructions, including `0x417c34` conditions,
`0x417c7d` variable clearing and the six-way dispatch at `0x418348` (bounded
by `cmp eax,5` at `0x41833f`). No preparation algorithm, RNG table or authored
mission record is reimplemented or embedded in the worker.

### Final start callbacks

Preparation and final launch are separate UI phases. Callback `0x445b99`
checks selected bay `0x482b82`, calls `0x4110c1` and `0x411135` on hangar
`0x482ba7`, then `0x40fc7e`; failing checks call alert helper `0x44d90c` with
IDs 2/0/1/3. Its successful path calls `0x40f68c` at `0x445c1a`, selects exit
2 at `0x445c21`, and sets `0x46c268=1`. Callback `0x431935` sets
`0x473784=1`, calls `0x40ec56(0)`, `0x44c58b`, UI refresh helpers selected by
`0x481f4c`, and `0x40e8a5` using option c3; it rewrites MEC/variables at
`0x4319aa` and selects exit 2 at `0x4319b1`. Neither calls the MSN/script
preparer, so equipment changes made in the UI reach the MEC after script
preparation. The worker supplies frozen SAV/default equipment before
preparation; the two byte-exact results below show that no intervening UI
equipment operation is needed for those states. The launch-callback UI and
alert behaviour is not executed in preparation mode; slot mode runs
`0x445b99`, `0x431935` and their checks (see [Slot mode](#slot-mode)).

## Static call inventory

[boundary.py](../../tools/reference-game/shell-invitro/boundary.py) rebuilds
an address-only inventory from the hash-checked EXE and optional instruction
traces: every discovered function's call sites and targets, import boundaries,
absolute global/address operands and unresolved transfers. It has 213 roots,
including preparation and the original loaders. All discovered instruction
starts decode; traces of both fixtures, the CD defaults and all 61 missions
cover 11,045 instructions, 145 called targets and 648 distinct call edges. The
final inventory has no undecoded instruction and no unresolved indirect target
within these roots and stream types; static resolutions are kept apart from
executed edges (`static_virtual_slots`). The complete callee adjacency is the
[appendix](#callee-inventory). The per-instruction global references are more
precise than assigning one guessed semantic name to a whole data region.

The inventory is complete for the discovered static closure and the observed
calls; it is no proof of all paths for every possible state.

| Static dispatch | Evidence |
| --- | --- |
| File input vtable `0x47a730` | Installed at `0x44ed92`; archive file is constructed at `0x4027f3`. |
| Codec-7 vtable `0x47a7a8` | Installed at `0x44f5b6`; selected at `0x403422..0x40349e`. |
| Codec-9 vtable `0x47a800` | Installed at `0x44f8e6`; selected at `0x4034ab..0x403524`. |
| Archive-aware stream vtable `0x46d14c` | Installed at `0x403108`; its seek/read dispatch is +8/+24. |
| Nested byte readers | Codec source at `0x44fa16` is passed at `0x44fa1e` to `0x44fd6c`; `0x44fd78` stores it in the bit-reader object. Codec seek loops at `0x44f743/763`, `0x44fb17/37` call their own read method. |
| Array-constructor callback `0x412e70` | Sole caller in this closure is `0x412fd0`, callback at `0x412fbd`, mode 1 at `0x412fc2`; only callback site `0x46b913` is reachable. The other ABI arms have the same supplied target but are not selected. |

The tool reads method targets from the hash-checked vtables; no vtable
contents are embedded in source. The archive/decompression alternatives need
descriptor -1 (`0x403733`, `0x4039af`) or a populated original volume group.
Successful VFS opens return descriptors >= 3 and the supplied group is empty,
so these branches do not execute in the worker. The original assertion UI,
post-preparation UI and process dispatch stay boundaries. The SAV preflight and
the original CD loaders define the supported state; every heap-indirect read
for arbitrary malformed runtime objects is not claimed.

## State contract

| Globals / storage | Provider |
| --- | --- |
| `0x46fc50`, `0x46fc54`, `0x46fc58/5a` | Original `0x412f2a`/`0x412e88` load CAREER groups and resolve MISSIONS strings; SAV or explicit zero-based group:index selects a mission. |
| `0x48235e`, `0x46ddf8` | Language 0=E, 1=F, 2=G (`0x417cf2` branches); original `0x408bb5` loads localized ESTEXT. German is the default. |
| `0x4826f0` | Shell mode; explicit override, otherwise 0 for group 0 and 1 for other groups. The default is a tool policy, not a claim that the mode can be recovered from a SAV. |
| `0x4826ea`, `0x4825c1..c5` | Explicit difficulty and five training-option bytes; default zero. Mode-zero header receives c1,c2,c3,c5; c4 selects a vehicle. |
| `0x473784`, `0x479c54` | Alternate training-preset dword flag / signed-word index read by `0x41cbb1`. `--preset-flag` (0..255, widened to dword) and `--preset-index` (raw word). Index > 3 and zero flag select the c4-indexed INI; defaults are zero. |
| `0x482b5c..0x482ba6` | Player/team state, restored by `0x4107dc`, including selected roster pointers at `0x482b9b`, bay `0x482b82`, skill `0x482b85`. Default player uses `0x41072b`. |
| `0x482ba7` | Hangar restored by `0x410e2e`; default `0x410b11(...,1)` loads CD HERCs. Mode zero first calls `0x410aaf` to clear restored bays before original mission equipment construction. |
| `0x4833e0`, `0x483c2c/32` | 36 original pilot records and two selection arrays; `0x41029b` restoration, or `0x410055` CD defaults. |
| `0x483c38` | Nine HERC information records loaded by `0x411e40`. |
| `0x483cc8` (availability at `0x483cde`), `0x46fa14/16`, `0x484086` | Item categories/availability and associated banks restored by `0x412781`/`0x411fad`; default inventory uses `0x4125e8`. Availability serialization is `0x412877`. |
| `0x482bd8`, `0x482bdc` | Tally and 2,000-byte variable bank; original SAV stream reads, optional explicit bank override. Preparation updates reserved variables itself. |
| `0x482409..0x48247a` | 114-byte RNG state initialized by original `0x465cbc`; draw helpers `0x465ce8`, `0x465d2c`. No original RNG constants are in source. |
| `0x4840cc..0x48419d` | Nine SAV selection/briefing spans (152 bytes); exact original reader widths. `0x4134f2` later replaces the briefing selection from the retained pretext record. |
| `0x4826f2`, `0x4833ac` | Final saved word and 20-byte block, restored unchanged; semantic labels unresolved. |
| `0x470740..0x4707aa`, `0x48552a..0x4855cc` | Retained mission-list pointers/counts, team count, setup and remap workspaces filled by original MSN preparation: outputs and intermediates, not supplied tables. |
| `0x482480/48c/494` | Original allocation bookkeeping. The worker's allocator supplies a size dword immediately before each block, as read by the original free/statistics helpers. |
| `0x482369`, `0x4824dc` | Original name/path scratch used by `0x40d99d`; left to original code. |
| `0x46cae4`, `0x5ddfa0` | Original volume-group pointer and Borland exception-depth pointer; supplied empty group / private counter. |
| `0x48dd04/08`, `0x48de34..0x48e137` | Assertion/error and path-splitting scratch in stream alternatives; no UI objects supplied. |

Fixed tables, string literals, vtables and cleanup metadata are read in place
from the checked image. The address-reference inventory separates neither
reads from writes nor heap-indirect reads; it is not a dynamic memory-read log.

### SAV restoration

The SAV is preflighted with explicit little-endian readers, span/count checks,
name termination and index bounds before any original loader sees it
(`state.c`). Then `0x412781`, `0x41029b`, `0x4107dc`, `0x410e2e`, `0x411fad`
restore their sections through an original file stream. The nine selection
spans are read through that stream directly: `0x4131e3` also copies old launch
companions, which would turn expected outputs into inputs, so the companion
copies are not called. The framing agrees with [mission.md](../formats/mission.md).

`GAME_R.SAV` consumes 4,753 of 4,763 bytes; variables begin at offset 2,731.
Its opaque 10-byte trailer is reported and ignored. `GAME_T.SAV` consumes all
4,322 bytes; variables begin at offset 2,300. `GAME_0` and `GAME_1` also pass
restoration and select C1_01. GAME_R SHA-256
`ae19c8e622adf64173d895b31d4ebcacbadbf7667a57fa9d793deacd97b9b2cb`;
GAME_T SHA-256
`1e0cfc3705127b504fafd3a60b92246e4de916113ea24bd0e82ddcec8bf8de0f`.

### RNG

The original RNG initializer first initializes its state, then advances it N
times (`0x465cbc..0x465ce2`, signed 16-bit loop). `--seed` takes 0..65535 as a
raw word: 0..32768 advance that many draws; 32769..65535 advance none.
`0x40ef06` sets the mode-1 group/index variables, clears the bank in mode zero,
sets the difficulty and draws three bounded-12 values at `0x40ef4a/5f/74`.
Seed 71 reconstructs the first fixture, 103 the second. These are sufficient
equivalent preparation states, not proof of the historical startup seeds: a
SAV holds no RNG state and intervening shell operations may consume draws.
Without a SAV, roster creation uses seed 0 and the requested seed is applied
immediately before preparation.

## Worker and file boundary

The worker uses the shared private mapper `invitro:image_mapping` (a small
textual-header library): DBSIM's mapper body, messages, hash, image size and
FPU initialization after macro specialization, with the CW3220 memcpy/memset
assembly shared directly. No existing preparation code is duplicated and no
`formats/` change is needed.

The worker executable uses base `0x10000000`; the checked VSHELL image is at
`0x00400000`. It never calls the PE entry point, loader callbacks or original
DLLs. All 133 IAT slots initially point to named fatal traps; exactly the
imports observed in the traced sweeps are bound to own memory I/O, allocator
and C string/memory adapters (15 for preparation; slot mode adds `_sprintf`,
the prefs loader `_memset`/`_memcpy`). Original assertion entry `0x44e560` is
changed in private memory only, after hash and 0x55 prologue checks, to a
fatal breakpoint that prints caller information. Optional tracing places and
restores private breakpoints and single-steps original instructions
(`ES2_SHELL_TRACE=1`; `exec` keeps only the executed-address set); it emits
addresses only. No EXE byte is changed on disk. No Win32 UI, process-launch,
audio or graphics import is forwarded.

| Import | Behaviour |
| --- | --- |
| `_open`, `_close`, `_read`, `_write`, `_lseek`, `_remove` | Virtual descriptors, separate positions, bounded buffers, EOF/seek/truncate handling; no host output path opens. CD/SAV inputs are read-only. Only four recognized output names can be written or removed. |
| `_calloc`, `_free` | Private bounded allocations with preceding size metadata; original ownership/cleanup runs. |
| `_memcpy`, `_memset` | Shared CW3220 register-compatible stubs (destination EAX, count EDX, zero ECX); not host CRT replacements. |
| `_memmove`, `_strcpy`, `_strcat`, `_strlen`, `_strchr` | Host C adapters for the original trusted format/path operations. |
| Everything else, including `_errno` and `__ErrorExit` | Fatal named trap. Error-path errno-thunk semantics are not emulated. |

Original stream constructors, destructors and methods execute unchanged. Read
method `0x403985` reaches `_read` at `0x4039c0`; seek `0x40370b` reaches
`_lseek` at `0x403741`; writer dispatch resolves to `0x44f354`. `0x44ed84`
opens the SAV input stream, `0x44edbc` closes it. The VFS serves archive
members as successful loose-file opens, so original VOL scanning and
decompression are bypassed at the import boundary; `formats/vol` performs the
archive decoding. Mounted sources use descending VOL priority, ties follow
enumeration. Original cross-archive precedence for differing duplicate
payloads is not established.

| Path opened | Provider |
| --- | --- |
| `{eng,fre,ger}/estext.bin`, `missions.bin` | CD language archives, original catalogue loader |
| `gam/herc_inf.dat`, `gam/career.dat` | CD archives, original readers |
| `input.sav` | Read-only alias for the explicitly supplied SAV; never its companions |
| `{eng,fre,ger}/weapons.bin`, `esnames.bin`; `gam/weapons.dat`, `gam/hercs.dat` | Original default-state loaders, only without SAV |
| `MSN/<selected>.MSN`, `MSN/<selected>.{eng,fre,ger}` | Original preparation and conditional text reader |
| Training preset paths selected through `0x47083c` | Alternate UI path; selected by `--preset-flag`/`--preset-index` |
| `data/script.dat`, `data/MISSION.STR`, `data/mission.var`, `data/player.mec` | Four private memory output buffers |

Every successful virtual read open reports path, size and SHA-256 into the
manifest. Host reads are the checked EXE, CD `VOL/*.VOL`, the optional SAV and
the optional 2,000-byte bank. The worker returns outputs over stdout only
(`ES2P`: length-prefixed JSON metadata, then script, STR, var and MEC buffers,
plus the emitted SAV as sixth block with `--emit-save`); publication is a
separate Python operation.

## Usage and output policy

Run from the main checkout `<es2>` with the output base of
[shell/README.md](../../shell/README.md#tools).

```powershell
Set-Location <es2>
$python = '<es2-dependencies>/python/python.exe'
$base = 'build/bazel/es2-shell'
bazel --output_base=$base build //tools/reference-game/shell-invitro:es2shellprep `
  --repo_contents_cache="$base-contents" --lockfile_mode=error
$worker = "$base/execroot/_main/bazel-out/x86_windows-fastbuild/bin/tools/reference-game/shell-invitro/es2shellprep.exe"

# The output must be new; its parent must exist. Inside a repository it must be
# git-ignored, which es2-data/oracle/launch is.
& $python -B tools/reference-game/shell-invitro/es2shellprep.py --worker $worker `
  --cd-root build/extracted/cd `
  --save build/installs/timing-r111/SAV/GAME_R.SAV `
  --seed 71 --language G --output <es2-data>/oracle/launch/<new-name>

# CD-only state, or add --variables <2000-byte-bank> and/or --save <state>.
& $python -B tools/reference-game/shell-invitro/es2shellprep.py --worker $worker `
  --cd-root build/extracted/cd --campaign 3:9 --seed 0 `
  --language G --output <es2-data>/oracle/launch/<new-name>
```

The frontend checks the destination before starting a worker, rejects
installation paths and any Git-worktree ancestor (including linked worktrees
and junction targets), refuses existing destinations, stages the four files
plus `manifest.json`, then renames the new directory. A path inside a
repository is accepted only if `git check-ignore` reports it ignored;
es2-data ignores `/oracle/launch/`, so sealed sets go directly into the
canonical launch area ([oracle_suites.md](oracle_suites.md#published-loadout-sets)).
The schema-1 manifest matches `SuiteLaunch.psm1`:
`schema,name,utc,files[{file,size,sha256}],provenance`. Provenance identifies
`generated-vshell`, the original and worker hashes, worker executable, PID and
actual Windows creation time (plus full-resolution FILETIME ticks), arguments,
state choices and resource hashes.

`.bazelrc` sets `--lockfile_mode=error` and the data root for tests.
Real-data tests print SKIP when `ES2_DATA_ROOT` is unset. The tests of the
worker itself use synthetic data: every truncation of a synthetic SAV and
packet, invalid counts and indices, memory stream truncation/gaps/EOF/overflow,
forbidden output paths and manifest serialization; `real_test` performs the two
byte comparisons below. `Read-SuiteLaunch` accepts the manifest of this
frontend.

## Byte-exact acceptance

Both cases compare complete byte arrays in memory, not hashes alone. No
expected output is used as a template, patched, or passed into the
preparation.

| Case / file | Size | SHA-256 |
| --- | ---: | --- |
| C1_01 script.dat | 12286 | `61efab9bedce8e1a45b484f6fd5157ba0e5d94629ad5881e7ac0720d24a135bb` |
| C1_01 MISSION.STR | 939 | `4c359b61fd35bbf9cd3d2cac21e79f39a23bd48ec167ff6049e19d3de14584f9` |
| C1_01 mission.var | 2000 | `20c8eb568dc09fb2f9f3a64a231264b70d31dd1c225709b146b829993d74dbfc` |
| C1_01 player.mec | 215 | `f669d1250486b6e1e3ff9ed18370f0bc14cdc60793fed8caaf4b49cb78174984` |
| DEMO script.dat | 4514 | `dd28822315105ec3054aa54609c13cada1aaef5871f475163b98ee439641cff9` |
| DEMO MISSION.STR | 882 | `12a693e44050085648cd7f256e2ad48b5bff67193bcb2fcfc57e4b08283ba6fb` |
| DEMO mission.var | 2000 | `13cedfdea94b642bca3976ee8798f5bb038d19fda9e706302c62912a91fa62c4` |
| DEMO player.mec | 151 | `ec5b87a6cdaa1e0d9bf87510c9d99a8db114eac78874b213fc064ce02d30ce68` |

C1_01: `GAME_R.SAV`, seed 71, German, mode 1, difficulty 0, training bytes all
zero, compared with the four files of `timing-r111/DATA`. DEMO: `GAME_T.SAV`,
seed 103, German, mode 0, training bytes `1,1,0,0,0`; CAREER position 0:8 is
`MSN/DEMO.MSN`. The companions script11/missn11/player11 are the reference;
there is no numbered `.var`, so the expected variable bank is the 2,000-byte
span of GAME_T at offset 2,300. Matching proves sufficient state, not the
historical pairing of a SAV and companions.

All 61 CAREER entries prepare with the original CD default state, zero bank,
seed 0, German, inferred mode and zero options, without assertion, fault or
import trap; the order comes from original `0x412f2a`. `audit.py --sweep`
records each mission's position, success, the four sizes and full hashes and
the trap field (no launch bytes). An instruction-traced second sweep, a sweep
at seed 127 with a synthetic bank whose words are all 57, and English and
French C3_10 preparations also complete. Seed 72 changes the C1_01
script/variables; mode-zero DEMO discards a synthetic input bank and still
matches all four reference files.

```powershell
& $python -B tools/reference-game/shell-invitro/audit.py --worker $worker `
  --cd-root build/extracted/cd --sweep `
  --report build/claude/acceptance-sweep.json
```

### Omicron identification

At `0x444a12/19` the sector UI loads the group and subtracts one;
`0x444af5..0x444b02` looks up catalogue entry `group - 1 + 0x77`. German
ESTEXT index 121 (`0x79`) is Omicron, at member offset `0x7d0`; ESTEXT
SHA-256 `fc2821fc440dc712411ac88f2d48872641281abc63c9e15cb3b8068ecafab166`.
CAREER group 3 is therefore Omicron, C3_01 through C3_10 (indices 0..9).
C3_10's conditional debrief names the sector in all three languages; German
C3_10 member SHA-256
`3d3d70493ab15ce002590aeea7ce90ddb783dc97e0ae6cf5a2cb77c4222340be`, debrief
record ID 121, condition 32, local text offset `0x32a`. The sector name alone
identifies no unique mission.

## Differential sweep

`sweep_compare.py` compares the worker with the own preparer (`shell/`,
`es2prep_core`) on the four buffers plus the returned 114-byte RNG and 60-byte
reset-selector state. The own preparer builds CD-default player, roster,
hangar and availability state and applies the mission equipment itself; it
does not execute VSHELL and reads its runtime tables from the hash-checked CD
EXE (see the [default-state contract](../reverse-engineering/vshell_preparation.md#default-state-and-mission-equipment)).
The original runs in the private mapped worker.

- Independent requests restore their state; retained A/B requests carry each
  implementation's equipment plus A's returned bank/RNG/selectors.
- Every request starts with empty output sinks. Stale tails of files opened
  without truncation on a real disk are not emulated.
- The original batch is bounded to 128 requests (64 for retained pairs) to
  bound loader allocations. An original assertion terminates the worker; it is
  recorded and the remaining independent requests resume in a new one. Only a
  matched bounded rejection at `0x41ce61` is classified outside the contract;
  other failures count as mismatches.

Run from the main checkout after the build command above and
`//shell:es2prep_core`:

```powershell
$python = '<es2-dependencies>/python/python.exe'
$bin = 'build/bazel/es2-shell/execroot/_main/bazel-out/x86_windows-fastbuild/bin'
& $python -B tools/reference-game/shell-invitro/sweep_compare.py `
  --core "$bin/shell/es2prep_core.exe" `
  --worker "$bin/tools/reference-game/shell-invitro/es2shellprep.exe" `
  --cd-root build/extracted/cd `
  --save build/installs/timing-r111/SAV/GAME_R.SAV `
  --save build/installs/timing-r111/SAV/GAME_T.SAV `
  --profile coverage --report build/claude/coverage.json
```

Profiles: `tiny`, `baseline`, `coverage`, `retained`, `seed-initializer`,
`loadout`. Reports are JSON plus Markdown with all six hashes per successful
case and the first differing offset/field per mismatch; no original buffers
are published. Source selection, mission, language, raw controls, seed and bank
are attached to each row. `--config <JSON>` selects a Cartesian matrix (plus
its zero-input baseline), for example:

```json
{"missions":["C1_01","DEMO"],"languages":["ENG","FRE","GER"],
 "modes":[0,1],"state_sources":["cd-defaults","GAME_R","GAME_T"],
 "seeds":[0,127,32767,32768,32769,65535],"difficulties":[-32768,0,32767],
 "training_options":[[0,0,0,0,0],[1,1,2,8,255]],
 "preset_flags":[0,1],"preset_indices":[3,4],
 "banks":["zero","GAME_R","GAME_T"],"authored_comparisons":true}
```

`bank_files` adds explicit 2,000-byte banks. All controls are raw; training
option labels are not inferred. The worker frontend accepts unsigned raw
difficulty/index words; the private batch also accepts signed forms.

| CD-default selection, seed/bank zero | ENG | FRE | GER |
|---|---:|---:|---:|
| Group-zero mode 0; other groups mode 1 | 61/61 | 61/61 | 61/61 |
| All mode 0 | 61/61 | 61/61 | 61/61 |
| All mode 1 | 59 equal + 2 assertions | 59 equal + 2 assertions | 59 equal + 2 assertions |

Each equality covers script, STR, var, MEC, RNG and reset state; both SAV
sources with zero bank give the same fixed-mode results. TRAIN1/TRAIN2 in mode
1 assert at `0x41ce61` (condition zero in `src/msn_gen.cpp:2005`) and have no
four-file result; the safe library rejects the missing accepted pretext
transactionally. C1_01 (seed 71/GAME_R) and DEMO (103/GAME_T) stay byte-exact
including MEC.

| Profile / axis | Cases | Exact | Outside contract | Mismatches |
|---|---:|---:|---:|---:|
| Baseline: 61 x 3 languages x 2 modes x 3 sources | 1,098 | 1,080 | 18 | 0 |
| Seed axis | 43,623 | 43,623 | 0 | 0 |
| Difficulty axis | 1,998 | 1,998 | 0 | 0 |
| Two SAV-bank axis | 666 | 666 | 0 | 0 |
| Nine preset files | 1,647 | 1,647 | 0 | 0 |
| Preset predicate boundaries | 732 | 732 | 0 | 0 |
| Training header bytes | 549 | 549 | 0 | 0 |
| Authored comparison boundary banks | 21,933 | 21,933 | 0 | 0 |
| Coverage total (includes baseline) | 72,246 | 72,228 | 18 | 0 |
| Retained A/B preparation steps | 2,151 | 2,133 | 18 | 0 |
| Direct original seed initialization | 1,188 | 1,188 | 0 | 0 |
| C1_01/DEMO configurable cross-product + boundaries | 18,972 | 18,972 | 0 | 0 |
| Combined reports (baseline counted once) | 94,557 | 94,521 | 36 | 0 |

Counts across reports include repeated input combinations. Coverage uses all
seeds 0..127 plus 32767, 32768, 32769, 65535; difficulty -32768, -1, 0, 1, 2,
3, 32767; zero and both SAV banks; all nine c4 presets and both predicate
branches. Every authored type-0 variable comparison contributes
literal-1/literal/literal+1 (signed-word wrap) and -32768/32767 banks,
deduplicated by mission/variable/value; each bank changes one variable from
zero. Beyond the baseline, these axes use German and omit group-zero mode-1
cases. Retained runs cover three languages, both modes and three sources,
seed 127 and selectors containing 999, 50, 0, -1; A goes to the next CAREER
mission, skipping TRAIN1/2 in mode 1. TRAIN3 can also lack a pretext at that
seed: nine failed A and nine failed B preparations are the 18 retained
rejections.

The `seed-initializer` profile uses C1_01 and all 132 seeds x three languages
x three sources. It calls original `0x465cbc` through one-shot `--seed`
without injecting own RNG bytes and compares all six results; SAV sources keep
their own banks. The other profiles supply identical reconstructed RNG
states. The JSON configuration above ran as 15,552 Cartesian cases, 3,384
comparison-boundary cases and 36 baseline cases, all exact.

Limits: the matrix is not the Cartesian product of every axis. Not covered:
non-German comparison/difficulty/preset interactions outside the C1_01/DEMO
cross-product, group-zero mode-1 seed axes, longer state sequences, arbitrary
SAV/loadout mutation, condition-parent paths and raw modes other than 0/1.
The frontend bounds c4 to 0..8 even when disabled (the original skips its use
on that branch); preset flags are byte-valued zero/nonzero controls, not
arbitrary dwords. Boundary banks attempt inputs; preparation can overwrite
reserved words or suppress their records. The fixture seeds reconstruct
outputs, not historical user actions.

## SAV emission and loadout sweep

Two worker options take loadouts as input; neither starts the game or changes
a file on disk.

- `--emit-save` calls the original SAV writer sequence (`0x40e9d2..0x40ea81`:
  inventory `0x4127f3`, roster `0x41023a`, player/team `0x410791`, hangar
  `0x410c7c`, nine-word bank `0x411f78`, tally, bank, final fields) through the
  original write stream `0x44e9d8` into a private fifth memory sink, after the
  state is restored (or built from CD defaults) and the explicit position and
  bank are applied, before preparation. The selection writer `0x413095` also
  copies the old launch companions, so its nine proven spans are written
  directly through the same stream. The `ES2P` packet gains a sixth block;
  `emitted_save` in its metadata announces it. `--emit-save-after` emits the
  SAV of the state after the preparation instead.
- `--player-name` (1..29 bytes) and `--player-rank` feed the original
  new-career player constructor `0x41072b` (defaults "In vitro" and 0).

Restoring the real saves of `timing-r111` (read-only) with the original
readers and writing them with the original writers reproduces GAME_0, GAME_1
and GAME_T byte for byte, and GAME_R without its ten opaque trailing bytes.

`sweep_compare.py --profile loadout` builds every SAV with the own
`formats:sav` writer (through the private core), prepares it with `shell/` and
with the original worker in batch mode, compares the four files plus returned
RNG/reset state, and requires the SAV emitted by the original after restoring
to equal the own SAV. Player plans: for each of the nine Hercs, rotating
variants that place every hangar-legal item on every slot whose ARM record
admits it at least once, plus a sparse and an empty bay (184 plans); then each
item ID 1..32 on every slot of the Ogre with the hangar rules off and explicit
secondary words for odd IDs (32 plans). Case *i* adds a team of *i* mod 4
members (roster group (*i*+*k*) mod 3, varying pilot index, hangar-legal Herc
loadouts), zeroes the bank for every fifth case and uses explicit all-one or
all-zero flags for two of seven. The profile then runs the CD-default SAV
comparison (`default-sav`).

```powershell
& $python -B tools/reference-game/shell-invitro/sweep_compare.py `
  --core "$bin/shell/es2prep_core.exe" `
  --worker "$bin/tools/reference-game/shell-invitro/es2shellprep.exe" `
  --cd-root build/extracted/cd `
  --save build/installs/timing-r111/SAV/GAME_R.SAV `
  --save build/installs/timing-r111/SAV/GAME_T.SAV `
  --save build/installs/timing-r111/SAV/GAME_0.SAV `
  --profile loadout --report build/claude/sweep-loadout.json
```

| Axis | Cases | Equal (incl. SAV round trip) | Mismatches |
|---|---:|---:|---:|
| CD-default base, German, 19 missions | 4,104 | 4,104 | 0 |
| GAME_R / GAME_T / GAME_0 base, two rotating missions each | 1,296 | 1,296 | 0 |
| ENG and FRE, one rotating mission | 432 | 432 | 0 |
| Seed (*i*·4099+1) mod 65536, one rotating mission | 216 | 216 | 0 |
| CD-default SAV vs original writers (3 languages × 4 names × 4 ranks × 4 positions) | 192 | 192 | 0 |
| Total | 6,240 | 6,240 | 0 |

Missions: C1_01, C1_04, C1_05, C1_06, C2_09, C3_01..C3_10, C4_01, C4_08,
C5_02, C5_07. The 6,048 loadout cases cover all nine Hercs as player and as
team member; every item ID 1..32 on the player (896 cases outside the hangar
rules), the 26 hangar-legal IDs on team members; requested team sizes 0..3,
effective sizes 0..3; no SAV build error, no original assertion.

Limits: batch mode injects the RNG state computed from the seed; published
suites use the one-shot worker with the original seed initializer. The matrix
is not a Cartesian product. A matching preparation and SAV round trip show
that the original accepts and prepares the SAV exactly as `shell/` does; they
do not show that the VSHELL UI could produce the loadout (only the
[hangar rules](../reverse-engineering/vshell_preparation.md#hangar-rules-of-the-vshell-ui)
are checked) or that DBSIM runs it.

### Published suites

`shell/tools/es2loadout.py` proves each set with the one-shot worker
(`es2shellprep.py --save <generated SAV> --campaign G:P --mode 1 --seed S
--emit-save`), i.e. through the original seed initializer and the original SAV
readers, and seals it only if everything matches. The sets and their contents
are listed in [oracle_suites.md](oracle_suites.md#published-loadout-sets).

## Results path

The worker runs the original results path in addition to preparation
(contract: [results path](../reverse-engineering/vshell_preparation.md#results-path)).

- `--results FILE --mission-var FILE --save SAV [--mode] [--repair-option N]
  [--restock-option N] [--rng-state FILE|--seed N] [--campaign G:I]`: after
  the start-up loaders (catalogue, HERC_INF, `gam\damage.dat` `0x413c4c`,
  WEAPONS `0x4125e8`, CAREER) and the SAV restore, the worker sets mode, the
  two option bytes and the RNG state, then calls `0x40f09f`. The returned
  `mission.var` and `results.dat`, and the `MISSION.STR` that `0x418014`
  writes during the same run, are served from memory sinks on read.
- `0x40f09f` first calls `0x40f011`, which restores the returned
  `data/mission.var` (2,000 bytes at `0x40f06c..7d`), then opens
  `data/results.dat` at `0x40f0c6`, reads outcome and tally into
  `0x482bcd/cf`, updates the total `0x482bd8` and writes the outcome to
  variable zero at `0x40f146`. Results alone are insufficient: the returned
  variables are a separate input. `0x4133eb` selects the debrief before
  incrementing the mission at `0x41342d`, wrapping the group at
  `0x413452/45b`; later it calls `0x4139f6`.
- The eleven UI/disk entries called directly by `0x40f09f` (`0x402266`,
  `0x4077ee`, `0x407b62`, `0x407c1b`, `0x40e933`, `0x40f904`, `0x413305`,
  `0x41e8c0`, `0x41e98c`, `0x41f93a`, `0x44d0e7`) are entry-byte checked and
  replaced in private memory by breakpoints; the handler records the call and
  its first three arguments and returns 0. The static closure shows no other
  UI, process or file effect. The return path is read from these calls:
  `0x44d0e7(3)` player lost, no `0x40f904` other mode, `0x40e933(10,0)` return
  1, `0x44d0e7(0)` return 0, `0x413305` return 2.
- Afterwards the original SAV writers emit the state (as `--emit-save`). The
  `ES2R` packet holds JSON metadata (position before/after, final word, total,
  lost count `0x482bd3`, debrief record `0x484160`/`0x48415e`/`0x48419e` with a
  sentinel showing whether `0x4139aa` ran, RNG state, UI calls) and three
  blocks: SAV, `MISSION.STR`, debrief text (`0x46fc68`).
- `--results-batch` takes one control line (mode, options, position or -1) and
  four hex lines (SAV, mission.var, results.dat, RNG) per request. Each
  request restores a snapshot of the DATA section (`0x46c000`, 0x172000 bytes)
  taken after start-up, so every request sees a freshly started VSHELL; frees
  are deferred for the batch. `--overlay NAME FILE` (up to four) shadows a CD
  path with a synthetic test file; the portable core accepts the same pairs
  after `--results`.

`results_compare.py` builds every case from the own SAV builders (CD-default
career plus loadout plans, then field edits in a small Python SAV model),
synthetic `mission.var`/`results.dat` and the original seed rule, and compares
SAV bytes, position, return path, final word, total, lost count, debrief
record, `MISSION.STR`, text and RNG state. Only rejections whose original
failure site is the one named in the contract count as outside the contract.

```powershell
& $python -B tools/reference-game/shell-invitro/results_compare.py `
  --core "$bin/shell/es2prep_core.exe" `
  --worker "$bin/tools/reference-game/shell-invitro/es2shellprep.exe" `
  --cd-root build/extracted/cd --random-cases 2000 --hook-cases 48 `
  --synthetic-missions 60 --report build/claude/results-full.json
```

| Axis | Cases | Equal | Matched rejections | Mismatches |
|---|---:|---:|---:|---:|
| Every CAREER position × outcome 0/1 × ENG/FRE/GER | 366 | 318 | 48 (TRAIN1..8, `0x41d925`) | 0 |
| Signed outcome words at group ends | 72 | 72 | 0 | 0 |
| Hooks (1,3)/(1,6), vehicle 8, bay 7 | 144 | 124 | 20 (`0x412314`) | 0 |
| Random state/results/options/modes | 6,000 | 5,968 | 32 (`0x412314`) | 0 |
| Repair budget = cost, ±1 | 138 | 138 | 0 | 0 |
| Authored list-1 comparison boundaries (debrief) | 3,507 | 3,507 | 0 | 0 |
| Invented MSN/text overlays (debrief) | 480 | 447 | 33 (null text pointer) | 0 |
| **Total** | **10,707** | **10,574** | **133** | **0** |

A run with `--seed 7 --random-cases 600 --hook-cases 12 --synthetic-missions
10 --skip-debrief-comparisons` covers team count 0: 2,459 cases, 2,343 equal,
116 matched rejections (47 of them the divide fault at `0x40f4eb`), 0
mismatches.

Limits: the SAVs come from the own builders, not from a played campaign;
`results.dat` bytes are synthetic, not DBSIM output. The trapped UI functions
do not run, so their later UI effects are not compared. The invented MSN files
exercise reader rules the CD missions do not reach; they are test inputs, not
claims about CD content.

## Slot mode

The worker runs VSHELL's own save-slot, load and new-career functions on a
memory disk (`main.c`, `run_slots`; client `slots_oracle.py`).

- **Inputs.** `--file NAME PATH` puts a file on the disk under the name VSHELL
  forms (`sav\gamefile.str`, `sav\GAME_R.SAV`, `sav\script10.dat`,
  `data\script.dat`, ...; compared without case, either slash). Read opens of
  `sav...`/`data...` are served from the disk only (a missing file fails as on
  a real disk); CD members still come from the VFS. Writes create or update
  disk files with the original open flags (`0x8102` keeps old bytes, `0x8301`
  truncates). `_sprintf` is bound in this mode (the companion names of
  `0x413095`/`0x4131e3`).
- **Start-up** as game init `0x40e736` for the files involved: the slot table
  `0x40e380` (from `sav\gamefile.str`), DAMAGE `0x413c4c`, WEAPONS `0x4125e8`
  (catalogue, HERC_INF and CAREER as in every mode). Then optionally
  `--save SAV` (the original readers) with the loaded flag `0x4826ee` := 1
  unless `--not-loaded`, the mode word (`--mode`, default 1) and the RNG
  (`--rng-state` or `--seed`). The slot statistics `0x4309f0` run only through
  the `stats` operations.
- **Operations**, in order (`--op`, up to 512): `new:RANK:NAME` ->
  `0x40e8a5(NAME, RANK)`; `load:K:FLAG` -> `0x40eaaa(K, FLAG)`;
  `save:K[:TEXT]` -> `0x40e933(K, TEXT or NULL)`; `practice:I` -> `0x479c54`
  := I. Two entries are trapped like the results path's UI entries (call
  recorded, returns 0): the DEBUG dialog `0x413305` at the end of a new game
  and the map object `0x40e75f` of a load. The operations of the other
  screens are listed in the
  [tool README](../../tools/reference-game/shell-invitro/README.md).
- **Output** `ES2D` (with `--pipe`): metadata (loaded flag, mode, final word,
  salvage total, CAREER position, RNG state, the slot table before and after
  the operations, trapped calls), the whole disk (name and bytes per file),
  and with `--emit-save` the SAV of the final memory state from the original
  writers. Without `--pipe`, hashes only. `ES2_DISK_TRACE=1` prints every disk
  write (offset, length).

The worker starts from a restored SAV and a restore reverses every item list;
to give it a memory state `S` the tests pass `es2_shell_loaded_save(S)` (an
involution on writer images). Contracts:
[vshell_ui.md](../reverse-engineering/vshell_ui.md#9-save-slots-and-new-career).

| Check (`//shell:slots_real_test`) | Compared with | Result |
| --- | --- | --- |
| Save `0x40e933`: `GAMEFILE.STR`, slot SAV, companions (3 languages, slots 0..11, modes 0/1, texts, old files) | `es2prep_core --slot-save` | equal |
| Load `0x40eaaa` + writers | `es2_shell_loaded_save` | equal |
| New game `0x40e8a5` (mode 1): SAV, RNG, salvage, final word, position | `es2_shell_new_game` | equal |
| Preparation + writers (`--emit-save-after`), 50 missions × 3 SAV sources × 2 seeds | `es2_shell_prepared_save` | equal |
| New game in mode 0 | `es2_shell_new_game` | differs: the original builds no HERCS hangar (4,134 against 4,678 bytes); not covered |

## Service-bay operations

Slot mode takes further operations so that the service bay's WEAPONS screen
and the Rock & Roll launch files run as VSHELL's own code (`main.c`,
`run_slots`; at most 512 `--op` per run):

| `--op` | Original call |
| --- | --- |
| `mount:B:S:I` | `0x411b10(record of bay B, S, I)`, the mount the WEAPONS click calls through `0x43e2d4`; refused when bay B has no record (`[0x482ba7 + 4B]` = 0) |
| `guidance:B:S:M` | bay selection `0x482bc9` := B, slot selection `0x476658` := S, `0x43e346(M)` (the guidance buttons' store) |
| `rebalance:O` | option byte `0x4825c9` := O, `0x412a37` (leaving WEAPONS, REPAIR or BUILD) |
| `arm:B:S:I` | the ARM loader `0x41508f` once, the selections (S = 255: none), `0x41501f(I)`; the answers are listed in the metadata as `"arm": [[B, S, I, answer], ...]` |
| `launch` | `0x40f68c`: `data\mission.var` and `data\player.mec` land on the memory disk |

The click rule `0x43fdac` around the mount is UI code (it writes widget state
through pointers the harness does not build) and is not run; the tests
translate every click the own rule applied into `mount`.

Comparisons ([vshell_ui.md](../reverse-engineering/vshell_ui.md#10-service-bay-and-weapons)):
`//shell:bay_real_test` (91 worker runs) finds 2,277 ARM answers, 57
launch-file states (8 against `0x40f68c`) and 74 operation sequences (356
mounts, 97 guidance stores, 62 rebalances, 30 launches) equal;
`//shell:menu_real_test` compares the own menu's `SAV` and `DATA` files after a
WEAPONS session and after a mission with a changed loadout;
`//shell:chain_real_test` with `ES2_CHAIN_VERIFY` replays a menu campaign's
service-bay steps and launch files through these operations.

## Open points

Open:

- runtime parity of the statically resolved archive/decompression alternatives
  and all heap-alias/global-read effects outside the supported constructed
  state;
- original archive precedence with conflicting duplicates, errno/error
  behaviour, semantic names of all SAV fields, exhaustive equivalence for all
  bank/seed combinations, wider raw modes;
- application of results beyond the sweeps above: UI effects of the trapped
  entries, and the choice of a particular Omicron mission/equipment for a
  suite.

Hypotheses (not historical claims): the reconstructed seeds and option bytes
were the actual original startup values; the GAME_R trailer came from an older
longer, untruncated save; the default state represents a desired owner
loadout.

## Callee inventory

Each row is `function -> direct, observed virtual or constructor-resolved
callee`. Imports are named. The graph is generated from the checked binary by
`boundary.py`; assertions terminate at the worker's trap and their UI
implementation is excluded.

```text
00402d2b -> 0044ef10, 0044efdc, 00450aec, 00450c18, 0046b9d0
00402f24 -> 0044e560, 0044e8c8
00403065 -> 004032b1, 0044e560, 0044e8c8
004030e4 -> 0040323e, 00408760, 0044e8e4, 0044f9a8, 00453cb0, 0046b574, 0046b66e
0040323e -> 0044f664, 0044fa5c, 0046b98e
004032b1 -> 0040323e, 004040e9, 004086b7, 0044e560, 0044f5a8, 0044f62c, 0044f8d8, 0044fa04, 0046b574, 0046ba06
004036d4 -> 0046ba18
0040370b -> 00403929, 0044e560, 0044ef10, 0044f62c, 0044f664, 0044f6cc, 0044fa04, 0044fa5c, 0044faac, 0046ba18
00403929 -> (leaf)
00403985 -> 0040370b, 00403929, 0044e560, 0044eef4, 0044efdc, 0044f79c, 0044fb48, 0046ba00
004040e9 -> 00402d2b
00407db3 -> (leaf)
00407de1 -> (leaf)
00407e20 -> 00407db3, 00407de1
00407e67 -> (leaf)
00407e97 -> 00407de1, 0044e560, 0046b9c4
00408013 -> 00407db3, 00407de1, 00407e20, 0044e560
0040869c -> 0046ba84
004086b7 -> 00407e67, 00407e97, 0040869c, 0046b9c4
0040870a -> 00407e67, 00407e97, 0040869c, 0046b9c4
00408760 -> 00407db3, 00408013, 0046ba66
004087a1 -> 00407db3, 00408013, 0046ba66
004087f0 -> 0044e560
0040883a -> 00403985, 0040870a, 0044e560
004088d4 -> (leaf)
00408bb5 -> 00403065, 004030e4, 0040323e, 004086b7, 00408760, 0040883a, 004088d4, 0044e560, 0046b574, 0046b9d6, 0046b9fa
0040d99d -> 0046b9d6, 0046b9fa
0040ec80 -> (leaf)
0040ec8f -> 0041094b, 0041098a, 0044e560
0040eef1 -> 0046b9c4
0040ef06 -> 0040eef1, 00465d2c
0040ef83 -> 0044e560, 0044f100, 0044f138, 0044f354, 0046b574
0040f68c -> 0040d99d, 0040ef83, 004108fa, 00410cdb, 00412877, 0044e560, 0044f100, 0044f138, 0044f1f4, 0044f354, 0046b574, 0046ba42
0040ff80 -> 00465d2c, 0046b9c4
00410000 -> 00465d2c
00410045 -> (leaf)
00410055 -> 00408760, 004087f0, 00408bb5, 0040ff80, 00410000, 00410045, 004102fc
00410173 -> 0041033b
0041029b -> 00410520, 0044efdc
004102fc -> 0041033b, 0046b9fa
0041033b -> (leaf)
00410520 -> 0044efdc
00410704 -> (leaf)
0041072b -> 00410173, 004102fc, 00410704, 00465d2c
004107dc -> 00410520, 0044efdc
004108e2 -> (leaf)
004108fa -> (leaf)
00410923 -> 0041098a
0041094b -> (leaf)
0041098a -> 004108fa, 0041094b
00410aaf -> 00408760, 00411408
00410b11 -> 00402f24, 004030e4, 0040323e, 004032b1, 00403985, 004086b7, 00410aaf, 0041139e, 0041149d, 0044e560, 0046b574
00410cdb -> 0044f354
00410e2e -> 004086b7, 00410aaf, 0041139e, 004116e6, 0044e560, 0044efdc, 0046b574
0041118c -> 0044e560
00411354 -> (leaf)
00411378 -> 00411354
0041139e -> 004121ac, 004122e1
00411408 -> 00408760, 004122e1
00411455 -> 004087f0
0041149d -> 00403985, 004086b7, 00411354, 00411378, 00411408, 00411455, 00411ffc, 0041205a, 0044e560, 0046b574
00411602 -> 00411354, 00411378, 00411408, 00411455
004116e6 -> 004086b7, 00411408, 00411455, 00411ffc, 004120ca, 0044e560, 0044efdc, 0046b574
00411bea -> 004086b7, 00411ffc, 004120a3, 004122e1, 0044e560, 0046b574
00411e40 -> 00403065, 004030e4, 0040323e, 00403985, 0044e560, 0046b574
00411fad -> 0044efdc
00411fd8 -> (leaf)
00411ffc -> (leaf)
0041205a -> 00403985, 00411fd8
004120a3 -> 00411fd8
004120ca -> 0044efdc
004121ac -> (leaf)
0041229c -> (leaf)
004122e1 -> 0041229c
0041237b -> 00403985, 004125c1
004123df -> 004086b7, 00411ffc, 004120ca, 00412521, 004125c1, 0044efdc, 0046b574
004124eb -> 00408760, 004125ab
00412521 -> 004086b7, 0044e560, 0046b574
004125ab -> (leaf)
004125c1 -> 00408760, 004124eb
004125e8 -> 00402f24, 004030e4, 0040323e, 004032b1, 00403985, 004086b7, 004087f0, 00408bb5, 00411ffc, 0041205a, 0041237b, 00412521, 00412761, 0046b574
00412761 -> (leaf)
00412781 -> 004123df, 00412761, 0044efdc
00412877 -> 0044f354
00412e70 -> (leaf)
00412e88 -> 00403985, 0040870a, 004087f0, 0044e560
00412f2a -> 00402f24, 004030e4, 004032b1, 00403985, 00408bb5, 00412e88, 0044e560, 0046b574, 0046b898
004134f2 -> (leaf)
00415ff4 -> (leaf)
004160a0 -> (leaf)
00416107 -> (leaf)
00416168 -> (leaf)
004161cf -> (leaf)
00416236 -> (leaf)
0041629d -> (leaf)
00416304 -> (leaf)
0041636b -> (leaf)
004163da -> (leaf)
00416441 -> (leaf)
004164ab -> (leaf)
00416511 -> (leaf)
00416580 -> (leaf)
004165e7 -> 00465d2c
00416744 -> 004165e7
00416799 -> 004165e7
004167ee -> 004165e7
00416843 -> 004165e7
0041689a -> 004165e7
004168ef -> 004165e7
00416948 -> 004165e7
0041699d -> (leaf)
00416ac9 -> 004087a1
00416b45 -> (leaf)
00416d8c -> (leaf)
00416e88 -> (leaf)
0041723b -> (leaf)
0041749e -> (leaf)
0041770c -> (leaf)
004178aa -> (leaf)
00417c34 -> (leaf)
00417c7d -> (leaf)
00417cb0 -> 00402f24, 004030e4, 0040323e, 004032b1, 004036d4, 0040370b, 00403985, 0040870a, 004087a1, 004160a0, 00416ac9, 00417c34, 0044e560, 0046b574, 0046b994, 0046b9fa
00418014 -> 0044e560, 0044e95c, 0044ea10, 0044eabc, 0044eafc, 0044eb48, 0044eb64, 0044ec1c, 0046b574, 0046ba5a
0041818b -> 00403065, 004030e4, 0040323e, 004036d4, 0040370b, 00403985, 0040870a, 004087a1, 00415ff4, 004160a0, 00416107, 00416168, 004161cf, 00416236, 0041629d, 00416304, 0041636b, 004163da, 00416441, 004164ab, 00416511, 00416580, 00416744, 00416799, 004167ee, 00416843, 0041689a, 004168ef, 00416948, 0041699d, 00416b45, 00416d8c, 00416e88, 0041723b, 0041749e, 0041770c, 004178aa, 00417c34, 00417c7d, 00417cb0, 0044e560, 00465d2c, 0046b574, 0046ba48
0041b278 -> 00418014, 0044e560, 0044e95c, 0044ea10, 0044eabc, 0044eafc, 0044ec1c, 0046b574
0041c862 -> (leaf)
0041c8b3 -> 004087a1
0041cbb1 -> 00402f24, 004030e4, 0040323e, 004032b1, 004086b7, 0040ec80, 0041118c, 0041139e, 0041149d, 00411602, 00411bea, 0041c862, 0044e560, 0046b574
0041cd61 -> 004086b7, 0040ec8f, 0040ef06, 0040f68c, 004108e2, 00410923, 0041118c, 0041139e, 00411602, 00411bea, 004134f2, 0041818b, 0041b278, 0041c862, 0041c8b3, 0041cbb1, 0044e560, 0046b574
0044e560 -> assertion trap
0044e8c8 -> (leaf)
0044e8e4 -> 00408760, 0044e944
0044e944 -> (leaf)
0044e95c -> 0044e8c8
0044ea10 -> 00408760, 0044e8e4, 0044eabc
0044eabc -> 0046b98e
0044eafc -> 0044eabc, 0044f3f9, 0046ba06
0044eb48 -> 0046ba18
0044eb64 -> 0044ebf8, 0046ba18
0044ebf8 -> (leaf)
0044ec1c -> 0044ebf8, 0046ba2a
0044eef4 -> 0046ba18
0044ef10 -> 0044efa4, 0046ba18
0044efa4 -> (leaf)
0044efdc -> 0044efa4, 0046ba00
0044f100 -> 0044e8c8, 0044f234
0044f138 -> 00408760, 0044e8e4, 0044f1f4
0044f1f4 -> 0046b98e
0044f234 -> 0044f1f4, 0044f3f9, 0046ba06
0044f330 -> (leaf)
0044f354 -> 0044f330, 0046ba2a
0044f3f9 -> (leaf)
0044f5a8 -> 0044e8c8, 0044f60c
0044f60c -> (leaf)
0044f62c -> 0044f664, 00450a60
0044f664 -> 0044f774
0044f6cc -> 0044ef10, 0044f8bc, 00450a60
0044f774 -> (leaf)
0044f79c -> 00450a60, 00450a6b, 0046282c
0044f8bc -> 0044f79c
0044f8d8 -> 0044e8c8, 0044f93c
0044f93c -> 004086b7, 0040870a, 0044fcec, 0046b574
0044f9a8 -> 00408760, 004087a1, 0044e8e4, 0044fa5c, 0044fd2c
0044fa04 -> 0044fa5c, 0044fd6c, 00450a60
0044fa5c -> 0044fc34
0044faac -> 0044ef10, 0044fcd1, 00450a60
0044fb48 -> 0044feb4, 0044ff04
0044fc34 -> (leaf)
0044fcd1 -> 0044fb48
0044fcec -> 0040870a
0044fd2c -> 00408760, 004087a1
0044fd6c -> (leaf)
0044feb4 -> 0044ff88, 004501e8
0044ff04 -> 0044ff88, 0044ffe4
0044ff88 -> 004540a9
0044ffe4 -> 004540a9
004500b4 -> 0046ba4e
004501e8 -> 004500b4
00450a60 -> (leaf)
00450a6b -> 0044efdc
00450aec -> 0046b994, 0046ba12, 0046ba3c
00450c18 -> 0046b9d6
00453cb0 -> 00408760, 0044e8e4
004540a9 -> 0044efdc
0046282c -> 0044efdc
00465c90 -> 00465d50, 0046ba4e
00465cbc -> 00465c90, 00465ce8, 00465d55
00465ce8 -> (leaf)
00465d2c -> 00465ce8
00465d50 -> (leaf)
00465d55 -> (leaf)
0046b574 -> (leaf)
0046b66e -> (leaf)
0046b898 -> 0040870a, 00412e70, 0046b9dc
0046b98e -> cw3220.DLL!_close
0046b994 -> cw3220.DLL!_strchr
0046b9c4 -> cw3220.DLL!_memset
0046b9d0 -> cw3220.DLL!_stricmp
0046b9d6 -> cw3220.DLL!_strcat
0046b9dc -> cw3220.DLL!__ErrorExit
0046b9fa -> cw3220.DLL!_strcpy
0046ba00 -> cw3220.DLL!_read
0046ba06 -> cw3220.DLL!_open
0046ba12 -> cw3220.DLL!_strrchr
0046ba18 -> cw3220.DLL!_lseek
0046ba2a -> cw3220.DLL!_write
0046ba3c -> cw3220.DLL!_strncpy
0046ba42 -> cw3220.DLL!_remove
0046ba48 -> cw3220.DLL!_memcpy
0046ba4e -> cw3220.DLL!_memmove
0046ba5a -> cw3220.DLL!_strlen
0046ba66 -> cw3220.DLL!_free
0046ba84 -> cw3220.DLL!_calloc
```
