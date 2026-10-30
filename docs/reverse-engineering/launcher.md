# Launcher chain: ES.EXE, the own shells and DBSIM's process

The launcher `VER95/ES.EXE` (11,808 bytes, PE32 GUI, entry `0x401000`, not
large-address-aware, linker 2.25), the exit-code protocol with VSHELL and
DBSIM, the own launcher and shells that take their places, and DBSIM's
process shell from WinMain to the process exit with its own code in `sim/`.
Our launcher is [`launcher/`](../../launcher/README.md).

Sources: the `llvm-objdump -d -M intel` listings of the reference
`VER95/ES.EXE` and `VER95/DBSIM.EXE` (724,512 bytes, checked against
`data/reference_hashes.txt` before use), import thunks resolved through the
import tables, program literals read from the images. Addresses are VAs at
image base `0x00400000` of the executable the section names. **In vitro** = the
original's machine code and the own code gave the same results; **live** =
seen in a run of the game; **hypothesis** = a role or reading not checked.

## Overview

ES.EXE is a Borland C++ program whose WinMain is `0x401168` (`ret 0x10`). It
parses its command line, reads the language byte, loads `error.str`, checks
memory, and then runs a loop that starts VSHELL or DBSIM with Borland
`_spawnv(P_WAIT, ...)`. **The exit code of the child that just ended decides
the next child.** ES.EXE always returns 0.

## Command line

`0x4010d4` splits `lpCmdLine` at single spaces (code 0x20 only; no tabs, no
quotes), skipping repeated spaces, into an array whose element 0 is the
literal `dbsim` (`0x4010e9`). Elements 1.. are examined in order
(`0x4011ee..0x4012dd`). Only tokens starting with `-` count; the character
after the dash selects:

| Option | Effect | Evidence |
| --- | --- | --- |
| `-s…` | toggles flag S | `0x401234` |
| `-a…` | toggles flag A | `0x401255` |
| `-L…` | toggles flag L | `0x401246` |
| `-r…` | sets flag R and **appends the token without its dash** to a 20-byte buffer that starts as `-`; repeated `-r` tokens concatenate | `0x401264..0x401270` |
| `-SPRUNKNOWN` (exact) | toggles flag SPR | byte compare `0x40127a..0x40129d` |
| `-X…` | toggles flag FORCE, only if SPR is already set | `0x4012ae` |
| `-D…` | `atol` of the rest; the result is discarded | `0x4012c3..0x4012d0` |

Any other token is ignored. Option letters are case-sensitive. Two more flags
exist but are constant: the `-m` flag is always 0 and the `-Z` condition is
always false, so ES.EXE never passes `-m` or `-Z`.

## Language and strings

`data\language.cfg` is opened `"rb"`; one byte is read into the global at
`0x403075` (initially 0). Byte `E` is replaced by 0 (`0x401314..0x401320`).
A nonzero byte later produces the argument `-` followed by that byte (the
global at `0x403074` holds `-`), e.g. `-G` on the German reference. A
missing file leaves 0, so no language argument is passed.

`error.str` (current directory) is loaded by `0x401758`: a u32 body size,
then the body. Two string groups are taken from it (`0x4018ac`, which uses
the count stored in the file, not its third argument): group 0 into six
pointers at `0x4036ac`, group 1 into three at `0x4036c4`. The framing is the
packed STR grammar of [mission.md](../formats/mission.md#missionstr): i16
count, then per string i16 length, the bytes (including NUL) and one extra
byte. ES.EXE stores **every** string of a group, even beyond its array: the
English `ERROR.STR` in the CD root holds 6 and 3 strings, but the installed
German file (`GERMAN\ERROR.GER`, copied to `ERROR.STR` by the installer)
holds 12 and 3, so group 0 overruns into the group-1 array, which the second
load then rewrites. Only group 0 strings 0–2 and group 1 string 0 are used.
If the file cannot be loaded, ES.EXE shows its own fixed message box (caption
"Error", `MB_ICONEXCLAMATION`) and returns 0 without starting anything.

## Memory check

`0x401399..0x4013c0`: try `GlobalAlloc(GMEM_FIXED, n)` with n = 20,000,000,
lowering n by 200,000 until it succeeds; call `GlobalMemoryStatus`; free the
block. With T = `dwTotalPhys`, P = `dwTotalPageFile`, A = `dwAvailPageFile`:

1. T < 0x73a000 → message group 0 string 0;
2. else P = 0 → group 0 string 1;
3. else P < 0x7a1200 → group 0 string 2;
4. else limit = (T ≥ 0xdac000 ? 18,000,000 : 12,000,000); if limit is
   greater than A + n, compared **signed 32-bit** (`jle` at `0x401473`) →
   group 0 string 2.

Every message uses caption group 1 string 0 and `MB_ICONEXCLAMATION`, then
sets the state to 0, so nothing is started. `GlobalMemoryStatus` reports
values capped for a non-large-address-aware 32-bit process; our launcher
must have the same PE flag so that it sees the same numbers.

## Start loop

State S (global `0x403070`) starts at 1. While S ≠ 0 (`0x401666`): if FORCE
is set, S = 2 (`0x4014ae`); then a seven-entry jump table at `0x4014d3`
dispatches on S as an unsigned value:

| S | Action |
| --- | --- |
| 0, > 6 (incl. negative) | S = 0, leave the loop |
| 1, 3, 4, 6 | start VSHELL |
| 2 | start DBSIM |
| 5 | start DBSIM with `-D` |

S becomes the `_spawnv` result, i.e. the child's exit code, or -1 if the
start failed (which ends the loop).

Arguments, in order, each a separate argv element:

- **VSHELL** (`0x4014ef..0x40156b`): `dummy`, `-eggplant`, `-X`S, then `-s`
  if S-flag, `-a` if A, `-@` if SPR, the language argument if any.
- **DBSIM** (`0x401582..0x40164f`): `dummy`, `-eggplant`, `-X`S, `-R`N with a
  counter N that starts at 0 and increases after each DBSIM start, then `-s`
  if S-flag, `-D` if S = 5, `-l` if L, the `-r` buffer if R, `-SPRUNKNOWN` if
  SPR, the language argument if any.

Numbers are formatted by `itoa(…, 10)`.

**Live** (German installation, no options): the first child was `vshell.EXE`
in the installation directory with the command line `dummy -eggplant -X1 -G`
(CIM `Win32_Process`). Borland `_spawnv` therefore joins the argv elements
with single spaces without quotes and resolves `vshell` to `vshell.EXE` in
the current directory. The oracle tooling's DBSIM arguments
`-eggplant -X2 -R0 -G` ([timing.md](timing.md)) are exactly the first DBSIM
start of this contract.

**Trap:** to stop a running chain, end the launcher (ES.EXE or
`ES2LAUNCH.EXE`) first, then VSHELL/DBSIM. Ending only the child triggers the
next start: the Windows Task Manager ends a process with exit code 1, and
the loop answers VSHELL's 1 with a fresh VSHELL (seen live: four restarts).

## Other side of the protocol

**VSHELL (static):** WinMain (`ret 0x10` at `0x406bf8`) returns the
signed word at `0x46e348` (getter `0x408d28`, only setter `0x408d1a`). The
option parser stores `atoi` of the `-X` argument there (`0x401768..0x40177b`);
the start-up routine then copies it into the entry mode `0x482362` and resets
the exit code to 0 (`0x401bc1..0x401bce`). Later setters pass only 0
(`0x4079ea`, `0x42f358`), 2 (`0x4319b1`, `0x445c21`, `0x44d1f7`, `0x44dbdc`,
`0x44dce2`) and 5 (`0x431c24`). No path returns the `-X` value itself; an
assertion ends through `__exit(1)` at `0x44e5b2`. The flag `0x46c274` is set
on every entry path once the first screen is up; modes 0 and 1 run a
colour-depth check (`GetDeviceCaps(…, 12)` = 8 bits) with a message. VSHELL's
process side in full (WinMain, the option parser and what each ES.EXE
argument does, the start-up checks with their `error.str` messages, the
window, the main loop, every way the process ends and its exit code, the
Alt+Tab crash `0x453a68`): [vshell_process.md](vshell_process.md).

**Live:** VSHELL `-X1` returned **2** after the mission was chosen, and the
launcher started `dbsim.EXE dummy -eggplant -X2 -R0 -G`; DBSIM returned
**1** at the end of that mission (the clone still carried oracle settings,
so the mission played by itself) and the launcher started
`vshell.EXE dummy -eggplant -X1 -G`.

**DBSIM (static):** WinMain `0x46538c` (`ret 0x10` at `0x465489`)
returns the dword at `0x4d27b8` (BSS, initially 0) after the game main
`0x45f160`. Two option parsers run over the same argv:
[Option parsers](#option-parsers-0x45e6cc-and-0x45e758). There is **no case
for `X` or `@`** in the main parser, so `-@` from ES.EXE has no effect on
DBSIM, and the early parser's `-X` value is overwritten by the game main.

Exit code writes, all to `0x4d27b8`: the `-X` value (`0x45e744`); 0 at the
start of the game main (`0x45f26d`); the mission end's 6 / 0 / 4 / 3
([The mission end](#the-mission-end-0x461ff0-as-own-code), also
[mission.md](../formats/mission.md#dbsim-exit-states-are-distinct-from-objective-success));
0 again in the clean-up `0x46133c..0x461343` of the abort notifier
`0x461304`. The error report `0x479c78` (43 callers) shows an "Error"
message box for severities 4–6 and ends DBSIM with `PostQuitMessage(0)` and
**`__exit(1)`** (`0x479cd2..0x479cdb`).

Resulting chain meaning (the dispatch above): DBSIM 3 / 4 / 6 → VSHELL
`-X3` / `-X4` / `-X6` (VSHELL treats 3 and 4 alike; 6 skips the result
readback per mission.md); DBSIM 1 (fatal error) → VSHELL `-X1` (fresh
start); DBSIM 0 → ES.EXE quits. **Live:** a player-ended mission returned 3;
a scripted replay of the `c1_01-collision` input journal on a mission chosen
in VSHELL returned 1, the fatal-error path (**hypothesis:** the journal did
not match the mission; the message box was not recorded).

## ES.EXE behaviour → our launcher

| ES.EXE | `es2launch.exe` |
| --- | --- |
| Command-line options and their toggles | identical, portable parser (`launch_logic.c`) |
| `data\language.cfg` first byte, `E` → none | identical |
| `error.str` packed STR, groups 0/1 | identical parse with bounds checks; every string is framed, only the first slots are kept; a malformed file counts as missing |
| Missing `error.str` box | own wording (no original string is embedded), same caption style and icon, return 0 |
| Memory check | same API calls, constants, signed comparison and messages from `error.str`; not large-address-aware |
| Start loop, states, argv, `-R` counter | identical |
| `_spawnv` | `CreateProcessA("<name>.EXE", "<argv joined by spaces>")` in the current directory, inherited environment, wait, exit code; start failure = -1 |
| Exit code | 0 |
| Own additions | none on the command line; environment `ES2_LAUNCH_LOG` (log of every start/exit), `ES2_LAUNCH_SCRIPT` (dry run with scripted exit codes, starts nothing), `ES2_LAUNCH_NO_UI` (messages to the log instead of a box), the shell replacement ([Campaign chain](#campaign-chain-through-the-own-launcher)) and the DBSIM replacement ([The own DBSIM process](#the-own-dbsim-process-behind-the-launcher)) |

**Hypotheses:** Borland `_spawnv` may also try `.COM`/`.BAT` before `.EXE`
and may pass handle-inheritance details or a specific `STARTUPINFO`; we use
`.EXE` only, `bInheritHandles = FALSE` and a default `STARTUPINFO`.

**Live:** started through `start-own-launcher.ps1`, the first child was
`vshell.EXE` with the command line `dummy -eggplant -X1 -G`, parent
`ES2LAUNCH.EXE`; after VSHELL was terminated (exit code -1) the loop ended,
as in ES.EXE. The German 12-string group 0 is covered by the real-data test.
A live run goes VSHELL `-X1` → exit 2 (mission chosen) → DBSIM
`dummy -eggplant -X2 -R0 -G` → exit 3 (mission ended by the player) → VSHELL
`dummy -eggplant -X3 -G` → exit 0 (quit in the menu) → end of the loop,
launcher exit 0 (clone `calibration-r111`).

**Start** (`tools/reference-game/setup/start-own-launcher.ps1`, default clone
`calibration-r111`; `native-r111` is refused; clones with oracle settings
are refused unless `-AllowOracleConfig`): builds `es2launch.exe`, copies it
next to ES.EXE as `ES2LAUNCH.EXE`, sets `ES2_LAUNCH_LOG`, and starts it the
way `start-native.ps1` starts ES.EXE. It sets `__COMPAT_LAYER` (the
installation's `compatibility_layer`, `256COLOR`) for `ES2LAUNCH.EXE` and
its children.

## `-SPRUNKNOWN` debug/cheat mode

**ES.EXE:** toggles flag SPR; passes `-@` to VSHELL and `-SPRUNKNOWN` to
DBSIM, and only with SPR set does `-X` force the loop to start DBSIM again
and again without VSHELL ([Start loop](#start-loop)).

### DBSIM key codes and dispatch

`-SPRUNKNOWN` toggles the dword `0x49ef38`. The keyboard handler `0x477bec`
maps the Windows virtual key through the dword table `0x4a10ec` (index below
`0xdf`) to a code numbered like PC scan-code set 1 (S → `0x1f`, N → `0x31`,
Num+ → `0x4e`, Up/Down/Left/Right → `0x48`/`0x50`/`0x4b`/`0x4d`) and adds
`0x200` for Alt, `0x400` for Ctrl, `0x800` for Shift (`0x477c45..0x477c75`).
The per-frame input routine `0x460870` passes only the record code (word
`0x4d22c6+0`) to the main command handler `0x45feb8` (also used for Q,
[mission.md](../formats/mission.md#q-einsatzalarm-and-incomplete-waypoints));
its other callers are `0x45a810` (its key queue `0x4d20c4`, `0x45ad62`), the
window procedure (code `0x410`, `0x466522`, `0x466535`) and joystick button
action 6 (code `0x14`, `0x460e83`); details in [input.md](input.md). Codes
without a case there go to the key method `+0x2c` of the player actor
`[0x4d24e6]` if dword `0x49ef34` is nonzero, otherwise of the current view
subject `[0x4d2684]` (`0x46082f..0x46085a`). In the vehicle vtable
`0x49a282` that method is `0x4157c8` (flyer vtable `0x49a5e0`: `0x4215c0`).
The cheat keys are split over these two handlers. All 19 references to
`0x49ef38` are accounted for: 7 in `0x4157c8`, 1 in `0x45a274`, 2 in the
option parser (`0x45e88b`, `0x45e898`) and 9 in `0x45feb8`.

### Vehicle key method `0x4157c8`

After the equipment handler `0x410f78` declined the key, these keys work
only when the flag is set:

| Keys | Code | Effect |
| --- | --- | --- |
| Alt + Up / Num8 | `0x248` | move the Herc by (0, +step, 0) in its own frame (forward) |
| Alt + Down / Num2 | `0x250` | (0, −step, 0) (back) |
| Alt + Left / Num4 | `0x24b` | (−step, 0, 0) (left) |
| Alt + Right / Num6 | `0x24d` | (+step, 0, 0) (right) |
| Ctrl + Left / Num4 | `0x44b` | heading word `+0x10` += turn step, pose cache cleared |
| Ctrl + Right / Num6 | `0x44d` | heading −= turn step |
| Alt + Ctrl + 1…9 | `0x602..0x60a` | step and turn step from two tables at `0x49941c`/`0x49942e`: 500, 1000, 1500, 2000, 3000, 4500, 6000, 7500, 9000 |

The moves rebuild the pose matrix if needed (`0x47ebb8`), transform the
offset by the pose (`0x4801d4`) and write the result directly into the
position dwords at actor `+0x26`: a teleport, not a movement through the
physics. One raw unit is 6 game mm ([player_state.md](player_state.md)), so a
step moves about 3 m to 54 m. Because unhandled codes go to the view subject
when `0x49ef34` is zero, these keys presumably move whichever vehicle is
being viewed (**hypothesis**). In `0x45a274` the flag also makes keys with
Alt or Ctrl return 2 instead of 1 to the caller (`0x45a411..0x45a426`).
**Live:** the teleport and turn keys work.

### Main command handler `0x45feb8`

Nine cases test the flag. Eight do nothing without it; Alt+S has two other
conditions.

| Keys | Code | Handler | Effect |
| --- | --- | --- | --- |
| Alt + S | `0x21f` | `0x46039e` | toggles the freeze word `0x4d24f2` (below). The only key here that also works without the flag: while a tape is played back (`-D` or `-p` set `0x4d24d6`) or recorded (`-r` sets `0x4d24d8`) |
| Alt + Num+ | `0x24e` | `0x4601f0` | single step: clears `0x4d24f2`, sets word `0x4d24fc` |
| Ctrl + N / Ctrl + P | `0x431` / `0x419` | `0x460214` | moves the view to the next / previous entry of actor list `0x4a9af8` (`0x411e40` / `0x411e60`), skipping entries whose `+0x2e` is below −99,000. It stores the target in `0x4d251c` and sets `0x4d24f0` = 1. Requires camera object `[0x4d24ea]` `+0x4a` = 0. Unlike the plain N key (`0x460446`: view mode 3 and the player's group only), any list entry qualifies |
| Ctrl + F | `0x421` | `0x4602e1` | the view-mode switch of the plain V key (`0x4603d9`: `0x42a3f4` on `[0x4d24c0]` with 3 or 2, word `0x4d251a`), but sets word `0x4d2534` = 1 where V clears it. In view mode 3 (`0x4d24ee`) it clears camera `+0x36` and sets mode 1. The meaning of `0x4d2534` is **open** (no read through its absolute address) |
| Ctrl + T | `0x414` | `0x460377` | toggles word `0x4d24f0`. The view-subject setter `0x45df34` stores 1 there for any subject other than the player, and word `0x4d2518` for the player (`0x45e1d4..0x45e1f1`). **Hypothesis:** outside versus cockpit view |
| Alt + Ctrl + , / . | `0x633` / `0x634` | `0x4601a2` / `0x4601c9` | decrements / increments word `0x4d2500` within 0…29 (no other write) |
| Alt + Ctrl + D | `0x620` | `0x46016e` | calls the damage method `+0x74` of the view subject `[0x4d2684]` with slot `0x4d2500`, amount 300 and the player pointer as fourth argument |
| Alt + Ctrl + N | `0x631` | `0x4600bf` | the **first** actor of list `0x4a9af8` (count `0x4a9afe`) whose group side byte `+0x12` is 1 (in C1_01 the twelve hostiles), whose group has no pending activation (`+0x14` = 0), whose byte `+0x99` is 0, and whose distance to the player (`0x49288c`) is below 100,000: `+0x74` with slot 0, amount 32,000 (`0x7d00`) and the player pointer; then the handler jumps to its exit (`0x46013e` → `0x46085c`), so one actor per key press |

`+0x74` is the damage method of [collision.md](../formats/collision.md#collision-virtual-methods-dependency-boundary):
`0x417de4` for vehicles maps the slot to a damage region, and the radial hit
`0x411b3c` calls it with the same order (actor, slot 0, amount, passed-through
pointer; `0x411b7b..0x411b82`). `0x49288c` subtracts the two position vectors
(`0x492a5e`) and returns the length (`0x492a7d`). If that length is exact,
100,000 raw units are 600 m (6 mm per unit); the exactness was not checked.

### Freeze and single step

The logic frame `0x45f570` (called by the main loop at `0x45f487`,
[main_loop.md](main_loop.md)) tests `0x4d24f2`. While it is set, the frame
skips:

- the object update block `0x45f5ba..0x45f816`: the object lists with their
  update calls, including group activation and orders `0x45f7ec`/`0x45f7f5`;
- timers and spatial triggers (`0x45f860`/`0x45f86c`, [mission.md](../formats/mission.md));
- the actor loop `0x45f881..0x45f8cc` (`0x4123ac`, then `0x41aa5c` per actor);
- the branch for a destroyed player (byte `+0x99` set): `0x45fa84` at
  `0x45f8f8`.

`0x45fc10` stores the negation in `0x4d267e`. Two parts are not gated: input
(`0x460870`, called at `0x45f838`), and the outcome evaluation `0x4131ac` at
`0x45f992`, which is taken when the player's `+0x99` is 0. That evaluation can
still return a nonzero result from the frame (`0x45fa40`). Otherwise the frame
returns 0, so the main loop still calls `0x45fca8`/`0x45fba4` and increments
the frame counter `0x4d257b`. Frozen frames are therefore counted.

A step request (`0x4d24fc`) runs exactly one object update block. The same
pass then clears the request and sets `0x4d24f2` again (`0x45f818..0x45f82b`),
before the timers, triggers and actor loop, so these do not run in a step
pass. The test at `0x460ad4` in the input routine is the freeze check of the
flyer input path (`0x41bb9c`); the ground movement input is not gated by the
freeze word. Not traced: the second writer `0x45dcdc` (sets `0x4d24f2` = 1).

The nine keys of `0x45feb8` have not been tried live.

### VSHELL `-@`

`-@` or `/@` sets byte `0x482368` (option table `0x40162c` / `0x401664`,
`?`..`v`). It is read only in `0x413305`, which is called after a mission's
results are read (`0x40f647` in the results path `0x40f09f`) and after a game
is loaded (`0x41308e` in `0x413053`). Without the flag it calls `0x44e1b5`
and then creates and queues two `0x29`-byte objects (types 2 and 1) through
`0x468780`; with the flag only `0x44e1b5` runs. The reset routine
`0x4079a9..0x4079e3` clears the byte again. With `-@` VSHELL's DEBUG dialog
waits at these points ([Graphical shell](#graphical-shell), vshell_ui.md).

**Live** (`calibration-r111`):
the chain carried the options as derived: `vshell.EXE dummy -eggplant -X1 -@
-G` → 2 → `dbsim.EXE dummy -eggplant -X2 -R0 -SPRUNKNOWN -G` → 3 →
`vshell.EXE dummy -eggplant -X3 -@ -G`. That VSHELL then failed an assertion
after 9 s (`assert.log`: `Assert::err: Line 841 in "src\msn_gen.cpp":
msn\ASASASAS.msn`), i.e. `__exit(1)`, and the loop started VSHELL with `-X1`
again. The name `ASASASAS` is not a string in VSHELL; it is built through
the `%s.msn` format (`0x479746`) from an invalid campaign entry.
**Hypothesis:** in cheat mode VSHELL prepares a mission from a stale or
invalid entry because `0x413305` does not queue the campaign progression
steps. **Open:** whether ES.EXE with `-SPRUNKNOWN` shows the same assertion
(it would pass identical arguments).

## Campaign chain through the own launcher

`es2launch.exe` can run a campaign without VSHELL: a replacement program
started in VSHELL's place prepares the mission, DBSIM plays it, the
replacement runs the results path and prepares the next mission. The
contracts reproduced are those of [vshell_preparation.md](vshell_preparation.md):
preparation `0x41cd61` (four launch files), results path `0x40f09f` with
advancement `0x4133eb` and debrief `0x41d8e7`. The replacements are the
script `es2chain.py` (this section), the text menu `es2menu`
([Own menu](#own-menu-es2menu)) and the graphical shell `es2shell`
([Graphical shell](#graphical-shell)).

### The launcher's shell replacement

- `ES2_LAUNCH_SHELL` holds a command prefix that is started *instead of*
  VSHELL, with the VSHELL argv appended (`dummy -eggplant -X<state> [-s] [-a]
  [-@] [-<language>]`), with `CREATE_NO_WINDOW`, or with `CREATE_NEW_CONSOLE`
  when `ES2_LAUNCH_SHELL_CONSOLE=1` (`es2l_replacement_console`).
- Its exit code 2 starts DBSIM as before (`-X2 -R<n>`), 5 starts
  `dbsim.EXE -X5 -R<n> -D` like ES.EXE (VIEW DEMO, vshell_ui.md section
  15.5); **any other code ends the loop** (`es2l_after_replacement`). This is
  the one deviation from ES.EXE and applies only in this mode: ES.EXE answers
  VSHELL's 1 with a fresh VSHELL, which would loop forever on a crashing
  replacement. DBSIM's exit codes keep their ES.EXE meaning (1/3/4/6 start
  the shell again, 0 ends). Without the variable nothing changes.

### The replacement shell without the 256COLOR layer

`es2launch` leaves `__COMPAT_LAYER` out of the **replacement's** environment
only (removed around its `CreateProcessA`, restored at once); VSHELL and
DBSIM keep the layer that `start-own-launcher.ps1` sets. For es2sim the
launcher puts the removed value into `ES2SIM_COMPAT_LAYER` for the same
`CreateProcessA` (and clears it afterwards), so that es2sim can rebuild the
guest's environment copy the way DBSIM's holds it: es2sim itself runs without
the layer, and only the copy it makes into the guest's heap gets
`__COMPAT_LAYER=<value>` at its sorted position (rtl_imports.md, "The
environment copy"). The shell replacement gets no such variable.

An own true-colour
program has no use for Windows' emulated 256-colour display (es2shell's
frame is converted through the uploaded table, vshell_presentation.md "The
own shell's frame, presenter and window", deviation 3; the art's own
palette colours look the same in VSHELL, item 5 there). The launcher log
is unchanged. `launch_chain_test` case I:
the stand-in records the layer it inherited (`layers.log`); DBSIM has one,
the replacement none (Windows rewrites an unknown inherited layer to
`ElevateCreateProcess`, so the test checks presence, not the value); the
one-line mutation that keeps the variable is caught. Case J adds that
es2sim receives the value as `ES2SIM_COMPAT_LAYER` (the same string VSHELL
inherits, not named in the test because Windows rewrites it), that VSHELL
does not get that variable, and that the launcher does not keep it; the
mutation that omits the hand-over is caught. The DPI declaration
(vshell_presentation.md "The own shell's frame, presenter and window",
deviation 4) keeps es2shell's window the size of VSHELL's on a scaled
desktop.

### es2chain

`shell/tools/es2chain.py step --state DIR`, one process per shell start,
state in a chain directory created by `es2chain.py init` (`chain.json`,
`save-NNN.sav` after every results path, `mission-NNN/` with the four launch
files and `dbsim/` with the `results.dat`/`mission.var` DBSIM returned,
`chain.log`):

| Shell start | VSHELL | Chain |
| --- | --- | --- |
| `-X1`, nothing pending | start-up, menus, the player picks the mission | prepare the SAV's CAREER mission into `DATA/`, exit 2 |
| `-X3`/`-X4` | load save 10, `0x40f09f` (3 and 4 alike) | read the fresh `DATA/results.dat` and `mission.var`, results path on the chain's SAV; return 2: keep the SAV, prepare the next CAREER mission, exit 2 |
| results path: final word 3 / mode not 1 / return 1 / return 0 | UI end screens | stop (`stop-player-lost`, `stop-other-mode`, `stop-return-1`, `stop-return-0`), SAV kept, exit 0 |
| `-X1` with a mission pending | DBSIM failed: fresh start, no results read | stop `stop-dbsim-without-results`, exit 0 (opt-in `--results-on-exit-1`: read a results.dat DBSIM wrote before exiting with 1, for measurement runs) |
| `-X6`, other states | the demo end skips the readback | stop, exit 0 |

- **Carried state (es2chain's own convention):** the SAV (bank, roster,
  hangar, inventory, position), the 114-byte RNG state and the 30 reset
  selectors pass from each step to the next (preparation, results path,
  preparation). VSHELL instead resets the preparation stream `0x482409` and
  advances it `GetTickCount() & 127` times once per process (`0x401d39` →
  `0x407b42`); its menus, screens and animated background never draw; only
  new game, preparation and the results path draw
  ([vshell_preparation.md](vshell_preparation.md#rng-use-in-the-menus-and-the-start-up-seed)).
  The carry is therefore a documented deviation (effect on SAV bank words
  4..6 and type-1 outcomes); the [Shell state engine](#shell-state-engine)
  follows VSHELL. Mode 1 (campaign groups 1..5), option bytes
  `0x4825c8`/`0x4825c9` from `init` (default 0/0).
- **Hangar decision:** VSHELL's hooks can leave the player without a Herc.
  The (1,3) hook moves the player to bay 4 (on GAME_R the vehicle-8 Herc under
  construction); the (1,6) hook removes vehicle 8 and sets the player's bay
  to -1. Before C1_07 the real player picks a Herc in the hangar UI. The chain
  stops there (`stop-player-without-herc`) unless `--hangar lowest-free`:
  the player takes the lowest bay no roster pilot is assigned to (bay 0 on
  GAME_R), written as `save-NNN-hangar.sav` and recorded per mission. This is
  a **chain policy, not VSHELL behaviour**. On the unpatched SAV our
  preparation refuses (`member not found 0`) while the original reads bay -1
  and prepares files from it, an out-of-range read our safe API does not
  emulate.
- **Game directory:** only `DATA/` is written (the four launch files). The
  chain backs up `script.dat`, `MISSION.STR`, `mission.var`, `player.mec`,
  `results.dat`, `EXIT.CFG` before its first write and restores them (files
  that did not exist are removed) when it stops, unless `--keep-data`;
  `es2chain.py restore --state DIR` does it by hand (e.g. after the launcher
  was ended from outside). The game's `SAV/` directory is never written: the
  real tests read GAME_R.SAV from the clones by pinned hash. A game directory
  with a path component `native-r111` is refused, the step refuses to run in
  another directory than the one given to `init`, and a `results.dat` that
  DBSIM did not rewrite since the launch (size, SHA-256, mtime) is not read.
  The chain directory must be new and outside repositories unless ignored
  (`es2prep.export_path`); it holds prepared mission data and SAV copies and
  is never committed.
- Not made by the chain: UI decisions (Herc choice, purchases, team changes,
  repair and restock options) beyond the `lowest-free` policy and the two
  option bytes.

### Verification

- `//launcher:launch_logic_test`: command line of the replacement (prefix,
  appended argv, capacity), the exit-code rule, `es2l_replacement_console`.
  `//launcher:launch_chain_test` cases F/G: the launcher with a stand-in
  replacement (shell `-X1` 2, `dbsim -X2 -R0` 3, shell `-X3` 2, `dbsim -R1`
  1, shell `-X1` 1, end 0; VSHELL never started) and with an unstartable
  replacement (end 0).
- `//shell:chain_test` (synthetic, fake engine): argument parser, decisions
  per state, continuation, freshness, DATA backup and restore, RNG and
  selector carry, mission limit, all stop paths, the hangar policy and its SAV
  walker, `--results-on-exit-1`, and that the step entry answers 0 (never 2)
  on any failure. Mutations of the limit comparison, the RNG carry, the
  freshness check, the hangar choice and the `-X1`-with-pending decision each
  fail it; the RNG-carry mutation also fails the real test.
- `//shell:chain_real_test` (needs `ES2_DATA_ROOT`; read only):
  1. **Recorded suites.** For every distinct `results.dat`/`mission.var`
     pair a suite produced (golden plus all parallel oracle runs),
     starting from the SAV and RNG its launch files were prepared from
     (GAME_R.SAV and 71 draws for `c1_01-success`; `loadout.sav` and seed 0
     for the launch sets): our first preparation equals the files DBSIM ran
     (pins, launch-set manifest); our results path equals the original
     (`es2shellprep --results-batch`) in SAV, position, path, final word,
     debrief strings and text, and RNG; where the chain continues, our next
     preparation equals the original preparation from the original's SAV and
     RNG (four files, RNG, selectors):

     | Suite | Recorded pairs | Results path | Chain |
     | --- | --- | --- | --- |
     | c1_01-success | results.dat / mission.var of the success output and of the outcome-0 variant | equal; return 2, next (1,1) | C1_02 prepared, equal to the original |
     | c3_09-loadout-b | 1 | equal; final word 3 | `stop-player-lost` |
     | c3_10-loadout-hercs | 1 | equal; final word 3 | `stop-player-lost` |
     | c4_08-razor-plasma | 1 | equal; final word 3 | `stop-player-lost` |

  2. **Launcher end to end** (temporary game directory, synthetic
     `error.str`, `dbsim.EXE` is a stand-in that copies the recorded
     c1_01-success output): launch log `shell-replacement … -X1 -G`, 2,
     `dbsim.EXE dummy -eggplant -X2 -R0 -G`, 3, `shell-replacement … -X3 -G`,
     0, `end 0`; C1_01 equals the pins; C1_02 equals part 1; DATA restored;
     the chain directory replays equal against the original.
  3. **Whole campaign** through the launcher with synthetic DBSIM results
     (outcome 1, healthy Hercs, one slot at condition 79 on odd missions,
     counter words) and `--hangar lowest-free`: C1_01 to C5_10, 50 missions,
     one hangar decision (before C1_07, bay -1 to 0), ends with
     `stop-return-1` after C5_10. Replayed through the original: 50 of 50
     results steps and 50 of 50 preparations equal.
  4. `from_slot_campaign`: the campaign of a clone whose slot 10 holds its
     copies, built with our library (GAME_R.SAV's loaded image, its launch
     files = the pins, the c1_01-success output, results path from reset +
     71, C1_02 prepared with the reset RNG's script.dat and mission.var),
     replayed: 1 results step, 1 of 1 preparations, 1 slot launch; save-001
     equal.

  `ES2_CHAIN_VERIFY=<chain directory>` (as `--test_env`) additionally
  replays an existing chain or menu campaign directory: every results step
  (from `results_rng`, else es2chain's `output_rng`; a from_slot step without
  `results_rng` is a failure, never replayed from another state), every
  prepared mission, every recorded service-bay step (`bay`) and, per launched
  from_slot mission without bay steps, the original `0x40f68c` (slot mode
  `launch` on the loaded state) against the recorded `launch_files` (older
  logs: `files`) `mission.var` and `player.mec` (`slot_launches()`). Missions
  with `replay_of` or `from_slot` were not prepared and are skipped as
  preparations. A record without `results_rng` and a wrong launched
  `player.mec` are reported, not raised.

Expected C1_02 launch files after the recorded c1_01-success golden output
(GAME_R.SAV, seed 71, RNG carried; the test pins the SHA-256 of each file):

| File | Size |
| --- | ---: |
| script.dat | 12108 |
| MISSION.STR | 1081 |
| mission.var | 2000 |
| player.mec | 303 |

With the outcome-0 variant only MISSION.STR differs (1164 bytes). With
the RNG reset at the process start (the engine's rule) `script.dat` and
`mission.var` differ from the carried-RNG ones.

**Live** (original DBSIM, `timing-r111-p1`, the c1_01-success suite's DBSIM
settings, one mission; the chain run records the launcher log, `chain.json`,
`chain.log`, status, normalized tick log and comparison):

1. Launcher log: `start shell-replacement … -X1 -G`, `exit shell-replacement
   2`, `start dbsim.EXE dummy -eggplant -X2 -R0 -G`, `exit dbsim.EXE 3`,
   `start shell-replacement … -X3 -G`, `exit shell-replacement 0`, `end 0`.
   The console Python replacement ran under the launcher's `__COMPAT_LAYER`.
2. `missions[0].files` equal the pinned C1_01 hashes.
3. `results.dat` and `mission.var` equal the golden values; the tick log (6,198 records, 16,305 watch changes on both sides)
   is identical to the `c1_01-success` golden after the usual normalization:
   the launcher path does not change the mission.
4. `finished.reason` = `stop-mission-limit`; `missions[0].advance`: ending
   `advanced`, advance 2, next (1,1) C1_02; `missions[1]` has exactly the
   predicted C1_02 hashes.
5. `chain_real_test` with `ES2_CHAIN_VERIFY` passes.
6. `finished.restored` lists the six DATA files; the clone's `DATA` files
   have the pinned C1_01 hashes again and `SAV\GAME_R.SAV` is unchanged.

**Open:** a second mission live (C1_02 has no recorded input script); the
synthetic DBSIM results of the whole-campaign test exercise the advancement,
the hooks, the group changes and the end of the campaign, not real DBSIM
outputs (only C1_01 has a recorded successful result; the three loadout
suites end with the player lost). A stalled DBSIM is not stopped by the
launcher (no tick limit).

## Own menu (es2menu)

`es2menu.exe` (`//shell:es2menu`) is the own text-mode shell that the
launcher starts instead of VSHELL. It holds VSHELL's state in its own process
through the [Shell state engine](#shell-state-engine) and starts no backend.
Screen facts are [vshell_ui.md](vshell_ui.md); the flow, the file layer and
the host are shared with the [Graphical shell](#graphical-shell).

### Programs and set-up

- `shell/src/menu.c`: console screens and Win32 glue; `menu_logic.c` /
  `menu.h`: argument parser (`es2m_parse_args`), ESTEXT catalogue lookup
  (`es2m_bin_raw`, `es2m_bin_string`), SAV summary (`es2m_save_info_read`);
  the file layer `shell/src/host.c` (`//shell:host`) over the engine's
  `engine_io`.
- `shell/tools/es2menu_backend.py setup --menu NEW_DIR --menu-exe EXE --core
  CORE --cd-root CD --game-dir GAME ...` writes `menu.ini` (read by es2menu:
  `game_dir`, `cd_root`, `seed`, `hangar`, `chain_dir`; `core` is recorded
  for the evidence log's tools) and prints the `ES2_LAUNCH_SHELL` value
  `"<es2menu.exe>" --menu "<menu directory>"`. Its `new_game_request()` (the
  new career through `es2prep_core --new-game`) is the real-data tests'
  reference.
- **Menu directory** (new, outside repositories unless ignored, never
  committed): `menu.ini`, the evidence log's `campaign-NNN/` and `current`,
  `menu.log` (transcript with the game's text bytes), `input.pos` (scripted
  input only: `--input FILE` replaces stdin and the position is kept across
  the menu starts of one launcher run).
- **Labels** are read at run time from the installation's `VOL\LANG0.VOL`,
  `<language>/ESTEXT.BIN` (a BIN catalogue, not a packed STR file; VSHELL's
  string lookup `0x4087f0`), by string ID, and printed as stored (DOS code
  page bytes, e.g. German 0x94 for o-umlaut). The `.str` files go through the
  STR reader/writer (`formats:str`): `SAV/GAMEFILE.STR` (slot names and
  flags); the debrief text is the results path's composition from
  `DATA/MISSION.STR`. No original string is in the repository; without
  LANG0.VOL the menu prints own short words.
- Exit codes: 2 (start DBSIM) only after a successful launch, 5 for VIEW
  DEMO, 0 for Quit, the end of the input and every failure.

### Main menu

Entries 1 START NEW GAME, 2 CONTINUE GAME, 3 Rock & Roll, 4 PREFERENCES, 5
QUIT, 6 SAVE/RESTORE, 7 PRACTICE MISSIONS, 8 INSTANT ACTION, 9 VIEW DEMO, 10
CREDITS, 11 the online manual (unless the help policy hides it). Practice,
instant action, VIEW DEMO, the DEBUG dialog of `-@`, the mission screen
texts, the launch checks with their warning box, the Replay and End boxes,
movies (the text menu prints them) and the bay editors follow vshell_ui.md.

| Start / entry | VSHELL | Own menu |
| --- | --- | --- |
| `-X1`, `-X6`, other | intro or main menu, no game loaded (`0x4826ee` = 0) | main menu, no game loaded |
| 1 START NEW GAME (0x04) → REGISTRATION (0x30) | mode 1 (`0x40ec56`), name, SKILL LEVEL 0x35..0x38, ACCEPT → new career `0x40e8a5` → Use Default | prefs +0x2a := 1 (single-byte rewrite, `es2_prefs_overlay`); name 1..29 printable ASCII; skill 1..4 = rank 0..3; ACCEPT → new career, mission (1,0) prepared into `DATA`, service bay |
| 2 CONTINUE GAME (0x05) | mode 1, `0x40eaaa(10, 1)`: nothing when the slot flag is 0; else the three copies back to `DATA`, loaded; final word 2 → service bay, else End of game box (still loaded) | the same load; with the flag of slot 10 at 0 it prints "Restore or start a new game." (0x132) where VSHELL shows the End of game box for the final word in memory (vshell_ui.md §9.2, **H**); neither loads anything |
| 3 Rock & Roll (0xb8) | launch checks, `0x40f68c`, exit 2 | the same (the engine's `launch`) |
| 4 PREFERENCES (0x0a) | radio buttons, Accept writes 54 bytes, Cancel nothing | Repair Options +0x2c (0x113..0x115), Weapons Building +0x2d (0x111/0x112), Display +6 (0x124/0x125); Accept writes the 54-byte block (`es2_prefs_serialize`; a short file becomes the zero-extended 54 bytes) and the memory byte `0x4825c9`, Cancel nothing |
| 5 QUIT (0x0e), end of input | quit flag, exit 0 | exit 0 |
| 6 SAVE/RESTORE (0x06) → SAVED GAMES (0x22) | mode 1; ten rows, statistics; SAVE (0x1d) with a loaded game, ACCEPT/CANCEL; RESTORE (0x1e) of a used slot; EXIT (0x1f) | mode 1; the ten slot texts as stored, for used slots name, skill, salvage, sector and mission numbers from the slot's SAV; `1` SAVE: slot 1..10, name (up to 24 of `^`, digits, letters, space), `1` ACCEPT → save `0x40e933(slot, row)`, `2` CANCEL writes nothing; `2` RESTORE: slot 1..10 with its flag → load, slot 10 saved at once, service bay for every final word; `3` EXIT |
| every save | `0x40e933` | slot table loaded with the running language's ESTEXT 0x21, marked, rewritten without truncation (size field = file length − 4); the memory state's writer image over the slot file without truncation; the three `DATA` launch files copied (truncating) |
| every exit with a loaded game | save slot 10 (11 in mode 0) | the same |

**No delete:** VSHELL has none (vshell_ui.md §9.3), and the menu offers none.

### New career `0x40e8a5`

`es2_shell_new_game` (`shell/src/save.c`): the default SAV of
`es2_shell_default_save` (inventory, roster, player, HERCS hangar, zero bank),
but the roster and the player variant are drawn from the given RNG, then the
salvage total = 100,000 + 1,000 × draw(11), final word 2. Order in VSHELL
(static): `0x40e8a5` stores 2 at `0x4826f2`, clears the bank
(`0x40eef1`, 2,000 zero bytes at `0x482bdc`), loads WEAPONS (`0x4125e8`, no
draw), the roster (`0x410055`: `0x40ff80` and `0x410000` push `0x482409` at
`0x40ffae`, `0x41001a`), the player (`0x41072b`: bound 11 from `0x482409` at
`0x41073c`), the hangar (`0x410b11`, no draw) and the CAREER position
(`0x413053` → `0x413305`, the dialog; no draw), then `0x40e906..0x40e924`:
`0x465d2c(0x482409, 11)` = (word & 0x7fff) mod 11 (the helper
`es2_shell_rng_draw` reproduces), sign-extended, times 1,000, plus 100,000,
stored at `0x482bd8`. So the roster comes from the **preparation RNG**, not
from the seed-zero stream `es2_shell_default_save` keeps for its harness
convention (vshell_preparation.md); with a seed-zero stream both agree
except tally and final word. 3 × (4 + 3 + 12) + 1 = 58 draws precede the
salvage draw. Six new careers equal `0x40e8a5` in vitro
(`//shell:slots_real_test`).

### Saved games

Facts and in-vitro results: [vshell_ui.md §9](vshell_ui.md#9-save-slots-and-new-career);
harness: [shell_invitro.md "Slot mode"](../reference-game/shell_invitro.md#slot-mode).
Library (`//shell:flow`, portable, no file I/O): `es2/shell/slots.h` (slot
table reader `0x40e380` with the ESTEXT 0x21 append, writer `0x40e503`,
`0x40e6cd`, slot 10 → 11 in mode 0, the row text `"%2d. %s"` with the row's
edit set, companion names); `es2_shell_loaded_save` (the state a load
restores: every item list reversed) and `es2_shell_prepared_save` (the state
after a mode-1 preparation: position, bank = `mission.var`, briefing
selection, team words) in `es2/shell/save.h`; `es2_shell_output.briefing`
(the 148 bytes `0x4134f2` derives) from `es2_shell_prepare`. Private requests
in `es2prep_core`: `--slot-save`, `--loaded-save`, and an optional 18th batch
field that returns the prepared SAV (`es2prep.request(prepared_save=True)`).

Deviations: the typed slot name is limited to 24 characters (VSHELL: the
row's pixel width, not computed); the statistics show numbers instead of the
sector and mission names and no kill counts. Without `VOL\LANG0.VOL` the
empty text is unknown: a slot table whose texts need it is refused, nothing
is written.

### Service bay and WEAPONS

Facts and in-vitro results: [vshell_ui.md §10](vshell_ui.md#10-service-bay-and-weapons);
harness: [shell_invitro.md "Service-bay operations"](../reference-game/shell_invitro.md#service-bay-operations).

- **Library** (`//shell:flow`): `es2/shell/hangar.h`,
  `es2_shell_service_bay(vfs, tables, SAV, operations)` on the memory-state
  SAV (lists head first): MOUNT (`0x411b10`), CLICK (the row rule of
  `0x43fdac`: disabled rows, the four mount conditions, then the mount),
  GUIDANCE (`0x43e346`, missile racks 13..16, modes 0..3) and REBALANCE
  (`0x412a37` with the weapons-building option; the results path's own
  restock code). It lives in `campaign.c` next to the results path and shares
  its SAV projection. `es2_shell_launch_files(SAV)` is the launch-file writer
  `0x40f68c` (`player.mec` and `mission.var` of the stored state). Private
  core requests `--service-bay` and `--launch-files`; `--describe-save` also
  prints the list lengths, the salvage total and the build bank.
- **Front end** (`es2loadout.py`): `service_bay`, `launch_files`,
  `weapon_rows` (the 27 rows of `0x476a4c` with count, availability, ARM
  answer and enabled state), `arm_admits` (the CD's ARM tables, indexed by the
  bay record's vehicle word as `0x41501f` does) and `click_op`. Hangar rows
  are named by `<language>\WPN_DESC.BIN` entry 3 × item and the guidance
  modes by entry 0x63 + 3 × mode, as VSHELL's list does.
- **Menu:** the service bay (WARTUNGSHANGAR, ESTEXT 0x0c) is shown after a
  new career, CONTINUE (final word 2), RESTORE and a results path with
  return 2. The screen word follows `0x4758b8`: a bay button for the screen
  already shown does nothing in VSHELL (the text menu shows that sub-screen
  again); any other leaves the current screen first, and leaving WEAPONS runs
  REBALANCE `0x412a37` with the memory byte `0x4825c9`. MAIN MENU saves slot
  10 (`0x43a705`) and shows the main menu; SAVE opens Saved games without the
  mode store (`0x43a768`; EXIT returns to the bay); MISSION shows the mission
  texts and Rock & Roll. WEAPONS lists the bays with Herc name (ESTEXT 0x6e +
  vehicle) and pilot, the selected bay's slots with item and guidance mode,
  and offers bay, weapon (slot, then the 27 rows with count; `-` marks a
  disabled row → CLICK), guidance (a slot with a missile rack 13..16, then
  SARH/ARH/ARM/EO) and back. REPAIR, BUILD, ARMORY and CREW are the editors
  (`es2/shell/editors.h`).

Deviations: the bay selection of the squad panel is a prompt (not
serialized either way); the default bay is the player's (VSHELL keeps its
last selection or `0x41124e`'s). Guidance is offered for slots that hold a
rack 13..16; VSHELL keeps the panel visible after the slot selection changes,
which could store a mode into another item (not offered). The click rule
`0x43fdac` is static (C) and unit-tested; the in-vitro comparison runs the
mount it decides on.

### Deviations

- A slot without its three copies (e.g. the pinned `GAME_R.SAV` alone) gets
  its CAREER mission prepared after the load instead of being launched with
  its copies and a fresh `0x40f68c` MEC and `mission.var`; VSHELL's copy
  `0x40da8d` stops at an assertion on a missing source.
- The player without a Herc: es2chain's `lowest-free` policy (`menu.ini`
  `hangar`, default; recorded per mission) instead of VSHELL's hangar choice;
  `stop` refuses the preparation.
- `-X3`/`-X4` with no game in slot 10 (11): the message and the main menu
  (VSHELL's results path would run on its image state; not reached through
  ES.EXE).
- The menu does not back up or restore the game's `DATA`.

### Verification

- `//shell:save_test`: `es2_shell_new_game` with a seed-zero stream equals
  the default SAV except tally and final word; with an own nonzero stream the
  state advances by exactly 59 draws (independent model in the test), the
  salvage is 100,000 + 1,000 × the 59th draw, the size query and every error
  leave stream, output and size unchanged; `es2_shell_prepared_save` on a
  synthetic SAV (only the four fields change, stale tail dropped, refusals).
- `//shell:menu_test`: argument parser, ESTEXT catalogue bounds, GAMEFILE.STR
  slot read and mark (stale tail kept and counted, only the flag byte
  changes, idempotent), `es2m_bin_raw`.
- `//shell:slots_test` (synthetic): reader (append, short texts, partial and
  negative counts, every refusal), writer (stale tail, size field, overlap),
  mark, slot 10 → 11, rows and the edit set, the composite, companion names.
- `//shell:slots_real_test` (ES2_DATA_ROOT): 12 saves (CD table in three
  languages, own tables with short, named and partial texts and a stale tail,
  slots 0..11, modes 0/1, old slot files absent, shorter and longer) equal the
  original save in `GAMEFILE.STR`, the slot SAV and the companions; the save
  without a loaded game writes nothing and the save of slot 3 without a text
  faults, as ours refuses it; 4 loads (GAME_R, GAME_T, a new career, a
  results-path SAV with distinct records) equal `es2_shell_loaded_save` and
  copy the companions; a load of a slot without its flag loads nothing; 6 new
  careers equal `0x40e8a5`; 82 preparations equal the original preparation
  followed by the writers (all 50 campaign missions on a new career, every
  seventh on GAME_R with seeds 0 and 71, on GAME_T and on the results-path
  SAV).
- `//shell:campaign_test` (synthetic): mount from the head and return to the
  head, the 13..15 rule, empty lists, item 0, every refusal with the failing
  operation's index; the click's disabled rows, its four conditions (the
  word +8 comparison included) and the no-Herc bay; guidance limits;
  rebalance with options 0 and 1; `es2_shell_launch_files` fields and
  refusals.
- `//shell:bay_real_test` (ES2_DATA_ROOT): vshell_ui.md §10.4 (ARM 2,277
  answers, 57 launch-file states, 74 operation sequences).
- `//shell:menu_real_test` (stdin scripts through `es2launch.exe` and the
  stand-in DBSIM in temporary game directories; see also the engine's
  verification): argument and input refusals (another working directory,
  `native-r111`, bad arguments); Preferences Accept/Cancel; the mode byte of
  START NEW GAME, name and skill checks; Continue from the pinned GAME_R.SAV
  with seed 71 → Rock & Roll → recorded c1_01-success output → Quit (launcher
  log `shell-replacement -X1`, 2, `dbsim.EXE -X2 -R0 -G`, 3,
  `shell-replacement -X3`, 0, `end 0`; C1_01 equals the pins); a new
  campaign (name, skill 2; tally 110,000 with seed 71, final word 2, zero
  bank, RNG after 59 draws); the Saved games session (refusals without a
  game; new career, SAVE slot 3, EXIT, QUIT; RESTORE of slot 3, a refused
  name, CANCEL; CONTINUE of a slot 10 with final word 0); the bay sessions
  ((a) a new career, WEAPONS with a stocked row, row 27, a disabled row, a
  refused guidance, a missile rack and its guidance, WEAPONS again, SAVE slot
  4, MAIN MENU, QUIT: `GAMEFILE.STR`, `GAME_3.SAV`, `GAME_R.SAV` and the six
  companions equal the original mounts, guidance, rebalance and three saves
  on the same files; (b) CONTINUE, row 27 on slot 1, MISSION → stand-in DBSIM
  → results → bay → MAIN MENU → QUIT: DBSIM was handed the `mission.var` and
  `player.mec` of the original `0x40f68c` after the same mount, the results
  path started from the original state after it, slot 10 equals VSHELL's
  save). Six labels equal the ESTEXT.BIN entries read by the test's own VOL
  reader. Every `SAV` file and the `DATA` copies equal the original
  save/load sequence on the same files.
- Mutations (each caught, then reverted): salvage factor 1,000 → 100; no
  ESTEXT append, a truncating slot-table rewrite, slot 10 kept in mode 0, a
  load that keeps the item order, a prepared SAV without the team words, a
  briefing count that includes −1 entries, no exit save for a slot-only game,
  RESTORE without its slot-10 save; the old item returned to the tail of its
  list, the bay SAV read as a file (lists not turned back), the 13..15 rule
  widened to 16, condition word 22 + slot, the click without the word +8
  comparison, launch files with a changed bank word, no rebalance when
  WEAPONS is left, a rebalance when WEAPONS is chosen again, a launch that
  ignores the bay changes, the ARM tables of the wrong vehicle.

In-vitro equality and stand-ins are not live validation.

## Shell state engine

`es2menu.exe` and `es2shell.exe` hold VSHELL's state in their own process and
keep between processes only what VSHELL keeps, in `SAV\` and `DATA\`; no
Python runs at run time. The globals and their lifetimes are
[vshell_state.md](vshell_state.md); the RNG facts are
[vshell_preparation.md](vshell_preparation.md#rng-use-in-the-menus-and-the-start-up-seed).

- **Library** (`//shell:engine`, `es2/shell/engine.h`, `shell/src/engine.c`,
  no file I/O): `es2_shell_engine` holds the memory state as a SAV image (item
  lists head first, as `es2_shell_prepared_save`/`es2_shell_loaded_save`
  give it), the loaded flag `0x4826ee`, the mode word `0x4826f0`, the final
  word `0x4826f2`, the RNG `0x482409` and the thirty reset selectors
  `0x48553e`. Operations: `start` (a process start: nothing loaded, final word
  0, selectors 0, RNG reset + n draws), `new_game` (`0x40e8a5` at CAREER
  (1, 0)), `load` (`0x40eaaa`: `es2_shell_loaded_save`, the slot's final word),
  `set` (practice and instant action), `prepare` (Use Default, mode 1, then
  `es2_shell_prepared_save`), `results` (`0x40f09f` over the slot file's
  writer image, `DATA\mission.var`, `DATA\results.dat`, the mode word and the
  start-up option bytes), `bay_view`/`bay`/`admits` (the WEAPONS list rows of
  `0x476a4c` with the CD's ARM tables, `es2_shell_service_bay`) and `launch`
  (`es2_shell_launch_check`, then `0x40f68c`). The executable tables (the 740
  preparation bytes with the nine preset paths, the 322 results bytes, the 27
  list rows, the nine ARM names) are read from the hash-checked
  `VER95\VSHELL.EXE` at run time (`es2_shell_engine_tables_read`).
- **File layer** (`shell/src/engine_io.c`): opens the CD (`cd_root` of
  `menu.ini`) at every start, writes `DATA` as VSHELL's writers do
  (`script.dat` and `MISSION.STR` from offset 0 without truncation,
  0x41b278/0x418014, file_io.md; `mission.var` and `player.mec` truncating),
  and keeps the evidence log.

| Start / action | VSHELL | Engine |
| --- | --- | --- |
| every start | image state, prefs blocks, mode := +0x2a (`0x40e751`), RNG reset + `GetTickCount() & 127` draws (`0x407b42`) | the same; n = `ES2_MENU_SEED`, else `menu.ini` `seed`, else `GetTickCount() & 127` |
| `-X3`/`-X4` | load slot 10 (11 in mode 0) with its copies, results path, the debrief rewrites `MISSION.STR` | the same; the results path reads the slot file, `DATA\mission.var`, `DATA\results.dat`; return 2: the next mission prepared into `DATA` at once, service bay; 3/0: Replay box; 1: slot 10 saved, end text; mode 0 (the practice return): ending 4, main menu |
| `-X1`, `-X6` after a launch | fresh start, nothing read | the same; the log records the start |
| new career | `0x40e8a5` from the process RNG, Use Default | the same |
| CONTINUE / RESTORE | load: memory state and the three copies to `DATA`, no preparation | the same; a slot without its copies gets its CAREER mission prepared ([Deviations](#deviations)) |
| leaving WEAPONS | `0x412a37` with the memory byte `0x4825c9` | the same |
| Rock & Roll | launch checks, `0x40f68c`, exit 2 | the same |
| Replay Yes | load slot 10, exit 2, no `0x40f68c` | the same |
| every exit with a loaded game | slot 10 (11) from memory | the same |
| practice / instant action | the process's RNG stream | the same |

### RNG convention

The engine restarts the 114-byte RNG at every process
start as VSHELL does (es2chain carries it instead). With the same n the
first preparation is unchanged; the results path and every later
preparation start from reset + n. For `menu_real_test`'s C1_01 to C1_02
session (n = 71) C1_02's `script.dat` and `mission.var` differ from the
carried chain's, `MISSION.STR` and `player.mec` do not.

### Evidence log

`menu.ini` `chain_dir` (written by `es2menu_backend.py
setup` as the menu directory; `--no-log` leaves it empty), overridden by the
environment variable `ES2_MENU_CHAIN_DIR` (empty: off). It holds
`campaign-NNN` directories in es2chain's schema 1 (`chain.json`, `chain.log`,
the SAV after every step, `mission-NNN\` with the four prepared files,
`dbsim\` and `launch\`, `current`), written by C (`engine_io.c`, a small JSON
tree and SHA-256). Fields beyond es2chain's: `results_rng` and `seed` of each
results step (the RNG the `-X3` process started from, written for every
launched record at the start of the results path), `seed` in each start
record, `from_slot` for a mission that is a slot's own copies, and the
service-bay operations as `bay` steps (kinds 1 MOUNT and 3 GUIDANCE). A
from_slot mission always records `launch_files` and `launch_save`
(`eio_launch`): the slot's `script.dat`/`MISSION.STR` and the
`mission.var`/`player.mec` that `0x40f68c` writes at Rock & Roll; its
`files` keep the slot's copy of `player.mec`, which is not what DBSIM gets.
After bay changes the launch writes `mission-NNN\launch\` and records
`launch_files` and `launch_save`; the results path's start state is
`results_save`. A `-X3` without a matching campaign writes no record. The
log decides nothing the shell does; it only chooses the record a step is
appended to (the current campaign when the loaded slot's writer image equals
its SAV).

### Verification

- `//shell:engine_test` (synthetic): the ARM reader and its refusals, the
  table reader on an invented PE image (strings, pointers, refusals),
  hardpoints through a loose VFS, the process start (RNG reset + n,
  refusals), set/load, the bay view (rows, ARM answers, pilots), guidance and
  click on the memory state, refusals without a loaded game, the launch files
  exactly when the checks pass, the preparation's refusals (CAREER group,
  player without a Herc with and without the policy).
- `//shell:menu_real_test`: every `SAV` and `DATA` file the sessions write
  equals the mapped original for the same choices; the first preparation
  equals es2chain's; the results path equals our results path from the slot
  file and the reset RNG (summary, SAV, debrief, RNG), C1_02 equals our
  preparation from that state; `DATA\MISSION.STR` after the `-X3` process is
  C1_01's, the debrief's and C1_02's written over each other; `-X1` after a
  launch reads and writes nothing, the next CONTINUE and Rock & Roll hand
  DBSIM the slot's copies and `0x40f68c`'s `mission.var`.
- `//shell:chain_real_test` with `ES2_CHAIN_VERIFY` on an engine campaign
  (C1_01 from the pinned `GAME_R.SAV`, n = 71, the recorded c1_01-success
  output): `results_steps` 1, `preparations` 2, `preparations_equal` 2.
- Mutations: the results mode, the weapon-list row count and the start-up
  draw count are caught by `menu_real_test`; the enabled-row rule, the final
  word of a load and the hangar check by `engine_test`. **Not caught by any
  test:** the debrief's `MISSION.STR` write and its non-truncation (C1_02's
  text is the longest of the three in the corpus), the RNG after the results
  path (C1_01's debrief draws nothing in this session).
- **Open:** no automated test runs es2menu on a slot 10 with its copies and a
  fresh menu directory (the from_slot record of the C writer).

## Owner acceptance: one command

`tools/reference-game/setup/start-own-menu.ps1` sets up, runs and restores
one session of the own shell on an installation; the player plays one
mission and reports what was odd.

```powershell
pwsh -File tools\reference-game\setup\start-own-menu.ps1 -InstallName rebuild-r111 -Shell gui
pwsh -File tools\reference-game\setup\start-own-menu.ps1 -InstallName rebuild-r111 -Shell vshell
```

`-Shell text` (es2menu, default), `gui` (es2shell) or `vshell` (the original
`VSHELL.EXE` through the same backup and restore, for the comparison);
`-Runs 2` runs the launcher twice in one session (Route B); `-Seed` (default
71), `-NoVerify`, `-SettleSeconds`, `-DataRoot`, `-Python`,
`-BazelOutputBase` are described in the script's parameter block. The game
windows (and the menu console with `-Shell text`) are visible by design. The
play clone is `rebuild-r111` (windowed-DirectDraw proxy, `presenter=gdi`,
`directdraw=windowed`); on `calibration-r111` DBSIM crashes after the
shell's Full Screen ([DBSIM's full screen](#dbsims-full-screen-in-interactive-sessions)).

**Steps** (`-DryRun` prints them, with what the launcher script would do, and
changes nothing):

1. Refusals: not `native-r111` (the primary installation) unless
   `-AllowPrimary`, in which case the output says so (the backend,
   `start-own-launcher.ps1` and `es2menu.exe` refuse `native-r111`
   themselves, so the switch only lifts the script's own refusal); no
   `ES.EXE`, `VSHELL.EXE`, `DBSIM.EXE` or `ES2LAUNCH.EXE` of the installation
   running (CloneGuard rules; nothing is stopped); no other session's clone
   lock; no linked path. `start-own-launcher.ps1` still refuses clones that
   carry oracle settings.
2. The clone lock (`installs\.clone-locks`) is held for the whole script.
3. **Backup first:** `DATA` and `SAV` of the installation are copied to
   `es2-data/logs/own-menu-<stamp>/backup` (a `.gitignore` of `*`
   keeps the game files out of that repository), the copy is verified by
   hash, the tree hashes are printed and `restore.json` records every file
   hash.
4. The shell (`//shell:es2menu`, or `//shell:es2shell` with `SDL3.dll`) and
   `//shell:es2prep_core` are built with Bazel (output base
   `build/bazel/es2-shell`), `es2menu_backend.py setup` creates
   the menu directory `oracle\launch\own-menu-<stamp>` (seed 71), and
   `start-own-launcher.ps1` starts the launcher with `ES2_LAUNCH_SHELL` (and
   `ES2_LAUNCH_SHELL_CONSOLE=1` for the text menu). The script waits for
   `ES2LAUNCH.EXE` and for every game process of the installation to end.
5. Evidence is collected, then **restored always** (a `finally`): `DATA` and
   `SAV` are made equal to the backup again (added files removed, changed and
   deleted files restored), the tree hashes before the session, after it and
   after the restore are printed and must match. If a game process of the
   installation still runs (an `ES2LAUNCH.EXE` ended early, a DBSIM that
   lives on), the restore is **deferred**, never forced: the script says so
   (exit code 2) and prints the `-RestoreOnly` command:

   ```powershell
   pwsh -File tools\reference-game\setup\start-own-menu.ps1 -RestoreOnly es2-data\logs\own-menu-<stamp>
   ```

   `-RestoreOnly` checks the backup against `restore.json`, refuses while a
   game process runs, and with `-DryRun` lists what it would change. Exit
   codes: 0 restored, 1 refused or failed (restored if a backup was made), 2
   restore deferred, 3 restore incomplete, 4 a post-session step failed.
6. `bazel test //shell:chain_real_test` with `ES2_CHAIN_VERIFY=<the menu's
   campaign>` replays the results step, both preparations and the bay against
   the mapped original VSHELL (`-NoVerify` skips it); the compare checklist
   is printed.

**Routes** (German labels; the script prints the play guide, a checklist per
screen and the compare checklist):

- *Route A* (default, one run): SPIEL FORTSETZEN opens the service bay with
  C1_01 of the clone's `GAME_R.SAV` (with seed 71 the pinned C1_01, a Herc
  Outlaw in bay 1); WAFFEN: slot 1, row 13 (Laser 300 GW) → mounted; slot 3,
  row 16 (Raketenwerfer 6X) → mounted; MISSION (Rock & Roll) starts DBSIM;
  the cockpit shows a 300 GW laser instead of the first 50 mm cannon and a
  6-missile rack instead of the EMP cannon; play C1_01 to its end; the
  mission report and the bay (C1_02) appear; HAUPTMENÜ; quit.
- *Route B* (`-Runs 2`): run 1: NEUES SPIEL, a name, a skill, ACCEPT; in the
  bay SPEICHERN, slot 3, a name such as `TEST 1`, ACCEPT, AUSGANG; HAUPTMENÜ;
  quit. Run 2: SAVE/RESTORE, LADEN slot 3; in the bay MISSION (Rock & Roll);
  play it to its end; HAUPTMENÜ; quit. (The clone's slot table has slots 1
  and 2 in use ("S1", "Z1") and the flag of slot 10 set.)
- To stop early: end `ES2LAUNCH.EXE` first, then DBSIM; the script never
  stops a game process.

**Evidence** (`es2-data/logs/own-menu-<stamp>`): `restore.json`; `evidence\` with
`before-session-hashes.txt`, `after-session-hashes.txt`, `changes.txt` (files
added, removed and changed per folder), `launcher-run<N>.log` and
`summary.txt` (the launcher log events), `chain.json`, `chain.log`,
`status.json`, `mec-hashes.txt` and `sav-compare.txt`, `session.json`,
`restore-report.txt`, `chain-verify.log`, `checklist.txt`; `private\menu.log`
or `private\shell.log` (game text, therefore in an ignored directory like
`backup\` and `after-session\`, which holds the `DATA` and `SAV` copies as
the session left them). Launch files are recorded by hash only
(`after-session-hashes.txt`); their bytes stay in the ignored
`after-session`.

**Compare checklist:**

1. Launcher log (`launcher-run*.log`, `summary.txt`): `start
   shell-replacement ... -X1 -G`, `exit shell-replacement 2`, `start dbsim.EXE
   dummy -eggplant -X2 -R0 -G`, `exit dbsim.EXE 3` (1 after a scripted
   replay), `start shell-replacement ... -X3 -G`, `exit shell-replacement 0`,
   `end 0`. A run without a mission (Route B run 1): `-X1`, `exit
   shell-replacement 0`, `end 0`.
2. `chain.json` / `status.json`: `missions[0]` is C1_01 (a `from_slot` record
   when the clone's slot 10 holds its copies: CONTINUE launches them as
   VSHELL does); on Route A without slot copies its `files` are the pinned
   hashes `61efab9b...`, `4c359b61...`, `20c8eb56...`, `f669d125...`;
   `advance` and `missions[1]` as the played outcome gives (after a success
   like the golden: advance 2, C1_02 with the reset RNG's `script.dat` and
   `mission.var`); `results_rng` in the played mission. Service bay: the
   `bay` steps are each `applied` 1; `launch_files.player.mec` differs from
   `files.player.mec` (`mec-hashes.txt`) when weapons were changed, the other
   three equal; `results_save` = `launch_save`. Route B: one campaign
   (`campaign-000`, no `campaign-001`: RESTORE of the own slot reuses it),
   mission 0 C1_01 launched and advanced, mission 1 prepared, `save` ends in
   `-prepared.sav`.
3. `chain-verify.log`: the `verify` line has `results_steps` 1,
   `preparations_equal` = `preparations` and, with the bay, a `bay` entry
   (`steps`, `original_runs`), no FAIL.
4. `private\menu.log`: the German labels and the debrief text; Route B:
   ` 3. LEER` before and ` 3. TEST 1` after the save, `[saved slot 10:
   SAV\GAME_R.SAV]` after LADEN and after HAUPTMENÜ. `sav-compare.txt`: the
   clone's `GAME_R.SAV` after the session against the campaign's last SAV
   (equal when the new image is not shorter than the old file, as after
   C1_01).
5. `restore-report.txt` and the printed tree hashes: `DATA` and `SAV` equal
   the state before the session.
6. The player's verdict: the menus were usable, nothing of VSHELL appeared,
   the mission played as usual, the cockpit showed the changed weapons.

**Optional, not done by the script:** the real VSHELL for the same choices on
the clone (after the restore): start the original game, SPIEL FORTSETZEN,
WAFFEN, the same clicks, MISSION, Rock & Roll; while the mission runs copy
`DATA\player.mec` aside, then end the mission and restore `DATA` and `SAV`
from the backup once more (`-RestoreOnly`). Its hash is expected to equal
`mission-000\launch\player.mec` of the own session (for the pinned
`GAME_R.SAV` SHA-256 `a3c8262c...003b`). Before the restore, the original
VSHELL's SAVE/RESTORE screen can also be opened on the clone: slot 3 shows the
saved career with its statistics.

**Test:** `//tools/reference-game/setup:start_own_menu_test` runs the script
on synthetic installations in a temporary `DataRoot`: the real
`es2menu_backend.py setup`, a fixture launcher script that plays the session
(changes `DATA` and `SAV`, runs the stand-in DBSIM of
`//shell:chain_real_test`, leaves a campaign directory), a mock game process;
`-DryRun`, the refusals (primary installation, bad names, running game
process, held lock, `-AllowPrimary`), a whole session with its evidence and
restore, two runs, a failing launcher, a killed session and `-RestoreOnly`
(dry run, damaged backup, forged `restore.json`, running game), a game that
outlives the launcher (restore deferred), and `-Shell gui` / `-Shell vshell`
sessions. Never the real game, the real menu or a real installation.
Equality with fixtures is not live validation; the session has not run live.

### Unattended own-shell chain

The live check of the shell line that needs no interaction: one mission through
the own shell on a timing clone, with DBSIM under the oracle settings of the
suite `c1_01-success`, judged against that suite's golden. A switch of the
script and an optional stage of `tools/check.ps1 -Live`. Tool only: nothing
of VSHELL's behaviour changes.

```powershell
pwsh -File tools\reference-game\setup\start-own-menu.ps1 -Unattended -InstallName timing-r111-p1
tools\check.ps1 -Live -OwnShell        # after the suites, on the first clone of -InstallName
```

What `-Unattended` adds to the session (backup first, clone lock, restore in
a `finally` and `-RestoreOnly` are unchanged; `-DryRun` lists the steps):

1. Refusals before anything is created: a measurement clone only
   (`installation-manifest.json` with `measurement_clone_of`, as
   `configure-tick-log.ps1` requires; default `timing-r111`), one run
   (`-Runs 1`), the seed of the golden (71; `-Seed` otherwise refused), the
   suite in `oracle-suites.json` once and plain (no launch set, no extra
   arguments, no tape, no frame window, ends by itself), the golden
   `<GoldenRoot>\c1_01-success\normalized.json` present. Without `-Unattended`
   its parameters are refused.
2. After the menu directory: the menu script `evidence\menu-input.txt`
   (default `2`, `4`, `1`, `5`: CONTINUE, MISSION (Rock & Roll), after the
   `-X3` results HAUPTMENUE, QUIT; `-MenuInput` gives another), passed as
   es2menu's `--input` at the end of `ES2_LAUNCH_SHELL`.
3. DBSIM's settings as `run-tick-log-mission.ps1` applies them for the suite:
   `configure-tick-log.ps1 -TickLogPath <session>\run\c1_01-success.jsonl
   -VirtualTimeStepMs 47 -VirtualTimePace 0 -VirtualTimeIntraFrame 1
   -InputMode replay -InputJournal input-scripts\c1_01-success.jsonl -Watch
   <watch set> -TickTarget 8000 -HiddenWindow -Hooks <-Hooks>
   [-PreparedProxy] -Apply`, with the clone lock token of this script in
   `ES2_CLONE_LOCK_TOKEN`.
4. The launcher (`start-own-launcher.ps1 -AllowOracleConfig`) without
   `ES2_LAUNCH_SHELL_CONSOLE` (the menu runs with `CREATE_NO_WINDOW`) and with
   `ES2_LAUNCH_NO_UI=1`. DBSIM's audio session is set to `-VolumePercent`
   (default 0) by the oracle's `session_volume` helper once DBSIM appears.
   After `-TimeoutSeconds` (default 900) the run's own `ES2LAUNCH.EXE` and
   then its game processes are stopped: only processes whose executable lies
   directly in the clone and that started after the run began, PID, path and
   start time read again right before the stop
   (`evidence\timeout-stops.txt`); anything else is left alone and named.
5. Evidence: `run\c1_01-success-output\` holds what DBSIM returned (the
   campaign's `mission-000\dbsim\results.dat` and `mission.var`), as the
   runner's `<suite>-output`.
6. After the restore and the chain verify (`-NoVerify` skips it), the verdict
   `evidence\unattended.json`:
   - the launcher events are exactly `start shell-replacement -X1`, `exit
     shell-replacement 2`, `start dbsim.EXE dummy -eggplant -X2 -R0 -G`, `exit
     dbsim.EXE 3`, `start shell-replacement -X3`, `exit shell-replacement 0`,
     `end 0` (the state is read after `dummy -eggplant` since a path in the
     prefix may contain `-x1`);
   - `launch_kind` = `prepared` or `from_slot`; a prepared `missions[0]` has
     `files` (and `launch_files`, if any) with the pinned C1_01 hashes of
     `reference_pins.py`; a from_slot record's `launch_files` must exist and carry
     the pinned hashes (the files DBSIM was handed), its `files` are not
     judged, and the printed line says `launch files pinned (slot 10's
     copies)`;
   - `oracle_regression.py normalize` of the tick log with the outputs, then
     `compare` with the golden (as `run-oracle-regression.ps1 -Mode check`):
     identical; then the raw tick log is deleted (normalized form and
     comparison stay);
   - the verify line of `chain_real_test` (the one with `"directory"`; a
     crash after the campaign line is "no verify line"): bazel exit 0,
     `results_steps` at least 1, `preparations_equal` = `preparations`.

   Printed as `UNATTENDED PASSED|FAILED: launcher sequence …; launch files …;
   DBSIM … to the c1_01-success golden; chain verify P of Q preparations
   equal, R results step(s)`. Exit codes: the session's 0 to 4, plus 5 (timed
   out, processes stopped) and 6 (a judgement failed).

**`check.ps1 -OwnShell`** (needs `-Live`; `-OwnShellInstall` picks the clone,
which must be one of `-InstallName`): inside the live stage, after the runner
and before the prepared proxy's binaries are removed and the runtime is
restored, `start-own-menu.ps1 -Unattended` runs in a PowerShell process of
its own with the run's prepared proxy, hook selection (`-Hooks`),
`-OracleDataRoot`, `-BazelOutputBase` and `-Python`; its session is
`<EvidenceRoot>\own-shell`, its output `own-shell.log`. The stage `own-shell`
follows `live`: PASSED when the script exits 0 and the verdict is PASSED; NOT
RUN when the live stage did not reach its runner; with `-Overlap` it is
DISCARDED like `live` when another stage failed. The live stage's duration
includes it.

**Limits.** Only `c1_01-success` (the only suite whose mission the shell
prepares from the clone's `GAME_R.SAV`); the other suites start from launch
sets the shell does not produce. The clone keeps the oracle configuration
and `ES2LAUNCH.EXE` afterwards, as after any oracle run.

**Live** (`timing-r111-p1`, text menu): the launcher sequence is the
expected one and DBSIM identical to the golden; when the clone's slot 10
holds its copies, `missions[0]` is a from_slot record. The verdict's
from_slot rules and the verify's `results_rng` handling above exist for that
case. **Open:** a live PASSED run with the from_slot record (expected:
`launch_kind` `from_slot`, `mec-hashes.txt` with the pinned player.mec hash
for `mission-000\launch\player.mec`, `slot_launch` 1 in the verify line,
chain verify 1 of 1 preparations equal, 1 results step).

**Test** (`//tools/reference-game/setup:start_own_menu_test`, unattended
cases): `-DryRun` (the configure arguments, `--input`, `-AllowOracleConfig`,
the judgement step; nothing created); the refusals (not a measurement clone,
`-Runs 2`, `-Seed 5`, an existing session directory, a missing menu script, a
missing golden, an unattended parameter without `-Unattended`); a whole run
with a fake `configure-tick-log.ps1` (records its arguments and that the
inherited lock token is the holder's), a fixture launcher that writes the
tick log and the campaign, a golden made by the real `oracle_regression.py`
and a fake bazel for the verify line: PASSED, raw tick log removed, restore;
a different golden and other launch files (exit 6, both problems, raw log
kept); DBSIM ending with 1 and a failed verify (exit 4); no verify line (exit
6); a stalled launcher and DBSIM (mock processes) stopped after a 5 s
timeout, launcher first (exit 5, restored); the from_slot PASSED run, without
`launch_files`, with a wrong launched `player.mec`, the campaign line only.
Every process of these cases lives in the case's own temporary installation
and the script only looks at processes whose executable lies directly in
that installation, so stand-ins of parallel tests are invisible to it.
`//tools:check_script_test` `test_own_shell` (mock `start-own-menu.ps1`): no
stage without `-OwnShell`; arguments, proxy still present, runner done and
the clone still on the own runtime when it starts; PASSED, FAILED with
problems, no verdict, exit 2 with a PASSED verdict, NOT RUN, DISCARDED under
`-Overlap`, the three refusals. Fixtures are not live validation.

### Unattended chain through es2shell

The unattended chain with the graphical shell in headless mode as the
program `es2launch` starts through `ES2_LAUNCH_SHELL`.

```powershell
pwsh -File tools\reference-game\setup\start-own-menu.ps1 -Unattended -Shell gui -InstallName timing-r111-p1
tools\check.ps1 -Live -OwnShell [-OwnShellShell text|gui|both]   # default both
```

What `-Shell gui` changes (everything else is the unattended run's;
`-Shell text` stays the default; `-Shell vshell` and `-MenuInput` with
`-Shell gui` are refused):

1. The build is `//shell:es2shell` with `SDL3.dll`; the menu directory is set
   up with `--menu-exe es2shell.exe`.
2. `ES2_LAUNCH_SHELL` = `"<es2shell.exe>" --menu "<dir>" --headless --script
   "<evidence>\shell-script-x{X}.txt" --report "<evidence>\shell-report-x{X}.txt"`.
   es2launch hands every start of the run the same command line, so es2shell
   replaces `{X}` in `--script` and `--report` by the `-X` entry mode of the
   start (`shell/src/gui/main.c`, `per_entry_path`; a path without `{X}` is
   used as given). The `-X1` start reads `shell-script-x1.txt`, the `-X3`
   results start `shell-script-x3.txt`; any other mode finds no script,
   reports `error 0 no script` and fails the verdict. The `{X}` substitution
   is a test option of our program, not VSHELL behaviour; VSHELL's argv is
   unchanged.
3. The scripts (fixed, written into the evidence; the controls of
   `gui_route_real_test`'s CONTINUE and results cases): `-X1`: `step 200`,
   `expect screen=0`, `click main_menu.09` (CONTINUE GAME), `expect screen=5
   game=1`, `click service_bay.19` twice (MISSION, page 1), `click
   mission.27` (Rock & Roll); `-X3`: `step 20`, `expect screen=5 page=4`,
   `click service_bay.05` (MAIN MENU), `expect screen=0`, `click
   main_menu.13` (QUIT). This is the graphical counterpart of the text
   menu's `2`, `4` | `1`, `5`; CONTINUE GAME reaches the bay with or without
   slot 10's copies.
4. Evidence: both scripts and both reports in `evidence\`, es2shell's
   `shell.log` in `private\`.
5. The verdict adds `shell` = `gui`, `shell_reports` (`x1`, `x3`: the report's
   `end` line, `exit`, `errors`, the first five `error` lines) and
   `shell_final` (the `-X3` end line, the headless report's final state). A
   problem when a report has no `end` line, a script error, or an exit other
   than 2 (`-X1`, DBSIM) and 0 (`-X3`, quit). Printed as the text run's line
   plus `; es2shell headless X1 exit 2, X3 exit 0, 0 script error(s)`. The
   text run's verdict has `shell` = `text` and no headless part.

**`check.ps1 -OwnShell -OwnShellShell text|gui|both`** (default `both`;
refused without `-OwnShell`, and without `-Live` as the other live
parameters): the own-shell stage runs `start-own-menu.ps1 -Unattended -Shell
<s>` for each shell in turn (text first) on the same clone with the same
prepared proxy, hooks and runtime. The text run keeps the session
`<EvidenceRoot>\own-shell` and `own-shell.log`; the gui run uses
`own-shell-gui` and `own-shell-gui.log`. The stage has one sub-verdict per
shell (`text PASSED: ... / gui PASSED: ...; headless X1 exit 2, X3 exit 0, 0
script error(s)`; details prefixed `text:` / `gui:`) and is PASSED only when
each sub-run exited 0 with a PASSED verdict. NOT RUN and DISCARDED as for the
text run.

**Test.** `//tools/reference-game/setup:start_own_menu_test`: `-DryRun -Shell
gui` (the headless command line and judgement listed, nothing created); the
refusals of `-Shell vshell` and `-MenuInput` with `-Shell gui`; a whole gui
run with the fixture launcher (it checks the `--headless --script ...
--report ...` tail with `{X}` and that both scripts exist, writes the two
reports): PASSED, the printed line, `shell_reports`, `shell_final`, the
scripts in the evidence, `shell.log` collected, `session.json` `shell` =
`gui`; a script error in the `-X3` report and a missing `-X3` report (exit 6,
restored, DBSIM still identical); the text verdict has no headless part.
`//tools:check_script_test` `test_own_shell`: `-OwnShellShell text`, `gui`,
the default `both` (two runs, sessions and logs, same proxy, hooks and
runtime), text passing and gui failing (stage FAILED, both sub-verdicts
named), the three refusals. The `{X}` replacement is checked by hand on the
built `es2shell.exe` (pattern mode, `-X3`: `r-x3.txt` written). Not covered
live.

### DBSIM's full screen in interactive sessions

With prefs byte 6 = 1 ([cfg.md](../formats/cfg.md); the shell's Full Screen or Alt+Enter
writes it) the original DBSIM takes its DirectDraw branch and, on this machine, faults
with 0xc0000005 at `0x4654a8` (`mov edx,[eax]` with the primary surface `[0x4d3714]`
NULL; reached when `[0x4d255e]` ≠ 0) unless the proxy replaces the branch with
`[presentation] directdraw=windowed`
([presentation.md](presentation.md#capture-own-gdi-presenter-windowed-directdraw)), the
documented platform deviation. `rebuild-r111` has such a proxy and an `es2-oracle.ini`
with `presenter=gdi` and `directdraw=windowed`; `calibration-r111`'s proxy has
neither option.

`start-own-menu.ps1` (interactive sessions only):

- It warns (also in `-DryRun` and into `evidence\full-screen-warning.txt`) when the
  clone has no proxy, a proxy without the options (its bytes lack the ASCII strings
  `directdraw` and `presenter`) or `directdraw` other than `windowed`; nothing is
  refused.
- `-WindowedDirectDraw` sets `directdraw=windowed` and `presenter=gdi` in
  `[presentation]` after backing up `es2-oracle.ini` (recorded in `restore.json`),
  keeping every other line and the encoding (a new file is UTF-16 LE); the restore
  (`finally`, `-RestoreOnly`) puts the bytes back or deletes a created file. Refused
  with a proxy without the options and with `-Unattended`.

`//tools/reference-game/setup:start_own_menu_test` covers the warnings, the refusals
and the byte-exact restores on synthetic proxies and inis.

**Open.** Whether the GDI presenter shows DBSIM full screen or windowed after byte 6 = 1
is not re-checked live.

## Graphical shell

`es2shell.exe` (`//shell:es2shell`, `shell/src/gui/main.c`, GUI subsystem) is
the own graphical shell that `es2launch` starts through `ES2_LAUNCH_SHELL`
instead of `VSHELL.EXE`. Every screen behaviour is in `vshell_ui.md`
sections 18 to 39, `vshell_presentation.md` and `vshell_widgets.md`; this
section says how the program is wired.

### Wiring

- **Command line.** `es2shell --menu DIR [--headless --script FILE --report
  FILE] [--fullscreen] [--environment FILE] <VSHELL argv>`. `--environment`
  gives the environment block (`es2_os_environment_load`, as es2sim's) the
  shell reads `ES2_MENU_*` and `ES2_SHELL_*` from instead of its process
  environment; a file that is not such a block ends the program with 64. The menu directory and its `menu.ini`
  are the text menu's (`es2menu_backend.py setup --menu-exe es2shell.exe`
  writes the same file; `ES2_LAUNCH_SHELL` = `"<es2shell.exe>" --menu
  "<dir>"`, without `ES2_LAUNCH_SHELL_CONSOLE`). The VSHELL argv goes through
  `es2_gui_parse_args`: `-X` the entry mode, `-g`/`-f` the language, `-s` the
  sound flag, `-a` the movie flag, `-@` the cheat byte. Preference byte 6 (or
  `--fullscreen`) opens the window full screen. Without `--menu` the program
  is the test pattern (`//shell:gui_launch_test`).
- **Language.** Without a language letter the language word is prefs byte
  +0x2b (`0x4079b7`; WinMain loads `prefs.cfg` at `0x406b28`, before the
  defaults `0x40796c(0)` at `0x401b15`); 0, 1, 2 give `ENG`, `FRE`, `GER`, any
  other value no directory prefix (`0x408bb5`, the switch at
  `0x408c07..0x408c17`), so the language files are not found, as in VSHELL;
  `shell.log` names the language. The movie player then uses English.
- **One host for both presenters** (`shell/src/host.c`, `//shell:host`):
  `menu.ini`, the primary-installation and game-directory checks, the
  start-up CD check, ESTEXT, the flow's host callbacks over `engine_io`
  (prefs writes, save and load of the slots, new career, campaign, launch,
  rebalance, results path, next preparation, replay, the online manual with
  its assertion), the practice tables and launch. What the host reports goes
  to the presenter's `say` (the console and `menu.log` for `es2menu`,
  `shell.log` in the menu directory for `es2shell`); the results path's
  screen part, the DEBUG dialog of `-@` and the assertion box are presenter
  callbacks. Both presenters therefore write the same files from the same
  choices (`//shell:gui_route_real_test`).
- **Presenter** (`shell/src/gui/app.c`, `//shell:gui_app`): the toolkit on the
  window shell; every screen built in the construction order of
  vshell_ui.md (not the dead pilot-name screen); resources from the
  installation's `VOL\ZONES.VOL`, `SHELL0.VOL`, `SHELL1.VOL`, `LANG0.VOL` in
  VSHELL's priority order (else from `menu.ini`'s `cd_root\VOL`), language
  files looked up with backslashes: `dfn\font2.dfn` for `0x46ddfc` and
  `0x46de00`, `dfn\black.dfn` for `0x46de04`, DBM files and DBA members
  decoded once, the DPL palettes of the table `0x46de14` for `ui->palette`,
  `<lang>\ESTEXT.BIN`, `WPN_DESC.BIN`, `WEAPONS.BIN`, `WPN_INFO.BIN`,
  `CAMPAIGN.STR`. The literals, the squad panel's band tables (`0x47665c`,
  `0x476666`) and the editor tables (`0x47665c`, `0x46f868`, `0x46f87a`,
  `0x476a4c`) are read at run time from the hash-checked `VER95\VSHELL.EXE`;
  nothing is embedded.
- **The flow's state on screen.** The screens feed the flow and call their
  `changed` hooks; after the drain of every main-loop pass (the movie hook)
  the presenter sets the shell's quit flag from the flow's, shows the flow's
  box (`es2_boxes_sync`) and shows the flow's screen when no screen handler
  has shown it already (`0x431718`, `0x43c29a`, `0x43a19c`, `0x436d45`,
  `0x44c322`, `0x43b7f2(8)` + `0x43b758`). The bay's `screen` hook shows and
  leaves WEAPONS, REPAIR, BUILD, ARMORY, CREW and the mission screen
  (`es2_mission_screen_show(flow.page)`); its `selected` hook routes the
  squad panel's selection to the editors, `repair_grid` to REPAIR. The
  flow's load and new game clear the bay selection
  (`es2_service_bay_game_loaded`). INSTANT ACTION and PRACTICE MISSIONS use
  the main menu's `entry` stages (`es2_practice_screen_show`, then the mode
  store `0x40ec56(0)`; INSTANT ACTION the practice writes, then the launch).
  A `-X3`/`-X4` start with advance 2 shows the bay, then
  `es2_mission_screen_open(4, 1)` and the statistics `0x40f904`.
- **Squad panel.** The `panel` hook draws the Herc pictures
  (`0x41547f`/`0x4146cd`, decoded once and cached), `host.squad`'s
  `0x414cbe` REPAIR's diagram, WEAPONS' `slot_shown` the marker `0x415bff`
  (vshell_ui.md section 36); the condition is `0x4121f8(record + 8, record +
  0x4c)` (`es2_shell_record_condition`, equal in vitro). VSHELL never shows
  the hot spots of `gam\arm_hots.dat`/`gam\rpr_hots.dat` (`0x43cfa5`), so
  WEAPONS selects slots only with its arrows. After a Build the panel shows
  the new Herc in the same frame; ARMORY's cost column is filled without a
  loaded game (start-up table); ARMORY's German "Erf. aterial" is VSHELL's
  own clipping (vshell_ui.md section 38).
- **Editors.** REPAIR and CREW change the engine's memory state in place (the
  same SAV form as the text menu's bay); BUILD and ARMORY keep it in their
  game struct, which mirrors the engine's buffer around every call. The
  double leave: the flow's `host.rebalance` runs the leave `0x412a37` of
  WEAPONS, REPAIR and BUILD; BUILD and ARMORY then open from the bay screen
  -1 (as REPAIR's show does), so `0x412a37` runs once.
- **Evidence log.** WEAPONS mounts and guidance stores are `bay` steps (kinds
  1 and 3, which `chain_real_test`'s `bay_steps` replays as `0x411b10` and
  `0x43e346`): the screen's operation is read back (bay, slot, the slot's new
  item or the mode), the state before the pass is set back and `eio_bay`
  applies and records it; the result must equal the screen's state byte for
  byte, else `shell.log` says "not recorded" (the step `eio_bay` wrote stays
  in the log then).
- **Main-loop hooks.** Timer: the toolkit's pass and the audio service.
  Movie: main-loop step 5 has run (`0x46c280`), the flow's state, then the
  movie player `0x41e98c(1)` over the movie queue. Map: step 6, the mission
  map's fly-over (below), then the Quit-box repaint (step 10). System: Alt+F4
  the Quit box; leaving or toggling full screen stores and writes prefs byte
  6. Focus: the sound focus.
- **Mission map.** One `es2_mission_map_globals` per process (the generator
  `0x48110a` from the table `0x47f850`, `mis_icon.dba`'s frames); the map
  object `0x40e75f` is built after a load, a new career's and the results
  path's preparation and a campaign prepared from its slot, from
  `DATA\script.dat`, `player.mec`, `MISSION.STR`, `data\mforms.dat`,
  `data\mat0.dat` (the game directory's, else the CD's), the zone's
  `dat\zone<N>.dat`/`dba\zone<N>.dba` and the executable's cosine
  (`0x47b480`) and arc-tangent (`0x47aaf4`) tables. When the mission screen
  sets its request (`map_pending`, page 1), step 6 (`0x4019c6`) applies the
  palette request, flushes the key ring, sets `0x46c26c` and runs the
  fly-over step, a present of the map rectangle and the pump until it ends;
  Esc or Space in the key ring or a click (`0x46c270`) skip. The six arrows
  run 1 to 4 steps by the button's repeat counter, each presented. The
  fly-over is a blocking loop inside step 6, as VSHELL's; its tick is the
  platform clock in 16 ms units (headless: 20 ms per pump).
- **Movies.** The player's control flow with its actions on the screens'
  windows (show, hide and repaint of the six VSHELL objects, the bay
  buttons, the mission teardown, the theatre step and hide, palettes, the CD
  prompt as a box under the CD policy). In the window the MCI actions are the
  player with the SDL window's native handle as parent
  (`es2_gui_window_native`, `gui_sdl`); headless the movie's path is written
  to `shell.log` and the movie counts as ended. While a movie plays the host
  pumps SDL's queue through the input path while the movie flag is still set
  (button messages dropped as VSHELL's window procedure does) and empties the
  key ring (VSHELL's loop pops every key), so the skipping click does not
  reach the screen behind; a button down on the main window or any window
  inside it skips, and the mouse capture is set back after each message
  (SDL's auto-capture releases it on a button up). The player's pump between
  movies and the wait for the focus are the shell's pump (VSHELL's
  `0x406328` into its window procedure), so the focus word changes while the
  player waits (vshell_ui.md section 37).
- **Sounds.** The start-up with the sound flag (prefs byte 5 flipped and
  written as VSHELL does), the SDL3 device in the window, a null device (no
  output) headless that still loads the samples, so plays are logged and
  tested. The samples come from the installation's `VOL\SHLSOUND.VOL`, else
  the CD's, as VSHELL's (audio.md); missing: a `shell.log` line and silence,
  as VSHELL's failed `SFX::cache`. `shell.log` records the start-up (sound
  flag, active, music file, device, samples loaded) and every play and
  stop-all. Button clicks (`ui->click_sound`), the bay bar, the main menu's
  background callback, the Preferences ramps, the movie player's focus and
  ramps. The music fades run the shell's pump on every pass as VSHELL's
  `0x42f894`/`0x42f7f0` run `0x406328` (audio.md "The fades pump messages"): paints, focus,
  close and the window's input path work during the 1.1 to 1.6 s; clicks and
  keys are queued and reach the screen after the fade; the session's end
  ends the fade and the shell.
- **Notice 0x2f.** The main menu's first notice for prefs byte 0x2f shows
  error.str's message 6 and caption 2 (the game directory's `error.str`,
  else the CD's `ERROR.STR`; groups 0 and 1 as VSHELL's start-up reader keeps
  them; an unfilled slot, such as message 6 of the English file, is an empty
  text).
- **DEBUG dialog (`-@`).** The practice launch's DEBUG dialog waits after
  Begin Mission's new game (no posted click), and also at the end of a mode-1
  new career (`0x41308e`) and on the results path's advance 2 (`0x40f647`) in
  both presenters: the flow keeps the game (final word 2, page 0 or 4) and
  waits (`flow.debug`); Use Default or ACCEPT (typed `msn\<name>.msn`, the
  CAREER position kept) prepare through the host's `debug_prepare`
  (`es2_host_practice_debug_answer` for the practice launch), then the bay
  (results path: the mission screen's page 4). Mode 1 never launches from the
  dialog (`0x44dc9c..0x44dcd9`; vshell_ui.md section 39). A refused name or a
  failed preparation returns to the main menu (own).
- **Exit.** The flow's exit code (2 DBSIM, 5 demo, 0), 1 after the online
  manual's assertion; the loop end saves slot 10 (11 in mode 0) when a game
  is loaded, also when the window is closed.
- **Headless mode** (`--headless --script --report`): `main.c`'s platform
  delivers the script's messages through the window-procedure functions on a
  virtual clock of 20 ms per pass. Commands: `step`, `click <screen>.<NN>`
  (the click at the centre of the window built for the control), `move`,
  `ldown`, `lup`, `rdown`, `rup`, `key`, `type`, `focus`, `close`, `state`,
  `expect FIELD=VALUE`, `snap` (frame and table hashes), `dump FILE` (an
  8-bit BMP for a look in a scratch directory; never for the repository). The
  end of the script closes the window. Movies are logged and count as ended;
  no sound output.

### Deviations and open points

- The movie window is placed through the presenter's layout (the 640 × 480
  frame scaled by the largest integer that fits the client area, or shrunk,
  and centred), so in full screen and in any client larger or smaller than
  640 × 480 it covers the same part of the picture; VSHELL switched the
  display to 640 × 480 instead (platform deviation).
- REPAIR, BUILD, ARMORY and CREW operations are not recorded in the evidence
  log: the text menu has no such steps and the verify has no replay kinds for
  them (the editors' original functions would need new `es2shellprep` bay
  operations).
- `0x414a3c`'s data-error assertion is written to `shell.log`; the process
  does not end with exit code 1 (**open**).
- VSHELL's movie paths for a language value other than 0, 1, 2 are not
  checked (**open**).
- The map's fly-over is not built for a practice launch (the process ends at
  once).
- The window, the movies and the sounds are checked by hand; the window
  ran only on SDL's `dummy` video and `disk` audio drivers in tests.

### Verification

- `//shell:gui_route_real_test` (needs `ES2_DATA_ROOT`): es2shell headless
  against es2menu in temporary game directories with the same `menu.ini`
  (seed 71). Route A (new career TESTPILOT skill 2; WEAPONS: slot 1 emptied,
  a stocked weapon mounted; MISSION page 0 with its movies, MISSION again
  page 1; Rock & Roll): exit code 2, two mounts, the four launch files and
  every SAV file equal; with `chain_dir` es2shell's bay steps and their SAVs
  equal es2menu's. Saved games: run 1 (new career, the bay's SAVE, row 3
  named "ALPHA 7", ACCEPT, EXIT, MAIN MENU, QUIT) and run 2 (SAVE/RESTORE,
  RESTORE row 3, MISSION twice, Rock & Roll, exit 2): the launch files and
  the SAV directory equal after each run. Every bar screen opened and left,
  MAIN MENU, QUIT, then CONTINUE GAME, MISSION, Rock & Roll: equal. The
  results path: CONTINUE of the pinned `GAME_R.SAV` (C1_01, seed 71), Rock &
  Roll, the recorded c1_01-success output of DBSIM into `DATA`, the `-X3`
  start (advance 2: the bay with the mission screen's page 4), MAIN MENU,
  QUIT: equal after both processes. The language from prefs +0x2b (five
  cases), the 0x2f notice (error.str with and without message 6), the `-@`
  DEBUG dialog (practice: Use Default and ACCEPT of `TRAIN8` against
  es2menu's files, no dialog without `-@`; new career answered with Use
  Default, ACCEPT `C1^01` (Use Default's launch files) and `C1^02`, then Rock
  & Roll; the `-X3` results path's dialog answered with Use Default and
  `C1^02`): es2shell's DATA and SAV files equal es2menu's. Without data: no
  `menu.ini` and another game directory end with 0 and write nothing.
  Mutations caught: WEAPONS not shown, the flow's screen not shown, slot 10's
  flag not read for CONTINUE, the language directories, the notice's message
  index, the DEBUG hold, the recorded mount value.
- `//shell:gui_sound_real_test` (needs `ES2_DATA_ROOT`): headless `-X1 -G`
  (the four samples loaded, the music chosen by prefs byte 5 looping from the
  focus gain, byte 5 flipped and written; lswitch2 on the main menu, gm_69 at
  NEW CAREER, bptlt2 at WEAPONS, in that order, none "not loaded"), `-s` (no
  sound object, no play, byte 5 kept), and the window on SDL's `dummy` video
  and `disk` audio drivers (`-X6 -a` and `-X3 -a`, each ended after 6 s: the
  device open, the fade-in at 100, the audio file not silent). One mutation (the samples
  looked up in the resource VFS) is caught.
- `//shell:app_sound_test` (virtual clock): the fades and the pump.
- `//shell:gui_bay_real_test`: the Build refresh, ARMORY's cost column, the
  archive order.
- `//shell:flow_test` `debug_dialog`; `//shell:menu_real_test`, `menu_test`,
  `engine_test` and `flow_test` cover the shared host.
- A headless smoke over REPAIR, BUILD, ARMORY, CREW and WEAPONS (no crash,
  the screen word per bar button; frames looked at in a scratch directory,
  not compared).

### The own shell as the installation's default

`configure-shell.ps1 -Apply` deploys `ES2LAUNCH.EXE` and `ES2SHELL\`
(`es2shell.exe`, `SDL3.dll`, `menu.ini`) into a portable installation and
records their hashes in `own-shell-manifest.json`; `start-native.ps1` then
starts the own launcher with `ES2_LAUNCH_SHELL = "<install>\ES2SHELL\es2shell.exe"
--menu "<install>\ES2SHELL"` instead of `ES.EXE`, so neither `ES.EXE` nor
`VSHELL.EXE` runs (both stay in place; `-Shell vshell` starts them as
before). The launcher's VSHELL argv goes to es2shell, exit code 2 starts
DBSIM, 5 DBSIM `-D`, anything else ends the loop. Deployed on `rebuild-r111`;
details in [setup.md](../reference-game/setup.md#own-shell-as-the-default).

## DBSIM's process shell, start-up and exit

Everything DBSIM does from WinMain to the mission-load call and from the end
of the main loop to the process exit. The main loop, pump, window procedure,
limiter and clock are in
[main_loop.md](main_loop.md#the-main-loop-pump-window-procedure-pause-limiter-and-clock),
the modal dialogs in [main_loop.md](main_loop.md#the-modal-dialogs). The own
code: [DBSIM's start-up as own code](#dbsims-start-up-as-own-code), the loop
tail and the DIB section in
[main_loop.md](main_loop.md#the-main-loop-pump-window-procedure-limiter-and-clock-as-own-code)
and own_process.md
["The main loop, the journal and the exit"](../reference-game/own_process.md#the-main-loop-the-journal-and-the-exit),
the mission end as the hook `mission_end`
([below](#the-mission-end-0x461ff0-as-own-code)).

### Order of the process

| # | Where | What | Compared by the oracle |
|---|---|---|---|
| 1 | c0w32 entry `0x401000` → CW3220 `__startup` | 65 init records, then WinMain ([rtl_imports.md](../reference-game/rtl_imports.md)) | after-load snapshot only |
| 2 | WinMain `0x46538c` | argv, `[0x4d36c4]` ← hInstance, class, `prefs_video(0)`, early parser, window and DIB, DirectDraw switch, wait cursor | snapshot (globals, heap) |
| 3 | game main `0x45f160` (from `0x465462`) | `drive.cfg`, root strings, exit code ← 0, main parser, stage 0, stream buffer size, VOL registration, stage 1, `error.str`, the `-eggplant` check, memory check, arena | snapshot |
| 4 | `0x461608` (from `0x45f42b`) | mission load ([mission_load.md](mission_load.md)); registers the abort notifier `0x461304` (`0x46162b`) and runs timing init `0x467840` (`0x4617c3`) | snapshot after load |
| 5 | `0x45f430..0x45f4b3` | stage 3, arrow cursor, word `0x4d2564` ← 1, main loop | tick log, watches |
| 6 | `0x45f4c5..0x45f568` | loop tail: sound release, frame clear, prefs byte 6, tape-mode deletes, mission end `0x461ff0` | return files, exit code |
| 7 | WinMain `0x46546a..0x465489` | `PostQuitMessage(0)`, pump until `WM_QUIT`, DirectDraw release `0x466730`, return `[0x4d27b8]` | exit code |
| 8 | CW3220 `exit` | atexit functions, the 26 exit records, stream flush, `ExitProcess` ([rtl_imports.md](../reference-game/rtl_imports.md#exit-paths)) | exit code |

### WinMain `0x46538c` (`0x46538c..0x465489`)

`int __stdcall WinMain(hInstance, hPrevInstance, lpCmdLine, nShowCmd)`,
`ret 0x10`; the frame's `0x6c` bytes are the argv array (27 slots at
`[ebp-0x6c]`).

1. `argc = 0x4652f8(lpCmdLine, argv)`; `[0x4d36c4]` ← hInstance.
2. Only when hPrevInstance = 0 (always under Win32): `0x4650e8(hInstance)`
   registers the class; a zero result returns 0 from WinMain at once (no
   message).
3. `prefs_video(0)` (`0x45e510`, below), then the early parser
   `0x45e6cc(argc, argv)`.
4. `0x465158(hInstance, nShowCmd)` creates the window; on 0:
   `LoadStringA(hInstance, 1000, 0x4d3614, 32)`,
   `LoadStringA(hInstance, 1001, 0x4d3634, 128)`,
   `MessageBoxA(0, 0x4d3634, 0x4d3614, MB_ICONEXCLAMATION)`, return 0.
5. If dword `[0x4d255e]` ≠ 0 (DirectDraw requested by prefs byte 6, `-Z1`,
   or a missing `prefs.cfg`: init record 46's 1 stands): `[0x4d255e]` ← 0,
   the toggle `0x4667c8` (which switches to DirectDraw and sets it to 1
   again), then `0x46548c` (clear the frame buffer and show it).
6. `LoadCursorA(0, IDC_WAIT)`; a nonzero handle goes to `0x464c68`
   (cursor handle `[0x4d3720]`, which the pump sets after every drain).
7. Game main `0x45f160(argc, argv)`.
8. `PostQuitMessage(0)`; `0x464c78()` repeated until it returns nonzero
   (it does when it removes `WM_QUIT`); `0x466730()`: a final `0x46548c`
   when `[0x4d3710]` is set, then the back buffer `[0x4d3718]`, primary
   `[0x4d3714]`, palette `[0x4d371c]` and DirectDraw object `[0x4d3710]`
   released through slot `+8` and cleared, then `ClipCursor` to the whole
   screen; return EAX = `[0x4d27b8]`.

**Command line split `0x4652f8(cmd, argv)`** (cdecl, returns argc):
`argv[0]` ← the literal `"dbsim"` (`0x4a0876`); then for every run of
characters other than space: a block of (run length + 2) bytes from
`0x4774f0` (the game's allocator, rtl_imports.md), the run copied and
terminated. Spaces separate, runs of spaces are skipped; no quoting, no
tab handling and **no bound**: more than 26 arguments write past the
array into WinMain's frame (quirk; the launcher passes at most five).

**Window class `0x4650e8(hInstance)`** (cdecl): `WNDCLASS` on the stack:
style 0, `lpfnWndProc` = `0x466034` (store at `0x4650fb`), no extra
bytes, `LoadIconA(hInstance, "es2icon")`, `LoadCursorA(0, IDC_ARROW)`, no
brush, menu `MAKEINTRESOURCE(1)`, class name `"ES2"`;
`LoadLibraryA("BWCC32.DLL")` (handle discarded; the DLL is never used,
[dll_imports.md](../reference-game/dll_imports.md)); returns
`RegisterClassA`'s atom zero-extended.

**Window `0x465158(hInstance, nShowCmd)`** (cdecl, returns 1 or 0):
`SetRect(r, 0, 0, [0x4d253e], [0x4d2542])`, `AdjustWindowRectEx(r,
0xce0000, FALSE, 0)`, `0x464c10(left, top, right, bottom, 0, 0x4d36cc)`
(stdcall, `ret 0x18`: the rectangle centred on the screen into `0x4d36cc`,
which holds x, y, width, height: `0x464c10` calls `SetRect(r, x, y, w, h)`,
`0x464c52..0x464c5a`), `0x464cc4()` (display record `0x4d3020`:
BITMAPINFOHEADER size `0x28`, DIB width/height, 1 plane, 8 bit). Size: the
game size, or the screen size when `[0x4d255e]` ≠ 0 (then also `[0x4d36fc]`
← 1, `[0x4d3700]` ← 1). x = (screen width − width) / 2 − `SM_CXFRAME`;
y = (screen height − height) / 2 − `SM_CYFRAME` − `SM_CYCAPTION`; outer
width = width + 2 × `SM_CXFRAME`, outer height = height + 2 ×
`SM_CXFRAME` + `SM_CYCAPTION` (the horizontal frame width is used
vertically: quirk). `[0x4d36f4]`/`[0x4d36f8]` ← screen size.
`CreateWindowExA(0, "ES2", "EarthSiege II", 0x100e0000, x, y, w, h, 0, 0,
hInstance, 0)` → `[0x4d3610]`; in DirectDraw mode `SetWindowPos(hwnd,
HWND_TOPMOST, x, y, w, h, 0x108)`. With a window: `0x464d44(hInstance)`
(DIB section, below), `ShowWindow(nShowCmd)`, `UpdateWindow` (a
synchronous `WM_PAINT`, which presents through `0x464a14` in GDI mode),
result 1. Without: `0x4650d0()`, result 0.

**DIB section `0x464d44`** (the allocation here, the rest from
[flyer.md](flyer.md#function-table) and main_loop.md): the memory DC
`[0x4d3034]`, the system palette read, then `CreateDIBSection([0x4d3034],
0x4d304c, DIB_PAL_COLORS, &[0x4d3028], 0, 0)` → `[0x4d3038]`
(`0x464e13..0x464e26`): a **top-down** 8-bit DIB of the DIB size (`biHeight`
= −height at `0x4d3054`). A failure only shows a message box (`0x464e40`)
and goes on. The bits pointer `[0x4d3028]` is OS memory, not heap; it is
copied into a 14-byte descriptor from `0x47749c(0xe)` (`[0x4d3020]`: +0
height, +2 width, +4 bits per pixel 8, +6 size, +0xa the bits pointer) that
the frame-buffer objects use: the guest objects that point into OS memory
start here. It then builds the graphics library's frame-buffer objects (a
`new(0xa0)` object through `0x480f58`, `0x481028`, `0x481224`; abort-notify
and atexit registrations `0x480f30`/`0x480f38`). Own code:
`sim/src/frame_buffer.c` (`es2_sim_dib_section`), the bits a host region
(`es2sim`: 1280 × 960 bytes; main_loop.md, in vitro).

**DirectDraw switch `0x4667c8`** (entry; the DirectDraw body is
in flyer.md): with `[0x4d255e]` = 0 it records the window rectangle (unless
`[0x4d3700]`), sets `[0x4d255e]` ← 1, creates DirectDraw (`0x46658c`) and
the 8-bit mode (`0x4665c4(w, h, 8)`); if either fails: `0x4650d0()`,
`PostQuitMessage(0)` and return. With `[0x4d255e]` ≠ 0 it switches back to
the window (`0x46693a..`). Callers: WinMain, `0x4669b4`/`0x4669c4` (back
to GDI before a message box or WinHelp), the playback key check
`0x45fe6c` (`0x45fea9`, command code `0x21c`, i.e. Alt with scan code
`0x1c`: Alt+Enter, hypothesis for the key name) and a cockpit handler
(`0x4345da`). Headless the switch has no compared effect except
`[0x4d255e]`, which the loop tail writes to prefs byte 6 (below).

### Option parsers `0x45e6cc` and `0x45e758`

**Early parser `0x45e6cc(argc, argv)`** (cdecl, from WinMain, before the
window): arguments 1..argc−1, only those whose first byte is `-`; dispatch
on the second byte, case-sensitive:

| Argument | Effect | Address |
|---|---|---|
| `-X<n>` | `[0x4d27b8]` ← `atol(arg+2)`; overwritten with 0 by the game main, so it never reaches the exit code | `0x45e736..0x45e744` |
| `-Z<c>` | dword `[0x4d255e]` ← 1 if `c` = `'1'`, else 0 | `0x45e706..0x45e715` |
| `-c` | word `[0x4d252e]` ← 1 | `0x45e71d` |
| `-v<d>` | `prefs_video(d − '0')` (only the low byte counts) | `0x45e728..0x45e72e` |

**Main parser `0x45e758(argc, argv)`** (from the game main; jump table
`0x45e7a3`/`0x45e7d6` over the letters `B`..`t`, read from the image). Its
letters are `B C D E F G P R S T V W a b d e l m p r s t`:

| Argument | Effect |
|---|---|
| `-R<n>` | dword `0x4d2573` (no read through its absolute address; reads relative to the block base `0x4d24bc` not traced) |
| `-s` | toggles `0x4d24c8` |
| `-l` | word `0x4d2680` (the arena, below) |
| `-m` | byte `0x4d267d` = 1 |
| `-SPRUNKNOWN` | toggles `0x49ef38` (`0x45e88b`, `0x45e898`); any other `-S…` sets `0x4d24c7` |
| `-eggplant` | word `0x4d2524` (read through EBX + `0x68` at `0x45f333`) |
| `-E` / `-F` / `-G` | language character `s` / `f` / `g` at `0x4d2536` |
| `-D` | demo selection (`0x45ceb8`, a `tap` file, demo flag `0x4d2530`) |
| `-p` / `-r` | play back / record a `tap` file ([file_io.md](file_io.md#dbsim-recording-and-playback)); the tape options' restore and prefs copy `0x45e9fe..0x45ebdf` |
| `-C<name>` | `0x42d2b8(arg+2)`: the first of 13 names `[0x49ac64+4i]` equal under `_stricmp`: word `0x49ac4c` = i, dword `0x49ac50` = the argument |
| `-t<n>` | `atol` in `[0x49d248]` (`0x44bac4`) |
| `-V` / `-W` / `-T` | the low words of `atol` in `0x4d2512` / `0x4d2514` / `0x4d2516` |
| `-B` | word `0x4d252c` = 1 |
| `-P` | byte `0x4d24c9` = 1 |
| `-a` | byte `0x4d2539` = 0 |
| `-b` | `0x45e84a`: the present-mode word `[0x4d2566]` ← 0 (case index 9 sits at offset `0x20` of the byte table `0x45e7a3`, letter `0x42 + 0x20` = `b`) |
| `-d` | a case of the table (not described here) |

**`-EGGPLANT` sets the language, not the word.** Case `e` compares the
argument after `-` with `eggplant` and `EGGPLANT`; an argument whose second
byte is `E` is dispatched to case `E` (language `s`) first, so the second
compare can never match. The path helpers of the parser are `0x45eec0`,
`0x492b90`/`0x492b68`; the static direct-call reach of the parser is 68
functions without a pump or clock call. There is no case for `X` or `@`.

**Video preference `0x45e510(m)`** (hook `prefs_video`): the first call
(byte `[0x49f254]` = 0) opens root + `data\prefs.cfg` (`rb`, path through
`0x45ee00`), reads 7 bytes and on a full read takes the selector from
**byte 4** and stores **byte 6** zero-extended in `[0x4d255e]` (the
DirectDraw request); selector 1 → mode 0, anything else (also a missing
file: then the argument) → mode 3; the once-flag is set. Later calls use
`m` directly. Mode 0: 320 × 240 game, 640 × 480 DIB, `[0x4d2537]` ← 0,
scale shifts `[0x4d253a]`/`[0x4d253c]` ← 0; mode 1: 640 × 480 game,
1280 × 960 DIB, `[0x4d2537]` ← 3, `[0x4d256c]` ← 0, shifts 1; modes ≥ 2:
as 1 with `[0x4d256c]` ← 1. Every mode sets the present-mode word
`[0x4d2566]` ← 1; only the main parser's `-b` sets it to 0. Globals: game
size `[0x4d253e]`/`[0x4d2542]`, DIB size `[0x4d2546]`/`[0x4d254a]`,
`[0x4d254e]`/`[0x4d2552]` (`0x45e595..0x45e6bf`). Without `data\prefs.cfg`
init record 46's `[0x4d255e]` = 1 stands (byte 6 is stored only after a full
read), so WinMain asks for DirectDraw. `prefs_video` runs before the hook
arming point, so under the proxy it never runs as own code.

### Game main `0x45f160`, start-up part (`0x45f160..0x45f458`)

cdecl `(argc, argv)`, frame `0x1c4` bytes, EBX = `0x4d24bc` (the main
block; offsets are given below as absolute addresses), EDI = `0x49f287`
(base of its literals), exception record `0x49630c` with `0x49f273`.

1. **`drive.cfg`.** `_fopen("data\drive.cfg", "rt")`; NULL →
   `0x479c78(0x6c56c8, 5, 0, "MAIN.CPP", 572, path)`: fatal (below).
   `_fscanf(f, "%s", buf)` (80-byte frame buffer `[ebp-0x90]`, result r),
   `strcat(buf, "\")`; root R = buf when r ≠ 0, else `""`; a second
   `_fscanf(f, "%s", [ebp-0xe0])` whose result and buffer are unused. The
   file is **never closed**. `buf` is copied to `0x4d280c` (the CD root, at
   `0x45f230`) **whatever r is**, so an empty `drive.cfg` gives the
   uninitialised frame buffer plus `\` (quirk; `drive.cfg` is written by the
   setup, [file_io.md](file_io.md)). The second token's buffer `[ebp−0xe0]`
   (80 bytes) lies right below the first: a token of 80 bytes or more runs
   into it before the CD root is copied out, so the root becomes the tail of
   the second line (the own code keeps this up to 159 characters and refuses
   beyond).
2. Root prefix `0x4d27bc` ← `""` (one byte); `0x479e18(0x4d27bc)` copies
   it to the error report's path `0x6c56dc`. **Exit code `[0x4d27b8]` ←
   0** (`0x45f26d`).
3. Main parser `0x45e758(argc, argv)` ([above](#option-parsers-0x45e6cc-and-0x45e758)).
4. `stage_dispatch(0)`; `0x467c34(0x200)` (`[0x4a088c]` ← max(n, 0x80));
   VOL registration `0x473260(1, ".vol", R, 0)` ([vol.md](../formats/vol.md));
   `stage_dispatch(1)`. The VOL buffer size is nevertheless **0x400**: the
   manager constructor `0x467b9c` (`0x467bb4`) sets it before the archives
   are opened.
5. **`error.str`.** A string holder at `[ebp-0x10]` (`0x453824`),
   `0x453878(holder, "error.str")`, then two groups
   `0x4539a0(holder, [ebp-0x110], 12, 0)` and `(holder, [ebp-0x11c], 3, 0)`
   ([str.md](../formats/str.md)). **Never fatal:** `0x4538d0` ends with
   `mov al, 1` (`0x45390e`) whatever its two stream reads did, so the branch
   at `0x45f30f` to the fatal report (`"MAIN.CPP"`, line 624, "Missing string
   resource: error.str.") is never taken. A missing `error.str` gives a
   holder over an 8-byte zero block (`new[](0)`), empty groups and, without
   `-eggplant`, a message box with the frame's stale pointers (own code:
   zero). **`0x4539a0(holder, array, n, out2)` does not read `n`**: it stores
   as many string pointers as the group's count word says (12 and 3 are only
   the callers' arrays: a larger group writes on into the game main's
   frame). `0x453918` skips a string's trailing extra bytes only when `out2`
   is given; the game main passes 0.
6. **`-eggplant` check** (`0x45f333`): word `[0x4d2524]` = 0 → close the
   tape `[0x4d24e2]` and, only when it was open, the dmp stream
   `[0x4d24de]`; `0x4669b4()` (back to GDI); `MessageBoxA(0, entry 4 of
   group 1, entry 0 of group 2, MB_ICONEXCLAMATION)`; `0x453848(holder,
   2)`; restore `fs:0` and return. No mission runs; the exit code stays 0.
7. **Memory check.** `GlobalMemoryStatus`; `dwTotalPhys` < `0x73a000` →
   fatal `0x479c78(0x6c56c8, 5, 0, "MAIN.CPP", 657, "need at least 8
   megs physical to run!")`; < `0xbb8000` → word `[0x4d2680]` ← 1 (as
   `-l`). With `[0x4d2680]` ≠ 0: arena `0x476df4(0x6bb2f8, 0xb0000,
   0x604f04)` and byte `[0x4d256c]` ← 0.
8. Mission load `0x461608()`; `stage_dispatch(3)`;
   `LoadCursorA(0, IDC_ARROW)` → `0x464c68` when nonzero; word
   `[0x4d2564]` ← 1 (the gate of the command handler `0x45feb8`, which
   returns 0 while it is 0: [input.md](input.md#command-handler-0x45feb8-key--handler));
   the main loop
   ([main_loop.md](main_loop.md#the-main-loop-pump-window-procedure-pause-limiter-and-clock)).

The compares `cmp byte [ebx+0xd], 0` at `0x45f399`, `0x45f46c` and
`0x46208a` are dead (their flags are overwritten before any jump).

### Loop tail `0x45f4c5..0x45f568`

After the loop leaves (pump quit in the pause path, word `0x4d2532`, word
`0x4d24fe`, or the logic frame's 1; main_loop.md):

1. `[0x49f8ec]` (sound context) ≠ 0 → `0x464874(ctx)`; `0x462734()`
   (sound context release: `[0x49f8ec]` written, handler removal
   `0x466010`, driver release); `0x46548c()` (clear and show the frame).
2. **Prefs byte 6.** If byte `[0x4d1f3e]` (byte 6 of the in-memory prefs
   block `0x4d1f38`) ≠ the low byte of `[0x4d255e]`:
   `0x459958(6, that byte, 1)` and `0x459b80(6)` (write `data\prefs.cfg`
   through `prefs_store` `0x459b94`, [cfg.md](../formats/cfg.md)). A run
   that ends in another display mode than it started in rewrites
   `prefs.cfg`.
3. Tape playback word `[0x4d24d6]` still set → `OpenFile("tapes\prefs.cfg",
   buf, OF_DELETE)` and `OpenFile("tapes\keyjoy.cfg", buf, OF_DELETE)`
   (through `0x4968a4`; `0x45f4e1..0x45f541` for steps 2 and 3).
4. `0x4650cc()` (empty), `0x4650d0()` (`KillTimer` only when
   `[0x4d3608]` ≠ 0, which nothing writes: [timing.md](timing.md#other-time-sources)).
5. The mission end `0x461ff0()`, called at `0x45f54b` with no argument
   ([below](#the-mission-end-0x461ff0-as-own-code)). The instructions after
   the call (`0x45f550..0x45f55e`) load EDX and ECX before use and do not
   read EAX.
6. `0x453848(error.str holder, 2)`; restore `fs:0`; return to WinMain
   step 8.

Own code: `es2_sim_main_loop` (`sim/src/loop.c`; main_loop.md, unit test
`//sim:loop_test`); headless, `OpenFile(…, OF_DELETE)` of the tail is
counted.

### The abort-notify list and the fatal error report

- **Registration `0x479cfc(fn)`**: an 8-byte node from `0x47749c` (NULL:
  `_fputs` of a message, then `_abort`) = {next = old head, fn}, pushed
  at the head `[0x6c56d8]`; never removed. Six sites: `0x46162b`
  (mission load: `0x461304`), `0x4779e5` (input set-up: `0x4779a0`,
  empty), `0x48063f` (`0x480804`, empty), `0x480b23` (`0x480bc3`, empty),
  `0x480f70` and `0x480fbc` (`0x480f30`, empty). Only the mission load's
  entry does something.
- **Notify `0x479c54(log, severity)`**: walks from the head (last
  registered first) and calls `fn(severity)` (cdecl, one slot).
- **`0x461304(severity)`**: `0x462734()` (sound release); the tape/dmp
  close as in the mission end (dmp only when the tape is open);
  `0x46a4ac()`; `[0x4d27b8]` ← 0 (`0x46133c..0x461343`).
- **Fatal report `0x479c78(log, severity, cond, file, line, text)`** (43
  callers): for severity 4, 5 or 6 **whatever `cond` is** (the callers test
  the condition themselves): the notify list, the message
  `0x479f24(severity, cond, file, line, text)` (the "Error" message box; one
  of its cases switches to GDI through `0x4669b4` at `0x479f75`), then
  `0x479f24(1, 1, 0, 0, 0)`, `0x4650cc`, `0x4650d0`, `PostQuitMessage(0)`,
  `__exit(1)` (`0x479cd2..0x479cdb`): exit code 1, no atexit functions, no
  exit records, no stream flush. Other severities only log through
  `0x479f24` and return. It writes `assert.log` in the working directory.

### Exit code

The process's exit code is `[0x4d27b8]` at WinMain's return: 0 after the
start-up reset or the `-eggplant` refusal, the mission end's 6/0/4/3, and 0
again through the abort notifier `0x461304` (on the fatal path, which exits
with 1 anyway). CW3220's `exit` then runs the atexit functions and the 26 exit
records, which free statics, decrement the construction counter and close
the `DARKMONO.VXD` handle
([rtl_imports.md](../reference-game/rtl_imports.md#the-26-exit-records-of-dbsim-static));
none writes `[0x4d27b8]`, a file or compared state. For the own process:
the exit code is the word the mission end computes; the exit records have
no compared effect. The golden manifests' `own_process_exit` checks it
(`c1_01-success`: 3).

### What the oracle compares

- Nothing of the start-up is in the tick log. Compared are the after-load
  snapshot ([mission_load.md](mission_load.md#13-l0-arming-before-the-load-and-the-after-load-snapshot):
  the globals named above, `0x4d253e..0x4d2566`, `0x4d255e`, `0x4d2680`,
  the root strings `0x4d27bc`, `0x4d280c`, `0x6c56dc`, and the heap
  blocks, of which the argv strings are the first; window, DC and DIB
  handles are host values and belong to the learned noise), the return
  files and the exit code.
- `drive.cfg`, `error.str` (VOL or loose), `prefs.cfg` and the memory status
  come from the installation directory and the host
  ([own_process.md](../reference-game/own_process.md#start-up-parsers-and-file-services)).
- The fatal paths (missing `drive.cfg`, below 7.6 MB of physical memory) and
  the `-eggplant` refusal are start-up behaviour of their own; the launcher
  always passes `-eggplant`.

### Open

- Whether a DirectDraw failure in WinMain step 5 ends the process: the
  `WM_QUIT` it posts is read by the first pump that runs (the mission
  load's pumps `0x4616a4` …; whether they test the result was not read).
- The role of `0x467c34`'s size `[0x4a088c]` beyond the VOL buffer
  (the constructor `0x467b9c` overrides it with 0x400; other readers not
  traced).

## DBSIM's start-up as own code

D's start-up (WinMain to the game main's call of the mission load) as own
code in `sim/`; the host side is
[own_process.md "Start-up, parsers and file services"](../reference-game/own_process.md#start-up-parsers-and-file-services).
In vitro: `//tools/reference-game/invitro:differential_startup_test`.

### What is own code

| Original | Own code (`sim/src/`) | Checked |
|---|---|---|
| CW3220's GUI branch after the records: `GetModuleHandleA(NULL)`, the command tail, `WinMain(h, 0, tail, 10)` | `es2_sim_startup_run` (`es2_rtl_command_tail`) | unit test |
| WinMain `0x46538c`, split `0x4652f8`, class `0x4650e8` (headless), window `0x465158` with `0x464c10` and `0x464cc4` (headless stores), wait cursor `0x464c68`, the tail (`PostQuitMessage`, the pump until `WM_QUIT`, `0x466730`) | `startup.c` (`es2_sim_startup.quit_pump`) | unit test; split in vitro |
| early parser `0x45e6cc`, main parser `0x45e758` with `0x42d2b8` (`-C`), `0x44bac4` (`-t`), the path helpers `0x45eec0`, `0x492b90`/`0x492b68`, the tape options' restore and prefs copy `0x45e9fe..0x45ebdf` | `startup.c` | in vitro (4,000 command lines), unit test (tape options) |
| game main `0x45f160..0x45f42b`: `drive.cfg`, the roots, `0x479e18`, the exit-code reset, stages 0 and 1, `0x467c34`, error.str (`0x453824`, `0x453878`/`0x4538d0` with `0x472f14`, `0x4539a0`/`0x453918`, `0x453848`), the `-eggplant` refusal, the memory check | `startup.c` | unit test; the string groups in vitro (4,000 holders) |
| VOL registration `0x473260`, `0x467b9c`, `0x467cd8`, `0x467c54`, `0x47d2c4`, `0x467f94`, archive open `0x47cab0` with the read stream (`0x47ad40`, `0x47ae68`, `0x47ae28`, `0x47aeb0`, `0x47aecc`, `0x47af98`, `0x47ad78`), the buffered stream (`0x47c644`, `0x47c6d0`, `0x47c718`, `0x47c95c`, `0x47c8c4`, `0x47c82c`), the archive constructor `0x47c9a0`, `0x496406` | `volumes.c` | in vitro (9 registrations) |
| the fatal report `0x479c78` with `0x479c54`, `0x479f24` (severity 5), `0x479dd0`, `0x479e8c`, `0x479e48` | `startup.c` | unit test |
| `0x45ee00` (install root + path) | `services_files.c`, a service row (the config adapters call it) | unit test |
| DIB section `0x464d44`; after the load the game main's rest (the loop, the tail, the holder) | `frame_buffer.c`, `loop.c` (`es2_sim_startup.dib_section`, `.after_load`) | main_loop.md (in vitro) |

**`0x496406(o, 0)`** is `[[vt − [vt−4] − 0xc] + 0x20]` (`0x496417..
0x496420`; the in-vitro comparison caught a reading that missed the last
load): it balances the read stream destructor's two decrements (in vitro
the counter ends at 0 after every registration).

Calls into other code go through the dispatch: the hooks `prefs_video`,
`stage_dispatch`, `stream_open`, the stream's read slot, `stream_destroy`,
`tape_save`, `tape_restore`, the mission load `0x461608` and the loop's
callees; the services of the heap, the string imports and the file imports.
Refused by name: `0x45ceb8` (`-D`'s demo pick), `0x476df4` (the `-l` arena),
`0x4667c8` (the DirectDraw switch).

### Headless deviations

- No window, class, cursor or message queue: `RegisterClassA`,
  `LoadLibraryA("BWCC32.DLL")`, `CreateWindowExA`, `ShowWindow`,
  `UpdateWindow` (its synchronous `WM_PAINT`), `LoadCursorA`, `ClipCursor`
  and `PostQuitMessage` are counted, not made; `[0x4d3610]` and
  `[0x4d3720]` hold stand-in handles; `AdjustWindowRectEx` and the system
  metrics are a fixed 640 × 480 screen with 4/4/18 frame and caption (the
  rectangle `0x4d36cc` is a host value in the original too). `[0x4d3034]`
  and `[0x4d3038]` hold host stand-ins and the DIB bits a host region.
- `GlobalMemoryStatus` answers 64 MiB of physical memory
  ([own_process.md](../reference-game/own_process.md#start-up-parsers-and-file-services)):
  no arena unless `-l`.
- Message boxes (the `-eggplant` refusal, the fatal report) are logged as
  `{"event":"platform","call":"MessageBoxA"}` lines and answered at once;
  the fatal report still writes `assert.log` in the working directory as
  the original does, then ends the process with 1 without the exit records.

### Exit codes

| Path | Original | es2sim |
|---|---|---|
| `data\drive.cfg` missing, or below 7.6 MB | fatal report, `__exit(1)` | 1, reason `fatal_error`, no exit records |
| no `-eggplant` | message box, WinMain returns `[0x4d27b8]` = 0 | 0, reason `winmain_returned`, after the 26 exit records |
| the launcher's line (`dummy -eggplant -X2 -R<n> -G`) | the mission load, the loop, the mission end | the same as own code; in a synthetic installation without `data\script.dat`: 1 through the fatal report of the launch peek `0x4614c0` (call at `0x461524`; notify `0x461304` runs; `assert.log`: `Line 222 in "INIT.CPP": data\script.dat`) |
| `-l` or below 12 MB | the arena `0x476df4` | 70, refusal `0x00476df4` |
| no `data\prefs.cfg`, or `-Z1`, or prefs byte 6 set | DirectDraw | 70, refusal `0x004667c8` |
| more than 26 arguments | writes past WinMain's argv array | 70, refusal `0x00465351` |

es2sim's own codes and the original's fault codes:
[own_process.md "Exit codes"](../reference-game/own_process.md#exit-codes).
`//sim:es2sim_real_test` runs the first three rows with the launcher
contract's command lines in a synthetic installation; the unit tests cover
the DirectDraw and argument-count refusals.

## The mission end `0x461ff0` as own code

The hook `mission_end` (group `mission_end`, RVA `0x61ff0`, core
`core/src/mission_end.c`, adapter `proxy/mission_end_hooks.h`). `0x461ff0` is
entered once, from `0x45f54b`, with no argument. The prefs write and the
tape-mode deletes are not in this function; they are in the
[loop tail](#loop-tail-0x45f4c50x45f568) of its caller.

### The function (`0x461ff0..0x462099`)

`push ebx; mov ebx, 0x4d24bc` (the base of the run block; all fields
below are `[ebx+k]`), a plain cdecl function with no frame, no exception
record, no FPU use.

| Step | Address | What |
|---|---|---|
| 1 | `0x461ff6` | byte `[0x4d1dd6]` ← 0 |
| 2 | `0x461ffd` | `cmp [ebx+0x26], 0` (tape stream `0x4d24e2`); equal: skip to step 5 |
| 3 | `0x462003..0x462013` | EAX ← dump stream `[ebx+0x22]` (`0x4d24de`); nonzero: `_fclose(EAX)` (thunk `0x496838`, `0x46200b`), then `[ebx+0x22]` ← 0 (EDX = 0) |
| 4 | `0x462016..0x462022` | `_fclose([ebx+0x26])` (`0x46201a`), then `[ebx+0x26]` ← 0 (EAX = 0) |
| 5 | `0x462025..0x46202e` | `results_writer([ebx+0x2a])` (call at `0x462029`; `0x42412c`, dword `0x4d24e6`, the object the writer reads; [mission_runtime.md](mission_runtime.md)); each call is followed by `pop ecx` |
| 6 | `0x46202f..0x462088` | the exit code `[0x4d27b8]` (table below) |
| 7 | `0x46208a..0x462093` | `cmp byte [ebx+0xd], 0` (flags dead), `call 0x426514`, `call 0x425344` |
| 8 | `0x462098` | `pop ebx; ret` |

**Exit code** (first match; all tests read the globals as they are after
the writer):

| Test | Address | `[0x4d27b8]` |
|---|---|---|
| word `[ebx+0x74]` (`0x4d2530`, demo) ≠ 0 | `0x46202f` | 6 |
| word `[ebx+0x42]` (`0x4d24fe`, quit) ≠ 0 | `0x462042` | 0 (`xor ecx,ecx`) |
| byte `[[ebx+0x2a]+0x92+7]` ≠ 0 and word `0x4a9e52` ≠ 0 | `0x462053..0x462074` | 4 |
| otherwise | `0x462080` | 3 |

The pointer `[ebx+0x2a]` is read again at `0x462053` (after the writer). The
tests are on **words** (demo, quit, `0x4a9e52`: `cmp word`, bytes above
the low word do not count) and **dwords** (the two streams: `cmp dword`,
`test eax,eax`). The byte tested in the third row is `player+0x99`
(`0x92 + 7`), the byte the destruction path sets to 1
([player_movement.md](player_movement.md)), so exit 4 most likely means
"player Herc destroyed" (**hypothesis**).

**The two last calls are bare `RET`s.** The bytes at `0x425344` and
`0x426514` are `c3` (checked in the image; the DBSIM map classifies both as
`EP_RET`, `callers: ["0x461ff0"]`, one call in each suite that ends the
mission). The calls change no register and no memory; the core does not
mirror them (nothing to observe, and a one-byte body cannot take the hook's
five-byte detour; their verdict rows are in function_verdicts.md).

**Registers.** EAX/EDX/ECX at every call follow the listing: the first
`_fclose` has EAX = the dump stream and the entry EDX/ECX; the second has
the tape stream in ECX, EDX = 0 and EAX = the first close's result (EAX = 0,
the loaded dump dword, when there is no dump); the writer has EDX = the
object and, when a tape was open, EAX = 0. After each call `pop ecx` leaves
ECX = the argument slot as the callee left it. At the `ret`: demo path
EAX/EDX = the writer's, ECX = the slot; quit path as the demo path with ECX
= 0; third path EAX = object + 0x92, EDX = 1 when word `0x4a9e52` is 0 and
the byte is set (`sete dl; and edx,1`), else the writer's; ECX = the slot.
**The caller reads none of them** (EBX is restored by the function's own
`pop ebx`), so the hook needs **no entry shim**; EDX:EAX is nevertheless
exact and ECX is returned by the core (`es2_mission_end_registers`; the
in-vitro cases compare it against the original body).

### The hook

- `mission_end` (`0x461ff0`, 0 arguments, relocated range `push ebx; mov
  ebx,imm32` = 1 + 5 bytes, both on the framework's whitelist), `compare`
  unsupported (stream close, the file writes of the writer, the exit code).
- Game calls through the weapons guest bridge with the callee observer
  (keys `0x496838`, `0x42412c`): counters `hk_mission_end_call_496838`
  and `hk_mission_end_call_42412c`. `results_writer` is a hook of its own
  group and keeps its own mode when nested.
- Entry shim: none.

### Verification

- Core unit test `//core:mission_end_test`: all 16 combinations of
  demo/quit/byte/bank, the register picture of every path, tape with and
  without dump (arguments, entry registers and the global words at each
  call), reread of the quit word and the object pointer after the writer,
  word and dword widths.
- In vitro `//tools/reference-game/invitro:differential_mission_end_test`:
  direct cases (100,000 in the group run, then a 256-case limited pass) of
  the original body, the adapter and the core with both callees replaced by
  logging stubs that return random registers and change the streams, the
  quit/demo/bank words, the object byte and the object pointer; compared are
  the pair, ECX, the ordered calls with entry registers and the stream
  words, the global windows and the callee counters; 256 installed 31/a7
  cases (fast and preserving dispatch). All six conditional branches
  (`0x462001`, `0x462008`, `0x462034`, `0x462047`, `0x46205f`, `0x462072`)
  are taken and not taken. One mutation caught (demo exit code 7 instead of
  6). **No real-callee pass**: the real callees are the runtime's file close
  and the writer's file writes; the writer's own group covers the writer
  with its platform calls stubbed. In vitro; not checked live.

### Live picture

The DBSIM map enters `0x461ff0` once in 14 of 40
suites (the ones that end the mission: c1_01-cheat-destroy,
-cheat-disable-late, -cheat-disable-leg, -quit-dialog, -success, c1_02-probe,
c1_06-cheat-disable, c1_08-scan, c3_09-loadout-b, c3_10-loadout-hercs,
c4_08-joystick, -razor-plasma, -slope-dive, c5_05-moon-missiles). The
tape/dump branch (`_fclose` inside the function) is in none of them
(c1_01-tape-record has a mission-end count of 0 in the map; its 601
`_fclose` entries come from other callers): the branch is covered in vitro
only. In a replaced regression `hk_mission_end_calls` and
`hk_mission_end_call_42412c` are expected to be 1 in those 14 suites and 0
elsewhere (**hypothesis**); equality shows in the unchanged exit code (the
manifests' `own_process_exit`, e.g. `c1_01-success`: 3), the return files
(`results.dat`, `mission.var`) and the tick log.

### Open

- The tape/dump close path has no live suite (it needs a recorded or
  replayed tape that is still open when the mission ends normally).
- Who sets the word `0x4a9e52` to a nonzero value: the only direct-address
  write in the image is the clear at `0x4246bb`; its readers are
  `0x410813`, `0x410de5`, `0x4240f4` and this function (set through a
  pointer or a block copy, not traced here).

## The own DBSIM process behind the launcher

The launcher can start `es2sim` (`//sim:es2sim_window`) in place of
`DBSIM.EXE`. The command line, the start loop and the exit-code rules of the
launcher contract stay the same; only the program behind the DBSIM start
changes. An own addition like `ES2_LAUNCH_SHELL`; nothing in the original's
behaviour changes when it is not configured.

### Configuration

| Source | Effect |
|---|---|
| `ES2_LAUNCH_SIM` | Command prefix of the replacement, for example `"<inst>\ES2SIM\es2sim.exe" --headless --config "<ini>"`. |
| `es2launch.ini` `[sim] command` | Used when `ES2_LAUNCH_SIM` is unset or empty. Read from the working directory (the installation; `GetFullPathNameA("es2launch.ini")`, `GetPrivateProfileStringA`). `configure-sim.ps1` writes it; `configure-sim.ps1 -Remove` deletes it, and the launcher starts `DBSIM.EXE` again. |
| `ES2_LAUNCH_SIM_OUTPUT`, else `[sim] output` | A file that receives es2sim's stdout and stderr (appended; an inherited handle with `FILE_APPEND_DATA`). `output_failed sim-replacement <error>` is logged when the file cannot be opened; es2sim then starts without it. |

With neither source set, `DBSIM.EXE` starts as before. The VSHELL start and
`ES2_LAUNCH_SHELL` are independent of this. es2sim reads no DBSIM.EXE (the
image is built in).

### Command line

- es2launch appends ` --arguments "<DBSIM argv without argv[0]>"` to the
  prefix. For the mission start this is `--arguments "-eggplant -X2 -R0 -G"`.
- An argument that contains `"` refuses the start (`start_failed
  sim-replacement command`, treated like a failed spawn, exit -1). The
  original line never contains one.
- Trailing backslashes are doubled before the closing quote.
- es2sim builds the guest's command line as `"DBSIM.EXE" <arguments>`. With
  `--arguments` given, the configuration's `[game] arguments` and
  `command_line` are ignored.

The replacement starts with `CREATE_NO_WINDOW`; es2sim opens its game window
itself (SDL3), or none with `--headless`. As with the shell replacement,
`__COMPAT_LAYER` is left out of its environment and handed on as
`ES2SIM_COMPAT_LAYER` (see "The replacement shell without the 256COLOR layer").

### Exit codes and the chain

es2sim's exit code goes to `es2l_next` exactly as DBSIM's would: code 3
(mission over) starts the shell with `-X3`, and so on. es2sim's own codes 64
to 71 (64 usage, 65 image, 66 claim, 67 setup, 70 refused, 71 fault;
`sim/include/es2/sim/host.h`) are no DBSIM exit codes; the loop ends on them,
as it does on every code above 6. Where the original faults, es2sim exits
with the original's exception code (`0xc0000094`, `0xc0000095`);
`es2l_next` switches on the unsigned value, so the chain ends there too, as
after a crashed DBSIM (own_process.md "The original's fault paths").

**Log lines:** `start sim-replacement <prefix> --arguments "<argv>"`, `exit
sim-replacement <code>`, `start_failed sim-replacement <error>`; `(scripted)`
is added in the dry run (`ES2_LAUNCH_SCRIPT`).

### Deviations

- **argv[0].** The original's DBSIM gets argv[0] `dummy` from ES.EXE (`dummy
  -eggplant -X2 -R<n> -G`). es2sim's guest gets `DBSIM.EXE`. WinMain
  `0x46538c` skips argv[0] and parses the rest, so the tail it reads is
  identical.
- **Process structure.** The DBSIM process is es2sim, which hosts
  DBSIM.EXE's image in its own process. Its window comes from SDL3 instead of
  DBSIM's DirectDraw window.

### Tests

- `//launcher:launch_logic_test`: `sim_replacement()` covers the command
  form, the quote refusal, the backslashes, a short buffer, and the
  environment-before-INI order.
- `//launcher:launch_chain_test` (with a stand-in): J: the environment
  prefix, the output file, `__COMPAT_LAYER` left out and `ES2SIM_COMPAT_LAYER`
  passed on, and es2sim's 66 ending
  the loop; K: the INI in the working directory; L: the environment
  overriding the INI; M: a replacement that cannot start ends the loop like a
  failed DBSIM start.
- `//tools/reference-game/setup:configure_sim_test`: the real es2launch in a
  dry run reads the deployed INI.

### Open

Hypotheses until the live run of `start-own-menu.ps1 -Unattended -Shell gui
-Sim own` ([setup.md](../reference-game/setup.md#the-own-dbsim-process-in-an-installation)):

- es2sim reads and writes the guest's files relative to the installation's
  working directory (DATA, SAV), as DBSIM does. replay_suite.py runs it in a
  working directory prepared the same way.
- Its exit code for a mission the shell ended (c1_01-success) is DBSIM's 3.
