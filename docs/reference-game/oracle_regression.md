# Oracle regression

Every replacement must leave the game's behaviour unchanged. The regression plays
fixed, scripted scenes in a measurement clone with virtual time and compares them
with golden references recorded from a reviewed commit. A replacement in `compare`
or `replaced` mode passes only if the scene is identical. The same goldens accept
the own DBSIM, `es2sim.exe`, in [the own-process replay](#the-own-process-replay).

## Suites

The registry is [oracle-suites.json](../../tools/reference-game/oracle-suites.json);
how suites are prepared and registered is [oracle_suites.md](oracle_suites.md).
"Legacy C1_01" means a suite without a launch set: it runs on the clones' prepared
C1_01 files ([Launch sets for the legacy suites](#launch-sets-for-the-legacy-suites)).
An *outcome suite* ends the mission itself; DBSIM exits and writes `results.dat`
and `mission.var`.

| Suite | Script | Watches | Scope |
|---|---|---|---|
| `acceptance-60s` | [acceptance-60s.jsonl](../../tools/reference-game/input-scripts/acceptance-60s.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01, frames 0–1499 (run until at least 1,600 ticks): throttle, turns, reverse ([input_journal.md](input_journal.md#acceptance-result)) |
| `c1_01-success` | [c1_01-success.jsonl](../../tools/reference-game/input-scripts/c1_01-success.jsonl) | [c1_01-mission-hostiles.txt](../../tools/reference-game/watch-sets/c1_01-mission-hostiles.txt) | legacy C1_01, outcome suite: the whole mission to status 9: route, fights, `Q`, Tab/Enter (the C1_01 success run, `es2-data\logs\c1_01-dialog-20261006`) |
| `c1_01-collision` | [c1_01-collision.jsonl](../../tools/reference-game/input-scripts/c1_01-collision.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01, frames 0–2199 (run until at least 2,300 ticks): full throttle into the building ahead (collision and rollback at frame 1370), reverse, turn, second collision at frame 1995; the only suite that reaches 0x41a360's collision branch and the rollback 0x4027fc by driving |
| `c3_09-loadout-b` | [c3_09-loadout-b.jsonl](../../tools/reference-game/input-scripts/c3_09-loadout-b.jsonl) (derived replay journal of the owner recording) | [c3_09-loadout-b.txt](../../tools/reference-game/watch-sets/c3_09-loadout-b.txt) | launch set `c3_09-loadout-b` (Omicron C3_09, Ogre and a team of three, [oracle_suites.md](oracle_suites.md#suites-from-a-loadout)), outcome suite: owner play with mouse and keyboard, 8,886 frames until the player is destroyed and the end dialog is answered; object contact, building and vehicle damage, flights, impacts, sweep notifications |
| `c4_08-razor-plasma` | [c4_08-razor-plasma.jsonl](../../tools/reference-game/input-scripts/c4_08-razor-plasma.jsonl) (derived replay journal of the owner recording) | [c4_08-razor-plasma.txt](../../tools/reference-game/watch-sets/c4_08-razor-plasma.txt) | launch set `c4_08-razor-plasma` (Bravo C4_08, ice world, Razor with two plasma cannons, no team), outcome suite: owner play, 2,094 frames ending with the player's self-destruction and the end dialog; live `building_hit` calls |
| `c3_10-loadout-hercs` | [c3_10-loadout-hercs.jsonl](../../tools/reference-game/input-scripts/c3_10-loadout-hercs.jsonl) (derived replay journal of the owner recording) | [c3_10-loadout-hercs.txt](../../tools/reference-game/watch-sets/c3_10-loadout-hercs.txt) | launch set `c3_10-loadout-hercs` (Omicron C3_10, player Tomahawk, team Raptor II, Maverick, Outlaw), outcome suite: owner play, 936 frames with plasma fire and team members shot and destroyed, to the end dialog; with the other suites every one of the nine Hercs appears |
| `c1_01-cockpit-keys` | [c1_01-cockpit-keys.jsonl](../../tools/reference-game/input-scripts/c1_01-cockpit-keys.jsonl) | [c1_01-mission-hostiles.txt](../../tools/reference-game/watch-sets/c1_01-mission-hostiles.txt) | legacy C1_01, frames 0–999 (run until at least 1,100 ticks): the `c1_01-success` input up to frame 960 plus the cockpit target keys `'` (frames 105, 858, 862, 870) and `;` (855, 866); live calls of `cockpit_nearest` (4) and `cockpit_select` (2) |
| `c3_10-own-building-fire` | [c3_10-own-building-fire.jsonl](../../tools/reference-game/input-scripts/c3_10-own-building-fire.jsonl) | [c3_10-loadout-hercs.txt](../../tools/reference-game/watch-sets/c3_10-loadout-hercs.txt) | launch set `c3_10-loadout-hercs`, frames 0–699: the owner's C3_10 opening, then fire at an own building (the sweep's ally notification) |
| `c1_01-free-camera` | [c1_01-free-camera.jsonl](../../tools/reference-game/input-scripts/c1_01-free-camera.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01 with the DBSIM argument `-SPRUNKNOWN` (registry `game_arguments`), frames 0–999: cheat camera V and Ctrl+F free flight (mode 0 of `0x4011a0`) |
| `c1_01-mouse` | [c1_01-mouse.jsonl](../../tools/reference-game/input-scripts/c1_01-mouse.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01, frames 0–599: right-button drag, align, right double click, a modal alert answered by mouse |
| `c1_01-keys` | [c1_01-keys.jsonl](../../tools/reference-game/input-scripts/c1_01-keys.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01, frames 0–699: B ×3, `-`, `=`, Num−, Num+, Alt+1/Alt+2, `[`, `]`, `\` and Scroll Lock; live `group_toggle` calls |
| `c1_02-probe` | [c1_02-probe.jsonl](../../tools/reference-game/input-scripts/c1_02-probe.jsonl) | [c1_02-probe.txt](../../tools/reference-game/watch-sets/c1_02-probe.txt) | launch set `c1_02-probe` (C1_02, Tomahawk, no team), outcome suite: no movement, Q at 1200 answered with Enter, Q at 1500 with Tab, Enter; the mission ends with status 5 (incomplete) |
| `c1_08-escort-route` | [c1_08-escort-route.jsonl](../../tools/reference-game/input-scripts/c1_08-escort-route.jsonl) | [c1_08-escort.txt](../../tools/reference-game/watch-sets/c1_08-escort.txt) | launch set `c1_08-escort` (C1_08, mission mode 3), frames 0–1499: throttle up at 100–120, nothing else; `player_route` calls `order_actor` every frame |
| `c1_01-cheat-destroy` | [c1_01-cheat-destroy.jsonl](../../tools/reference-game/input-scripts/c1_01-cheat-destroy.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01 with `-SPRUNKNOWN`, outcome suite: Alt+Ctrl+, ×30 and Alt+Ctrl+D every four frames from 170 destroy the Herc (`+99h` at frame 222), Enter at the outcome alert (frame 430): player death in C1_01 |
| `c3_09-wingman-orders` | [c3_09-wingman-orders.jsonl](../../tools/reference-game/input-scripts/c3_09-wingman-orders.jsonl) | [c3_09-loadout-b.txt](../../tools/reference-game/watch-sets/c3_09-loadout-b.txt) | launch set `c3_09-loadout-b`, frames 0–3299: MFD group orders Alt+A/G/H/O/C/E/F and HDD map orders (1/2/3, D/A/F/T/G/O/C/E, Enter/Tab, X); `hk_mech_order_calls` 58, `hk_wingman_order_calls` 15, `hk_map_click_calls` 6 |
| `c5_05-moon-missiles` | [c5_05-moon-missiles.jsonl](../../tools/reference-game/input-scripts/c5_05-moon-missiles.jsonl) (derived replay journal of the owner recording; **not** identical to the recording, see [oracle_suites.md](oracle_suites.md#moon-missile-set-c5_05-moon-missiles); the deterministic replay is the suite) | [c5_05-moon-missiles.txt](../../tools/reference-game/watch-sets/c5_05-moon-missiles.txt) | launch set `c5_05-moon-missiles` (moon mission C5_05, WORLD8; Ogre with the three missile racks, team Apocalypse, Colossus, Samson), outcome suite: owner play, 8,262 frames: alerts answered by mouse, the objectives screen (F11) twice, EINSATZALARM (Q) answered with Enter twice, then Q, → and a click end the mission with success; live calls of `base_offsets`, `flyer_patrol` and `flyer_part` |
| `c3_09-orders-matrix` | [c3_09-orders-matrix.jsonl](../../tools/reference-game/input-scripts/c3_09-orders-matrix.jsonl) | [c3_09-loadout-b.txt](../../tools/reference-game/watch-sets/c3_09-loadout-b.txt) | launch set `c3_09-loadout-b`; the wingman-order matrix: the `c3_09-wingman-orders` prefix to frame 2404, then repeated, near/far and re-pick orders (HDD map 0Dh/0Eh/11h/0Ch/0Bh, Alt+G/H/A), 4,200 frames (`mech_order` 83, `wingman_order` 27, `map_pick` 11 calls) |
| `c5_05-orders-flyer` | [c5_05-orders-flyer.jsonl](../../tools/reference-game/input-scripts/c5_05-orders-flyer.jsonl) | [c5_05-moon-missiles.txt](../../tools/reference-game/watch-sets/c5_05-moon-missiles.txt) | launch set `c5_05-moon-missiles`: the recorded prefix to frame 176, then `'` + Alt+A on the FLYER 15 and an HDD map 0Bh order, 1,400 frames; live calls of `flyer_attack` (45) |
| `c1_01-cheat-disable-leg` | [c1_01-cheat-disable-leg.jsonl](../../tools/reference-game/input-scripts/c1_01-cheat-disable-leg.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01 with `-SPRUNKNOWN`, outcome suite: cheat damage on the own Herc, slot 6 ×10 then slot 7 ×11 disables it at frame 210 without destroying it; the alert at 315 answered with Enter; live `group_assess` status 3 (1 call) |
| `c1_01-cheat-leader` | [c1_01-cheat-leader.jsonl](../../tools/reference-game/input-scripts/c1_01-cheat-leader.jsonl) | [c1_01-mission-hostiles.txt](../../tools/reference-game/watch-sets/c1_01-mission-hostiles.txt) | legacy C1_01 with `-SPRUNKNOWN`: view 3 on the enemy leader h3, both weapons shot off with cheat damage, wingman attack order, h3 disabled at 1516; 1,700 frames (`weapon_eject` 2, `vehicle_part` 37, `attack_plan` 174) |
| `c1_01-cheat-keys` | [c1_01-cheat-keys.jsonl](../../tools/reference-game/input-scripts/c1_01-cheat-keys.jsonl) | [c1_01-mission-hostiles.txt](../../tools/reference-game/watch-sets/c1_01-mission-hostiles.txt) | legacy C1_01 with `-SPRUNKNOWN`: vehicle cheat keys, Esc display mode, camera mode 0 and the attached camera on flyer h10, h10 and h12 destroyed by cheat damage; 1,200 frames (`flyer_part` 4) |
| `c1_01-cheat-keys-plain` | [c1_01-cheat-keys.jsonl](../../tools/reference-game/input-scripts/c1_01-cheat-keys.jsonl) | [c1_01-mission-hostiles.txt](../../tools/reference-game/watch-sets/c1_01-mission-hostiles.txt) | the same script without `-SPRUNKNOWN`: the cheat keys take their flag-clear edges; 1,200 frames |
| `c1_01-options-dialog` | [c1_01-options-dialog.jsonl](../../tools/reference-game/input-scripts/c1_01-options-dialog.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01: the F12 (frame 300) and Alt+P (500) options dialog `0x45cff0`, each closed by a pump-keyed Esc, 700 frames; the nested root runs (`render_root` 3,576 against `command_key` 2,975 calls) and so does the stack budget of the nested root |
| `c1_01-keys-extra` | [c1_01-keys-extra.jsonl](../../tools/reference-game/input-scripts/c1_01-keys-extra.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01: Tab, Alt+W, Alt+9, Shift+B, Ctrl+1, Alt+S, the cheat keys without the flag, twist and align, the P and Ctrl+Q dialogs answered by Esc, the focus keys, 950 frames (`group_toggle` 1) |
| `c1_01-unarmed-keys` | [c1_01-unarmed-keys.jsonl](../../tools/reference-game/input-scripts/c1_01-unarmed-keys.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | launch set `c1_01-unarmed` (Outlaw with only an ECM): the weapon keys with no selected weapon, the digits (digit 1 switches the ECM, `ecm_state` from frame 360), 600 frames (`group_toggle` 2) |
| `c3_10-hunt-targ` | [c3_10-hunt-targ.jsonl](../../tools/reference-game/input-scripts/c3_10-hunt-targ.jsonl) | [c3_10-loadout-hercs.txt](../../tools/reference-game/watch-sets/c3_10-loadout-hercs.txt) | launch set `c3_10-targ-eo` (Tomahawk with ECM, TARG, laser, EO rack, plasma): target pick at 870, TARG cycling, wingman orders, two steered EO missiles, 2,400 frames; live calls of `targ_next` (7), `targ_aim` (104) and `missile_steer` (50); `hunt` 13,400, `target_search` 11 |
| `c4_08-slope-dive` | [c4_08-slope-dive.jsonl](../../tools/reference-game/input-scripts/c4_08-slope-dive.jsonl) | [c4_08-razor-plasma.txt](../../tools/reference-game/watch-sets/c4_08-razor-plasma.txt) | launch set `c4_08-razor-plasma`, outcome suite: the recording to frame 1099, then a banked dive until the Razor crashes (about frame 1300); the outcome alert at 1319 is answered with Enter (`mech_fly` 1,319) |
| `c4_05-convoy` | [c4_05-convoy.jsonl](../../tools/reference-game/input-scripts/c4_05-convoy.jsonl) | [c4_05-convoy.txt](../../tools/reference-game/watch-sets/c4_05-convoy.txt) | launch set `c4_05-convoy` (Tomahawk, no team): along the route toward region 3 (not reached in the window), group 1 follows group 0 the whole time, 1,700 frames; live calls of `follow_step` (13,659) |
| `c1_08-scan` | [c1_08-scan.jsonl](../../tools/reference-game/input-scripts/c1_08-scan.jsonl) | [c1_08-escort.txt](../../tools/reference-game/watch-sets/c1_08-escort.txt) | launch set `c1_08-escort`, outcome suite: the `c1_08-escort-route` input to frame 1884, then a turn toward the listening post (trigger 1 at 1646; the scan objective is not reached), Q at 2600 answered |
| `c4_10-probe` | [c4_10-probe.jsonl](../../tools/reference-game/input-scripts/c4_10-probe.jsonl) | [c4_10-probe.txt](../../tools/reference-game/watch-sets/c4_10-probe.txt) | launch set `c4_10-probe`: along route 0, objective 0 (type 7 on buildings 0-2) evaluated, 600 frames; live calls of `group_flagged` (18) |
| `c1_06-cheat-disable` | [c1_06-cheat-disable.jsonl](../../tools/reference-game/input-scripts/c1_06-cheat-disable.jsonl) | [c1_06-maverick.txt](../../tools/reference-game/watch-sets/c1_06-maverick.txt) | launch set `c1_06-maverick`, DBSIM `-SPRUNKNOWN`, outcome suite: orbit view onto the Maverick, cheat damage slot 7 disables it (objective 0 true at frame 420), Q at 700 answered (`vehicle_part` 15, `region_break` 3) |
| `c2_02-identify` | [c2_02-identify.jsonl](../../tools/reference-game/input-scripts/c2_02-identify.jsonl) | [c2_02-identify.txt](../../tools/reference-game/watch-sets/c2_02-identify.txt) | launch set `c2_02-identify`: along route 1 into the building group 4; a building is spotted and objective 0 (type 6) is true at frame 1680, 1,800 frames; live calls of `group_marked` (26) |
| `c1_01-tape-record` | [c1_01-tape-record.jsonl](../../tools/reference-game/input-scripts/c1_01-tape-record.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01, DBSIM `-rc1tape` (records `c1tape.tap` in the clone root; the tape transaction restores the clone, the tape is evidence only): the recording sites of `control_record` with keys, mouse and a freeze, 600 frames (`mouse_queue` 4) |
| `train1-timers` | [train1-timers.jsonl](../../tools/reference-game/input-scripts/train1-timers.jsonl) | [train1.txt](../../tools/reference-game/watch-sets/train1.txt) | launch set `train1` (TRAIN1, mode 0, es2prep, equal to the mapped original): the start dialog answered with Enter at frame 0, full throttle toward route point 1; the timer chain fires triggers 0-6 every 213 frames, the player enters region 0 (trigger 7, frame 1208) and timer 7 fires trigger 8 (1251), 1,600 frames. Needs early arming (TRAIN1 makes no MCI call). `trigger_fire` 18,583, `mission_timer` 35,784, `timer_scan` 2,556 |
| `c1_01-joystick` | [c1_01-joystick.jsonl](../../tools/reference-game/input-scripts/c1_01-joystick.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01: a scripted joystick on device 0 (`joystick_script.py`, program `input-scripts/programs/c1_01-joystick.json`): axes both ways, throttle, rudder, hat four ways, buttons 1-8, 1,400 frames |
| `c4_08-joystick` | [c4_08-joystick.jsonl](../../tools/reference-game/input-scripts/c4_08-joystick.jsonl) | [c4_08-razor-plasma.txt](../../tools/reference-game/watch-sets/c4_08-razor-plasma.txt) | launch set `c4_08-razor-plasma`, outcome suite: the same joystick program flying the Razor until it hits the ground; the outcome alert at frame 1040 is answered with Enter (`flight_model` 1,041) |
| `c1_01-cheat-disable-late` | [c1_01-cheat-disable-late.jsonl](../../tools/reference-game/input-scripts/c1_01-cheat-disable-late.jsonl) | [c1_01-mission-hostiles.txt](../../tools/reference-game/watch-sets/c1_01-mission-hostiles.txt) | legacy C1_01, DBSIM `-SPRUNKNOWN`, outcome suite: the `c1_01-cheat-leader` prefix, freeze and single step at 1400/1410, the own Herc disabled from 1440 (disabled at 1586), the outcome alert answered with Enter at 1785 (exit code 3; `member_assess` 2, `group_assess` 1) |
| `c1_01-display-keys` | [c1_01-display-keys.jsonl](../../tools/reference-game/input-scripts/c1_01-display-keys.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01: HDD page keys, the F11 objectives screen answered with Enter, Esc/F1 display modes, F10 (mode 2) and V (mode 4, panel off, 330-480) with Enter, N, Backspace, T and Alt+Num+ reaching `command_key`'s own cases, F9, 800 frames |
| `c3_10-detail-options` | [c3_10-detail-options.jsonl](../../tools/reference-game/input-scripts/c3_10-detail-options.jsonl) | [c3_10-loadout-hercs.txt](../../tools/reference-game/watch-sets/c3_10-loadout-hercs.txt) | launch set `c3_10-loadout-hercs`: the options dialog twice (204: detail byte `0x4d1f43` 2 → 1, 600: back to 2), `prefs.cfg` unchanged at the end, 700 frames (`prefs_store` 2) |
| `c1_01-tape-play` | [c1_01-tape-play.jsonl](../../tools/reference-game/input-scripts/c1_01-tape-play.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01, DBSIM `-ptapes\demo2`: playback of the CD tape `DEMO2` with live Ctrl+B, held at 650 ticks (`hold`), below the original's own fault at `0x453127` ([oracle_suites.md](oracle_suites.md#tape-playback-suites)) |
| `c1_01-tape-demo` | [c1_01-tape-demo.jsonl](../../tools/reference-game/input-scripts/c1_01-tape-demo.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01, DBSIM `-D` with `demo_tape` `DEMO1`: demo playback, held at 880 ticks (`hold`), below the fault at `0x453127` |
| `c1_01-quit-dialog` | [c1_01-quit-dialog.jsonl](../../tools/reference-game/input-scripts/c1_01-quit-dialog.jsonl) | [c1_01-mission.txt](../../tools/reference-game/watch-sets/c1_01-mission.txt) | legacy C1_01, outcome suite: Ctrl+Q at 201 answered with Tab and Enter (quit): the mission ends at frame 201 and DBSIM exits with 0 |

The call counts are the hook counters of the suite's replay; the replay is
deterministic, so they are properties of the suite.

## Running a regression

```powershell
# compare the current tree with the golden references
& .\tools\reference-game\setup\run-oracle-regression.ps1
# with hooks
& .\tools\reference-game\setup\run-oracle-regression.ps1 -Hooks 'rng_next=compare'
# several clones at once (check mode only)
& .\tools\reference-game\setup\run-oracle-parallel.ps1 -InstallName timing-r111-p1,timing-r111-p2
# (re)record golden references from a clean worktree of a reviewed commit
git worktree add --detach build\wt-oracle-baseline <commit>
& .\tools\reference-game\setup\run-oracle-regression.ps1 -Mode record -RepositoryRoot build\wt-oracle-baseline -Force
```

- `-RepositoryRoot` selects whose measurement scripts and proxy sources are built
  and deployed; suites, scripts and watch sets always come from the tree that
  contains the runner. `record` refuses a dirty tree.
- Each run writes `<es2-data>\logs\oracle-<mode>-<utc>\` with the tick logs,
  normalized forms, comparison reports, per-suite `<suite>-entry.json` and
  `summary.json`; `check` exits 1 on any difference.
- `-Suite` selects suites, `-SuitesFile` takes an external draft registry
  (script and watch paths may be absolute), `-DryRun` prints the plan without
  building, deploying or starting a process, `-MockDirectory` is for synthetic
  fixture trees only. `-ExitGraceSeconds` (default 30) and
  `-SuiteTimeoutSeconds` (default 300) bound a suite's process.
- **Registry format.** Format 2 adds an optional `launch` name under
  `DataRoot/oracle/launch`, format 3 an optional per-suite `game_arguments`
  list ([below](#suite-dbsim-arguments-game_arguments)) and the optional
  `demo_tape` and `hold` ([Tape suites](#tape-suites)). The scripts do not read the `format`
  number; an entry without these fields runs with the fixed launch arguments,
  frame window and normalization.
- **Launch-aware suites.** The runner verifies all four manifest hashes of the
  launch set, deploys inside a six-file backup/restore transaction
  ([run-mission-outcome.ps1](../../tools/reference-game/setup/run-mission-outcome.ps1),
  `SuiteLaunch.psm1`) and adds `launch_set` hashes to the entry and the golden
  manifest. Outcome runs with a launch clear stale results, require a natural
  exit and keep both return files even if `mission.var` is unchanged.
  Restoration handles originally absent, newly created and deleted files,
  attempts every file after evidence-copy failures and keeps a recovery backup
  on failure. New launch sets and outputs must be fresh; live outputs stay under
  `DataRoot/logs`. Neither primary installs nor linked clone paths are allowed.

### DBSIM arguments on the command line

The launch scripts pass DBSIM `-eggplant -X2 -R0 -G`
([launcher.md](../reverse-engineering/launcher.md)). `-GameArguments` appends
more arguments to that line exactly as given and in the given order, for probe
runs that need another command line (for example the demo playback,
[rng.md](../reverse-engineering/rng.md#demo-path)).
It exists on `start-timing-mission.ps1`, `run-tick-log-mission.ps1`,
`run-oracle-regression.ps1` (and its helpers `run-prepared-suite.ps1`,
`run-mission-outcome.ps1`) and `run-oracle-parallel.ps1`.

```powershell
# one bounded run, no golden: the launch report shows the final command line
# (timing-r111-p3 is a spare clone: new-measurement-clone.ps1 -Number 3 -Apply)
& .\tools\reference-game\setup\run-tick-log-mission.ps1 -InstallName timing-r111-p3 -OutputDirectory <dir> -RunName r3 `
  -Ticks 1600 -GameArguments '-ptapes\demo1'
# a suite under the regression / the parallel runner (check mode against the goldens)
& .\tools\reference-game\setup\run-oracle-regression.ps1 -Suite acceptance-60s -GameArguments '-ptapes\demo1','-rrun'
& .\tools\reference-game\setup\run-oracle-parallel.ps1 -GameArguments '-ptapes\demo1'
```

- Quote every element (PowerShell would read an unquoted `-ptapes\demo1` as a
  parameter name). `pwsh -File` cannot bind such an array: there write
  `-GameArguments:-ptapes\demo1` (one element) or give
  `run-oracle-regression.ps1` the list as `-GameArgumentsJson '["-p..","-r.."]'`;
  `run-oracle-parallel.ps1` uses that form for its child runners.
- Refused before anything is configured or launched (the message names
  `GameArguments[n]`): an empty argument, one with whitespace or a double
  quote (`Start-Process` would split it), `-eggplant`, any `-X…`, any `-R…`
  (case-sensitive: `-r<name>`, recording a tape, stays allowed) and a language
  letter `-E…`/`-F…`/`-G…`, because the fixed line already holds them. A
  repeated fixed argument (`-G`) is refused as "already passed". Everything
  else is passed on as given; what DBSIM does with it is the caller's
  responsibility (`-p`/`-r`/`-D` overwrite `DATA\` files of the clone,
  [file_io.md](../reverse-engineering/file_io.md#dbsim-recording-and-playback)).
- Evidence: without the option the command line, the JSON keys and the bytes
  are unchanged. With it, `start-timing-mission.ps1` writes `game_arguments`
  next to the complete `arguments` list in `<run>-launch.json`; the runner
  JSON lines, `<suite>-entry.json` (and `launch` in it), `summary.json` and the
  parallel `summary.json` carry `game_arguments` too. The parallel runner
  accepts a suite only if its child reported the very list it was given.
- The goldens belong to the recorded command line: `record` mode refuses the
  option (a suite's own arguments come from the registry), and a `check` run
  with it normally ends `DIFFERENT` (exit 1) while the tick log and the output
  files are still written. `-DryRun` prints the list in every plan
  (`arguments.GameArguments`, parallel: `game_arguments`).
- `run-live-coverage.ps1` does not forward the option.
- Tests: `suite_scripts_test` (dry plans, collisions, record refusal, mock
  entries/summary), `oracle_speed_scripts_test` (runner with a launcher stub,
  parallel with a mock child, `start-timing-mission.ps1 -Preview` on a
  synthetic clone). No test starts a game or a process of the original.

### Suite DBSIM arguments (`game_arguments`)

A suite whose scene needs another DBSIM command line (the cheat switch
`-SPRUNKNOWN`, a tape) lists the extra arguments in its registry entry. The
field is optional; absent, `null` and `[]` all mean "none".

```json
{ "name": "c1_01-cheat-disable", "script": "...", "watches": "...", "ticks": 3000,
  "outcome": false, "game_arguments": ["-SPRUNKNOWN"] }
```

- **Value:** a JSON array of strings, appended to the fixed `-eggplant -X2 -R0
  -G` in the given order. Each element passes `Assert-GameArguments` (the rules
  of the command-line option above); a non-array, a non-string element or a
  refused argument stops the run before anything is configured, naming the suite.
- **Record and check, both runners.** `run-oracle-regression.ps1` runs every
  suite with its own arguments, in `record` and in `check` mode, through all
  three runner paths (tick-log, outcome, prepared). `run-oracle-parallel.ps1`
  (check only) hands the suite to a child `run-oracle-regression.ps1`, which
  does the same; the parallel runner only validates, plans and reports
  (`-SuitesFile` selects another registry and is passed on to the children).
- **Golden manifest.** A recorded `manifest.json` carries `game_arguments` (the
  complete list the run used) and `suite_game_arguments` (the registry part)
  only for a suite that has them; a manifest without the keys means "none".
- **A check refuses** (before a clone lock, an output directory or a process
  exists; in both runners, for the selected suites only) when the suite's
  registry `game_arguments` differ from the manifest's `game_arguments`
  (exact, ordered, case-sensitive; a missing key is the empty list): the
  message names the suite, both lists and the manifest path. Adding,
  removing or changing the field therefore needs a new golden
  (`-Mode record -Force` from a clean tree). A suite without a golden
  manifest is not refused here; the comparison fails on the missing golden.
- **Command line.** `-GameArguments` stays refused in record mode. In check
  mode it is appended after the suite's own arguments, for every selected
  suite, and refused (naming `GameArguments[n]` and the suite) when it repeats a
  suite argument (case-insensitive) or uses the same option letter
  (case-sensitive: `-Sother` against `-SPRUNKNOWN`), besides the collisions
  with the fixed arguments.
- **Evidence.** `<suite>-entry.json` has `game_arguments` (complete list),
  `suite_game_arguments` (registry part) and `launch.game_arguments`;
  `summary.json` keeps its top-level `game_arguments` for the command-line
  list only and gives a suite with registry arguments a `game_arguments` key
  in its `results` item (the complete list it ran with); suites without
  arguments have no such key. `-DryRun` shows `arguments.GameArguments` for
  such a suite (parallel: `suite_game_arguments`, suite name to complete list,
  only for suites that have arguments). The parallel runner counts a child as
  `identical` only if its result item names that same list.
- `run-live-coverage.ps1` delegates to `run-oracle-regression.ps1` and so
  honours the field without forwarding anything itself.
- Tests: `suite_scripts_test` (dry plans with and without the field, refused
  values, command-line combination and collisions, record into the manifest,
  check identical, mismatches changed/removed/added/other, manifests without
  the keys) and `oracle_speed_scripts_test` (parallel plan, children, reported
  lists, golden guard, collisions). Registering such a suite:
  [oracle_suites.md](oracle_suites.md#suites-with-dbsim-arguments).

### Tape suites

DBSIM's tape options write and overwrite clone files that the six-file
transaction does not know ([file_io.md](../reverse-engineering/file_io.md#dbsim-recording-and-playback)):
`-r<name>` writes `<name>.tap` into the working directory (the clone root);
`-p<name>` and `-D` restore the tape header over `DATA\script.dat`,
`player.mec`, `mission.var`, `restore.dat` and `object.str` and write
`TAPES\prefs.cfg` and `TAPES\keyjoy.cfg` (deleted again only by an exit
while playback is still on, `0x45f50a`); `-D` picks its tape by
`time() % count` from `TAPES\DEMOLIST.STR` (`0x45ceb8`). A suite is a
**tape suite** when its registry `game_arguments` contain a tape option
(case-sensitive `-r`, `-p`, `-D`, `-d`, after `-` or `/`) or when it has the
optional field `demo_tape`:

```json
{ "name": "c1_01-tape-demo", "script": "...", "watches": "...", "ticks": 1600,
  "outcome": true, "game_arguments": ["-D"], "demo_tape": "DEMO1" }
```

- **Plan** (`SuiteLaunch.psm1` `Get-SuiteTapePlan`, from the effective
  arguments; refused with the suite named before anything runs): exactly one
  tape option. `-r<name>` (record) and `-p<name>` (play) take a plain name
  (`[A-Za-z0-9][A-Za-z0-9_-]{0,31}`), `-p` also `tapes\<name>` (a tape of the
  installation, e.g. the CD's `TAPES\DEMO2.TAP`). `-D` (demo) needs
  `demo_tape`, one plain name of at most eight characters, and `demo_tape`
  needs `-D`. `-d` is refused (the dump file shares the tape name; no suite
  needs it), as is a second tape option (DBSIM keeps only the first name for
  all of them, `0x4d24da`).
- **Routing.** A tape suite always runs through `run-prepared-suite.ps1`
  (switches `-TapeTransaction`, `-DemoTape`; `run-mission-outcome.ps1` passes
  them on), with or without a launch set, and its report must not be stalled
  (an outcome suite must end by itself), as for a launch-aware suite. Tape
  options given only on the command line (`-GameArguments`) get no plan and
  stay the caller's responsibility.
- **Transaction.** Besides the six launch files it backs up and restores
  `DATA\restore.dat`, `DATA\object.str`, `TAPES\prefs.cfg`,
  `TAPES\keyjoy.cfg`, for `-D` also `TAPES\DEMOLIST.STR`, for `-r` the
  recorded `<name>.tap` (removed again, or an earlier file put back). A
  playback tape must exist in the clone (DBSIM would read a missing one
  through a null FILE*). For `-D` the runner writes a synthetic one-entry
  `TAPES\DEMOLIST.STR` (u32 body size, one group of one string, the
  `demo_tape` name, [str.md](../formats/str.md)): with one entry DBSIM skips
  the wall-clock pick (`0x45cf87`), so the demo is deterministic (a
  read-only list, as copied from the CD, is made writable for the run and
  comes back from the backup with its attributes).
- **Outputs.** None of these files is ever copied to `<run>-output` (a tape
  and the files playback writes carry copies of installation files, and
  goldens are committed). In playback suites the overwritten `script.dat`,
  `player.mec` (and `MISSION.STR`) are not captured either, and `mission.var`
  only when `results.dat` changed too (the mission ended and `0x42412c`
  rewrote it). A record suite's tape is reported by size and SHA-256 in the
  run report (`tape`); the report and `<suite>-entry.json` carry `tape_plan`,
  the dry plans too. Suites without a tape option have no plan, no extra
  restore files and no tape keys.
- **Hold.** During playback DBSIM skips the limiter (`0x45f465`), so the run
  is unpaced and the runner's 500 ms poll stops it hundreds of frames late
  and at a varying frame. The optional field `"hold": true` makes the proxy
  park the game thread after exactly `ticks` records (`[trace] hold_frame`,
  [tick_log.md](tick_log.md#hold-at-a-record)); the runner then stops it as
  usual. The regression runner passes `-HoldAtTicks` to
  `run-prepared-suite.ps1` (or `run-tick-log-mission.ps1`), which writes
  `hold_frame=<ticks>`; the dry plan's `arguments` carry `HoldAtTicks`.
  `hold` must be `true` or `false` and is refused with `outcome` true (an
  outcome suite ends by itself). Without the field, or with `false`, plans,
  INI and reports are unchanged.
- **Goldens.** `game_arguments` are guarded by the golden manifest;
  changing `demo_tape` also needs a new golden (`-Mode record -Force`).
- Tests: `suite_scripts_test` lane `lane_tapes` (plans and refusals, the
  demo-list bytes, the transaction for play, ended play, demo with a
  read-only list, a failing demo, record, an earlier tape and missing tapes,
  the switches of the two runners, `-HoldAtTicks` and the `hold` field in the
  dry plans and their refusals, regression dry plans and a mock check of
  three tape suites); the synthetic clone has stand-in `TAPES\DEMO*.TAP` files.
  Suites: [oracle_suites.md](oracle_suites.md#tape-record-and-playback).

### Intra-frame clock

All runs write `virtual_time_intra_frame=1`
([timing.md](../reverse-engineering/timing.md#intra-frame-clock-and-the-cockpit-instruments)):
without it the cockpit instruments are not drawn. 38 of the 40 golden manifests
record it; `acceptance-60s` and `c1_01-collision` predate the field and replay
identically with it (`oracle-parallel-20261008T104329975Z`, `-104421030Z`,
`-104439587Z`). Per frame the clock advances 9 to 16 times in active play; only
seven frames at the end of `c1_01-success` (modal and effect loops) reach the
cap of 46. `vt_intra_advances` is logged but not compared.

### Pacing

`run-oracle-regression.ps1 -VirtualTimePace 1` passes `virtual_time_pace=1` to
the suite and tick-log runners: the proxy holds each 47 ms step of virtual time
in wall time, so wall-clock effects (the SOS voice notices,
[main_loop.md "The notices against the paced original"](../reverse-engineering/main_loop.md#the-notices-against-the-paced-original))
come out as at the game's own speed. The default 0 is what every golden was
recorded with; record mode refuses 1. The replay stays identical paced
(`c3_10-detail-options`, `c1_01-options-dialog`); the dry plan shows the value
in every tick-log plan (`suite_scripts_test`).

## What is compared

[oracle_regression.py](../../tools/reference-game/scripts/oracle_regression.py)
(`normalize`, `compare`, `normalize-compare`) normalizes a run:

- **Ticks**, keyed by frame counter: completed clock, limiter flag, pause flag,
  24 timing bytes, both RNG states, player position, angles, speed, throttle,
  virtual-time limiter polls and stall advances, injected input and key-state
  fallbacks. Not compared: `sequence`, `qpc`, `ebx`, the player pointer, and
  `in_dropped`/`in_focus_dropped` (real desktop input discarded by the replay).
- **Mission watches**, as a sequence of changes: watches that contain heap
  pointers are decoded first (`mission_globals`, `player_group`, `objectives`,
  `triggers`, `route`); target pointers become `set`/`none`; all other watches
  compare as raw bytes.
- **Return files** (`results.dat`, `mission.var`) by SHA-256.
- **Exit code** of the game process, see
  [Exit code of the own DBSIM](#exit-code-of-the-own-dbsim).

The tick count must match as well. Suites that do not end by themselves are
stopped by a 500 ms poll, so their logged length varies (observed 1,766 vs
1,828 ticks); their comparison window is fixed by `frame_limit`. Heap addresses
differ between runs by a constant base offset, which is why pointer-bearing
watches are compared decoded (`es2-data\logs\c1_01-20261006`).

**Live coverage.** For per-Jcc outcomes of the original code,
[`run-live-coverage.ps1`](../../tools/reference-game/setup/run-live-coverage.ps1)
delegates each suite to this runner in `check` mode, then accepts coverage only
when the golden matches and the report has the expected process, hook
selection, site inventory and finalized window. Default hooks remain original;
`saturate` is recommended for full suites. Coverage has separate JSON/Markdown
reports and does not change tick fields or comparison rules. INT3 dispatch can
leave stack residue, so golden identity is mandatory; commands and open
hypotheses: [live coverage](live_coverage.md).

**Coverage with the regression.** `run-oracle-parallel.ps1 -LiveCoverage
original` runs the regression unchanged and then, only if every suite is
identical, a second pass on the same clones with every hook original and
`[coverage] hooks=all` (in `coverage-original\`). `-LiveCoverage run`
instruments the regression itself and counts only original code that still
executes under its hook modes. Both write `coverage-union.json`, `.md` and
`coverage-column.md` beside `summary.json` (which gains a `live_coverage`
object) and a coverage summary per suite; the `results` and the exit code stay
those of the comparison. Default `off`; further `-CoverageMode saturate|count`
(default saturate), `-CoverageReportEveryTicks` (default 0) and
`-CoverageTimeoutSeconds` (default 3,600 s per suite, passed as
`run-oracle-regression.ps1 -SuiteTimeoutSeconds` only to a pass with coverage
traps, which runs 10 to 20 times slower; verdict passes keep 300 s). The
meaning of the counts:
[live coverage](live_coverage.md#coverage-with-every-regression).

**Entry probes** use the same check-mode suites and goldens as ordinary check
runs. `run-live-coverage.ps1 -ProbeEntries ...` adds probes beside coverage;
`-ProbesOnly` measures just entries. A changed simulation or return file
invalidates the measurement and fails the run. Separate JSON/Markdown reports
keep per-suite counts and receiver histograms; saturated counts are lower
bounds. Neither probe nor coverage runs record goldens. Configuration and
commands: [live_coverage.md](live_coverage.md#entry-probes).

## Golden references

Stored in `<es2-data>\oracle\golden\<suite>\`: `normalized.json`, the raw
`tick-log.jsonl`, `output\` (return files of outcome suites) and
`manifest.json` (commit, proxy SHA-256, script and watch-set hashes, launch
report including the prepared mission file hashes, `game_arguments`,
`own_process_exit`). They are derived runtime evidence, not original files.

## Clone guard

The runners enforce one session per clone and only ever wait for or stop a
process they started. The shared code is
[CloneGuard.psm1](../../tools/reference-game/setup/CloneGuard.psm1).

**Per-clone lock.** `run-oracle-regression.ps1` takes the lock of its clone,
`run-oracle-parallel.ps1` the locks of all listed clones, before anything is
created, mounted or built. The lock is `.clone-locks\<clone>.json` in the
installation root (`Get-CloneInstallRoot`: `build\installs` for
the data repository, `<DataRoot>\installs` for any other `-DataRoot`), outside
the clone so `new-measurement-clone.ps1` never copies it. It records the
holder's PID, executable path and start time, a session hint
(`ES2_SESSION_HINT` if set, otherwise user, parent process and repository),
the purpose and the time. The holder keeps the file open (share: read only,
delete on close), so it cannot be deleted while the holder lives and it
disappears when the holder exits, crashes or is killed. A lock file that can
be deleted has no holder and is reclaimed as stale. Children of the holder
(the parallel runner's per-clone runners, and the configure/launch scripts a
runner calls) inherit the lock through `ES2_CLONE_LOCK_TOKEN`. Another
session is refused before any change, for example:

```
Clone timing-r111-p1 is in use by another session: holder PID 1234 (C:\...\pwsh.exe,
started 2026-10-08T20:00:00.0000000Z), session 'user@HOST, parent claude.exe PID 999,
repository <es2>', purpose 'run-oracle-parallel', locked since ...
```

`configure-tick-log.ps1 -Apply` and `start-timing-mission.ps1` (without
`-Preview`) refuse when another session's live lock covers the clone; called
directly without any lock they take none.

**Runner environment.** `run-oracle-parallel.ps1` and
`run-oracle-regression.ps1` set `ES2_*` variables for their children
(`ES2_CLONE_LOCK_TOKEN`, `ES2_PREPARED_PROXY`, `ES2_PARALLEL_CLONES`,
`ES2_TICK_BUFFERED`, `ES2_ORACLE_SPEED_CONFIG`, `ES2_LIVE_COVERAGE_CONFIG`,
`ES2_ORACLE_HIDDEN_WINDOW`) and give the calling PowerShell process back as
they found it in a `finally` (`Get-RunnerEnvironment` /
`Restore-RunnerEnvironment` in `CloneGuard.psm1`: a variable that did not
exist is removed, one that existed gets its old value; what the caller set on
purpose, e.g. `ES2_AFTER_LOAD_SNAPSHOT` or `ES2_RTL_CONFIG`, stays). Trap:
`[Environment]::SetEnvironmentVariable(name, $null)` in PowerShell 7 reaches
.NET as an empty string, so the variable stays, empty, and every later DBSIM
start gets more environment strings (another heap layout: after-load
snapshots of two equal runs differ). Remove with `[NullString]::Value` or
`$env:NAME = $null`. Tests: `oracle_speed_scripts_test` (section
`environment`), `suite_scripts_test` (lane `lane_environment`),
`clone_guard_test`.

**Foreign game processes.** Every refusal names the process: name, PID,
executable path and start time; the process is left alone.
`configure-tick-log.ps1 -Apply` first waits up to `-BusyWaitSeconds` (default
15) for game processes in the clone to exit by themselves, which covers a
DBSIM that was just stopped and still holds `ddraw.dll`; it never stops one.
`start-timing-mission.ps1` applies a machine-wide rule limited to what can
hurt a clone (`Assert-CloneMachineIdle` in `CloneGuard.psm1`): only game
processes (ES/VSHELL/DBSIM) that run from an installation under the
installation root (`Get-CloneInstallRoot`) count. A game in the target clone
always blocks ("Close the game in this clone ..."); a game in any other
installation (`native-r111`, the owner's primary one, and every other clone)
blocks ("... another installation is running, outside the permitted, separate
parallel clones"), except a DBSIM in one of the clones listed in
`ES2_PARALLEL_CLONES` (the neighbours of a parallel run). A process anywhere
else (the stand-in `dbsim.EXE` of `//shell:chain_real_test` in a Bazel temp
directory) is ignored. Paths are compared case-insensitively after full-path
normalisation (`\\?\` prefix, `..`, 8.3 short names expanded through
`GetLongPathNameW`, trailing separators) and by whole directory names, so
`installs-old` or `timing-r111-p1-old` do not match `installs` or
`timing-r111-p1`. A game process whose path cannot be read is read again
every 250 ms for about three seconds (`-PathRetries`, `-PathReader`): right
after its start a process often has no readable path yet (a neighbour clone's
DBSIM started in the same second refused `c3_10-loadout-hercs` in
`check-r6-set1` before the retry existed). A path that appears is judged like
any other; a process that exits meanwhile is ignored; one whose path stays
unreadable (access denied) blocks ("... whose path cannot be read ..."),
because it cannot be shown to lie outside the installations. A run with a
synthetic `-DataRoot` guards only that root's `installs`. `check.ps1`
recognises the messages (`outside the permitted`, `It is left alone`) as
"refused, rerun". `run-oracle-regression.ps1` checks the clone before the run
and before each suite.

**Own child.** After each suite (also when the suite failed)
`run-oracle-regression.ps1` reads the suite's fresh `<suite>-launch.json`
written by `start-timing-mission.ps1`. The report must name an executable in
this clone and a start time no older than the suite's start (never a
historical PID). The process counts as own only while PID, path and start time
(`Process.StartTime`, the API the launcher records) all match. The runner waits
up to `-ExitGraceSeconds` (default 30) for it to exit, then stops it and waits
15 s more; if it is still alive the run fails with "stays busy". A reused PID
(same number, other start time or path) is never touched. The outcome is
stored in `<suite>-entry.json` as `own_process_exit` (`status`: `exited`,
`stopped`, `pid-reused`, `no-launch-report` or `not-started`; `pid`;
`waited_ms`; the exit code, see the next section). Mock runs skip the process
steps but take the lock.

**Parallel failure path.** `run-oracle-parallel.ps1` never kills children or
games on an orchestration failure; it waits for running child runners (at most
15 minutes each) before releasing the clone locks, so no other session can
take a clone while one of its children still works there.

**Test.** `//tools/reference-game/setup:clone_guard_test` uses a synthetic
DataRoot, PowerShell lock holders and an own mock game binary
(`clone_guard_mock_game`) started under a synthetic clone with a fixture-only
process name, so the real runners' machine-wide DBSIM check never sees it. A
lane tests the machine-wide rule with that mock: a process outside the
installs and in the look-alike `installs-old` is ignored, one in another clone
is refused (also under `\\?\`, upper case, `..` paths) unless permitted, a
look-alike clone name is not permitted, a process in the target clone is
refused first, an unreadable path blocks only while the process exists, and no
mock game is ever stopped. It covers: refusal naming the holder (PowerShell 7
and 5.1), inheritance by token, release, a killed holder, a stale file, process
listing and refusal naming PID/path/start time, reused PID, historical or
foreign-clone launch reports, stop after the grace period, waiting without
stopping, and the refusals of `configure-tick-log.ps1`,
`start-timing-mission.ps1`, `run-oracle-regression.ps1` and
`run-oracle-parallel.ps1` (no output created). `oracle_speed_scripts_test` and
`suite_scripts_test` run the scripts in their mock modes (all three carry the
tag `scripts`; see [setup.md](setup.md) for `--test_tag_filters=-scripts` and
how the steps share PowerShell processes). Outside the guard: the transaction
in `SuiteLaunch.psm1` (its "before deployment" message stays generic, but the
regression runner's own check runs first) and the stop in
`run-tick-log-mission.ps1`, which does not wait (the regression runner's wait
and the configure wait cover it).

### Exit code of the own DBSIM

`own_process_exit` carries the process's exit code, and check mode compares it
with the golden's: the exit path of DBSIM under the own runtime must end with
the same status as the original's.

- **Where the code comes from.** `start-timing-mission.ps1` hands the process
  object of `Start-Process -PassThru` to `Register-CloneStartedProcess`
  (CloneGuard.psm1); its handle stays open for the PowerShell process of the
  run, because a process nobody holds a handle to leaves no exit code behind
  (most suites are already gone, stopped by `run-tick-log-mission.ps1`, when the
  wait begins). `Wait-CloneOwnedProcessExit` takes the registered handle, else
  one it opens at its first look while the process still runs, and reads
  `ExitCode` after the exit. `exit_code_source` names the handle
  (`launcher-handle`, `opened-handle`) or is `none`. Under
  `run-oracle-parallel.ps1` every child runner is its own PowerShell process,
  so the registration holds there too.
- **Fields.** `own_process_exit` (entry and golden manifest) has `exit_code`
  (integer or `null`), `exit_code_state`, `exit_code_source` and
  `stopped_by_runner`. `exit_code_state`: `exited` (the game ended by itself;
  `exit_code` is its code), `none` (a runner stopped the process: this
  function after the grace period, or `run-tick-log-mission.ps1` once the ticks
  were logged, `stopped_after_identity_check` in the launch report; `exit_code`
  is `null`, since the code of a terminated process is the runner's, not the
  game's), `unknown` (the game ended by itself but no handle gave the code, or
  the PID was reused). Which suites exit by themselves is a property of the
  suite and of the original: `none` against `none` is equal; an outcome suite
  that ends by itself records its code.
- **summary.json.** Each result item of a run that started a process carries
  `exit_code` and `exit_code_state`, and in check mode `exit_code_check`:
  `same`, `different`, or `no golden exit code` (the golden manifest has no
  `own_process_exit.exit_code_state`; reported, never a difference).
  `<suite>-entry.json` adds `golden_exit_code_state` and `golden_exit_code`
  when the golden has the field. The parallel runner copies the three keys
  into its merged summary; `check.ps1` lists them per suite
  (`exit code <suite>: <state> <code> (<check>)`) and counts them in the live
  stage's numbers (`exit code same N, no golden exit code M[, DIFFERENT K]`).
  Mock runs (no process) carry none of the keys unless the fixture
  `own-exit.json` describes one.
- **Rule.** In check mode a state or code that differs from the golden's makes
  the suite `DIFFERENT` (exit status 1), whatever the tick comparison says; an
  `unknown` on either side is `different` too (nothing to confirm). `none`
  against `exited` is `different` in both directions: a game that used to be
  stopped by the runner and now exits by itself (or the reverse) changed
  behaviour. Record mode stores the record in the new golden's manifest.
- **Backfilled goldens.** For goldens recorded before the field existed,
  `exit_code` and `exit_code_state` come from ONE identical all-original check
  run (every hook original, the golden's own behaviour); `exit_code_backfill`
  names that run's summary, commit and time. 36 manifests are backfilled from
  `check-t42-original` (five clones, 36 of 36 identical); a second all-original run, `check-t42-original-2`, reported
  `exit code same` in 36 of 36.
- **State of the goldens.** All 40 manifests carry the record. 14 suites end
  by themselves: exit code **3** in `c1_01-success`, `c3_09-loadout-b`,
  `c4_08-razor-plasma`, `c3_10-loadout-hercs`, `c1_02-probe`,
  `c1_01-cheat-destroy`, `c5_05-moon-missiles`, `c1_01-cheat-disable-leg`,
  `c4_08-slope-dive`, `c1_08-scan`, `c1_06-cheat-disable`, `c4_08-joystick`,
  `c1_01-cheat-disable-late`, and **0** in `c1_01-quit-dialog`; the other 26
  are stopped by the runner (`none`).
- **Tests.** `//tools/reference-game/setup:clone_guard_test` (a mock game that
  returns a chosen code: opened handle, launcher handle for a process already
  gone, none for an unregistered one, none for a stopped process),
  `suite_scripts_test` lane `lane_exit_codes` (record, check, every verdict, the
  runner-stopped case), `check_script_test`.

### Empty hook mismatch logs

A hook in `compare` mode makes the proxy open `<suite>.jsonl.hooks.jsonl`
(`CREATE_NEW`, hooks.md) when the game starts, so a run without a single
mismatch leaves a zero-byte file. `configure-tick-log.ps1` writes
`mismatch_log` only when a hook is configured to compare (a mixed `replaced` and
`compare` selection counts; a pure `replaced` or `original` selection never
creates the file), so the file cannot be avoided from the script side without
changing the proxy; it is removed instead, after the game has exited:
`run-oracle-regression.ps1` removes a zero-byte `<suite>.jsonl.hooks.jsonl`
(`empty_hook_log_removed` in the entry), `check.ps1` removes any that remain
under `live/` (a detail line of the live stage), and `tools/evidence_budget.py`
refuses a staged empty `*.hooks.jsonl` anywhere. A mismatch log with content is
kept as evidence; the absence of the file means no mismatch was recorded (the
`hk_*_mismatches` counters say the same).

## Acceptance script

`tools/check.ps1` runs the per-batch acceptance protocol in one command and
writes one summary. Its header lists every option; stages, in this order:

| Stage | What | Switch |
| --- | --- | --- |
| quick | `bazel test //...` | `-SkipQuick` |
| full | `bazel test //tools/reference-game/invitro:differential_full //tools:scripts` | `-Full` (off by default) |
| signatures | `invitro/check_signatures.py` with `ES2_DATA_ROOT` | `-SkipSignatures` |
| audit | `scripts/compare_audit.py --check compare_mode.md --hooks-doc hooks.md` | `-SkipAudit` |
| live | prepared proxy, then `run-oracle-parallel.ps1` on the five clones `timing-r111`, `-p1`, `-p2`, `-p4`, `-p5` (`-InstallName`: one to six, [below](#more-clones-and-longest-first-dispatch)) | `-Live` (off by default) |
| own-shell | one unattended mission through the own shell (`start-own-menu.ps1 -Unattended`), inside the live stage | `-OwnShell` (needs `-Live`; `-OwnShellShell text\|gui\|both`) |
| own-sim | every suite through `es2sim` ([the own-process replay](#the-own-process-replay)) with the frame compare, then the unattended chain es2shell → es2launch → es2sim | `-OwnSim` (needs `-Live`) |
| evidence | `tools/evidence_budget.py`, read-only report of the es2-data repository | `-SkipEvidence` |

```powershell
& tools\check.ps1 -Full -Live `
    -BazelOutputBase build\bazel\check -BazelContentsCache build\bazel\check-contents
& tools\check.ps1 -SkipQuick -SkipSignatures -SkipAudit -Live -HookModes modes.txt -RtlConfig rtl.ini
& tools\check.ps1 -Live -SkipQuick -OwnShell -OwnSim -OwnCdRoot <copy of the CD without the executables>
```

**Live stage.** Every name of `proxy/hook-names.txt` gets `=replaced`;
`-HookModes <file>` overrides single names with `name=mode` lines (`original`,
`replaced`, `compare`; `#` comments; an unknown name refuses). `-RtlConfig
<ini>` sets `ES2_RTL_CONFIG` for the run. The proxy comes from
`New-Es2PreparedProxy` (`-PreparedProxy <manifest>` reuses one). The live stage
is refused up front, before any stage runs, when `git status` shows modified or
untracked files under the source-stamp paths (`core/`, `hook/`, `bazel/defs/`,
`bazel/toolchains/`, `tools/reference-game/{proxy,setup,scripts}`,
`invitro/branch_sites.h`, `MODULE.bazel`, `MODULE.bazel.lock`, `.bazelrc`, and
`rtl/` with `-Runtime own`; the child runners would refuse a stale proxy
anyway), and it does not start when an earlier stage failed (`NOT RUN`). The
stages run one after another by default. The stand-in `dbsim.EXE` of
`//shell:chain_real_test` (quick tier) runs outside `installs`, so a
`bazel test //...` of any session at the same time does not make the clone
guard refuse suites.

**Suite selection.** `-Suite <name[,name...]>` (needs `-Live`) hands the
runner its `-Suite`: only these suites of `oracle-suites.json` run, and the
hook counters, the verdict (`suites identical n of n`), the deletion of the
raw tick logs and the own-sim replays (`-OwnSim`) cover only them. A name the
registry does not have, a name given twice or an empty list is refused (exit
2, before anything is created; the refusal lists the registered names).
Without `-Suite` every suite of the registry runs. The summary names a
selection (`- suites:` header line, live details, `suites` in
`summary.json`, `all` without one).

```powershell
& tools\check.ps1 -SkipQuick -SkipSignatures -SkipAudit -Live -Suite acceptance-60s,c1_01-success
```

**`-Overlap`** (needs `-Live`, refused with `-SkipQuick`): the live stage and the
quick tier run at the same time. The live stage goes first; once it has its
prepared proxy (immediately with `-PreparedProxy`) the script starts
`bazel test //...` and keeps going with the oracle runner; it then waits for
the quick tier and runs `full`, `signatures` and `audit` one after another.
The proxy build comes first because both Bazel commands use the same output
base and the second waits for the server lock, so only the oracle run itself
overlaps with the tests. Reporting is as in the sequential run (same stages in
the same order, same numbers; the quick duration is the wall time from its
start). The live result counts only if no other stage failed: when `quick`,
`full`, `signatures` or `audit` FAILED, a live stage that PASSED is labelled
`DISCARDED` (numbers and details stay in the summary and the evidence, it is
neither passed nor failed) and the exit status is 1 through the failed stage; a
live stage that failed itself stays `FAILED`.

**Runtime.** `-Runtime own` (the default) runs every regression on the own
`cw3220.dll` ([rtl_imports.md](rtl_imports.md#the-own-runtime-as-the-default)):
before any stage, `setup/configure-runtime.ps1`'s preview must accept every
clone of the run (separate measurement clones only, the expected runtime
files); right before the runner the live stage builds `//rtl:cw3220.dll`
(or takes `-RuntimeSource <dll>`) and deploys it on each clone with
`-Apply`, and a `finally` restores the original `CW3220.DLL` on every clone it
touched (`-Remove`), also when a deployment, the runner or the stage fails;
a failed restore fails the stage and names the clone. A clone found with the
own runtime (an interrupted run) is redeployed and restored. `-Runtime
original` deploys nothing and refuses a clone that still has the own runtime;
the shim modes of `-RtlConfig` that work on CW3220's internals need it. The
summary names the runtime (header line, live details, `runtime` in
`summary.json`). `-Bwcc32 refuse` (the default) sets `ES2_BWCC32=refuse` for
the runner: the proxy answers DBSIM's `LoadLibraryA("BWCC32.DLL")` (`0x465140`)
with failure ([dll_imports.md](dll_imports.md#the-bwcc32-switch));
`-Bwcc32 load` clears the variable for the run, so DBSIM loads the DLL. The
summary names the mode either way.

**`-NewFail <site>:<n>|any:<n>`** makes DBSIM's `new` (`0x479cfc`) answer NULL
at the n-th counted request (`proxy/new_fail_switch.h`, `ES2_NEW_FAIL`, one
NDJSON log per suite under the evidence root). The suites are then expected to
end in DBSIM's fault: the live result is a record, not a regression verdict.
Refused together with `-OwnShell` and `-OwnSim`; a warning when the site lies
in a function whose hook is replaced in the run.

**After the runner:** `hook-counters.json` (`tools/check_counters.py`: per suite
the `hk_*` fields of the tick record number `common_ticks` of
`<suite>-comparison.json`, of the last tick record and of the `exit_counters`
record, null when the log has none), every nonzero `*_skipped`/`*_bypassed`
counter (and `*_mismatches`) in the summary (also the `rtl_*` counters of the
CW3220 shim, which are not kept in the file; `rtl_refused` and a failed own-heap
walk make the stage fail), and, only if every suite is identical and the
counters were extracted, deletion of the raw `<suite>/run/<suite>.jsonl` tick
logs (small-file evidence policy). A run with a differing suite keeps
everything and prints the first lines of each such suite's `runner.log`. A
suite whose `runner.log` shows the clone guard refusing (`Close the game in
this clone`, `in use by another session`, `outside the permitted`, `It is left
alone`; for example because a session runs a game in one of the clones) is
labelled `refused, rerun`, not `DIFFERENT`.

**Own shell and own sim.** `-OwnShell` runs, after the suites and with the
same proxy, hooks and runtime, one unattended mission through the own shell on
one clone (`-OwnShellInstall`, default the first of `-InstallName`): PASSED when
DBSIM's tick log and outputs are identical to the `c1_01-success` golden and
the chain verification passes. `-OwnSim` builds `//sim:es2sim_window` and
`//sim:sdl_runtime` from the clean tree, runs every selected suite through
`sim/tests/replay_suite.py --frames` (`-OwnSimJobs` at a time) and, for each
IDENTICAL suite, `sim/tests/frames_suite.py --capture` against the frame
references (`-OwnSimFramesReference`), then the unattended chain on one clone
(`-OwnSimInstall`). `-OwnCdRoot <dir>` hands both chains the CD root the own
executables read (`menu.ini` `cd_root`), for example a copy without
`VER95\DBSIM.EXE` and `VER95\VSHELL.EXE`; the oracle side keeps `-DataRoot`.
The script header has the full verdict rules.

**Evidence.** `-EvidenceRoot` (default `<es2-data>\logs\check-<utc>`)
must not exist. It receives `summary.md` (stage, result, duration, key numbers:
tests passed/failed, signatures, audit problems, per-suite live result,
nonzero counters) and the same as `summary.json`, one raw log per stage
(`quick.log`, `full.log`, `signatures.log`, `audit.log`, `live.log`,
`counters.log`, …), `live/` (the runner's output), `hooks-argument.txt`, copies
of the `-HookModes` and `-RtlConfig` files, and the sessions of the own-shell
and own-sim stages. Exit status: 0 only if every executed stage passed; 1 a
failed stage; 2 a refused call (existing evidence directory, dirty stamp paths
with `-Live`, bad parameters, a clone the runtime preview refuses, no stage
selected).

**Current state.** `check.ps1 -Live -SkipQuick -OwnShell -OwnSim -OwnCdRoot`
on a scratch copy of the CD without `DBSIM.EXE` and `VSHELL.EXE`: live 40/40
identical on the own runtime, own-shell text and gui PASSED, own-sim 40
replays IDENTICAL, frames 40/40, chain PASSED (`es2-data\logs\check-r13-1`,
[own_process.md](own_process.md)).

**Test.** `//tools:check_script_test` (`tools/test_check.py`) runs the script with
fake `bazel`/`python` commands, a fake `Es2Bazel` module and a mock live runner
that copies a synthetic run directory in the shape of a real one: stage
selection, exit codes, summary content, counter extraction, jsonl deletion only
when everything is identical, `refused, rerun`, the dirty-tree refusal, `-Overlap`
(the fake runner records that the fake Bazel was called while it ran; stage
order, the `DISCARDED` verdict, refusals), the runtime (a mock
`configure-runtime.ps1` with per-clone state files: preview, deployment before
the runner, restore after it, after a failing or throwing runner and a refused
deployment, a failed restore, `-Runtime original`, `-RuntimeSource`, the `rtl/`
stamp, `-Bwcc32`), `-Suite` (pass-through to the runner and the own-sim
replays, refusals), `-NewFail` (pass-through, restore, the warning for a site
in a replaced function, from a synthetic `hook_registry.h`) and
`check_counters.py` on its own. It starts no Bazel, game or clone.

## More clones and longest-first dispatch

**Up to six clones.** `timing-r111` is the source; `-p1` ... `-p5` are its
copies, so the machine can run at most six game processes of the oracle at
once. `run-oracle-parallel.ps1` and `check.ps1 -Live` take `-InstallName` with
one to six distinct names (more, repeated names and `native-r111` are refused
before anything is created; `pwsh -File` hands a comma-separated list as one
argument and `check.ps1` splits it). `check.ps1` defaults to five clones:
`timing-r111`, `-p1`, `-p2`, `-p4`, `-p5` (`-p3` is the demo-measurement probe
clone and stays out). `run-oracle-parallel.ps1` on its own defaults to `-p1`,
`-p2`. Other sets are given explicitly:

```powershell
& tools\check.ps1 -SkipQuick -SkipSignatures -SkipAudit -Live `
    -InstallName timing-r111,timing-r111-p1,timing-r111-p2,timing-r111-p4,timing-r111-p5
```

**Creating a clone.** `new-measurement-clone.ps1 -Number 1..5 [-Apply]`
copies `installs\timing-r111` to `installs\timing-r111-p<N>`. Without
`-Apply` it prints the plan (source, destination, file count). It refuses a
number outside 1 to 5, a source other than `timing-r111` (so never
`native-r111` and never a copy of a copy), a link or junction anywhere in the
source, an existing destination (nothing is overwritten or cleaned up), and a
running game in the source. Each file is hashed before and after its copy
(SHA-256, source read again as well); at the end the destination tree is
enumerated once more (exactly the copied files, every hash read again) before
the clone's `installation-manifest.json` is rewritten
(`destination`, `measurement_clone_of`) and `measurement-clone-copy.json`
records every hash. About 80 to 180 MB per clone. A clone that a probe run
changed (as a demo playback does, see
[rng.md](../reverse-engineering/rng.md#demo-path))
is not a clean copy: take the next free number or recreate it after deleting
it by hand.

```powershell
& .\tools\reference-game\setup\new-measurement-clone.ps1 -Number 4          # dry run
& .\tools\reference-game\setup\new-measurement-clone.ps1 -Number 4 -Apply   # copy + verify
```

**Dispatch order.** The runner starts the suites longest first, so the long
suites do not end up alone at the tail of the pass: a free clone always takes
the next suite of the sorted list. The length is the golden's tick count,
read from `<GoldenRoot>\<suite>\manifest.json` (`launch.ticks_seen`, capped by
the registry `frame_limit`, the window the golden was normalized with); a
golden without it falls back to the `tick_count` at the start of
`normalized.json` (first 8 KB), then to the registry `frame_limit`, then to 0
(unknown suites come last). Ties keep the registry order, so equal inputs give
one order. Only the **start order** changes: `summary.json`, the results and
the counters keep the registry order. `-DispatchOrder registry` starts in
registry order. `-DryRun` prints `dispatch`, `dispatch_order` and
`dispatch_basis` (ticks and source per suite); each pass writes
`dispatch.json` (planned and actual start order as `suite@clone`) into its
output directory. With the current goldens the order begins
`c3_09-loadout-b` 8886, `c5_05-moon-missiles` 8262, `c1_01-success` 6198,
`c3_09-orders-matrix` 4200, `c3_09-wingman-orders` 3300, `c1_08-scan` 2600,
`c3_10-hunt-targ` 2400, `c1_01-collision` 2200, `c4_08-razor-plasma` 2094.
The longest suite bounds the stage: on five clones the pass took 201 s from
first launch to last comparison (`check-r6-set1`), and more clones do not
shorten it.

**Tests.** `//tools/reference-game/proxy:oracle_speed_scripts_test` (mock
children: ordering from manifest, `normalized.json`, `frame_limit` and an
unreadable golden, ties, six/two/one clone, `-DispatchOrder registry`, a
selection, the three refusals; clone number range, source and existing-target
refusals) and `//tools:check_script_test` (`-InstallName` pass-through and
refusals). No clone or game is started.

## Bisecting a failed regression

`tools/reference-game/scripts/bisect_hooks.py` turns a failed hook regression
into a sequence of smaller regressions that name the first offending hook. It
is a state machine the coordinator drives one step at a time; it never builds,
deploys or starts anything itself.

```powershell
$py = '<es2-dependencies>\python\python.exe'
$bisect = 'tools\reference-game\scripts\bisect_hooks.py'
& $py -B $bisect plan --regression <es2-data>\logs\<failed run> --state <es2-data>\logs\<failed run>-bisect.json
# Each step prints two commands: the regression to run and the next call.
& 'tools\reference-game\setup\run-oracle-parallel.ps1' -Suite '<failing suites>' -Hooks '<step selection>' -OutputDirectory '<step dir>'
& $py -B $bisect next --state <state> --result '<step dir>'
& $py -B $bisect status --state <state>          # history, pending step or result; --json for the raw state
```

**Input.** The output directory of `run-oracle-parallel.ps1`
(`<suite>/run/…`) or `run-oracle-regression.ps1` (flat): `summary.json`
(commit, `hooks`, per-suite result), per suite `<suite>-comparison.json`,
`<suite>-entry.json` and the tick log `<suite>.jsonl`. The hook order and
the adapter groups come from `proxy/hook_registry.h` (last macro argument),
checked against `proxy/hook-names.txt`. A suite is `different` when its
comparison report differs, `error` when the summary says DIFFERENT without a
differing report (crash, stall, runner or normalization failure, or an entry
whose hooks differ from the summary).

**Selections.** Only the hooks of the failed selection that were not
`original` take part. Every step selection names each of those hooks with an
explicit mode: its failed-run mode (`replaced` or `compare`) or `original`.
A hook's mode is never implied by its caller's mode, because nested hooks
keep their own modes. Each step runs only the suites that failed.

**Kept hooks.** A hook whose `hk_<name>_calls`, `_bypassed` and `_skipped`
are all 0 in the last tick record of every failing suite was never entered
in the failed run (typically a nested hook whose caller's replacement calls
the core directly), so it cannot have caused that difference. These hooks
stay at their failed-run mode in every step and are not searched (`--no-keep`
searches them too). Setting a caller to `original` can make such a hook
entered: every step records the kept hooks with nonzero calls as warnings,
and if the kept hooks alone (all searched hooks original) reproduce the
failure, the search continues among the kept hooks. Without a tick log for a
failing suite no hook is kept.

**Search.** A prefix search: hooks are ordered by adapter group (groups in
order of first registry appearance), then by registry order. A step replaces
the kept hooks plus a prefix of that order; the search looks for the shortest
failing prefix, first over whole groups, then over the hooks of the offending
group with the earlier groups replaced. Its last hook is the *first offending
hook*. About log2(groups) + log2(hooks in the group) steps, plus one when
kept hooks exist and the first group is reached (the kept hooks alone are
then checked). If earlier hooks were replaced when the hook was found, a
final `confirm` step runs it alone (with the kept hooks): failing means the
hook fails on its own, passing means it fails only together with earlier
hooks, which the result lists (`--no-confirm` skips the step). The result
also gives the group, the VA and the hook's counts in the failed run.

**Step results.** `next` refuses (exit 2, state unchanged) a directory
without a check-mode summary, with another hook selection, without one of
the step's suites, with another commit or dirty flag than the failed run, or
one already used. A step fails if any suite is `different`; a step with
errors but no difference is inconclusive (exit 3, state unchanged): run the
step again into a fresh directory, or pass `--errors-fail` when the error is
the failure being bisected. If the failed run itself had error suites,
errors count as failures from the start.

**Assumptions.** The suites are deterministic (identical goldens on repeated
runs) and replacing more hooks never hides a difference that fewer hooks
show. With several independent offenders the search names the first one in
the order above; set it `original` (or fix it) and run the regression again
for the next. Step directories default to `<failed run>/../bisect-<failed
run name>-stepNN` (`--step-root`, `--name`); `run-oracle-regression.ps1`
requires them under `DataRoot\logs`.

**Test.** `//tools/reference-game/scripts:bisect_hooks_unit_test` drives the
state machine with synthetic registries, regression directories (both
layouts, BOM summaries, tick logs larger than the 1 MiB read chunk) and a
synthetic oracle: every single culprit, an interaction of two hooks, a kept
hook that fails only under an original caller, inconclusive and refused
steps, the command line and the repository registry.

## Headless window

In replay mode the proxy can keep DBSIM's window invisible and leave the
desktop alone: no window appears, nothing is activated, moved or resized,
and the mouse cursor is never moved or clipped. Code:
[window_shim.h](../../tools/reference-game/proxy/window_shim.h) (included by
`media_proxy.c` after `input_journal.h`). Record mode, mode `off` and
`-Interactive` runs never change.

**Mechanism.** DllMain chains DBSIM's `CreateWindowExA` IAT slot behind the
input journal's wrapper (the image and all eight `jmp dword [iat]` thunks
are checked first; refused otherwise, see the `mismatch`/`order` fixtures).
At the first top-level `CreateWindowExA` (WinMain → `0x465158`, before the
window exists) the shim reads the configuration after the journal's: only
when `[input] mode=replay` and `[window] hidden=1` it removes `WS_VISIBLE`
from the style (`0x100e0000` → `0x000e0000`) and replaces the IAT slots of
`ShowWindow`, `UpdateWindow`, `SetWindowPos`, `MoveWindow`, `SetCursorPos`,
`ClipCursor` and `ShowCursor` (slot RVAs `0x2cc478`, `0x2cc470`,
`0x2cc484`, `0x2cc410`, `0x2cc490`, `0x2cc45c`, `0x2cc47c`, read from the
import table of the reference `DBSIM.EXE`) with functions that never call
USER32. The video log (`[video] log`, `logs\video-compat.jsonl`) gets
`window_hidden` once and `window_hidden_<import>` at the first call of
each import. No tick field changes.

**Call sites and results** (llvm-objdump listing of the reference
`DBSIM.EXE`, confirmed by a raw scan of the code section for `call rel32`
to each thunk and for direct references to each IAT slot: no other site, no
indirect use):

| Import | Sites (function) | What the caller does with the result | Shim returns | Compared state reachable? |
|---|---|---|---|---|
| `ShowWindow` | `0x4652cf` (window creation `0x465158`, `nCmdShow` of WinMain) | discarded: `UpdateWindow` follows, then `mov eax, 1` (`0x4652e0`) | `FALSE` (the window was never visible) | no |
| `UpdateWindow` | `0x4652db` (same) | discarded (`0x4652e0`) | `TRUE` | no |
| `SetWindowPos` | `0x4652ac` (creation, DirectDraw only), `0x464bff` (centre helper `0x464b2c`, returned to its only caller `0x466436`, which ignores it), `0x466400`/`0x46641d` (private message `0x812`, posted after `WM_DISPLAYCHANGE`), `0x4668c2`/`0x466989` (window/full-screen toggle `0x4667c8`, not reached in the oracle) | discarded at every site | `TRUE` | only through the client size, see `WM_SIZE` below |
| `MoveWindow` | `0x4718a7` (`0x471868` ← `0x4719b4` in the AVI player `0x4718f8`, reached only after a successful `MCI_OPEN`) | returned by `0x471868`, unused | `TRUE` | no |
| `SetCursorPos` | `0x48093e` (warp helper `0x4808dc`: `GetClientRect`, scale, `ClientToScreen`; callers `0x43c98d`, `0x454a57`, `0x454cf6` ignore EAX; live in c1s/c3_09/c4_08/c3_10), `0x466923` (toggle) | discarded | `TRUE` | no: a replayed `GetCursorPos` answers from the journal or the journal's virtual cursor, never from the real cursor |
| `ClipCursor` | `0x4667bf` (teardown `0x466730`, at every exit, whole screen), `0x46690e` (toggle) | discarded | `TRUE` | no |
| `ShowCursor` | `0x466933` (`FALSE`), `0x466999` (`TRUE`), toggle only and only with a cursor sprite | discarded (`jmp` resp. `mov al, [0x4d255e]` follows); no loop on the counter | the display counter: starts at 0, +1 per `TRUE`, −1 per `FALSE` | no |

The oracle runs make no `MoveWindow` call (0 `window_hidden_MoveWindow`
events; [own_process.md](own_process.md)).

**What a hidden window changes, and why compared state cannot see it.**
The window procedure `0x466034` first calls its handler list `0x4d3b30`
with every message, then its own cases ([flyer.md](../reverse-engineering/flyer.md#mainwndproc-0x466034)).
The handlers react only to: keyboard `0x477bec` `WM_KEYDOWN`/`UP`,
`WM_SYSKEYDOWN`/`UP`; mouse `0x4809f8` `WM_SIZE` and `WM_MOUSEMOVE`,
`WM_LBUTTON*`, `WM_RBUTTON*`; sound `0x462398` `WM_TIMER` and
`MM_MCINOTIFY`; the AVI handler only inside the AVI player. Per message
kind:

- **Activation and focus** (`WM_ACTIVATE`, `WM_ACTIVATEAPP`,
  `WM_NCACTIVATE`): no case and no handler; `DefWindowProcA`. The focus
  pair `WM_SETFOCUS`/`WM_KILLFOCUS` would toggle the compared pause flag
  `0x4d2572` through `0x45f108`/`0x45f0d4`, but the replay subclass of
  [input_journal.h](../../tools/reference-game/proxy/input_journal.h)
  keeps both (and power suspend/resume) from the game in every replay, so
  the game behaves as if it always had the focus, hidden or not. The only
  focus messages that reach the game unfiltered are those sent *during*
  `CreateWindowExA` (before the subclass exists): with `WS_VISIBLE` a
  visible window may be activated there and get `WM_SETFOCUS` →
  `0x45f108`, which then writes 0 to the pause byte (already 0), returns at
  once from `0x463238` (`[0x49f8ec]` = 0 until the sound init `0x462410`
  in the mission load `0x46179f`) and skips the cockpit part
  (`[0x49b088]` = 0 until `0x4311a2`). The hidden window gets no such
  message; the state is the same. Parallel runs replay identically although
  at most one window can have the focus.
- **`WM_SIZE`**: the mouse handler recomputes the mouse scale
  `[0x4a2dcc]`/`[0x4a2dd0]` = (DIB size `[0x4d3040]`/`[0x4d3044]` << 15) /
  client size from `GetClientRect` (`0x480898`); without a `WM_SIZE` the
  same is computed lazily at the first mouse message (`0x480948`). The DIB
  size is written by `0x464cc4` before `CreateWindowExA`, and the handler
  is registered (`0x480694`) only during the game init after the window
  exists, so both orders give the same values as long as the client size
  is the creation size. A hidden window keeps exactly that size (the
  `SetWindowPos`/`MoveWindow` shims never resize it). A visible window can
  lose it only by the `0x812` path after a `WM_DISPLAYCHANGE`, by the
  toggle, or by the user dragging its frame — the last one is a source of
  nondeterminism the hidden window removes.
- **`WM_PAINT`**: the GDI present `0x465628` validates the window itself
  (`InvalidateRect` + `BeginPaint`/`EndPaint`), and `BeginPaint` and
  `StretchBlt` on a hidden window simply draw nothing (no result is read,
  [presentation.md](../reverse-engineering/presentation.md#gdi-branch-the-oracle-path)).
  A visible window still receives `WM_PAINT` when it is uncovered or
  shown (then `0x464a14` presents again); a hidden one never does
  (hypothesis from the Windows paint rules for invisible windows; the
  identical hidden runs are consistent with it). Presents read no clock and
  write no compared state; the palette cycler's clock read (`0x4306b6`) is
  part of the main-loop presentation frame `0x45fba4`, not of the present,
  and stays in place.
- **`WM_QUERYNEWPALETTE`** (sent on activation) installs the GDI palette
  through `0x465e28`: presentation state only. A hidden window is never
  activated, so it never gets this message; `WM_PALETTECHANGED`
  (broadcast) and `WM_DISPLAYCHANGE` reach hidden windows as well.
- **Non-client mouse messages** (`WM_NCMOUSEMOVE` …, `0x00a0..`) are not
  input messages for the journal, so in a visible run every mouse movement
  over the window frame is retrieved by the pump. A hidden window gets
  none.

**Pump-keyed answers (`"p"`).** The replay counts `PM_REMOVE` calls of
`PeekMessageA` per frame; real input messages are dropped inside one call
and do not count, every other retrieved message adds one call. A hidden
window removes `WM_PAINT` and non-client traffic from that count, leaving
the asynchronous SOS (`0xa000`) and MCI messages that already make `"p"`
inexact ([timing.md](../reverse-engineering/timing.md#other-time-sources)).
Fewer messages per modal pass means at least as many passes before the
answer, and the modal loops reach the intra-frame clock cap (step − 1 per
limiter interval) either way, so the clock — and with it every compared
field — is unchanged. The suites that use `"p"` (c1s, c1_01-cheat-destroy,
c1_01-mouse, c1_02-probe, c3_09-loadout-b, c3_10-loadout-hercs,
c4_08-razor-plasma, c5_05-moon-missiles) are the ones where a difference
would show first. `message_trace.py compare A B --tick-log-a … --tick-log-b …`
checks two message traces (`ES2_MESSAGE_TRACE=1`) of one journal against this
account: every pump difference must come from the unkeyed notices, and every
answered frame must reach the clock cap
([main_loop.md](../reverse-engineering/main_loop.md#the-pump-accounting)).

**Other windows.** The oracle starts `DBSIM.EXE` directly
(`start-timing-mission.ps1`, `-WindowStyle Hidden`), so VSHELL plays no
part. The volume helper runs without a window (`-NoNewWindow`), the
parallel children with `CreateNoWindow`. `MessageBoxA` is not shimmed: a
fatal DBSIM error (for example "Bad WinGBitmap") still shows its box. In
GDI mode the pump's `SetCursor` changes only the cursor shape of DBSIM's
own thread; it moves nothing.

**Switches.** `configure-tick-log.ps1 -HiddenWindow` writes
`[window]`/`hidden=1` (refused unless `-InputMode replay`; without the
switch the INI keeps its bytes) and records `hidden_window` and
`window_shim_header_sha256` in `oracle-media-manifest.json`.
`run-tick-log-mission.ps1 -HiddenWindow` (default from
`ES2_ORACLE_HIDDEN_WINDOW` = `1`) applies it only to replay runs without
`-Interactive`, sets the volume default to 0 (`-VolumePercent` or
`ES2_ORACLE_VOLUME_PERCENT` still win) and adds `hidden_window`,
`window_visible_seen` (whether `Process.MainWindowHandle`, which names only
a visible top-level window, was ever nonzero while polling; it sends no
message to the game) and `volume_percent` to its JSON line.
`run-oracle-regression.ps1 -HiddenWindow` decides for its suites (given
value, else `ES2_ORACLE_HIDDEN_WINDOW` `1`/`0`, else the default) and hands
the decision to the mission runners in `ES2_ORACLE_HIDDEN_WINDOW`;
`hidden_window` then appears in the dry plan, each `<suite>-entry.json` and
`summary.json` (only when hidden). `run-oracle-parallel.ps1` and `check.ps1`
take `-HiddenWindow` (or `-HiddenWindow:$false`) and pass
it down as `ES2_ORACLE_HIDDEN_WINDOW`, restoring the variable afterwards;
not given, nothing is set. From Windows PowerShell 5.1 `-File` cannot bind
`-HiddenWindow:$false`; set `ES2_ORACLE_HIDDEN_WINDOW=0` instead.

**Default.** `$hiddenWindowDefault = $true` in
[run-oracle-regression.ps1](../../tools/reference-game/setup/run-oracle-regression.ps1):
every check-mode run (direct, parallel, `check.ps1 -Live`,
`run-live-coverage.ps1`) hides the window and plays at volume 0 unless
`-HiddenWindow:$false` (or `ES2_ORACLE_HIDDEN_WINDOW=0`) is given; record mode
(goldens, the owner's recordings) keeps the visible window unless
`-HiddenWindow` is given.

**Evidence.** `check-r6-set1` (453 hooks and the 41 CW3220
imports replaced, heap shadow, five clones; `c3_10-loadout-hercs` in the rerun
`check-r6-set1-c3_10` after the clone guard had refused it): the sixteen
suites registered then are identical to their goldens with the window hidden;
every `<suite>-entry.json` has `hidden_window: true`, `window_visible_seen:
false`, `volume_percent: 0`, and `video-compat.jsonl` has `window_hidden`
once per suite run; every skipped/bypassed/mismatch counter is 0. A live check
with the window hidden is `check.ps1 -SkipQuick -SkipSignatures -SkipAudit
-Live -HiddenWindow`; besides the keys above, no game window appears and the
cursor is not moved or clipped.
What to look for in a hidden run that differs: a difference confined to a
`"p"` suite points at the pump count, one in a mouse suite at the client size.

**Tests.** `//tools/reference-game/proxy:window_shim_{hidden,visible,zero,record,off,mismatch,order}_test`
(synthetic image, fake USER32 functions that count calls): shims installed
only for replay with `hidden=1`, documented return values, `WS_VISIBLE`
removed from top-level windows only, no real USER32 call in that mode, the
replayed cursor unaffected by a warp, no slot touched otherwise, refusal on
a changed thunk or without the input journal. Script tests:
`oracle_speed_scripts_test` (the real `configure-tick-log.ps1 -Apply` on a
synthetic clone, the tick-log runner's decision/volume/keys, the parallel
runner's variable and summary), `suite_scripts_test` (resolution order,
evidence keys, restored variable, dry plans), `check_script_test`.

## The own-process replay

The goldens were recorded from the original game under the proxy. The own
DBSIM ([own_process.md](own_process.md), `es2sim.exe --headless`) is accepted
against the same goldens, without a game, a clone or a window: one registered
suite goes through the host and its tick log through the same comparison
`check.ps1 -Live` uses. All 40 suites replay IDENTICAL
(`es2-data\logs\check-r13-1`; `fulltest-r12-3-20261022.log`). The per-suite
tests are `manual`; `check.ps1 -OwnSim` runs them all.

### Running it

```powershell
$base = "build\bazel\<name>"      # your output base
# one suite (the target is //sim:replay_<suite>_test, one per registry entry):
bazel --output_base=$base test --repo_contents_cache="$base-contents" //sim:replay_c1_01-success_test --test_output=all
# every registered suite (a manual test_suite; every suite prints its result line):
bazel --output_base=$base test --repo_contents_cache="$base-contents" //sim:replay_all --test_output=all
# without Bazel, with one summary line at the end (needs a built es2sim.exe):
python -B sim\tests\replay_suite.py --sim <bazel-bin>\sim\es2sim.exe --all --output-dir <dir>
python -B sim\tests\replay_suite.py --sim <bazel-bin>\sim\es2sim.exe --suite c1_01-success --keep --output-dir <dir>
```

- The tests need `ES2_DATA_ROOT` (`.bazelrc` sets it: the extracted CD, or a
  directory with `extracted\cd` below it) and the goldens and launch sets at
  `<es2-data>\oracle` (`ES2_DATA_REPO`, which `.bazelrc` sets to
  `<es2-data>`; without it two levels above `extracted\cd`;
  `ES2_ORACLE_DIR` overrides). Without them, or without a golden or launch set
  of the suite, the result is `SKIP` and the exit status 0, like every
  real-data test.
- `ES2_REPLAY_TIMEOUT` (seconds per suite, default 3300; the Bazel size is
  `enormous`, 3600 s) and `ES2_REPLAY_PREFS` (a `PREFS.CFG` for `DATA\`, see
  [the working directory](#working-directory-the-files-the-own-process-gets)).
  `--keep` leaves the prepared directory beside the results.
- Further options of `replay_suite.py` (its docstring has the details):
  `--frames` (es2sim's frame capture, compared with the frame references; not
  part of the verdict), `--dump-frames`/`--dump-dir`, `--image-access`,
  `--after-load-snapshot`, `--window` (the windowed build, on SDL's dummy
  drivers unless `--window-devices`), `--message-trace`.
- Per suite `result.json` (the plan, the host's exit code, the timing, the full
  comparison report), `tick-log.jsonl`, `es2-replay.ini`, the host's stdout and
  stderr and `outputs\` go to `--output-dir`; under Bazel to
  `TEST_UNDECLARED_OUTPUTS_DIR` (`test.outputs` of the test's log directory). The
  working directory is created under `TEST_TMPDIR` and deleted; nothing is written
  below the CD, into the repository or into es2-data.
- The tests carry `manual`, `local`, `no-cache`, `exclusive`: `bazel test //...`
  skips them. The driver's own test, `//sim:replay_suite_test`, is in `//...`: a
  stub host (`sim/tests/replay_stub_sim.py`), a fake CD, launch sets and goldens
  built in the test (identical, a different tick field, a shorter run, return
  files, the exit code, a refusal, a fault, a set-up failure, nothing run, a
  timeout, the skips, an unstartable host), the `legacy_launch_set.py` helper,
  every registered suite's plan and INI, and the match between the registry and
  `sim/replay_tests.bzl`. Switching the exit-code comparison off makes it fail.

### Reading the result line

One line per suite, `replay <suite>: <VERDICT> <detail> [<ticks> ticks, <s> s]`:

| Verdict | Meaning | What to do |
|---|---|---|
| `IDENTICAL` | ticks, watches, return files and (where the golden has one) the exit code equal the golden | nothing |
| `DIFFERENT first difference tick N field F: golden X, run Y; K problems (kinds)` | the earliest difference by frame; `F` is a tick field of `TICK_FIELDS`, `tick_frames` (the frame sets differ: the run is shorter or longer), `watch <name>`, `return files` or `exit code` | bisect by frame as in "Bisecting a failed regression"; `result.json` holds the full report |
| `REFUSED 0x... "name" - reason (called from 0x...) see docs/functions/...` | the host stopped at a call its table does not serve (an unhooked function, an unserved import, a service that cannot answer): the host's own `refused` record (exit 70) | hook or implement that VA; `[n ticks]` says how far the run got |
| `FAULT` | an exception nothing handled in es2sim's own code (host exit 71), or the original's fault path (exit code `0xc00000xx` with the host's `FAULT: guest exception` line) where the golden did not exit with the same code | `host-stderr.txt`; for a guest fault the line names the code and the row's VA ([own_process.md](own_process.md#the-originals-fault-paths)) |
| `SETUP-FAILED host exit 64..67: message` | usage, image refused, range not claimable, set-up failed | the message is the host's |
| `NOT-RUN the host logged no ticks and exited with C (golden: N ticks, exit code ...)` | the host ran and exited without a frame | the host's stderr |
| `TIMEOUT` | no end within `ES2_REPLAY_TIMEOUT` | `tick-log.jsonl` has the ticks so far |
| `ERROR ...` | the driver could not run the suite: an unstartable host, a launch set whose member fails its recorded size or hash, a missing input journal | fix the setup |
| `SKIP ...` | no CD, no golden, or no launch set | not a result |

Order of judgement: timeout, fault, refusal (the `refused` event of the tick log,
else the host's stderr record), set-up failure, guest fault, nothing run, then the
comparison. The comparison is `oracle_regression.py normalize` with the
suite's `frame_limit` (the window is the suite's, so a run that logs more ticks
than the window is fine) and `compare`, unchanged. The exit code is compared as
the live runner does ([Exit code of the own DBSIM](#exit-code-of-the-own-dbsim)):
only when the golden's `own_process_exit.exit_code_state` is `exited`; a golden
that the runner stopped (`none`) compares no code.

### The contract between the driver and the host

- **Command line.** `es2sim.exe --headless --config <es2-replay.ini>
  --tick-log <log>`, working directory = the prepared install directory (the
  original runs in its install directory and every game path is relative,
  file_io.md). es2sim builds the guest image from its own tables and reads no
  `DBSIM.EXE`.
- **`es2-replay.ini`** (UTF-16 LE with a byte order mark, the proxy's format):
  `[trace]` `watch_count`, `watchN=NAME ADDRESS LENGTH` (the suite's watch set,
  the `mem_watch.h` syntax with pointer chains), `tick_log_max=0`,
  `tick_log_actor_raw=0`; `[time]` `virtual_time_step_ms=47`,
  `virtual_time_start_ms=16777216`, `virtual_time_pace=0`,
  `virtual_time_intra_frame=1` (what every golden was recorded with: the
  runner passes 47 and pace 0 and `configure-tick-log.ps1` defaults the rest;
  38 of 40 manifests say intra-frame 1, the two oldest predate the field and
  replay identically with it); `[input]` `mode=replay`, `journal=<the
  suite's .jsonl>`; `[arming]` `early=1`. Two sections are the host's own:
  `[game]` `arguments=<the suite's game_arguments, space separated>` (8
  suites: seven with `-SPRUNKNOWN`, `c1_01-tape-record` with `-rc1tape`; the
  host's argument parser reads it in place of a command line) and `[run]`
  `ticks=<registry ticks>`, `outcome=<0|1>` and, for the 26 suites with a
  window, `frame_limit=<n>`: the host stops after the tick of frame
  `frame_limit - 1` (reason `frame_limit` in its `exit` record), so the length is
  exact (the proxy's runs are stopped by a poll), or at the mission's own end.
- **Outputs.** The tick log (schema 2, tick_log.md) with the host's `refused`
  and `exit` records; the return files `results.dat` and `mission.var` in `DATA\`
  of the install directory (the mission end `0x461ff0` writes them there); the
  process exit code. The driver keeps `results.dat` when it exists and differs
  from the recording clones' copy, and `mission.var` when its bytes changed, and
  both for an outcome suite with a launch set, as `SuiteLaunch.psm1` does
  (`golden\<suite>\output\` holds exactly those).

### Working directory: the files the own process gets

Built per suite from `ES2_DATA_ROOT`, copied and never linked (the game rewrites
`PREFS.CFG`, `mission.var` and `results.dat`):

| In the install directory | From |
|---|---|
| `DATA\EXIT.CFG`, `KEYJOY.CFG`, `MAPLABEL.STR`, `MAT0.DAT`, `MFORMS.DAT`, `PREFS.CFG`, `SOUND.CFG` | the CD's `DATA\` |
| `DATA\script.dat`, `player.mec`, `mission.var`, `MISSION.STR` | the suite's launch set (below) |
| `DATA\DRIVE.CFG` | generated: the CD root, then the install directory, CRLF (`prepare-native.ps1` writes the same two lines; DBSIM reads them at `0x45f160`) |
| `DATA\LANGUAGE.CFG` | `G` (German: every golden is German; the launch sets' language is `GER`) |
| `ERROR.STR` | the CD's `GERMAN\ERROR.GER` |
| `VOL\*`, `SAV\*`, `TAPES\*` | the CD's folders (65 MB of VOL, about 0.2 s per suite) |

Not provided: the executables and DLLs, the language folder `GERMAN\`,
`ES2LAUNCH.EXE` and `VSHELL.EXE`. This is a superset of what DBSIM opens by the
list in [file_io.md](../reverse-engineering/file_io.md) (`drive.cfg`,
`prefs.cfg`, `error.str`, `keyjoy.cfg`, `sound.cfg`, `language.cfg`, the launch
files, the VOLs, tapes). Not confirmed by any run: that `DRIVE.CFG` needs no
existing CD path and that `EXIT.CFG` is only written.

**`PREFS.CFG`.** The recording clones' `DATA\PREFS.CFG` differs from the CD's
(read-only comparison of `timing-r111`: bytes 5, 40, 42 and 46 are
`00 01 00 00` on the CD and `01 05 01 01` on the clone, 54 bytes each). The
clone's copy is what the goldens were recorded with, restored after every run;
how it came to differ (an option screen or a mission end writing it) is not
known. The driver uses the CD's file unless `ES2_REPLAY_PREFS` names another,
and the 40 replays are identical with it, so the differing bytes reach no
compared state in these suites.

### Launch sets for the legacy suites

21 of the 40 suites name a launch set under `<es2-data>\oracle\launch`; the
driver verifies size and SHA-256 of its four members as `Read-SuiteLaunch`
does. The other 19 (`acceptance-60s`, `c1_01-success`, `c1_01-collision` and the
other `c1_01-*` suites without a set) ran on the clones' prepared C1_01 files,
which exist only as the clones' `DATA\` content: `script.dat`, `player.mec`,
`mission.var` and `MISSION.STR` are byte-identical on `timing-r111`, `-p1`,
`-p2`, `-p4` and `-p5` (not on `-p3`, last used for another launch set, nor on
`native-r111`, whose `player.mec` differs; read-only comparison, with the full
regression `check-r9-1` 40 of 40 identical on those clones). The driver runs
these suites on the launch set **`c1_01-legacy`** and reports `SKIP` (with the
command) while it is missing. It is created once from a clone, starting
nothing:

```powershell
& <es2-dependencies>\python\python.exe -B sim\tests\legacy_launch_set.py `
    build\installs\timing-r111\DATA <es2-data>\oracle\launch\c1_01-legacy
```

The script reads the four files, refuses any whose SHA-256 is not the pinned
one (`script.dat` `61efab9b...`, `player.mec` `f669d125...`, `mission.var`
`20c8eb56...`, `MISSION.STR` `4c359b61...`) and writes a schema-1 set that
`Read-SuiteLaunch` accepts too; `--dry-run` writes nothing. The set exists, and
the 19 legacy suites replay IDENTICAL on it, which confirms that these files
are the state the early goldens were recorded on.

### Trap cost and timing

Every `result.json` carries `timing`: `run_seconds`, the logged `ticks`, the
host's `exit` record `trapped_calls`, the estimate `trap_seconds_estimate`
(calls times 11.58 microseconds, the stage-1 trap's cost per call) and
`hot_calls` (the three most called VAs of the record's `calls` table). The
measured trap share, the frame times and the bridge seam built for the hot VAs
are in [own_process.md](own_process.md) (`sim/tests/perf_suite.py`).
