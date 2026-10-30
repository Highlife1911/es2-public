# Mutation witnesses of the shell's tests

Which deliberate one-line change ("mutation") of the library code each shell test
is known to catch. Every entry is a change of one line (or one condition), the named
test run, then the change reverted; "caught" means the named test (or both when
two are named) failed with the mutation in place. The table records those runs;
it is not re-run by any target.

How to read a row: *test* (Bazel target in `//shell` unless stated), *area*,
*mutation* (shortened), *result*. Mutations the tests missed are in the last
section; they are as informative as the catches.

## Real-data tests (`*_real_test`, `ES2_DATA_ROOT`)

| Test | Area | Mutation (what was broken) | Result |
|---|---|---|---|
| slots_real_test | slots | loader without the ESTEXT 0x21 append; writer rewrites by truncation; slot 10 stays 10 in mode 0; load keeps the item order; prepared SAV without team words; briefing count includes -1 entries | each caught (slots_real_test; M1/M2/M5 also slots_test/save_test) |
| menu_real_test | slots | load keeps the item order; menu exit does not save a slot-only game; RESTORE without the slot-10 save | caught |
| bay_real_test | service bay | mount returns the old item to the tail; bay state read as a file; rack word +8 rule 13..16; launch files without the bank; ARM answer by vehicle position + 1 | caught (the first three also campaign_test) |
| menu_real_test | service bay | leaving WEAPONS does not rebalance; WEAPONS again leaves WEAPONS; launch ignores the service-bay changes | caught |
| texts_real_test | texts | intelligence references read one word early (0x56 to 0x54); debrief column order +0x2d,+0x2f,+0x31; campaign index = group instead of group - 1; squad loop up to team word 0 inclusive | each caught (also texts_test; the ENG/FRE page and edit sweeps fail) |
| launch_real_test | launch files, preferences | condition > 50 to >= 50; item ID < 29 to <= 29; progress == 100 to >= 100; squad rules (last group wins, inactive pilots checked, orders up to capacity); prefs store zero beyond a short file; prefs cancel without revert; prefs accept without commit; music toggle sets 1 | caught (all but "progress" also campaign_test) |
| practice_real_test | practice | no early return for the selected entry; control wraps at >= modulus; INSTANT ACTION +0x2e not stepped; new game without the nine-word bank; Herc Type enabled from entry 3 | caught (1,707-record sweep) |
| editors_real_test | editors | band limit > to >= (0x43e05f); single repair limit threshold index; scrap deactivation without the active test; REPAIR enable >= to >; build countdown from +8; cond_set family 1 word | caught (6/6; the scrap case only by the real test, unit test passed) |
| editors2_real_test | CREW, ARMORY | vehicle-8 check without the order test; ARMORY add <= instead of <; scrap number without minimum 1; pilot click keeps the old holder's order; activity with order <= capacity; other button re-adds as many as it removed; CREW show without final order row 0; scrap without rebalance | caught (8/8) |
| stats_real_test | statistics | refresh drops CAREER sector word +0x3f; salvage dword skip 0x12 changed to 0x10 | caught (sweep and corpus) |
| help_real_test | help | byte 6 not cleared; committed block not updated; wrong German manual path | caught (3/3) |
| movies_real_test | movies | ring modulus `% 9`; ring lookup with the first slot skipped; DROPSHIP theatre flag not cleared (0x41ea6a); palette switch inside credits; sequence stored from the wrong argument | caught (5/5) |
| menu_real_test | menu, Replay | the synthetic part fails, Replay No passes | caught; movies_test failed too |
| menu_real_test | bay MAIN MENU | bay MAIN MENU without the slot-10 save; bar button equal to the word leaves again | caught (M1 leave of BUILD without 0x412a37 by flow_test) |
| menu_real_test | engine I/O | `engine.c` mode taken as 0 (M8); weapon rows minus one (M9) | caught; see the last section for the ones it missed |
| draw_real_test | blitter, DBM | mirror bit tests `& 1` to `& 2`; clip right bound `right + 1` to `right`; opaque test accepts index 0; `dbm.c` run count `& 127` to `& 63` | caught (the DBM one by the real test alone, draw_test passed) |
| text_real_test | text draw | baseline pen offset -3 to -2; edit width margin 6 to 5; centring `>> 1` to `/ 2`; label rectangle width minus one | caught (4/4) |
| widgets_real_test | widget draws | title stripes 13 lines per group; region dither phase starts at 1; frame without the corner pixel; pen high byte ignored; arm split line one row lower | caught (5/5, per-kind case counts) |
| mission_map_real_test | mission map | line step at e > 0 instead of e >= 0; triangle band colour + 1; clamp y margin from the half width | caught (map probes and raster cases; the clamp one by the 5,291 map probes only) |

## Unit and screen tests (no data needed)

| Test | Area | Mutation | Result |
|---|---|---|---|
| campaign_test, campaign_cli_test | campaign | signed mode; repair UI range; short-file pad; swapped options | caught (4/4) |
| flow_test | bay MAIN MENU | leave of BUILD without 0x412a37 | caught |
| gui_test | input paths | right-button value `l + 2 * r` to `l + r` (0x4050bd); strict containment `>` to `>=` (0x41ff44..0x41ff82) | caught (1 and 2 of 242 checks) |
| main_menu_test | main menu | frame 5 to 4; CONTINUE colour 0x22 to 0x26; CONTINUE hides for every outcome; CREDITS gate stays 0; lswitch2 flag not set | caught (5/5) |
| screen_test --suite registration | registration | ACCEPT frame colour 34 to 33 | caught (4 of 5 cases) |
| saved_games_test, screen_test | saved games | selected row colour; HERC/HERC+1 column order; row text NULL; select effects dropped (0x43863b); `flow.c` final word 2 shortcut | caught (5/5; the last by flow_test and saved_games_test) |
| practice_screen_test, screen_test | practice screen | row colour 0x29 to 0x27; enable `index >= 4` to 3; value store dropped; click button swapped; DBSIM flow call dropped; hide (0x44c97a) dropped; old row colour | caught (7/7; M2, M5, M6 by screen_test) |
| preferences_test, screen_test, flow_test | preferences | CANCEL ramps down; window does not leave full screen; music toggle before the ramp; SFX checkbox from byte 0; out-of-range radio; Alert in full screen | caught (6/6) |
| screen_test --suite service_bay | service bay screen | band compare `>` to `>=` (0x43e08f); CREW word store dropped | caught |
| weapons_screen_test, screen_test | WEAPONS screen | row colours 0x10 to 0x11; slot compare inverted; next slot `+ 2`; guide control order; oldest-first loop skips one; slot -1 test | caught (6/6; M5 by weapons_screen_test only) |
| repair_screen_test, screen_test | REPAIR screen | band `>` to `>=`; old row keeps colours; button frame colours swapped; accept without bay change; mode text for option 2; capacity `jl` to `jle` | caught (6/6; two by repair_screen_test only) |
| the build screen's test  | BUILD screen | colour 0x22 to 0x26; `[ %1d ]` format; nine-flag inverted; edit row compare inverted; table value 0x0e to 0x10; free count 5 to 4 | caught (6/6) |
| crew_test | CREW screen | colours `o <= capacity`; selection kept before BAY; CLEAR keeps pointer; vehicle text base 0x6f | caught (4/4) |
| mission_screen_test, screen_test | mission screen | page 4 leaves Rock & Roll enabled; statistics align; page-0 frame 423; page buttons turn the campaign panel; theatre palette; old panel hide 0x445343; statistics not hidden on page 1 | caught (7/7) |
| sounds_test | sounds | byte 5 `^= 1` to `= 1` | caught |
| boxes_test, screen_test --suite boxes | boxes | end line 0x12f to 0x130; quit passes > 50 to > 49; warning line 0x13d to 0x13c; cheat left-down gate | caught (4/4; mutation 2 by boxes_test only) |
| //media:iv32_bind_* | IV32 binding | ICINSTALL_FUNCTION branch removed; bind-status early return removed | caught (3 and 2 tests); a third pattern did not apply |
| //tools:process_starts_test, scripts | install scripts | no restore call; primary installation allowed; no extra file removal; game process ignored; dry run backs up; restore under the game | caught (6/7) |
| gui_test, widgets_test, gui_tables_real_test, saved_games_test | input tables | modifier command table read one word early; poll uses the built-in table only; a one-byte-short block accepted; saved games: table read one word early | caught (4/4) |
| gui_tour_real_test, screen_test | frame tour | bay button pressed state not set; caret one pixel narrower; the screen cases' main-menu history copy removed | caught (3/3) |

## Not caught, or caught by another test only

| Area | Mutation | Note |
|---|---|---|
| launch files, preferences | armed: slots below the capacity only | survived; equivalent under our SAV parser (load_save rejects slot records at or above the capacity) |
| practice | mode-0 SAV: item record +6 from the condition | survived (exit 0, no FAIL line) |
| install scripts | backup after launch not verified (digest) | missed by the script tests |
| engine I/O | `engine_io.c`: debrief text condition forced off (M3); MISSION.STR written without the over-write helper (M4); `engine.c`: final fields fixed to 2 (M5); player used-check dropped (M6); RNG not stored after launch (M7) | NOT CAUGHT by menu_real_test; M5 and M6 are caught by engine_test, M1 (row_enabled `&&` to `\|\|`) also by engine_test |
| service bay screen | palette 1 also when leaving the bay alone | all five service_bay suites still pass |
| WEAPONS screen | oldest-first loop skips one (M5) | screen_test passes, weapons_screen_test catches it |
| blitter, DBM | `dbm.c` run count mask | draw_test passes (cached); only the real test catches it |
