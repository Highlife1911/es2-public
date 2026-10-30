# VSHELL global state

The DATA/BSS globals of the reference `VER95/VSHELL.EXE` (SHA-256
`1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e`, image
base `0x00400000`) that the shell's handlers read and write outside the
SAV/CFG matrix of [vshell_ui.md](vshell_ui.md) ("Which screen writes which
field"): their image value, every writer and reader, their lifetime, and the
field of the own shell that holds each. A state VSHELL reaches is a state
the own shell reaches.

Not repeated here: the SAV and `prefs.cfg` fields
([vshell_ui.md](vshell_ui.md), [vshell_preparation.md](vshell_preparation.md),
[formats/mission.md](../formats/mission.md)), process start-up and the main
loop ([vshell_process.md](vshell_process.md)), the frame and palettes
([vshell_presentation.md](vshell_presentation.md)), the widget objects and
their handler tables ([vshell_widgets.md](vshell_widgets.md),
`shell/data/vshell_layout.json`), text drawing
([vshell_text.md](vshell_text.md)), the shell audio objects
([audio.md](audio.md#objects-globals-and-the-sos-boundary)) and the two RNG
states `0x482409`, `0x48110a`
([vshell_preparation.md](vshell_preparation.md#rng-use-in-the-menus-and-the-start-up-seed)).

## Method and confidence

The table below is a census of the canonical `llvm-objdump -d -M intel`
listing: every instruction with a direct `[0xADDR]` operand, an indexed
`[reg+0xADDR]` operand or an address-taken immediate in `DATA`
(`0x46c000..0x5ddfff`; raw data up to `0x4811ff`, zero-initialised above,
[vshell_process.md](vshell_process.md)) is classified as write
(`mov`/`inc`/`dec`/`add`/… with the memory destination), read or
address-taken, and assigned to the nearest preceding call target (the
"function" of a handler reached only through a pointer table is the
preceding call target, so the instruction addresses are exact and the
function names of the census are not). The image value is read from the
file. The option parser's jump-table area (`0x40169c..0x4017ff`), which the
linear listing mis-decodes, was disassembled with `--start-address`.

**C** = confirmed static at the cited addresses; **H** = hypothesis. Nothing
here is checked in vitro or live. Writes through object pointers
(`[reg+off]` with a run-time base) are not in the census; for the globals
that are fields of an object (the hangar, the prefs block) the methods were
read by hand. A pointer write to another global of the process, career or
screen tables is not excluded.

| Census (C) | Count |
|---|---|
| distinct DATA/BSS addresses used as direct operands | 3,142 |
| written directly somewhere | 1,063 (261 initialised part, 802 BSS) |
| written byte/word scalars | 196 |
| written addresses in `0x48d000..0x48dfff` | 464, almost all widget object pointers and handler-pointer tables filled by the 22 screen constructors |

The scalars that carry shell state are listed one by one below; the rest is
summarised by component in "Globals not listed one by one".

## Lifetimes

ES.EXE starts a new VSHELL for each `-X` state
([launcher.md](launcher.md#start-loop-confirmed)), so every non-file global
starts at its image value at every shell step (C). What crosses a DBSIM
round trip is only in `SAV\` (slot 10/11, `GAMEFILE.STR`), `data\prefs.cfg`
and the `data\` launch files.

| Class | Set by (C) | Ends |
|---|---|---|
| image | the DATA section or zero BSS; static initializers (`0x4023be` → `0x48110a`, `0x407d4f` → `0x482409`) | process start |
| process | start-up `0x401aa4`, defaults `0x40796c(0)`, option parser `0x4015d8`, game init `0x40e736`, or a one-time handler | process end |
| career | new game `0x40e8a5`, load `0x40eaaa`, and the handlers that call them | the next new game or load |
| screen | a handler; reset by the screen's show or leave routine | the next show or leave |

The own shells (`es2menu`, `es2shell`) are started once per `-X` state by
the own launcher and build their state fresh at every start; they read back
only `SAV\`, `DATA\` and `prefs.cfg`
([launcher.md](launcher.md#shell-state-engine)). The campaign directory of
`es2chain.py` is state VSHELL does not have; it is an optional evidence log.

## Process globals

| Address | Size | Image | Writers (C) | Readers (C) | Lifetime | Meaning |
|---|---|---|---|---|---|---|
| `0x46c268` quit flag | byte | 0 | `0x4050b1` (WM_CLOSE while interactive), `0x41edbb` (movie player, CD prompt Quit), INSTANT ACTION `0x4319b7`, VIEW DEMO `0x431c2a`, QUIT `0x431dc6`, Quit box `0x432258`, CD prompt `0x432579`, Rock & Roll `0x445c27`, Replay Yes `0x44d1fd`, Use Default mode 0 `0x44dbe2`, DEBUG ACCEPT `0x44dce8` | main loop `0x402063..0x40218a` (7 tests), drain helper `0x4022b1`, movie player `0x41edff`, `0x41ee24`, `0x41ee31` | process (never cleared) | leaves the main loop ([vshell_process.md](vshell_process.md)) |
| `0x46e348` exit code | word (stored as dword) | 0 | setter `0x408d1a` (`0x408d21`) only; callers in [vshell_ui.md](vshell_ui.md) and [vshell_process.md](vshell_process.md) | getter `0x408d28` (WinMain) | process | the code ES.EXE receives |
| `0x482362` entry mode | word | 0 | `-r` `0x40171c` (dead), start-up `0x401bc6` := `-X` value | dispatch `0x40188a`, start-up `0x401bd4..0x401c04` | process | `-X` state |
| `0x46c274` present gate | dword | 0 | `0x401bed` (`-X3/4`), dispatch `0x4018a1`, `0x4018d5`, `0x40199f` := 1; CREDITS `0x431cd8` := 0, `0x431d2a` := 1 | WM_PAINT `0x4057ab`, palette loader `0x40720b` | process (cleared only around `credits.avi`) | [vshell_process.md](vshell_process.md) |
| `0x46c278` `-eggplant` | dword | 0 | parser `0x4016f5` | start-up `0x401c6d` | process | missing → message 4, exit 0 |
| `0x46c280` loop started | dword | 0 | main loop step 5 `0x4020b8` := 1 (every iteration) | the nine service-bay bar handlers `0x43a5d1`, `0x43a680`, `0x43a77c`, `0x43a876`, `0x43a976`, `0x43aa76`, `0x43ab76`, `0x43ac6e`, `0x43ad84` | process | a bar button acts only when it is set (`jne` to the body, e.g. `0x43a87e`); **H:** guards bay clicks queued before the first loop iteration (the `-X3` results path shows the bay before the loop) |
| `0x46c288` focus | dword | 1 | WndProc `0x404fb8` (focus messages) | main loop `0x402018`, `0x402266`, movie player | process | [vshell_process.md](vshell_process.md) |
| `0x46c28c` interactive | dword | 0 | start-up `0x402009` := 1; `0x40ce0c` | WndProc | process | WM_CLOSE and the special keys act only when set |
| `0x46d878` full-screen preference | dword | 1 | WinMain `0x406ab8` (from prefs byte 6) | `0x4068ea`, WinMain, movie player, `0x42f905` | process | [vshell_process.md](vshell_process.md) |
| `0x481f4c` full screen active | dword | 0 | `0x4068ea`, `0x407636` | WndProc, present `0x406bfb`, results path `0x40f09f`, … (28 accesses) | process | [vshell_presentation.md](vshell_presentation.md) |
| `0x482354` `-m` | byte | 0 | `0x401704`, defaults | dead `0x42f905` only | process | no effect |
| `0x482355` keyboard | byte | 0 | defaults := 1, `-k` `0x401710` := 0 | start-up `0x401ed4` | process | keyboard device created |
| `0x482356` sound | byte | 0 | defaults := 1, `-s` `0x40172a`, `-v` `0x401757`, `-?` `0x4017f1` := 0 | buttons `0x40a004`, `0x40a2c4`, `0x40a6b0`, sound init `0x42f2f4`, `0x42f905` | process | [audio.md](audio.md#objects-globals-and-the-sos-boundary) |
| `0x482359` movies | byte | 0 | defaults `0x4079d5` := 1; `-a` `0x40169c` := 0 | queue `0x41e8c7`, player `0x41e995` | process | all AVIs off |
| `0x48235e` language | dword | 0 | defaults (prefs +0x2b), `-f` `0x4016b4`, `-g` `0x4016c3`, `-e…` `0x4016e6` | path prefix `0x408bb5`, `0x40da8d`, `0x40fd2d`, `0x417cb0`, movie player, `0x42f905` | process | 0 eng, 1 fre, 2 ger |
| `0x482368` cheat | byte | 0 | defaults `0x4079dc` := 0; `-@` `0x4016a8` := 1 | `0x413315` (DEBUG dialog auto clicks) | process | [vshell_ui.md](vshell_ui.md) (the DEBUG mission-file dialog) |
| `0x482364`, `0x482366` | word | 0 | parser `0x401736`, defaults | dead `0x42f905` | process | no effect |
| `0x48117c` | dword | 0 | start-up | — | process | write-only |

The option parser's and the defaults' other effects are in
[vshell_process.md](vshell_process.md). After the main loop starts only the
quit flag, the exit code, the focus and display flags and `0x46c274` change
in this table.

## Career globals outside the SAV fields

| Address | Size | Image | Writers (C) | Readers (C) | Lifetime | Meaning |
|---|---|---|---|---|---|---|
| `0x4826ee` loaded | byte | 0 | game init `0x40e736` := 0; new game `0x40e92a` := 1; load `0x40ebeb` := 1 | exit save `0x40e947`, statistics panel `0x4377f5`, slot click `0x43807b`, Saved games show `0x43a280` | process after the first new game or load (never cleared again) | exit save to slot 10/11; enables SAVE in Saved games |
| `0x4826f0` mode | word | 0 | game init `0x40e758` (prefs +0x2a); setter `0x40ec56` from INSTANT ACTION `0x431951` (0), START NEW GAME `0x431a1f` (1), CONTINUE `0x431a8c` (1), SAVE/RESTORE `0x431b3e` (1), PRACTICE `0x431f56` (0); the setter also stores prefs +0x2a | new game `0x40e8e3`, `0x40e8f8`; save `0x40e968`; load `0x40eac4`, `0x40ebf2`; bank `0x40ef06`; results `0x40f191`, `0x40f4ba`; `0x40f8df`; preparation `0x41cd85..0x41ced9`; bay buttons `0x43b764`; Use Default `0x44db97`; DEBUG ACCEPT `0x44dc9c` | process (follows the last main-menu entry) | slot 10 → 11 in mode 0; [vshell_preparation.md](vshell_preparation.md#shell-options) |
| `0x4826f2` final word | word | 0 | new game `0x40e8a8` := 2; results `0x40f480` := 3, `0x40f554` := 1, `0x40f603` := 0; load reads it from the SAV (`0x40ebc3`, address taken) | save `0x40ea52`, CONTINUE `0x431aa7`, `0x431ac8` | career | also a SAV field; listed for its memory side |
| `0x4826ea` difficulty | word | 0 | **none** (see "The difficulty word has no writer") | bank word 3 `0x40ef37` | image | always 0 (C static) |
| `0x479c54` practice index | word | −1 | `0x44c40c` (`0x44c581`, := entry index; from the eight practice entries and from INSTANT ACTION's `0x44c58b` with 8 + prefs +0x2e) | CAREER position of a mode-0 game `0x41306c`; preparation `0x41cc3d`; `0x44c413..0x44c449` | process | group-0 position |
| `0x473784` preset flag | dword | 0 | INSTANT ACTION `0x431945` := 1 | preparation `0x41cc49` (`0x41cbb1`) | process (never cleared; the process ends with exit 2) | instant-action preset branch |
| `0x482409` RNG A | 114 bytes | table | static initializer, reseed `0x407b42`, draws | — | process | [vshell_preparation.md](vshell_preparation.md#rng-use-in-the-menus-and-the-start-up-seed) |
| `0x48110a` RNG B | 114 bytes | table | static initializer, map rebuild draws | — | process | [vshell_mission_map.md](vshell_mission_map.md) |
| `0x482bc9` bay selection | word | 0 | squad panel `0x43dcdd` (`0x43ddd1`, `0x43dee5`, `0x43df65`, `0x43e042`); hangar clear `0x410aaf` := 0 (+0x22 of `0x482ba7`, from new game `0x410b11` and load `0x410e2e`); `0x410b11` uses +0x22 as a loop index and ends with := 0 (`0x410c58`) | 136 reads: hangar methods through the object (`0x410f46`, …) and absolute in `0x40ed0f`, WEAPONS `0x414cbe..0x415da4`, `0x43fbd8..0x4401f7`, REPAIR `0x43397c..0x4354ca`, squad panel `0x43d677..0x43e3ff`, CREW `0x4426e5`, BUILD `0x447064`, scrap `0x447dbb` | career (reset to 0 by new game and load), then screen-driven | the hangar's selected bay; not serialized |

SAV-backed globals (CAREER position `0x46fc58`/`0x46fc5a`, briefing
selection `0x4840cc..0x48419d`, team `0x482b5c..0x482ba6`, hangar
`0x482ba7`, salvage `0x482bd8`, bank `0x482bdc`, build bank `0x46fa14`,
nine-word bank, the lost count `0x482bd3` and the player-killed word
`0x482b8b` of the results path) belong to the SAV format and the SAV/CFG
matrix; they are career state (written by new game, load, results and the
editors).

## Screen state

### Main menu, registration, Saved games

| Address | Size | Image | Writers (C) | Readers (C) | Lifetime | Meaning |
|---|---|---|---|---|---|---|
| `0x474fde` selected slot | word | 10 | CONTINUE `0x431a9e` := 10; slot row click `0x4381be` (in `0x437fef`) := row | statistics panel `0x4377bc` (15 reads), edit field `0x437e62`, slot click `0x437fef`, RESTORE `0x4383a5`, CANCEL `0x4384bc`, ACCEPT `0x43869a..0x4386b9`, show `0x43a273..0x43a34d` | process (no reset on show) | the slot the Saved games screen works on |
| `0x474fdc` naming mode | byte | 0 | edit field `0x437fce` := 2; SAVE `0x438273` := 1; CANCEL `0x438632` := 0; ACCEPT `0x438822` := 0 | `0x437e72`, slot click `0x437ff6..0x43810f` | screen | 0 none, 1 SAVE pressed (next row click opens the edit field), 2 editing |
| `0x48d424` caller mark | word | 0 | main menu SAVE/RESTORE `0x431b44` := 0; bay SAVE `0x43a7fe` := 8 | EXIT `0x438439` | screen | where EXIT returns (0 main menu, 8 bay) |
| `0x476248` skill | word | 0 | SKILL LEVEL `0x43c3ca`, `0x43c6d2` := (v + 1) mod 4 | label `0x43c377`, `0x43c671`; ACCEPT `0x43c5bb` (dead second screen), `0x43c7ac` | process (not reset when Registration is shown again) | new career skill → rank |
| `0x47374c`, `0x473750` | dword | 0 | main-menu animation callback `0x4317c3` (`0x431875`, `0x43185e`) := 1 | the same callback | process (once) | menu buttons shown, first sound played ([vshell_presentation.md](vshell_presentation.md)) |
| `0x473bcc` CD prompt retry | dword | 0 | CD prompt Retry `0x432516` := 1; movie player `0x41ee0a` := 0 | movie player wait loop `0x41edf5` | screen (one CD prompt) | the player waits until Retry or the quit flag |
| `0x474d60`, `0x474d62`, `0x474d64`, `0x474d66` | word | −1 | Preferences setters `0x43712c` (+0x2c), `0x43714c` (+4), `0x4371e0` (+0x2d), `0x437200` (+6) | **none** | — | write-only shadows |

### Service bay and its editors

| Address | Size | Image | Writers (C) | Readers (C) | Lifetime | Meaning |
|---|---|---|---|---|---|---|
| `0x4758b8` screen word | word | −1 | bay show `0x43b7e8` := −1; MAIN MENU `0x43a712` := 0; SAVE `0x43a80c` := 1; WEAPONS `0x43a90d` := 2; REPAIR `0x43aa0d` := 3; BUILD `0x43ab0d` := 4; ARMORY `0x43ac05` := 5; CREW `0x43ad00` := 6; MISSION `0x43af5a` (in `0x43aee7`) := 7; movie player `0x41ef92` := −1 after a movie record with the mission-teardown flag (+0x14), which also runs the mission leave `0x445095` | leave dispatch `0x43a537`; the bar handlers' "same screen" tests; REPAIR tab `0x434da5`; squad panel `0x43cece`, `0x43d834`, `0x43dcf4`; Herc scrap ACCEPT `0x448345`, `0x448357` | screen | [vshell_ui.md](vshell_ui.md) (framework, bay buttons and the screen word, leaving and the screen word) |
| `0x47665a` selector click | byte | 0 | the eight bay selectors `0x43e477..0x43e7ef` := 1 around `0x43dcdd`, then := 0; CREW CLEAR `0x44268e`/`0x4426db` the same | `0x43deeb` in `0x43dcdd` | transient (one call) | with it a bay change also runs the CREW assignment `0x4426e5` and `0x43e167` |
| `0x476658` slot selection | word | −1 | bay change `0x43dfbf` := −1; `0x43e256` (`0x43e242`, set); WEAPONS show `0x43fc4f` := −1 | `0x41501f`, slot prev/next `0x43e399`/`0x43e3d9`, mount `0x43e2d4`, `0x43e346`, row click `0x43fdac` | screen | [vshell_ui.md](vshell_ui.md) (WEAPONS) |
| `0x476df4` mount flag | byte | 0 | the 27 WEAPONS row handlers `0x440990..0x4414f5` := 1 around `0x43fdac`, then := 0 | mount `0x43e2e3` | transient | only a row click mounts |
| `0x476df6` last row | word | −1 | `0x43fdac` (`0x44024b`) := row | `0x43fdac` (same row, no guidance shown and no slot → nothing, `0x43fdb4..0x43fdd7`), `0x4403f9`, description `0x4407ca` | screen (not reset on show) | **H:** suppresses a repeated click |
| `0x476df8` guidance shown | word | −1 | `0x4403f9` (`0x440601`) := mode; `0x43fdac` (`0x440236`) := −1 after hiding the panel (`0x43fcd4`) | `0x43fdbf`, `0x43ffde..0x43ffff`, `0x440400`, `0x44040c` | screen | highlighted guidance button (index into `[0x48d6e8]`) |
| `0x476dfc` guidance previous | dword | −1 | `0x4403f9` (`0x44060b`) | `0x440420` (switch on the previous mode) | screen | — |
| `0x476a82` guidance panel visible | byte | 0 | show `0x43fc87` := 1; hide `0x43fcd4` := 0 | the same two | screen | — |
| `0x474094`, `0x474096`, `0x474098`, `0x47409a` REPAIR selection | word | 0 | `0x434549(tab, row)` (`0x434653`, …): (0, 0) from the show `0x433a71` and the bay change `0x43dddb`, else the tab and row handlers | `0x414cbe`, `0x433ad5`, `0x434549`, `0x434d95`, `0x4351bd`, `0x43dcdd` | screen | [vshell_ui.md](vshell_ui.md) (REPAIR screen) |
| `0x48d33c` REPAIR salvage snapshot | dword | 0 | `0x433f86` (`0x433f93`) | CANCEL `0x435418` | screen (retaken at every bay change) | REPAIR CANCEL restores it |
| `0x48d340` REPAIR condition snapshot | 64 bytes | 0 | `0x433f86` (`rep movsd` to `0x48d340`, 16 dwords) | CANCEL `0x435422..`, `0x43553b` | screen | REPAIR CANCEL restores it |
| `0x478780` BUILD selection | word | −1 | `0x4472cb` (row click; the show `0x446f9d` calls it with 0 at `0x446fd7`) | `0x446fde`, `0x447064`, `0x44738a`, `0x4475ce` | screen (0 at every show) | [vshell_ui.md](vshell_ui.md) (BUILD) |
| `0x479210` ARMORY row | word | −1 | leave `0x449c46` (`0x449c72`) := −1; row click `0x449d2f` (`0x44a061`); `0x44a06e` (`0x44a32f`) | `0x449c46`, `0x449ccc`, `0x449d2f`, `0x44a06e`, `0x44a33b`, Scrap `0x44a508` | screen | [vshell_ui.md](vshell_ui.md) (ARMORY) |
| `0x48da50` weapons scrap item | word | 0 | warning show `0x448227` (`0x448231`) | ACCEPT `0x44845c` | screen | the item `0x40ed6a` scraps |
| `0x477778` CREW group | word | 0 | `0x442215(g)` (`0x4422d5`); the CREW show calls it with 0 (`0x44214b`) | squad panel `0x43de36`, CREW `0x442215..0x4427c5` | screen (0 at every show) | active roster group (lists `[0x48d824+4g]`) |
| `0x47777c` + 4·g CREW per-group selection | dword | 0 | CLEAR `0x442682` (indexed, := 0), `0x4425f1` | `0x4425e4` | screen | **H:** the selected list row per group |

### Mission screen and results

| Address | Size | Image | Writers (C) | Readers (C) | Lifetime | Meaning |
|---|---|---|---|---|---|---|
| `0x481108` page word | word | 0 | results path `0x40f62d` := 4; `0x43aee7(page, click)` (`0x43af0c`); Registration ACCEPT `0x43c7d1` := 0 / `0x43c7dc` := 1 (CAREER position 0 / else) | movie player `0x41ec48..0x41eed7`, bay background `0x43b87f..0x43b8b3`, Telecomm `0x4454ef`, `0x445531`, `0x445a61`, `0x445a6f`, Use Default `0x44dbc4`, DEBUG ACCEPT `0x44dcca` | process (no reset by load) | mission-screen page; 4 = debrief |
| `0x477946` group movies shown | byte | 0 | RESTORE `0x4383c6` := 0; Registration ACCEPT `0x43c7a5` := 0; mission open `0x444add` := 1 | MISSION button `0x43ae49`, mission open `0x444a69` | process (no reset by `0x40eaaa`) | `c<group>.avi` and the theatre movie once |
| `0x477947` briefing movie shown | byte | 0 | new game `0x40e8b1`, load `0x40ec06` := 0; mission open `0x444d18` := 1 | `0x444cd6` | career | briefing movie once ([vshell_ui.md](vshell_ui.md), movie queue and selection) |
| `0x477948` debrief movie shown | byte | 0 | new game `0x40e8b8`, load `0x40ec0d` := 0; mission open `0x444e0a` := 1 | `0x444dc8` | career | debrief movie once |
| `0x477944` | word | 0 | mission open `0x444b5b` := 1, `0x444cc6` := 2; leave `0x445095` := 0, `0x445152` := 2 | **none** | — | write-only |
| `0x478140` text panel | word | −1 | `0x445343(page)` (`0x4454ad`) | `0x445356` (switch −1..4), `0x445c95`, `0x445d46` | screen | briefing/objectives/intelligence/debrief panel shown |
| `0x46c269`, `0x46c26a` map animation | byte / word | 0 / −1 | mission open `0x444873` (pending, palette); main loop step 6 `0x4019c6` clears | `0x4019c6`, `0x444873` | transient | [vshell_process.md](vshell_process.md) (main loop, step 6) |
| `0x46c26c`, `0x46c270` | dword | 0 | `0x4019c6` (running, finish request); WndProc `0x405111`, `0x405176` := 1 | WndProc, `0x401a58` | transient | skip the map animation |

## Media globals

| Address | Size | Image | Writers (C) | Readers (C) | Meaning |
|---|---|---|---|---|---|
| `0x470fac` queue count | word | 0 | queue `0x41e8c0` (`0x41e96c` inc, `0x41e982`) | `0x41e8e3..0x41e973` | records in the ring `0x485748` |
| `0x470fae` play index | word | 0 | player `0x41e98c` (`0x41f0b1` inc, `0x41f0c7`) | 22 reads in `0x41e98c` | — |
| `0x470fb0` sequence | dword | 0 | player (4 writes) | button press `0x409d8a`, `0x40a2c4` | clicks ignored during a sequence |
| `0x470eb0` movie open | dword | 0 | `0x41e640` | WndProc, `0x409d8a`, `0x40a2c4`, Telecomm `0x4454b8` | [vshell_process.md](vshell_process.md) (window procedure) |
| `0x470ea0` movie ended | dword | 0 | MM_MCINOTIFY `0x4057f2`, `0x41e640` | `0x41e640` | — |
| `0x471120`, `0x471124` | dword | 0 | WndProc, `0x41e640`, player | player, WndProc | intro skip / intro playing |
| `0x47110c..0x47111c`, `0x471128..0x471130` | dword | 0 | player | `0x41e640`, player | movie window rectangle and counters (**H**) |

All media globals are transient (one call of the player) except the queue
ring, which the main loop drains every iteration. Which movies a state plays
is decided by the once-flags of the mission-screen table and by `0x482359`;
the queue and the player are in [vshell_ui.md](vshell_ui.md) (movie queue,
selection and player).

## Audio and display

The shell audio globals (`0x473338` SFX object, `0x473344` music volume,
`0x473348` focus, the sample handles `0x48ceac..0x48ceb8`, `0x473278`) are
listed with their writers in
[audio.md](audio.md#objects-globals-and-the-sos-boundary); the frame,
palette (`0x46de08`, pending byte `0x46de10`), DirectDraw and DIB globals
(`0x481948..0x481998`, `0x481f30..0x481f4c`) in
[vshell_presentation.md](vshell_presentation.md) and
[vshell_process.md](vshell_process.md). All are process lifetime and change
no file except through the prefs writes already described there (byte 6) and
in audio.md (byte 5).

## Globals not listed one by one

| Group | Range | Owner / where described |
|---|---|---|
| widget object pointers and handler-pointer tables of the 22 screens | most of `0x48d000..0x48dfff` (464 written addresses), e.g. `[0x48d1a0]`, the bar handler tables `0x48d2e4..0x48d338`, `0x48d5fc..0x48d658`, `0x48d788..0x48d7f0`, `0x48db90..0x48dbf4`, `0x48dc7c..0x48dc98` | written once by the constructors `0x40180c`…; [vshell_widgets.md](vshell_widgets.md), `shell/data/vshell_layout.json` |
| assertion context | `0x48dd04..0x48dd2c` (`0x48dd08` is incremented at 360 sites as a line counter, **H**) | debug only |
| allocator, VOL manager, stream library | `0x46cae4`, `0x46d168`, `0x46d304..0x46d334`, `0x48132c`, `0x481460`, `0x482494` | process; [formats/vol.md](../formats/vol.md), [file_io.md](file_io.md) |
| window, keys, mouse, DIB, DirectDraw | `0x4814a0..0x4818e4`, `0x481918..0x481998`, `0x481f30..0x481f57`, `0x482350..0x482353` | [vshell_process.md](vshell_process.md), [vshell_presentation.md](vshell_presentation.md), [vshell_widgets.md](vshell_widgets.md) (input) |
| text tables and fonts | `0x46ddf8..0x46de0c`, `0x4811a0..0x4811d8` | [vshell_process.md](vshell_process.md), [vshell_text.md](vshell_text.md) |
| preparation working state (`msn_gen`, MSN reader) | `0x470740..0x4707aa`, `0x485528..0x48558e`, `0x46fc5c..0x46fc74` | rebuilt per preparation call; [vshell_preparation.md](vshell_preparation.md) |
| map, map shapes, animation | `0x4719a4..0x471f4b`, `0x472370`, `0x48a1f8..0x48a510`, `0x5d8970..0x5d897c` | [vshell_mission_map.md](vshell_mission_map.md) (shellmap.cpp `0x4242a0..0x427634` and its helpers) |
| sound library and SOS | `0x47328c..0x4732a2`, `0x48aa20..0x48afea`, `0x5ddc8e..0x5dde0c`, `0x4800a0` | [audio.md](audio.md) |
| graphics library, decoders, event system | `0x47bce0..0x47e9cc`, `0x47f940..0x47f948`, `0x48e244..0x48e280`, `0x497ce4..0x497ffc`, `0x5d8004..0x5d8009` | [vshell_presentation.md](vshell_presentation.md), [vshell_widgets.md](vshell_widgets.md) |
| debug counters | `0x46c290`, `0x46c294`, `0x46d4b8`, `0x46d614`, `0x46d880`, `0x46dcb8`, `0x47112c`, `0x471130`, `0x481228` | debug prints and statistics only |

None of these carries a player-visible state across screens beyond what the
cited documents describe.

## Properties of the state

- **Bay selection after a new game and a load (C).** `0x482bc9` is 0, not −1.
  It is the hangar object's word +0x22 (hangar `0x482ba7`); the hangar clear
  `0x410aaf` (`0x410abe`) sets it to 0, the default hangar `0x410b11` ends
  with := 0 (`0x410c58`), and the hangar reader `0x410e2e` (from the load,
  `0x40eb86`) calls the clear and never reads +0x22. Only the squad panel
  `0x43dcdd` writes it absolutely. A load therefore starts the first WEAPONS
  or REPAIR visit at bay 0 if it holds a record (`0x43fbd8`).
- **Mission-screen state that survives a load (C).** The page word
  `0x481108` and the group-movie once-flag `0x477946` are not touched by
  `0x40eaaa`: CONTINUE GAME (`0x431a73`) keeps both from the process's
  earlier game; RESTORE (`0x438393`) clears only `0x477946`. The briefing
  and debrief once-flags `0x477947`/`0x477948` are cleared by every new game
  and every load, including the `-X3`/`-X4` slot-10 load.
- **The difficulty word has no writer (C).** `0x4826ea` has one access in
  the whole image, the read `0x40ef37` (bank word 3 in `0x40ef06`); no direct,
  indexed or address-taken reference to `0x4826e0..0x4826ed`; the
  neighbouring callback array `0x482608` ends at `0x4826df`. Statically it
  is always 0 (**H** only for writes through a run-time pointer, none
  found).
- **Write-only globals (C).** The Preferences setter shadows
  `0x474d60..0x474d66` and the mission-screen word `0x477944` are written and
  never read.
- **Screen word stores.** The bay handlers store the screen word
  `0x4758b8`; the movie player `0x41ef92` also stores −1 (mission-teardown
  record).

## Reset matrix

What the state-changing events do to the screen and career globals ("—" =
untouched, C):

| Event | `0x4826ee` | `0x4826f0` | `0x482bc9` | `0x474fde` | `0x481108` | `0x477946` | `0x477947/8` | `0x4758b8` |
|---|---|---|---|---|---|---|---|---|
| start-up (any `-X`) | 0 (`0x40e736`) | prefs +0x2a | 0 (image) | 10 (image) | 0 (image) | 0 | 0 | −1 (image) |
| new game `0x40e8a5` (Registration, practice, instant action) | 1 | — (set by the entry) | 0 | — | Registration ACCEPT: 0 or 1 | Registration ACCEPT: 0 | 0 | — |
| load `0x40eaaa` (CONTINUE, RESTORE, `-X3/4`) | 1 | — (set by the entry) | 0 | CONTINUE: 10 | — | RESTORE: 0 | 0 | — |
| results path `0x40f09f` | — | — | — | — | 4 | — | — | — |
| bay show `0x43b758` | — | — | — | — | — | — | — | −1 |
| bay MAIN MENU | — | — | — | — | — | — | — | 0 |
| mission open `0x444873` | — | — | — | — | via `0x43aee7` | 1 | 1 when shown | 7 |

Consequences for a shell that reproduces VSHELL (C, derived from the table):

- After `-X3/4` the results path sets page 4 and the DEBUG dialog's Use
  Default opens the mission screen on page 4 (`0x43aee7(0x481108, 1)`).
  `0x477946` is 0 in that new process, so the mission open takes its
  group-movie branch (`0x444a69` → `0x444a73..0x444add`) unless a condition
  inside the branch skips the movies (**H**; the movie choice is in
  [vshell_ui.md](vshell_ui.md), movie selection rules).
- CONTINUE in a process that already showed the mission screen keeps
  `0x477946` = 1 and the old page word: the group movies are not played
  again and the MISSION button chooses page 1 (`0x43ad5a`).

## Where the own shell holds each global

The state engine (`shell/include/es2/shell/engine.h`) holds the career
state of a process: the SAV image, the loaded flag, mode, final word and RNG
A. The flow (`shell/include/es2/shell/flow.h`, contract in
[vshell_ui.md](vshell_ui.md), shell flow as a state machine) holds the
process and screen state shared by both presenters; the graphical screens
(`shell/include/es2/shell/gui/`) hold the state of their own screens. Both
presenters (text `es2menu`, graphical `es2shell`) go through the same flow
and engine.

| Global | Own field |
|---|---|
| loaded `0x4826ee` | `es2_shell_engine.loaded`; `es2_shell_flow.game` (none, campaign in the service bay, game loaded without a campaign) |
| mode `0x4826f0` | `es2_shell_engine.mode`, `es2_shell_flow.mode` |
| final word `0x4826f2` | `es2_shell_engine.final_word`, `es2_shell_flow.final_word` |
| difficulty `0x4826ea` | `es2_shell_options.difficulty` (`prepare.h`), an explicit input of the preparation (bank word 3); the engine and `practice.c` pass 0 |
| practice index `0x479c54`, preset flag `0x473784` | `es2_shell_practice` (`practice.h`: `index`, `preset_flag`), held in `host.h` |
| RNG A `0x482409` | `es2_shell_engine.rng` (`es2_shell_rng`, `campaign.h`) |
| RNG B `0x48110a` | `es2_mission_map_globals.material_rng` (`gui/mission_map.h`) |
| bay selection `0x482bc9` | `es2_shell_flow.bay_select`, `es2_shell_editor_session.bay` (`editors.h`; the graphical app synchronises the two, `bay_select_sync` in `gui/app.c`), the WEAPONS screen's `bay` pointer; `es2_service_bay_game_loaded` resets it after a new game and a load; `es2_shell_editor_session_init` takes the bay from its caller, which passes 0 after a load |
| screen word `0x4758b8` | `es2_shell_flow.bay_word`, `es2_shell_editor_session.screen` |
| slot selection `0x476658`, guidance `0x476df6/df8/dfc`, panel `0x476a82` | `es2_weapons_screen` (`slot`, `row`, `guidance`, `guidance_previous`, `panel`) |
| mount flag `0x476df4`, selector click `0x47665a` | the `mount` argument of `es2_weapons_screen`'s row handler; the squad panel's `assign` argument (`gui/service_bay.h`) |
| REPAIR selection and snapshot `0x474094..0x47409a`, `0x48d33c`, `0x48d340`; BUILD `0x478780`; ARMORY `0x479210`; CREW `0x477778`/`0x47777c` | `es2_shell_editor_session` (`tab`, `row`, `family`, `index`, `salvage`, `conditions`, `herc_type`, `armory_row`, `order`, `crew`) |
| weapons scrap item `0x48da50` | `es2_build_armory.box_category` (`gui/build_armory.h`) |
| selected slot `0x474fde`, naming mode `0x474fdc`, caller mark `0x48d424`, skill `0x476248` | `es2_shell_flow.slot`, `.naming`, `.caller`, `.skill` |
| page word `0x481108` | `es2_shell_flow.page` |
| once-flags `0x477946..0x477948`, queue `0x470fac/ae`, sequence `0x470fb0`, enabled `0x482359` | `es2_shell_movies` (`movies.h`: `group_shown`, `briefing_shown`, `debrief_shown`, `write`, `play`, `active`, `enabled`) |
| text panel `0x478140`, write-only `0x477944`, map animation `0x46c269/6a` | `es2_mission_screen` (`panel`, `state`, `map_pending`, `map_palette`, `gui/mission.h`) |
| movie open/ended `0x470eb0/0x470ea0`, intro `0x471124/0x471120`, map skip `0x46c26c/0x46c270`, keyboard `0x482355`, interactive `0x46c28c` | `es2_gui_shell.input` (`es2_gui_input`, `gui/events.h`: `movie_open`, `movie_skip`, `intro_playing`, `skip_intro`, `map_animation`, `map_skip`, `keyboard`, `interactive`) |
| quit flag `0x46c268`, exit code `0x46e348` | `es2_shell_flow.quit`, `.exit_code`; `es2_gui_shell` (`gui/loop.h`) |
| focus `0x46c288`, full screen active `0x481f4c`, options `0x482354/55/56/59/5e/64/68`, `-eggplant` `0x46c278`, entry mode `0x482362` | `es2_gui_args` and `es2_gui_shell` (`gui/loop.h`); the text menu accepts `-a`, `-@` and the language only (`menu.h`) |
| present gate `0x46c274` | `es2_gui_frame.realize_gate` (`gui/frame.h`) |
| CD prompt `0x473bcc`, cheat `0x482368` | `es2_boxes.cd_retry`, `.cheat` (`gui/boxes.h`); `es2_shell_flow.debug_cheat` |
| menu animation `0x47374c`/`0x473750` | `es2_main_menu.menu_shown`, `.sound_played` (`gui/main_menu.h`) |
| Preferences shadows `0x474d60..0x474d66` | `es2_preferences.shadow_repair`, `.shadow_resolution`, `.shadow_build`, `.shadow_display` (`gui/preferences.h`, image −1) |
| `0x48117c` | not held (write-only) |

## Open points

1. Writes through run-time pointers are not in the census; they were checked
   by hand only for the hangar (+0x22) and the prefs block.
2. `0x476df6` (repeated-click suppression), `0x47777c` (CREW per-group
   selection), `0x46c280` (why the bay waits for the first loop iteration)
   and the movie rectangle globals `0x47110c..0x47111c` are hypotheses.
3. The order of the movie and once-flag writes inside `0x444873` is in the
   movie selection rules of [vshell_ui.md](vshell_ui.md).
4. Nothing here is checked in vitro or live. A cheap check: log the values of
   the career and screen globals at each `0x40e933` (exit save) of a scripted
   session and compare them with the reset matrix.
