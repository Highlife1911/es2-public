# DBSIM random number sources

DBSIM has a 56-word additive generator with two separately stored streams, A
and B, initialized from one fixed 112-byte table. It does **not** use
Borland's RNG (`_rand`, `_srand`, `__lrand` of CW3220). The `_time` call selects
a demo name; it does not seed the generator. Neither stream is saved, loaded
or seeded from a clock: every DBSIM process starts from the fixed table.

**Hypothesis** covers subsystem names, inferred
function boundaries, runtime reachability that was not measured and replay
sufficiency.

## Evidence and conventions

All VAs use the module's preferred base `0x00400000`. CW3220 addresses are
in **CW3220**, not DBSIM; use DLL-relative RVAs when it is relocated.

| Input | Notes |
|---|---|
| `VER95/DBSIM.EXE` of `build/extracted/cd` (724,512 bytes) | hashes: `data/reference_hashes.txt` |
| `CW3220.DLL` of `build/extracted/cd` (176,128 bytes) | |
| `VER95/VSHELL.EXE` of `build/extracted/cd` (hand-over, [VSHELL's generator](#streams-across-save-load-and-the-vshell-hand-over)) | |
| `llvm-objdump` listing of DBSIM with byte columns (stage-3 listing) | |
| `llvm-objdump` listing of DBSIM without byte columns | |

All 209,972 mapped instruction-byte rows of the stage-3 listing match the CD
executable; the other listing has no byte columns. Neither listing is a
function-boundary authority: linear decoding can consume a function's first
bytes as preceding padding or data. Entries below come from aligned decoding of
the pinned `llvm-objdump`. Call-graph statements follow direct calls only;
indirect and virtual calls are not followed.

DBSIM CODE: file offset = VA − `0x00400a00`, file-backed range
`0x00401000..0x00496bff`. Initialized DATA: file offset = VA − `0x00400e00`.
The two generator objects lie in the zero-filled DATA tail and have no
file-backed bytes.

## Borland RTL generator (not DBSIM's)

DBSIM's import directory is at RVA `0x002cc000`
(file `0x000a6e00`, declared size `0xc9a`). All 53 CW3220 imports are named;
there are no ordinal imports. No `rand`, `srand`, `random`, `lrand` or similar
entry is imported and DBSIM contains no ASCII `rand`/`seed` name, so DBSIM
has **no `srand` call site or RNG IAT slot**. Dynamic or transitive DLL use
is not excluded by these negative results.

CW3220 implements:

| Export / ordinal | DLL VA (RVA) | File offset | Operation |
|---|---|---|---|
| `_srand` / 166 | `0x00414e68` (`0x14e68`) | `0x14468` | stores the 32-bit stack argument to the low state, clears the high state; plain `ret` |
| `_rand` / 165 | `0x00414e80` (`0x14e80`) | `0x14480` | advances the low state, returns bits 16..30 in EAX |
| `__lrand` / 167 | `0x00414e9c` (`0x14e9c`) | `0x1449c` | advances a two-dword state, returns the low 31 bits of the updated high dword |

State: low dword `0x00421e44`, high dword `0x00421e48` (RVAs `0x21e44`,
`0x21e48`; file offsets `0x21244`, `0x21248`), initial bytes
`01 00 00 00 00 00 00 00` (low = 1, high = 0). All literal references to
these addresses in the DLL belong to the three routines; no direct relative
call to them exists in CW3220 (other DLLs are not excluded). `_rand` and
`__lrand` share the low state; `_srand` resets both.

```text
_rand:    low' = (low * 0x015a4e35 + 1) mod 2^32;  result = (low' >> 16) & 0x7fff
__lrand:  S = (high << 32) | low;  S' = (S * 0x000015a400004e35 + 1) mod 2^64
          result = (S' >> 32) & 0x7fffffff
```

`_rand` starts `69 05 44 1e 42 00 35 4e 5a 01` at `0x414e80`; `0x414e8a`
increments EAX, `0x414e8b` stores it, `0x414e90` shifts by 16, `0x414e93`
masks `0x7fff`; the high dword is untouched. `__lrand` loads `0x15a4` and
`0x4e35` at `0x414eac`/`0x414eb1`; the multiply, cross-product and carry
sequence `0x414eb6..0x414ec9` implements the 64-bit form (a static
arithmetic derivation, not a live comparison); `0x414ed0` and `0x414edd`
store both unmasked dwords.

## DBSIM's table generator

### State layout

A stream is a `0x72`-byte object:

| Offset | Meaning |
|---|---|
| `+0x00..+0x6f` | 56 little-endian 16-bit words `T[0..55]` |
| `+0x70` | source index `i`, initialized to 24 |
| `+0x71` | destination index `j`, initialized to 55 |

Stream A is at `0x004d2599` (ends at `0x4d260a`), stream B at `0x004d260b`
(ends at `0x4d267c`). Their offsets from the globals base `0x4d24bc` are
`0xdd` and `0x14f`. They start with identical contents and evolve
independently.

### Core and bounded wrapper

Core `0x00492ee0` (file `0x924e0`) and bounded wrapper
`0x00492f24` (file `0x92524`) use stack arguments and caller cleanup. At core
entry `[esp+4]` is the state pointer; at wrapper entry `[esp+4]` is the state
and the low word of `[esp+8]` the bound.

- **Core.** One call adds `T[i]` into `T[j]` with 16-bit wrap, returns the
  updated word and advances both indices modulo 56 (word load `0x492eea`,
  `66 01 0c 50` at `0x492ef2`, result load `0x492ef6`, index update and wrap
  tests `0x492efa..0x492f1a`). For valid indices EAX is in `0..65535`;
  callers often read AX as signed or mask bits. The output recurrence has lags
  31 and 56. No period or statistical quality is claimed.
- **Wrapper.** One core call at `0x492f2c`, mask `0x7fff` at `0x492f39`,
  signed-division remainder at `0x492f41..0x492f43`. For a positive signed
  16-bit bound `n` the result is `(raw & 0x7fff) % n`: modulo reduction, no
  zero-bound check.

The first eight raw outputs of a fresh stream are
`ea0b 2ab1 91b0 8105 a50a db51 4d01 d996` (static calculation from the table;
checked by the own implementation's check against the original table).

### Initialization, state writes and seed sources

Initializer `0x00492e88` (file `0x92488`) writes
indices 24/55 at `0x492e8f`/`0x492e93` and copies `0x70` bytes from
`0x004a6940` with `_memmove` (thunk `0x496814`, IAT `0x6cc354`) at `0x492e9f`.
The helper `0x492f48` after it is empty. The fixed table is at file offset
`0xa5b40`, length 112; it is identified by offset, not reproduced.

| Initialization call | Containing function | Destination | Bytes |
|---|---|---|---|
| `0x0045cbe9` | `0x0045caf4` | A `0x004d2599` | `68 99 25 4d 00`, then `e8 9a 62 03 00` |
| `0x0045cbf4` | `0x0045caf4` | B `0x004d260b` | `68 0b 26 4d 00`, then `e8 8f 62 03 00` |
| `0x00492ec0` | `0x00492eb4` | pointer from the caller | `e8 c3 ff ff ff` |

`0x45caf4` is the constructor of the globals object `0x4d24bc`; the file
holds a relocated pointer to it at VA `0x4a7aee` (file `0xa6cee`, bytes
`f4 ca 45 00`).

`0x492eb4` resets a supplied object and discards results: the loop calls the
core at `0x492ed0`, then decrements its counter and tests the signed low
word (`0x492ed6..0x492eda`), so counts 0..32767 discard exactly that many
draws. Its other helper `0x492f4d` is empty. Nothing in DBSIM calls or jumps to
`0x492eb4` and no literal function pointer to it exists. **Hypothesis:** an
unused alternate seed/skip interface.

All 80 literal references to the two state base addresses are arguments to
the identified draw and reset calls. No literal address into an object's
interior exists, including HIGHLOW relocations. The only literal reference to
the source table is in the initializer. The established writes are the
startup zero-fill (`0x401011..0x401022`), the initializer's copy and index
stores, and the core's word and index updates; there is **no time-derived
reseed**. Writes through computed aliases, larger structure copies, file
loading or external code are not excluded by the searches (see
[Open questions](#open-questions)).

## Stream initialization at runtime

**Each stream is initialized exactly once per DBSIM
process, A then B, before `WinMain`, by a C++ static constructor.**

1. The entry point `0x401000` zero-fills `0x4a7bfc..0x6cbee7`
   (`0x401011..0x401022`, covering both states) and jumps to CW3220
   `__startup` (thunk `0x4967d8`, DLL VA `0x41a6e0`) with the module table
   `0x49702c`: init records `0x4a79d8..0x4a7b5d`, exit records
   `0x4a7b5e..0x4a7bf9`, main function `0x46538c` (`WinMain`).
2. A record is 6 bytes: a "called" byte, a priority byte, the function. There
   are 65 init and 26 exit records, all with priority `0x20`. In init mode the
   selector `0x41a634` returns the first record not yet called whose priority
   is strictly the lowest (`cmp esi,edi; jge` at `0x41a68d..0x41a694`) and
   marks it (`0x41a6ce`), so equal priorities run in table order.
   `0x45caf4` is record 46 (0-based; the 47th of 65, at `0x4a7aec`).
   `__startup` calls all init records (`0x41a79e..0x41a7b3`) before `WinMain`
   (`call [esi+0x18]` at `0x41a80f`/`0x41a832`).
   [rtl_imports.md](../reference-game/rtl_imports.md) describes the lifecycle.
3. `0x45caf4` clears `+0x00..+0xc2` of the globals object (`_memset` at
   `0x45cb25`), constructs the member at `+0xc3` (`0x48156c`), sets fields,
   then initializes stream A at `0x45cbe9` and stream B at `0x45cbf4`. Its exit
   record `0x45cc0c` destroys only `+0xc3`. Neither touches the streams
   otherwise.
4. The direct-call closure of the 65 init records (133 functions; 56 indirect
   sites not followed) reaches an RNG routine only through these two calls;
   the closure of the 26 exit records (79 functions) reaches none. The only
   other call to the initializer is in the unused helper `0x492eb4`.

**Startup order (call order).** `WinMain 0x46538c` → early parser
`0x45e6cc` (`0x4653d4`) → game main `0x45f160` (`0x465462`) → main parser
`0x45e758` (`0x45f27b`; `-p`/`-D` restore the tape header here) → stage 0
(`0x45f285`) → VOL registration `0x473260` (`0x45f2a8`) → stage 1
(`0x45f2b2`) → **mission load `0x461608`** (`0x45f42b`) → stage 3 (`0x45f432`)
→ main loop `0x45f458` → **first logic frame `0x45f570`** (`0x45f487`).
`0x461608` has a single caller, so one DBSIM process loads exactly one mission
and never restarts one. The VSHELL hand-over happens before all of this:
VSHELL has exited with code 2 and the launcher starts a new DBSIM process
([launcher.md](launcher.md)); only files cross the boundary.

### Draws during the mission load

No direct path reaches a draw from `WinMain`'s calls before `0x45f160`,
`0x45e758`, stages 0, 1 and 3 (no callbacks are registered for them), or the
19 stage-2 callbacks (run at `0x4617b8`). The mission load reaches draws in
this order (static reach; [mission_load.md](mission_load.md#6-rng-during-the-load)
holds the per-mech detail):

| Call in `0x461608` | Chain to the draw | Stream | Condition (static) |
|---|---|---|---|
| `0x4617c8` world load `0x424308` | `0x4252f7` → terrain `0x42789c` → `0x46c754` (`0x427a8a`, `0x427b32`): R `0x46cac0`; → `0x46c4c4` (`0x46c86f`): R `0x46c69d` | A | branches of the terrain loader; `0x46c754`'s site `0x46cac0` is in its text-grid path, which no suite runs, so the draws seen come from `0x46c4c4` (one per block) |
| `0x46182b` script/actor load `0x4253d8` | `0x4259b0` → `0x4178e8` (MECH initial damage): R `0x417976`, `0x4179b1`; → `random_damage 0x417a04` (`0x4179f2`): R `0x417a2e`, N `0x417a48` | A | once per selected authored MECH; MEC actors get none. The damage virtual `+0x74` reached from there adds `vehicle_part` draws, stream-B sound draws included |
| `0x461dc3` `0x46313c` | first `mciSendCommandA` on a direct path (`0x464858` → `0x473c48`) | — | dword `0x49f8ec` ≠ 0 and word `0x4aa728` = 0; the late arming point |
| `0x461df1` rendering frame `0x45fca8` | `0x45fd4f` → panel frame `0x4327ac` → `0x43408c`: R `0x4340d1`, `0x4340ef` | B | word `0x4d2510` = 0; clock-gated |
| `0x461ebc` cockpit startup `0x4328cc` | `0x4328ed` → sound `0x462830`: N `0x46285c` | B | sound entry with more than one variant |
| `0x461f2c` drop sequence `0x45d85c` | R `0x45db50`, once per pass of a clock-bounded loop | B | word `0x4d2510` ≠ 0 |

A raw `E8`/`E9` scan confirms the callers. `0x4253d8`, `0x4178e8`, `0x42789c`,
`0x46c4c4`, `0x45d85c`, `0x4328cc`, `0x4327ac` and `0x43408c` each have one
caller, the one above. `0x46c754` has two call sites, both in `0x42789c`.
`0x417a04`, `0x462830` and `0x45fca8` are also called from the frame code.
Virtual constructors that run during the world load are not followed and may
draw as well.

### Arming and the load's draws

The proxy arms hooks, coverage, probes and counters either at the call of the
mission load `0x45f42b` (early arming, the default; see
[Arming time](../reference-game/live_coverage.md#arming-time) and
[mission_load.md](mission_load.md#13-l0-arming-before-the-load-and-the-after-load-snapshot))
or, with `ES2_EARLY_ARMING=0`, at the first DirectDraw or MCI call. DBSIM
creates DirectDraw at startup only when dword `0x4d255e` (`-Z1`) is set
(`0x46542b..0x46543c`) and the launcher does not pass `-Z1`; on the
direct-call paths the first MCI call is `0x461dc3`.

- **Early arming:** the load's draws are counted by the `hk_rng_*` counters.
  The constructor `0x45caf4` still runs before arming, so an entry probe on
  `0x492e88` reads 0.
- **Late arming:** every draw of the terrain and MECH loads precedes arming
  and is invisible to counters and probes. It is still in the stream state:
  the tick log's first record (counter 0, written after the mission load and
  frame 0) already includes it. Measured arming point: after the load and
  after the drop sequence (see [Live measurements](#live-measurements)).

## Streams across save, load and the VSHELL hand-over

**DBSIM neither saves nor restores either stream. Every
mission starts from the fixed table because every mission runs in a new DBSIM
process.** Live for a mission started from a SAV-derived launch
set ([Live measurements](#live-measurements)).

1. DBSIM has no save or load of its own and never opens `sav\` files
   ([file_io.md](file_io.md)). Its complete write set is `results.dat` and
   `mission.var` (results writer `0x42412c`), `prefs.cfg`, `assert.log`, and
   the tape and dmp files of the [demo path](#demo-path). The writer's absolute
   data references are the mission-variable block `0x4a9e70` and result fields
   `0x4a9e54..0x4a9e98`. The world load reads `mission.var` (`0x7d0` bytes) into
   `0x4a9e70` (`0x424428..0x424436`), far from the streams.
2. No instruction names an address inside either state (the 80-reference scan
   above). Every `fread`/`fwrite` with a fixed buffer uses `0x4a84fc` (dmp
   buffer), `0x49eaa0` or `0x4d20c4` (key queue, item 5); all others use stack or
   heap buffers. Fourteen functions receive or load the globals pointer
   `0x4d24bc`: `0x45caf4`, `0x45ceb8`, `0x45de30`, `0x45f160`, `0x45fa84`,
   `0x45fba4`, `0x45fca8`, `0x461608`, `0x461ff0`, `0x45c8bc`, `0x45c964`,
   `0x45caa4`, `0x438df8` and `0x442e20`. None accesses the streams' offsets
   `+0xdd..+0x1c0` or block-copies over them.
3. **VSHELL has its own copy of the generator.** Saving and loading happen in
   VSHELL (save `0x40e933`, load `0x40eaaa`, copies `0x40da8d`). VSHELL's
   core is `0x465ce8`, its initializer `0x465c90`, its reset/skip `0x465cbc`,
   with the same 112-byte table at VA `0x47f850`. It has two private states,
   `0x482409` and `0x48110a`, whose only literal references are the 15 draw
   and initialize calls, so VSHELL saves neither. A SAV contains no RNG state
   ([shell_invitro.md](../reference-game/shell_invitro.md)). The second state
   is the material choice of the map's height grid (`0x42959c` DBA path,
   `0x429b10` text path); it has no visible effect
   ([vshell_mission_map.md](vshell_mission_map.md)).
4. **What crosses from VSHELL (static, use hypothesis).** VSHELL resets
   `0x482409` and advances it `GetTickCount() & 0x7f` times
   (`0x407b42..0x407b51`) inside `0x40796c`, which its startup `0x401aa4`
   calls. The mission preparation `0x41cd61` calls `0x40ef06` (`0x41cd77`),
   which stores three `bounded(12)` draws (`0x40ef4a`/`5f`/`74`) at
   `0x482be4/6/8`: words `+8/+10/+12` of the 2000-byte bank `0x482bdc`, which
   `0x40ef83` writes as `mission.var` (`0x40efde..0x40efef`). DBSIM reads them
   as mission variables 4, 5 and 6. VSHELL's clock-seeded randomness therefore
   reaches a mission only as these launch-file words, never as DBSIM stream
   state; in the `mission.var` of all three CD demo tapes, words 4..6 lie in
   0..11 (consistent with this reading, not a proof). See
   [vshell_preparation.md](vshell_preparation.md).
5. **Unchecked alias (not a save path).** The key-queue appender `0x45a498`
   stores a word at index `[0x49eaa0]` of `0x4d20c4` and increments the count
   without a bound check; playback reads the count from the tape (`0x45a91e`)
   and then that many words into `0x4d20c4` (`0x45a9f8`). A count of 619 or
   more would reach stream A: `0x4d2599` lies 1,237 bytes above the queue, with
   other globals between (the control record `0x4d22c6` comes first). The queue
   is cleared every frame (`0x45b649`) and no CD tape holds more than 2 words
   ([input.md](input.md)).

## Demo path

**The demo path neither seeds nor replays the streams. A tape
carries launch files and per-frame input but no generator state. Playback
reproduces the streams only because each DBSIM process starts from the fixed
table and the replay requests the same draws in the same order.**

1. `-D` (`0x45e944`, inside the main parser `0x45e758`) calls `0x45ceb8`,
   which picks a tape name from `tapes\demolist.str` with `_time() % count`
   (see [`_time`](#_time-is-a-separate-choice-source)), sets the demo word
   `0x4d2530` (`0x45e955`) and the playback word `0x4d24d6` (`0x45e95e`) and
   continues as `-p`, which restores the tape header (`0x45cd5c`) over the
   launch files. This runs before the mission load. No RNG routine is reachable
   from `0x45e758` (47 functions), `0x45ceb8`, `0x45cd5c` or the header writer
   `0x45cca4`.
2. In every logic frame `0x45a810` reads one record: the first time 8 bytes
   into the block returned by `0x477904` (`0x6bb6a8`); every record then has
   16 bytes (control words, the time-scale word at `+0xa`, flag bits;
   recording side `0x45b520..0x45b52d`), the counts N and M, N 14-byte pointer
   entries and M key words. Playback copies the recorded time scale into
   `0x4d3b64` (`0x45f58b..0x45f5a4`) and the loop skips the limiter
   (`0x45f467`); the end of the tape is handled at `0x45a97c`.
3. The three CD tapes (`TAPES\DEMO1..3.TAP`) parse exactly to their end with
   this layout. Each starts with seven header files: script 13,520 bytes,
   player 143–495, `mission.var` 2,000, prefs 54, `restore.dat` 42,
   `object.str` 0, keyjoy 518. Then follow 1,499, 1,901 and 3,660 frame
   records with N ≤ 1 and M ≤ 2. There is no other block, so a tape stores no
   stream state. The header does not contain `mission.str`; playback uses the
   copy already in `data\`. The oracle-side tape facts are in
   [oracle_suites.md](../reference-game/oracle_suites.md).
4. **The clock is not replayed.** `0x467828` (based on `GetTickCount`) still
   gates stream-B draws (`0x4340b0`, the drop sequence's clock loop). With the
   limiter skipped the number of clock ticks per frame changes, so the B
   sequence of a playback can differ from the recording. Whether the number of
   stream-A draws depends on the clock is not established; the call-site
   survey found clock gates only on B. The demo's VSHELL-side randomness is
   replayed through the restored `mission.var`.

## `_time` is a separate choice source

`0x45cf91`, in `0x45ceb8`, calls the `_time` thunk
`0x4967fc` (`ff 25 44 c3 6c 00`, IAT `0x6cc344`) with a pointer to the local
`[ebp-0x30]` and reads that local at `0x45cf97`. The signed remainder modulo
`int16[0x4d26f0]` (`0x45cf9a..0x45cfa4`) is saved to word `0x49f034` and
indexes the pointer table at `0x4d26f4`; a count of one uses index zero
without calling `_time`. The loader uses the literal `tapes\demolist.str` at
`0x49f212` (`push` at `0x45cf00`). The sole direct caller is `0x45e944`, which
stores the returned pointer at `0x4d24da`. **Hypothesis:** automatic demo
selection. This is the only direct `_time` call; it neither calls a generator
nor writes a generator object.

`GetTickCount` has IAT `0x6cc3e8` and thunk `0x4968ec`; three direct calls
exist: `0x467828` in `0x467828`, `0x467883` in `0x467840`, `0x4678d0` in
`0x4678c0`. The first wrapper returns `uint32(GetTickCount() - [0x4d3b78]) >>
4` and is easy to miss in linear disassembly. None of the three seeds a
generator. Clock-dependent draw order nevertheless exists: `0x4340b0` calls
the wrapper, then clock comparisons gate the stream-B draws at `0x4340d1` and
`0x4340ef`. [timing.md](timing.md) describes the simulation limiter.

## RNG call sites

A byte-by-byte scan of the file-backed CODE section for relative `E8` calls
and `E9` jumps to the four routines, checked against decoded instructions,
found 50 calls to the core and 30 to the bounded wrapper, no tail jumps; no
literal function pointer to either draw routine exists. There are 78 external
draw sites: A has 37 raw + 25 bounded, B has 11 raw + 5 bounded. The other two
core calls are inside the wrapper and the alternate initializer. Counts
describe static sites, not runtime frequencies.

In the table, **R** targets `0x492ee0`, **N** targets `0x492f24`. Call
addresses, targets and the A/B argument are decoded; **all containing
function boundaries and subsystem labels are hypotheses**, based on
prologues, aligned decoding, local dataflow and nearby type names, and are not
original symbols. Stream A is not exclusively gameplay (effects consume it);
B's presentation-oriented use does not prove isolation from simulation state.

| Containing function (hypothesis) | Call sites | State | Subsystem / use hypothesis |
|---|---|---|---|
| `0x403914` | N `0x403b65`, `0x403b85`, `0x403ba5` | A | Actor/weapon spread; three centered components |
| `0x404100` | R `0x40447f`, `0x4044a2` | A | Actor combat decisions; low-five-bit tests |
| `0x4045c8` | N `0x40482e` | A | Actor/AI timer variation |
| `0x404d70` | R `0x404e31` | A | Actor probability gate |
| `0x404f20` | R `0x404f94` | A | Actor gate; mask `0xfff`, compare `0x1004` (do not normalize this quirk) |
| `0x405038` | R `0x405214`, `0x40524d` | A | Actor effects/damage variants |
| `0x407f1c` | N `0x4080cf` | B | Explosion/sound path; result appears discarded before sound call |
| `0x4086bc` | N `0x4086e4` | A | Debris weighted selection |
| `0x408940` | N `0x408993` | A | Debris lifetime, 2000 plus bounded 30000 |
| `0x4089e4` | R `0x408b44` | A | Debris motion ([Debris heading 4089e4](#debris-heading-4089e4)) |
| `0x408b74` | N `0x408b92` | A | Debris parameter range |
| `0x408bb4` | R `0x408bc0` | A | Debris helper argument |
| `0x40923c` | R `0x40929d` | B | Smoke variant, low two bits |
| `0x409b44` | R `0x409b7a`; N `0x409ba0` | A | Meteor direction/speed |
| `0x40b43c` | R `0x40b4c3`, `0x40b4df` | A | Bullet angular spread |
| `0x40b804` | R `0x40b933`, `0x40b948`, `0x40b95d` | A | Projectile/effect position offsets |
| `0x40cf44` | N `0x40cf8e` | A | Fire/weapon weighted selection |
| `0x40ee90` | R `0x40eefa` | A | Combat/AI probability gate |
| `0x4123ac` | N `0x412409` | A | Actor timer, 10000 plus bounded 1000 (actor-list pass of the logic frame; `0x411fa0` ends at `0x4123a8`, see [main_loop.md](main_loop.md)) |
| `0x412608` | N `0x412633` | A | Actor timer, 5000 plus bounded 1000 |
| `0x4178e8` | R `0x417976`, `0x4179b1` | A | Mech part/animation choices |
| `0x417a04` | R `0x417a2e`; N `0x417a48` | A | Mech probability and magnitude |
| `0x4187d0` | R `0x41884c` | A | Mech combat probability |
| `0x4188c8` | R `0x418a21`, `0x418b1a` | A | Mech damage/effect choices |
| `0x418ba8` | R `0x418d44` | A | Mech effect variant |
| `0x418f74` | N `0x4194ef`, `0x41950e`, `0x41952d`, `0x41954c`, `0x41956b`, `0x41958a` | A | Six mech part effects; same magnitude range |
| `0x41a360` | R `0x41a470` | A | Mech/AI turn direction; selects opposite signs |
| `0x41aa5c` | R `0x41accd` | A | Mech probability gate |
| `0x41cabc` | R `0x41cad8`, `0x41caf0` | A | AI actor comparison; masks `0x3e8`, not modulo 1000 |
| `0x41ce08` | R `0x41ce2b`, `0x41cede` | A | AI selection/ranking variation |
| `0x41d2c4` | R `0x41d365` | A | AI two-way state choice |
| `0x41e570` | N `0x41e6ed`, `0x41e727` | A | AI state durations |
| `0x41f358` | N `0x41f50d`, `0x41f522` | A | AI targeting/scoring; product of two draws |
| `0x41f5a0` | N `0x41f6b8`, `0x41f6cc`, `0x41f6e0` | A | AI target-position offsets |
| `0x421c8c` | R `0x421d7e` | A | Flyer effect variant |
| `0x4236c4` | R `0x42371c`, `0x423736`, `0x4237c8`, `0x4237ea` | A | Meteor/effect directions |
| `0x426528` | R `0x4268d9` | A | Actor animation/effect variant |
| `0x42f860` | R `0x42f874` | B | Dead: method of the never-constructed `TexPoly` class |
| `0x42f970` | R `0x42f9cc` | B | Dead: method of the never-constructed `TexPoly` class |
| `0x434010` | R `0x434053` | B | Timed presentation effect |
| `0x43408c` | R `0x4340d1`, `0x4340ef` | B | Timed presentation effect and variant |
| `0x435c48` | R `0x435cba` | B | Message/event variant |
| `0x436a3c` | R `0x436a6c` | B | Message/event variant |
| `0x438d6c` | R `0x438d84` | B | UI range helper, unsigned remainder plus minimum |
| `0x44b148` | R `0x44b165` | B | Cockpit panel graphic variant |
| `0x44b344` | N `0x44b3ae` | B | Cockpit panel timer variation |
| `0x45d85c` | R `0x45db50` | B | Mission/presentation effect |
| `0x45dc50` | N `0x45dd1c` | B | Timed mission/presentation effect |
| `0x462830` | N `0x46285c` | B | Sound variant selection |
| `0x4628e0` | N `0x462908` | B | Sound variant selection |
| `0x46c4c4` | R `0x46c69d` | A | Terrain/tile construction probability; uncertain subsystem |
| `0x46c754` | R `0x46cac0` | A | Terrain/tile update probability; uncertain subsystem |
| `0x492eb4` | R `0x492ed0` | argument | Reset/skip helper (unused) |
| `0x492f24` | R `0x492f2c` | argument | Bounded wrapper |

The `TexPoly` class (RTTI name at `0x4302da`, constructor `0x42f700`, vtable
`0x49afe6`) is never constructed: no `E8`/`E9` transfer reaches the
constructor, no dword in the file equals it, and the only reference to the
vtable is the constructor's own store at `0x42f75c`. Its methods, including
the two stream-B draw sites above, never run
([horizon.md](horizon.md)).

Type-name anchors that support, but do not prove, the labels: `RADAR_BASE *`
at `0x4069f9`, `ObjPool<DEBRIS>` at `0x408f38`, `SMOKE_BALL` at `0x40952c`,
`ObjPool<METEOR>` at `0x40a014`, `BULLET *` at `0x40b5f4`, `FIRE *` at
`0x40dcb0`, `MECH_TYPE_DATA[]` at `0x4208b3`, `TexPoly` at `0x4302da`,
`PanelStateGadget` at `0x44c0ac`.

## Other generators and the phase counter

**Negative search.** The DBSIM file contains no little-endian
32-bit `0x015a4e35`, `0x41c64e6d`, `0x000343fd` or `0x0019660d`; CW3220
contains the first once, in `_rand`. DBSIM also has none of `0x9e3779b9`,
`0x6c078965`, `0x9908b0df`. Multiply, non-zero XOR, rotate and frequently
called short-function candidates were inspected; for example
`0x47de4c..0x47de71` uses `cdq/xor/sub` for absolute values and an
approximate norm, not state evolution. No additional shift/XOR PRNG was
established. The searches do not prove that every inlined or indirect RNG is
absent.

**Phase counter (deterministic variation).** Function `0x413e50` increments
the word at `0x4a9b70` by 13 at `0x413e6a` (`66 83 05 70 9b 4a 00 0d`),
masks the new value with 15 at `0x413e79` and adds it to `[owner+4]` before
storing at `[object+5]` (`0x413e82`), cycling through 16 offsets. The two
literal references to the global are the increment and the read in this
function; it starts in the startup-cleared DATA tail. **Hypothesis:** AI
update scheduling stagger rather than a general-purpose PRNG; the purpose is
open, so preserve the counter when restoring state for replay. It
draws no stream ([mech_order.md](mech_order.md)).

The 30 direct calls found by the raw relative-call scan (call addresses and
target decoded, containing boundaries and interpretation hypotheses; one call
is missed by the linear listing):

| Containing function (hypothesis) | Calls to `0x413e50` |
|---|---|
| `0x415bb0` | `0x415f33`, `0x415f46`, `0x415f59` |
| `0x417de4` | `0x41829b`, `0x4185ff`, `0x418674` |
| `0x41c0f4` | `0x41c15f`, `0x41c185` |
| `0x41cb94` | `0x41cc38`, `0x41cdf6` |
| `0x41cf18` | `0x41d2b3` |
| `0x41d60c` | `0x41d74a` |
| `0x41d7d0` | `0x41d920` |
| `0x41dd64` | `0x41de09` |
| `0x41de9c` | `0x41debd` |
| `0x41e224` | `0x41e387`, `0x41e3a6` |
| `0x41eb34` | `0x41eb67`, `0x41ec14`, `0x41ec6c`, `0x41ed89` |
| `0x41f7b8` | `0x41f9a5` |
| `0x420ad4` | `0x4212d6`, `0x421369`, `0x421491`, `0x42153c` |
| `0x4215f4` | `0x42172d` |
| `0x421bb4` | `0x421c61` |
| `0x422bf0` | `0x422c02` |
| `0x422d00` | `0x422d80` |

## Own implementation

- **`core/rng`** ([core/README.md](../../core/README.md#rng)): portable C17
  implementation of the 114-byte state, raw and bounded draws, caller-supplied
  table initialization. The unused reset/skip helper `0x492eb4` has no model. No seed table is committed;
  `core/tests/check_rng_real.py` reads the 112 bytes at file offset `0xa5b40`
  of the verified `DBSIM.EXE` (module hash and table hash checked) and compares
  the eight predicted outputs. The synthetic x86/x64 tests and this check agree
  with the static model above.
- **Hook adapters** (`tools/reference-game/proxy/rng_hooks.h`; registry rows
  `rng_next` `0x492ee0` and `rng_bounded` `0x492f24`; contract in
  [hooks.md](../reference-game/hooks.md#rng-rng_next-492ee0-rng_bounded-492f24)).
  Entry signatures: core `55 8b ec 8b 45 08 0f be 50 70`, wrapper
  `55 8b ec 53 8b 5d 08 53`, initializer `55 8b ec 53 8b 5d 08 c6 43 70 18`. Hooking CW3220
  `_srand` alone would miss both DBSIM streams. Compare mode and replaced mode
  are validated live ([Live measurements](#live-measurements)).
- **Own process (`es2sim`).** The globals constructor `0x45caf4` runs from
  `sim/src/init.c` (`i46`, record 46) and initializes both streams through the
  original `0x492e88` path (indices 24/55, `_memmove` of the table at
  `0x4a6940` from the loaded image). `rng_next` and `rng_bounded` are served by
  the same adapters. Both 114-byte states are logged per tick
  ([tick_log.md](../reference-game/tick_log.md)) and compared by the
  [oracle regression](../reference-game/oracle_regression.md). `_time`
  `0x4967fc` is unserved: a demo pick from a list of more than one entry stops
  there ([own_process.md](../reference-game/own_process.md)).
- **Proxy tick log.** `TICK_LOG_RNG_CORE_RVA` is `0x00092ee0`; the record
  carries `rng_a_hex` and `rng_b_hex`.

## Constraints on replacing the generator or the clocks

1. Observe resets through `0x492e88` and every draw through `0x492ee0`; the
   core covers the bounded wrapper. Verify both stream resets and the
   initialization timing before permitting the first draw.
2. A fresh replay either retains the original table and indices or restores an
   explicitly captured **114-byte state per stream** after initialization and
   before any consumption. A single integer seed is insufficient for an
   arbitrary checkpoint. Preserve both streams, the phase counter `0x4a9b70`
   and ordinary world state; do not reseed every tick or skip effect draws.
   Every oracle run starts a fresh process from the fixed table, which holds
   this by construction.
3. A `_time` hook must return the chosen value **and write through the
   non-null argument pointer**, because the caller reads the pointed-to local.
   Pinning `_time` alone initializes no generator.
4. Virtualize `GetTickCount` through IAT `0x6cc3e8`, covering all three direct
   sites and their callers, and preserve the advancing-clock/busy-wait contract
   of [timing.md](timing.md). The oracle's virtual time is implemented this way.
   A fixed RNG state cannot compensate for a changed number or order of
   clock-gated draws.
5. Verify module hashes and site bytes before any in-memory hook; use module
   base plus RVA, also for DLL addresses. An RTL-wide hook is only an extra
   audit option if other DLLs are shown to consume the shared RTL state.

## Live measurements

Hooks `rng_next=replaced,rng_bounded=replaced`, every other hook original,
tick log with `hk_rng_*` counters.

**Hook equivalence.** Compare mode found 0 mismatches in 42,408 draws of the
`c1_01-success` run, and replaced mode reproduced both oracle suites
bit-identically ([hooks.md](../reference-game/hooks.md)).

**Stream position.** For a tick record, k_A and k_B are the draw counts since
a fresh initialization that reproduce `rng_a_hex` and `rng_b_hex` (computed
from the table in `DBSIM.EXE`). **D = k_A + k_B − (hk_rng_next_calls +
hk_rng_bounded_calls)** is the number of draws the hooks did not see. With both
hooks replaced every post-arming draw is counted exactly once, because a
replaced bounded wrapper calls the core directly. A constant D over a run
means no reseed, no other writer and no draw outside the two entries.

**`c1_01-success`, late arming (6,198 records).** The probes on `0x461608`,
`0x46c754`, `0x46c4c4`, `0x4253d8`, `0x4178e8`, `0x45d548` and `0x45d85c` read
0; `0x4327ac` and `0x43408c` read 6,199 (once per frame), `0x462830` read 61.
Both tick-0 states lie on the fresh-table orbit: k_A = 16,474, k_B = 0, of
which the hooks counted 56; **D = 16,418** on every record. At the last
record k_A = 58,617, k_B = 209, counted 42,408 = `hk_rng_next_calls` 1,709 +
`hk_rng_bounded_calls` 40,699 of the `exit_counters` record. The tick log is
identical to the golden. Consequences:

- One initialization per process from the fixed table, no reseed, no other
  writer during the mission.
- Late arming happens after the mission load and after the drop sequence
  (`0x45d548` and `0x45d85c` read 0); the static order through `0x461dc3` does
  not describe the arming point. All 16,418 pre-arming draws are stream A;
  stream B is not drawn before the first frame in this suite.

**`c3_09-loadout-b`, late arming (8,886 records, mission from a SAV-derived
launch set).** The results writer `0x42412c` was reached once. The first record
lies on the orbit (k_A = 1,060, k_B = 0, counted 36); **D = 1,024** on every
record. At the last record counted = 328,787; the `exit_counters` record holds
11,497 + 317,304 = 328,801, the 14 extra draws coming after the last tick
record (the mission-end path), all counted. Neither final 114-byte state of the
last record, nor its first 16 bytes, nor the 112 bytes after the head, occurs in
`results.dat`, `mission.var` or the nine files of the launch set
`oracle\launch\c3_09-loadout-b`. No generator state comes with a SAV-derived
launch set and none leaves the process.

**Entry probes.** `0x492e88` reads 0 (no re-initialization after arming),
`0x42789c` runs once, in the load step (early arming), and `0x45f570` once
per frame
([dbsim_map.md](../reference-game/dbsim_map.md#map-with-every-hook-original)).

**Early arming.** The load-time `rng_next` calls counted per suite lie in
1,024..16,436 ([mission_load.md](mission_load.md#13-l0-arming-before-the-load-and-the-after-load-snapshot)).

## Debris heading 4089e4

Hook `debris_heading` (`4089e4..408b70`), core `es2_debris_heading_registers`
in `core/src/node.c`, adapter `tools/reference-game/proxy/node_hooks.h`. It
**draws once from stream A** (`push 4d2599h` at `408b3f`, `rng_next` at
`408b44`), which agrees with the call-site table. Callers: `408b74` at `408ba8`
(itself called from `408674` and, through `408bb4`, from `408684`), `40d901` and
`40f741`.

**Arguments** (cdecl, four slots): O = debris object, D = divisor dword,
A = pitch word, B = yaw dword (its high word matters).

1. q = (s16(word `4a9668h`) · 1024) / D by IDIV (remainder r kept for a
   register). D must be nonzero (the original faults otherwise); the
   numerator's magnitude is below 2^25, so no quotient overflows.
2. h = s16 of `47e0c0`(s16(q), cos(A)) where cos is `47dff0` (`heading_cos`)
   and `47e0c0` is the pure Q14 product s32(x)·s16(w) >> 14 (bits 14..45; EDX =
   high dword, ECX = s16(w)), computed in core.
3. O+0c dword = 0, O+10 = B, O+32 = 0 (angles (0, 0, B); matrix cache
   invalidated).
4. O+4a = `47e0c0`(h, cos(B + 4000h)), O+4c = `47e0c0`(h, cos(B)), O+4e =
   `47e0c0`(s16(q), cos(A + 0c000h)) (low words; each written before the next
   cosine call).
5. If dword `4a9660h` ≠ 0 (JE `408b15`): word adds O+4a += [P], O+4c += [P+4],
   O+4e += [P+8], with P reread from `4a9660h` before each read (P may alias O;
   each read follows the previous write).
6. n = `rng_next(4d2599h)` & 3ffh; O+5b (word, unaligned) and AX = low word of
   `47e0b0`(−1700, n) (`multiply_q10`) + 0fce0h (−800).

Result: EAX = `47e0b0`'s EAX with that low word, EDX = `47e0b0`'s EDX; the hook
reproduces both. ECX (`4d2599h` after the original Q10 call) is dead at every
caller: `40d906` and `40f746` directly, and through `408b74`'s return at
`408679` and `408689`. EDX can travel further up through `408b74`'s caller,
which is why it is exact.

**Register flows reproduced.** The cosine argument slots are full dwords:
(r & ffff0000h) | A, ((O+0c) & ffff0000h) | (B + 4000h), (high word of the
second `47e0c0` product) | B, and ((B & ffff0000h) | A) + 0c000h with its carry
into the high word. The entry ECX reaches the first cosine call. The RNG entry
registers depend on step 5 (P's words or the last product); the Q10 entry ECX
is `4d2599h`. `heading_cos`, `rng_next` and `multiply_q10` are called through
the guest bridge (entered as hooks, keeping their own modes); the hook refuses
compare mode. Callee counters: `call_47dff0`, `call_492ee0`, `call_47e0b0`.

**Validation (in vitro).** `node_cases.h`: 1,001,024 direct cases with all
three callees as logging stubs (divisors ±1, 1000, 1200, INT32 extremes; speed
words 0, ±1, 7fffh, 8000h; quarter-turn angle words with random high words; P
null, separate or aliasing O+42/46/4a; stubs change the motion words between
the cosine calls); 256 installed 31/a7 probes; 3,072 cases with the **real**
cosine, `rng_next` (stream A state randomized, 56 words and both indices) and
Q10 multiply in rotating original/replaced/compare modes, comparing the full
pair, the RNG state and memory. Branch site `408b15` is taken both ways.

## Open questions

- Initialization order at runtime for paths other than the measured suites;
  writes to either state range outside the known routines (alias, bulk-copy,
  save/load and demo paths); indirect and dynamically loaded DLL uses.
- Whether the number of stream-A draws before arming depends on the clock (the
  count differs by mission, 16,418 vs 1,024; whether it differs by run is
  unknown).
- Demo playback: no measurement shows that the stream positions of a playback
  stay on the fresh-table orbit with constant D. The playback suites fault in
  the original before the tape ends
  ([oracle_suites.md](../reference-game/oracle_suites.md#tape-playback-suites)).
- Draws made inside virtual constructors during the world load.
- The purpose of mission variables 4..6 and of the phase counter `0x4a9b70`;
  all subsystem labels in the call-site tables.
