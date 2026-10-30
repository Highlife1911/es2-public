# Shell: the own `VSHELL.EXE`

`shell/` reconstructs VSHELL as own code (the original's behaviour, no
modernisation): a portable state half (C17, no Win32, no file I/O), a portable
graphical half over VSHELL's 640 × 480 8-bit frame, and thin Windows front ends.
Every state change is a library call that is byte-equal to VSHELL's own function
in vitro; the screens only draw and route clicks. Contracts:
[vshell_ui.md](../docs/reverse-engineering/vshell_ui.md) (per screen and
operation), [vshell_preparation.md](../docs/reverse-engineering/vshell_preparation.md)
(preparation, SAV, results path), the other `vshell_*.md` documents and
[launcher.md](../docs/reverse-engineering/launcher.md) (start-up, the text menu,
the state engine). Status: [README.md](../README.md#status).

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

All libraries are private to the package; `//tools/reference-game` sees
`es2prep_core`, `data/vshell_layout.json`, `tools/*.py` and
`tests/real_acceptance.py`. `backend_data.bzl` (`BACKEND_DATA`) lists the
runfiles the backend-driven real tests share. Sources are under `shell/src/`
and `shell/include/es2/shell/`.

| Target (`//shell:...`) | What it is |
|---|---|
| `:bytes` | little-endian byte access shared by every library |
| `:flow` | the state half: preparation, SAV, slots, campaign and results path, the service-bay editors, practice, mission texts, movie queue, CD check, the flow (screen word, transitions and boxes as a state machine with host callbacks) |
| `:engine` | the state VSHELL keeps in one process (SAV image, mode and final words, RNG, selectors) and its operations; the text presenter's menu logic |
| `:host`, `:help` | what both front ends share: `menu.ini`, the file layer over `DATA`/`SAV`, slots, the practice launch, CD check; the ONLINE MANUAL |
| `:gui`, `:gui_sdl` | the frame, dirty-rectangle queue and present, VSHELL's mouse and key paths, main loop, headless mode; the SDL3 window and presenter |
| `:draw` | VSHELL's blitter, DBA/DBM decode, DPL palettes, the main-menu background sequence |
| `:screens` | the widget toolkit, the generated layout table of the 22 screens, every screen, the modal boxes, the mission map, the squad panel's Herc pictures |
| `:sounds`, `:gui_sound_sdl` | the shell sounds as logic (music choice, prefs byte 5 flip, three effects, ramp, focus) and their SDL3 playback |
| `:mci_player` | the movie player over MCI with the IV32 binding of `//media:iv32_bind` |
| `:gui_app` | the graphical presenter: builds every screen, supplies the CD's VOLs and VSHELL's span store, shows the flow's screen and box, plays movies and sounds |
| `:es2menu` | the text front end of the flow (console); the test front end |
| `:es2shell` | the graphical shell (GUI subsystem); `--menu DIR <VSHELL argv>` under `es2launch`, `--headless --script` for tests, without `--menu` a test pattern |
| `:es2prep_core` | the preparation core behind the Python tools (hex-pipe batches; not a stable API) |
| `:es2movie`, `:es2sound` | manual tools: play one movie, play the shell sounds |

Dependencies: `//formats:*` → `:flow` → `:engine` → `:screens` → `:gui_app`
→ `es2shell`; `:flow` → `:host` → `es2menu`/`es2shell`; `:gui` → `:gui_sdl`.

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
| REPAIR `0x4326c7`, BUILD `0x445de8`, ARMORY `0x4484c4`, CREW `0x441548`, the scrap warnings | `repair.c`, `build_armory.c`, `crew.c` | `editors.h` | §12, §17, §30 to §32 |
| Mission screen `0x442bc4` (pages 0, 1, 4), the map | `mission.c`, `mission_map.c` | `texts.h`, `movies.h`; map inputs from the host | §16, §33, vshell_mission_map.md §14 |
| Quit, End of game, launch warning, Replay, CD prompt, DEBUG dialog | `boxes.c` | flow | §22 |

## The state half

The headers are the API (`prepare.h`, `save.h`, `slots.h`, `campaign.h`,
`hangar.h`, `editors.h`, `practice.h`, `texts.h`, `movies.h`, `sounds.h`,
`flow.h`, `engine.h`). All functions take explicit inputs (SAV bytes, the prefs
blocks, tables of VSHELL's image, VFS spans) and follow the formats output
convention (NULL output queries the size; errors leave output untouched).
Inputs on which the original faults or asserts are refused. Contracts the
headers do not carry:

- `es2_shell_prepare` produces `script.dat`, `MISSION.STR`, `mission.var` and
  `player.mec` in memory; the state (1,000 variables, RNG words, selectors, the
  preparation projection) is passed into later preparations. `default_tables`
  holds the 164 bytes of VSHELL's image and is passed in by the caller.
- `es2_shell_prepared_save` and `es2_shell_loaded_save` give the memory state
  after a preparation and after a load, with inventory lists reversed because
  the original reader prepends.
- `es2_shell_results` applies a finished mission as `0x40f09f` does;
  `es2_shell_new_game` is `0x40e8a5`. The practice mode-0 launch ends in a new
  game, a posted Use Default, the preparation and the slot-11 SAV.
- The flow keeps the screen word `0x4758b8` with its leave dispatch; events are
  VSHELL's handler addresses from the layout table; I/O is host callbacks. The
  engine's start resets the RNG and draws `GetTickCount() & 127` times.

## The graphical half

- Headless mode (`gui/headless.h`) runs a script of window messages and shell
  actions and reports one line per present with the SHA-256 of the frame, the
  colour table and the screen.
- The blitter `0x45a09c` clips and mirrors; index 0 is transparent unless flag
  `0x10`. The index-41 recolour and the edit field's width rule are in
  `gui/text.h`.
- The layout table is compiled from `data/vshell_layout.json` by
  `tools/gen_layout_table.py` into `gui/layout_table.h` (`layout_table_test`
  checks it against the JSON).
- Every screen press is a flow event or a library operation; the mission map is
  a raster layer reproducing VSHELL's drawing context and primitives; the Herc
  pictures read `gam\arm_*.dat`, `gam\rpr_*.dat` and the hot-spot files
  ([herc_pictures.md](../docs/formats/herc_pictures.md)).
- Sound uses the four samples of `VOL\SHLSOUND.VOL` (the last 44 bytes cut as
  VSHELL's loader does); movies go through the own MCI player with VSHELL's
  command strings.

## Front ends and host

`es2launch.exe` starts the shell named by `ES2_LAUNCH_SHELL` with VSHELL's
argv. `es2menu.exe` is a console menu that prints the movie it would play;
`es2shell.exe --menu DIR` is the graphical shell; both share `src/host.c`.
`menu.ini` (written by `tools/es2menu_backend.py setup`) names the game
directory, `cd_root` and `chain_dir` (the evidence log, empty = off) and holds
the two policies (VSHELL's behaviour by default): `cd_check =
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
  installations and non-ignored repository paths); `--help` lists the controls.
- `tools/es2loadout.py --core <es2prep_core.exe> --cd-root <cd> --loadout shell/loadouts/<name>.json`
  turns a loadout JSON into a SAV, prepares it with the library and with the
  mapped original (`es2shellprep.exe`), and seals a launch set only when all
  four files, the RNG/reset state and the SAV round trip are identical
  (`--output <es2-data>/oracle/launch/<name>`, `--base-sav <SAV>`). The
  manifest records every hash, the resolved loadout, the findings and the
  unchecked conditions. Loadout schema 1 and `ui_rules = enforce | report`:
  [vshell_preparation.md](../docs/reverse-engineering/vshell_preparation.md).
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
$base = "$PWD/build/bazel/es2-shell"
bazel --output_base=$base build //shell:es2prep_core //tools/reference-game/shell-invitro:es2shellprep `
  --repo_contents_cache="$base-contents" --lockfile_mode=error
$core = "$base/execroot/_main/bazel-out/x86_windows-fastbuild/bin/shell/es2prep_core.exe"
& python -B shell/tools/es2prep.py --core $core --cd-root build/extracted/cd --campaign 1 0 --seed 0
```

## Verification

- **Unit tests** (`tests/test_*.c`, synthetic data, no original bytes as
  fixtures): one per library and screen; `flow_test` (the state machine
  with a synthetic host), `widgets_test` (every class and event rule, the
  22 screens built from the table), `help_test` (nothing is started).
- **Real tests** (`*_real_test`, skipped without `ES2_DATA_ROOT`): the
  library against VSHELL's own functions in the in-vitro harness
  ([shell_invitro.md](../docs/reference-game/shell_invitro.md)).
  `menu_real_test` runs every text-menu session against the original,
  `chain_real_test` replays a menu campaign through it (`ES2_CHAIN_VERIFY`),
  `gui_route_real_test` drives `es2shell --headless` through Route A and the
  Saved games route and compares its files with `es2menu`'s. The references
  are pinned by size and SHA-256 in
  `tools/reference-game/shell-invitro/reference_pins.py` and read from a
  measurement clone only where a copy with the pinned hash exists
  (`timing-r111*`, `calibration-r111`; never `native-r111`).
- **Screen tests** (`screen_test --suite <screen>`, `tests/screen_*.c` over
  `screen_harness.c`): the shell on a scripted platform; verbs `step`, `move`,
  `ldown`/`lup`, `key`, `click CONTROL` (the centre of the layout table's
  rectangle), `press`, `type`, `snap LABEL` (frame, table and screen hashes).
  With `ES2_DATA_ROOT` the harness reads the capture tour's `frames.json` from
  the newest `vshell-screens-*` directory of the es2-data log root (or `--test_env=ES2_VSHELL_SCREENS=...`)
  and compares each snap with the VSHELL frame of the same
  `<run>:<screen>:<state>`; a difference is one line, classified as `palette`,
  `geometry`, `font` or `state`; without a capture directory the real
  comparison prints SKIP. Known differences are rows of `tests/screen_known.c`:
  such a snap passes as `known` while its first difference keeps that class and
  fails once the difference is gone (drop the row). On a mismatch the own frame
  goes to Bazel's undeclared outputs (`<case>.<snap>.idx`/`.rgb`). A new
  screen's cases go into `tests/screen_<screen>.c` with
  `ES2_SCREEN_SUITE(<screen>, cases)`; the BUILD glob picks the file up.
- **Whole frames against VSHELL** (`gui_tour_real_test`): es2shell headless on
  the tours' game_r path (main menu, CONTINUE GAME, bay, WEAPONS, MISSION) on a
  career it builds itself (new career Z1, seed 12, CREW and BUILD steps), each
  snap's index and colour-table hash compared with the newest session's
  `frames.json` (the compositions, vshell_ui.md §41). `screen_known.c` is empty.
- **Without VSHELL.EXE** (`no_vshell_real_test`): that career and tour and
  es2menu's PRACTICE MISSIONS and INSTANT ACTION, once with `ES2_DATA_ROOT` as
  `cd_root` and once with a temporary hard-link mirror without
  `VER95\VSHELL.EXE`: exit codes, reports, console output and the DATA and SAV
  files are equal.
- **Mutations:** [mutation_witnesses.md](mutation_witnesses.md) lists which
  mutation each test caught.
- **Live:** `tools\check.ps1 -Live -OwnShell` runs the unattended chain (text
  and gui): `es2menu`/`es2shell` from a script, `es2launch`, DBSIM with the
  oracle settings, the tick log against the `c1_01-success` golden.
  `-OwnCdRoot <dir>` points the own executables at another CD root; both chains
  work with a copy without `DBSIM.EXE` and `VSHELL.EXE`.

## VSHELL's data

es2shell and es2menu never open `VER95\VSHELL.EXE`. VSHELL's tables, literals
and class icon are the span store `:vshell_store` (`data/vshell_store_data.c`,
generated); readers take the sentinel `ES2_VSHELL_STORE`, and
`es2_shell_exe_span` answers it only. `cd_root` names the CD for the VOLs, the
texts, the movies and the manual. The section walk over a file image is test
code (`tests/vshell_file.c`, `:vshell_file`), installed with
`es2_shell_set_file_reader` by the tests that compare the store with the
reference file (`vshell_store_real_test`, `practice_test`, `engine_test`, the
screen cases of practice and WEAPONS). The window icon is
`es2_gui_store_icon`. The Python tools (`tools/*.py`) are reference and test
tooling and do read the file. See
[vshell_presentation.md](../docs/reverse-engineering/vshell_presentation.md),
sections "the VSHELL span store", "es2shell and es2menu without VSHELL.EXE" and
"the own shell's frame, presenter and window" (deviation 11).
