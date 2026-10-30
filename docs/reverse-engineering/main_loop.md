# DBSIM main loop and logic frame

The main loop of `DBSIM.EXE`, its logic frame `0x45f570` and render root
`0x45fca8`, the object pools the frame updates, the modal dialogs and their
pump, and the own code that implements them (`core/src`, `sim/src`).
Addresses refer to the reference `VER95/DBSIM.EXE`
([reference_version.md](../reference_version.md)) at image base
`0x400000`; they are virtual addresses, not file offsets. Every contract was
read from the `llvm-objdump -d -M intel` listing.

Status words:

- **Hypothesis** marks role names and field meanings that the bytes do not
  prove. Role names in the tables are hypotheses unless stated otherwise.
- The rows of the own process (`sim/src`, `sim/dispatch/services*`) are
  in vitro only unless a live result is named.

Live call counts come from the entry-probe counts of the
[DBSIM map](../reference-game/dbsim_map.md) (the six-suite map, in the order acceptance-60s / c1_01-success / c1_01-collision / c3_09-loadout-b /
c4_08-razor-plasma / c3_10-loadout-hercs, saturated at 1000; "—" = no probe
row).

## Loop boundary and frame counter

The containing function sets EBX to `0x4d24bc` at `0x45f176`. The active
outer loop starts at `0x45f458`. It calls the timing limiter at `0x45f467`
unless the word at `EBX+0x1a` bypasses it. Pause and exit paths branch
around the normal update path.

The normal path calls `0x45fca8` and `0x45fba4`, then the stage dispatcher
`0x401d94` with argument 6. At `0x45f4ad` the instruction
`FF 83 BF 00 00 00` increments the unaligned 32-bit counter at `EBX+0xbf`,
`0x4d257b`. The following `EB A3` returns to the top of the loop. The pause
path sleeps for 100 ms and can repeat without incrementing the counter.

This is an observation point after one completed active update and render
iteration, immediately before its counter increment. It does not by itself
prove that every subsystem updates exactly once per iteration or that all
physics uses a fixed timestep. Logs name it `frame_counter_before_increment`.

`tools/reference-game/scripts/probe_timing.py` reads the counter and the
timer state with `ReadProcessMemory`; it never attaches a debugger or writes
target memory. Counter differences measure aggregate progress even when
polling misses single values; asynchronous polling cannot give a coherent
player snapshot or a complete per-iteration log. Per-frame state comes from
the in-process [tick log](../reference-game/tick_log.md). Neither method
controls time, inputs or RNG.

Results: [timing.md](timing.md); player fields:
[player_state.md](player_state.md).

## The logic frame `0x45f570`

### The main loop around it

`0x45f458..0x45f4b3`, one active iteration:

1. `0x401d94(4)` at `0x45f45a`: the stage-4 callbacks (none are registered).
2. Limiter `0x4678c0` at `0x45f467`, **skipped while the word at `0x4d24d6`
   (`EBX+0x1a`) is set**. That word is the tape-playback flag
   ([launcher.md](launcher.md): `-D`/`-p`).
3. Pause byte `0x4d2572` → 100 ms sleep and message pump, no counter
   increment; words `0x4d2532` and `0x4d24fe` → leave the loop.
4. `0x45f570()` at `0x45f487`. A nonzero AX leaves the loop (`0x45f48f` →
   `0x45f4c5`, mission shutdown).
5. `camera_update` `0x4011a0([0x4d24ea])` at `0x45f495`.
6. `0x45fca8` at `0x45f49b`. Its **first** action is `0x401d94(5)` at
   `0x45fcb3`: the stage-5 callbacks, which free the objects queued for
   removal during this frame (below). Then world and HUD rendering.
7. `0x45fba4` at `0x45f4a0` (presentation).
8. `0x401d94(6)` at `0x45f4a7`, then the frame counter `0x4d257b` increment
   at `0x45f4ad`.

**Stage registry.** `0x401d64(fn, stage)` appends a 6-byte entry
(function pointer, stage word) to the table at `0x4a804c` (count word
`0x497074`); `0x401d94(stage)` calls every entry with that stage in
registration order. A raw scan of the CODE bytes finds 31 `call 0x401d64`
(19 for stage 2, 11 for stage 5, one for stage 6, none for stages 0, 1, 3
or 4; each pushes an immediate stage) and no other reference to the table
`0x4a804c` or its count. `0x401d94` is called seven times: stages 0, 1, 3 at
mission start (`0x45f285`, `0x45f2b2`, `0x45f432`), 2 at `0x4617b8`, and 4,
5, 6 in every frame.

- The stage-5 callbacks are `0x405ea4`, `0x4077e8`, `0x407b3c`, `0x4083e0`,
  `0x408f9c` (which also calls `0x408f84`), `0x409904`, `0x409a1c`,
  `0x40a108`, `0x41544c`, `0x4215a8`, `0x46b188`. Each calls
  `0x478644(removal list, pool)` for one object pool, which runs the queued
  objects' virtual `+0x8` (flag 2), returns them to the pool (`0x471bc8`)
  and clears the list. `0x46b188` also resets the dword `0x6b4f3c` to
  `0x7fffffff` (see `0x46b678` below). Their order is the runtime
  registration order, which is not traced.
- The stage-6 callback `0x401e5c` acts only in record mode (word
  `0x4d24d8`): it writes a buffer at `0x4a84fc` to a file and resets it
  (**hypothesis:** a recording flush; the oracle suites do not record).

**Consequence:** object removal is deferred. An update that asks for removal
only appends the object to its pool's removal list (`0x478628`: store at
`list[count++]`); the object stays linked while `0x45f570`, `camera_update`
and the rest of the frame's logic see it, and is freed at the start of
`0x45fca8`. A replacement of `0x45fca8` must keep stage 5 running, or pool
membership diverges.

### The frame itself

**Contract.** No stack arguments; reads and writes only globals
(28 absolute addresses) and the objects reached through them; no x87. The
prologue calls `0x49630c` with EAX = `0x49f558` (the runtime's exception-frame
registration, see [weapons.md](weapons.md#effect-creation)); both exits restore `fs:0`
(`0x45f84a`, `0x45fa76`). The words stored to `[ebp-0x20]` and the dword
adjustments through `[0x6cbee4]` around the dialog object are the runtime's
exception bookkeeping, not game state. Return value: AX = 0 continues the
mission, 1 ends it (cases below).

**Object pools.** Each pool is a header with free count
`+0`, used count `+2`, free list `+0xa` and used list `+0xe`; each object has
an 8-byte link header before it (next `+0`, previous `+4`). Allocation
`0x471b30` inserts at the **head** of the used list. The iterator
`0x471c70(pool, prev)` starts (prev = 0) at the **tail** and then follows the
previous links toward the head, so every pool is updated **oldest object
first**. An object allocated into the same pool during its own pass becomes
the new head and is still reached in that pass; an object allocated into a
pool whose pass is already over waits for the next frame.

**Pool identities (hypothesis).** The template type names `ObjPool<…>` stand
in CODE at the end of the module that owns the pool: `LightSource` `0x407a1a`,
`EXPLOSION` `0x40838e`, `DEBRIS` `0x408f38`, `SMOKE_BALL` `0x409569`, `SMOKE`
`0x4095b9`, `FlatObj` `0x4099cc`, `METEOR` `0x40a014`, `MECH` `0x41c0a9`,
`FLYER` `0x42204d`, `FIRE` `0x46bbf9`. Each string follows the stage-5 drain
of exactly one pool (and, except for `MECH` and `FLYER`, its construction by
`0x471ad8(pool, capacity, size)`) and precedes the next pool's code; that
pool gets the name in the table. The actor pool `[0x4a95a0]` and the
projectile pool `[0x4a96c2]` have no such name. Two names have independent
support: the meteor pool by the resource name `meteor`
([mission.md](../formats/mission.md)), the debris pool by the debris RNG
sites ([rng.md](rng.md)).

### Direct calls in execution order

"Calls per suite" are the map's entry-probe counts. **F** = gated by the
freeze word `0x4d24f2` (the step skips it while set; "Freeze and single
step" in [launcher.md](launcher.md)). "Hook" names the registered hook that
replaces the callee; "original" means it runs as the original.

| # | Call site | Callee and arguments | Condition | Role | Calls per suite | Status |
|---:|---|---|---|---|---|---|
| 1 | `0x45f57e` | `0x49630c` (EAX = `0x49f558`) | always | runtime exception-frame registration | 1000 ×6 (52 callers) | RTL, original |
| 2 | `0x45f583` | `0x45a810()` → EAX = `0x4d22c6` | always | builds the 48-byte per-frame control record `0x4d22c6`: message pump `0x464c78` (a nonzero result sets `0x4d2532`, which ends the loop), keyboard/joystick commands, tape playback/recording (reads/writes; field meanings hypothesis) | 1000 ×6 (also called by modal screens) | hook `control_record` |
| — | `0x45f58b` | — | tape playback (`0x4d24d6`) | **copies record word `+0x2e` into the time scale `0x4d3b64`** | | see time sources |
| 3 | `0x45f5c2` | `0x40813c(obj)` per object of pool `[0x4a961e]` (iterator `0x45f5ea`); nonzero AX → `0x478628([0x4a9622], obj)` at `0x45f5d7` | F | `EXPLOSION` (40 × `0x5b`; created through `0x407f1c`, see [weapons.md](weapons.md#explosion_create-0x407f1c-cdecl-o-t-p-d-s)): frame animation on timer `+0x4a` (`timer_word_step`); at each expiry it advances the frame, sets a light of `[0x4a9608]` (`+0x53`, via `0x4076a0` → `0x407048` → `0x40735c`), advances a linked object's frame (`+0x4f`) and re-arms from the type descriptor; done when the frame wraps to 0 or the type's word in `[0x4a963c]` is negative | 340/1000/602/1000/1000/1000 | hook `explosion_update` |
| 4 | `0x45f603` | `0x4091cc(obj)`, pool `[0x4a966e]` (`0x45f62b`), removal `[0x4a9672]` | F | `SMOKE_BALL` (200 × `0x4c`): position `+0x26..` += word velocity `+0x41..`; timer `+0x47` (`timer_dword_step`) re-armed to 200 advances the frame; done when the frame wraps to 0 | — | hook `smoke_ball_update` (never live in the suites) |
| 5 | `0x45f644` | `0x4092dc(obj)`, pool `[0x4a9684]` (`0x45f66c`), removal `[0x4a9688]` | F | `SMOKE` emitter (20 × `0x2a`): while count `+0x24` > 0, every timer `+0x21` expiry (re-armed 200) takes a position from the owner's node (`+0x15`, `+0x19`; `euler_matrix`, `transform_point`) and spawns a `SMOKE_BALL` (`0x471b30([0x4a966e])`, `0x409178`); done at count 0 | — | hook `smoke_update` with `smoke_ball_init` `0x409178` (never live) |
| 6 | `0x45f685` | `0x46b678(obj)`, pool `[0x6b4f30]` (`0x45f6ad`), removal `[0x6b4f34]` | F | `FIRE` (10 × `0x5b`): looping animation (timer `+0x56`, re-armed `0x40`), lifetime `+0x59` decremented per loop, position from the owner's node (`+0x4a`, `+0x4e`); each instance closer to the camera `[0x4d24ea]` than every earlier one of this frame (`0x47df9c` of the squared distance against the running minimum `0x6b4f3c`, reset in stage 5) calls `0x46297c(0x33, position)`; done at lifetime 0 | 545/1000/0/1000/1000/1000 | hook `fire_update`; `0x46297c` is audio (`sfx_refresh`) |
| 7 | `0x45f6c6` | `0x408bd8(obj)`, pool `[0x4a9642]` (`0x45f6ee`), removal `[0x4a9646]` | F | `DEBRIS` (150 × `0x61`): flight with `movement_step`/`multiply_q10`, ground via `0x427d80`, timed effects through `0x407f1c` | 1000 ×6 | hook `debris_update` with `ground_height` `0x427d80` |
| 8 | `0x45f707` | `0x409d2c(obj)`, pool `[0x4a96aa]` (`0x45f72f`), removal `[0x4a96ae]` | F | `METEOR` arrival object; places the pending group's first actor and clears group `+0x14` (mission.md); calls `radius_hit`, `terrain_height`, sound `0x4628e0` | 81/351/172/0/0/0 | hook `meteor_update` |
| — | `0x45f740` | — | F | clears word `0x4d2526` (set by projectile flight `0x40a644`, read by `0x410a13`… and `0x45ad19`…) | | |
| 9 | `0x45f753` | virtual `+0x14(obj)`, pool `[0x4a96c2]` (`0x45f779`), removal `[0x4a96c6]` | F | projectiles/bullets: `projectile_flight` `0x40a538` (table `0x498448`), `bullet_flight` `0x40b124` (table `0x498628`) ([weapons.md](weapons.md#projectile-flight-and-impact)) | hook counters | hooks `projectile_flight`, `bullet_flight` |
| 10 | `0x45f7a6` | virtual `+0x18(obj)`, pool `[0x4a95a0]` (`0x45f7b5`); return value ignored | F; object's group `[obj+0x45]` has no pending activation (`+0x14` = 0) | building-pool classes only ([vtables.md](vtables.md#13-the-two-update-dispatchers)): `turret` `0x404100` (`GUN_BASE` `0x4978ac`), `0x403ca8` (`BASE` `0x497940`, `RADAR_BASE` `0x4979d4`), `0x46a6d4` (`VEHICLE` `0x497818`), `0x4045c8` (`LC_BASE` `0x497784`, live in c4_08 only); the `MECH` update `0x415b38` (`0x49a282`) is dispatched in row 11 by `0x411cec` from `order_update`, not here | `0x403ca8`: 1000 ×6 (`0x415b38`, row 11: 1000 ×6); `0x46a6d4`: 1000 ×5, 0 in c3_10 | hooks `turret`, `base_update`, `convoy_think`, `lc_base_update` |
| 11 | `0x45f7ec` / `0x45f7f5` | `0x4236c4(group)` if group `+0x14` ≠ 0, else `0x423a74(group)`; groups `[0x4a9e00]`, count word `[0x4a9e04]`, stride `0x7a`, index order | F | pending group activation (meteor/placement modes, RNG stream A) / current-order update (mission.md) | 1000 ×6 each | hooks `group_activate`, `order_update` |
| — | `0x45f818` | — | step request `0x4d24fc` | clears it and sets the freeze word again | | |
| 12 | `0x45f838` | `0x460870(record)` | always | input/command dispatch; nonzero → return 1 at once (rest of the frame skipped) | 1000/1000/1000/1000/1000/937 | hook `player_input` (command handler `0x45feb8`) |
| 13 | `0x45f860` | `0x426b48()` | F | mission timers: `0x4230a4` per timer (`[0x4a9e38]`, count `[0x4a9e34]`, stride `0x31`) (mission.md) | 1000/…/937 | hook `timer_scan` |
| 14 | `0x45f86c` | `0x426b70([0x4d24e6])` (player) | F | spatial triggers (mission.md) | 1000/…/937 | hook `trigger_scan` |
| 15 | `0x45f872` | `0x45de30()` | always | applies a requested view mode (`0x4d251a` → `0x4d24ee`), view subject `0x45df34`, countdown `0x4d2520` → `0x45e208` (HUD/view code) | — (every logic frame that gets here) | hook `view_apply` |
| 16 | `0x45f881` | `0x4123ac()` | F | actor list `[0x4a9af8]` (count `[0x4a9afe]`): two word timers `+0x1e2`, `+0x1e5`; on expiry re-arm `+0x1e6` = 10000 + `rng_bounded(A, 1000)` and `0x41251c(actor)`; then `0x4128f8` for each actor whose group is not pending and has side byte `+0x12` = 0 and whose bytes `+0x99`, `+0xa4` are 0 (the last one with `+0xa3` set is held back and done after the loop); then clears `+0xa2` | — (`0x41251c` 768/1000/1000/1000/549/297, `0x4128f8` 1000 ×6) | hook `actor_pass` (`contact_expire`, `contact_scan`, `contact_share`) |
| 17 | `0x45f8ac` | `0x41aa5c(vehicle)`, pool `[0x4a9b7a]` (`MECH`, iterator `0x45f8bd`) | F; group not pending, byte `+0x99` = 0 | per-vehicle update ([player_movement.md](player_movement.md)) | 1000 ×6 | hook `vehicle_update` |
| 18 | `0x45f8f8` | `0x45fa84()` | player `[0x4d24e6]` byte `+0x99` set; F | death sequence: first call sets the view and starts countdown `0x4d2a58` (duration from table `0x49f4f4`); returns 1 when it expires | 0/0/0/209/63/209 | hook `death_sequence` |
| 19 | `0x45f90a` … `0x45f974` | `0x47749c(0x57e)`, `0x455950(obj, 2, 1, 0)`, virtual `+0x8(obj)`, `0x4543b0(obj, 3)` | `0x45fa84` returned nonzero | alert dialog (status 2) → return 1 regardless of the button | `0x455950`: 0/8/0/1/1/1 (all callers) | modal UI, original (own rows in the own process) |
| 20 | `0x45f992` | `0x4131ac([0x4d24e6])` → s16 status | player byte `+0x99` = 0 (**not** gated by F) | periodic outcome policy (mission.md) | 1000/1000/1000/1000/1000/728 | hook `outcome_poll` |
| 21 | `0x45f9bd` | `0x423f08([player+0x45])` | status 3 | `0x41b804` for every member of the player's group; afterwards a set `+0x99` turns the status into `0x12` | — | hook `group_assess` (`member_assess` `0x41b804`; never live) |
| 22 | `0x45f9e5` … `0x45fa63` | alert as in row 19 with the status | status ≥ 0 and ≠ 2 | returns (button == word table `0x49f5c0[original status]`) (input_journal.md) | as row 19 | modal UI, original |

The pool update functions (rows 3–8) take the object as their only cdecl
argument and return AX ≠ 0 to request removal. `0x47749c` (allocation from
the heap at `0x6bb2f8`, zero-filled), `0x478628` and `0x471c70` are runtime
helpers of the frame, not subsystems. Since `0x45f570` itself is own code
([The logic frame as own code](#the-logic-frame-as-own-code)), every row
reaches its callee through the frame's own call order.

**Live cross-check (map counts).** In c3_10 the counts are exact:
`0x460870` 937 = `0x4131ac` 728 + `0x45fa84` 209, and `0x426b48` = `0x426b70`
= 937. So in that suite every logic frame passed the input dispatcher with
AX = 0, none was frozen, and each frame took exactly one of the two branches
at `0x45f8e4`. `0x45fa84` runs only in the three suites in which the
player's byte `+0x99` gets set (c3_09, c4_08, c3_10); `0x409d2c` (meteor)
only in the three c1_01/acceptance suites. `0x45a810` reaches 1000 in c3_10
because it has other callers too (the modal screen loop,
[input_journal.md](../reference-game/input_journal.md)).

### Per-frame update order

Derived from the calls above (order of calls; subsystem names are
the hypotheses of the table):

1. Time scale: limiter (or, in tape playback, the tape value).
2. Input record (`0x45a810`).
3. Object pools, each oldest first: explosions → smoke balls → smoke emitters
   → fires → debris → meteors → projectiles/bullets → actors and buildings
   (virtual `+0x18`) → groups by index (activation or order step).
4. Input/command dispatch `0x460870` (player commands, weapon fire paths of
   [weapons.md](weapons.md#weapon-emission)), which can end the mission.
5. Mission timers, then spatial triggers.
6. View mode.
7. Actor list pass `0x4123ac`, then every vehicle `0x41aa5c` (oldest first).
8. Outcome: death sequence or periodic outcome evaluation, possibly an alert.
9. `camera_update`; removals (stage 5); rendering; presentation; stage 6;
   frame counter.

Consequences for an own implementation:

- The RNG draws of stream A in steps 3 (debris, meteor placement), 7 and the
  vehicle code happen in this order and must keep it.
- Objects spawned by the player's commands (step 4) or by vehicles (step 7)
  into pools of step 3 first update in the next frame.
- A vehicle that is in both the actor pool and the vehicle list
  (**hypothesis:** every HERC, including the player's) is updated in step 3
  (`0x415b38`) and again in step 7 (`0x41aa5c`), the second time after the
  commands.
- Removals of a frame are invisible until the start of `0x45fca8`.

**Time sources.** Every countdown in the frame (`timer_word_step`,
`timer_dword_step`, `movement_step`) reads the scale word `0x4d3b64`.
Normally the limiter writes it before the frame. In tape playback
(`0x4d24d6`) the limiter is skipped (`0x45f465`) and `0x45f570` overwrites
the scale from the record (`0x45f595..0x45f5a4`), which `0x45a810` filled
from the tape file `[0x4d24e2]` (`0x45aa96`); in record mode `0x45a810`
writes the current `0x4d3b64` into the tape entry (`0x45b520`). The original
tape format therefore already stores the per-frame elapsed value, as replays
need (**hypothesis** for the file layout; the oracle uses the input journal
and virtual time instead). No other time source is read in the frame.

### Callees that stay original

- `0x49630c` (RTL exception-frame registration) and `0x47749c` (allocator).
- The alert dialog `0x455950` / virtual `+0x8` / `0x4543b0` (modal UI; own
  rows in the own process, [The modal dialogs in the own
  process](#the-modal-dialogs-in-the-own-process) and [Dialog constructors
  and the F12/Alt+P session](#dialog-constructors-and-the-f12altp-session)).
- Inside the hooks: `0x46297c` only tests the sound flag `0x49f8ec` and
  calls `0x46299c` (SOS audio), which stays original through the guest
  bridge. `0x407b20` (the explosion table lookup, `[0x4a9628] + 40 × type`)
  is a pure leaf.

`0x4076a0` writes a light entry of the table `[0x4a9608]` (`0x40735c`
computes its `+0x17` from `+0xd`, `+0xf`, `+0x13`); the rendering frame reads
`[0x4a9608]` (`0x45fd74`). Its classification is in [`light_level`
`0x4076a0`](#light_level-0x4076a0-cdecl-l-v--classification).

**Call site `0x412409`** (stream A, 10000 + bounded 1000) lies in `0x4123ac`,
the directly called actor list pass of row 16 (`0x4123ff..0x412415`), not in
`0x411fa0`: the map's CFG ends `0x411fa0` at `0x4123a9`.

**Open.** Execution order of the stage-5 callbacks (runtime registration
order); what `0x45a810`'s record fields mean beyond `+0x2e`; the readers of
`0x4d2526`; the callees without a probe row (`0x4091cc`, `0x4092dc`,
`0x45de30`, `0x4123ac`, `0x423f08`, `0x478628`) need a count-mode run if
their frequency matters.


## Explosion and fire pool updates

Rows 3 and 6 of the call table. Three hooks: `explosion_update`
(`0x40813c`), `light_level` (`0x4076a0`) and `fire_update` (`0x46b678`);
own code `core/src/logic.c`, adapter group `logic`
(`proxy/logic_hooks.h`). Every game call of the replacements goes through
the weapons guest bridge with the entry registers the original holds at
that call, so nested hooks keep their own modes and counters. Registers
named below are full 32-bit values.

### `explosion_update` `0x40813c` (cdecl, one argument O)

Prologue `55 8b ec 51 53 56 57 8b 5d 08` (relocated 5 bytes: 1,2,1,1). The
`push ecx` slot is the local at EBP-4; its low word is written (`0x408175`)
before its only reads, its high word (EBP-2) is written at `0x4081bc` before
its only read at `0x4081d2`: no uninitialized read, no entry shim.

1. `timer_word_step(O+4a)` (entry EAX = O+4a, EDX/ECX = entry EDX/ECX).
   AX ≠ 0 (timer still running, `0x40815c`): return EDX:EAX = 0:0.
2. The type entry T = `[0x4a9628] + 40 × s8(O+41)` is the pure leaf
   `0x407b20` (EAX = EDX = T, ECX untouched), computed in core at each of
   its three call sites; the original's `pop ecx` leaves ECX = O.
   W = word `[[0x4a963c] + 4 × s16(T+0)]`, kept in DX (high word from T).
   s16(W) ≤ −1 (`0x40817e`): return EAX = (`[0x4a963c]` & FFFF0000h) | 1,
   EDX = (T & FFFF0000h) | W.
3. Shape I = `[O+34]`: frame count C = word `[[I+4]+20] + 2W`, frame word F
   at `[I+8] + 2W`; F ← (F + 1) mod C (IDIV, C > 0 required: a zero count
   raises #DE in the original, the caller guarantees it). The new frame is
   re-read; zero (`0x4081c3`): return EAX = 1, EDX = `[I+8]` (re-read).
4. Light L = `[O+53]` ≠ 0 (`0x4081ca`): `0x4076a0(L, v)` with
   v = (T & FFFFFF00h) | byte `[T + 8 + 2 × s16(F)]` (the full EAX is the
   argument slot); entry EAX = v, EDX = s16(F), ECX = O.
5. Linked object K = `[O+4f]` ≠ 0 (`0x4081ea`): its frame 0 advances the
   same way with `[[K+34]+4]+20` and `[[K+34]+8]` at index 0 (no wrap test).
6. Re-arm: word `O+4b` ← word `T+2`; return EAX = 0, EDX = T.

Callees: `timer_word_step` (hook), `0x4076a0` (hook `light_level`); nothing
else. It reads O, the shape and type tables, `[0x4a9628]`, `[0x4a963c]` and
writes only the timer word (through the callee), `O+4b`, the two frame
words and what `0x4076a0` writes.

### `light_level` `0x4076a0` (cdecl L, v) — classification

`0x4076a0` → `0x407048(table, idx, value)` → `0x40735c(table, entry)`, with
table = `[0x4a9608]`, idx = (L & FFFF0000h) | word `L+c` (only the low word
is used), value = byte v zero-extended. Entry E = table + 6Ch + 35 ×
s16(idx); word `E+d` ← value; dword `E+1b` ← value; dword `E+17` ←
trunc(s32(s16(word E+d) × (dword E+13 << 5)) / 10) + (dword E+f << 5) (the
IMUL keeps only the low 32 bits before CDQ/IDIV by 10, which cannot fault).
Return EAX = the stored `E+17`, EDX = dword `E+f` << 5, ECX = E (dead at the
only caller `0x4081dd`). No other calls, no x87, no drawing or palette code.

**Replaced** (hook `light_level`, prologue `55 8b ec 33 d2 8b 0d 08 96 4a
00`, relocated 5 bytes: 1,2,2): three integer data writes into one light
entry that the own code reproduces exactly. Readers of the table (raw scan
for the absolute `0x4a9608`): the light module itself (`0x407625`…`0x407745`,
construction and the other setters `0x407018`, `0x407048` via `0x407645`),
the per-object light application `0x407098` called from the draw routine
`0x42876c` (`0x42878e`), and `0x40707c` from the rendering frame
(`0x45fd74`, camera position into the table). No simulation code reads the
entries, so the state is presentation state that the game keeps in data;
replacing the writer changes nothing as long as the bytes are equal, which
the in-vitro comparison checks. `0x407048` and `0x40735c` have other
callers (`0x406f2d`, `0x406fac`, `0x407645`) and stay original there; no
hook is placed on them.

### `fire_update` `0x46b678` (cdecl, one argument O)

Prologue `55 8b ec 83 c4 d4` (6 bytes: 1,2,3). Locals: P (EBP-20h, 12
bytes), Q (EBP-14h, 12 bytes), D (EBP-2Ch, 12 bytes), owner copy (EBP-8),
node word (EBP-2); each written before it is read on every path, so no
entry shim.

1. `timer_word_step(O+56)` (entry EAX = O+56, EDX/ECX = entry). AX = 0
   (`0x46b69a`): word `O+57` ← 40h; shape I = `[O+34]`, frame word
   F = `[[I+8]]` ← (F + 1) mod word `[[I+4]+20]` (count > 0 required);
   a new frame 0 (`0x46b6c2`) decrements word `O+59` (the lifetime).
   Registers afterwards: expired: EAX = quotient, EDX = remainder, ECX = I;
   running: EAX = timer result, EDX = 0, ECX = O+56.
2. Position source, owner A = `[O+4a]`:
   - s16(`O+4e`) ≤ −1 and byte `O+41` ≠ 0 (`0x46b6cd`, `0x46b6d5`): Q ←
     the 12 bytes at A+26 (the owner's position); go to 4.
   - s16(`O+4e`) ≤ −1, byte `O+41` = 0 (`0x46b6e0` not taken): P ← s16
     words `O+50`, `O+52`, `O+54` (registers ECX = x, EAX = y, EDX = z).
   - s16(`O+4e`) ≥ 0: the owner's shape S = `[A+34]`, node object N =
     `[S+4]`; virtual `[N]+20(N, node)` with the node word zero-extended
     (entry EAX = vtable, EDX = node, ECX = N; its EDX high word is 0 on
     every path into `0x46b6fb`). A nonzero result R with s16(`R+4`) > −1
     (`0x46b724`, `0x46b72b`) selects the matrix `[S+16] + 32 × s16(R+4)`
     (EDX = that product); otherwise the static record `0x6c56a8`. Then
     byte `O+41` = 0 (`0x46b746`): `0x4801d4(O+50, matrix, P)` (entry
     EAX = matrix, EDX = O+50, ECX = P); else P ← the 12 bytes at matrix+14h
     (ECX = x, EAX = y, EDX = z).
3. (P paths) A is re-read; if word `A+32` = 0 (`0x46b77a`):
   `euler_matrix(A+c, A+12)` (entry EAX = A+12, EDX/ECX as left by step 2),
   then word `A+32` ← 1. `transform_point(P, A+12, Q)` (entry EAX = A+12,
   EDX = Q, ECX = P).
4. `O+2e`, `O+26`, `O+2a` ← Q (in that order).
5. D = `[0x4d24ea]+4..+f` (camera position) − Q; s = Dx² + Dy² + Dz² mod
   2³² (the IMULs keep the low dwords); r = `0x47df9c(s)`, a pure leaf
   computed in core (digit-by-digit square root over the bit pairs from
   BSR; EAX = r, EDX = s unchanged, ECX dead because of the `pop ecx`).
   Signed r < s32 `[0x6b4f3c]` (`0x46b825`): `0x46297c(33h, Q)` (entry
   EAX = r, EDX = s, ECX = Q), then `[0x6b4f3c]` ← r. The running minimum
   is reset to 7FFFFFFFh by the stage-5 callback `0x46b188`, so only the
   closest fire of a frame so far plays sound 33h; the read, the call and
   the store stay in this order.
6. Return EAX = (word `O+59` = 0) (SETE/AND: 0 or 1), EDX = s or the EDX
   left by `0x46297c`.

`0x46297c` tests the sound flag `[0x49f8ec]` and calls SOS `0x46299c`
([audio.md](audio.md)); it is reached through the bridge. `0x4801d4`
(`matrix_point`), the virtual +20, `euler_matrix`, `transform_point` and
`timer_word_step` are reached through the bridge too and run under their
own modes. Callers: `0x45f685` (logic frame, tests AX) and `0x46b545` (the
fire constructor `0x46b48c` updates a new fire once; it discards the
result). ECX is dead after every caller of the three functions (`pop ecx`
or an overwrite before any read).

Branch sites: `explosion_update` 5 (`0x40815c`, `0x40817e`, `0x4081c3`,
`0x4081ca`, `0x4081ea`), `fire_update` 10 (`0x46b69a`, `0x46b6c2`,
`0x46b6cd`, `0x46b6d5`, `0x46b6e0`, `0x46b724`, `0x46b72b`, `0x46b746`,
`0x46b77a`, `0x46b825`), `light_level` none.

## Remaining logic-frame pool updates

Rows 4, 5, 7 and 8 of the call table. Six hooks in the adapter group
`pools` (`proxy/pools_hooks.h`, own code `core/src/pools.c`,
`es2/core/pools.h`): `smoke_ball_update` (`0x4091cc`), `smoke_update`
(`0x4092dc`), `smoke_ball_init` (`0x409178`, the smoke-ball constructor
body, called only by `0x4092dc`), `debris_update` (`0x408bd8`),
`ground_height` (`0x427d80`, called only by `0x408bd8`) and
`meteor_update` (`0x409d2c`). Every game call of the replacements goes
through the weapons guest bridge with the registers the original holds at
that call, so nested hooks keep their own modes and counters. Registers
named below are full 32-bit values; "(X hi)" is the high word of X left in
a register whose low word a 16-bit load replaced. O is the only cdecl
argument of the four updates; their caller `0x45f570` tests AX only and
pops ECX, so EDX:EAX is reproduced exactly and ECX is dead (the framework
returns the entry ECX).

**Frames.** `smoke_update`, `debris_update` and `meteor_update` start with
the runtime frame setup `0x49630c` (EAX = descriptor `0x497f9a`,
`0x497e10`, `0x4981b0`, whose +4 places the record at EBP−24h, −24h,
−28h; the context word record+10h is written 14h/8h, and 2Ch/20h in the
meteor, around constructor calls) and end by restoring FS:0 from
record+0. The adapter gives the bridge a synthetic EBP (scratch+208) and
restores FS:0 to its entry head after the return; the record's first dword
is that head, which the originals also return in EDX on their FS-restoring
exits. Every other local is written before it is read on every path: no
entry shim. None of the six reads its entry EAX/EDX/ECX except as values
handed on unchanged to a callee (the frame setup keeps EDX and ECX).

### `smoke_ball_update` `0x4091cc` (cdecl O)

Prologue `55 8b ec 53 56 57 8b 5d 08` (relocated 5: 1,2,1,1).

1. Position += velocity words, each a read of the s16 word and an RMW of
   the dword: O+2E += s16(O+45), O+26 += s16(O+41), O+2A += s16(O+43).
2. `timer_dword_step(O+47)` (entry EAX = O+47, EDX = s16(O+43), ECX =
   s16(O+41)). Nonzero EAX (all 32 bits, `0x409203`): return EAX = 0, EDX =
   the timer's EDX.
3. Dword O+48 ← C8h; shape I = [O+34], frame word F = [[I+8]], count C =
   word [[[I+4]+20h]] (zero-extended; nonzero required, the IDIV faults on
   0). F ← (F + 1) mod C with quotient Q = (F + 1) / C. Remainder ≠ 0
   (`0x40922c`): EAX = 0; else EAX = (Q & FFFF0000h) | 1 (Q = 10000h for
   F = FFFFh, C = 1). EDX = the remainder.

### `smoke_ball_init` `0x409178` (cdecl B, P, k)

Prologue `55 8b ec 53 8b 15 78 96 4a 00` (relocated 10: 1,2,1,6; the MOV
has an absolute operand). `object_init(B, shape)` with shape =
[[4a9678h] + 4 × s16(k)] (entry EAX = s16(k), EDX = [4a9678h], ECX =
shape); then [B] ← 498084h (vtable), word B+4 ← 4, B+2E/B+26/B+2A ← the
dwords P+8/P+0/P+4 (read and written in that order), dword B+48 ← C8h,
the runtime construction count [[6cbee4h]] += 1. Returns EAX = B, EDX =
[P+4]. No branch.

### `ground_height` `0x427d80` (cdecl P)

Prologue `55 8b ec 8b 45 08` (relocated 6: 1,2,3).
`terrain_height([4a0be0h], P)` with entry EAX = P, EDX = [4a0be0h], ECX =
entry ECX; returns its EDX:EAX. ECX is dead at the only caller
(`0x408cfb`: `multiply_q10` does not read it, `0x408d43` overwrites it).
`0x408bd8` pushes its saved product above the two slots P, 0 and pops it
afterwards; the callee reads only P.

### `smoke_update` `0x4092dc` (cdecl O)

Prologue `55 8b ec 83 c4 bc` + `b8 9a 7f 49 00` (relocated 6: 1,2,3). Frame
f = EBP: P = f−44h (12 bytes), Q = f−38h (12), owner copy f−2Ch, node
word f−26h, record f−24h, context word f−14h, slot f−4.

1. Frame setup. Word O+24 (spawns left) = 0 (`0x4092f7`): exit (step 6).
2. `timer_word_step(O+21)` (entry EAX/ECX = the frame setup's, EDX =
   O+21). AX ≠ 0 (`0x409313`): exit.
3. Word O+22 ← C8h; word O+24 −= 1. Node word N = O+19, flag byte O+C,
   owner A = [O+15]:
   - s16(N) ≤ −1 and flag ≠ 0 (`0x409328`, `0x409330`): Q ← the dwords
     A+2E, A+26, A+2A in that order (EAX = A.y, EDX = A.z, ECX = A.x);
     go to 5.
   - s16(N) ≤ −1, flag = 0 (`0x40933b` falls through): P ← s16 words
     O+1F, O+1B, O+1D (EDX = z, ECX = x, EAX = y); go to 4.
   - s16(N) ≥ 0: S = [A+34], node object M = [S+4]; virtual
     `[M]+20h(M, n)` with n = (timer EDX hi) | N (entry EAX = vtable,
     EDX = n, ECX = M). A nonzero result R with s16(R+4) > −1
     (`0x40937f`, `0x409386`) selects the matrix X = [S+16h] + 32 ×
     s16(R+4) (EDX = 32 × s16(R+4)), else X = `0x6c56a8` (S is the
     value read before the call). Flag (reread) = 0 (`0x4093a1`):
     `0x4801d4(O+1B, X, P)` (entry EAX = X, EDX = O+1B, ECX = P); else P ←
     the dwords X+1C, X+14, X+18 in that order (EDX, ECX, EAX).
4. C = [O+15] + 0Ch (owner reread). Word C+26h = 0 (`0x4093d5`):
   `euler_matrix(C, C+6)` (entry EAX = C+6, EDX/ECX as left), then word
   C+26h ← 1. `transform_point(P, C+6, Q)` (entry EAX = C+6, EDX = Q,
   ECX = P).
5. K = `pool_alloc([4a966eh])` (entry EAX/ECX as left, EDX = the pool).
   K ≠ 0 (`0x40942d`): context 14h; `smoke_ball_init(K, Q, k)` with k =
   (K hi) | word O+26 (entry EAX = k, EDX = Q, ECX = K); the count
   [[6cbee4h]] −= 4; context 8. Then word O+26 ← ((word O+28 << 2) +
   ((word O+26 + 1) & 3)) mod 2¹⁶.
6. Exit: EAX = (word O+24 = 0) (SETE/AND: 0 or 1), EDX = record+0; FS:0
   restored.

### `debris_update` `0x408bd8` (cdecl O)

Prologue `55 8b ec 83 c4 bc` + `b8 10 7e 49 00` (relocated 6: 1,2,3).
Frame: P = f−44h (12 bytes, the new position), velocity copy V = f−34h
(3 words), angle copy E = f−2Ch (3 words), index word f−36h, record
f−24h, context word f−14h, slot f−4. Result register s (ESI) starts at 0.

1. Frame setup. Gravity g = −0Ah if word [4a9e4eh] = 4 (`0x408c34`), else
   −20h. P ← dwords O+26, O+2A, O+2E; E ← words O+C, O+E, O+10. Then EAX =
   that equality (0/1), EDX = (P.x hi) | word O+C, ECX = (P.y hi) | word O+E.
2. `movement_step(w)` with w = (P.x hi) | word O+5B (also EDX); word E.x
   += AX. V ← words O+4A, O+4C, O+4E (EDX = (callee EDX hi) | O+4A, ECX =
   (P.x hi) | O+4C, EAX = ((O+4A) hi) | O+4E). `movement_step(g)`; word
   O+4E += AX. For O+4A, then O+4C: `multiply_q10(1Eh, s16(word))` (entry
   EAX/EDX = the previous callee's, ECX = g for the first, the first
   product for the second), `movement_step(product)` (entry = the product
   call's registers), word −= AX.
3. V words += the new words O+4E, O+4A, O+4C (16-bit adds; EDX = (callee
   EDX hi) | O+4E, ECX = (product hi) | O+4A, EAX = ((O+4A) hi) | O+4C),
   each V word SAR 1, then P.z, P.x, P.y += s16 of V.z, V.x, V.y.
4. Virtual `[O]+10h(O)` (entry EAX = s16(V.y), EDX = vtable, ECX =
   s16(V.x)); h = `multiply_q10(1F4h, s16(AX))` (entry ECX = O);
   `ground_height(P)` (slots P, 0; entry EAX = P, EDX/ECX = the product
   call's); z = h + s16(AX). s32(z) > s32(P.z) (`0x408d0c`): P.z ← z; word
   O+4E ← −AX of `multiply_q10(1C2h, s16(O+4E))` (entry EAX = z, EDX = the
   s16 height, ECX = the ground call's); dword O+55 ← 0; s16(O+4E) < 2Dh
   (`0x408d34`): s ← 1.
5. O+2E/O+26/O+2A ← P.z/P.x/P.y; dword O+C ← the dword E.x|E.y (E.y is the
   unchanged copy of O+E), word O+10 ← E.z; word O+32 ← 0. ECX = (old O+E
   << 16) | old O+10.
6. s8(O+41) ≤ −1 (`0x408d69`, INC/JLE): return s. If s = 0 (`0x408d72`):
   `timer_dword_step(O+54)` (entry EAX = s8(O+41) + 1, EDX = O+54);
   nonzero EAX (`0x408d89`): return 0.
7. s16(O+59) > −1 (`0x408d94`): K = `pool_alloc([4a961eh])` (entry EAX =
   the pool; EDX = P.y if s = 1, else the timer's EDX; ECX = the step-5
   value if s = 1, else 1). K ≠ 0 (`0x408da7`): context 14h;
   `explosion_create(K, t, O+26, 0, 1)` with t = (pool hi) | word O+59
   (entry EAX = K, EDX = O+26, ECX = t); count −= 4; context 8.
8. Byte [4d1f43h] ≠ 0 (`0x408dde`): [4a964ch] ← [O+5D]; words 4a9664h,
   4a9666h, 4a9668h ← BB8h, 1F40h, 140h; ESI ← O+26; word f−36h ←
   s8(O+41); the pure leaf `0x4089bc(f−36h)` (computed in core: T =
   `0x4a9650` if s16(word 4a9654h) > the s16 index, else T = [4a964ch] and
   the index word −= word 4a9654h); `0x408530([T] + 10 × i, [T+6], O+26,
   0)` with i the s16 index after the leaf (entry EAX = the first slot,
   EDX = i, ECX = 10 × i); then the three words ← 1770h, 3E80h, 1A4h.
9. SI ← 1 (step 7/8 path). Return EAX = ESI: 1, or ((O+26) & FFFF0000h) |
   1 after step 8; EDX = record+0 on every exit.

`0x408530` (debris burst: memset, then `0x4086bc`, which draws RNG stream A
and allocates debris) is reached through the bridge at the same point, so
stream A keeps its order. The virtual +10h (`0x46b910` for the debris
class) is reached through the bridge.

### `meteor_update` `0x409d2c` (cdecl O)

Prologue `55 8b ec 83 c4 cc` + `b8 b0 81 49 00` (relocated 6: 1,2,3).
Frame: S = f−34h (12 bytes), record f−28h, context word f−18h, shape copy
f−8, slot f−4.

1. Frame setup. Byte O+4B = 0 (`0x409d49`): falling, step 4.
2. Landed: shape I = [O+41], F = sar([O+4D], 10), count C = word
   [[[I+4]+20h]] (zero-extended). s32(F) < C (`0x409d63`): word [[I+8]] ←
   F; `movement_step(5DCh)` (entry EAX = F, EDX = [I+8], ECX = C); dword
   O+4D += s16(AX); return EAX = 0, EDX = the callee's EDX.
3. Animation done. Group G = [O+55] ≠ 0 (`0x409d75`): if byte O+4C = 0
   (`0x409d7f`), the group's first actor A = [[G+0Ch]] gets A+2E/A+26/A+2A
   ← O+2E/O+26/O+2A and dword [O+55]+14h ← 0 (G reread; the pending-group
   word of mission.md). K = `pool_alloc([4a968dh])` (entry EAX = the
   pool, EDX = 0 after the copy or I, ECX = the O+26 dword or C); context
   14h; `object_init(K, [[4a9698h]+0Ch])` (entry EAX = [4a9698h], EDX =
   the shape, ECX = K); [K] ← 49a262h, word K+4 ← 9, count += 1, count −=
   4 (two RMWs); context 8; K+2E/K+26/K+2A ← O's position. A null K
   (`0x409db9` taken) writes at 26h..31h and faults: outside the contract.
   Then I' = [O+41] (reread) ≠ 0 (`0x409e26`; always, I was dereferenced
   in step 2): `0x496406(I', 0)` (runtime; entry EAX = I', EDX/ECX as
   left), count += its EAX, context 2Ch, virtual `[I']+18h(I', 3)` (the
   destructor; entry EAX = vtable, EDX = [6cbee4h], ECX = I'), context
   20h. Return EAX = (EAX hi) | 1 (the destructor's high word), EDX =
   record+0.
4. Falling: S ← O+26/O+2A/O+2E; S.z += s16(O+49), S.x += s16(O+45), S.y +=
   s16(O+47). `angle_ratio(7D0h, s16(O+49))` (entry EAX = s16(O+47), EDX
   = s16(O+49), ECX = s16(O+45)); word O+C ← AX; word O+32 ← 0. Dword O+51
   = 0 and s32(S.z) < 50000 (`0x409ece`, `0x409ed7`): sound
   `0x4628e0(2Fh, [4d24eah]+4)` (entry EAX = O+C, EDX = the angle, ECX =
   the camera point); dword O+51 ← 1. h = `terrain_height([4a0be0h], S)`
   (entry EAX = the grid). s32(h) > s32(S.z) (`0x409f06`): S.z ← h; byte
   O+4B ← 1; words O+C, O+32 ← 0; sound `0x4628e0(30h, camera+4)` (entry
   EAX = O+C, EDX = camera+4); `radius_hit(S, BB8h, 2710h, 0, 0)` (entry
   registers the sound's); AX ≠ 0 (`0x409f4b`): byte O+4C ← 1. Then
   O+2E/O+26/O+2A ← S.z/S.x/S.y. Return EAX = 0, EDX = S.y.

The sound calls `0x4628e0` (`sfx_play_at`) draw RNG stream B and advance
the intra-frame clock, so they are called through the bridge at the same
point with the same arguments ([audio.md](audio.md)). `0x496406` is the
runtime's (stays original); the virtual +18h and +20h are reached through
the bridge.

### Callees and their modes

| Hook | Game calls (through the bridge) | Computed in core |
|---|---|---|
| smoke_ball_update | `timer_dword_step` | — |
| smoke_update | `0x49630c`, `timer_word_step`, virtual +20h, `0x4801d4`, `euler_matrix`, `transform_point`, `pool_alloc`, `smoke_ball_init` | — |
| smoke_ball_init | `object_init` | — |
| ground_height | `terrain_height` | — |
| debris_update | `0x49630c`, `movement_step`, `multiply_q10`, virtual +10h, `ground_height`, `timer_dword_step`, `pool_alloc`, `explosion_create`, `0x408530` | `0x4089bc` |
| meteor_update | `0x49630c`, `movement_step`, `pool_alloc`, `object_init`, `0x496406`, virtual +18h, `angle_ratio`, `0x4628e0`, `terrain_height`, `radius_hit` | — |

`0x4089bc` has ten callers; it has its own hook (`debris_table`) for them.

Branch sites: `smoke_ball_update` 2 (`0x409203`, `0x40922c`),
`smoke_update` 10 (`0x4092f7`, `0x409313`, `0x409328`, `0x409330`,
`0x40933b`, `0x40937f`, `0x409386`, `0x4093a1`, `0x4093d5`, `0x40942d`),
`debris_update` 9 (`0x408c34`, `0x408d0c`, `0x408d34`, `0x408d69`,
`0x408d72`, `0x408d89`, `0x408d94`, `0x408da7`, `0x408dde`),
`meteor_update` 10 (`0x409d49`, `0x409d63`, `0x409d75`, `0x409d7f`,
`0x409db9`, `0x409e26`, `0x409ece`, `0x409ed7`, `0x409f06`, `0x409f4b`),
`smoke_ball_init` and `ground_height` none. Proved one-way:
`meteor_update` `0x409db9` (a null flat object faults) and `0x409e26`
(the shape was dereferenced at `0x409d58`). In the DBSIM map `0x4091cc`,
`0x4092dc` and `0x409178` are dead in all six suites; `0x408bd8` and
`0x427d80` run 1000 ×6, `0x409d2c` 81/351/172/0/0/0.

## Death sequence 45fa84

Step 18 of the logic frame, `0x45fa84` (hook `death_sequence`, group
`death`, own code `core/src/death.c`). Contract from the listing; the caller
list, the jump table of `0x42a3f4` and the table bytes were also read with
the REA MCP server on a copy of the same file and checked against the
listing. In vitro (`differential_death_test`); validated live
(every hook replaced, the three suites that reach the step
identical).

**Role.** Runs once per logic frame while the player's byte `+0x99` is set
(the caller `0x45f570` tests it at `0x45f8e0` and skips the step while the
freeze word `0x4d24f2` is set). The first call switches to the death
camera and arms a countdown; every call drives the outside camera control
and steps the countdown; the result is 1 when it has run out, and the
caller then opens the mission-end alert (step 19). Classification:
simulation state (the countdown decides when the mission ends; the view
block and the camera feed `camera_update`).

**Inputs.** No stack argument. Entry ECX: only its high word is used; it
becomes the high word of the first and fourth argument of `0x401c74` on
frames after the first (on the first frame the high word comes from the
ECX that `0x45de30` returns). Entry EAX and EDX are not read. EBX and ESI
are saved and restored; no callee reads EBX, ESI or EDI before writing
them.

Globals (view block V = `0x4d24bc`):

| Address | Use |
|---|---|
| V+0x04 `[0x4d24c0]` | display object D, argument of `0x42a3f4`/`0x42a4c4` (reread before each) |
| V+0x2a `[0x4d24e6]` | player P; read at entry and again as the argument of `0x45df34` |
| V+0x2e `[0x4d24ea]` | camera C; reread before every access |
| V+0x32 word | view mode; 2 selects the outside-view branch |
| V+0x34 word | outside-view word `0x4d24f0` ← 1 |
| V+0x5e word | requested view mode `0x4d251a` ← 3 (mode-2 branch) |
| V+0x60 dword | view subject `0x4d251c` ← P |
| V+0x64 word | view countdown `0x4d2520` ← 2 (other branch) |
| `0x4d2a68` dword | started flag; set to 1 on the first call and never cleared by any code (zero-initialized data, so once per DBSIM process) |
| `0x4d2a58` | countdown object: dword at `+1` (`0x4d2a59`), stepped by `timer_dword_step` |

Class k = s16 at `[[P+0x1f2]+0x50]`, not range-checked. Seven tables, one
entry per class (shipped values for k = 0 / 1):

| Table | Width | k = 0 | k = 1 | Use |
|---|---|---|---|---|
| `0x49f4f4` | dword | 20000 | 6000 | countdown length (`0x4d2a59`) |
| `0x49f4fc` | word | 0x32 | 0x32 | first argument of `0x401c74` |
| `0x49f500` | word | −0x1e | 0x1e | second argument of `0x401c74` |
| `0x49f504` | word | 0 | 0x100 | third argument of `0x401c74` |
| `0x49f508` | word | 0x1f40 | −0x1f40 | camera `+0x26` (zero-extended dword) |
| `0x49f50c` | word | 0 | 0 | camera `+0x2a` |
| `0x49f510` | dword | 0x9c4 | 0x1b58 | camera `+0x22` (low word) |

Each table is two entries long, so any other k reads the neighbouring
table or data; the own code reproduces the same addresses.

**First call** (flag 0): flag ← 1; `0x4d2a59` ← table `0x49f4f4`[k];
V+0x60 ← P. If V+0x32 = 2: `0x42a3f4(D, 2)` (entry EAX = the countdown
length, EDX = D, ECX = entry ECX), then V+0x5e ← 3. Else
`0x45df34([V+0x2a])` (EAX = countdown length, EDX = P from entry, ECX =
the reread P), then V+0x64 ← 2. Then V+0x34 ← 1; `0x42a4c4(D)` (EAX = D,
EDX/ECX from the previous call; after the `0x45df34` branch ECX is its
argument slot, popped); `0x45de30()` (ECX = D, popped). If dword C+0x4a =
0: C+0x26 ← table `0x49f508`[k] zero-extended, word C+0x2a ← table
`0x49f50c`[k]. C reread; if C+0x4a = 0: word C+0x22 ← low word of table
`0x49f510`[k]. The two tests give the same result unless the stores alias
the pointer cell `0x4d24ea`.

**Every call:** C+0x4a ← 0; `0x401c74(C, a, b, c)` with a, c = ECX high
word | table `0x49f4fc`[k] / `0x49f504`[k] and b = table `0x49f500`[k]
(high word 0); entry registers EAX = C, EDX = b, ECX = a. Then C is
reread and C+0x4a ← 1; `timer_dword_step(0x4d2a58)` with EAX = that C and
EDX/ECX as `0x401c74` returned them. `0x401c74` only acts while C+0x4a =
0, so the death camera runs although the camera is otherwise locked
(compare `0x45de30`, which sets C+0x36 only while C+0x4a = 0).

**Result.** EAX = 1 if `timer_dword_step` returned 0, else 0 (`sete`,
masked, sign-extended from AX). EDX and ECX are the two frame dwords
popped on return: on the first call ECX = table `0x49f508`[k]
zero-extended and EDX = table `0x49f50c`[k] with an uninitialized high
word, on later calls both are uninitialized stack. The only caller tests
EAX and then either reloads EDX/ECX (`0x45fa71`) or calls `0x47749c`, which
reads neither, so EDX and ECX are outside the contract. The hook returns
the EDX of its last game call; its ECX is whatever the adapter leaves.

**Callees.** All are game calls through the bridge, in this order and with
these entry registers; the hooked ones run under their own modes.

| Callee | Role | Mode under the hook |
|---|---|---|
| `0x42a3f4` | display state request ([display_modes.md](display_modes.md)) | original, bridge |
| `0x45df34` | view subject setter (input.md) | bridge; hook `view_subject_set` |
| `0x42a4c4` | start a display transition ([display_modes.md](display_modes.md)) | original, bridge |
| `0x45de30` | applies the requested view mode (step 15 of this table) | bridge; hook `view_apply` |
| `0x401c74` | outside camera control (input.md) | bridge; hook `outside_camera` |
| `0x467aa8` | `timer_dword_step` | nested hook, own mode |

None of the callees writes its argument slot, so popping the slot gives
the argument back (checked for `0x45df34` and `0x42a4c4` in the listing).

**Branch sites** (all four reached both ways in vitro): `0x45faa2`
(started flag), `0x45fac6` (view mode 2), `0x45fb2a` and `0x45fb49`
(camera lock).

**Stack.** The original keeps an 8-byte frame (the two attachment words
and a zero word) and calls its callees at its own stack depth. Under the
hook the callees run from host frames through the bridge, so the stack
bytes below the logic frame's ESP differ after the step. No reader of
those bytes is known; the live run of the three suites that reach the step
(c3_09, c4_08, c3_10) found none (compare
[ai.md](ai.md#stack-residue-under-aim_fire), where
such residue mattered).

## Control record 45a810

Row 2 of the logic frame, `0x45a810` (hook `control_record`, group
`control_record`, own code `core/src/control_record.c`); the contract is in
[input.md](input.md#control-record-45a810). The callees stay
original behind the guest bridge, in the original order and with the
original's entry registers: the message pump `0x464c78` (whose nonzero
result still sets `0x4d2532`, which ends the loop), the device poll
`0x45baa8`, the joystick and key readers and the runtime's file calls. The
record construction is own code: key filtering, the joystick mapping, axis
and button selection, latches, the camera pointers +22h/+26h and the tape
entry encoding and decoding. With it, every row of the frame that feeds
simulation state is own code except the game callees that the record step
reaches through the bridge (`0x45feb8` for queued keys, `0x452d40`,
`0x45a4cc`) and the view step 15.

**Time source.** In tape playback the record word +2Eh comes from the tape
entry and `0x45f570` copies it into the time scale `0x4d3b64` (`0x45f595`);
in recording mode the entry stores the current `0x4d3b64`. Both are written
by the own code, so the per-frame elapsed value of an original tape passes
through own code (the suites use the input journal and virtual time
instead).

**Registers after the step.** `0x45f570` stores EAX and reads neither EDX
nor ECX before writing them (`0x45f5e3`; the frozen path reaches
`0x460870`, which reads neither at entry). The modal loops hand ECX/EDX to
a screen virtual, so the hook returns the original's ECX through an entry
shim (input.md, "Registers").


## Object removal: flush 478644, pool free 471bc8, append 478628

The deferred object removal of stage 5 (see "Stage registry" above): the
flush `0x478644` (hook `removal_flush`), its only callee in the game code
`0x471bc8` (hook `pool_free`) and the append `0x478628` (hook
`removal_append`), group `removal`, own code `core/src/removal.c`. A byte
scan of the whole file finds no dword equal to any of the three entry
addresses (no vtable or callback slot), so the direct calls listed below
are all callers.

**Removal list.** A small header: dword `+0` the item array, signed word
`+4` the entry count. Each pool has one at the pool global + 4
(`0x4a95a0`/`0x4a95a4`, `0x4a95fe`/`0x4a9602`, `0x4a961e`/`0x4a9622`,
`0x4a9642`/`0x4a9646`, `0x4a966e`/`0x4a9672`, `0x4a9684`/`0x4a9688`,
`0x4a968d`/`0x4a9691`, `0x4a96aa`/`0x4a96ae`, `0x4a96c2`/`0x4a96c6`,
`0x4a9b7a`/`0x4a9b7e`, `0x4a9db9`/`0x4a9dbd`, `0x6b4f30`/`0x6b4f34`). Each
list global is written once at mission setup and otherwise only read to
be pushed for `0x478628` or `0x478644`, so every entry goes through the
append and leaves through the flush. The lists `0x4a95a4`, `0x4a9b7e`
and `0x4a9dbd` have no append caller (their flushes always find count 0);
`0x4a9602` is filled only by the destructor `0x407e48` (the linked object
`+0x53`), `0x4a9691` by that destructor (`+0x4f`) and by `vehicle_part`
(attachments).

### `removal_append` `0x478628` (cdecl L, v)

Prologue `55 8b ec 8b 45 08 66 8b 50 04` (the byte before it, `0x478627`,
is padding `00`; the one-byte push kept it out of the probe inventory).
No branch, no call, no local.

1. DX ← word `L+4` (EDX keeps its high word), word `L+4` += 1 (`incw`,
   16-bit wrap), ECX ← sx(DX).
2. EAX ← dword `L+0` (read **after** the count write), EDX ← v, dword
   `EAX + 4·ECX` ← v.

Result EAX = the item array, EDX = v, ECX = sx(old count); entry
registers are not read. No bound check: a negative count writes below the
array, and an array that overlaps the header sees the incremented count
(the own code keeps the order; an array pointer near `L` lands the store on
the header).

Callers (15): `0x407e82`, `0x407e99` (destructor `0x407e48`), `0x418175`,
`0x418634` (`vehicle_part`, bridged as `call_478628`), the logic-frame
pool loop `0x45f5d7`, `0x45f618`, `0x45f659`, `0x45f69a`, `0x45f6db`,
`0x45f71c`, `0x45f766`, and `0x461846`, `0x461880`, `0x46b55d`,
`0x46b64c`. ECX is dead at all of them: each reloads ECX before reading
it, or calls `0x407b3c`/`0x4083e0` (flush callbacks; the flush hands its
entry ECX only to `0x49630c`, which does not read it, and to its own
result when the list is empty; the next reader at `0x46185f` reloads it).
EAX and EDX are reproduced exactly (EDX = v is the entry EDX of the
following `0x471c70` calls in the frame loop).

### `pool_free` `0x471bc8` (cdecl P, O)

Prologue `55 8b ec 53 8b 55 08 66 ff 4a 02`. Pool header as above (free
count `+0`, used count `+2`, free head `+0xa`, used head `+0xe`); the node
N = O − 8 (next `+0`, previous `+4`). Every access in order:

1. word `P+2` −= 1, word `P+0` += 1 (16-bit read-modify-writes, in this
   order).
2. prev = dword `N+4`. Nonzero (`0x471be1` falls through): dword `prev+0`
   ← dword `N+0`; zero (taken): dword `P+0xe` ← dword `N+0` (unlink; the
   used-list head case).
3. next = dword `N+0` (reread). Nonzero (`0x471bf2` falls through): dword
   `next+4` ← dword `N+4` (reread).
4. dword `N+0` ← dword `P+0xa`; dword `P+0xa` ← N (push on the free list).
5. dword `N+0` reread; nonzero (`0x471c08` falls through): dword `+4` of
   it ← N.
6. dword `N+4` ← 0.

Result EAX = N, EDX = 0, ECX = the old free head (the step-5 reread);
EBX preserved; entry registers not read. Every reread is kept in the own
code, so aliased links (prev or next = N, the free head = N or = prev) give
the original's bytes. The only caller is the flush (`0x4786a2`), which
writes EAX and EDX before reading them; ECX reaches the flush's result
(below).

### `removal_flush` `0x478644` (cdecl L, P)

Prologue `55 8b ec 83 c4 dc b8 78 14 4a 00 53 56 57`; frame 24h bytes,
EDI = L, EBX = the item cursor, ESI = the index.

1. `0x49630c` with EAX = descriptor `0x4a1478` (its `+4` is −24h: the
   runtime record at EBP−24h, context word EBP−14h; the runtime frame
   records are described in [weapons.md](weapons.md#effect-creation)), EDX/ECX = the entry
   values. It returns EAX = the previous FS:0 head and leaves EDX/ECX.
2. EBX ← dword `L+0` (read **once**), ESI ← 0; the loop runs while the
   signed word SI < the signed word `L+4` (`cmpw 4(%edi),%si; jl` at
   `0x4786b2`; the count is **reread** every round, so a destructor that
   appends to the same list extends the loop):
   1. O ← dword `[EBX]`, stored to EBP−4 and reread from there.
   2. O ≠ 0 (`0x478669` falls through): `0x496406(O, 0)` (runtime RTTI
      size; entry EAX = O, EDX = O, ECX = the previous round's value) →
      EAX; dword `[[0x6cbee4]]` += EAX (the pointer `[0x6cbee4]` is read
      after the call, into ECX); word EBP−14h ← 14h; the virtual
      `[O]+8 (O, 2)` (the destructor without the delete flag; entry EAX =
      O, EDX = the vtable, ECX = `[0x6cbee4]`); word EBP−14h ← 8.
   3. ECX ← dword `[EBX]` (**reread** after the destructor), EAX ← P;
      `pool_free(P, ECX)` (entry EDX = whatever the previous call left).
      A null entry reaches `pool_free(P, 0)` and faults on `[−4]` in the
      original: outside the contract (the stub-driven in-vitro cases take
      the null edge anyway).
   4. ESI += 1, EBX += 4.
3. word `L+4` ← 0; EDX ← dword EBP−24h (the record's saved head); FS:0
   ← EDX.

Result: EAX = the last `pool_free`'s EAX (its node) when the loop ran,
else the head `0x49630c` returned; EDX = the saved FS:0 head; ECX = the
last `pool_free`'s ECX when the loop ran, else the entry ECX. The epilogue
loads only EDX (ECX is not a copy of EDX). The SI compare is 16-bit signed:
a count of 8000h or more is negative and the loop does not run, and SI
cannot wrap before it reaches a positive count.

Callers (12 sites): the eleven stage-5 callbacks `0x405ea4`, `0x4077e8`,
`0x407b3c`, `0x4083e0`, `0x408f9c` (which flushes, then calls `0x408f84`,
the twelfth site), `0x409904`, `0x409a1c`, `0x40a108`, `0x41544c`,
`0x4215a8`, `0x46b188`; each pushes `[pool]` then `[list]`, calls, pops 8
and returns (`0x46b188` stores `0x6b4f3c` first). The stage dispatcher
`0x401d94` reads none of EAX/EDX/ECX after `call *(%ebx)`; the
mission-end path `0x461846…0x461858` calls `0x407b3c`, `0x4077e8`,
`0x409904` in a row and reloads ECX afterwards. `0x409904` is also
bridged by `vehicle_part` (`call_409904` at `0x4187c4`), which returns
its EDX:EAX unchanged, so EAX and EDX are reproduced exactly. ECX reaches
the V74 dispatchers above `vehicle_part` only when `vehicle_part` runs as
original code; `vehicle_part` replaced already drops it. No caller reads
stack residue below its ESP after these calls (each pops or returns at
once), and none reads the flags.

**Callees and their modes.** `0x49630c` (runtime frame setup) runs
original through the bridge with a synthetic EBP, and `0x496406` (runtime)
stays original. The destructor virtual `+8` runs the class's code through
the bridge (for example `0x407e48`: its own runtime record, two appends of
linked objects, a child's virtual `+18h`, the `0x6cbee4` count, no delete
for flag 2; Borland-generated, no read of an uninitialized local; its own
hook is in [Object destructors](#object-destructors)); `pool_free` is
called through the bridge as well, so it keeps its own mode.

**Registers.** `removal_flush` and `pool_free` return EDX:EAX exactly;
their ECX (the old free head of the last free) is modelled by the own code
but not returned by the adapters (dead at every caller above, except
through an original-mode `vehicle_part`). `removal_append` returns EDX:EAX
exactly; its ECX (sx(old count)) is dead at all 15 callers. The flush's
adapter restores FS:0 to the returned EDX, as the epilogue does.

**Branch sites.** `0x478669` (null entry), `0x4786b2` (loop),
`0x471be1` (previous link), `0x471bf2` (next link), `0x471c08` (old free
head); `removal_append` has none.

## Pool iterator 471c70 / 471c18

The two pool-walking helpers named in "Object pools"
above: `pool_prev` (`0x471c70`) and `pool_next` (`0x471c18`), group
`pool_iter`, own code `core/src/pool_iter.c`. Both are hot in every suite
(DBSIM map entry probes ≥ 1000 in all six).

**Layout used.** Pool header P: free count word `+0`, used count word
`+2`, object size word `+4`, array pointer dword `+6` (only read by the
debug report below), used-list head `+0xe` (a node address). A node N
is the 8-byte link header before the object: next `[N+0]`, previous
`[N+4]`; the object pointer is N + 8. A link value L becomes the object
pointer L + 8 when L ≠ 0, else 0 (`L = FFFFFFF8h` therefore also yields
0, by the 32-bit add).

### `pool_next` `0x471c18` (cdecl P, O)

- EDX ← O, EAX ← P (`0x471c1f`, `0x471c22`).
- O = 0 (`0x471c27` falls through): L = `[P+0xe]` (the head, first used
  object); EDX stays 0.
- O ≠ 0: N = O − 8 (EBX and EDX). When N < `0x100000` unsigned
  (`0x471c39` falls through) the **debug report**: the words `[P+4]`,
  `[P+2]`, `[P+0]` are read in that order and sign-extended, then the
  dword `[P+6]`, and the CW3220 `sprintf` thunk `0x496796` (`jmp
  [0x6cc300]`) is called with (EBP−20h, `0x4a0cd8` = "array ptr = %ld,
  num_avail = %d, num_used = %d, size = %d", `[P+6]`, sx`[P]`,
  sx`[P+2]`, sx`[P+4]`) and entry EAX = the buffer, EDX = sx`[P+2]`,
  ECX = sx`[P]`. The shortest possible output is 53 bytes (with the
  NUL), the buffer has 32, so the real `sprintf` always overwrites the
  saved EBP and the return address with text: in the original game this
  branch returns through four ASCII bytes and crashes. Then L = `[N]`
  (the next link).
- Result EAX = L ? L + 8 : 0. EDX = 0 (O = 0), N (O ≠ 0, no report), or
  the `sprintf`'s EDX (report). ECX = the entry ECX, except after the
  report (the `sprintf`'s). No memory is written (apart from the report's
  stack buffer). Flags are dead at the only caller.

Only caller: `0x471c70` (`0x471c88`); no data reference to `0x471c18`
exists in the file (byte search for the address dword).

### `pool_prev` `0x471c70` (cdecl P, O)

- O ≠ 0 (`0x471c80` taken): N = O − 8, L = `[N+4]` (the previous link);
  EAX = L ? L + 8 : 0 (`0x471ca2`). EDX and ECX keep their entry values;
  no call.
- O = 0: the loop `0x471c84`…`0x471c94` calls `0x471c18(P, c)` with c =
  0 first and then each nonzero result, until a call returns 0; EAX = the
  last nonzero result (the tail, the oldest object), 0 for an empty used
  list. Each call gets EAX = the previous call's EAX (the entry EAX first),
  EDX/ECX as the previous call left them (the entry EDX/ECX first). EDX
  and ECX at return are the last `0x471c18` call's: EDX = 0 for an empty
  list, else the tail's node (tail − 8); ECX = the entry ECX (unless a
  report ran). The walk is finite only on a list without a cycle (the
  original then loops forever, the replacement too).

### Callers and registers

42 direct call sites (no data reference): `0x404b86`, `0x404c54`,
`0x41b8e5`, `0x41b9ac`, `0x41ba80`, `0x41cb78`, `0x41e202`, `0x420e87`,
`0x423eeb`, `0x426458`, `0x42846a`, `0x4284ed`, `0x42858f`, `0x428619`,
`0x428646`, `0x428695`, `0x4286c2`, `0x4286ef`, `0x428726`, `0x428753`,
`0x4315b3`, `0x43ff04`, `0x44dfde`, `0x44e006`, `0x44e02e`, the frame
loop's nine (`0x45f5ea` … `0x45f8bd`), `0x461866`, `0x461896`, `0x461958`,
`0x461cea`, `0x461d7e`, `0x46b471`, `0x46b65f`, `0x471cbb` (the skip
helper `0x471cb0`). Every site is followed by `add esp,8`, so the flags
are dead everywhere. EDX after the call is observable: `0x404ae4` keeps
its high word in the count slot (mission_runtime.md, the 140-entry
overflow), and `0x471cb0` returns it in EDX (its high word, all of it when
the walk ends). The replacement therefore returns the exact EDX of every
path; ECX is the entry ECX on every path without the report, which is what
the hook framework restores (popad), so ECX is exact too.

### Replacement and the debug report

`pool_next` computes the walk in core; the report stays a guest call to
`0x496796` through the weapons bridge with the original's arguments, in
the original's read order and with the original's entry EAX/EDX/ECX. Its
buffer is 128 bytes of adapter scratch (the longest possible output is 78
bytes), so the replacement does **not** reproduce the original's stack
smash (the crash) on that branch; it returns L + 8 and the `sprintf`'s
EDX. ECX after the report is the entry ECX instead of the `sprintf`'s.
The branch needs a node below 1 MiB; the pools live in the game heap
(`0x471ad8`), so it is not expected live, but it is not PROVED. It is
covered in vitro by nodes inside a read-only system mapping below
`0x100000` found at run time (no 64 KiB granule is free there in the
harness), with the thunk replaced by a logging stub; both directions of
`0x471c39` are observed, the boundary nodes `0xffff8`…`0x100000` as
well when that address is readable. Live counter
`hk_pool_next_call_496796` should stay 0.

`pool_prev` calls `0x471c18` through the bridge on the O = 0 path (so a
nested `pool_next` keeps its own mode and the callee counters see every
step), with the registers each original call gets, and returns the last
call's EDX. With both hooks replaced, `hk_pool_next_calls` =
`hk_pool_prev_call_471c18` (`0x471c70` is the only caller of
`0x471c18`). No frame local is read before it is written and no entry
register is read (beyond being passed on), so no entry shim. Both hooks
refuse compare mode (game calls).

## Meteor constructor 409b44 and shape instance 490b64

The meteor constructor `0x409b44` (hook `meteor_create`) and the
shape-instance constructor `0x490b64` (hook `shape_instance`), group
`meteor`, own code `core/src/meteor.c`. A byte scan finds no data reference
to either entry; the direct calls below are all callers. Validated live
(every hook replaced, all six map suites identical, every callee counter of
`meteor_create` equal to `hk_meteor_create_calls`).

### `shape_instance` `0x490b64` (cdecl O, S)

Prologue `55 8b ec 53 56 57 8b 5d 08` (relocated 5: 1, 2, 1, 1). No
frame, no local. EBX = O. Every access in order:

1. dword `O+0` ← `4a69f0h`, then ← `4a6a14h` (two vtable stores of the
   base classes); the runtime construction count `[[6cbee4h]]` += 1 (the
   pointer read into EAX, then `inc dword [eax]`); dword `O+0` ← `4a62edh`.
2. dword `O+4` ← S; SI ← word `S+24h` (read **after** the `O+4` store).
3. SI = 0 (`jbe` at `0x490b93` taken, an unsigned test of a word against
   0): dword `O+8` ← 0 (EAX = 0).
4. SI ≠ 0: `0x47a288(2·n, [6c5830h])` (the arena allocation; n = the
   word zero-extended, the size as a dword; entry EAX = `[6c5830h]`, EDX
   = 2·n, ECX = the entry ECX); dword `O+8` ← its EAX (A); then EAX ←
   dword `O+4` (**reread**), EDX ← 2·word `[EAX+24h]` (**reread**), ECX ←
   dword `[EAX+1Ch]`, and the `memcpy` thunk `0x49680e(A, ECX, EDX)`
   (entry EAX = the reread S, EDX/ECX as pushed).
5. EDX ← `[6cbee4h]`; `[EDX]` += 1; EAX ← O.

Result EAX = O, EDX = the counter pointer `[6cbee4h]` (on both paths),
ECX = the entry ECX (path 3) or the `memcpy` thunk's ECX (path 4). Entry
registers are read only on path 4 (ECX passed on to `0x47a288`, which
does not read it). Callers (5): `0x40207c`, `0x40214e`, `0x40222b`
(`object_init` `0x402188`, which calls it through the guest bridge, so
the hook is entered from a replaced `object_init` too), `0x409c90`
(`meteor_create`) and `0x478846` (constructor `0x478838`). ECX is dead at
all five (each reloads ECX before reading it or calls the `memset`
thunk, a cdecl RTL `memset` that does not read it); EDX is reloaded at
the first four and reaches only the `memset` thunk's entry at `0x478846`.
The hook returns EDX:EAX exactly; the own code also models ECX (in vitro
against the original).

### `meteor_create` `0x409b44` (cdecl O, P, G)

Prologue `55 8b ec 83 c4 b8` + `b8 70 81 49 00 53 56 57` (relocated 6:
1, 2, 3). Frame 48h bytes at f = EBP: the runtime record f−24h (the
descriptor `0x498170` has −24h at `+4`), context word f−14h, heap slot
f−4, the sine/cosine matrix M = f−2Ch (four words), the vector V =
f−34h (two dwords), the rotated point R = f−48h (two dwords) with its z
at f−40h, the drop vector D = f−3Ch (three words). EBX = O, EDI = the
first draw, ESI = the speed and then the quotient.

1. EAX ← `498170h`; `0x49630c` (runtime frame setup; entry EDX/ECX = the
   entry values; it links the record into FS:0 and returns the previous
   head in EAX).
2. EDX ← `[4a96b4h]` (the meteor resource table), ECX ← dword `[EDX]`;
   `object_init(O, ECX)` (`0x402188`; entry EAX = `0x49630c`'s).
3. dword `O+0` ← `4982b8h` (the meteor vtable).
4. `r = rng_next(4d2599h)` (stream A; entry EAX = O+45h, EDX/ECX =
   `object_init`'s); ECX ← `4d2599h` (POP); EDI ← r (all 32 bits).
5. `sincos_pair(r, M)` (`0x47eb30`; entry EAX = r, EDX = M, ECX =
   `4d2599h`).
6. `rng_bounded(4d2599h, 61A8h)` (stream A, bound 25000; entry registers
   as `sincos_pair` left them); s = 11170h + sx(AX) (`movsx eax, ax`).
7. dword `O+55h` ← G (the trigger group; read from the stack into EDX
   first); dword V+0 ← 0, dword V+4 ← −s; `rotate_2d(V, M, R)`
   (`0x47ffe8`; entry EAX = M, EDX = V, ECX = R).
8. q = s / 7D0h (`cdq; idiv`, signed, truncating); z = 50 · sar(lo32(q·q),
   1) (`imul esi; sar eax,1`, then ×2, ×5, ×5 in 32 bits), dword f−40h ←
   z; words D+0 ← 0, D+2 ← 7D0h, D+4 ← 0 (in that order, after f−40h).
9. `0x47ff8c(D, M, O+45h)` (the `enter`-prologue 2×2 rotation of words,
   hook `rotate_words`: it writes the words O+45h and O+47h; entry EAX =
   O+45h, EDX = M, ECX = D), a game call through the bridge.
10. word `O+49h` ← lo16(−z / q) (`neg; cdq; idiv esi`; z is reread from
    f−40h); word `O+10h` ← lo16(r) (the heading); word `O+32h` ← 0;
    dword `O+4Dh` ← 0; dword `O+51h` ← 0 (in that order).
11. P (read once from the stack into EAX): f−40h += dword `P+8`, R.x
    += dword `P+0`, R.y += dword `P+4` (each read right before its
    add, in this order); then dword `O+2Eh` ← f−40h, `O+26h` ← R.x, `O+2Ah`
    ← R.y (in this order); byte `O+4Bh` ← 0 (not landed), byte `O+4Ch` ←
    0.
12. `h = 0x47749c(0Ch)` (the game heap; entry EAX = O+26h, EDX = R.y,
    ECX = R.x); ECX ← 0Ch (POP); dword f−4 ← h.
13. h ≠ 0 (`je` at `0x409c7a` not taken): word f−14h ← 14h; ECX ←
    `[4a96b4h]` (**reread**), EAX ← dword `[ECX+4]` (the second shape);
    `shape_instance(h, EAX)` with EDX = h reread from f−4 (entry EAX =
    the shape, EDX = h, ECX = the table); then `[[6cbee4h]]` −= 2 (the
    pointer read into ECX), word f−14h ← 8, EAX ← f−4. h = 0 (taken):
    EAX ← f−4 (0).
14. dword `O+41h` ← EAX (the shape instance or 0); EDX ← `[6cbee4h]`;
    `[EDX]` += 1; ECX ← f−24h (the record's saved head); FS:0 ← ECX;
    EAX ← O.

Result EAX = O, EDX = the counter pointer `[6cbee4h]`, ECX = the saved
FS:0 head. No local is read before it is written (M by `sincos_pair`, R
by `rotate_2d`, the rest by the constructor itself), so no entry shim.
The quotient q cannot be 0: s = 70000 + sx16 lies in 37232 … 102767, so
q lies in 18 … 51; neither IDIV can fault or overflow and the own code's
truncating C division is exact for every value `rng_bounded` can return
(its stub-driven in-vitro cases use arbitrary 16-bit results). Stream A
is drawn twice, `rng_next` before `rng_bounded` (rng.md table), both
through the bridge at the original's call points.

The only caller is `group_activate` (`0x4236c4`, a hook; it calls
`0x409b44` through the guest bridge at `0x423785` and returns the
callee's EDX, so EDX must be exact); it reloads ECX at `0x42378d` and
EAX in its epilogue, and reads no stack residue below its ESP. The map
probes see the constructor only in c1_01 and the acceptance suite
(1/4/2/0/0/0) and the shape instance 147/800/93/≥1000/649/377 (mostly
through `object_init`).

### Callees, modes and the replacement

`meteor_create` calls every callee through the weapons guest bridge with
the original's arguments and entry EAX/EDX/ECX, so the nested hooks keep
their own modes: `object_init`, `rng_next`/`rng_bounded`,
`sincos_pair`/`rotate_2d`, `rotate_words` and `shape_instance`.
`0x49630c` (runtime) runs original with a synthetic EBP (frame scratch of
60h bytes with f at +58h); `0x47749c` (game heap) stays original. The
adapter restores FS:0 to the record's saved head (the own code returns it
as ECX), as the epilogue does. `shape_instance` keeps `0x47a288` (arena
allocation) and the `memcpy` thunk `0x49680e` as guest calls. Both hooks
refuse compare mode (game calls, heap and arena allocations). Callee
counters: `hk_meteor_create_call_<va>` for `49630c`, `402188`, `492ee0`,
`47eb30`, `492f24`, `47ffe8`, `47ff8c`, `47749c`, `490b64`;
`hk_shape_instance_call_47a288` and `_call_49680e`.

**Branch sites.** `0x409c7a` (heap result null), `0x490b93` (point
count zero); both both ways in vitro.

**`enter` prologues.** The hook framework relocates `ENTER imm16, imm8`
(`hook/hook.c`, `instruction_size`: `0xc8` → 4 bytes; `0x47eb30` and
`0x47ffe8` are `enter` prologues).

## Fire allocation 46b430, fire constructor 46b48c, fire release 46b62c

The three helpers of the `FIRE` pool that the damage code calls: `0x46b430`
(hook `fire_alloc`), `0x46b48c` (hook `fire_create`) and `0x46b62c` (hook
`fire_release`), group `fire`, own code `core/src/fire.c`. A byte scan
finds no data reference to any of the three entries; the direct calls
listed below are all callers. None of the three has a runtime exception
record or an `enter` prologue. `0x46b62c` is a release loop over the
`FIRE` pool (below), not a flat-object creation. Validated live (every hook
replaced, all six map suites identical; the pool never ran full,
`fire_alloc`'s `call_471c70` counter 0).

Globals: the `FIRE` pool header pointer `[6b4f30h]` (10 × 5Bh, see the
call table of the logic frame above), its removal list `[6b4f34h]`, the
kind table pointer `[6b4f28h]` (one shape dword per kind), the
burning-fire count `[6b4f38h]` (the destructor `0x46b574` decrements it
and stops sound 33h through `0x462ac4` when it reaches 0), the world
word `[4a9e4eh]` (the first script parameter word, mission.md; the value
4 also selects the low gravity of the debris burst above) and the
runtime construction count `[[6cbee4h]]`.

### `fire_alloc` `0x46b430` (cdecl, one ignored argument)

Prologue `55 8b ec 83 c4 f8` (relocated 6: 1, 2, 3). Locals: c = f−4
(the iterator cursor), m = f−8 (the running minimum). The pushed argument
(5Bh at all four sites, the object size) is never read.

1. EAX ← `[6b4f30h]`; `pool_alloc(pool)` (`0x471b30`; entry EAX = the
   pool, EDX/ECX = the entry values); ECX ← the pool (POP); EDI ← the
   result A.
2. A ≠ 0 (`jne` at `0x46b449` taken): return A.
3. A = 0 (the pool is full): c ← 0, m ← 10000 (2710h). Loop:
   `pool_prev([6b4f30h], c)` (`0x471c70`; the pool pointer **reread**
   every round; entry EAX = the pool, EDX/ECX = what the previous call
   left: first `pool_alloc`'s EDX and the pool), c ← its result F; F = 0
   (`jne` at `0x46b480` not taken) ends the loop. Otherwise v =
   sx16(word `F+59h`) (the lifetime, read after the call); v < m signed
   (`jge` at `0x46b460` not taken): m ← v, EDI ← F.
4. Return EDI: on a full pool the fire with the smallest lifetime below
   10000 in iteration order (oldest first; the first of equal minima), or
   0 when no fire's lifetime is below 10000. The chosen fire is **not**
   freed or unlinked; the constructor reinitialises it in place.

Result EAX = A (step 2) or EDI (step 4); EDX = `pool_alloc`'s EDX (step
2) or the last `pool_prev`'s EDX (step 4); ECX: step 2 returns the
**uninitialized** local c (`pop ecx; pop ecx` at `0x46b487`/`0x46b488`
leave f−4 in ECX: stack residue of an earlier frame), step 4 returns c =
0. Entry EDX/ECX are only passed on to `pool_alloc`. The own code returns
EDX:EAX exactly and ECX = 0 on the scan path; on the allocation path it
reports the entry ECX (the residue is not modelled). ECX is dead at all
four callers (`pop ecx` at `0x4039da`, `0x403a36`, `0x40d4ff`,
`0x40d546`). EDX: `base_parts` (`0x403a48`, `mov dx`) keeps its high
word in the fifth argument of `fire_create`, which reads only the low
word of it; the other paths overwrite EDX first.

### `fire_create` `0x46b48c` (cdecl F, O, w, P, k)

Prologue `55 8b ec 53 56` (relocated 5: 1, 2, 1, 1). No frame local.
EBX = F, ESI = k (all 32 bits; only its low word is used).

1. EDX ← `[6b4f28h]`; ECX ← dword `[EDX + 4·sx16(k)]` (the kind's shape;
   EAX = sx16(k)); `object_init(F, ECX)` (`0x402188`; entry EAX =
   sx16(k), EDX = the table, ECX = the shape).
2. dword `F+0` ← `4a0ba0h` (the fire vtable); flag = (word `[4a9e4eh]`
   = 4), read after `object_init`; EAX ← flag (SETE; AND 1), EDI ←
   flag.
3. flag = 0, or the word k = 3, or k = 1 (`je` at `0x46b4c7`,
   `0x46b4cd`, `jne` at `0x46b4d3`): the burning path 4. Else (world 4
   and another kind) the discard path 5.
4. Burning path. ESI ← F+41h; `memset` thunk `0x49681a(F+41h, 0, 8)`
   (CW3220 `_memset` import; entry EAX = F+41h, EDX/ECX = `object_init`'s
   EDX/ECX). Then dword `F+4Ah` ← O (EDX ← O, the owner); word `F+4Eh`
   ← w (CX ← w; the high word of ECX is the thunk's). P ≠ 0 (`je` at
   `0x46b4f9` not taken): EAX ← P, EDX ← dword `[P]`, dword `F+50h` ←
   EDX, DX ← word `[P+4]`, word `F+54h` ← DX (each read right before its
   store; P may alias F). P = 0: EAX ← F+41h, byte `F+41h` ← 1. Word
   `F+4` ← 4, word `F+57h` ← 40h, DX ← 5, flag = 0 (`jne` at `0x46b524`
   not taken): EDX += 19h (a 32-bit add without a carry out of the
   word); word `F+59h` ← DX (the lifetime: 1Eh, or 5 in world 4).
   Dword `[6b4f38h]` = 0 (`jne` at `0x46b534` not taken): `sfx_play(33h)`
   (`0x462830`, stream B per audio.md; entry EAX = P or F+41h, EDX = (the
   high word of `[P]` or of O) | the lifetime, ECX = (the high word of
   the thunk's ECX) | w), ECX ← 33h (POP). Then dword `[6b4f38h]` += 1
   (a read-modify-write after the sound call); `fire_update(F)`
   (`0x46b678`, the fire's first animation step; entry registers as the
   sound left them, or without the sound as above), ECX ← F (POP).
5. Discard path. Dword `[6b4f38h]` += 1 (also here, although this fire
   never burns; its destructor decrements it again); EDX ← `[6b4f34h]`;
   `removal_append(list, F)` (`0x478628`; entry EAX = F, EDX = the list,
   ECX = `object_init`'s ECX). F is reinitialised but gets no owner,
   node, offset or lifetime.
6. ECX ← `[6cbee4h]` (read after the last call); EAX ← F; dword `[ECX]`
   += 1.

Result EAX = F, EDX = `fire_update`'s EDX (path 4) or `removal_append`'s
(path 5), ECX = the counter pointer `[6cbee4h]`. No entry register is
read. `[6b4f38h]` is tested once (path 4) and incremented once on both
paths.

### `fire_release` `0x46b62c` (cdecl O)

Prologue `55 8b ec 51 53` (relocated 5: 1, 2, 1, 1). One local c = f−4
(the `push ecx` slot, set to 0 before it is read). EDI = O.

1. c ← 0, EAX ← 0. Loop: `pool_prev([6b4f30h], c)` (the pool pointer
   reread every round; entry EAX = the previous EAX (0 first), EDX = the
   previous EDX (the entry EDX first), ECX = the pool); c ← the result
   F; F = 0 (`jne` at `0x46b66e` not taken) ends the loop.
2. F ≠ 0 and dword `F+4Ah` = O (`jne` at `0x46b640` not taken):
   `removal_append([6b4f34h], F)` (the list pointer read after the owner
   compare; entry EAX = F, EDX = the list, ECX = `pool_prev`'s ECX). The
   fire stays in the pool until the stage-5 flush, so the walk continues
   from it.

Result EAX = 0, EDX = the last `pool_prev`'s EDX, ECX = c = 0 (POP).
Only caller: `0x40d4f2` in the region damage `0x40d434` (`pop ecx`, then
`fire_alloc` with this EAX/EDX as entry registers; its `pool_alloc` call
receives the EDX). Every fire whose owner is O is released, so the new
fire of the same object replaces the old ones.

### Callers and what they read

`0x403914` (`base_parts`, a hook that calls `fire_alloc`/`fire_create`
through the guest bridge, see vtables.md) and `0x40d434` (the region
damage, hook `region_break`). After `fire_alloc` the callers test EAX
(the object) and pop ECX; after `fire_create` they reload ECX
(`[6cbee4h]`, count −= 4) and overwrite EDX before reading it
(`0x403c84`, `0x40d57e`); `base_parts`' next round hands EAX (F, or 0
after a failed allocation) to `timer_word_step` as its entry EAX, which
the hooks reproduce exactly. No caller reads stack residue below its ESP
after these calls; the only uninitialized read in the three bodies is
`fire_alloc`'s returned ECX. So no entry shim and no fixed outgoing stack
coordinate are needed.

### Callees, modes and the replacement

Every game call goes through the weapons guest bridge with the original's
arguments and entry EAX/EDX/ECX, so the nested hooks keep their own
modes: `pool_alloc`, `pool_prev`, `object_init`, `sfx_play` (stream B),
`fire_update` and `removal_append`. The `memset` thunk `0x49681a` (RTL)
stays a guest call. The adapters return EDX:EAX; the own code also returns
the original's ECX (exact except `fire_alloc`'s residue). All three refuse
compare mode (game calls, pool allocation). Callee counters:
`hk_fire_alloc_call_471b30`, `_call_471c70`; `hk_fire_create_call_402188`,
`_call_49681a`, `_call_462830`, `_call_46b678`, `_call_478628`;
`hk_fire_release_call_471c70`, `_call_478628`.

**Branch sites.** `fire_alloc` 3 (`0x46b449`, `0x46b460`, `0x46b480`),
`fire_create` 6 (`0x46b4c7`, `0x46b4cd`, `0x46b4d3`, `0x46b4f9`,
`0x46b524`, `0x46b534`), `fire_release` 2 (`0x46b640`, `0x46b66e`).

**Map counts:** `0x46b430` 1/7/0/27/6/5, `0x46b48c` 1/7/0/27/6/5 (so no
live allocation returned 0), `0x46b62c` 1/3/0/9/1/3. The in-vitro cases
carry the evidence for the full-pool scan and world 4.


## Object destructors

`removal_flush` reaches the destructors of the stage-5 pool objects through
slot `+8`; they and the shape-instance destructor pair below them run as ten
hooks of the group `destructors` (core `core/src/destructors.c`: one module,
because the eight object destructors share one `TS_OBJ` tail; the
constructors live in `meteor.c`, `fire.c` and `weapons.c`, and a tail split
over three modules would be duplicated or cross-linked):

| Hook | VA | Class (vtable) | Frame descriptor | Map probe a60/c1s/c1c/c3_09/c4_08/c3_10 |
| --- | --- | --- | --- | --- |
| `explosion_dtor` | `0x407e48` | `EXPLOSION` (`0x497d4c`) | `0x497c28` | 34/253/47/≥1000/224/117 |
| `fire_dtor` | `0x46b574` | `FIRE` (`0x4a0ba0`) | `0x4a0af1` | 0/7/0/25/4/2 |
| `beam_dtor` | `0x40ba94` | `BEAM` (`0x4987c4`) | `0x49867c` | 0/0/92/≥1000/377/55 |
| `debris_dtor` | `0x40f7e6` | `DEBRIS` (`0x497ed8`) | `0x498a18` | 96/242/19/≥1000/286/251 |
| `flat_dtor` | `0x41bd46` | `FlatObj` (`0x49a262`) | `0x49a206` | 2/2/1/11/2/6 |
| `meteor_dtor` | `0x424034` | `METEOR` (`0x4982b8`) | `0x49a6ec` | 1/4/2/0/0/0 |
| `bullet_dtor` | `0x40b64d` | `BULLET` (`0x498628`) | `0x498579` | 0/280/12/≥1000/70/4 |
| `rocket_dtor` | `0x40ab8d` | `ROCKET` (`0x498448`) | `0x4983a0` | 8/6/6/138/9/0 |
| `shape_dtor` | `0x490bdc` | shape instance (`0x4a62ed`) | — | 142/798/89/≥1000/595/380 |
| `shape_base_dtor` | `0x48be20` | shape base (`0x4a6a14`) | — | 142/798/89/≥1000/595/380 |

All ten are cdecl `(O, flag)` with the Borland `delete` flag: bit 0
frees the object with `0x477548`, and only bit 0 of the low byte is tested
(`test byte [ebp+0Ch],1`). `[[6cbee4h]]` is the runtime construction
count (`[6cbee4h]` is a pointer; every decrement reads the pointer first,
then decrements the dword it points to). Entry registers are not read
except as the values the original hands on to its first callee.

**Entries.** A byte scan of the file finds each of the ten entry
addresses in exactly one vtable slot (slot `+8` of the class vtables
above; slot `+18h` of `0x4a62ed` and `0x4a6a14`) and once in the class's
runtime type descriptor (type `+28h`, e.g. `0x427005` for `EXPLOSION`),
which only the runtime's exception cleanup reads. Direct calls: none for
the eight object destructors; `0x490bdc` from `0x47895c` (the destructor
of the derived shape class with vtable `0x4a14c4`, flag 0); `0x48be20`
from 34 sites in 33 functions (among them `0x490c01`), every one
`push 0; push <object>; call 48be20`. In the suites the object
destructors are entered through `removal_flush`'s slot `+8` call with
flag 2, the shape destructors through the `TS_OBJ` tail's slot `+18h`
call with flag 3 (the map counts of `0x490bdc` and `0x48be20` are equal).

### Common parts

**Runtime record.** The eight object destructors open with
`push ebp; mov ebp,esp; add esp,-24h` and call `0x49630c` with EAX = the
frame descriptor and EDX/ECX = the entry values (EXPLOSION, FIRE, BEAM
push EBX and ESI, DEBRIS, FlatObj and METEOR only EBX; BULLET and ROCKET
push EBX/ESI and load ESI = `6cbee4h` first). Each descriptor's `+4` is
−24h: the record at EBP−24h (saved FS:0 head at `+0`), the context word
EBP−14h, the record's counter copy EBP−8; EBP−4 is the local that holds
the shape instance. `0x49630c` returns EAX = the previous FS:0 head and
leaves EDX/ECX. The epilogue loads the saved head from EBP−24h into EAX
(EXPLOSION, FIRE, BEAM), EDX (DEBRIS, FlatObj, METEOR) or ECX (BULLET,
ROCKET) and stores it to FS:0.

### `TS_OBJ` tail

All eight run it after the class part: dword `O+0` ← `0x4a0b80`; S ← dword `O+34h` (the shape instance). S ≠ 0: EBP−4 ← S;
`cmp dword [ebp-4],0` (always nonzero: just stored, and in the variants
that read `O+34h` twice there is no store between the two reads:
**PROVED** fall-through); `0x496406(S, 0)` (the runtime RTTI count) →
EAX; `[[6cbee4h]]` += EAX (the pointer read after the call); word
EBP−14h ← 14h; the virtual `[[S]+18h](S, 3)` (the vtable read after
`0x496406`); word EBP−14h ← 8. Then two decrements of `[[6cbee4h]]`, and
if flag bit 0: `0x477548(O)`, `pop ecx` (ECX = O). Three register
variants:

| Variant | Classes | S test and load | entry of `0x496406` | entry of `[S]+18h` | final decrements |
| --- | --- | --- | --- | --- | --- |
| A | EXPLOSION, FIRE, BEAM | ESI ← `O+34h` once, `test esi,esi` | EAX = S | EAX = vtable, EDX = counter pointer, ECX = S | through EDX, then ECX |
| B | DEBRIS, FlatObj, METEOR | `cmp [O+34h],0`, then EAX ← `O+34h` | EAX = EDX = S | EAX = S, EDX = vtable, ECX = counter pointer | through ECX, then EAX |
| C | BULLET, ROCKET | as B, with EDX ← `O+34h` | EDX = ECX = S | EAX = S, ECX = vtable, EDX = counter pointer | through EAX, then EDX |

(Registers not listed keep the previous value: ECX and EDX of A at the
`0x496406` call, ECX of B and EAX of C at that call are what the class
part left.)

### The class parts

- **A, common start.** After `0x49630c`: EDX ← `[6cbee4h]`, decrement.
  O = 0 (`0x407e67`, `0x46b593`, `0x40bab3` taken): exit (EAX = head,
  EDX = counter pointer, ECX = entry ECX).
- **EXPLOSION `0x407e48`.** `O+0` ← `0x497d4c`. EAX ← dword `O+4Fh`;
  nonzero (`0x407e78`): EDX ← `[4a9691h]` (the removal list of the
  `0x4a968d` pool, [object removal](#object-removal-flush-478644-pool-free-471bc8-append-478628)), `removal_append(EDX, EAX)` (entry
  EAX = the value, EDX = the list). EAX ← dword `O+53h` (read after the
  first append); nonzero (`0x407e8f`): EDX ← `[4a9602h]`,
  `removal_append(EDX, EAX)`. ECX ← `[6cbee4h]`, decrement. Tail A.
- **FIRE `0x46b574`.** `O+0` ← `0x4a0ba0`; dword `[6b4f38h]` −= 1 (the
  burning-fire count); zero (`0x46b5a5` falls through): `sfx_stop(33h)`
  (entry EAX = head, EDX = counter pointer, ECX = entry ECX), `pop ecx`
  (ECX = 33h). ECX ← `[6cbee4h]`, decrement. Tail A.
- **BEAM `0x40ba94`.** `O+0` ← `0x4987c4`; ECX ← dword `O+58h`;
  `0x47758c(ECX)` (the arena free), `pop ecx`. EAX ← `[6cbee4h]`,
  decrement; EDX ← `[6cbee4h]`, decrement. Tail A.
- **B: DEBRIS `0x40f7e6`, FlatObj `0x41bd46`, METEOR `0x424034`.** No
  class part (the compiler emitted no derived vtable store). O = 0
  (`0x40f7fc`, `0x41bd5c`, `0x42404a` taken): exit at once (EAX = EDX =
  head, ECX = entry ECX). Else EDX ← `[6cbee4h]`, decrement; ECX ←
  `[6cbee4h]`, decrement; tail B.
- **C: BULLET `0x40b64d`, ROCKET `0x40ab8d`.** O = 0 (`0x40b669`,
  `0x40aba9` taken): exit (EAX = head, EDX = entry EDX, ECX = head). Else
  three decrements through EDX, ECX, EAX in this order; tail C.

Results for O ≠ 0, flag bit 0 clear / set: A: EAX = head, EDX = counter
pointer / the free's EDX, ECX = counter pointer / O. B: EAX = counter
pointer / the free's EAX, EDX = head, ECX = counter pointer / O. C: EAX
and EDX = counter pointer / the free's, ECX = head.

### `shape_dtor` `0x490bdc` (cdecl S, flag)

Prologue `55 8b ec a1 e4 be 6c 00` (relocated 8: 1, 2, 5; `MOV EAX,
[moffs32]` is on the hook whitelist). No frame record.

1. EAX ← `[6cbee4h]`, decrement. S = 0 (`0x490bec` taken): return (EAX =
   counter pointer, EDX/ECX = entry).
2. `S+0` ← `0x4a62ed`; EDX ← dword `S+8` (the arena block of the point
   words, see `shape_instance` above); `0x47758c(EDX)` (entry EAX =
   counter pointer, ECX = entry ECX); `pop ecx` (ECX = the block).
3. `shape_base_dtor(S, 0)` (`0x48be20`, a hook; entry EAX/EDX as
   `0x47758c` left them, ECX = the block).
4. Flag bit 0 (`0x490c0d` falls through): `0x477548(S)`, `pop ecx` (ECX =
   S).

Result: EDX:EAX of the last call; ECX = S (flag set) or the base
destructor's ECX.

### `shape_base_dtor` `0x48be20` (cdecl S, flag)

Prologue `55 8b ec 8b 45 08` (relocated 6: 1, 2, 3). EAX ← S; EDX ←
`[6cbee4h]`, decrement. S ≠ 0 (`0x48be30` falls through): `S+0` ←
`0x4a6a14`; flag bit 0 (`0x48be3c` falls through): `0x477548(S)` (entry
EAX = S, EDX = counter pointer, ECX = entry ECX), `pop ecx`. Result EAX =
S (0 for S = 0) or the free's EAX, EDX = counter pointer or the free's
EDX, ECX = entry ECX or S.

### Callers and what they read

- `removal_flush` (`0x47868f`, slot `+8`, flag 2): after the call it
  stores the context word, reloads ECX (`0x47869b`) and EAX and passes
  EDX on to `pool_free`, which reads no entry register. Its next calls
  (`pool_free`, `0x496406`, the next destructor) read only their
  arguments and locals they wrote, so no stack residue below its ESP is
  read.
- The `TS_OBJ` tails (slot `+18h`, flag 3): after the shape destructor
  they reload the counter pointer into two of EAX/EDX/ECX. ECX is
  overwritten at once in A and B; in C it reaches `0x477548` (flag set),
  which reads no entry register (`0x476e2c` and `0x47700c` write EAX,
  EDX and ECX before reading them), or the epilogue's head load (flag
  clear).
- `0x47895c` (`call 490bdc` with flag 0) and the 34 direct calls of
  `0x48be20` (flag 0): `add esp,8`, then the caller's own flag test
  (`0x46bfbb` first calls `0x48be20` a second time), then
  `0x477548(this)`/`pop ecx` or `pop ebx; pop ebp; ret`. With their own
  flag clear they return the callee's registers to their caller, which is
  again a slot `+18h` or `+8` site of a void destructor call.
- The type-descriptor entries (`+28h`) are read only by the runtime's
  exception cleanup, which does not run in the suites.

EDX:EAX are reproduced exactly by the adapters. ECX is modelled by the
cores (compared in vitro against the original) but not returned; it is
dead at `removal_flush` and in all eight tails. In the suites the chains
are `removal_flush` → object destructor (flag 2, never frees) → shape
destructor (flag 3, from the tail) → base destructor (flag 0), so every
ECX the hooks leave is overwritten at `0x47869b` or in a tail. No caller
reads the flags or stack residue below its ESP. No entry shim, no fixed
outgoing stack coordinate.

### Callees, modes and the replacement

Every game call goes through the weapons guest bridge with the original's
arguments and entry EAX/EDX/ECX: `0x49630c` (runtime, not hooked; synthetic
EBP f = scratch + 30h in a 40h-byte scratch through
`weapons_impact_guest_call`, as for `removal_flush` and
[effect creation](weapons.md#effect-creation); the adapter
restores FS:0 to the head the epilogue loads), `0x496406` (runtime RTTI
count), the heap free `0x477548` and the arena free `0x47758c` (both
original), the virtual `[S]+18h` (so `shape_dtor`, `shape_base_dtor` or
the original `0x47895c` run in their own modes), and the hooks
`removal_append`, `sfx_stop` and, from `shape_dtor`,
`shape_base_dtor`. All ten refuse compare mode (game calls, heap and
arena frees, the FS:0 record). Callee counters `hk_<hook>_call_<va>` for
`49630c`, `496406`, `477548` and, where called, `478628`, `462ac4`,
`47758c`, `48be20`, and `hk_<hook>_slot_18` for the virtual call.

**Branch sites.** EXPLOSION `0x407e67`, `0x407e78`, `0x407e8f`,
`0x407eb4`, `0x407ebd` (PROVED fall-through), `0x407f03`; FIRE
`0x46b593`, `0x46b5a5`, `0x46b5c2`, `0x46b5cb` (PROVED), `0x46b611`; BEAM
`0x40bab3`, `0x40bae3`, `0x40baec` (PROVED), `0x40bb32`; DEBRIS
`0x40f7fc`, `0x40f818`, `0x40f824` (PROVED), `0x40f869`; FlatObj
`0x41bd5c`, `0x41bd78`, `0x41bd84` (PROVED), `0x41bdc9`; METEOR
`0x42404a`, `0x424066`, `0x424072` (PROVED), `0x4240b7`; BULLET
`0x40b669`, `0x40b681`, `0x40b68d` (PROVED), `0x40b6c7`; ROCKET
`0x40aba9`, `0x40abc1`, `0x40abcd` (PROVED), `0x40ac07`; `shape_dtor`
`0x490bec`, `0x490c0d`; `shape_base_dtor` `0x48be30`, `0x48be3c`.

**Counters.** `hk_<hook>_calls` follow the map probes of the table
above. For the eight object destructors `call_49630c` = calls,
`call_477548` = 0 (flag 2) and `call_496406` = `slot_18` = the entries with
a shape instance; `hk_explosion_dtor_call_478628` at most twice its calls,
`hk_fire_dtor_call_462ac4` at most its calls, `hk_beam_dtor_call_47758c`
= its calls; `hk_shape_dtor_call_47758c` = `_call_48be20` = its calls
and `_call_477548` = its calls when every entry carries flag 3;
`hk_shape_base_dtor_call_477548` = 0 while it is entered only from
`shape_dtor`.

**In vitro** (`destructors_cases.h`, full tier): per hook 1,001,024
direct cases against the original body with logging stubs (pair, ECX of
the core, ordered calls with entry registers and context word, arena,
globals, FS:0, callee counter deltas), 256 installed 31/a7 poison cases,
and 1,536 real-callee cases in which `removal_flush` flushes a consistent
pool of objects of all eight classes (real vtables, shape instances of the
classes `0x4a62ed`, `0x4a14c4` and `0x4a6a14`) over real `0x49630c`,
`0x496406`, `0x47895c`, `pool_free`, `removal_append` and `sfx_stop`, with
only the two frees and the sound driver call `0x464118` stubbed; 31 branch
sites both ways, 8 PROVED; one caught mutation per hook.

**Live.** Validated live (every hook
replaced): all suites identical, no skip or bypass, `shape_dtor` =
`shape_base_dtor` in every suite. The receiver histograms of the original
code show each object destructor on its own class only
([hook_frontier.md](../reference-game/hook_frontier.md#receiver-histograms-of-the-virtual-call-leaves)).

## The logic frame as own code

`0x45f570` (420 instructions, `0x45f570..0x45fa83`) runs as own code behind
the hook `logic_frame` (group `logic_frame`, core `core/src/logic_frame.c`,
adapter `tools/reference-game/proxy/logic_frame_hooks.h`, in vitro
`tools/reference-game/invitro/logic_frame_cases.h`). This
section adds the register flow, the frame and the exception record.

### Caller, frame and exception record

**Caller.** Only `0x45f160` (`call 0x45f570` at `0x45f487`, the main loop);
a byte scan of the whole file finds no dword `0x0045f570` (no vtable or
callback slot). After the call the loop tests AX (`0x45f48c`); on 0 it
writes EDX (`mov edx,[ebx+2Eh]`) and calls `camera_update` `0x4011a0`,
which writes EAX (`0x4011a1`) and ECX (`0x4011c8`) before reading them; on
nonzero it reaches the mission shutdown `0x45f4c5`, which writes EAX and
hands EDX/ECX to `0x464874` or `0x462734`. The caller's EBX is the view
block `0x4d24bc` (set at `0x45f176`).

**Prologue.** `55 8b ec 83 c4 8c b8 58 f5 49 00 53 56 57`: `push ebp; mov
ebp,esp; add esp,-74h; mov eax,49F558h; push ebx; push esi; push edi`. The
caller's EBX/ESI/EDI are saved at EBP−78h/−7Ch/−80h; the outgoing ESP of
every call is EBP−80h minus the pushed slots. No stack argument (cdecl),
no x87 instruction.

**Exception record.** `0x49630c` (runtime; reads EAX and EBP only) places
the record at EBP + `[0x49f558+4]` = EBP − 30h (bytes `d0 ff ff ff` at
`0x49f55c`): `+0` the previous `FS:0` head, `+4` the handler thunk
`0x4967f0`, `+8` the descriptor `0x49f558`, `+0Ch` its caller's ESP
(EBP−80h), `+10h..+15h` zero (the context word is EBP−20h), `+1Ch` the
construction counter `[[0x6cbee4]]` (EBP−14h); then `FS:0` ← record. It
returns EAX = the previous head and leaves EDX/ECX. Both exits reload the
saved head into EDX (`mov edx,[ebp-30h]`) and store it to `FS:0`
(`0x45f84a`, `0x45fa76`). The context word gets 14h/8 around the first
alert's constructor and 2Ch/20h around its destruction, 44h/38h and
5Ch/50h for the second alert (12-byte entries at `0x49f558 + context`
for the contexts 8 … 5Ch, read by the runtime handler only on an unwind;
their meaning was not decoded). The counter `[[0x6cbee4]]` −= 0Ah after each
alert constructor and += 9 before each `0x4543b0`. As rtl_imports.md §4.2
says, DBSIM never throws and has no catch block except the runtime
terminate's; the record only matters when an OS exception unwinds.

**Frame locals** (each written before it is read on every path; no
uninitialized read): EBP−34h the record pointer R (`0x45a810`'s EAX),
EBP−36h the tape time-scale word, EBP−3Ch … −54h the cursors of the seven
pool loops, EBP−58h the actor-pool cursor, EBP−5Ch the group countdown
word, EBP−60h the group pointer, EBP−64h the MECH cursor, EBP−68h and
EBP−70h the player + 92h, EBP−4/−8/−6Ch the first alert object and its
copies, EBP−0Ch/−10h/−74h the second's, EBP−20h the context word,
EBP−30h … −11h the record.

### Globals

Read: `0x4d24d6` (tape playback word), `0x4d24f2` (freeze word, read four
times: `0x45f5ac`, `0x45f856`, `0x45f877`, `0x45f8ea`), the pool globals
`0x4a961e`, `0x4a966e`, `0x4a9684`, `0x6b4f30`, `0x4a9642`, `0x4a96aa`,
`0x4a96c2`, `0x4a95a0`, `0x4a9b7a` (each re-read before every `0x471c70`
call), the removal lists `0x4a9622`, `0x4a9672`, `0x4a9688`, `0x6b4f34`,
`0x4a9646`, `0x4a96ae`, `0x4a96c6` (re-read before every `0x478628`),
`0x4a9e04` (group count word) and `0x4a9e00` (group array; both read
once, into EBP−5Ch/−60h), `0x4d24fc` (step request), the player
`0x4d24e6` (re-read at `0x45f865`, `0x45f8ce`, `0x45f98c`, `0x45f9b4`,
`0x45f9c3`), `0x6cbee4` (the counter pointer, re-read at each adjustment)
and the read-only word table `0x49f5c0`. Written: the time-scale word
`0x4d3b64` (tape playback only), word `0x4d2526` ← 0 (`0x45f740`), word
`0x4d24fc` ← 0 and word `0x4d24f2` ← 1 (step request), the counter
`[[0x6cbee4]]`, the frame and `FS:0`. Objects are only read, for the loop
tests: `[O+45h]` (the group) dword `+14h`, byte `O+99h` (through `lea
eax,[ebx+92h]; cmp byte [eax+7],0`), the player's `+45h` and `+99h`, group
dword `+14h`, and the vtables.

### Order of work and registers at every call

"ret" is the return address the CALL leaves. Entry: EAX is overwritten
(`mov eax,49F558h`) before any read; EDX/ECX and the caller's EBX/ESI/EDI
reach the first two calls unchanged.

1. `0x49630c` (EAX = `0x49f558`, EDX/ECX entry, EBX/ESI/EDI the caller's;
   ret `0x45f583`).
2. `0x45a810` (`control_record`; EAX/EDX/ECX as `0x49630c` left them,
   EBX/ESI/EDI the caller's; ret `0x45f588`). EBP−34h ← EAX (R).
3. Word `0x4d24d6` ≠ 0 (`0x45f593`): EDX ← R, CX ← word `R+2Eh` (ECX's high
   word kept), word EBP−36h ← CX, AX ← that word (EAX's high word = R's),
   word `0x4d3b64` ← AX.
4. ESI ← 0 (`0x45f5aa`). Word `0x4d24f2` ≠ 0 (`0x45f5b4`): skip to step 8.
5. Seven pool loops, in this order: explosions (`0x40813c`, pool
   `0x4a961e`, list `0x4a9622`, cursor EBP−3Ch), smoke balls (`0x4091cc`,
   `0x4a966e`, `0x4a9672`, −40h), smoke (`0x4092dc`, `0x4a9684`,
   `0x4a9688`, −44h), fires (`0x46b678`, `0x6b4f30`, `0x6b4f34`, −48h),
   debris (`0x408bd8`, `0x4a9642`, `0x4a9646`, −4Ch), meteors
   (`0x409d2c`, `0x4a96aa`, `0x4a96ae`, −50h), then word `0x4d2526` ← 0
   and the projectiles (virtual `+14h`, `0x4a96c2`, `0x4a96c6`, −54h).
   Each loop starts with cursor ← 0 through `xor eax,eax` (the first; EAX
   = 0) or `xor edx,edx` (the others; EDX = 0, and EAX is 0 from the
   previous loop's last `0x471c70`). Per round:
   - check: EDI ← cursor, ECX ← `[pool]`; `0x471c70(ECX, EDI)`
     (`pool_prev`; EAX/EDX as left before, EBX = the previous object, the
     caller's EBX in the first round of the first loop); cursor ← EAX, EBX
     ← EAX; EAX = 0 (full dword) ends the loop.
   - update: `U(EBX)` with EAX = EBX and EDX/ECX as `0x471c70` returned,
     or for the projectiles ECX ← `[EBX]` and the virtual `[ECX+14h](EBX)`
     (target read at the call); `pop ecx` (ECX = the slot as the callee
     left it); AX = 0 → next round.
   - removal: EAX ← EBX, EDX ← `[list]`; `0x478628(EDX, EAX)`
     (`removal_append`, ECX = the popped slot); next round.
   ESI = 0 and EDI = the cursor of the round at every update and append.
   Return addresses (update/append/`0x471c70`): `0x45f5c7`/`0x45f5dc`/
   `0x45f5ef`, `0x45f608`/`0x45f61d`/`0x45f630`, `0x45f649`/`0x45f65e`/
   `0x45f671`, `0x45f68a`/`0x45f69f`/`0x45f6b2`, `0x45f6cb`/`0x45f6e0`/
   `0x45f6f3`, `0x45f70c`/`0x45f721`/`0x45f734`, `0x45f756`/`0x45f76b`/
   `0x45f77e`.
6. Actor pool (cursor EBP−58h ← 0 through `xor edx,edx`): `0x471c70(
   [0x4a95a0], cursor)` (ret `0x45f7ba`) as above; for an object O: ECX ←
   `[O+45h]`, EAX ← (dword `ECX+14h` ≠ 0) (`setne al; and eax,1`); AX ≠ 0
   skips O; else EDX ← `[O]` and the virtual `[EDX+18h](O)` (EAX = 0, ECX
   = the group; ret `0x45f7a9`), `pop ecx`; its result is not tested.
7. Groups: DX ← word `0x4a9e04` (EDX's high word kept), word EBP−5Ch ←
   DX; ECX ← `[0x4a9e00]`, EBP−60h ← ECX. Loop at `0x45f7fb`: AX ← word
   EBP−5Ch (EAX's high word kept), word EBP−5Ch −= 1 (16 bits); AX ≠ 0 →
   EAX ← EBP−60h and EBP−60h += 7Ah, AX = 0 → EAX ← 0; EAX = 0 ends the
   loop. For a group G: EDX ← (dword `G+14h` ≠ 0); DX ≠ 0 →
   `0x4236c4(G)` (`group_activate`, ret `0x45f7f1`), else `0x423a74(G)`
   (`order_update`, ret `0x45f7fa`), with EAX = G and ECX = the previous
   popped slot (`[0x4a9e00]` for the first group); `pop ecx`. A count of 0
   runs no group; a negative count runs down through FFFFh (kept).
8. `0x45f818` (also on the frozen path): word `0x4d24fc` ≠ 0 → word
   `0x4d24fc` ← 0, word `0x4d24f2` ← 1.
9. EDX ← EBP−34h (R); `0x460870(EDX)` (`player_input`; EAX as left: 0 after
   the group loop, `control_record`'s or step 3's on the frozen path;
   EBX/EDI the actor loop's last values (0 and the last cursor), the
   caller's on the frozen path; ret `0x45f83d`); `pop ecx`. AX ≠ 0: AX ← 1
   (EAX's high word kept), EDX ← EBP−30h, `FS:0` ← EDX, return.
10. Word `0x4d24f2` = 0 (`0x45f85e`): `0x426b48()` (`timer_scan`, ret
    `0x45f865`), ECX ← `[0x4d24e6]`, `0x426b70(ECX)` (`trigger_scan`, ret
    `0x45f871`), `pop ecx`.
11. `0x45de30()` (view request; ret `0x45f877`), on every path that gets
    here.
12. Word `0x4d24f2` = 0 (`0x45f87f`): `0x4123ac()` (`actor_pass`, ret
    `0x45f886`); EAX ← 0, cursor EBP−64h ← 0; the MECH loop: EDI ← cursor,
    **EDX** ← `[0x4a9b7a]`, `0x471c70(EDX, EDI)` (ret `0x45f8c2`); cursor ←
    EAX, EBX ← EAX; for an object O: EDX ← `[O+45h]`, ECX ← (dword
    `EDX+14h` ≠ 0); CX ≠ 0 skips O; EAX ← O + 92h; byte `EAX+7` (O + 99h)
    ≠ 0 skips O; else `0x41aa5c(O)` (`vehicle_update`, ret `0x45f8b1`; EAX
    = O + 92h, EDX = the group, ECX = 0), `pop ecx`.
13. EDX ← `[0x4d24e6]` + 92h, EBP−68h ← EDX, ECX ← EBP−68h; byte `ECX+7`
    (the player's +99h) ≠ 0 (`0x45f8e4`) → step 14, else step 15.
14. Death path: word `0x4d24f2` ≠ 0 → return 0. `0x45fa84()`
    (`death_sequence`; EDX = ECX = player + 92h; ret `0x45f8fd`); EAX = 0
    → return 0. Else the alert: `0x47749c(57Eh)` (ret `0x45f90f`), `pop
    ecx`, EBP−4 ← EAX; EAX ≠ 0: context 14h, `0x455950(obj, 2, 1, 0)` (EAX
    = EDX = obj, ECX = the popped slot; ret `0x45f92c`), counter −= 0Ah,
    context 8, EAX ← EBP−4. EBP−6Ch ← EAX, EDX ← EBP−6Ch, ECX ← `[EDX]`,
    the virtual `[ECX+8](EDX)` (ret `0x45f952`), `pop ecx`; EAX ←
    EBP−6Ch, EBP−8 ← EAX; EBP−8 ≠ 0: counter += 9 (EDX = the counter
    pointer), context 2Ch, ECX ← EBP−8, `0x4543b0(ECX, 3)` (EAX = obj; ret
    `0x45f979`), context 20h. ESI ← 1, return 1.
15. Outcome: EAX ← `[0x4d24e6]`; `0x4131ac(EAX)` (`outcome_poll`; EDX =
    ECX = player + 92h; ret `0x45f997`); `pop ecx`; EDI ← EAX, EBX ←
    sx(DI). EBX ≤ −1 or EBX = 2 → return 0. EBX = 3: EAX ← `[0x4d24e6]`,
    EDX ← `[EAX+45h]`, `0x423f08(EDX)` (`group_assess`; ECX = the popped
    slot; ret `0x45f9c2`), `pop ecx`; ECX ← `[0x4d24e6]` + 92h, EBP−70h ←
    ECX, EAX ← EBP−70h; byte `EAX+7` ≠ 0 → EBX ← 12h. Then the alert as in
    step 14 with `0x455950(obj, EBX, 1, 0)` (rets `0x45f9ea`, `0x45fa06`,
    `0x45fa2c`, `0x45fa68`; contexts 44h/38h/5Ch/50h; copies EBP−0Ch,
    EBP−74h, EBP−10h); after the virtual `+8`: EDX ← sx(DI) (the status
    before the 12h substitution), `pop ecx`, ECX ← sx(word `0x49f5c0 +
    2·EDX`), ESI ← (EAX = ECX) (`sete al; and eax,1`, all 32 bits).
16. Exit `0x45fa71`: EAX ← ESI, EDX ← EBP−30h (the saved head), `FS:0` ←
    EDX; EBX/ESI/EDI are popped from their save slots.

**Results.** EAX: 0 or 1 (ESI), or `player_input`'s EAX with AX = 1 on the
early return (step 9). EDX: the saved `FS:0` head. ECX: whatever step 9,
14, 15 or 16 left (a popped slot, a callee's ECX, the player + 92h or the
table word).

### Callees and their modes

Hooks reached (each keeps its own mode and counters): `control_record`,
`explosion_update`, `smoke_ball_update`, `smoke_update`, `fire_update`,
`debris_update`, `meteor_update`, `removal_append`, `pool_prev`,
`group_activate`, `order_update`, `player_input`, `timer_scan`,
`trigger_scan`, `actor_pass`, `vehicle_update`, `death_sequence`,
`outcome_poll` and `group_assess` (the 19 hook links), and through the
virtuals the projectile classes' `+14h` (`projectile_flight`,
`bullet_flight`) and the actor-pool buildings' `+18h` (`turret`,
`base_update`, `convoy_think`, `lc_base_update`). **Guest calls that stay
original:** the runtime record `0x49630c`; the view request `0x45de30`
(H3); the heap `0x47749c` (RTL); the alert dialog `0x455950`, its modal
slot `+8` (`0x456000` in the `GeneralAlert` vtable `0x49e120`) and
`0x4543b0` (H3); all classified in
[function_verdicts.md](../reference-game/function_verdicts.md). Register reads of
the callees that make the exact registers necessary: `player_input`
reads EAX's high word (it reaches the command handler's slot) and the
caller's EDI ([input.md](input.md#stack-residue-under-the-command-handler)); `vehicle_update` replaces only AX
before pushing EAX (`0x41aa6b`), so the high word of O + 92h reaches
`0x467924`'s slot; `death_sequence` uses ECX's high word; `0x45de30` hands
its entry EDX/ECX to `0x45e208` on one path.

**Stack residue.** Callees below the frame read stack they did not write:
the group order `0x4231a4` under `player_input` → `command_key` →
`hud_key` reads its priority word from what the earlier rows of this frame
leave there ([input.md](input.md#stack-residue-under-the-command-handler)), and the AI state methods under
`order_update` read residue ([ai.md](ai.md#stack-residue-under-aim_fire)). The replacement
therefore makes every game call at the original stack coordinate (entry
shim, below), so the callees see the original's EBP, EBX/ESI/EDI,
argument slots and return addresses and the residue the original leaves.

### Exception frame under the hook

`0x49630c` runs through the stack bridge with the original EBP, so the
record lies at EBP−30h of the game's stack, links the `FS:0` head that the
frame found at entry and holds handler `0x4967f0`, descriptor `0x49f558`,
ESP = EBP−80h and the counter copy; the core writes the context word and
adjusts `[[0x6cbee4]]` at the original's points. Every callee therefore
sees the same `FS:0` chain, record bytes and context word as under the
original (checked in vitro: the stubs record the `FS:0` head and the head
record's 20h bytes at entry). An OS exception raised in a callee is
dispatched through the same records; DBSIM has no catch block, so the
search ends in the runtime's `__startup` handler and
`UnhandledExceptionFilter` as in the original. Not supported, as for every
hook (hook/README.md): an unwind that resumed in an outer frame would skip
the framework's and the adapter's host frames below the gap. The adapter
stores the saved head back to `FS:0` on return, as both epilogues do.

### Hook

`logic_frame` (group `logic_frame`), cdecl without argument slot,
`ES2_RESULT_PAIR` through the context replacement; compare mode refused
(game calls, dialogs, allocation, the whole simulation step). Prologue
`55 8b ec 83 c4 8c b8 58 f5 49 00 53 56 57`, 6 bytes relocated (1/2/3),
no branch into them. **Entry shim** `logic_frame_entry`: PUSH EBP; the caller's EBX/ESI/EDI to
EBP−78h/−7Ch/−80h; 112 guard-probed pages (448 KiB, `or` 0 keeps their
bytes); the context {EBP, outgoing ESP = EBP−80h, ECX} below the gap; the
framework's dispatch; ECX from the context. The deepest subtree below the
frame is `order_update`'s host frames plus `actor_step`'s 84-page gap
(which holds the trampolines' 80 pages) and host frames, about 350 KiB;
`player_input`'s 64 pages and the modal alert loop with `command_key`'s 48
pages are smaller, so about 100 KiB stay spare. Every game call, `0x49630c`
included, goes through `ai_behaviour_stack_call` with the EBX/ESI/EDI and
return address the core names; the adapter stores the core's ECX in the
context and the record's saved head in `FS:0`. EAX, EDX and ECX return
exactly. Without the shim (direct and legacy calls) the frame lives in a
128-byte local buffer and the calls go through `logic_frame_machine_call`
(EBX/ESI/EDI as named, EBP = the buffer's synthetic EBP). Stack use with
the hook: the caller's depth + 88h + 448 KiB + a few KiB of host frames,
against the original's depth + about 350 KiB (DBSIM's main thread reserves
1 MiB). Replaced hooks nest up to 32 dispatch levels; with the frame
replaced every nested hook is one level deeper, and a compare level starts
only below depth 8 (`ES2_HOOK_MAX_COMPARE_DEPTH`), so a compare-mode hook
at depth 7 under the original frame (for example deep in the AI tree) is
skipped in compare diagnostics; run those with `logic_frame=original`.

Callee counters `hk_logic_frame_`: `call_49630c`, `call_45a810`,
`call_471c70`, `call_40813c`, `call_4091cc`, `call_4092dc`, `call_46b678`,
`call_408bd8`, `call_409d2c`, `call_478628`, `slot_14`, `slot_18`,
`call_4236c4`, `call_423a74`, `call_460870`, `call_426b48`, `call_426b70`,
`call_45de30`, `call_4123ac`, `call_41aa5c`, `call_45fa84`, `call_47749c`,
`call_455950`, `slot_8`, `call_4543b0`, `call_4131ac`, `call_423f08`.

**Branch sites (39):** `0x45f593`, `0x45f5b4`, the seven pool loops'
update tests and loop tests (`0x45f5cb`/`0x45f5f9`, `0x45f60c`/`0x45f63a`,
`0x45f64d`/`0x45f67b`, `0x45f68e`/`0x45f6bc`, `0x45f6cf`/`0x45f6fd`,
`0x45f710`/`0x45f73e`, `0x45f75a`/`0x45f788`), `0x45f7a1`, `0x45f7c4`,
`0x45f7e9`, `0x45f807`, `0x45f816`, `0x45f820`, `0x45f841`, `0x45f85e`,
`0x45f87f`, `0x45f89d`, `0x45f8a9`, `0x45f8cc`, `0x45f8e4`, `0x45f8f2`,
`0x45f8ff`, `0x45f915`, `0x45f95d`, `0x45f9a0`, `0x45f9a9`, `0x45f9b2`,
`0x45f9d9`, `0x45f9f0`, `0x45fa4c`. 35 both ways in vitro; **PROVED**
one-way: `0x45f915` and `0x45f9f0` (a null `0x47749c` result is
dereferenced at `0x45f94d`/`0x45fa27`), `0x45f95d` and `0x45fa4c` (the
object those tests reread is that non-null result), as for `command_key`.

### Validation (in vitro)

`differential_logic_frame_test` / `_full_test` (`logic_frame_cases.h`,
full tier): 1,001,024 direct cases (100,000 on the quick tier): the
original body and the adapter's plain path over logging stubs for all 24
direct callees and the three virtual slots (EDX:EAX, the ordered calls with
arguments, EAX/EDX/ECX/EBX/ESI/EDI and slot counts, the arena with real
pool links, removal lists, groups, players and dialog, the 25 image
globals, `FS:0`, callee-counter deltas); the stubs change what the
original reads again in one call of four (freeze, step and tape words,
player and +99h, pool and list globals switched to spares, group and
object flags, nodes added at a pool's head, the counter pointer). 1,024
frame cases through the entry shim (fast and preserving dispatch, 31/a7
stack, distinct EAX..EDI, the real `0x49630c`): EAX/EDX/ECX, every stub's
EBX/ESI/EDI, EBP and ESP relative to the caller, a hash of the 256 bytes
below its ESP, the `FS:0` head and a hash of the head record's 20h bytes,
changed argument slots (about 7,000 per run), x87 environment, and the
1,024 bytes below the caller's ESP after return. 1,024 real-callee cases
over the real `0x471c70`, `0x471c18`, `0x478628` and `0x49630c` with
`logic_frame`, `pool_prev`, `pool_next` and `removal_append` original or
replaced in all 16 combinations. 256 installed cases with every hook
replaced. Core test `//core:logic_frame_test` (248 checks). Five caught
mutations ([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)),
among them the context word before `0x4543b0`, caught through the record
hash that the callee sees.

### Live expectation and result

`player_input` `0x460870` is called only by this frame (`0x45f838`, on
every path) and has no pointer in the image, so in every suite and record
`hk_logic_frame_calls` = `_call_49630c` = `_call_45a810` = `_call_460870` =
`hk_player_input_calls`: at the compared tick the tick count (a60 1,500,
c1_01-success 6,198, c1_01-collision 2,200, c3_09-loadout-b 8,886,
c4_08-razor-plasma 2,094, c3_10-loadout-hercs 936, c1_01-cockpit-keys
1,000, c3_10-own-building-fire 700, c1_01-free-camera 1,000, c1_01-mouse
600, c1_01-keys 700, c1_02-probe 1,500, c1_08-escort-route 1,500,
c1_01-cheat-destroy 430, c3_09-wingman-orders 3,300, c5_05-moon-missiles
8,262) and one more at exit where the mission ends in the frame (6,199,
8,887, 2,095, 937, 1,501, 431, 8,263; `hk_player_input_calls` of
the all-replaced runs). The single-caller
callees give: `_call_426b48` = `_call_426b70` = `hk_timer_scan_calls`;
`_call_45de30` = the calls minus the frames ended by `player_input` (one
at exit in c1_01-success, c1_02-probe, c5_05); `_call_4123ac` =
`hk_actor_pass_calls`; `_call_41aa5c` = `hk_vehicle_update_calls`;
`_call_4131ac` = `hk_outcome_poll_calls`; `_call_45fa84` =
`hk_death_sequence_calls` (208/62/208/208 in c3_09-loadout-b, c4_08,
c3_10, c1_01-cheat-destroy at the compared tick, one more at exit, 0
elsewhere), and there the alert counters `_call_47749c` = `_call_455950` =
`_slot_8` = `_call_4543b0` = 1 at exit; `_call_423f08` = 0;
`_call_40813c` = `hk_explosion_update_calls`; `_call_408bd8` =
`hk_debris_update_calls`; `_call_409d2c` = `hk_meteor_update_calls`;
`_call_4091cc` = `_call_4092dc` = 0; `_call_46b678` +
`hk_fire_create_call_46b678` = `hk_fire_update_calls`; `_call_4236c4` =
`hk_group_activate_calls`; `_call_423a74` = `hk_order_update_calls`;
`_slot_14` ≥ `hk_projectile_flight_calls` + `hk_bullet_flight_calls`
(equal without beams in flight: the BEAM class's +14h `0x40c2a0` is not a
hook); `_slot_18` > 0 in every suite (the buildings; `base_update` is also
entered from `turret`, `lc_base_update` and `convoy_think`).

Validated live (every hook and the 41 CW3220
imports replaced, heap shadow): all suites identical,
every skipped/bypassed/mismatch counter 0; `hk_logic_frame_calls` =
`hk_logic_frame_call_49630c` = `hk_player_input_calls` = the tick count of
every suite at the compared tick, `_call_4123ac` = `hk_actor_pass_calls`,
`_call_41aa5c` = `hk_vehicle_update_calls`. The 448 KiB shim gap and the
extra host stack caused no fault.

## The render root as own code

The render root `0x45fca8` (128 instructions, `0x45fca8..0x45fe34`), the
stage dispatcher `0x401d94` (21, `0x401d94..0x401dbf`) and the vector copy
`0x40707c` (12, `0x40707c..0x407096`) run as own code behind the hooks
`render_root`, `stage_dispatch` and `vector_copy` (group `render_root`, core
`core/src/render_root.c`, adapter
`tools/reference-game/proxy/render_root_hooks.h`, in vitro
`tools/reference-game/invitro/render_root_cases.h`).

### Callers

A byte scan of the whole file finds none of the dwords `0x0045fca8`,
`0x00401d94`, `0x0040707c`: all three are reached by direct calls only.
`0x45fca8` has 13 call sites: the main loop (`0x45f49b`, once per
iteration that passed the logic frame), the mission load (`0x461df1`, after
the proxy arms at `0x461dc3`, [mission_load.md](mission_load.md)), the
option-parser path `0x45ceb8` (`0x45cfe9`, a tail position: `call; ret`),
the F12/Alt+P dialog `0x45cff0` (`0x45d1d8`, `0x45d256`, `0x45d295`,
`0x45d3f0`, `0x45d414`, `0x45d436`; called by `command_key` at `0x4607d7`)
and the drop sequence `0x45d85c` (`0x45d9be`, `0x45db68`, `0x45dd2b`,
`0x45dd4e`; from the mission load at `0x461f2c`). In the suites only the
first two run (`hk_panel_frame_calls` = ticks + 1 in the all-replaced runs).
`0x401d94` has seven call sites, each followed by `pop ecx` (or, at
`0x45f45a`, by a `cmp` and then `pop ecx`): stages 0 and 1 at `0x45f285`,
`0x45f2b2` and stage 2 at `0x4617b8` (all before arming), stage 3 at
`0x45f432` (right after the mission load), stage 4 at `0x45f45a` and stage
6 at `0x45f4a7` in every main-loop iteration (stage 4 also in paused
iterations), stage 5 at `0x45fcb3` in the render root. `0x40707c` has one
call site, `0x45fd7b` in the render root.

### `render_root` `0x45fca8` (cdecl, no argument)

**Frame.** `push ebx; add esp,-10h; mov ebx,4D24BCh`: no frame pointer;
EBX = the view block B = `0x4d24bc` for the whole function (callees keep
it); EBP, ESI and EDI are the caller's at every call. The caller's EBX is
at T−4 (T = entry ESP), the 16 bytes of locals F at T−14h..T−5 (F+0/F+4
the jolt x/y copy, F+8/F+0Ch the cursor copy, whose address reaches
`0x434520`), the outgoing ESP is T−14h. Every local is written before it
is read. No x87.

**Globals.** Read: bytes `0x4d1f41`, `0x4d1f42` (detail options), words
B+16h/+18h (`0x4d24d2`/`0x4d24d4`) and B+0B1h/+0B3h (`0x4d256d`/
`0x4d256f`), the graphics context `[B+12h]` = `[0x4d24ce]` (read twice,
`0x45fd16` and `0x45fdf5`), the camera `[B+2Eh]` = `[0x4d24ea]` (re-read
before every use), the player `[B+2Ah]` = `[0x4d24e6]`, byte B+9 =
`0x4d24c5`, the map `[0x4a0be0]`, the light table `[0x4a9608]`, the cockpit
`[0x49b088]` (read three times), the cursor `0x4d22c8`/`0x4d22cc`. Written:
words B+16h, B+18h (when they differ), B+0B1h, B+0B3h, the canvas origin
`[[0x4d24ce]+220h]`/`+224h` (the jolt added, later subtracted), F.

**Order and registers.** "ret" is the CALL's return address; "pop" means
the following `pop ecx` (ECX ← the argument slot as the callee left it);
"add" means `add esp,n` (ECX stays the callee's).

| # | ret | Call | Arguments | EAX | EDX | ECX | after |
|---:|---|---|---|---|---|---|---|
| 1 | `0x45fcb8` | `0x401d94` (`stage_dispatch`) | 5 | entry | entry | entry | pop |
| — | | | | AX ← zx byte `0x4d1f41`; word B+16h ← AX; equal to word B+18h → skip 2 | | | |
| 2 | `0x45fcd0` | `0x45d490` (detail 1) | zx byte | the byte | as left | popped | DX ← word B+16h (EDX's high word kept), pop, word B+18h ← DX |
| — | | | | EAX ← zx byte `0x4d1f42`; word B+0B1h ← AX; equal to word B+0B3h → skip 3 | | | |
| 3 | `0x45fcf6` | `0x45d50c` (detail 2) | zx byte | the byte | as left | popped | DX ← word B+0B1h, pop, word B+0B3h ← DX |
| 4 | `0x45fd0a` | `0x42d870` (jolt point) | — | as left | as left | as left | P = EAX; F ← [P], F+4 ← [P+4]; C = [B+12h]; [C+220h] += F, [C+224h] += F+4 (read-modify-write; EAX = C+220h, EDX = [F], ECX = [F+4]) |
| 5 | `0x45fd33` | `0x48c2e4` (camera setup) | cam | cam | [F] | [F+4] | pop |
| 6 | `0x45fd46` | `0x46cb9c` (terrain) | `[0x4a0be0]`, cam, 0 | 5's | cam | `[0x4a0be0]` | add 0Ch |
| 7 | `0x45fd54` | `0x4327ac` (`panel_frame`) | H = `[0x49b088]` | H | 6's | 6's | pop |
| 8 | `0x45fd5a` | `0x42841c` (object binning) | — | 7's | 7's | popped | |
| 9 | `0x45fd5f` | `0x42db18` (3D view in this layout) | — | 8's | 8's | 8's | AL = 0 → step 15 |
| 10 | `0x45fd6c` | `0x48c2e4` | cam | 9's | cam | 9's | pop |
| 11 | `0x45fd80` | `0x40707c` (`vector_copy`) | `[0x4a9608]`, cam+4 | cam+4 | `[0x4a9608]` | popped | add 8 |
| 12 | `0x45fd94` | `0x46cb9c` | `[0x4a0be0]`, cam, 0 | `[0x4a0be0]` | 11's | cam | add 0Ch |
| 13 | `0x45fd9e` | `0x48c444` (view transform) | 0 | 12's | 12's | 12's | pop |
| 14 | `0x45fda8` | `0x42e700` (sky, horizon) | cam | 13's | cam | popped | pop |
| 15 | `0x45fdb8` | `0x42883c` (eye cell objects) | cam, zx byte B+9 | cam | 14's | the byte | add 8 |
| 16 | `0x45fdc4` | `0x41b130` (`cockpit_sync`) | `[B+2Ah]` (player) | as left | player | as left | pop |
| 17 | `0x45fdd7` | `[[H+1F5h]]+8` (HUD draw) | O = `[H+1F5h]` | O | [O] | H (re-read) | pop |
| 18 | `0x45fddd` | `0x433158` (port getter) | — | 17's | 17's | popped | |
| 19 | `0x45fde3` | `[[Q]]+10h` (DialogPort frame) | Q = 18's EAX | Q | 18's | [Q] | pop |
| 20 | `0x45fdf5` | `[[E]]+10h` (EventPort frame) | E = `[H'+20Bh]`, H' = `[0x49b088]` re-read | H' | E | [E] | EAX ← [B+12h] (re-read), pop; [EAX+220h] −= F, [EAX+224h] −= F+4 (EAX = C'+220h, EDX = [F], ECX = [F+4]) |
| 21 | `0x45fe2d` | `0x434520` (widget hover/draw) | H'' = `[0x49b088]`, F+8 | H'' | `[0x4d22cc]` | F+8 | F+8 ← `[0x4d22c8]`, F+0Ch ← `[0x4d22cc]` before; add 8 |

Steps 10–15 run only when AL of `0x42db18` is nonzero. Exit: `add
esp,10h; pop ebx; ret`; **EAX, EDX, ECX = `0x434520`'s**. Because the
context is re-read at step 20, a callee that switches `[0x4d24ce]` between
the add and the subtract leaves the jolt on the first context and takes it
off the second (reproduced; checked in vitro).

**Branch sites (3):** `0x45fcc8` (detail 1), `0x45fcee` (detail 2),
`0x45fd61` (3D view), all both ways in vitro.

### `stage_dispatch` `0x401d94` (cdecl S)

`push ebp; mov ebp,esp; push ebx; push esi; push edi`; EBX ← `0x4a804c`,
EDI ← S, ESI ← 0. Loop at `0x401db2`: `cmp si,[0x497074]; jl` (the count
word, signed 16-bit, re-read every round, against SI; ESI is incremented as
a dword); body: `cmp di,[ebx+4]` (the entry's stage word against the low
word of S); equal → `call [ebx]` (the pointer read at the call, no
arguments; the callee sees EBX = the entry, ESI = its index, EDI = S, EBP =
this frame and EAX/EDX/ECX as the previous callback left them, the
caller's at the first); then `inc esi; add ebx,6`. Returns the last
callback's EAX/EDX/ECX (the entry ones without a call). A callback that
registers a new entry (`0x401d64`) or changes the count or a later entry is
seen in the same pass. The 31 registrations found by a scan of `push
stage; push fn; call 0x401d64` are 19 for stage 2, eleven for stage 5
(`0x405ea4`, `0x4077e8`, `0x407b3c`, `0x4083e0`, `0x408f9c`, `0x409904`,
`0x409a1c`, `0x40a108`, `0x41544c`, `0x4215a8`, `0x46b188`) and one for
stage 6 (`0x401e5c`); none reads a register or a stack slot it did not
write first (the stage-5 ones push two globals and call `removal_flush`;
`0x408f9c` also calls `0x408f84`, `0x46b188` writes `0x6b4f3c`; `0x401e5c`
pushes EBX/ESI before use). All seven callers pop ECX. **Branch sites (2):**
`0x401daa` (stage match), `0x401db9` (count loop), both ways in vitro.

### `vector_copy` `0x40707c` (cdecl D, S)

`[D+8]` ← `[S+8]`, `[D]` ← `[S]`, `[D+4]` ← `[S+4]`, each read right
before its store (overlapping vectors copy as the original). Returns EAX =
D, EDX = `[S+4]` as read, ECX = `[S]` as read (dead at the only caller,
which loads ECX at `0x45fd85`; the framework returns the entry ECX). No
branch.

### Callees and their modes

Hooks reached from the root (each keeps its own mode and counters):
`stage_dispatch`, `panel_frame`, `vector_copy`, `cockpit_sync`, and below
them `removal_flush` and the hooks of the panel, HUD and cockpit trees.
**Guest calls that stay original (H3):** `0x45d490`, `0x45d50c` (detail
levels), `0x42d870` (jolt point), `0x48c2e4` (camera setup of the render
context), `0x46cb9c` (terrain draw), `0x42841c` (object binning),
`0x42db18` (3D view visible in this layout), `0x48c444` (view transform),
`0x42e700` (sky, horizon), `0x42883c` (eye cell objects), the HUD draw
`0x43d704`, the port getter `0x433158`, the DialogPort and EventPort
frames `0x4361cc`/`0x436f70` (message queue and ports, which stay with the
presentation side) and `0x434520`; verdicts in
[function_verdicts.md](../reference-game/function_verdicts.md#the-render-root).
The jolt only moves the canvas origin of the graphics context
`[0x4d24ce]` for the duration of the draws (the screen jolt, [display_modes.md](display_modes.md));
it is not simulation state, but the root reproduces it exactly because
`cockpit_sync` and the draws run while it is applied.

### Stack residue and the entry shim

The root's callees read stack they did not write (the EventPort message
records under `panel_frame`, the reason for its shim, and the draw trees,
which were not audited), so the replacement makes every game call at the
original stack coordinate. **Entry shim** `render_root_entry`: it executes the original's `PUSH EBX` (the
caller's EBX at T−4), reserves `RENDER_ROOT_PAGES` = 116 guard-probed pages
(464 KiB; `or` 0 keeps their bytes), leaves the context {the caller's EBP,
T−14h, ESI, EDI, ECX} below the gap and calls the framework's dispatch;
afterwards it loads ECX from the context (the core's when the replacement
ran), sets ESP to T−14h and does the original's `add esp,10h; pop ebx;
ret`. Every game call goes through `ai_behaviour_stack_call` with EBX =
`0x4d24bc`, the caller's ESI/EDI and EBP, ESP = T−14h minus the pushed
slots and the original return address. EAX, EDX and ECX return exactly.
Without the shim (direct and legacy calls) the locals live in a 16-byte
buffer and the calls go through `logic_frame_machine_call`.

### Stack budget

The 1 MiB main-thread stack holds the main loop (`0x45f160` and its
callers) and then either the logic frame or the render root: they are
siblings in the loop. A nested hook with an own shim, called at the root's
original coordinate, starts its gap there, so it lies **inside** the
root's gap; it does not add to the depth, but the root's gap must exceed
the whole footprint of every callee below T−14h, or that callee's host
frames would overwrite the root's context and host frames. The deepest
callee is `cockpit_sync` (112-page shim gap with its host frames below
it); its callees (the weapon fire tree, the HUD trees) run above its gap,
inside its 448 KiB gap. `panel_frame`'s 48 pages,
the stage callbacks (`removal_flush`, the destructors) and the H3 draw
trees are much smaller. With `cockpit_sync` original (not installed) the
original `0x41b130` and the same callee trees run at the coordinate, which
also fits in 448 KiB. Measured in vitro (`differential_render_root_test`,
probes that paint 640 KiB below the caller and find the deepest changed
dword): the shimmed root writes 475,816 (fast dispatch) / 475,928
(preserving) bytes below its caller's ESP, i.e. return address, `PUSH
EBX`, the 475,136-byte gap and 672/784 bytes of context and host frames;
`cockpit_sync` through its own shim over the cockpit_sync group's stubbed
world writes 459,496 / 459,608 bytes below its caller's ESP (argument,
return address, `PUSH EBP`, the 458,752-byte gap, 732/844 bytes of context
and host frames). Under the root (its caller's ESP is the root's T−14h)
that is at most 459,628 bytes below T, **15,512 bytes above the root's
context** (the probe fails below zero). Peak main-thread use with the root
replaced: the main loop's depth + about 465 KiB, against the logic frame's
depth + about 450 KiB; both well inside 1 MiB.

The F12/Alt+P dialog `0x45cff0` calls the root from inside `command_key`
under `player_input` under the logic frame. There the root's subtree
(`cockpit_sync`'s 448 KiB gap alone, with the root original or replaced)
starts about 1–2 KiB below the logic frame's coordinate and reaches below
the logic frame's 448 KiB gap, where the logic frame's host frames live;
the [shim stack floor](#shim-stack-budget) keeps them apart.

### Hooks

`render_root` (cdecl, no argument slot, `ES2_RESULT_PAIR`, compare
refused: game calls, draws, nested shims), prologue `53 83 c4 f0 bb bc 24
4d 00`, 9 bytes relocated (1/3/5), no branch into them. `stage_dispatch`
(one slot, compare refused: game calls through the table), prologue `55 8b
ec 53 56`, 5 bytes (1/2/1/1); no entry shim: the callbacks run on the host
stack through `logic_frame_machine_call` with the original's EBX/ESI/EDI
and EBP at a host copy of the frame ([EBP+8] = S). `vector_copy` (two
slots, compare **accepted**: span {D, 0, 12}, comparator: the bytes, EAX −
D equal, EDX equal; the vectors must be disjoint, as at the only caller),
prologue `55 8b ec 8b 45 08`, 6 bytes (1/2/3).

Callee counters `hk_render_root_`: `call_401d94`, `call_45d490`,
`call_45d50c`, `call_42d870`, `call_48c2e4`, `call_46cb9c`, `call_4327ac`,
`call_42841c`, `call_42db18`, `call_40707c`, `call_48c444`, `call_42e700`,
`call_42883c`, `call_41b130`, `slot_8`, `call_433158`, `slot_10` (both
port frames), `call_434520`. `hk_stage_dispatch_`: `call_405ea4`,
`call_4077e8`, `call_407b3c`, `call_4083e0`, `call_408f9c`, `call_409904`,
`call_409a1c`, `call_40a108`, `call_41544c`, `call_4215a8`, `call_46b188`,
`call_401e5c` (keyed by the original VA of the table pointer).

### Validation (in vitro)

`differential_render_root_test` / `_full_test` (`render_root_cases.h`,
two shards). **render_root**: 1,001,024 direct cases (100,000 on the quick
tier): the original body and the adapter's plain path over logging stubs
for its 16 direct callees and the four slot targets (EDX:EAX, the ordered
calls with arguments, slot counts and entry EAX/EDX/ECX, the arena with two
cockpits, objects, cameras, contexts, jolt points and lights, the 333 bytes
of image globals, the callee-counter deltas of the first 4,096 cases); the
stubs change what the original reads again in one call of four. 1,024
frame cases through the entry shim (fast and preserving dispatch, 31/a7
stack, distinct EAX..EDI): EAX/EDX/ECX, every stub's EBX/ESI/EDI, EBP and
ESP relative to the caller, a hash of the 256 bytes below its ESP, changed
argument slots (about 5,000 per run), x87 environment and the 1,024 bytes
below the caller's ESP after return; the depth and nested-budget probes
above. 1,024 real-callee cases over the real `0x401d94` and `0x40707c`
(stage callbacks stubbed at their own VAs) with `render_root`,
`stage_dispatch` and `vector_copy` original or replaced in all eight
combinations. **stage_dispatch**: 1,001,024 direct cases (about 5.6 million
callback calls; tables of 48 entries with stages 4/5/6 or random words,
counts 0..48 or negative, stages with random high words; callback stubs
record EBX/ESI/EDI and change the count or a later entry) and 1,024
installed 31/a7 cases. **vector_copy**: 1,001,024 direct cases (35 %
overlapping, any alignment), 1,024 installed 31/a7 cases and 512
installed compare cases (exact 0 mismatches, skewed 256). Branch sites 3
+ 2, all both ways; no PROVED edge. Core test `//core:render_root_test` (188 checks).
Seven caught mutations
([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

### Live expectation and result

At the compared tick N of every suite (N = the tick count: 1,500 a60,
6,198 c1_01-success, 2,200 c1_01-collision, 8,886 c3_09-loadout-b, 2,094
c4_08, 936 c3_10, …): `hk_render_root_calls` = `hk_panel_frame_calls` =
N + 1 (the mission-load render at `0x461df1`; 1,501, 6,199, 2,201, 8,887,
2,095, 937 as `hk_panel_frame_calls` in the all-replaced runs), at exit the same
value where the mission ends in the logic frame (the frame that ends it
returns before the render). `_call_401d94` = `_call_42d870` =
`_call_4327ac` = `_call_42841c` = `_call_42db18` = `_call_41b130` =
`_slot_8` = `_call_433158` = `_call_434520` = `hk_render_root_calls`,
`_slot_10` = twice that; `_call_40707c` = `_call_48c444` = `_call_42e700`
= `_call_42883c` = `hk_vector_copy_calls` = the renders with the 3D view
(all of them in the flight suites, fewer when a layout hides the view);
`_call_48c2e4` = `_call_46cb9c` = `hk_render_root_calls` +
`hk_vector_copy_calls`; `_call_45d490`, `_call_45d50c` small (only when a
detail option byte differs from its word). `hk_cockpit_sync_calls` =
`hk_render_root_calls` (its load-time call at `0x461d02` is before
arming). `hk_stage_dispatch_calls` = 3N + 2 at the compared tick (stage 3
once, stages 4 and 6 N times, stage 5 N + 1 times) plus one per paused
main-loop iteration (stage 4); `hk_stage_dispatch_call_409904` and the
other ten stage-5 callbacks = N + 1 each, `_call_401e5c` = N.
`hk_removal_flush_calls` ≥ 11 × (N + 1) as before.

Validated live (every hook and the 41 CW3220
imports replaced, heap shadow): all suites identical, every
skipped/bypassed/mismatch counter 0; `hk_render_root_calls` =
`hk_cockpit_sync_calls` in every suite. The 116-page shim caused no fault.

## Shim stack budget

The stack footprint of nested entry shims and the shared stack floor that
keeps their host frames apart. No hook of its own; the floor lives in the
entry shims and their two stack bridges. The call graph was reconstructed
from the listing.

### The problem

A gapped entry shim (`command_key`, `actor_step`, `cockpit_sync`,
`logic_frame`, `render_root` and the others)
runs the game's code above a guard-probed gap at the original stack
coordinate and keeps its own host frames (the context, the framework
dispatch, the adapter, the core and the stack bridge) **below** the gap.
When the shim's replacement calls game code through the bridge at the
original coordinate and that code reaches a second gapped shim, the inner
shim's gap starts at a shallow coordinate inside the outer gap and extends
below it, so the inner host frames land where the **outer** host frames
live and overwrite them. One instance is reachable: the F12/Alt+P
dialog `0x45cff0` (`command_key` case `0x58`/`0x219`) calls the render root
from inside `command_key`, under `player_input`, under the logic frame, so
`cockpit_sync`'s 448 KiB gap (and `render_root`'s 464 KiB) reaches below the
logic frame's 448 KiB gap into its host frames.

The reconstructed call graph (direct calls plus the known callback edges:
the vtable slots of vtables.md, the AI-state member pointers, the stage
callbacks, the window procedures `0x466034`/`0x462398` and the global
function pointers) makes this not a single pair but a dense set. Counting
only direct calls and the named callback edges, the gapped-shim pairs whose
inner shim is statically reachable from the outer shim's replacement and
whose inner gap ends below the outer host frames (gap sizes without the
shared floor) are:

| Outer O | Inner I | Path (game code between the shims) | Depth O→I (bytes) | gap(O)−(depth+gap(I)+8 KiB) |
|---|---|---|---:|---:|
| `logic_frame` | `cockpit_sync` | `45a810`→`45feb8`(`command_key`)→`45cff0`→`45fca8`(`render_root`)→`41b130` | 684 | −2,732 |
| `logic_frame` | `render_root` | `45a810`→`45feb8`→`45cff0`→`45fca8` | 656 | −19,088 |
| `player_input` | `cockpit_sync` | `45feb8`→`45cff0`→`45fca8`→`41b130` | 488 | −199,144 |
| `player_input` | `render_root` | `45feb8`→`45cff0`→`45fca8` | 460 | −215,500 |
| `command_key` | `panel_frame` | `45cff0`→`45fca8`→`4327ac` | 324 | −2,372 |
| `command_key` | `cockpit_sync` | `45cff0`→`45fca8`→`41b130` | 324 | −264,516 |
| `command_key` | `render_root` | `45cff0`→`45fca8` | 296 | −280,872 |

The depth O→I is the sum of frame sizes along the deepest path from O's
original coordinate to the CALL of I (the listing's per-function ESP
tracking); the last column is the budget without the floor, `gap(O) ≥ depth + gap(I) +
host`, with an 8 KiB host bound. Every row is negative: the inner gap ends
below the outer host frames. The AI-tree nestings (`attack_step` →
`aim_select` → `aim_fire` → `weapon_choose`/`torso_aim`, documented in
ai.md) all have large positive margins and are **not** affected; it is only
the dialog/draw shims reached from the input and logic-frame shims that
overflow, because the render root's and cockpit sync's gaps (464/448 KiB)
are nearly the whole of the logic frame's 448 KiB gap.

A per-shim regrowth of the gaps cannot fix this. With the full vtable and
member-pointer graph included, almost every gapped shim becomes reachable
from almost every other (about 500 violating pairs), and two of them —
`render_root` and `command_key` — are reachable from themselves (the render
root's DialogPort/EventPort frames and the window procedure can re-enter
through dialogs), so a gap large enough for the worst nesting does not
exist.

### The fix: a shared stack floor

A global `es2_machine_x86_floor` (`hook/machine_x86.S`, declared in
`es2/hook/machine.h`) holds the lowest ESP at which a machine call switched
from host frames to a game coordinate and is still suspended, or 0 when none
is; every host frame alive on the main thread lies at or above it. Every
x86-backend `es2_machine_call` with a nonzero ESP — among them
`ai_behaviour_stack_call` (`ai_behaviour_hooks.h`, used by the logic frame,
player input, command key, hud key, torso aim, the render root, cockpit
sync, panel frame and the AI shims) and `weapons_round3_stack_call` (the
emitters) — saves the floor, lowers it to the end of its host frame for the
duration of the call and restores it on return, so it nests with the
dispatch.

After its guard-probe loop, before it writes its context, every gapped shim
continues at `es2_machine_floor` (`shim_gap`, `entry_shim.h`): if the floor is set, lies on this thread's
committed stack (at or above the TIB `StackLimit` at `FS:8`) and is below
the shim's current ESP, ESP moves **down** to the floor. So the inner
shim's context and all its host frames lie below every suspended outer host
frame, and no outer host frame is overwritten. ESP only ever moves down:
the gap is never shortened, the game's stack coordinates and the bytes the
game reads above the gap are unchanged (the residue reads that motivated
the shims stay exact), and the skipped pages between the gap's end and the
floor are already committed (above `StackLimit`). Nothing is stored
between the gap's end and the floor. **Trap:** a store at the gap's end
− 4 (such as a `PUSH` of a register for the `StackLimit` compare) lies in the
suspended bridge's host frame when a probe ends a few bytes above the floor.
In es2sim's `c1_01-options-dialog` replay `cockpit_sync`'s 112-page probe
under the F12 dialog ends 4 bytes above the floor; such a push replaces the
saved EBP of player input's `ai_behaviour_stack_call` with EAX (the dialog's
view object), and the bridge's return leaves through that pointer.
`rr_floor_budget` (below) also sweeps 64 floors just below the probe's end
with EAX unequal to the paint pattern; with a `PUSH`, 15 of them lose a
sentinel dword. The
render root's shim saves EBX at T−4, probes its gap below T−4 and writes
its context at the floor; the outgoing ESP it names is T−14h.

Correctness: the floor is monotone along a nesting (each bridge lowers it,
each shim moves to it or stays deeper), so inner host regions stack strictly
below outer ones and never alias them. Because each inner shim probes its
gap from a shallow game coordinate and then relocates its host frames to
the floor, the peak main-thread depth is not the sum of the gaps but the
**deepest single gap** (`render_root`, 464 KiB) plus the host frames
stacked at the floor (measured below, about 0.8 KiB per level). Even the
32-level dispatch maximum adds under 32 KiB, so the peak stays near 500 KiB,
well inside the 1 MiB main-thread reserve (`SizeOfStackReserve` `0x100000`).
Recursion through `render_root` or `command_key` is bounded the same way
(each level adds host frames, not a gap) and by the game's own finite
dialog/draw nesting.

### Validation (in vitro)

`differential_render_root_test` / `_full_test` gains `rr_floor_budget`
(`render_root_cases.h`): with `cockpit_sync` installed replaced through its
own entry shim, `rr_floor_run` paints the stack, sets `es2_machine_x86_floor` to a
stand-in outer host-frame address and calls the shim. With a floor deeper
than the shim's 448 KiB gap the shim relocates its context to the floor
(deepest write 590,568 bytes below the caller, i.e. the 589,824-byte floor
plus 744 host bytes); with a floor shallower than the gap the shim keeps
its own 458,752-byte gap (deepest write 459,512); in both runs, and with no
floor, the 64 bytes at and above the floor keep their paint pattern, so an
outer host frame there is untouched. The run also drives the real bridge's
floor save/lower/restore (relocated `cockpit_sync` calls its own callees
through `ai_behaviour_stack_call`). The reachable-pair table and the
F12/Alt+P chain's gap list are encoded in `rr_reach_pairs` /
`rr_f12_gap_pages`; the test asserts the chain peak (≈520 KiB) stays under
1 MiB. The pre-existing `rr_nested_budget` still passes (cockpit_sync under
the root, 15,508 bytes above the root's host frames). All sixteen gapped
groups' quick and full in-vitro tiers are unchanged with the shim edits.

### Live

The floor changes no replacement's result or counters. The suites
`c1_01-options-dialog` (F12 at 300, Alt+P at 500) and
`c3_10-detail-options` (F12 at 204 and 600) open the dialog `0x45cff0`,
which calls the render root (and cockpit sync, panel frame) under the logic
frame. In the own process, where every shim of the chain runs, both replay
identically for 700 ticks. With a floor macro that pushes EAX both stopped
with an access violation at the first dialog key: a bisect over the shim
ranges showed the stop needs `player_input` and `render_root`, and a
watchpoint on the saved EBP named the push of `cockpit_sync`'s shim.

## The stage-5 flushes and the manager slot methods

Twelve removal flushes (the eleven stage-5 callbacks of the table
`0x4a804c` and `0x408f84`, which one of them calls) run as own code behind
hooks of the group `stage_flush`; ten one-line slot methods of the object
base and of the two animation clip classes run behind hooks of the group
`manager_slots`. Core in `core/src/render_root.c` (flushes),
`core/src/fire.c` (the object base methods) and `core/src/animation.c`
(clip methods); adapters `tools/reference-game/proxy/stage_flush_hooks.h`
and `manager_slots_hooks.h`; in vitro `stage_flush_cases.h` and
`manager_slots_cases.h`. Contracts from the listing and a byte scan of the
file.

### The flushes

Every flush has the same form (no frame, no local): `mov eax,[P]` (`a1`),
`push eax`, `mov edx,[L]` (`8b 15`), `push edx`, `call 0x478644`, `add
esp,8`, `ret`, where L = P + 4 is the pool's removal list (the pairs of
[object removal](#object-removal-flush-478644-pool-free-471bc8-append-478628)). So `removal_flush(L, P)` runs with EAX = `[P]`, EDX =
`[L]` (P read first, each global read once) and ECX = the caller's ECX.
Two differ after the call: `0x408f9c` then calls `0x408f84` (no argument;
EAX/EDX/ECX are the flush's results), `0x46b188` stores dword `0x6b4f3c` ←
`7fffffffh`.

| Callback | Hook | Pool P | Registered at | After the flush |
|---|---|---|---|---|
| `0x405ea4` | `flush_buildings` | `0x4a95a0` (buildings and vehicles; the list has no append caller) | `0x40697e` | |
| `0x4077e8` | `flush_lights` | `0x4a95fe` | `0x4078ea` | |
| `0x407b3c` | `flush_explosions` | `0x4a961e` | `0x4082a6` | |
| `0x4083e0` | `flush_debris` | `0x4a9642` | `0x408ebe` | |
| `0x408f84` | `flush_smoke_balls` | `0x4a966e` | not registered (called at `0x408fb1`) | |
| `0x408f9c` | `flush_smoke` | `0x4a9684` | `0x4094ae` | `0x408f84()` |
| `0x409904` | `flush_attached` | `0x4a968d` (150 × 41h bytes; appended by the destructor `0x407e48` and `vehicle_part`) | `0x409953` | |
| `0x409a1c` | `flush_meteors` | `0x4a96aa` | `0x409f9a` | |
| `0x40a108` | `flush_projectiles` | `0x4a96c2` | `0x40a16e` | |
| `0x41544c` | `flush_mechs` | `0x4a9b7a` (no append caller) | `0x41bc7e` | |
| `0x4215a8` | `flush_flyers` | `0x4a9db9` (no append caller) | `0x421fc3` | |
| `0x46b188` | `flush_fires` | `0x6b4f30` | `0x46b882` | `[0x6b4f3c]` ← `7fffffffh` |

"Registered at" is the `push fn` of the `push 5; push fn; call 0x401d64`
registration, the only dword equal to the address in the file. There is no
twelfth stage-5 registration; the twelfth flush is `0x408f84`, reached
only from `0x408f9c`. Direct calls besides the table: the mission-end
drains `0x46184e` (`0x407b3c`), `0x461853` (`0x4077e8`), `0x461858`
(`0x409904`), `0x461888` (`0x4083e0`), and `vehicle_part`'s `0x4187c4`
(`0x409904`).

**Results.** EDX:EAX = the last callee's (`removal_flush`'s, or
`0x408f84`'s for `0x408f9c`), ECX the same callee's. The hooks return
EDX:EAX exactly; the framework returns the entry ECX, which equals the
original's whenever `removal_flush` is replaced (its adapter returns its
entry ECX, and the flush passes its own entry ECX on). ECX is dead at
every caller: the dispatcher reads no register after `call [ebx]`; at the
mission-end drain the next flush passes it on only as the entry ECX of
`removal_flush`, which consumes none (`49630c`, `496406`, the destructor
and `pool_free` get ECX written first, [object removal](#object-removal-flush-478644-pool-free-471bc8-append-478628)), and the chain ends
at `mov ecx,[..]` (`0x46185f`, `0x46188f`); after `0x4187c4` ECX reaches
the V74 dispatchers only through an original-mode `vehicle_part`, as
before ([object removal](#object-removal-flush-478644-pool-free-471bc8-append-478628)). No entry register other than ECX is read, no stack
below the entry ESP is read (the callees run on the host stack, as they
already do under the replaced `stage_dispatch`), the flags are dead.

**Callees and their modes.** `removal_flush` is a hook (group `removal`):
the flushes call it through the weapons guest bridge, so it keeps its own
mode; so does `0x408f84` (`flush_smoke_balls`) under `0x408f9c`. Counters
`hk_<flush>_call_478644` (all twelve) and `hk_flush_smoke_call_408f84`.

**Prologue.** `mov eax,[P]` (five bytes, `a1`, on the hook whitelist
because its operand is absolute) is relocated; the signature also checks
the push, the list load and its push (13 bytes). No branch.

### Hooks of their own; the table stays the dispatch

`stage_dispatch` ([render root](#the-render-root-as-own-code)) calls each table entry through the guest bridge
at the pointer it reads from the table. With the flushes as hooks that
pointer is still the original VA, whose patched prologue enters the hook:
the table `0x4a804c` stays the source of truth (an entry registered or
changed by any code, the 19 stage-2 entries and the stage-6 `0x401e5c` are
called as before), the direct callers above reach the same own code, and
each flush keeps its own mode and counter. Computing the twelve inside
`stage_dispatch`'s core would have bypassed their modes and left the
direct callers original. `stage_dispatch` is therefore unchanged: its
counters `hk_stage_dispatch_call_<va>` keep counting the table calls (N +
1 each at tick N, [render root](#the-render-root-as-own-code)); with the flushes replaced each of those
calls lands in own code, which `hk_flush_<x>_calls` shows: equal to the
dispatcher's counter of its VA plus the direct calls (`flush_attached`:
plus `vehicle_part`'s `call_409904`; `flush_smoke_balls`: equal to
`hk_flush_smoke_calls`; the mission-end drains run after the compared
tick).

### The object base methods (TS_OBJ slots +00, +04, +10)

A scan for the three dwords finds `0x46b8ea` in five class tables at slot
`+00` (`0x4973ac`, `0x497d4c`, `0x498084`, `0x4a0b80`, `0x4a0ba0`),
`0x46b905` at `+04` and `0x46b910` at `+10` of 22 tables (every actor
class of [vtables.md](vtables.md#22-shared-slots-0x000x10-ts_obj-part)
and the pool objects); none is called directly.

- **`0x46b8ea`** (slot `+00`, the draw): `0x401fe4(O)` (the lazy object
  matrix and the view transform `0x48c444`, H3 in vtables.md), then the
  draw slot `+1Ch` of the shape `[O+34h]`. **H3**, not hooked
  ([function_verdicts.md](../reference-game/function_verdicts.md#stage-5-flushes-and-manager-slots)).
- **`object_position` `0x46b905`** (cdecl O): EAX = O + 26h; EDX and ECX
  are the caller's.
- **`shape_word` `0x46b910`** (cdecl O): if `[O+34h]` = 0: ECX ← 0, EAX ←
  0 (EDX the caller's). Else EDX ← S = `[O+34h]`, ECX ← `[S+4]`; if that is
  0: EAX ← 0. Else `[O+34h]` is read again (S'; the jump at `0x46b92e` to
  `xor edx,edx` cannot be taken: nothing is written between the reads),
  EAX ← S', EDX ← `[S'+4]` (read again), AX ← word `[EDX+8]`: EAX = high
  word of S' : the shape word. ECX = 0 on the two zero paths, the first
  `[S+4]` otherwise. **Branch sites** `0x46b91a`, `0x46b928` (both ways),
  `0x46b92e` (fall-through only, `PROVED`).

### The clip methods (ANSequence `0x4a684c`, ANCyclicSequence `0x4a6810`)

Each method is the only entry of its slot in the two tables (`+20h` next
frame, `+24h` previous frame, `+28h`/`+2Ch` the reached tests of the route
search); none is called directly. Arguments (cdecl): the clip C (frame
count n = signed word C+0Ah) and the full slots a, b, c; the callers push
words with undefined high halves and consume AX
([player_movement.md](player_movement.md)), but the methods compute on the
full slots. hi(x) is the high word of x, sx the sign extension of a low
word:

| Method | Hook | EAX | EDX | ECX |
|---|---|---|---|---|
| `0x478760` (seq `+20h`) | `sequence_next` | a + 1 if sx(a) < sx(n) − 1 (32-bit compare), else a | sx(n) − 1 | sx(a) |
| `0x47877c` (seq `+24h`) | `sequence_prev` | a − 1 if a's low word ≠ 0, else a | entry | entry |
| `0x47878c` (seq `+28h`) | `sequence_reach_up` | hi(a):1 if s16(b) > s16(a), or the words are equal and c's word is 0; else 0 | b | entry |
| `0x4787b8` (seq `+2Ch`) | `sequence_reach_down` | the same with s16(b) < s16(a) | b | entry |
| `0x4787e4` (cyc `+20h`) | `cyclic_next` | a + 1 if sx(a) < sx(n) − 1, else 0 | a | sx(a) |
| `0x478804` (cyc `+24h`) | `cyclic_prev` | a − 1 if a's low word ≠ 0, else (hi(a):n) − 1 (32-bit: n = 0 borrows from the high word) | a, or EAX in the second case | entry |
| `0x478820` (cyc `+28h`) | `cyclic_reach_up` | hi(entry EAX):1 | entry | entry |
| `0x47882c` (cyc `+2Ch`) | `cyclic_reach_down` | hi(entry EAX):1 | entry | entry |

`0x478820`/`0x47882c` complete the cyclic class's four slots (nine
identical bytes each). The branch sites `0x478773`, `0x478785`, `0x478798`, `0x4787a3`, `0x4787aa`,
`0x4787c4`, `0x4787cf`, `0x4787d6`, `0x4787f7`, `0x478810` are already
listed under `track_advance`/`track_target` (a site has one owner); the
direct cases of these hooks run the originals through both edges of each.

**ECX.** The framework returns the entry ECX, so `shape_word`,
`sequence_next` and `cyclic_next` do not reproduce the original's ECX. A
scan of the 37 `call [r+10h]`, 26 `call [r+20h]`, 17 `call [r+24h]`, 9 +
16 `call [r+28h]`/`[r+2Ch]` and 103 `call [r+4]` sites (both edges
followed, up to 60 instructions) finds ECX written before any read at
every one-argument `+10h` site (`pop ecx`, `mov ecx,..`) and at every clip
site (only `mov cx,..` before a use of `cx`); the sites that carry ECX on
into a call or a `ret` are multi-argument slots of other classes (sound
objects `[O+77h]`, HUD and port frames, renderer objects). The clip
methods are reached only from the animation code (`0x478ed4`, `0x4792df`,
`0x47950d`, `0x4797bf`, `0x4798d7`, `0x4794bd`/`0x4794d6`,
`0x479605`/`0x47961e`, `0x479782`), whose functions are all hooks that
compute both classes in core (`es2_clip_call`): with them replaced the
clip hooks are not entered; they run when an animation hook is original.
EDX:EAX are exact. No method writes memory or calls code: all ten accept
compare mode (no span).

### Validation (in vitro)

`differential_stage_flush_test` and `differential_manager_slots_test`
(`stage_flush_cases.h`, `manager_slots_cases.h`; one shard each). Quick
tier only: 100,000 direct cases per hook; the full tier (1,001,024) is not
run.

**stage_flush** (per hook): 100,000 direct cases: the original body, the
adapter (pair) and the core (pair and ECX) over logging stubs for
`removal_flush` and, under `flush_smoke`, `0x408f84` (arguments, slot
count, entry EAX/EDX/ECX in order; the stubs return random registers and in
one call of three rewrite one of the 24 pool/list dwords or `0x6b4f3c`),
the globals and the callee-counter deltas of the first 4,096 cases; 256
installed 31/a7 cases (fast and preserving dispatch, nonempty x87); 1,024
real-callee cases over the removal group's consistent pool (real `49630c`,
`496406` and `pool_free`, the destructor a logging stub) with
`removal_flush` nested original/replaced (under `flush_smoke` also
`flush_smoke_balls`) and the hook original/replaced, against a run with
only the nested hooks (pair, destructor calls, arena, globals, FS:0). Once,
under `flush_buildings`: 512 dispatch cases, the real `0x401d94(5)` over a
table of the eleven registered flushes (some repeated, some with stage 6)
with `removal_flush` stubbed, all twelve flush hooks replaced and
`stage_dispatch` original or replaced, against the all-original run (the
ordered `removal_flush` calls with their arguments and EAX/EDX, ECX
masked; the pair; the globals); every flush hook and the dispatcher were
entered. No branch site.

**manager_slots** (per hook): 100,000 direct cases (original against the
adapter's pair and the core's pair and ECX, the arena unchanged) over
objects with a null `[O+34h]` or `[S+4]`, clips with count words 0, 1, 2,
5, 7FFFh, 8000h, 8001h, FFFFh or random and frame slots at those edges
and around the count, under random high words; 256 installed 31/a7 cases;
the compare probe (512 cases: exact replacement 0 mismatches, skewed 256).
Branch sites `0x46b91a`, `0x46b928` both ways, `0x46b92e` `PROVED` (fall
9,229, taken 0); the ten clip sites of `track_advance`/`track_target` are
reached by these cases too.

Core tests `//core:render_root_test` (the twelve flushes: order of reads,
arguments and registers, the `0x408f84` call, `0x6b4f3c`),
`//core:fire_test` (the three `shape_word` paths, `object_position`),
`//core:animation_test` (each clip method at its edges). One caught
mutation per hook (22,
[MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

### Live expectation and result

At the compared tick N of every suite `hk_flush_<x>_calls` =
`hk_stage_dispatch_call_<va>` = N + 1 for the ten registered flushes other
than `flush_attached`,
`hk_flush_smoke_balls_calls` = `hk_flush_smoke_calls`,
`hk_flush_attached_calls` = the same plus `vehicle_part`'s
`call_409904`; each `hk_<flush>_call_478644` = its calls
(`hk_flush_smoke_call_408f84` too); `hk_removal_flush_calls` unchanged.
`object_position` and `shape_word` nonzero in every suite (probe 1000

`object_position` and `shape_word` nonzero in every suite (probe 1000
×6).

Validated live (every hook and the 49
shim imports replaced): all suites identical, every
skipped/bypassed/mismatch counter 0, the equalities above hold
(`hk_flush_attached_calls` slightly above the frame count through the
`vehicle_part` calls). The clip hooks are entered although the animation
hooks are replaced: `cyclic_next` in every suite (the cyclic clips are also
stepped from code that is still original), `sequence_next` only in
c4_10-probe.

## The main loop, pump, window procedure, pause, limiter and clock

The loop around the logic frame, read from the listing: per function the
call sites, the globals, the clock and `GetTickCount` calls, the pump calls
and the order of the effects the oracle compares (tick record
[tick_log.md](../reference-game/tick_log.md), virtual-time counters
[timing.md](timing.md#virtual-time), pump-keyed journal entries
[input_journal.md](../reference-game/input_journal.md)). The start-up, the
loop tail and the exit are in [launcher.md](launcher.md#dbsims-process-shell-start-up-and-exit). The own code of all
of them is [the next section but two](#the-main-loop-pump-window-procedure-limiter-and-clock-as-own-code).

**Under the proxy.** Hooks inside the loop: `stage_dispatch` `0x401d94`,
`logic_frame` `0x45f570` (with `control_record` `0x45a810`,
`command_key` `0x45feb8` and `hud_key` `0x432bc8` below it),
`camera_update` `0x4011a0`, `render_root` `0x45fca8`, `time_scale`
`0x467894` (called by the limiter). Original: the loop itself (inside the
game main `0x45f160`), the limiter `0x4678c0`, the clock `0x467828`,
timing init `0x467840`, the presentation frame `0x45fba4`, the pump
`0x464c78`, MainWndProc `0x466034` and its handler list, pause/resume
`0x45f0d4`/`0x45f108`.

### The loop (`0x45f44f..0x45f4c3`)

EBX = `0x4d24bc`. Before the first pass word `[0x4d2564]` ← 1 (the
command handler's gate). One pass:

| Step | Address | Action | Oracle-visible effect |
|---|---|---|---|
| 1 | `0x45f45a` | `stage_dispatch(4)` (no stage-4 callback is registered) | none |
| 2 | `0x45f45f..0x45f467` | word `[0x4d24d6]` (tape playback) = 0 → limiter `0x4678c0` | `vt_limiter_polls` +1; timing bytes `0x4d3b64..0x4d3b77` |
| 3 | `0x45f470..0x45f477` | byte `[0x4d2572]` (pause) ≠ 0 → pause path (below) | `loop_pause_flag` |
| 4 | `0x45f479..0x45f485` | word `[0x4d2532]` ≠ 0 or word `[0x4d24fe]` ≠ 0 → leave | no tick for this pass |
| 5 | `0x45f487..0x45f48f` | AX = `logic_frame()`; AX ≠ 0 → leave | the frame's simulation; one pump call inside (step 2 of the frame, `control_record`); a modal dialog may run inside (below) |
| 6 | `0x45f491..0x45f49a` | `camera_update([0x4d24ea])` | |
| 7 | `0x45f49b` | `render_root()` (its first action: `stage_dispatch(5)`, the removal flushes) | pools; clock calls of the cockpit |
| 8 | `0x45f4a0` | presentation frame `0x45fba4()` | palette cycling reads the clock once (dbsim_gap.md §4.5) |
| 9 | `0x45f4a7` | `stage_dispatch(6)` (record mode only) | |
| 10 | `0x45f4ad` | `inc dword [0x4d257b]` (frame counter) | **the tick record**: the proxy writes it at this instruction (tick_log.md), so a pass that leaves at step 4 or 5 writes none |

**Pause path** (`0x45f4b5..0x45f4c3`): `Sleep(100)`, then the pump
`0x464c78()`; nonzero (it removed `WM_QUIT`) → leave; else back to step 1
(stage 4 and the limiter run again, no frame counter increment). Under
virtual time every pause pass is one limiter poll, i.e. one step of the
clock ([timing.md](timing.md#other-time-sources)); replays never pause because the proxy keeps the
focus messages from the game (input_journal.md "Focus").

**Leaving** goes to the loop tail at `0x45f4c5` ([launcher.md](launcher.md#loop-tail-0x45f4c50x45f568)). The four ways: pump quit in the pause path;
word `0x4d2532` (set by `control_record` when its pump returns nonzero
or by its quit-on-key case in tape playback,
[input.md](input.md#control-record-45a810), and by
`WM_CLOSE` in demo playback); word `0x4d24fe` (the quit dialog's second
button, through `command_key` `0x4606cd`); the logic frame's 1 (the end
conditions of [the frame](#the-frame-itself), among them the Q alert's matching button:
`command_key` returns 2 and `player_input` 1, input.md).

**Per active pass:** exactly one limiter call (one
`GetTickCount` poll under virtual time), exactly one pump call (inside
`control_record`, before any game input is read), the clock reads of the
frame's game code (9 to 16 in C1_01, timing.md; 10 per
tick with the HUD drawn and 9 without, plus messages, blips and dialogs,
[hud.md](hud.md#the-coupling-set-measured)), no `Sleep`. The frame counter is the journal's and the
virtual clock's key ([own_process.md §7](../reference-game/own_process.md#7-virtual-time),
[§8](../reference-game/own_process.md#8-the-journal-provider)).

### The pump `0x464c78` (`0x464c78..0x464cc0`)

No arguments; a 28-byte `MSG` on its own frame. Loop:
`PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)`; zero → leave the loop; message
`WM_QUIT` (`0x12`) → return EAX = 1 **at once** (no `SetCursor`); else
`TranslateMessage`, `DispatchMessageA`, next peek. After the loop:
`SetCursor([0x4d3720])` when the handle is nonzero; return 0.

- **Peek count.** One call = (messages retrieved + 1) `PM_REMOVE` peeks,
  or (messages before `WM_QUIT` + 1) when it ends on `WM_QUIT`. Every
  peek counts for the journal's pump key, the empty one included
  (`input_journal.h` `ij_count_pump`: the first peek of a frame sets the
  count to 0, each further peek adds 1; an entry with `"p": N` is
  injected at the first peek whose count is ≥ N, i.e. the (N+1)-th peek
  of the frame). Injected messages are returned instead of a real peek,
  so each costs one peek.
- **Callers (15 sites):** `control_record` `0x45a824` (every logic frame
  and every modal pass), the pause path `0x45f4bc`, WinMain `0x465471`
  (until `WM_QUIT`), the dialog open `0x4546a5` (result ignored, below),
  the mission load `0x4616a4`, `0x461728`, `0x461835`, `0x461a6a`,
  `0x461a74`, `0x461e03`, `0x461e0d`, `0x461f44`, the start sequence
  `0x45d9c3`, the AVI player `0x4719f2`/`0x471a21` (no running caller).
  The modal loops reach it only through `control_record` (`0x454d0e`,
  `0x45601d`, `0x456e7d`, `0x457b1d`, `0x45880f`).
- `0x4659e8` is a second pump without the `WM_QUIT` test; its only caller
  `0x465a14` has no caller (dead).
- **Headless:** the pump becomes the journal's message source
  ([own_process.md §8](../reference-game/own_process.md#8-the-journal-provider)): per call, deliver the frame's due entries one per
  peek, count every peek including the final empty one, dispatch each
  through the own window procedure, never return 1 (no `WM_QUIT` in a
  replay) except after the game main has returned (WinMain's own
  `PostQuitMessage`, launcher.md).

### MainWndProc `0x466034` and the handler list

The contract outline is not repeated:
[flyer.md](flyer.md#mainwndproc-0x466034)
(handler list, then the jump table for messages 0..`0x16` and the compare
chain above `0x24`). Additions:

- **The list `0x4d3b30`** (10 dwords). Add `0x465fec(fn)`: the first
  slot that is 0 gets `fn`; a full list ignores it. Remove
  `0x466010(fn)`: the first slot equal to `fn` ← 0. So a removed
  handler leaves a hole that the next registration fills: **call order
  is slot order 0..9, not registration time.** The procedure calls every
  nonzero slot (stdcall, the four arguments) and returns 0 at the first
  nonzero result, before its own cases.
- **Registrations, in this order:** the input set-up `0x45a5dc` (called
  by the mission load at `0x461631`) constructs the keyboard device
  (`0x45a617` → `0x45c01c` → `0x4779a8`), which registers the keyboard
  handler `0x477bec` (`0x477a4c`, call `0x477a58`) when it is the
  current keyboard (`[0x6c5eb8]`), then the mouse device (`0x45a674` →
  `0x45c1d4` → `0x4805e0`), which registers the mouse handler `0x4809f8`
  (`0x480694`, call `0x4806a1`) when `0x480ae4()` (= `0x48d98f(2)`)
  returns it; later the mission load's sound init `0x462410` (call
  `0x46179f`) registers `sfxWndProc` `0x462398` (`0x46242d`). With the
  list empty before (BSS) that gives slots 0 keyboard, 1 mouse, 2 sfx.
  The video handler `0x4718d4` (`0x471931`) is registered only inside the
  AVI player. Removals: sfx in the sound release `0x462734` (loop tail),
  the others in their devices' teardown (`0x477a70`, `0x480712`,
  `0x471a40`).
- **`WM_CLOSE` and `WM_QUERYENDSESSION`** call the command handler with
  code `0x410` (Ctrl+Q), i.e. they open the quit dialog **inside the
  window procedure**, nested in whatever pump dispatched the message
  (a modal dialog inside a modal pass is possible). In demo playback
  (word `0x4d2530`) `WM_CLOSE` only sets word `0x4d2532`.
- **Oracle view:** the procedure changes compared state only through the
  handlers (key ring, mouse queue), pause/resume and the command handler.
  A replay sees only journal messages (keyboard and mouse); the focus
  pair is answered by the proxy's subclass (input_journal.md), so
  headless the own procedure runs only the handler list and the cases
  those messages reach (`WM_KEYDOWN`/`WM_SYSKEYDOWN` …, mouse messages:
  none of the cases above; `WM_SYSCHAR` is swallowed).

### Pause and resume `0x45f0d4` / `0x45f108` (`0x45f0d4..0x45f13b`)

No arguments, no frame.

| | Pause `0x45f0d4` | Resume `0x45f108` |
|---|---|---|
| flag | byte `[0x4d2572]` ← 1 | byte `[0x4d2572]` ← 0 |
| sound | `0x46317c()` (pause the playing samples, audio.md) | `0x463238()` (resume) |
| cockpit, only when `H = [0x49b088]` ≠ 0 | `0x435b58(0x433158())`, then `0x435b58([H+0x20b])` | `0x435b80(0x433158())`, then `0x435b80([H+0x20b])` |

`0x433158()` returns `[H+0x207]` (0 without a cockpit). These are the two
cockpit message ports (hud.md, "EventPort"): **`0x435b58(port)`** reads the
clock only when the port is not yet paused (`[port+0x4cc]` = 0 → ← clock)
and increments the nesting word `+0x4d0`; **`0x435b80(port)`** decrements
it and, when it reaches 0, reads the clock once, shifts the port's queued
times by (clock − pause start) (`0x435298(port+4, d)`), moves the current
message's end `[port+0x49a]+0x1c/+0x20` by d when it is still in the
future (else sets byte `+0x4cb`), clears the pause start and adds d to
dword `[0x4d1294]`. So a pause/resume pair costs two clock reads per port
at nesting 0 and moves the cockpit's message timing by the paused clock
time. Callers: MainWndProc only (`WM_KILLFOCUS`/`WM_SETFOCUS` and power
suspend/resume, `0x466369`, `0x466373`, `0x4664e0`, `0x4664ea`); the
dialogs use the same port pair themselves (below). Headless: never called
(no focus messages in a replay); the init store `0x45cbcb` sets the flag
to 0 ([timing.md](timing.md#other-time-sources)).

### Limiter `0x4678c0`, clock `0x467828`, timing init `0x467840`

The contract stands in [timing.md](timing.md) ("Timing code", "Virtual
time"); restated in execution order:

- **Timing init `0x467840`** (one caller, the mission load `0x4617c3`):
  words `[0x4d3b64]`, `[0x4d3b66]` ← 256; `[0x4d3b70]` ← 1000;
  `[0x4d3b72]`, `[0x4d3b68]`, `[0x4d3b6a]` ← 1000 / 8 = 125 (signed
  rounding toward zero); dword `[0x4d3b6c]` ← 125; then **one
  `GetTickCount`** (`0x467883`) → both the clock base `[0x4d3b78]` and the
  last limiter time `[0x4d3b74]`.
- **Limiter `0x4678c0`** (no arguments; ESI = `0x4d3b6c`): threshold T =
  `[0x4d3b74]` + 40; repeat `GetTickCount` (`0x4678d0`, return address
  `0x4678d5`) → `[0x4d3b6c]` while T > value (signed compare); then
  `[0x4d3b6c]` ← value − `[0x4d3b74]` (elapsed e); word `[0x4d3b64]` ←
  `time_scale([0x4d3b68], e)` and word `[0x4d3b66]` ←
  `time_scale([0x4d3b6a], e)` (hook `0x467894`: clamp to 64..450 of
  e × 256 / reference); `[0x4d3b74]` += e. Under virtual time one poll
  answers previous + step, so e = step (47) and the wait has one poll.
- **Clock `0x467828`** (no arguments): `(GetTickCount() − [0x4d3b78]) >> 4`
  (logical shift; unit 16 ms). The listing shows `0x467825..0x46782d`
  misaligned (`00 00 00 e8 …`): the bytes from `0x467828` are
  `e8 bf f0 02 00` (call `0x4968ec`), `2b 05 78 3b 4d 00`, `c1 e8 04`,
  `c3`. 64 call sites. Before the timing init the base is 0 (BSS).
- `0x467838` (`[0x4d3b78]` −= 16, i.e. clock + 1) has no reference in the
  image (no call, no pointer): dead. `0x467824` is a bare `ret` reached
  only as init record 50 (pointer at `0x4a7b06`).
- **Compared:** the tick record's 24 timing bytes from `0x4d3b64`
  (`clock_raw_hex`), `completed_clock_ms` and `vt_limiter_polls`,
  `vt_stall_advances` (timing.md). The host's three call sites name the
  caller explicitly ([own_process.md §7](../reference-game/own_process.md#7-virtual-time)).

### Open

- Whether both device conditions (`[0x6c5eb8]`, `0x48d98f(2)`) hold in
  every run, i.e. whether the slots are always keyboard, mouse, sfx.
- Which messages the own window procedure must handle beyond the
  journal's keyboard and mouse messages in a window: the
  [flyer.md](flyer.md#mainwndproc-0x466034)
  outline lists them; none is reached in a replay
  ([own_process.md](../reference-game/own_process.md#the-windows-open-points)).

## The modal dialogs

The five modal screens DBSIM opens during a mission, their run loops, the
pump calls and clock reads per pass, and what ends them; read from the
listing. Their callers `logic_frame`, `command_key` and `hud_key` are hooks
that call them through the bridge. Own code: [The modal dialogs in the own
process](#the-modal-dialogs-in-the-own-process) and [Dialog constructors and the F12/Alt+P
session](#dialog-constructors-and-the-f12altp-session).

### The screens

All are objects of the alert base (constructor `0x4541b8`, base vtable
`0x49e034`) with the slots +0 draw, +4 `0x454b74`, **+8 run (the modal
loop)**, **+0xc button callback**, **+0x10 key handler** (`0x454e2c` in
every class), +0x14.

| Screen | Opened by | Constructor | Vtable | Run `+8` | Button callback `+0xc` |
|---|---|---|---|---|---|
| Mission alert (`gnl_alrt`) | `logic_frame` step 19 (`0x45fa84` nonzero: `0x455950(obj, 2, 1, 0)`, then return 1 whatever the button, [the frame](#the-frame-itself)); `command_key` Q (`0x4605ea`: `0x455950(obj, status, 1, 0)`, input.md); the start sequence `0x45d85c` | `0x455950(obj, status, flag, small)` on `0x47749c(0x57e)` | `0x49e120` | `0x456000` | `0x45617c`: button 0 → done ← 2, button 1 → done ← 3 |
| Pause (P) and quit (Ctrl+Q, `WM_CLOSE`) | `command_key` `0x460759` / `0x4606cd` | `0x4561dc(obj, 0 or 1, 1, 0)` on `0x47749c(0x57e)` | `0x49e1e4` | `0x456000` | `0x45617c` |
| Objectives (F11) | `hud_key` `0x432bc8` | `0x457538` on `0x47749c(0x3b8)` | `0x49e4d0` | `0x457b00` | `0x457c4c`: button 0 → done ← 1, others nothing |
| Options (F12, Alt+P) | `command_key` `0x4607d7` → `0x45cff0` | `0x4566e0` on a `0x3c7`-byte object | `0x49e3e4` | `0x456d68` | `0x4573c4`: action ← button index (below) |
| Joystick page | options action 9 | `0x457d38` (frame object) | [input.md](input.md#the-joystick-page-and-the-sdl3-joystick) | `0x45866c` | |

The run's result is `done & 1` (`0x456076..0x45607c`, the same at
`0x457b63`, `0x4570dc`): the mission alert and the pause/quit screen
return 0 for button 0 (done 2) and 1 for button 1 (done 3); the caller
compares it (Q: with the word `0x49f5c0[status]`; Ctrl+Q: nonzero → word
`0x4d24fe` ← 1; input.md). After the run the caller destroys the object
with `0x4543b0(obj, 3)`.

**Object fields used by the loops** (offsets in the object): +4..+0x13
the screen rectangle, +0x2c background save buffer (allocated when the
constructor's flag is set), +0x34 the STR holder, +0x285 the panel
(`0x398` bytes, `0x4526ec`; its release list `+0x356`, count word
`+0x396`), +0x289 the previous panel (`[0x49dbd8]`), +0x291 the gadget
(button) pointers, count `+0x2cd`, **+0x2d1 the done byte**, +0x2f7 the
focused button (−1 = none), +0x2fb the cancel button (Esc; −1 = none),
+0x300 the saved 48-byte control record, +0x331 the deferred done byte.

### Constructors `0x4541b8`, `0x455950` (no pump, no clock)

- **`0x4541b8(obj, save_background, rect)`**: base vtable, the rectangle,
  the STR holder (`0x453824`), canvas `0x480c34(obj+0x48)`, `0x48358c`,
  gadget count 0, done 0, the background buffer (`0x488045` size,
  `0x4774f0`) when the flag is set, the panel `0x47749c(0x398)` +
  `0x4526ec(panel, obj+4)`, clip from `[[0x6c5ed0]+0x3c]`, cancel −1,
  `+0x330` ← 1, `+0x331` ← 0, and the present interval **dword
  `[0x4d1e60]` ← 1**. Construction counter `[[0x6cbee4]]` bookkeeping.
- **`0x455950(obj, status, flag, small)`**: rectangle from `0x454f14`
  (small ≠ 0) or `0x454f50`, scaled by the shifts `[0x4d253a]`/
  `[0x4d253c]`; `0x4541b8`; vtable `0x49e120`; once per process (byte
  `[0x49e04c]`) a layout table `0x4d1e9c..0x4d1ebe`; the texts from
  `gnl_alrt.str` (resolver `0x45ef1c`, STR reader `0x453878`, three
  `0x4539a0` groups: a VOL or loose **file read at every alert**,
  [mission.md](../formats/mission.md)); a `hba`/`dba` bitmap name by
  `[0x4d256c]`; buttons (`0x47749c(0x50)` + `0x454f80`, labels
  `0x47749c(0x21)` + `0x4387ac`/`0x438884`, each added to the panel with
  `0x452c6c`); cancel ← button 0 (`0x455f7e`); status 2 → `+0x330` ← 0.
  Direct-call reach: 108 functions, no clock, no pump (its indirect calls
  are the stream and bitmap readers).
- The other constructors (`0x4561dc`, `0x457538`, `0x4566e0`) have the
  same shape (direct reach without clock or pump: 109, 106, 105
  functions); their texts and buttons are in
  [input.md](input.md#command-handler-0x45feb8-key--handler) and
  [oracle_suites.md](../reference-game/oracle_suites.md#dialog-weapon-and-flight-suites).
- **`0x4543b0(obj, flags)`** (destructor): deletes the gadgets, the
  background buffer, `+0x30`'s object (virtual `+0x18(3)`), `[obj+0x44]`,
  `0x4835c8(obj+0x2e2, 2)`, the STR holder; deletes the object when
  flags bit 0. No clock, no pump.

### Open `0x45464c(obj)` (`0x45464c..0x4548c4`)

1. W = `0x433158()` (port `[H+0x207]`); `0x435b38(W, 0)` (byte
   `W+0x4cb` ← 1); W's virtual `+0xc(W)`; `0x435b58(W)`;
   `0x435b58([[0x49b088]+0x20b])`: the two port pauses of the pause
   function above (**clock read per port when not already paused**). The
   cockpit `[0x49b088]` is dereferenced without a test (a dialog needs a
   cockpit).
2. Nesting word `[0x49dd8c]` = 0 → bytes `[0x49eaa4]` ← 0, `[0x49dbec]`
   ← 0.
3. **Pump `0x464c78()`; the result is ignored** (a `WM_QUIT` removed here
   is lost: quirk).
4. `[[0x4d1e04]]` ← 0; the control record R `0x4d22c6` (48 bytes) copied
   to `obj+0x300`; button latches `0x45b734(i, 1)` for i = 1..4
   (input.md "Button latch").
5. `0x454c98(obj, −1)`: with a mouse device (`0x45bd8c` → `0x45bf68`:
   index `[0x4d24a0]` into the device table `0x4d2468`, entry − `0x3a`)
   it calls **`control_record` (a second pump)** and focuses the button
   under the cursor (or none); without a mouse only focus ← −1. The mouse
   object is constructed unconditionally by the input set-up
   (`0x45a65b..0x45a695`: `0x47749c(0x3f)`, `0x45c1d4`, which announces
   it as device kind 2 through `0x45bda4(obj+0x3a, 2, …)`), so the
   second pump is expected in every run (hypothesis: the writer of
   `[0x4d24a0]` was not read).
6. Panel `0x452bbc`, canvas switch (`+0x281` ← `[0x6c5f70]`,
   `0x480d44(obj+0x48)`, clip through `[0x4a57ec]`/`[0x4a57f0]`), byte
   `+0x2f6` ← `[0x49dbdf]`, `[0x49dbdf]` ← 1, the optional callback
   `[0x49dd94]`, the frame object `[0x49dd90]` (created once with
   `0x430fb0`), `[0x4a5828]`, background save `0x488012` when `+0x2c`,
   draw (slot 0), `0x454b8c(obj, [0x49dd90])`, `[0x4a5824]`, nesting
   `[0x49dd8c]` += 1, and **`0x454ac0()`: one clock read** →
   `[0x4d1e64]` (the next present time).

### Close `0x4548c8(obj)` (`0x4548c8..0x454abe`)

`[0x49dbdf]` ← 0, `[0x4a5828]`, background restore `0x488143` when
`+0x2c`, `0x464a14` (present) when word `[0x4d2566]` ≠ 0, `0x454c2c`,
`[0x49dbdf]` ← `+0x2f6` (and `[0x4a5824]` when nonzero), callback
`[0x49dd98]`, canvas and clip back (`+0x281`, `+0x28d`), panel
`0x452c04`, `0x452bbc(+0x289)` when set, **R restored from `obj+0x300`**
(the keys of the dialog do not reach the frame), the cursor put back with
the mouse device (`0x4808dc` and `0x48993a` or `0x486e7c`), nesting −1
(at 0: `[0x49eaa4]`, `[0x49dbec]` ← 1), then `0x435b80` on both ports
(**clock read per port when its nesting returns to 0**; the ports' times
and `[0x4d1294]` move by the dialog's clock time). No pump.

### The run loops

**Mission alert, pause and quit: `0x456000(obj)`** (`0x456000..0x456083`):

```text
open(obj); 0x454c98(obj, 0)                    focus button 0
while done == 0:
    R = control_record()                       one pump call
    if word [0x4d2532]: done = 1; continue     (demo stop / pump quit)
    key handler +0x10(obj, R)                  0x454e2c
    for each button g: g.vtable[+0xc](g)       0x455080: redraw countdown, no clock
    0x454acc(obj)                              loop step
close(obj); return done & 1
```

**Objectives (F11): `0x457b00`** is the same loop without the `0x4d2532`
test (`0x457b00..0x457b70`).

**Loop step `0x454acc(obj)`** (`0x454acc..0x454b73`):
1. word `[0x4d24fe]` ≠ 0 → done ← 1.
2. Sound context `[0x49f8ec]` ≠ 0 → `0x464260(ctx)` (the driver service,
   [timing.md](timing.md#other-time-sources); [audio.md](audio.md)).
3. `0x4530fc(panel)`: for each entry of the panel's release list
   (button, release time): **one clock read**; clock < release time →
   button state `+0x1b` ← 1 (redraw through its slot `+4` if it changed),
   next; else state ← 0, redraw, entry removed.
4. **Deferred close:** done ≠ 0 and the release list not empty
   (`0x4531a4`) → `+0x331` ← done, done ← 0; else if `+0x331` ≠ 0 → done
   ← `+0x331`, `+0x331` ← 0. The close is therefore deferred by **exactly
   one pass**, whatever the release times are (the restore does not test
   the list again).
5. Word `[0x4d2566]` ≠ 0 (always, [launcher.md](launcher.md#dbsims-process-shell-start-up-and-exit)): **one clock read** c;
   c > `[0x4d1e64]` (unsigned) → present `0x464a14()` and `[0x4d1e64]` ←
   c + `[0x4d1e60]` (= 1).

**Key handler `0x454e2c(obj, R)`** (returns AL = 1 when it acted, else
EAX = 0):

| Input | Action |
|---|---|
| byte R+0x0d ≠ 0 (the select button) | focused ≠ −1 → press it (`0x454de8(obj, i, 1)`), else focus 0; `0x45b734(1, 1)` |
| byte R+0x17 ≠ 0 | focus next (`0x454d98`, wraps to 0); `0x45b734(2, 1)` |
| key word R+0 = 1 (Esc) | cancel `+0x2fb` ≠ −1 → press it |
| `0x0f` (Tab), `0x52` (scan code `0x52`) | focus next |
| `0x1c` (Enter) | focused ≠ −1 → press it, else focus 0 |
| `0x80f` (Shift+Tab) | focus previous (`0x454dbc`); unreachable from the keyboard, the record's key word is masked with `0x47ff` (input.md) |

The record comes from `control_record`, which also hands every queued
key code to `command_key` (input.md "Command handler": Enter, `-`, `=`,
Num+, Num− … are queued by the key callback), so a key that answers a
dialog reaches the command handler inside the modal pass as well; the
dialog's own reaction comes from the record's key word.

**Press `0x454de8(obj, i, 1)`**: a button with state word `+0x1b` = 2
(disabled) does nothing; else its slot `+8` `0x45509c` → the owner's
button callback `+0xc(obj, g, flag)` (sets done; **`0x454ac0`: one clock
read**), the button's slot `+4` (redraw countdown 2), its label's slot
`+8`; then `button_release` `0x4530bc(panel, g)` (hook): appends (g,
**clock + 10**) to the release list (**one clock read**).

**Focus `0x454c98(obj, i)`**: i ≠ −1 → the cursor is moved to the
button's centre with the mouse device (`0x4808dc`, `0x486e7c`); i = −1
with a mouse device → `control_record` (a pump) and a hit test of the
cursor against the buttons. Stores `+0x2f7`.

**Options: `0x456d68(obj)`** (`0x456d68..0x4570f8`): with no sound
context the four sound buttons get state 2; action `+0x356` ← −1; open;
focus 0; canvas; then while done = 0: `control_record` (one pump), the
key handler, the action `+0x356` (0..8 step a prefs byte, 9 the second
page `0x457d38`/`0x45866c`, 10 `0x4574e8` (prefs write) and done ← 3,
[oracle_suites.md](../reference-game/oracle_suites.md#c1_01-options-dialog)),
action ← −1, `0x457480` (the idle callback `[obj+0x3ba]` between canvas
switches: `0x45cfc8` = camera step, `camera_update`, `render_root`), and
`0x45719c`: word `[0x4d24fe]` → done ← 1; button redraw countdowns; the
first pending highlight byte of `+0x3be..+0x3c6` → `0x457210` (ends with
`0x454ac0`: one clock read); `0x4530fc` (one clock read per release
entry); **present `0x464a14` every pass** (no clock gate). There is no
deferred close and no `0x4d2532` test; close; return done & 1. The
callback `0x4573c4` stores the pressed button's index in `+0x356` (and
moves the highlight for indices < 9), then `0x454ac0` (one clock read).

### Pump calls and clock reads per pass (the oracle contract)

| | Pump calls | Clock reads (in order) |
|---|---|---|
| open `0x45464c` | 1 (`0x4546a5`), + 1 with a mouse device (`control_record` in `0x454c98(obj, −1)`) | port A (if not paused), port B (if not paused), `0x454ac0` |
| pass of `0x456000`/`0x457b00` | 1 (`control_record`) | on a press: the callback's `0x454ac0`, then `button_release`; then `0x4530fc` one per release entry; then the present gate (1) |
| pass of `0x456d68` | 1 (`control_record`) | on a press: callback `0x454ac0`, `button_release`; the idle callback's `render_root` (its own reads); `0x457210` (1, when a highlight is pending); `0x4530fc` one per release entry |
| close `0x4548c8` | 0 | port A (if its nesting returns to 0), port B (same) |

Each pump call costs (messages + 1) peeks. With no message traffic (a
headless replay) a pass of `0x456000` is exactly **one peek**, the open
one or two, so a journal answer keyed `"p": N` in the alert's frame
arrives in the pass whose peek is the (N+1)-th of the frame, counting
the frame's earlier peeks (the logic frame's `control_record` at row 2,
the open). Worked example, `c1_01-success` (mission alert every 126
frames, Enter keyed p 300 and its key-up p 400,
`tools/reference-game/input-scripts/c1_01-success.jsonl`): the Enter
down is delivered at peek 301, the pass presses button 0 (done 2,
deferred because `button_release` queued a release), the next pass
restores done and the loop ends; the key-up at p 400 is never reached
in that frame and arrives at the first peek of the next frame (entries
of earlier frames are delivered, input_journal.h). The golden's alert
frames show `vt_intra_advances` +46 and `vt_stall_advances` 0
(`es2-data\oracle\golden\c1_01-success\tick-log.jsonl`, frames 5316,
5442, …, 6072).

**Why only the order, not the exact number, of clock reads is compared.** Under virtual time with `virtual_time_intra_frame` = 1 every
clock read after the first in a frame advances the clock by 1 ms, at
most step − 1 = 46 times per frame (timing.md). A modal frame answered
at p ≥ 290 makes hundreds of reads, so the cap is always reached and the
frame's step stays 47 ms; the stall guard (100,000 reads) is never hit.
What the reads still decide is **which clock values the dialog stores**:
the ports' pause starts (first reads of the open), the release time
(clock + 10, 160 ms), the present time `[0x4d1e64]`, and at the close the
shift added to the ports and `[0x4d1294]` (the cockpit's message timing
afterwards). These are the values of the k-th read of the frame (k
counted from the frame's first read), so the own code reproduces the order and
the count of reads up to the cap (the first 47 reads of the frame), not
beyond. A suite answered at a small p (fewer than about 46 reads before
the close) would make the exact count compared.

**Headless.** No window, no present, no background save; the
open's and the loop's pump calls go to the journal source of [the own
loop](#the-main-loop-pump-window-procedure-limiter-and-clock-as-own-code) with the same peek counts; the mouse device decides the
open's second pump (constructed in every run, see the open); the key
handler, the presses,
the release list and the deferred close as above; the STR read of the
mission alert through the file services. The nested `render_root` of the
options loop is the hook (its stack budget: [Shim stack
budget](#shim-stack-budget)).

### Open

- The start sequence's alert (`0x45d85c`) was not read.
- Whether `0x45bf68` finds the mouse in every replay (decides one peek
  of the open; the writer of `[0x4d24a0]` was not read); the map counts
  of `0x454c98` (0/17/0/2/2/2 against 0/8/0/1/1/1 alerts) fit both
  readings.
- The role of `+0x330` (cleared for status 2; reader not traced).

## The main loop, pump, window procedure, limiter and clock as own code

The loop, pump, window procedure, pause, limiter and clock of [the section
above](#the-main-loop-pump-window-procedure-pause-limiter-and-clock), the
loop tail and the process exit as own code in `sim/` (`sim/src/loop.c`,
`es2/sim/loop.h`), plus the DIB section `0x464d44`
(`sim/src/frame_buffer.c`); the host side is [own_process.md "The main
loop, the journal and the exit"](../reference-game/own_process.md#the-main-loop-the-journal-and-the-exit).
Source: the `llvm-objdump -d -M intel` listing of `VER95/DBSIM.EXE`
tables and literals read from the image.
**In vitro** = the original's
machine code and the own code gave the same bytes
(`//tools/reference-game/invitro:differential_loop_test`, `loop_cases.h`).
The own process's replays are IDENTICAL ([replay table](../reference-game/own_process.md#replay-table)); not
checked live in the original process.

### What is own code

| Original | Own code | Checked |
|---|---|---|
| game main `0x45f430..0x45f568`: stage 3, `LoadCursorA(IDC_ARROW)` → `0x464c68`, word `0x4d2564` ← 1, the loop (steps 1..10 of [the loop](#the-loop-0x45f44f0x45f4c3)), the pause path, the loop tail (sound calls, frame clear, prefs byte 6, the tape deletes, `0x4650cc`/`0x4650d0`, the mission end, `0x453848(holder, 2)`) | `es2_sim_main_loop` | unit test (`//sim:loop_test`) |
| limiter `0x4678c0`, clock `0x467828`, timing init `0x467840` under virtual time | `es2_sim_limiter`, `es2_sim_clock`, `es2_sim_timing_init`; rows `limiter`, `clock`, `timing_init` | in vitro (2,000 sequences) |
| pump `0x464c78` (journal, TranslateMessage, DispatchMessageA, SetCursor) | `es2_sim_pump`; row `pump` | in vitro (4,000 journals) |
| MainWndProc `0x466034`: the handler list, `WM_SYSCHAR`, DefWindowProcA; focus, close, paint, destroy, end session | `es2_sim_window_proc` | handler list and default path in vitro (4,000 cases); the other cases unit test |
| pause `0x45f0d4`, resume `0x45f108` | `es2_sim_pause`, `es2_sim_resume` | unit test |
| handler add `0x465fec`, remove `0x466010`; abort-notify registration `0x479cfc` | rows `handler_add`, `handler_remove`, `abort_notify_add` | in vitro (2,000 sequences, EAX/EDX) |
| CW3220's `_atexit` and `exit` (atexit functions last first, the exit records, the stream exit hooks) | `es2_sim_atexit` (row `rt_atexit`), `es2_sim_exit` | unit test; the order is rtl_imports.md's ("The runtime lifecycle", in vitro there) |
| DIB section `0x464d44` with `0x480ef4`/`0x47a0b8`, `0x480f58` (`0x480590`, `0x4812f0`), `0x481028` (`0x489416` and the BMP_8 driver slots 7..11, `0x48a1d4`, `0x489570`), `0x481224` (`0x4805b4`, `0x4893aa`, `0x4894cc`, `0x489558`), `0x4813f8` case 5; surface release `0x480f38` with `0x4811e8` and slot 12 `0x48a32c`; the empty `0x480f30` | `es2_sim_dib_section`, `es2_sim_surface_release`; row `surface_notify` | in vitro (64 cases: video modes 0 and 1, an earlier DIB, random palette bytes) |

Every call into other code goes through the dispatch: the hooks
`stage_dispatch`, `logic_frame`, `camera_update`, `render_root`,
`time_scale`, `mission_root` (the load) and `mission_end`; the services of
the heap, the string and file imports and the rows above; the presentation
frame `0x45fba4`, the sound calls `0x464874`/`0x462734`/`0x46317c`/`0x463238`,
the frame clear `0x46548c`, the prefs pair `0x459958`/`0x459b80`, the
cockpit ports, the present `0x464a14` of `WM_PAINT` and the handlers the
devices register. A callee without a row or hook is refused by name.

### Facts read on the way

- **The DIB section builds the graphics library's current surface.**
  `0x480ef4` registers the GL table (`0x47a0b8(0x4a2eb8, [0x4a2f24] = 9)`:
  pair `[0x4a1690]` of `0x6c5834`); after the GDI calls, a 14-byte
  descriptor `[0x4d3020]` (height, width, 8, flags `0x10`, size, bits) and a
  `0xa0`-byte surface `[0x4d3024]`: `0x480f58(s, "BMP_8")` clears `[s]`,
  takes the device slot 7 `0x6c5ed0` when it is free, registers the
  abort-notify function `0x480f30` (an empty body) and the atexit function
  `0x480f38`, and stores the format code `0x4812f0(s, name)` (names
  `0x4a2e88`: `VGA_8` 1, `VGA_4` 0, `BMP_8` 3, else 4) at `+0x30`; the
  construction counter moves +1 there, +1 − 2 after the `strncpy` of the
  name to `+0x90` (net 0). `0x481028(s, desc)` calls the BMP_8 driver table
  `0x4a586c` (from the pairs `0x4a5840`; slots 8, 9, 10 answer 0, slot 7 is
  empty) and its slot 11 `0x48a1d4`: the row table (game calloc
  `0x47747c(height × 4 + 8)`: the bits pointer, then the row offsets i × DIB
  width) in `[0x4a03d0]` and `s+4`, and the surface size **forced to the game
  size** by `[0x4a03c8]`/`[0x4a03cc]` (the DIB section stores 320 × 240 there
  before the call; the driver resets both to −1), the DIB size at
  `s+0x28`/`s+0x2c`, the driver globals `0x4d2ff0..0x4d301c`; then two
  `0x22c`-byte clip records at `s+0x40`/`s+0x44` (`0x489570`: the corner at
  `+0x214`/`+0x218` = size − 1, the pointers `0x4a3604`/`0x4a360c`; the
  second a copy of the first), `[s] |= 1`. `0x481224(s)` makes it current
  (slot 7 cleared through `0x48154c` and set again), copies the driver table
  (`[0x4a583c]` = `0x24` dwords) to `0x4a57ac`, the surface's dwords
  `+4..+0x2c` to `0x4a3652`, calls the driver's slots 18, 16, 17 (empty),
  copies the first clip record to `0x4a33d8` and stores the second in
  `[0x4a3614]`. The surface counter `[0x4a2e2c]` ends at 1.
- **`0x4894a5` (driver slot 12's caller) pushes one argument and removes
  two** (`push [ebp+0xc]` … `add esp, 8` at `0x4894c1`), so its five pops
  are shifted: it returns with EDI = the entry ESI, ESI = the entry EDX,
  EDX = ECX, ECX = EBX, EBX = its own saved EBP. Through `0x4811e8` (EDX = 0
  after `xor edx, edx` at `0x4811fe`) the atexit function **`0x480f38`
  returns with ESI = 0 and EDI = the caller's ESI** (in vitro: every case).
  CW3220's exit loop calls it; the goldens' exit codes show the original
  survives it. The own exit path has no registers to corrupt.
- **The surface release frees the row table through `_free` directly**
  (slot 12 `0x48a32c`), not through `delete`, so the heap byte count
  `[0x6bb30c]` keeps the block's header size; the second clip record
  `s+0x44` is never freed, and `[0x4a03d0]` keeps the freed pointer.
- **MainWndProc's cases** (jump table `0x466089` for 0..`0x16`, compare
  chain above `0x24`): `WM_DESTROY` (`0x4650cc`, `0x4650d0`,
  `PostQuitMessage`), `WM_SETFOCUS`/`WM_KILLFOCUS` (resume/pause),
  `WM_PAINT` (DefWindowProc with DirectDraw, else the present `0x464a14`;
  **no BeginPaint**), `WM_CLOSE`, `WM_QUERYENDSESSION` (both
  `command_key(0x410)` into word `0x4d370c`; the second returns 1 for an
  answer of 2), `WM_ENDSESSION` (`DestroyWindow` for wParam 1),
  `WM_GETMINMAXINFO`, `WM_DISPLAYCHANGE`, `WM_SYSCHAR` (returns 0:
  swallowed), `WM_POWERBROADCAST` (suspend/resume = pause/resume), `0x219`,
  `WM_QUERYNEWPALETTE`, `WM_PALETTECHANGED`, `0x812`; every other message
  goes to DefWindowProcA. The handlers run first, each with the four
  arguments (stdcall), the first nonzero answer returns 0.
- **The limiter's two `time_scale` calls** push full dwords whose high words
  the hook does not read: the reference in the low word of EAX (high word
  from the last poll value), the elapsed time in the low word of ECX (high
  word from the loop's `pop ecx`, 0), then the first call's ECX and EDX for
  the second (`0x4678e5..0x46790a`). The own limiter passes the same.
- **Registers the rows leave** (for guest callers): the clock EAX only (ECX,
  EDX as at the call, GetTickCount's under the host); the timing init EAX =
  the read, EDX = 125 (`movsx edx, ax`); the pump EAX 0/1 (ECX/EDX are the
  last USER32 call's in the original: host values; the 15 call sites test
  only EAX, hypothesis); `0x465fec` EAX = the slot (`0x4d3b58` when full),
  EDX = its index, ECX = fn; `0x466010` the same with ECX = 0 when found;
  `0x479cfc` EAX = the node, EDX = the old head, ECX = fn; `_atexit` EAX
  0/1 (its three callers `0x480f7b`, `0x480fc7`, `0x4779da` pop or
  overwrite ECX and EDX before reading them).

### The pump's message source

Per PM_REMOVE peek: the journal's due message first (the frame's peek count
as `input_journal.h` counts it, the empty peek included), then the messages
the own code posted (TranslateMessage's characters, first in first out),
`WM_QUIT` last (after `PostQuitMessage`): the order a proxy replay sees,
where injected messages come before the real queue. **A key-down with a
character costs one more peek**: TranslateMessage posts `WM_CHAR`
(`WM_SYSCHAR` for `WM_SYSKEYDOWN`) with the key-down's lParam, retrieved
after the journal's messages due at that peek. In vitro the pump's machine
code over a model of the proxy's replay and the own pump agree on every
journal; the characters are the host's `ToAscii` ([the characters of
`TranslateMessage`](#the-characters-of-translatemessage)).

### Headless deviations

- No window, device context or palette: `GetDC`, `CreateCompatibleDC`,
  `ReleaseDC`, `SelectObject`, `DeleteObject` are counted and answer
  stand-in handles; `GetSystemPaletteEntries` answers 0 and writes nothing
  (what a display without a palette answers; whether the recording
  machine's DC had one is not known, so the 1,024 colour bytes at
  `0x4d3074` are the image's, byte-swapped); `CreateDIBSection` answers a
  host region of 1280 × 960 bytes at `[0x4d3028]` (the bits are OS memory
  in the original too).
- DefWindowProcA has no effect. The keyboard menu activation it would start
  (`WM_SYSKEYUP` of F10 or Alt: the window has menu 1) is counted
  (`menu_keys`) and has no effect either: the golden of `c5_05-moon-missiles`, recorded with the
  window hidden, goes on after three F10 presses (ticks 8113..8134) with
  the frame counter running, so no modal menu loop held the game's loop
  there. What a visible window does is a live check
  ([own_process.md](../reference-game/own_process.md#the-windows-open-points)).
- `Sleep(100)` of the pause path returns at once; `SetCursor` and
  `LoadCursorA` are counted; `OpenFile(…, OF_DELETE)` of the loop tail
  deletes in the working directory (the original's install directory).

- `WM_CLOSE`/`WM_QUERYENDSESSION` outside demo playback call the key
  dispatcher `0x45feb8(0x410)` (the hook `command_key`) through the
  dispatch, which opens the quit dialog inside the window procedure; the
  answer is kept at `[0x4d370c]` ([the modal dialogs in the own
  process](#the-modal-dialogs-in-the-own-process)). Window, display and power messages are refused (no
  replay sends them).

### Acceptance

- **Unit** `//sim:loop_test` (synthetic image): the DIB section's objects
  and their release, the timing init/clock/limiter, the pump with a journal
  (pump keys, `WM_CHAR`, cursor, `WM_QUIT`), the window procedure's cases,
  the main loop with its call order, `frame_limit`, tape playback, the pause
  path, the tail's refusals, the atexit table (32, last first) and the
  exit. `//sim/oracle:oracle_test` covers the journal library.
- **In vitro** `differential_loop_test` (about 70 s): dib 64, timing 2,000,
  pump 4,000, window 4,000, rows 2,000 cases identical (DATA
  `0x497000..0x6cc000`, the emulated heap, the call logs, the registers
  named above). Two mutations caught.

- **Replay** (`replay_suite.py --all`): all 40 suites IDENTICAL
  ([replay table](../reference-game/own_process.md#replay-table)).

### Interfaces to the other own code

- The input set-up `0x45a5dc` constructs the keyboard and mouse devices,
  which register the window handlers through the row `handler_add` and
  their atexit and abort-notify functions through the rows above
  ([mission_load.md](mission_load.md) section 30). `0x467840`, `0x464c78`
  and `0x479cfc` are rows.
- The modal passes' pump is the row `pump`; a modal pass's peek count is
  the journal's.
- The keyboard handler `0x477bec` and the mouse handler `0x4809f8` are
  called from the own window procedure through the dispatch; their atexit
  functions (`0x47799c`, …) run in the own exit.
- The frame-buffer objects exist as guest state: the descriptor
  `[0x4d3020]`, the surface `[0x4d3024]` (current in `0x6c5ed0`), the row
  table `[0x4a03d0]`, the bits at `[0x4d3028]` (host region); the
  presentation frame `0x45fba4`, the present `0x464a14` and the frame clear
  `0x46548c` are reached through the dispatch
  ([presentation.md](presentation.md)).

### Open

- The keyboard layout of the recording machine for the `WM_CHAR` of a
  replayed key-down ([the characters of
  `TranslateMessage`](#the-characters-of-translatemessage)).
- `GetSystemPaletteEntries` on the recording machine (the colour bytes).
- The WinMain tail's pump with journal entries still due (it dispatches
  them before `WM_QUIT`, as the proxy would).

## The modal dialogs in the own process

The modal dialogs of [the section above](#the-modal-dialogs) as own code
in `sim/src/dialogs.c` (`include/es2/sim/dialogs.h`, in `//sim:services`),
headless, without a window, answered by the input journal's pump-keyed
entries, with the clock calls in the original order. 42 service rows
(`sim/dispatch/services/modal_dialogs.h`). The key dispatcher `0x45feb8` is the
hook `command_key`; the constructors, the destructors and the F12/Alt+P
entry `0x45cff0` are rows of [their own section](#dialog-constructors-and-the-f12altp-session). In
vitro; not checked live.

### Own code (listing ranges of DBSIM.EXE)

| Row | VA | Listing | What |
|---|---|---|---|
| `alert_run`, `confirm_run` | `0x456000`, `0x457b00` | `0x456000..0x456083`, `0x457b00..0x457b70` | the mission-end alert's and the quit confirmation's modal loop: the open, focus 0, per pass the control record, the stop word (alert only: state 1), the key handler (vtable +0x10), every gadget's tick (table +0xc), the step; the close; EAX = the end state's bit 0 |
| `options_run` | `0x456d68` | `0x456d68..0x4570f8` | the options dialog's loop: without a sound object the first four gadgets start in state 2; the page +0x8c with the background clear `0x487e60` bracketed by the driver slots when the presents are off, the page +0x88; per pass the control record, the keys, the pending option (0..8 pref toggle/cycle and the label's refresh count 2, 10 the store `0x459b94(9, 0x49e2dc)` and end 3), the page hook, the step |
| `dialog_open`, `dialog_close` | `0x45464c`, `0x4548c8` | `0x45464c..0x4548c4`, `0x4548c8..0x454abe` | port A (`0x433158`) marked and paused, the cockpit's port `[[0x49b088]+0x20b]` paused; the depth-0 flags; the pump `0x464c78`; the control record copied to d+0x300 and back; the key flags 1..4; the focus hit test; the panel, the GC `0x480d44`, the pages via the driver slots `[0x4a57ec]`/`[0x4a57f0]`; the draw flag; the callbacks `[0x49dd94]`/`[0x49dd98]`; the font (`0x492bec` + `0x430fb0` when `[0x49dd90]` is 0); `[0x4a5828]`/`[0x4a5824]`; the fill `0x488012`, the dialog's draw (vtable +0), the pen save; the close's blit `0x488143` and present `0x464a14`, the cursor placed back and the pen set, the ports resumed |
| `dialog_step` | `0x454acc` | `0x454acc..0x454b73` | the quit word -> state 1, the sound `0x464260([0x49f8ec])`, the release list, the one-pass hold-back of the end while an entry is shown (d+0x331), the present when due (clock site `0x454b51`, next = now + `[0x4d1e60]`) |
| `dialog_present_time` | `0x454ac0` | `0x454ac0..0x454acb` | `[0x4d1e64]` <- the clock (site `0x454ac0`), EAX = now |
| `dialog_keys` | `0x454e2c` | `0x454e2c..0x454f10` | the key handler: the click byte +0xd, the tab byte +0x17, words 1 (the default gadget), 0xf (next), 0x52 (previous), 0x1c (press the focused), 0x80f (previous); EAX bit 0 (AL) |
| `dialog_focus_set`, `_next`, `_previous` | `0x454c98`, `0x454d98`, `0x454dbc` | `0x454c98..0x454d60`, `..0x454dba`, `..0x454de4` | the focus with the pen (`0x486e7c`) placed at the gadget's centre (the cursor through `0x4808dc` when a device is current), or the hit test over the control record's point (i = -1) |
| `dialog_gadget_press`, `_tick`, `_fire` | `0x454de8`, `0x455080`, `0x45509c` | `0x454de8..0x454e2b`, `0x455080..0x455099`, `0x45509c..0x4550ca` | the press through the gadget table (+8) and the panel's release list `0x4530bc`; the tick count g+0x1f; the fire: the owner's select (vtable +0xc), the redraw (table +4), the part's table (+8) |
| `alert_select`, `confirm_select`, `options_select` | `0x45617c`, `0x457c4c`, `0x4573c4` | `0x45617c..0x4561c4`, `0x457c4c..0x457c84`, `0x4573c4..0x457456` | the selects: slot 0 -> end 2, slot 1 -> end 3 (the confirm: slot 0 -> 1); the options' pending slot d+0x356, the highlighted gadget d+0x3b6 (state 3/0, redrawn), the focus; then `0x454ac0` |
| `options_step`, `_label`, `_page`, `_store`, `_hook`, `_text` | `0x45719c`, `0x457210`, `0x457480`, `0x4574e8`, `0x45746c`, `0x457458` | `0x45719c..0x45720e`, `0x457210..0x4573c0`, `0x457480..0x4574e7`, `0x4574e8..0x4574fb` | the options' step (quit word, ticks, the first pending label, the release list, the present); the label of option k from the value tables d+0x38e/+0x396/+0x3a2 (word tables `0x49e2b2..` for 4, 6, 7, 8) through `0x438920([d+0x44], value)`; the page hook; the store; the accessors |
| `prefs_toggle`, `prefs_cycle`, `prefs_cycle_back` | `0x45999c`, `0x4599d4`, `0x459a10` | `0x45999c..0x4599d0`, `0x4599d4..0x459a0d`, `0x459a10..0x459a4d` | the pref byte flipped / plus one modulo n / less one modulo n through `0x459958` (the input layer's row); EAX = the new value (AL) |
| `panel_release_tick`, `panel_release_count` | `0x4530fc`, `0x4531a4` | `0x4530fc..0x4531a2`, `0x4531a4..0x4531b2` | per entry the clock (site `0x45311a`); expired entries redraw their gadget (table +4) and are removed; the count sign-extended |
| `port_mark`, `port_pause`, `port_resume`, `port_shift` | `0x435b38`, `0x435b58`, `0x435b80`, `0x435298` | `0x435b38..0x435b55`, `0x435b58..0x435b7c`, `0x435b80..0x435bec`, `0x435298..0x4352e9` | the ports' redraw byte, the pause clock (site `0x435b68`) at the first pause, the count-down and at zero the shift of the port's indexed entries (b+index*0x31, +0x24/+0x28), its item (q+0x49a: +0x1c/+0x20) and `[0x4d1294]` by the pause (sites `0x435b9a`, `0x4352a1`) |
| `key_flag`, `device_current` (`0x45bf68`, `0x45bd8c`), `panel_last`, `panel_free` | `0x45b734`, `0x45bf68`, `0x45bd8c`, `0x452bbc`, `0x452c04` | | the key flag bytes `0x4d22f5..`/`0x4d22fd..`; the current device `[0x4d2468+4*[0x4d24a0]]-0x3a`; the last panel; the panel's delete through `0x477548` |
| `dialog_cursor_save`, `dialog_cursor_restore`, `dialog_label`, `alert_text`, `confirm_text` | `0x454b8c`, `0x454c2c`, `0x454b74`, `0x4561c8`, `0x457c88` | `0x454b8c..0x454c28`, `0x454c2c..0x454c97`, `0x454b74..0x454b8b` | the pen state saved into the dialog (+0x2e6 = `[0x4a3618]`-4, +0x2ea/+0x2ee, +0x2f2 the font); the restore has no effect (only the empty routines); the label draw; the texts |

Everything is written as the listing has it (guest reads and writes through
`hud_internal.h`'s helpers over the bound loop's memory, the calls to other
code and the hooks through the dispatch, the clock through
`hud_clock` so that `es2_sim_hud`'s trace holds the sites). The modal loops
count their passes in `es2_sim_dialogs.passes`; `pass_limit` (0: none)
refuses a dialog that does not end, a safety net for the tests. The pump of
a pass is the loop's row `pump`: a pass's peek is the journal's, and the journal's
pump-keyed entries reach the handler list `[0x4d3b30]` from inside the
dialog exactly as the proxy delivers them (the unit test `alert_journal`
and the in-vitro `alert` part).

### Headless decisions

- **The cursor placement** `0x4808dc` (focus, close): `GetClientRect` is the
  row `client_rect` (the game size); `ClientToScreen`/`SetCursorPos` are not
  made (no window) and counted in `es2_sim_hud.headless`; the arithmetic
  (the two `idiv`s, refused on a zero half size at `0x480909`) is done so
  that the counts are the original's.
- **Empty routines** `0x48992e`, `0x486e70` (the cursor restore `0x454c2c`
  and `0x454b8c`'s tails) and `0x4898d4` (`0x48993a`) are not called; their
  argument computation (`[f+4]`) is not reproduced (reads only).

- **The joystick page** (options slot 9: `0x457d38`, `0x45866c`,
  `0x4585f4`) is own code ([input.md](input.md#the-joystick-page-and-the-sdl3-joystick),
  `sim/dispatch/services/joystick_page.h`); the own options run calls it at
  `0x457036..0x457093`.

- **`0x454d64`** (the focus to a gadget's slot) has no static reference in
  DBSIM.EXE (no relocated pointer, no `call rel32`): dead code, no row
  (`//sim:table_test` would reject the row for lack of entry evidence).
- **EAX:** the key handler `0x454e2c` and the prefs rows `0x45999c`,
  `0x4599d4`, `0x459a10` return a byte in AL with the original's scratch
  above it; the own rows return the zero-extended byte. Compared as AL in
  vitro; no caller reads more (`0x456000`/`0x456d68` discard it, the
  options' pending switch reads nothing back).
- **SEH frames** of the modal loops and the net-zero counter bumps around
  them are not reproduced (no guest state after the return differs).
- **The one-pass hold-back**: while a release entry is shown
  (`0x4530fc` returns a count), the step keeps d+0x331 and the dialog ends
  one pass later; the in-vitro `alert` part covers it (the script's end sets
  the quit word every pass; the entries expire on the moved clock).
- **The release list's capacity** (known difference): the list holds 8
  entries (`+0x356..+0x395`); `0x4530bc` compares the count with 7
  (`0x4530c3`) without a branch, so a ninth entry lands on the count word
  `+0x396` and 6 bytes past the `0x398`-byte panel, and the next `0x4530fc`
  scan reads gadgets past the list (the original faults at `0x453127`: DEMO1
  tape playback, [oracle_suites.md, "Tape playback
  suites"](../reference-game/oracle_suites.md#tape-playback-suites)). The own
  dialog code refuses the ninth append (`0x4530bc`, exit 70) and a scan with a
  count above 8 (`0x4530fc`, which also catches the mission's own press
  path); below 9 entries nothing changes. `//sim:dialogs_test` refuses a
  press with 8 entries (nothing written past the list) and a scan with 9.

- **`WM_CLOSE`/`WM_QUERYENDSESSION`** (`sim/src/loop.c`, `0x466508`,
  `0x466530`): the demo word `[0x4d2530]` sets the stop word; otherwise the
  key dispatcher `0x45feb8(0x410)` through the dispatch (the hook
  `command_key`) and its answer kept at `[0x4d370c]`
  (`WM_QUERYENDSESSION`: the result 1 when the sign-extended answer is 2).

### Acceptance

- **Unit** `//sim:dialogs_test` (synthetic image at `0x620000`, 7 cases):
  the alert answered by the journal (the entry delivered at the frame's
  third peek, the handler, the press held back one pass, the ports' shifted
  times, the clock order of [the run loops](#pump-calls-and-clock-reads-per-pass-the-oracle-contract)), the stop word and `WM_QUIT`, the key handler,
  the release list, the options dialog (five tabs, a click, Enter: pref 8,
  the label refreshes, the store), the pass limit, `WM_CLOSE` and
  `WM_QUERYENDSESSION`.
- **In vitro** `//tools/reference-game/invitro:differential_dialogs_test`
  (`dialogs_cases.h`, about 75 s): alert 1,500, options 800, rows 2,000 and
  wndproc 1,000 cases identical (DATA `0x497000..0x6cc000`, the emulated
  heap, the stub log with every argument, EAX, the messages dispatched, the
  peeks, `SetCursor`, the journal's state, `SetCursorPos` against
  `headless`, the virtual time, the clock sites against that list).
  Coverage of the run: 62,608 modal passes, 9,896 journal entries delivered
  inside dialogs; 1,786 dialogs ended by the stop/quit word, 250 by gadget 0,
  264 by gadget 1 or the store. The harness stubs the other code
  on both sides (the control record `0x45a810` with the original pump, the
  key dispatcher, the store, the surface fill/clear/blit, the present, the
  label draw, the GC, the sound, port A, the font load, the path join) and
  runs `0x486e7c`, `0x48993a` and `0x4530bc` as the original on both sides.

- **Mutation:** `options_run`'s EAX `& 1u` -> `& 3u`: `//sim:dialogs_test`
  fails at the options case.

### Suites and replay

The suites whose journals open a dialog: `c1_01-options-dialog` (F12 and
Alt+P with the pump-keyed Esc), `c1_01-quit-dialog` (Ctrl+Q, then Tab and
Enter pump-keyed in frame 201: the process ends by itself),
`c3_10-detail-options`, and every ending suite with the mission-end alert
(`c1_01-success` answers it with Enter at the end). All of them replay
IDENTICAL ([replay table](../reference-game/own_process.md#replay-table): `c1_01-options-dialog` 700 ticks,
`c1_01-quit-dialog` 201 ticks with the exit code compared,
`c3_10-detail-options` 700 ticks, `c1_01-success` 6198 ticks).

## The shell leftovers as own rows

The callees that the start-up and the loop's own code left as hooks or
inline copies, and two platform points: the characters of
`TranslateMessage`, and `GetSystemPaletteEntries` headless. The code is in
`sim/src/loop.c`, with the translate answer in
`sim/include/es2/sim/loop.h` and `sim/src/main.c`. The rows are in
`sim/dispatch/services/shell_leftovers.h`. Every body was read from the
`llvm-objdump` listing of the reference DBSIM.EXE; REA was used only
read-only, on a scratch copy, for orientation.

### Rows (listing ranges of DBSIM.EXE)

| Row | VA | Range | Contract |
|---|---|---|---|
| `key_capture_push` | `0x461250` | `0x461250..0x461271` | Byte `[0x4d2a80 + depth]` = the capture flag `[0x49eaa5]`. The depth `[0x49f704]` goes up by 1, then `0x45b68c(0)` runs. EAX/EDX: the callee's. ECX: 0 (the popped argument). |
| `key_capture_pop` | `0x461274` | `0x461274..0x461291` | The depth goes down by 1, then `0x45b68c(flag)` runs with the saved flag in CL and ECX's upper bytes as at entry. EAX/EDX: the callee's. ECX: that dword. |
| `hud_sub_object` | `0x4610ad` | `0x4610ad..0x4610b8` | EAX = `[[0x49b088] + 0x1e1]`. EDX and ECX unchanged. |
| `language_path` | `0x45ef1c` | `0x45ef1c..0x45ef81` | Byte `0x49f257` = `[0x4d2536]`. b = `0x492b90(name, ext)`. `0x4d291c` = "st<letter>" (an inline copy), then `_strcat(.., "\\")` and `_strcat(.., b)`. Registers: the second `_strcat`'s. |
| `voice_name` | `0x45ef84` | `0x45ef84..0x45ef9b` | `movsx` of `[0x4d2536]`. Unless the letter is `0x72` ('r'), byte 7 of "simvoice" (`0x49f260`) = the letter. EAX = `0x49f259`, EDX = the sign-extended letter, ECX unchanged. |
| `cursor_store` | `0x464c68` | `0x464c68..0x464c74` | `[0x4d3720]` = the argument. EAX = the argument. |
| `window_nothing` | `0x4650cc` | `0x4650cc` | `ret` (`es2_sim_service_nothing`). |
| `timer_kill` | `0x4650d0` | `0x4650d0..0x4650e6` | EAX = `[0x4d3608]`. If it is nonzero the original calls `KillTimer([0x4d3610], it)`; the own row refuses that call at `0x4650d9`. Nothing in DBSIM writes `0x4d3608`. The inline copies refuse the same way (WM_DESTROY, the loop tail, `startup.c`'s fatal report). |
| `subject_label` | `0x45e208` | `0x45e208..0x45e45e` | Described below. |

#### `0x45e208`, the subject label

Callers: `0x45d427` and `0x45d44c` in `0x45cff0`, and `0x45df29` in the
view apply.

1. **First-label byte.** `0x49f211` is set once.
2. **Font.** F = `[0x4d1e2c]`, kept in `0x4d27a4`. The height is
   h = `movsx [F+0xa]`.
3. **y and the rectangle.**
   - W1 = `[0x4d253e]` - 1, H1 = `[0x4d2542]` - 1, top = `[0x4cfa4c]`.
     (`0x42db10` answers `0x4cfa40` and is inlined.)
   - y = ((H1 - top) `sar` 1) + top - (h `sar` 1), computed in 16 bits.
   - If H1 - 1 - h < y (signed), then y = (H1 - 1) - h, in 16 bits.
   - The rectangle `0x4d27a8` = {0, y, W1, y + h}.
4. **First fill.**
   - GC = `[0x6c5f70]`. GC+0x208 and GC+0x20c are saved and zeroed.
   - The frame's colour pair {0, `movsx [0x4d3ba2]`} becomes GC+0x22c,
     which is left pointing at the dead frame.
   - Then `0x486704(0x4d27a8)` runs.
5. **Shift and first point.** The shift is s = `[0x4d253a]` & 31
   (`shl r32, cl`). The point is {`movsx [0x49f02e]` << s, `[0x4d27b4]`}.
6. **The subject's name.** The subject is `[[0x4d24ea]+0x32]`.
   - If it is the player `[0x4d24e6]`, the name is `[0x4d14dc]`.
   - Otherwise the hook `wingman_index` `0x433134(subject)` runs (through
     the dispatch). A low word of 0xffff gives
     `0x49f240` ("").
   - Any other index gives `[[0x49d230] + 334 * index + 0x264]`. This is
     `0x434298`/`0x44b928` inlined; `0x44b928` never reads its argument.
7. **The label text.** The text is `[0x4d16a0]` + the name (an inline
   strlen and `rep movs`, then `_strcat`).
   - Non-empty: `0x482634(F', point, text)`.
   - Empty: the box {x, y - `movsx [F'+0xa]`, (0x32 << s) + x, y}, with the
     colour read again, then `0x486704(box)`.
8. **The value text.**
   - x = `movsx [0x49f030]` << s.
   - The text is `[0x4d16a4]` + (`[0x4d24f0]` (word) ? `[0x4d16a8]` :
     `[0x4d16ac]`).
   - Then `0x482634` runs and GC+0x208/0x20c are restored.
9. **Registers.** EAX, EDX and ECX are the last `0x482634`'s.

**Frame limits.** The frame's text buffer holds 0x44 bytes before the
saved registers. A longer text would overwrite the original's frame; the
own row refuses it by name at `0x45e208`. The own frame (0x54 bytes) comes
from the loop's call area, and `0x45e20c` refuses when that area is
exhausted.

### Inline copies kept (no change)

Where own code already did a row's work, it keeps doing it inline. Each
copy was checked against the row's listing:

- **`startup.c`:**
  - `0x464c68` after `LoadCursorA(IDC_WAIT)`;
  - `0x4650cc`/`0x4650d0` in the fatal error report's tail (the
    original's `0x479cc8`/`0x479ccd`).
- **`loop.c`:**
  - the window procedure's WM_DESTROY (`0x46613e`/`0x466143`);
  - the loop tail (`0x45f541`/`0x45f546`);
  - the loop start (`0x45f449`, `0x464c68`).
- **`audio.c` and `hud_cockpit.c`:** `0x45ef1c`.
- **`input.c` and `hud_widgets.c`:** `0x45ef84`, inside `0x45ef9c`.

The rows serve the guest callers that the dispatch reaches:

- the modal dialogs' callbacks `[0x49dd94]`/`[0x49dd98]` (stored at
  `0x461b10`/`0x461b1a`; nothing calls them directly);
- `0x4610ad`, called from `0x410b2b`;
- `0x45ef1c`, called from the dialog constructors (`0x455b53`, `0x45637f`,
  `0x456867`, `0x457763`, `0x457ec4`) and from `0x4310c9`, `0x435a0d` and
  `0x462562`;
- `0x4650cc`/`0x4650d0`, called from `0x4652e7` and `0x46685d`.

### The characters of `TranslateMessage`

**Nothing reads them.** None of the window handlers (`0x477bec`,
`0x4809f8`, `0x462398`, `0x4718d4`) and no case of the window procedure
`0x466034` reads `WM_CHAR` (0x102), `WM_DEADCHAR` (0x103) or
`WM_SYSDEADCHAR` (0x107). `WM_SYSCHAR` (0x106) returns 0. Only the number
of characters matters. Each one is one more message, so one more
`PeekMessageA` with `PM_REMOVE` in the frame, and that moves every
pump-keyed journal entry (`p`) after it.

**Host measurement** (a probe on a STATIC window, layout 00000407,
Windows 10 32-bit; the probe script and its log were not kept):

- A dead key (VK 0xdc '^', 0xdd '´') makes `TranslateMessage` post one
  `WM_DEADCHAR`.
- A following key that does not combine posts two `WM_CHAR`, the accent
  first: '^' then 'P' gives 0x5e, 0x70; '^' then Esc gives 0x5e, 0x1b.
- Space after a dead key gives one character, the accent.
- A dead key after Alt posts `WM_SYSDEADCHAR`.
- `ToAscii` with an all-up key state gives the same answers (-1, 2, 1). It
  shares the dead-key state with `TranslateMessage`, and that state is the
  session's, not the process's: a dead key one process translates is still
  pending in the next process ([the dead key as a known
  difference](#the-dead-key-as-a-known-difference)).
- Of two characters, `TranslateMessage` posts the first (the accent that did
  not combine) with lParam bit 25 (`0x02000000`, a bit Windows documents as
  reserved) set, the second with the key-down's lParam; `WM_SYSCHAR` alike.

**Why the count matters.** In `c1_01-keys-extra`, '^' comes at frame 585
and P at frame 700, the same frame as a pump-keyed Esc (`p` 300). A model
that posts nothing for the dead key and one character for the pair moves
that Esc by one peek.

**Own model.** The platform's `translate` answers one of:

- 0: no character;
- one character (`ES2_SIM_TRANSLATE_ONE`, which also marks the character
  0);
- two characters (`ES2_SIM_TRANSLATE_TWO`, the first in bits 0..7);
- a dead key (`ES2_SIM_TRANSLATE_DEAD`: one `WM_DEADCHAR` or
  `WM_SYSDEADCHAR`).

`es2sim` passes the host's `ToAscii` (`main.c`). Headless, without a host
answer, the default stays: no characters.

**Hypothesis** (the journals cannot confirm it): the recording machine had
the same layout as this host (00000407). CapsLock and NumLock do not change
the count for the VKs the scripts press.

### `GetSystemPaletteEntries` headless

**Host measurement** (a probe; script and log not kept). The desktop is
32-bit: `NUMCOLORS` -1, `SIZEPALETTE` 0, `RASTERCAPS` 0x7e99 (no `RC_PALETTE`). `GetSystemPaletteEntries(dc, 0, 256,
p)` returns 0 but writes all 1024 bytes:

- entries 1..9 and 246..255: the static colours;
- entry 0 and entries 10..245: zero.

The result is the same for `GetDC(0)`, a memory DC and a window DC. The
answer under the 256-colour compatibility shim is unknown: the shim did not
apply to the probe.

**Effect in DBSIM.** Only the DIB colour table at `0x4d3074` (the byte swap
of `0x464d44`) is affected, and only before the first palette upload. The
palette load (`0x4d372c`) and the present overwrite all entries, and no
watch or tick-log field reads them.

**Decision.** Headless keeps "0, nothing written" (the loop's headless
deviations). A host answer belongs to the windowed presenter.

### Deviations

- `timer_kill` refuses when a timer is set. There is no headless timer,
  and DBSIM never sets one.
- A subject label text longer than the original's frame is refused instead
  of overwriting saved registers.
- The palette answer stays headless (see above).

### Acceptance

- **`//sim:loop_test`, section 5:**
  - the dead-key pump: 8 peeks, messages 0x100, 0x101, 0x100, 0x101,
    0x103, 0x102 '^', 0x102 'w';
  - `WM_SYSDEADCHAR` and the character 0;
  - every row through the dispatch, with registers;
  - the capture push/pop with a recording `0x45b68c`;
  - the subject label's player, no-index, index and clamp cases, and the
    overrun refusal.
- **In vitro, `//tools/reference-game/invitro:differential_shell_rest_test`**
  (`shell_rest_cases.h`, about 50 s): 2,000 call cases and 2,000 label
  cases identical. Compared: DATA, the heap, the world, the stand-in log
  and the registers of the contract. Every branch key was reached.
- **`//tools/reference-game/invitro:differential_loop_test`:** unchanged
  (its translate model has no dead keys).

- **Mutation:** `voice_name`'s `0x72` -> `0x73` makes the suite fail at
  call case 34 (`0x49f260` differs).

### Open

- The `GetSystemPaletteEntries` answer under the 256-colour shim (the
  presenter).
- The recording machine's keyboard layout (the hypothesis above).

## Dialog constructors and the F12/Alt+P session

The dialog constructors, the destructors, their draws and the F12/Alt+P
options session as own code in `sim/src/dialogs.c`, 23 service rows in
`sim/dispatch/services/dialog_ctors.h`. Every function was read from the
`llvm-objdump` listing of the reference `DBSIM.EXE` (ranges below);
in vitro; the dialog suites replay IDENTICAL
([own_process.md](../reference-game/own_process.md#replay-table)); not
checked live.

### Rows

| VA | Row | Listing | What it does |
|---|---|---|---|
| `0x453824` | `text_block_init` | `0x453824..0x453847` | the text block's three dwords and two words cleared, counter +1; EAX the block |
| `0x453848` | `text_block_free` | `0x453848..0x453876` | counter -1, the buffer `[h]` freed (array delete), the block with flag bit 0 |
| `0x4541b8` | `dialog_ctor` | `0x4541b8..0x4543af` | the dialog base (below) |
| `0x4543b0` | `dialog_dtor` | `0x4543b0..0x4544bd` | the base's destructor (below) |
| `0x454f14` | `dialog_rect` | `0x454f14..0x454f4e` | once byte `0x49ddfc` raised; `[0x4d1e88]` <- {x, y, x+w, y+h}; EAX the rectangle, EDX y+h |
| `0x454f50` | `dialog_rect_centred` | `0x454f50..0x454f7c` | centred on `[0x4d1e70]`/`[0x4d1e74]` (arithmetic shift), then as `0x454f14`; ECX the x |
| `0x454f80` | `dialog_button_init` | `0x454f80..0x455012` | the gadget base `0x4524a0`, the button's tables, owner and index, state 0, the colours `[0x4d1e20]`, `[0x4d1e24]`, `[0x4d1e28]`, `[0x4d1e20]`, counter +2 |
| `0x455014` | `dialog_button_draw` | `0x455014..0x45507d` | the state's image (`0x45c8bc`), the owner's text (table +0x14), the state's colour, the label draw `0x438920`; bracketed by the driver slots `[0x4a5828]`/`[0x4a5824]` when the presents are not due |
| `0x455924` | `alert_base_ctor` | `0x455924..0x45594e` | the base with the alert's table, counter +1 |
| `0x455950` | `alert_ctor` | `0x455950..0x455faa` | the mission-end alert: shifted 0xde x 0x6d, its once table (guard `0x49e04c`), the buttons of the status class, the status texts (status 5: `[0x4d1e98]`), four line labels; status 2 clears o+0x330 |
| `0x455fac` | `alert_dtor` | `0x455fac..0x455ffc` | the four line labels, the base (`0x4543b0` with 0) |
| `0x456084` | `alert_draw` | `0x456084..0x456178` | backdrop, the leading non-empty status lines (`0x496766`, at most 4; a single line goes to the second label), the gadgets; also the pause dialog's draw (table `0x49e1e4` +0) |
| `0x4561dc` | `pause_ctor` | `0x4561dc..0x4566de` | the pause dialog: shifted 0x59 x 0x22, guard `0x49e138`, status 0 one button, status 1 two |
| `0x4566e0` | `options_ctor` | `0x4566e0..0x456cee` | the F11/F12 options dialog: shifted (0, 0x96) 0x13f x 0x59, guard `0x49e2da`, the bitmap `0x49e3ba` kept in `[0x4d1efc]`, eleven buttons from the word tables `0x49e1fc..0x49e254`, nine value labels |
| `0x456cf0` | `options_dtor` | `0x456cf0..0x456d67` | the nine labels; with `[0x4d1efc]` set: `0x48349c(it, 3)` and o+0x30 cleared; the base |
| `0x4570fc` | `options_draw` | `0x4570fc..0x457199` | backdrop, gadgets, the value labels (`0x457210`, `options_label`) |
| `0x457538` | `objectives_ctor` | `0x457538..0x457a9a` | the mission objectives: shifted 0x13b x 0x8b, a second text block o+0x332, guard `0x49e3fc`, the objective texts (count word `0x4a9e44`, indices `[0x4a9e48]` into `0x4aa660`), one button, seven line labels |
| `0x457a9c` | `objectives_dtor` | `0x457a9c..0x457afd` | the seven labels, the second text block, the base |
| `0x457b74` | `objectives_draw` | `0x457b74..0x457c49` | backdrop, the non-empty objective texts (at most 7), gadgets |
| `0x42d89c` | `view_rect_set` | `0x42d89c..0x42d943` | the view rectangle `0x4cfa40` and origin `0x4cfa50` (saved to `0x4cfa28`), the copy `0x4cfca4`; with a camera entry (`0x429820`) of kind 4 also into the graphics context +0x210/+0x220; EAX 0, the entry, or the context +0x220 |
| `0x42d944` | `view_rect_reset` | `0x42d944..0x42d9e5` | the same, restored from `0x4cfa28` |
| `0x45cfc8` | `options_idle` | `0x45cfc8..0x45cfee` | the session's page hook: `0x401c74([0x4d24ea], -0x40, 0, 0)`, `0x4011a0`, `0x45fca8` |
| `0x45cff0` | `options_session` | `0x45cff0..0x45d48e` | the F12/Alt+P session (below) |

The dialog base `0x4541b8(o, save, rect)`: the tables, the text block
o+0x34, the graphics context o+0x48 (`0x480c34`, run inline as
`hud_gc_construct`), the holder o+0x2e2 (`0x48358c`, inline), the
rectangle o+4 and its copy o+0x258, the inner size o+0x1c, with save's
low byte a save buffer of `0x488045`'s size (array new), the panel
o+0x285 (new 0x398, `0x4526ec`), the page o+0x28d, default gadget -1.
The destructor `0x4543b0(o, f)`: counter -1, the gadgets deleted, the
save buffer, the owned bitmap o+0x30 (RTTI count, virtual delete +0x18),
the title label o+0x44, the holder (`0x4835c8` with 2, inline), the text
block (`0x453848` with 2), counters -2 and -3, o itself with f bit 0. The
dialog method tables (`0x49e034` base, `0x49e120` alert, `0x49e1e4`
pause, `0x49e3e4` options, `0x49e4d0` objectives) hold no destructor
slot: +0 draw, +4 frame `0x454b74`, +8 modal loop, +0xc key, +0x10
`0x454e2c`, +0x14 owner text (read from the reference image's DATA).

The session `0x45cff0()` (F12 and Alt+P from the key dispatcher
`0x45feb8`): the options dialog (new 0x3c7, `0x4566e0` with 0) over the
3D view; with G = `0x4d24bc`: G+0x36 forced to 1 (low byte kept),
`0x4d267e` cleared, a view record (`0x48bd84`, table `0x49f8e8`) holding
a copy of the camera `[G+0x2e]` (bytes 4..0x4d), the view rectangle
{0, 0, [G+0x82]-1, [d+8]-1} with the origin {-x2 >> 1,
(int16)(0xffce << G+0x80)} (`0x42d89c`). Camera mode `[[G+4]+0x14]`
other than 4: frames until the display `[0x4cf99c]` is idle (byte +0x1c
clear, +0x18 = -1), the display request `0x42a3f4(view, 2)`, frames
until the display turned busy and idle again (the offsets
`0x45edb0(G+0x2a)` at its first busy frame); mode 4: the offsets and one
frame. Then the page hook `0x45cfc8`, the modal loop (table +8), the
dialog deleted (`0x456cf0` with 3), the camera restored, the rectangle
restored (`0x42d944`), the camera mode restored over two frames (mode 4:
`0x42da08` and the subject label `0x45e208`), G+0x36 restored, the view
record destroyed (`0x48bcc4` with 0). The own frame loops count against
the dialogs' pass limit (a refusal instead of a hang).

### Verdicts

- `0x45feb8` is the hook `command_key`; it gets no row here. Its F12/Alt+P
  case reaches `0x45cff0` through the dispatch, so own key code
  only has to call VA `0x45cff0` (and the constructors `0x455950`,
  `0x4561dc`, `0x457538` with the destructors `0x455fac`, `0x4543b0`,
  `0x457a9c` for the alert, pause and objectives dialogs).
- `0x480c34`, `0x48358c` and `0x4835c8` run inline inside the own rows
  (no rows of their own here; `0x48358c` is called only by `0x4541b8`
  among the rows).
- `0x42a3f4`, `0x45e208`, `0x45ef1c`, `0x4011a0`, `0x45fca8`, `0x45edb0`
  and `0x401c74` are reached through the dispatch (hooks or other
  rows).
- ECX of `0x454f50` (the x) is set by the row but not compared in vitro:
  the harness's machine call returns EAX and EDX only. The listing
  (`0x454f50..0x454f7c`) leaves the x in ECX; no caller among the rows
  reads it.

### Evidence

- **In vitro** `//tools/reference-game/invitro:differential_dialog_ctors_test`
  (`dialog_ctors_cases.h`, about 140 s): dialogs 1,500, rows 1,500 and
  session 750 cases identical (DATA `0x497000..0x6cc000`, the emulated
  heap, the stub log with arguments or their contents, EAX of the
  constructors, `0x453824`, `0x454f14`, `0x454f50`, `0x454f80`,
  `0x42d89c`, `0x42d944`, EDX of `0x454f14`/`0x454f50`). Operator
  new/delete, `0x49667c`, `0x48de2f`, `0x49545b`, `0x480c34` and, inside
  the set, the rows themselves run as the original on its side; the
  other callees and the method-table and driver slots are
  recording stubs on both sides; `0x45fba4`'s stub plays a display script
  so both frame loops end. Every coverage key is non-zero (all six
  constructors, the alert's three button classes and statuses 2 and 5,
  pause statuses 0/1/other, at a position and centred, save buffer or
  none, once tables filled and kept, the options bitmap kept and cleared,
  both destructor flags, the text block's null/buffer/empty frees, camera
  entry null/kind 4/other, session mode 4 and the waiting frames).
- `//tools/reference-game/invitro:differential_dialogs_test`,
  `//sim:dialogs_test`, `//sim:services_test` and `//sim:table_test` pass.

- **Mutation:** `0x454f50`'s y `>> 1` -> `/ 2`: the suite fails at rows
  case 14 (EDX `203b2ce5` / `203b2ce6`).

### The joystick page

Options gadget 9 (`0x457036..0x457093`) runs the joystick page: its
constructor `0x457d38` (listing `0x457d38..0x4585f0`), modal run
`0x45866c` (`0x45866c..0x458c83`, a 15-way jump table at `0x458859`),
destructor `0x4585f4` (`0x4585f4..0x45866b`: twelve labels at o+0x3f6,
the bitmap `[0x4d1f34]` through `0x48349c(it, 3)`, the base) and the
helpers `0x457cf8`, `0x458c84`, `0x458d3c`, `0x458ed8`, `0x45915c`,
`0x459314`, `0x45933c`, `0x4593a8`, `0x459894`, `0x459958`, `0x4599d4`,
`0x459a10`. The run reads the joystick object (`0x45c524(3)`, `0x477904`)
and an assignment byte table at `0x4d1f38` indexed from `[0x4d2577]`.
Contract and own code: [input.md](input.md#the-joystick-page-and-the-sdl3-joystick).

## The message trace of the replay pump

A diagnostic of the proxy (`input_journal.h`): which messages DBSIM's
`PeekMessageA` calls remove besides the journal input, per frame and per
pump call. It counts the `0xa000` (SOS voice notice, [next
section](#the-sos-voice-notices-in-es2sims-pump)) and `MM_MCINOTIFY`
retrievals inside the options dialogs, the cause of a class of known
differences ([own_process.md](../reference-game/own_process.md#the-known-differences-list)).

### The calls it sees (static, listing)

`PeekMessageA` is IAT slot `0x6cc40c`; its only reference is the thunk
`0x49691c: jmp dword [0x6cc40c]` (`input_journal.h` replaces the slot).
The thunk has two callers, both `call rel32` with (msg, 0, 0, 0,
`PM_REMOVE`) pushed:

| Call site | Return address | Pump | Status |
|---|---|---|---|
| `0x464ca2` | `0x464ca7` | `0x464c78` (the [pump](#the-pump-0x464c78-0x464c780x464cc0), 15 callers; the modal loops reach it through `control_record` `0x45a824`) | live |
| `0x465a05` | `0x465a0a` | `0x4659e8` (no `WM_QUIT` test); only caller `0x465a1a` in `0x465a14`, which has no caller | dead |

Verified: `llvm-objdump -d` of `VER95\DBSIM.EXE`
(docs/reference_version.md): the only operand `0x6cc40c` in the listing is
the thunk; the only `call 0x49691c` are the two rows above, each preceded
by `push 1` (`PM_REMOVE`), three `push 0` and the `MSG` pointer. So every
traced call of a real run should show `"c":"0x464ca2"`; any other value is
a finding.

### Switching it on

- INI: `[input] message_trace=1` in `es2-oracle.ini` (any other value or
  no key: off). The file is `<[trace] tick_log>.messages.ndjson`, beside
  the tick log (not `*.jsonl`: the evidence rules treat a `*.jsonl` in a
  run directory as a raw tick log, as for the presenter's
  `.capture.ndjson`). It is created new; an existing file, or no
  `[trace] tick_log`, refuses the trace (`message_trace_refused` in the
  proxy's event log) and the run goes on without it.
- Scripts: `setup/configure-tick-log.ps1 -MessageTrace`, or
  `ES2_MESSAGE_TRACE=1` in the environment (the switch's default, so it
  passes through `run-oracle-parallel.ps1`, `run-oracle-regression.ps1`,
  `run-tick-log-mission.ps1` and `capture-dbsim-frames.ps1`, whose
  children call that script). The script writes `message_trace=1` as the
  last key of `[input]`, refuses an existing trace file before the
  deployment and records the path as `message_trace` in
  `oracle-media-manifest.json`. `capture-dbsim-frames.ps1` deletes its
  work directory after a run with every suite ok, so a traced capture
  needs `-KeepWork`.
- Without the key nothing changes: no file, no record, the hook takes the
  same paths with the same results (the seven other `input_journal_*`
  fixture modes and the window-shim, presenter, RTL-life and oracle-speed
  fixtures pass unchanged; `input_journal_trace_off_test` checks that a
  configured tick log alone creates nothing). Any input mode works (off,
  record, replay); the measurement is meant for replays.

### Format

One JSON object per line (CRLF), written from a 64 KiB buffer that is
flushed whenever the frame counter `[0x4d257b]` has moved (a terminated
process loses at most the current frame) and at the proxy's detach.

- Start: `{"event":"start","schema":1,"kind":"message_trace","key":"frame_counter_0x4d257b","pump":"pm_remove_calls_in_frame"}`.
- A message: `{"k":K,"f":F,"p":P,"m":M,"w":W,"l":L,"h":H,"t":T,"c":"0x…","r":"0x…"}`
  - `k`: `msg` (the real `PeekMessageA` removed it and DBSIM gets it),
    `drop` (real keyboard or mouse input removed and withheld by the
    replay; it shares `p` with the call's final result), `inject` (a
    journal entry delivered instead of a real peek; `t` = 0).
  - `f`: the frame counter at the call.
  - `p`: the call's pump ordinal, the number of `PM_REMOVE` calls earlier
    in the same frame. This is the journal's `"p"` count (`ij_count_pump`,
    [the pump](#the-pump-0x464c78-0x464c780x464cc0)): an entry with `"p": N` is injected at the first call
    whose `p` is at least N.
  - `m`, `w`, `l`, `h`, `t`: message, wParam, lParam, hwnd, `MSG.time`
    (unsigned decimal). `0xa000` = 40960, `MM_MCINOTIFY` = 953 (`0x3b9`).
  - `c`: the VA of the `call rel32` that made the call (return address
    minus 5 when the byte there is `E8` inside the image), `"0x0"` for a
    caller outside DBSIM; `r`: the return address.
- Per frame, when the counter has moved and at the end: `{"k":"frame","f":F,"pumps":N,"empty":E,"msgs":M,"inject":I,"drop":D,"noremove":R}`:
  `PM_REMOVE` calls, those that returned FALSE, `msg` records, `inject`
  records, `drop` records, other calls; `pumps = empty + msgs + inject`.
  A frame without any `PeekMessageA` call has no record.
- End: `{"event":"end","other_thread":n}`. Only the thread of the first
  traced call is traced (DBSIM's main thread); calls from other threads
  are only counted here.

Not visible: messages that `PeekMessageA` dispatches internally (sent
messages such as the focus messages), and messages retrieved by loops
inside USER32. The menu loop that F10 or a lone Alt starts in
`DefWindowProcA` with a visible window pumps on its own, so for the F10
run the trace shows only the frames and pump ordinals around it.

The trace costs a formatted line per removed message and one file write
per frame change; inside a modal dialog (frame counter standing) it
writes only when the buffer fills. The SOS timer posts in wall time, so
runs to be compared should all have the trace on.

### The frame-600 spread of `c3_10-detail-options`

The presents of `c3_10-detail-options` frame 600 (the detail options
dialog, 146 or 147) differ between runs of the original
([presentation.md](presentation.md#the-originals-own-frame-spread)).
The dialog presents once per modal pass, each pass is one pump call
`0x464c78`, and each pump call is (messages + 1) `PM_REMOVE` calls. Per
run, for frame 600 (and 204..230), the trace gives:

1. the `inject` records' `p` (fixed by the journal);
2. the `msg` records with `m` = 40960 or 953 and `p` below each inject's
   `p`: the asynchronous retrievals that use up pump ordinals;
3. the passes before each injection: the inject's `p` minus the number of
   `msg` and `inject` records with a smaller `p` (what is left are the
   empty calls, one per pass); the frame record's `empty` is the total.

The hypothesis (`0xa000` / `MM_MCINOTIFY` explain the spread) holds if,
between the runs, the difference in frame 600's present count (the
capture report of the same run) equals the difference in item 3, and
that equals the difference in item 2's count of `m` = 40960/953
retrievals. It is refuted if item 2 is the same in all runs while the
presents differ, or if other message ids (`m` other than 40960/953, for
example `WM_TIMER` or `WM_PAINT`) carry the difference. `c` confirms
which pump removed them; `h` tells the main window from another one (an
MCI notify window); `t` places the posts in time.

### Result of the frame-600 runs

Three runs of `c3_10-detail-options` and `c1_01-options-dialog` with the
trace on (`capture-dbsim-frames.ps1 -HiddenWindow -KeepWork`, labels
a, b, c), and one visible run of `c5_05-moon-missiles`; every run
ended identical to its golden.

| `c3_10` frame | presents (a/b/c) | pumps | empty (a/b/c) | `0xa000` (a/b/c) |
|---|---|---|---|---|
| 204 | 461 / 460 / 461 | 503 | 459 / 458 / 459 | 11 / 12 / 11 |
| 600 | 436 / 437 / 435 | 483 | 434 / 435 / 433 | 19 / 18 / 20 |

- The hypothesis holds. `pumps` is fixed by the journal; in every row
  presents = empty + 2 and empty = pumps - messages - injects, so each SOS
  voice notice (`0xa000`, wParam `0xa003`) retrieved inside the dialog
  costs one modal pass and one present. The suite's total of `0xa000` is
  84 in all three runs; only their spread over the frames (wall time)
  differs. This is the frame-600 spread.
- No `MM_MCINOTIFY` in any run; the other ids are `WM_CHAR` (from the
  journal's key-downs) and one `0x31f` at frame 0. Every call came from
  `0x464ca2`.
- `c1_01-options-dialog`: dialog frames 300 and 500 have the same counts
  in all three runs (1 and 4 messages), no present difference.
- F10, visible window: the journal's `WM_SYSKEYDOWN`/`WM_SYSKEYUP`
  (`0x79`) at frames 8113..8134 are injected at pump 0 and the frame
  counter goes on with one present per frame, so no menu loop holds the
  main thread there. Loops inside USER32 stay invisible to the trace.

- Consequence for es2sim: a pump that posts `0xa000` where SOS posts it
  gives the dialog frames the original's structure (pumps fixed, presents
  = pumps - messages - injects + 2); their counts match a given run only
  when its timer phase is reproduced.

### Tests

- `//tools/reference-game/proxy:input_journal_trace_test`: replay of a
  pump-keyed entry (`"p":1` at frame 5) on the synthetic image; real input
  is dropped (`drop`), `0xa000` (with `MSG.time`) and `MM_MCINOTIFY` are
  traced as `msg`, the journal key as `inject` at `p` 1; the three frame
  records' counts, a `PM_NOREMOVE` call, a second thread's call counted in
  the end record, the call-site rule (`E8` before the return address
  inside the image, else 0), exactly nine lines.
- `input_journal_trace_off_test`: tick log configured, key absent: no
  file, no state. `input_journal_trace_exists_test`: an existing trace
  file is kept byte for byte and the trace stays off.
- `//tools/reference-game/proxy:oracle_speed_scripts_test` (the
  `configure-tick-log.ps1 -Apply` cases on a synthetic clone): modes
  `trace` and `trace-env` (the key and the manifest path), `trace-exists`
  (refused before the deployment); every other mode checks that neither
  the key nor the manifest field appears without the switch.

- Mutations: the pump
  ordinal `mt_pumps++` to `++mt_pumps` fails `input_journal_trace_test`;
  the script never writing the key fails `oracle_speed_scripts_test`.

## The SOS voice notices in es2sim's pump

Each `0xa000` message retrieved inside a dialog costs the original one
modal pass and one present ([the message trace](#result-of-the-frame-600-runs)). Where those
messages come from, what es2sim does with them, and why the headless
replay does not post them.

### Where the `0xa000` messages come from (static, SOS9503.DLL)

They are not timer ticks. They are the voice notices of SOS9503.DLL's
digital driver. Source: `llvm-objdump -d -M intel` of `VER95\SOS9503.DLL`
([audio.md](audio.md#the-mixing-device)), image base `0x400000`. All facts
below are static readings of that listing.

- `sosDIGIStartSample` installs three default callbacks in the voice
  record when the sample's fields are 0. DBSIM passes 0 ([audio.md](audio.md#what-dbsim-hands-the-driver)).
  - `0x405f7e`: +60h = `0x404140`, the end callback.
  - `0x405fbb`: +64h = `0x4041ae`, the remove callback.
  - `0x405ff8`: +68h = `0x40421c`, flag 1000h. The listing has no `call [reg + 0x68]`
    (the calls of +60h and +64h are `0x406dd9`, `0x403101`, `0x407164`
    and `0x40719d`), so it is taken as never called.
- How a callback posts (`0x404146..0x4041a3`):
  - It reads the voice's driver at +74h and the window table entry
    `[0x41809e + driver * 0xa58]`.
  - If the entry is 0, it posts nothing.
  - Otherwise it calls `PostMessageA(entry, 0xa000, 0xa003, handle << 16 | flag)`.
    The handle is the word at +2Ch. The flag is 4000h for the end callback
    and 2000h for the remove callback.
- `sosDIGISetActiveWindow` `0x404050..0x404134` is the only writer of the
  table.
  - Driver word >= 1: it answers `driver | 180000h`.
  - Driver word `[0x41766c + driver * 0xa58]` without bit 1000h (driver
    not initialised): it answers `driver | 50000h`.
  - Window 0: it stores 0 (`0x4040db`).
  - Any other window: it calls IsWindow (`[0x41f10c]`, `0x4040ed`). If
    IsWindow returns 0, it answers `window | 40000h`. Otherwise it stores
    the window (`0x404122`).
  - It answers 0 whenever it stores.
  - DBSIM calls it after the driver init, with the init's window, and with
    0 before the uninit (audio.c `driver_init`, `driver_shutdown`).
- When the end callback runs: in the mixer, when a voice with no loops
  left reaches the end of its data (`0x406d9f..0x406df5`). The voice gets
  +24h |= 4000h, and the callback is called at mix time.
- When the remove callback runs: at WOM_DONE (`0x402fa1..0x403123`).
  - +996h is incremented.
  - Every listed voice whose +70h equals +996h gets +24h = 2000h and is
    unlinked.
  - The callback is called at `0x403101`.
- StopSample and StopAll only set 4000h and post nothing. The removal
  posts 2000h later.
- DBSIM's window procedure ignores both messages ([audio.md](audio.md#window-messages)). Their
  only effect is the peek they cost.
- Trace check: in the message traces the handles post in pairs, 4000h first.
  The 2000h follows about 234 ms later (`c1_01` frame 500: 344 ms after
  the key, then 234 ms). Five buffers of 1024 frames at 22050 Hz are
  232 ms. This fits the model: 4000h at the mix of the buffer that holds
  the end, which happens one ring (5 buffers) before that buffer comes
  back; 2000h when it comes back. Consistent, not proven.

### What es2sim does

- `sim/src/audio.c`, the SOS mixer:
  - `ES2_SOS_SET_ACTIVE_WINDOW` gives the DLL's answers and stores the
    window in `mixer.window`. IsWindow is the optional `is_window` hook;
    NULL means every handle is a window.
  - The end point in `es2_sim_sos_mix` posts flag 4000h through
    `mixer.post`. The removal in `sos_buffer_done` posts flag 2000h. Both
    post only while the window is nonzero. A refused post is counted in
    `posts_lost`.
- `sim/src/loop.c`: `es2_sim_loop_post` appends to the pump's `posted`
  queue (depth 10000, [the posted queue's depth](#the-posted-queues-depth); full answers
  1). Posted messages are retrieved after the journal's messages and
  before the window's queue, so before input, as with PeekMessage.
- `sim/src/main.c` (two lines, windowed only): after
  `es2_sim_audio_window_attach`, the window mixer's `post` is
  `es2_sim_loop_post` with the loop.
- The windowed es2sim therefore posts the notices on its device's wall
  time, as the original does.
- Headless: the silent device has no buffers and no mix, so it posts
  nothing.

Deviations (windowed):

- The window handle is not passed on. The pump dispatches with the main
  window's handle, and DBSIM gives the driver its main window.
- IsWindow is not asked.
- A notice posted while the window layer's peek pumps the mixer is
  retrieved at the next peek, not at that one.
- The queue's depth is the platform's default limit; messages the system
  posts to the thread do not count against it ([the posted queue's
  depth](#the-posted-queues-depth)).

### Why the headless replay does not post them

Each 0xa000 retrieved inside a dialog removes one present, so es2sim's
headless excess in a dialog frame equals the number of 0xa000 the
original retrieves there. Records per frame from
`//sim:frames_<suite>_test`, against the manifests
the DBSIM frame captures and the message-trace runs
a/b/c:

| Frame | es2sim | Original runs | `0xa000` in the frame (runs a/b/c) |
|---|---|---|---|
| `c3_10` 204 | 472 | 460..461 | 11 / 12 / 11 |
| `c3_10` 600 | 455 | 433..437 | 19 / 18 / 20 |
| `c1_01` 300 | 302 | 302 | 0 / 0 / 0 |
| `c1_01` 500 | 300 | 298 | 2 / 2 / 2 |

So es2sim's excess is the missing notices: 11..12, 18..22, 0 and 2.

The posts follow the device's wall time. On the frame clock they cannot
be placed:

- Wall time per step, from the `MSG.time` of the message traces:

  | Run | ms per frame outside dialogs | ms per modal pass |
  |---|---|---|
  | `c3_10` a / b / c | 4.39 / 4.38 / 4.21 | 3.55 / 3.48 / 3.37 |
  | `c1_01` a / b / c | 4.39 / 4.28 / 4.27 | 3.91 / 3.80 / 3.70 |

  Inside a dialog the virtual clock reads only the intra-frame steps (at
  most step - 1 ms; timing.md), so a device on the virtual clock does not
  advance there. The original's device advances about 3.5 ms per pass.
- The positions move between runs of the original. `c3_10` frame 204's
  first notice falls at passes 216 / 212 / 219. Frame 600's last falls at
  241 / 477 / 474, which is why the counts are 19 / 18 / 20. `c1_01`
  frame 500's pair falls at 98+155, 91+148 and 114+174.
- Verdict: no posting on the virtual clock makes these frames exact. The
  original does not repeat itself either; its runs differ by up to four
  records at `c3_10` frame 600. A headless model would need a device clock
  that stands in for wall time. That means fixed costs per frame and per
  pass fitted to the table above, which is a calibrated guess, not the
  original's behaviour. It would also post in every replay; `c5_05` has
  1242 notices. It was therefore not built.

- `frames_known.py`'s `c3_10` row (`ORIGINAL_VARIES`, frames 204 and 600
  excluded) stays as it is.
- Headless dialog frames inside the spread would need that calibrated
  clock: a deviation that would change the pump counts in every replay.

### Tests

- `//sim:sos_mixer_test`:
  - SetActiveWindow's answers (180001h, window | 40000h, stored, 0,
    50000h);
  - the end and removal notices of a one-buffer ring (`0x14000`, then
    `0x12000`);
  - a refused post counted;
  - window 0 posts nothing.
- `//sim:loop_test`: a platform post is dispatched before WM_QUIT, one
  peek each.
- Mutation witness: the removal posting 4000h instead of 2000h fails
  `//sim:sos_mixer_test`.

- `//sim:sos_mixer_differential_test`: the notices against SOS9503.DLL's
  in vitro, call by call. With the refill before the removal they are
  equal (14,023 notices over 400 seeds); +68h (`0x40421c`) is called 0
  times. With the removal first the same notices often come in another
  order (184 of 605 runs), and in some runs other ones
  ([audio.md "Tests of the mixer"](audio.md#tests-of-the-mixer)).

### Open

- Not observed live: the order of refill and removal, so the order of the
  notices when several voices end near one buffer
  ([audio.md](audio.md#forced-deviations-and-guards)). The notices are
  ignored by DBSIM; only their count per peek matters.

## The windowed notices measured, and the windowed replay

The instrument that counts the `0xa000` notices in `es2sim_window`, the
windowed replay of an input journal, and what both show against the
original.

### The instrument (`loop.c`, `es2/sim/loop.h`)

- `es2_sim_loop` has a note hook (`peek_note`, NULL by default). The pump's
  peek reports every result to it with its source: empty, journal, posted
  queue, window, WM_QUIT. With the hook NULL nothing runs.
- `es2_sim_message_trace` sits behind the hook. It records only; no message
  is changed, added or dropped.
  - The mixer's `post` goes through `es2_sim_message_trace_post`, which
    calls `es2_sim_loop_post` unchanged and counts each `0xa000` as posted
    or refused (the refused ones are the mixer's `posts_lost`). It also
    keeps the post's `GetTickCount` for the message's `t`, as the
    original's `MSG.time` is the post's time.
  - Per frame (frame counter `0x4d257b` as in the proxy's trace): pumps (peeks), empty,
    msgs, inject. A **modal pass** is every pump call of a frame after its
    first. Each pump call ends in exactly one empty peek or one WM_QUIT, so
    a frame's modal passes are those ends minus 1. A notice retrieved
    before the frame's first end counts as "first pass", any later one as
    "modal".
- **Switch for the trace file:** `[input] message_trace=1` (the proxy's
  key) or the environment variable `ES2SIM_MESSAGE_TRACE`, in both builds;
  off by default.
  - `1` writes `<tick log>.messages.ndjson`; any other value but `0` is the
    path.
  - The file is created new. An existing file, or no tick log, refuses the
    trace with one stderr line, and the run goes on without it.
- **Format:** the proxy's records ([format](#format)) with CRLF, so the same scripts read both. The
  start record carries `"source":"es2sim"`. `msg` and `inject` carry the
  fields `k f p m w l h t c r`, with `c`/`r` fixed to the live call site
  `0x464ca2`/`0x464ca7`; `inject` has `t` 0. `frame` records have `drop`
  and `noremove` 0, since the own pump has neither. The end record adds the
  counts (`notices_posted`, `notices_refused`, `notices_retrieved`,
  `retrieved_first_pass`, `retrieved_modal`, `modal_passes`,
  `frames_modal`).
- **es2sim_window's summary**: windowed, the instrument is always attached
  (counting only; the file needs the switch). The summary gets two lines:
  - `es2sim: notices: P posted (L lost), R retrieved: F in a frame's first
    pump pass, M in modal passes (N modal passes in K frames); at most X in
    frame Y; Q still queued`;
  - `es2sim: sos:`, the mixer's SOS call counts and its end state (voices,
    listed, written, done, outstanding, the sink's queued bytes, block,
    sink error). This line shows a stalled ring ([trap](#trap-the-sos-ring-on-the-sdl-sink)).
- Headless without the switch the instrument is not attached. Both
  builds' output, tick logs and replays are unchanged.

### The windowed replay (`main.c`)

`es2sim_window` with `[input] mode=replay` replays the journal.

- The journal is the message source in front of the posted queue, as in
  the headless pump (`peek` order: journal, posted, window, WM_QUIT).
- The window stays the presenter. Its own events are drained and dropped
  (no input, focus, size, close or paint message reaches the game); the
  count goes into `es2sim: replay: N window events dropped`.
- The input answers (`GetAsyncKeyState`, cursor position) stay the
  journal's. The cursor rows answer as headless (`es2_sim_cursor_bind(NULL)`,
  the hidden window of the goldens).
- Virtual time is the goldens': no `clock` (wall-time catch-up), no `pace`,
  the loop's own DefWindowProcA.
- The mixer runs on the SDL device's wall time and posts its notices into
  the posted queue ([what es2sim does](#what-es2sim-does)).
- These are properties of a replay, not deviations from the original's
  behaviour: a replay reproduces the recording's input and timing, and only
  the audio device runs on its own time.

**Acceptance.** `replay_suite.py --window --message-trace` on SDL's
dummy video and audio drivers (no visible window, no sound device; the
dummy device takes the buffers in wall time): `acceptance-60s`,
`c1_01-success` (6198 ticks, exit code compared), `c1_01-options-dialog`
and `c3_10-detail-options` are IDENTICAL. The notice counts differ from
run to run (they follow the device's wall time).

- The notices change no tick: DBSIM's window procedure ignores them. The
  extra modal passes consume no journal entry, because a dialog frame's
  pumps are fixed by the journal's ordinals.
- So the tick log needs neither a visible window nor a real device.
- `//sim:replay_window_acceptance-60s_test` and
  `//sim:replay_window_c1_01-success_test` (manual) run the first two.

### Notices on the real devices

`c1_01-options-dialog` and `c3_10-detail-options` with `--window
--window-devices --message-trace`, three runs each: all
IDENTICAL (700 ticks), the notice counts, frames and pumps the same in all
three runs, no buffer stuck (`0 still queued`), 0 lost. The original's
traces are the message-trace runs a/b/c.

| Suite, frame | Original a/b/c `0xa000` | es2sim runs 1–3 `0xa000` | pumps original / es2sim |
|---|---|---|---|
| c3_10-detail-options, total | 84 / 84 / 84 | 132 / 132 / 132 | — |
| c3_10, frame 204 | 11 / 12 / 11 | 14 | 503 / 503 |
| c3_10, frame 600 | 19 / 18 / 20 | 1 | 483 / 483 |
| c1_01-options-dialog, total | 10 / 10 / 10 | 10 / 10 / 10 | — |
| c1_01, frame 300 | 0 | 3 | 303 / 303 |
| c1_01, frame 500 | 2 | 0 | 303 / 303 |

- The pumps of every dialog frame agree. The structure (journal ordinals,
  one empty peek per pump call) is the original's.
- c1_01 has the original's total; c3_10 has 48 more notices. In both
  suites the notices come one dialog frame earlier than in the original.
  Both follow the pacing ([below](#the-notices-against-the-paced-original)).
- The original's traces run 837..957 frames against es2sim's 700; the original
  posts 0..3 notices after frame 699.
- Frame 0: the original also retrieves message `0x31f` once. That is
  WM_DWMNCRENDERINGCHANGED, which Windows sends to a real top-level window.
  es2sim has no Win32 window, so it has no such message. This is a
  platform message, not game input; that the game's handlers ignore it is
  an inference from the handler list and was not checked again.

### Trap: the SOS ring on the SDL sink

A buffer counted as played only when the SDL stream's queued bytes fall
below its share, rounded up (`(q + block - 1) / block`), never comes back
when SDL keeps a few bytes of the last put (14 bytes; that they are SDL's
resampler tail is an inference). Once the ring has thinned to one buffer
(refills happen only on a return, and a return with no voice in the mix
writes nothing), `sos_start` does not prime (it primes only with nothing
outstanding), the ring stays dry, started voices are never mixed, never
end and post nothing, and after 32 starts every voice is taken. The
`es2sim: sos:` summary line shows it: one buffer outstanding, 14 bytes
queued, 32 voices listed. The real devices showed the stall as the dummy
driver did. The
original's `waveOut` returns every buffer (WOM_DONE), so its ring never
sticks; the own sink's rule is in [audio.md](audio.md#the-ring-runs-dry-as-waveouts-does).

### Tests

- `//sim:loop_test` `message_trace()`, on synthetic peeks:
  - not attached: no note;
  - attached: the journal's inject (`t` 0), a posted notice with its post
    time, a non-notice post not counted, the first-pass versus modal
    split, the proxy's frame records, a frame's record written only when the
    frame ends;
  - a full queue's refusal counted;
  - the end record with the counts; close once; nothing after close.
- `//sim:replay_suite_test` (19 tests):
  - `--window` drops `--headless` and sets the dummy drivers and
    `ES2SIM_MESSAGE_BOX=none`; `--window-devices` keeps SDL's drivers;
  - `--message-trace` sets the variable and on its own keeps `--headless`;
  - the notice line lands in `result.json`;
  - `--window-devices` without `--window` is refused.
  The stub sim records its environment for this.
- Mutation witness: `if (pass) ++t->retrieved_modal` changed to
  `if (pass > 1u)` fails `//sim:loop_test`.

### The notices against the paced original

Does the original, paced like es2sim, post es2sim's 132 notices in c3_10,
and where do they fall?

**Runs.**
- The original, paced: `run-oracle-regression.ps1 -InstallName
  timing-r111-p1 -Suite c3_10-detail-options,c1_01-options-dialog
  -VirtualTimePace 1` with `ES2_MESSAGE_TRACE=1`. The
  proxy holds each 47 ms step of virtual time in wall time; the window is
  hidden (check mode). Both suites ended IDENTICAL to their goldens (700
  ticks).
- es2sim: `replay_suite.py --window --message-trace` (SDL's dummy
  drivers), both IDENTICAL. Its notice figures equal the real devices'
  in every frame.
- The unpaced oracle replay runs at a median of 3.97 ms per tick in c3_10
  ([own_process.md](../reference-game/own_process.md#a-time-per-dbsim-capture-record)).

**Notices** (`0xa000`; frames as the trace counts them):

| c3_10-detail-options | total | < 204 | 204 | 205..599 | 600 | 601..699 |
|---|---|---|---|---|---|---|
| original, unpaced (run a) | 84 | 2 | 11 | 42 | 19 | 10 |
| original, paced | 132 | 12 | 2 | 114 | 0 | 4 |
| es2sim (dummy and live) | 132 | 5 | 14 | 109 | 1 | 3 |

c1_01-options-dialog: total 10 on all three; frame 300 has 0 / 0 / 3 and
frame 500 has 2 / 0 / 0 (unpaced original / paced original / es2sim).

**Wall time per step** (from `MSG.time`; figures only, scripts not
kept):

| | ms per frame (frames 60..200) | ms per modal pass (dialog frames) |
|---|---|---|
| original, unpaced (runs a/b/c) | 4.1 | 3.4..3.9 |
| original, paced | 46.9 | 1.1..1.3 |
| es2sim windowed replay | 24.4 | 11.6..12.6 |

**Reading.**
- The count is the pacing. Paced, the original posts 132 notices in
  c3_10, es2sim's number; the 84 of the unpaced runs come from its 4 ms
  frames, in which most voices have not ended by tick 700.
- The placement is the time per modal pass. es2sim spends about 12 ms of
  wall time per pass with the dummy drivers as with the real ones, so the
  present is not the cost: it is es2sim's own busy time per pass (its
  windowed median is 13.0 ms, [own_process.md](../reference-game/own_process.md#es2sims-frame-times)).
  The paced original spends about 1.2 ms. A dialog frame of 503
  passes therefore lasts about 6 s in es2sim and under 0.4 s in the paced
  original, so voices started before the dialog end inside it in es2sim
  (14 in frame 204) and after it in the original (2). Frames outside the
  dialogs run faster in es2sim (24 ms; a replay is unpaced) than in the
  paced original (47 ms), which moves the rest.
- Nothing points at the SOS sink or the notice path: the voice set is the
  same (132), and the notices follow each side's wall clock as SOS's
  device does.
- The placement follows execution speed, which the original's own runs do
  not repeat either. es2sim's time per pass is a performance topic, not a
  behaviour contract. `frames_known.py`'s c3_10 row (`ORIGINAL_VARIES`)
  stays.

**Why no pacing model removes the earlier placement.** The difference
stays. A pacing model can only hold wall time back; it cannot make a modal
pass cost less. es2sim's 11.6..12.6 ms per pass is its own busy time (the
dispatch and its traps, [own_process.md](../reference-game/own_process.md#es2sims-frame-times)),
against 1.1..1.3 ms in the paced original, so its dialog frames already
last about ten times longer in wall time, and pacing would lengthen them
further. The only other lever is the clock the notices follow. The mixer
posts when the SDL device has taken a buffer, the sound card's time, as SOS
posts on waveOut's returns. Notices on a clock that advances about 1.2 ms
per modal pass would either come apart from the buffers the device plays (a
removal notice for a voice still audible) or need a slower device (pitch and
timing of every sound). Both change the audible output to move a placement
that the original's own runs do not repeat (frame 204: 11 / 12 / 11 notices
in runs a/b/c). Virtual time and the 40 ms limiter (`0x4678c0`) are not
involved: the notices change no tick, and the count (132) already matches
the paced original. Only an es2sim modal pass of about 1.2 ms busy time
would remove it: a performance target, not a behaviour change.

- Tooling: `run-oracle-regression.ps1 -VirtualTimePace 1` runs the check
  suites paced ([oracle_regression.md](../reference-game/oracle_regression.md));
  recording stays unpaced.

### Limits

- The `0x31f` message of frame 0 is a platform difference with no game
  effect expected; it is not modelled.

## The pump accounting

How the original's pump count depends on messages no journal holds, what of
it a replay reproduces, and the bound on the rest. Tool:
[message_trace.py](../../tools/reference-game/scripts/message_trace.py)
(`summary`, `compare`), on the traces of [the message
trace](#the-message-trace-of-the-replay-pump) (the proxy's and es2sim's
format).

### The model

A message the pump retrieves is either the journal's (an `inject`, or a
message the game derives from one, such as `WM_CHAR` from a key-down) or
**unkeyed**: the SOS voice notice `0xa000`, `MM_MCINOTIFY` `0x3b9`, and the
platform message `0x31f` (WM_DWMNCRENDERINGCHANGED, only a real top-level
window gets it). The unkeyed ones arrive in the run's wall time. Per frame:

- The injections (frame, `p`, message, wParam, lParam) are the journal's: an
  entry with pump key N is injected at ordinal N whatever else the frame
  retrieves ([format](#format)).
- The other messages of the frame (as a multiset, without `p`) are the
  journal's.
- An unkeyed message retrieved **before** the frame's last injection takes an
  ordinal that an empty call would have taken: one modal pass fewer before the
  answer, one empty call fewer, one dialog present fewer (presents = empty + 2,
  [result of the frame-600 runs](#result-of-the-frame-600-runs)). The frame's
  pump calls stay the same.
- An unkeyed message retrieved **after** the last injection (or in a frame
  without one) adds one call to that pump call.
- So `pumps - unkeyed after the last injection` (the fixed count) is the
  journal's, and the difference of two runs' empty calls is the difference of
  their unkeyed retrievals before the last injection.

`compare A B` checks the three journal-fixed quantities in every frame up to
the last frame both traces contain and reports the unkeyed part per frame as
bounded (counts, passes and empty calls A − B, the shift of each answer). Any
difference the model does not explain (an injection, another message, a fixed
count, an empty-call difference not matching the passes, a missing frame, a
structurally broken trace) fails. With `--tick-log-a`/`-b` it also checks the
intra-frame clock of every frame with an answer inside a modal screen (an
injection with at least one finished pass before it): `vt_intra_advances`
must grow by the cap (step − 1 = 46) in that frame's tick. A frame with no
tick after it (the end dialog before the exit) is listed with no value.

### Results

All traces are of the hidden-window original or of es2sim; the original's
runs and es2sim's replays were IDENTICAL to their goldens. The headless
es2sim traces come from `replay_suite.py
--message-trace --keep --output-dir DIR --suite S` (headless by default),
each IDENTICAL to its golden.

| A against B | c3_10-detail-options | c1_01-options-dialog |
|---|---|---|
| original run a / original paced | accounted; 124 frames bounded through 804; `0xa000` 84 / 132 | accounted; 19 frames through 811; 10 / 10 |
| original run a / es2sim windowed (dummy drivers and real devices alike) | accounted; 104 frames through 699; 84 / 132 | accounted; 17 frames; 10 / 10 |
| original run a / es2sim headless | accounted; 32 frames through 699; 84 / 0 | accounted; 9 frames; 10 / 0 |

`0x31f` once at frame 0 in every original trace, never in es2sim's; no
`MM_MCINOTIFY` in any trace. The answers inside modal screens (fewest passes
before an answer in the frame / unkeyed retrievals before its last answer /
clock advances of the frame):

| Frame | original run a | original paced | es2sim headless |
|---|---|---|---|
| c3_10 204 | 293 / 11 / 46 | 298 / 2 / 46 | 299 / 0 / 46 |
| c3_10 600 | 280 / 19 / 46 | 299 / 0 / 46 | 299 / 0 / 46 |
| c1_01 300 | 299 / 0 / 46 | 299 / 0 / 46 | 299 / 0 / 46 |
| c1_01 500 | 286 / 2 / 46 | 288 / 0 / 46 | 288 / 0 / 46 |

es2sim headless `c5_05-moon-missiles` (8262 ticks, IDENTICAL): six frames
with answers inside modal screens (5985, 8122, 8141, 8219, 8242, 8262), fewest
passes 298..300, no unkeyed message; the clock reaches 46 in the five frames
with a tick after them.

Seven further pump-keyed suites, the hidden-window original on
`timing-r111-p1` against es2sim headless (all runs identical to their
goldens):

| Suite | Through frame | Bounded frames | Unexplained |
|---|---:|---:|---:|
| c1_01-cheat-destroy | 430 | 4 | 0 |
| c1_01-mouse | 599 | 4 | 2 |
| c1_02-probe | 1500 | 5 | 0 |
| c3_09-loadout-b | 8886 | 194 | 0 |
| c3_10-loadout-hercs | 936 | 12 | 0 |
| c4_08-razor-plasma | 2094 | 33 | 0 |
| c5_05-moon-missiles | 8262 | 184 | 0 |

The two c1_01-mouse frames are not unkeyed messages but the character
messages of the dead key `^`, a known difference (next section).

### The dead key as a known difference

`c1_01-mouse` presses `^` (VK 0xdc, scan 0x29) at frames 250 and 305, Down
at 290, Up at 340 and Q at 450 (then Enter, pump key 2000). What the two
traces retrieve after each key-down:

| Key-down | original | es2sim headless |
|---|---|---|
| 250 `^` | `WM_CHAR` 94 lParam `0x02290001`, `WM_CHAR` 94 `0x00290001` | `WM_DEADCHAR` 94 `0x00290001` |
| 305 `^` | `WM_DEADCHAR` 94 `0x00290001` | `WM_DEADCHAR` 94 `0x00290001` |
| 450 Q | `WM_CHAR` 94 `0x02100001`, `WM_CHAR` 113 `0x00100001` | `WM_CHAR` 94 `0x00100001`, `WM_CHAR` 113 `0x00100001` |

So `compare` finds frame 250 (fixed pump count 4 against 3, other
messages) and frame 450 (one lParam).

- **Frame 250: the host's pending dead key.** Windows keeps the dead-key
  state of `TranslateMessage` and `ToAscii` per session, not per process: a
  probe that translates one `^` and exits leaves it pending, and the next
  process's first `^` then gives two `WM_CHAR` '^' (lParam `0x02290001`,
  `0x00290001`), its second `^` a `WM_DEADCHAR` and Q '^', 'q' (`0x02100001`,
  `0x00100001`): exactly the original's trace. From no pending dead key the
  same key-downs give `WM_DEADCHAR`, then '^', '^', then 'q' alone. The
  original's run therefore started with a dead `^` left pending by some
  other process of the session. es2sim's trace (dead, dead, '^' 'q') fits
  neither start: its `ToAscii` shares the same session state, which some
  other process cleared between frames 250 and 305. DBSIM imports no
  translation besides `TranslateMessage`, and the proxy calls none.
- **Frame 450: lParam bit 25.** Of two characters, `TranslateMessage` posts
  the first (the accent that did not combine) with bit 25 (`0x02000000`,
  documented as reserved) set; es2sim's model posts both with the
  key-down's lParam ([the characters of
  `TranslateMessage`](#the-characters-of-translatemessage)).
- **Why it stays.** The character messages depend on state outside the
  process and the journal (what other processes of the session typed or
  translated), so neither the original's run nor es2sim's is reproducible
  in them; no handler and no window procedure case reads them, and both
  replays are IDENTICAL to the golden. The difference touches only the
  pump count of a frame with a dead key and the lParam of a character
  nothing reads.

The host probes (layout 04070407, Windows 10) called `TranslateMessage` on a
message-only STATIC window, one process per probed key, and `ToAscii`.

### The bound

- The unkeyed part is wall-time dependent: the original's own runs differ in
  it (11 / 12 / 11 and 19 / 18 / 20 notices in the two dialog frames of runs
  a/b/c), and paced the original posts 132 notices instead of 84
  ([the notices against the paced original](#the-notices-against-the-paced-original)).
  Headless es2sim has no device time and posts none
  ([why the headless replay does not post them](#why-the-headless-replay-does-not-post-them)).
  It is therefore bounded, not modelled.
- What it moves: the pass at which an answer arrives (by the unkeyed count
  before it) and the dialog's present count. What stays: every journal-fixed
  quantity above, measured equal in all six comparisons.
- Why the tick log does not see the shift: the dialogs reach the
  intra-frame clock cap in every answered frame of every run (46), so the
  clock an answer meets is the same; and the original's runs with different
  unkeyed counts are all IDENTICAL to the same golden. Pump keys start at 300
  ([input_journal.md](../reference-game/input_journal.md)), so the first answer
  of a frame comes after 299 − (unkeyed before it) passes. Assuming at least
  one clock read per pass, the clock is saturated before the first answer as
  long as fewer than 253 unkeyed messages arrive before it; the most measured
  is 19.
- `MM_MCINOTIFY`: bound 0 in the replays (next section).
- Not covered: a modal screen whose passes have compared side effects other
  than the clock (none is known among the suites' screens); the other
  pump-keyed suites (`c1_01-cheat-destroy`, `c1_01-mouse`, `c1_02-probe`,
  `c3_09-loadout-b`, `c3_10-loadout-hercs`, `c4_08-razor-plasma`,
  `c5_05-moon-missiles`) have no archived original trace yet.

### MM_MCINOTIFY in the replays (listing and live counts)

The CD start `0x473c48` (listing `0x473c48..0x473d43`): `MCI_OPEN` of
`cdaudio`; on success the device ID goes to `[0x4a0e2c]` and `MCI_SET` (TMSF)
follows; a failing SET calls the stop `0x473c00` (`0x473ca3`) without the
track query; then the track length `0x473d84` (its only caller `0x473cae`);
a length of 0 calls `0x473c00` (`0x473cbd`) and plays nothing; else
`MCI_PLAY` with NOTIFY, and a failing play calls `0x473c00` (`0x473d31`).
`0x473c00` stops and closes the device when `[0x4a0e2c]` ≥ 0 and sets it to
−1. Its other callers are the stop row `0x464874` (`0x464880`) and the
position play `0x473dcc` (`0x473e27`, `0x473e96`), whose only caller is
`0x46499c` (`0x4649ac`). `0x46499c` is called from the focus resume
`0x463238` (`0x46329f`) and from the sound start `0x462e58` (`0x462e7d`),
which the prefs callback of byte `+0x00` (`0x459cb4`, [cfg.md](../formats/cfg.md))
calls at `0x459ccf` when the switch is turned on; turned off, it calls the
sound stop `0x462d78` (`0x459cd7`), one of the callers of `0x464874`
(`0x462da5`; the others `0x45f4cf` in the loop tail, `0x4623ed` in the notify
handler, `0x46274f`, `0x463172`, `0x4631c7`).

Live map (40 suites): `train1-timers` makes no MCI
call (TRAIN1 starts no CD track; every count below is 0 there). In the other
39: `0x473c48` and `0x473d84` 1; `0x464874` 0 in 25 suites and 1 in 14
(the 14 include the suites that end a mission; which caller ran is not
recorded); `0x473c00` equals `0x464874` + 1 (1 or 2); `0x464858` (the CD
start row, also the replay call of the notify handler at `0x462401`) 1. With the position play idle in a replay (the replay
subclass keeps the focus messages from the game; whether a suite turns the
music switch on is not checked; `0x473dcc` 0 in the six suites of `d-21-live`,
[timing.md](timing.md#time-dependence-outside-the-virtual-clock)),
the one extra `0x473c00` is the CD start's own failure call. So the start
failed after the open in every suite (no disc on the oracle machine is
consistent with it, not checked), the device was closed again and no play
with NOTIFY was pending: no `MM_MCINOTIFY` can come, and the handler's replay
branch `0x4623ec` never ran (it would call `0x464874` and `0x464858` once
more). The traces agree (none in any run). es2sim headless answers
`MCI_OPEN` with 306 (no device): the start fails one step earlier, without
the `0x473c00` call, and the end state (`[0x4a0e2c]` = −1, no device) is the
same.

### Tests

- `//tools/reference-game/scripts:message_trace_unit_test` (13 tests,
  synthetic traces and tick logs): the counts, passes and clock of a dialog
  frame; a frame below the cap and one without a tick; structural problems;
  `MM_MCINOTIFY` and `0x31f` unkeyed; notices before and after the last answer
  bounded with the predicted pass and empty-call differences; notices
  outside dialogs; another message, a moved answer, an extra modal pass, a
  clock below the cap and a missing frame unexplained; the shorter trace's
  end; exit codes 0/1/2.
- Mutation witness: the fixed count taken as `pumps` (without subtracting the
  unkeyed retrievals after the last answer) fails two of them.

## The posted queue's depth

es2sim's own posted queue against the original's Windows message queue:
the platform's limit is the contract. A shallow queue loses `0xa000`
notices: a 16-entry queue lost 10 and 2 in windowed `c1_01-success` runs
(at most 16 queued in a frame; one of them in [the windowed start-up with
`--image-access`](../reference-game/own_process.md#the-windowed-start-up-with---image-access)).

### Contract: the per-queue limit of posted messages

What the original's posting goes through. Every message the game's process
posts to its main thread (the characters of `TranslateMessage`, the SOS
driver's `PostMessageA` voice notices `0xa000`, anything else) sits
in the one posted-message queue of that thread until the pump removes it;
`PostQuitMessage` is a flag, not a queued message. Messages leave in the
order they were posted.

- **Documented behaviour** (Win32 documentation of `PostMessage`, from
  memory, not re-fetched here). A queue holds at most 10000 posted messages
  by default; the limit is the registry value `USERPostMessageLimit` (a
  DWORD under `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Windows`,
  minimum 4000). A post beyond the limit fails: `PostMessageA` and
  `PostThreadMessageA` return FALSE and `GetLastError` is
  `ERROR_NOT_ENOUGH_QUOTA` (1816). The probe only reads the value and
  never changes it.
- **Measured** (`//sim:post_limit_probe_test`, `sim/tests/post_limit_probe.c`,
  a synthetic program of ours: no game file, no visible window, no sound
  device, no child process). It creates its thread's queue with one
  `PeekMessageA`, posts numbered messages to its own thread until a post
  fails, reads the queue back with `PeekMessageA(PM_REMOVE)`, removes one
  message and posts once more. On the build machine: Windows 10 Pro,
  10.0 build 19045, update revision 7807 (`CurrentVersion\UBR`),
  `USERPostMessageLimit` present with the value 10000.

  | Post call | Accepted before the first failure | Error | Read back | In post order | A post after removing one |
  |---|---|---|---|---|---|
  | `PostThreadMessageA` (own thread) | 10000 | 1816 | 10001 | 10001 | accepted |
  | `PostMessageA` (NULL window = own thread) | 10000 | 1816 | 10001 | 10001 | accepted |
  | `PostMessageA` (message-only window of the thread) | 10000 | 1816 | 10001 | 10001 | accepted |

  (10001 read back: the 10000 accepted plus the one reposted after the
  removal.) The limit here is exactly the documented 10000, the same for the
  three ways of posting; the queue is FIFO and the quota returns with every
  removed message. The test checks these properties (and compares the count
  with the registry value when present, else with 10000) and prints the
  numbers with `--test_output=all`. It is `no-cache`: the answer is the
  machine's.
- **Inferred, not measured.** That the original's queue ever holds more
  than a few dozen messages is not expected (the proxy's traces have at most 20
  notices in a frame, `c3_10` frame 600); the depth is a limit, not a typical fill. A
  real queue also counts messages the system posts to the thread against the
  same quota; es2sim has none of them and ignores that. A machine with
  another `USERPostMessageLimit` has another limit; es2sim takes the
  documented default, which is also this machine's value.

### The change (`loop.h`, `loop.c`)

- `ES2_SIM_POSTED_DEPTH` is 10000. `posted[]` is a ring of that many
  entries (`posted_head` the oldest, `posted_count` in use). The ring takes
  120000 bytes of the loop record; every instance is static.

- The order is unchanged: the pump's peek takes the journal first, then the
  posted queue's oldest entry (FIFO), then the window's queue, then WM_QUIT.
  `TranslateMessage`'s characters queue behind notices already posted.
- A post into a full ring still returns 1 and drops the message
  (`es2_sim_loop_post`), as a failed `PostMessageA` does; the mixer counts it
  in `posts_lost`, the instrument as `notices_refused`. Nothing is
  overwritten.

- The instrument keeps one post time per queued notice
  (`ES2_SIM_TRACE_POST_TIMES`), as many as the queue holds: 10000 (fewer
  would misdate the notices retrieved beyond them).
- Headless the queue's order and contents are those of a shallow queue,
  and the headless replays post no notices.

### Acceptance

- `//sim:loop_test`: the depth's refusal (10000 accepted, the 10001st
  refused, count unchanged; `notices_posted` 10002 in the instrument's
  end record); `posted_ring()`: a burst of 100 retrieved in order with one
  peek each; the ring wrapping (head three before the end, eight posts);
  full at the depth with all 10000 retrieved in order and the queue
  accepting again; a key-down's WM_CHAR queued behind 20 notices; post
  times kept beyond 32 (the 31st to 40th notice carry their own post's
  time).
- Mutation: `post()`'s `posted_count >= ES2_SIM_POSTED_DEPTH` changed to `>`
  fails `//sim:loop_test` (the full-queue refusal).
- Passing with the ring: `//sim:replay_acceptance-60s_test`,
  `//sim:replay_c1_01-success_test`, `//sim:replay_c1_01-options-dialog_test`,
  `//sim:replay_c3_10-detail-options_test` (headless, IDENTICAL),
  `//sim:replay_suite_test`, `//sim:sos_mixer_test`, `//sim:audio_test`,
  `//sim:audio_window_test`, `//sim:audio_sdl_test`,
  `//sim:es2sim_window_test`, `//sim:platform_sdl_test`, `//sim:dialogs_test`,
  `//sim:abort_cleanup_test`, `//tools/reference-game/invitro:differential_loop_test`,
  `differential_audio_test`, `differential_dialogs_test`.

### Live

`c1_01-success` windowed on the real devices (`--window --window-devices
--message-trace`), three runs, every run IDENTICAL: 6198
ticks, 16305 watch changes, exit code 3 compared.

| Run | Posted | Lost | First pass / modal | At most |
|---|---:|---:|---|---|
| 1 | 1416 | 0 | 1410 / 6 | 6 in frame 3277 |
| 2 | 1409 | 0 | 1406 / 3 | 5 in frame 3055 |
| 3 | 1415 | 0 | 1411 / 4 | 5 in frame 2982 |

- 0 still queued in every run.
- Nothing is lost, and the most notices in one frame is 6.
- The posted totals vary by 7 between runs, because voice ends follow
  the device.

### Limits

- The per-queue limit is the documented default and this machine's value
  (10000); another `USERPostMessageLimit` gives another limit. es2sim keeps
  the default and reads no registry value.
