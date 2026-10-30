# Shell: the own `VSHELL.EXE`

`shell/` reconstructs VSHELL as own code (the original's
behaviour, no modernisation). It has a portable state half (C17, no Win32,
no file I/O), a portable graphical half over VSHELL's 640 × 480 8-bit frame,
and thin Windows front ends. Every state change is a library call that is
byte-equal to VSHELL's own function in vitro; the screens only draw and
route clicks. Contracts: [vshell_ui.md](../docs/reverse-engineering/vshell_ui.md)
(per screen and operation), [vshell_preparation.md](../docs/reverse-engineering/vshell_preparation.md)
(preparation, SAV, results path), the other `vshell_*.md` documents
(process, presentation, widgets, text, mission map, state) and
[launcher.md](../docs/reverse-engineering/launcher.md) (start-up, the text
menu, the state engine). Status: [README.md](../README.md#status).

## The process boundary

DBSIM takes from the shell only its arguments (`-X2 -R<n>`, language, flags),
the four launch files (`script.dat`, `MISSION.STR`, `mission.var`,
`player.mec`) and the shared `prefs.cfg`; it never reads a SAV
([file_io.md, DBSIM call sites](../docs/reverse-engineering/file_io.md#dbsim-call-sites)).
It returns `results.dat`, `mission.var` and its exit code, and the launcher
drives the shell↔DBSIM loop by exit codes
([launcher.md](../docs/reverse-engineering/launcher.md)). So every shell
decision that matters for the simulation is testable as bytes without
DBSIM. Whole-session equality with a running VSHELL is not a goal: VSHELL
reseeds from `GetTickCount() & 127` at every start, so equality holds per
step for a given state and RNG. With the own launcher and shell neither
`ES.EXE` nor `VSHELL.EXE` runs, so the proxy's VSHELL-only measures are off
by default ([media_compatibility.md](../docs/reference-game/media_compatibility.md)).

## Targets

| Target (`//shell:...`) | What it is | Sources (`shell/`) |
|---|---|---|
| `:bytes` | little-endian byte access shared by every library | `include/es2/shell/bytes.h` |
| `:flow` | the state half: preparation, SAV construction, slots, campaign and results path, the service-bay editors, practice, mission texts, text drawing, the movie queue, CD check, the flow (the screen word, transitions and boxes as a state machine with host callbacks) | `src/prepare.c`, `equipment.c`, `save.c`, `campaign.c`, `slots.c`, `practice.c`, `texts.c`, `gui/text.c`, `flow.c`, `movies.c`; `include/es2/shell/*.h` |
| `:engine` | the state VSHELL keeps in one process (SAV image, mode and final words, RNG, selectors) and its operations; the text presenter's menu logic | `src/engine.c`, `menu_logic.c` |
| `:host` | what both front ends share: `menu.ini`, the file layer over `DATA`/`SAV` (`engine_io.c`, the optional evidence log), slots, the practice launch, CD check, ONLINE MANUAL | `src/host.c`, `engine_io.c`, `src/win/help.c` (`:help`) |
| `:gui`, `:gui_sdl` | the frame, colour table, dirty-rectangle queue and present, VSHELL's mouse and key paths, event queue, main loop, headless mode; the SDL3 window, pump and presenter | `src/gui/frame.c`, `events.c`, `loop.c`, `headless.c`; `window.c`, `present_sdl.c` |
| `:draw` | VSHELL's blitter, DBA/DBM decode, DPL palettes, the main-menu background sequence | `src/gui/draw.c` |
| `:screens` | the widget toolkit, the generated layout table of the 22 screens, every screen, the modal boxes, the mission map, the squad panel's Herc pictures | `src/gui/widgets.c`, `screens.c`, one file per screen (table below) |
| `:sounds`, `:gui_sound_sdl` | the shell sounds as logic (music choice, prefs byte 5 flip, three effects, ramp, focus) and their SDL3 playback | `src/sounds.c`, `src/gui/sound.c`, `sound_sdl.c` |
| `:mci_player` | the movie player over MCI with the IV32 binding of `//media:iv32_bind` | `src/win/mci_player.c` |
| `:gui_app` | the graphical presenter: builds every screen, supplies resources from the CD's VOLs and VSHELL's tables, literals and icon from the span store, shows the flow's screen and box, plays movies and sounds | `src/gui/app.c`, `app_sound.c` |
| `:es2menu` | the text front end of the flow (console); the test front end | `src/menu.c` |
| `:es2shell` | the graphical shell (GUI subsystem); `--menu DIR <VSHELL argv>` under `es2launch`, `--headless --script` for tests, without `--menu` a test pattern | `src/gui/main.c` |
| `:es2prep_core` | the preparation core behind the Python tools (hex-pipe batches; not a stable API) | `tools/es2prep_core.c` |
| `:es2movie`, `:es2sound` | manual tools: play one movie, play the shell sounds | `tools/es2movie.c`, `es2sound.c` |

Dependencies: `//formats:*` → `:flow` → `:engine` → `:screens` → `:gui_app`
→ `es2shell`; `:flow` → `:host` → `es2menu`/`es2shell`; `:gui` → `:gui_sdl`.
All libraries are private to the package; `//tools/reference-game` sees
`es2prep_core`, `data/vshell_layout.json`, `tools/*.py` and
`tests/real_acceptance.py`. `backend_data.bzl` (`BACKEND_DATA`) lists the
runfiles the backend-driven real tests share.

Screens (all in `:screens`, registered by one line each in
`src/gui/screens.c`; the state changes go through the libraries named):

| Screen (VSHELL) | File (`src/gui/`) | State library | vshell_ui.md |
|---|---|---|---|
| Main menu `0x430fc4` | `main_menu.c` | flow | §23 |
| Registration `0x43bd2e` | `registration.c` | flow | §24 |
| Practice `0x44b310` | `practice_screen.c` | `practice.h` | §15, §25 |
| Saved games `0x438c40` | `saved_games.c` | `slots.h` (slots, statistics) | §9, §13, §26 |
| Preferences `0x435598` | `preferences.c` | flow (prefs blocks) | §27 |
| Service bay `0x43afd4`, squad panel `0x43d029` | `service_bay.c`, `squad_pictures.c` | `editors.h` session; `campaign.h` condition | §28, §36 |
| WEAPONS `0x43ebbc` | `weapons.c` | `engine.h`, `hangar.h` | §29 |
| REPAIR `0x4326c7`, Herc scrap warning | `repair.c` | `editors.h` | §12, §30 |
| BUILD `0x445de8`, ARMORY `0x4484c4`, weapons scrap warning | `build_armory.c` | `editors.h` | §12, §17, §31 |
| CREW `0x441548` | `crew.c` | `editors.h` | §17, §32 |
| Mission screen `0x442bc4` (pages 0, 1, 4), the map | `mission.c`, `mission_map.c` | `texts.h`, `movies.h`; map inputs from the host | §16, §33, vshell_mission_map.md §14 |
| Quit, End of game, launch warning, Replay, CD prompt, DEBUG dialog | `boxes.c` | flow | §22 |

## The state half

All functions take explicit inputs (SAV bytes, the prefs blocks, tables
of VSHELL's image, VFS spans) and follow the formats
output convention (NULL output queries the size; errors leave output
untouched). Inputs on which the original faults or asserts are refused.

- **Preparation** (`prepare.h`): `es2_shell_prepare` turns a CAREER
  position, mode, difficulty, training controls, RNG state and a player
  source (CD defaults, a SAV, a MEC template or a frozen loadout) into
  `script.dat`, `MISSION.STR`, `mission.var` and `player.mec` in memory;
  the state (1,000 variables, RNG words, selectors, the preparation
  projection) is passed into later preparations. `default_tables` holds the
  164 bytes of VSHELL's image, passed in by the caller. `es2_shell_debrief_select` is the debrief reader.
- **SAV construction** (`save.h`): `es2_shell_default_save` (the career SAV
  after VSHELL's new-career loaders), `es2_shell_plan_save` (a loadout plan
  on any SAV), `es2_shell_prepared_save` and `es2_shell_loaded_save` (the
  memory state after a preparation and after a load: inventory lists
  reversed, because the original reader prepends).
- **Slots** (`slots.h`): `SAV/GAMEFILE.STR` as VSHELL holds, writes and
  marks it; the Saved games row text under the edit width rule
  (`es2_shell_slot_row_typed`); the slot statistics `0x4309f0`/`0x430ec4`/`0x4377bc`.
- **Campaign** (`campaign.h`): `es2_shell_results` applies a finished
  mission as the results path `0x40f09f` does (SAV, next CAREER position,
  return path, final word, debrief record and text, rewritten
  `MISSION.STR`, advanced RNG); `es2_shell_new_game` is the new career
  `0x40e8a5`; `es2_shell_record_condition` the squad panel's condition.
- **Service bay** (`hangar.h`, `editors.h`): the WEAPONS mount, click rule,
  guidance store and rebalance; `es2_shell_editors` applies every button
  and row of REPAIR, BUILD, ARMORY, CREW and both scraps to a SAV with the
  session the screens keep between presses; the launch files of `0x40f68c`.
- **Practice** (`practice.h`): the practice screen's entries and buttons,
  INSTANT ACTION, the DEBUG dialog's ACCEPT, and the mode-0 launch they end
  in (new game, posted Use Default, preparation, the slot-11 SAV).
- **Texts** (`texts.h`): the mission screen's pages 0, 1 and 4 (sector
  title, campaign text, briefing/objectives/intelligence, the debrief
  words) and the text panels' line and page split.
- **Movies and CD** (`movies.h`): the queue `0x41e8c0`, the 86 names, the
  selection rules per event, the player's control flow; `es2_shell_cd_check`
  and `es2_shell_cd_prompt`.
- **Sounds** (`sounds.h`): music choice by prefs byte 5 and its start-up
  flip, the three effects with their gates, focus gain/loss, the ramp.
- **Flow** (`flow.h`): the shown screen and box, the screen word `0x4758b8`
  with its leave dispatch, the handlers' effects (events are VSHELL's
  handler addresses from the layout table), the boxes' decisions, the `-X`
  entry dispatch and the exit save; I/O is host callbacks.
- **Engine** (`engine.h`): the per-process state and its operations
  (start with the RNG reset and `GetTickCount() & 127` draws, new career,
  slot load, Use Default preparation, results path, the WEAPONS view and
  operations, Rock & Roll's checks and the launch files).

## The graphical half

- **Frame and loop** (`gui/frame.h`, `events.h`, `loop.h`, `headless.h`):
  the 8-bit frame, the colour table from DPL records, the dirty-rectangle
  queue and present, the window procedure's mouse and key paths, the event
  queue, the main loop. Headless mode runs a script of window messages and
  shell actions and reports one line per present with the SHA-256 of the
  frame, the colour table and the screen.
- **Drawing** (`draw.h`, `gui/text.h`, `gui/canvas.h`): the blitter
  `0x45a09c` (clip, mirror modes, index 0 transparent unless flag `0x10`),
  DBM/DBA decode, DPL conversion; glyphs of a DFN font, alignment, the
  index-41 recolour, the edit field's width rule, the text pages.
- **Widgets** (`gui/widgets.h`, `layout.h`): VSHELL's window framework
  (tree, show/hide and the events they post, the modal box pair, hit test,
  hover, capture, drain rules, every class's press/release/fire rule and
  draw, the 20-record timer list, the WinConsumer keys). The layout table
  is compiled from `data/vshell_layout.json` by
  `tools/gen_layout_table.py` into `gui/layout_table.h`
  (`layout_table_test` checks it against the JSON).
- **Screens, boxes, map, pictures**: one file each (table above); every
  press is a flow event or a library operation; the mission map is a raster
  layer reproducing VSHELL's drawing context and primitives; the Herc
  pictures read `gam\arm_*.dat`, `gam\rpr_*.dat` and the hot-spot files
  ([herc_pictures.md](../docs/formats/herc_pictures.md)).
- **Sound and movies** (`gui/sound.h`, `src/win/mci_player.c`): the four
  samples of `VOL\SHLSOUND.VOL` over an audio device (the last 44 bytes cut
  as VSHELL's loader does); the own MCI player with VSHELL's command strings.

## Front ends and host

`es2launch.exe` starts the shell named by `ES2_LAUNCH_SHELL` with VSHELL's
argv. `es2menu.exe` is a console menu (New campaign, Continue, Rock & Roll,
Saved games, Preferences, Practice, Instant action, View demo, Credits,
Online manual, Quit) that prints the movie it would play; `es2shell.exe
--menu DIR` is the graphical shell; both share `src/host.c`. `menu.ini`
(written by `tools/es2menu_backend.py setup`) names the game directory,
`cd_root` and `chain_dir` (the evidence log, empty = off) and holds the
two policies (VSHELL's behaviour by default): `cd_check =
original | skip` and `online_manual = open | text | hide`; any other value
refuses to start. `shell.log` in the menu directory records the flow events,
the movies, the editor and bay steps, the sounds and the notices. A missing
or foreign `data\language.cfg` is VSHELL's assertion (report text,
`assert.log`, exit code 1). Deviations from VSHELL are listed next to the
contracts (for example: no DPI awareness is declared, so Windows enlarges the
window as it does VSHELL's; the Alert box only while windowed).

Headless: `es2shell.exe --menu DIR --headless --script FILE --report FILE`
with the verbs `click <screen>.<NN>` (a control of the layout table),
`type`, `key`, `step`, `expect`, `snap`, `dump`; `es2shell.exe --headless
--script FILE --report FILE` without `--menu` runs a test pattern.

## Tools

- `tools/es2prep.py --core <es2prep_core.exe> --cd-root <cd> --campaign G P --seed N`
  prepares one mission (`--all` every CAREER entry; `--output <new dir>`
  seals the four files with a schema-1 manifest and refuses existing paths,
  installations and non-ignored repository paths). Controls: `--mode`,
  `--difficulty`, `--training-options`, `--preset-index`, `--preset-flag`,
  `--save`, `--player-template`, `--loadout`, `--variables`, `--rng-state`,
  `--reset-selectors`, `--language ENG|FRE|GER`.
- `tools/es2loadout.py --loadout shell/loadouts/<name>.json` turns a loadout
  JSON into a SAV, prepares it with the library and with the mapped original
  (`es2shellprep.exe`), and seals a launch set only when all four files, the
  RNG/reset state and the SAV round trip are identical (`--output
  <es2-data>/oracle/launch/<name>`, `--base-sav <SAV>`). Schema 1:
  `mission` or `campaign [group, position]`, `language`, `seed`,
  `difficulty`, `player {herc, weapons}`, up to three `team` members
  (`pilot {group, index}` or a roster name, `herc`, `weapons`),
  `availability`, `variables`, `player_name`, `player_rank`, `ui_rules =
  enforce | report`. The manifest records every hash, the resolved loadout,
  the findings and the unchecked conditions.
- `tools/es2campaign.py --save <SAV> --mission-var <file> --results <file> [--output-save <new.sav>] [--prefs ...]`
  applies the results path; `tools/es2chain.py` plays a campaign chain
  through the launcher; `tools/es2menu_backend.py` writes `menu.ini` and
  serves the real tests' `new_game_request`.
- `bazel run //shell:es2shell -- dummy -eggplant -X1` (window),
  `bazel run //shell:es2movie -- --cd-root <cd> intro`, `bazel run //shell:es2sound`.
  The full start: `tools\reference-game\setup\start-own-menu.ps1
  -InstallName rebuild-r111 -Shell gui` (`-Shell vshell` for the original,
  `-Unattended` for the scripted chain).

Build once with a documented output base, then use the binaries:

```powershell
Set-Location <es2>
$base = 'build/bazel/es2-shell'
bazel --output_base=$base build //shell:es2prep_core //tools/reference-game/shell-invitro:es2shellprep `
  --repo_contents_cache="$base-contents" --lockfile_mode=error
$python = '<es2-dependencies>/python/python.exe'
$core = "$base/execroot/_main/bazel-out/x86_windows-fastbuild/bin/shell/es2prep_core.exe"
& $python -B shell/tools/es2prep.py --core $core --cd-root build/extracted/cd --campaign 1 0 --seed 0
```

## Verification

- **Unit tests** (`tests/test_*.c`, synthetic data, no original bytes as
  fixtures): one per library and screen; `flow_test` (the state machine
  with a synthetic host), `widgets_test` (every class and event rule, the
  22 screens built from the table), `help_test` (nothing is started).
- **Real tests** (`*_real_test`, skipped without `ES2_DATA_ROOT`): the
  library against VSHELL's own functions in the in-vitro harness
  ([shell_invitro.md](../docs/reference-game/shell_invitro.md)): the
  preparation of every CAREER mission and the differential sweep,
  loadouts, slots and new careers, the editors, statistics, practice, texts
  (three languages), the blitter and decoder, text and widget draws, the
  mission map byte for byte, movies, help, the condition; `menu_real_test` runs every text-menu
  session against the original and `chain_real_test` replays a menu
  campaign through it (`ES2_CHAIN_VERIFY`); `gui_route_real_test` drives
  `es2shell --headless` through Route A and the Saved games route and
  compares its files with `es2menu`'s. The references are pinned by size
  and SHA-256 in `tools/reference-game/shell-invitro/reference_pins.py`
  and read from a measurement clone only where a copy with the pinned hash
  exists (`timing-r111*`, `calibration-r111`; never `native-r111`). The
  five longest real tests are sharded.
- **Screen tests** (`screen_test --suite <screen>`, `tests/screen_*.c` over
  `screen_harness.c`): the shell on a scripted platform; verbs `step`,
  `move`, `ldown`/`lup`, `key`, `click CONTROL` (the centre of the layout
  table's rectangle in the capture tour's click form), `press`, `type`, `snap LABEL`
  (frame, table and screen hashes). With `ES2_DATA_ROOT` the harness reads
  the capture tour's `frames.json` from the newest `logs\vshell-screens-*` (or
  `--test_env=ES2_VSHELL_SCREENS=...`) and compares each snap with the
  VSHELL frame of the same `<run>:<screen>:<state>`; a difference is one
  line, classified as `palette`, `geometry`, `font` or `state`. Without a
  capture directory the real comparison prints SKIP. Known differences are
  rows of `tests/screen_known.c`: such a snap passes as
  `known` while its first difference keeps that class, and fails once the
  difference is gone (the row is then dropped). On a reference mismatch the
  own frame is written to Bazel's undeclared outputs
  (`<case>.<snap>.idx`/`.rgb`, never into the repository). A new screen's cases
  go into `tests/screen_<screen>.c` with `ES2_SCREEN_SUITE(<screen>, cases)`;
  the BUILD glob picks the file up.
- **Whole frames against VSHELL** (`gui_tour_real_test`): es2shell
  headless on the tours' game_r path (main menu, CONTINUE GAME, bay, WEAPONS,
  MISSION) on a career it builds itself (new career Z1, seed 12, CREW and
  BUILD steps), each snap's index and colour-table hash compared with the
  newest session's `frames.json`; the screen cases cover single screens,
  this test the compositions (vshell_ui.md §41). `screen_known.c` is empty.
- **Without VSHELL.EXE** (`no_vshell_real_test`): es2shell's career
  and tour of `gui_tour_real_test` and es2menu's PRACTICE MISSIONS and
  INSTANT ACTION, once with `ES2_DATA_ROOT` as `cd_root` and once with a
  temporary mirror of it (hard links) without `VER95\VSHELL.EXE`:
  exit codes, reports, console output and the DATA and SAV files are equal.
- **Mutations:** which mutation each test caught:
  [mutation_witnesses.md](mutation_witnesses.md).
- **Live:** `tools\check.ps1 -Live -OwnShell` runs the unattended chain
  (text and gui): `es2menu`/`es2shell` from a script, `es2launch`, DBSIM
  with the oracle settings, the tick log against the `c1_01-success` golden.
  `-OwnCdRoot <dir>` points the own executables at another CD root; both
  chains work with a copy without `DBSIM.EXE` and `VSHELL.EXE` (es2-data
  `logs\check-r13-1`).

## VSHELL's data

es2shell and es2menu never open `VER95\VSHELL.EXE`. VSHELL's tables,
literals and class icon are the span store `:vshell_store`
(`data/vshell_store_data.c`, a generated text source); readers take the sentinel `ES2_VSHELL_STORE`, and
`es2_shell_exe_span` answers it only. `cd_root` names the CD for the VOLs,
the texts, the movies and the manual. The section walk over a file image is
test code (`tests/vshell_file.c`, `:vshell_file`), installed with
`es2_shell_set_file_reader` by the tests that compare the store with the
reference file (`vshell_store_real_test`, `practice_test`, `engine_test`,
the screen cases of practice and WEAPONS). The window icon is
`es2_gui_store_icon` (vshell_presentation.md, "the own shell's frame,
presenter and window", deviation 11). The Python tools (`tools/*.py`)
are reference and test tooling and still read the file.
[vshell_presentation.md](../docs/reverse-engineering/vshell_presentation.md),
sections "the VSHELL span store" and "es2shell and es2menu without
VSHELL.EXE".
