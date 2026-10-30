# VSHELL UI: screens, handlers and shell state

The menu and screen code of the reference `VER95/VSHELL.EXE`
([reference_version.md](../reference_version.md)) and the
own shell that reproduces it (`shell/`, [shell/README.md](../../shell/README.md)).
Addresses are VAs at image base `0x00400000`. UI labels are the English
`ESTEXT.BIN` strings (LANG0.VOL, `eng\estext.bin`) the code passes to the
string lookup, cited by ID.

Status markers:

- **L** read in the `llvm-objdump -d -M intel` listing (pinned LLVM-MinGW)
  at the cited addresses; the static confirmation.
- **V** equal to the original: VSHELL's own code run in vitro by the
  shell-invitro slot mode ([shell_invitro.md](../reference-game/shell_invitro.md)),
  an enumeration over the executable's bytes, or VSHELL's captured frames.
- **H** hypothesis.
- Own tests (named per fact) check the own code, or the own presenters
  against each other; they are not evidence about VSHELL.

Contracts documented elsewhere are referenced, not repeated: preparation,
results path, debrief selection, SAV fields and preferences in
[vshell_preparation.md](vshell_preparation.md); the ES/VSHELL/DBSIM chain in
[launcher.md](launcher.md); file call sites in [file_io.md](file_io.md); SAV
and GAMEFILE framing in [mission.md](../formats/mission.md#sav-field-values);
the widget toolkit in [vshell_widgets.md](vshell_widgets.md); the process and
main loop in [vshell_process.md](vshell_process.md); the map of MISSION
page 1 in [vshell_mission_map.md](vshell_mission_map.md).

Sections 1 to 8 are the inventory (framework, entry, screen tree, the
screens' handler tables, field matrix, DBSIM transitions, open questions);
9 to 22 the state libraries behind the screens; 23 to 42 the graphical
screens and the presenter.

## 1. Framework

| Item | Evidence (L) | Notes |
| --- | --- | --- |
| Screen objects | `0x40180c` calls 22 constructors (`0x401812..0x40187b`) before the entry dispatch | Every screen is built once at start-up, hidden, and later shown/hidden. No screen is created on demand. |
| Show / hide | `0x41f93a(obj)` / `0x41fabd(obj)` | E.g. START NEW GAME shows the registration frames with `0x41f93a` (`0x43c29a`), its Accept/Cancel hide them with `0x41fabd` (`0x43c349`). Modal box show/hide `0x40b7e9` / `0x40b808`. |
| Controls | text button `0x40a004(obj, parent, rect, font, handler, label)`; image button `0x40a2c4(…, handler, label)` (9 args); panels `0x40bc48`, `0x40b0fc`; label `0x40b828`; text update `0x40b8bb` | The handler is a code pointer argument; handlers end by destroying the event object (`call [vtbl+0x18](ev, 3)`). Release/press semantics: §15.1. |
| Strings | `0x4087f0([0x46ddf8], id)` | `[0x46ddf8]` is the `<lang>\estext.bin` table loaded by `0x40796c`. |
| Root window / main loop | root `[0x481180]`; loop `0x402018..0x402191`: input-device poll `0x41e0bf` (not the Windows message pump, which is `0x406328`: vshell_process.md §8), event dispatch `0x46848c`, present of the dirty rectangles `0x41fe49`, queued movies `0x41e98c(1)` | Quit flag byte `0x46c268` is tested after every step (`0x402063..0x4020da`, `0x40218a`). Step 6 is the map animation (§42.6), step 10 the Quit-box repaint (§22.3). |
| Exit | loop end `0x402197`: `0x40e933(10, 0)` | Saves the current game to slot 10 on **every** exit (also exit 2 to DBSIM), but only when a game is loaded (`0x4826ee` ≠ 0, test `0x40e947`). Mode 0 redirects slot 10 to 11 ([mission.md](../formats/mission.md)). |
| Exit code | setter `0x408d1a`; callers in §6 | The value ES.EXE receives ([launcher.md](launcher.md#other-side-of-the-protocol)). |
| Event queue | post `0x468780` (→ `0x469ea8`); drain `0x469ee4`, loop `0x469fd7..0x469fe5` | The drain pops until the queue is empty, so events posted by a handler run in the same drain (§15.1). Type `0x20` events with byte +0x24 = 2, then 1 and target +0x20 are synthetic clicks (press, release). |
| Movie queue | enqueue `0x41e8c0(index, rect, …)`, ring of 10 records at `0x485748`, skipped when flag `0x482359` is zero or the index is already queued (`0x41e88f`); player `0x41e98c` | Index → name through the pointer table `0x470fb4` (86 names, `avi\pt1.avi` … `avi\dropship.avi`; [file_io.md](file_io.md)). An AVI that cannot be opened shows the CD prompt (`0x41ede0`, §20). Details §19. |
| Service-bay screen word | `0x4758b8`: −1 bay only, 1 save, 2 weapons, 3 repair, 4 build, 5 armory, 6 crew, 7 mission (stores in the bay handlers `0x43a768..0x43af5a`) | Leaving a screen runs `0x43a537` (jump table `0x43a54a`): 2 → `0x43fc62` + `0x412a37`; 3 → `0x433a7b` + `0x412a37`; 4 → `0x446fde` + `0x412a37`; 5 → `0x449c46`; 6 → `0x44218a`; 7 → `0x445095`. |

Source modules, from assertion file-name strings (L): `wsrvbayi.cpp` service
bay `0x43afd4`; `warmingi.cpp` weapons `0x43ebbc`; `warmoryi.cpp` armory
`0x4484c4`; `wcrewi.cpp` crew `0x441548`; `wmissini.cpp` mission screen
`0x4445c3`/`0x44472f`; `wsquadi.cpp` squad panel `0x43c830`; `shellmap.cpp`
map `0x4242a0..0x427634`; `hercdisp.cpp` Herc pictures (§36); `GAME.CPP`
game state/launch files; `CAREER.CPP` CAREER and briefing texts;
`msn_gen.cpp` preparation; `AVI.CPP` movies; `WMAIN.CPP` help; `stats.cpp`
slot statistics.

Own code: the state machine `shell/src/flow.c` (`es2/shell/flow.h`, §18);
the toolkit, event queue and main loop of the graphical shell in
`shell/src/gui/` (`widgets.c`, `events.c`, `loop.c`, `app.c`;
[vshell_widgets.md](vshell_widgets.md)); one file per screen, registered by
one line each in `shell/src/gui/screens.c`; the text presenter
`shell/src/menu.c`; the host (files, preparation, launch)
`shell/src/host.c`.

## 2. Start and entry dispatch

`-X<state>` is stored in the entry mode `0x482362` (start-up `0x401aa4`,
[launcher.md](launcher.md)). Start-up (L): VOL registration, `error.str`,
option parser `0x4015d8`, a CD check that opens `avi\pt1.avi` on the CD
(`0x401cb1`, §20), game init `0x40e736` (GAMEFILE.STR, HERC_INF, DAMAGE,
slot statistics §13.2, mode word from `prefs.cfg` +0x2a), WEAPONS `0x4125e8`,
CAREER `0x412f2a`, map tables `0x424408`, sound `0x42f2f4`, screens
`0x40180c`. The language word: §35. Dispatch at `0x40188a`:

| `-X` | Path (L) |
| --- | --- |
| 3, 4 | load slot 10 (`0x40eaaa(10,0)`; slot 11 in mode 0), results path `0x40f09f` ([results path](vshell_preparation.md#results-path)); its UI is in §4.9 |
| 6 | no results; music, show main menu `[0x48d1a0]` |
| 1, 0, 2, 5, other | intro movies `intr_pt1.avi` (0x44), `intr_pt2.avi` (0x45), then main menu |

Own code: `es2_shell_flow_entry_of` and `es2_shell_flow_start` (`flow.c`,
entries `ES2_FLOW_ENTRY_INTRO`, `_MENU`, `_RESULTS`) and the host's entry
(`host.c`).

## 3. Screen tree

```
Main menu 0x430fc4
├─ INSTANT ACTION ──────────────────────────────► exit 2 (DBSIM)
├─ START NEW GAME → Registration 0x43bd2e ─► (debug dialog, auto) ─► Service bay
├─ CONTINUE GAME ─► Service bay | End of game box 0x44d2bc
├─ SAVE/RESTORE ─► Saved games 0x438c40 ─► Service bay | Main menu
├─ PRACTICE MISSIONS ─► Practice 0x44b310 ─► (debug dialog, auto) ───► exit 2
├─ PREFERENCES ─► Preferences 0x435598 (+ Alert box)
├─ VIEW DEMO ───────────────────────────────────► exit 5 (DBSIM -D)
├─ CREDITS (credits.avi) · ONLINE MANUAL (WinHelp)
└─ QUIT ──────────────────────────────────────── exit 0
Service bay 0x43afd4 (bar of image buttons, left squad panel 0x43d029)
├─ MAIN MENU (saves slot 10)   ├─ SAVE ─► Saved games
├─ WEAPONS 0x43ebbc            ├─ REPAIR 0x4326c7 ─► Scrap warning 0x4479b8
├─ BUILD 0x445de8 ─► Scrap warning 0x4479b8
├─ ARMORY 0x4484c4 ─► Weapons scrap warning 0x447e3e
├─ CREW 0x441548               └─ MISSION 0x442bc4 ─► Rock & Roll ─► exit 2
Modal boxes: Quit 0x431fa8, CD prompt 0x4322a8, End of game 0x44d2bc,
Replay mission 0x44cdac, Launch warning 0x44d66c, DEBUG mission file 0x44dd38
```

Main-menu strings without a button: CONTROLS (0x8), VEHICLE PREVIEW (0x9),
SERVICE BAY (0xc) are never referenced; the constructor creates exactly the ten
buttons of §4.1 (L). The CONTROLS screen strings 0xdd..0xef, "Theater Map"
(0xb1), "Sector Map" (0xb2), "Mission Debriefing" (0xb5), "Arming" (0x101),
"Graphic Detail Level" (0x104), "Language" (0x105) and "Prompt for Saves"
(0x109/0x10a) have no literal reference either (L: no `push id` before
`0x4087f0`); **H:** cut features. A second name-entry screen `0x43b8f0`
(ENTER NEW PILOT NAME, SKILL LEVEL, Accept `0x43c5ab` → `0x40e8a5`) is built,
but its show routine `0x43bcf0` has no static caller or pointer (L; **H:**
dead).

Own code: the screen identifiers of `flow.h` (`ES2_FLOW_MAIN_MENU`,
`ES2_FLOW_SERVICE_BAY`, …).

## 4. Screens: controls and handlers

One table per screen: the controls, their handlers and what they change (L
unless marked). The screen-side details (objects, show/hide, drawing, own
code) are in sections 23 to 33; the state changes in sections 9 to 17.

### 4.1 Main menu (`0x430fc4`, frame `[0x48d1a0]`, buttons in `[0x48d1ac]`)

Resources: animated background `dbm\bay2a_80..84.dbm` (§23.1). Mode word
writes go through `0x40ec56`
([shell options](vshell_preparation.md#which-controls-write-them)).

| Button (ID) | Handler | Effect (L) |
| --- | --- | --- |
| INSTANT ACTION (3) | `0x431935` | preset flag `0x473784` := 1; mode 0; `0x44c58b`: selects practice index 8 + prefs +0x2e and steps +0x2e modulo 3 (writes prefs.cfg); new game `0x40e8a5("TRAINEE", c3)`; `0x40f68c` (player.mec, mission.var); exit 2; quit flag (§15.3) |
| START NEW GAME (4) | `0x431a08` | mode 1; shows Registration (§4.2) |
| CONTINUE GAME (5) | `0x431a73` | mode 1; load slot 10 with briefing texts (`0x40eaaa(10,1)`); selected slot `0x474fde` := 10; final word `0x4826f2` = 2 → service bay (`0x43b7f2(8)`, `0x43b758`), else End of game box with text by final word (§4.10, §11.3) |
| SAVE/RESTORE (6) | `0x431b27` | mode 1; caller mark `0x48d424` := 0; Saved games (§4.3) |
| PRACTICE MISSIONS (7) | `0x431f3a` | Practice (§4.4); mode 0 |
| PREFERENCES (0xa) | `0x431b9b` | Preferences (§4.5) |
| VIEW DEMO (0xb) | `0x431bfe` | exit 5; quit flag (§15.5) |
| CREDITS (0xd) | `0x431c7b` | queues and plays `credits.avi` (0x54), back to the menu |
| ONLINE MANUAL (0xf) | `0x431e1b` → `0x431e79` | prefs +6 := 0 and write prefs.cfg; `WinHelpA` on `<CD>\english|french|german\es2guide.hlp` by `language.cfg` (§21) |
| QUIT (0xe) | `0x431db6` | quit flag (exit code 0) |

### 4.2 New career: Registration (`0x43bd2e`, frames `[0x48d574/578/57c]`)

| Control | Handler | Effect (L) |
| --- | --- | --- |
| SKILL LEVEL button | `0x43c6ad` | `0x476248` := (value+1) mod 4, label ROOKIE/REGULAR/VETERAN/ELITE (0x35+value, `0x43c671`) |
| CANCEL (0x33) | `0x43c728` | hide, back to main menu (`0x431718`) |
| ACCEPT (0x34) | `0x43c78b` | HERC_INF reload `0x411e40`; `0x477946` := 0; new game `0x40e8a5(name, skill)`; mission-screen page `0x481108` := 0 if CAREER position = 0, else 1 |

New game `0x40e8a5(name, rank)` (L): final word `0x4826f2` := 2; briefing/debrief
movie flags cleared; bank cleared (`0x40eef1`); WEAPONS inventory `0x4125e8`;
roster `0x410055`; player `0x41072b` with the **skill as rank**; hangar
`0x410b11(…, mode)`; CAREER position `0x413053(mode)` (mode 1 → (1,0); mode 0 →
(0, practice index `0x479c54`)) which ends in the DEBUG dialog (§4.11, §39);
then salvage total `0x482bd8` := 100,000 + 1,000 × draw(bound 11) from the
preparation RNG `0x482409` (`0x40e906..0x40e924`); game-loaded flag `0x4826ee`
:= 1. So the in-game "skill level" and the practice "Mission Difficulty" are the
player rank word that becomes script header word 7 in mode 1. What the new
game keeps from the previous game: §9.4, §15.4. The screen: §24.

### 4.3 Saved games (`0x438c40`, frames `[0x48d428]`, `[0x48d42c]`, `[0x48d498]`)

Shown by `0x43a19c` (from the main menu with `0x48d424` = 0, from the bay
with 8). Ten slot rows `[0x48d434+4i]`, text `"%2d. %s"` from the slot names
`0x4826f4+0x5e·i` (`0x4381cd`); statistics labels Name/Skill/Rank/Herc,
Flyer, Base kills, Current/Total, Salvage, Sector, Mission (0x24..0x2e,
`0x4377bc`, from `0x430ec4(slot)`, `stats.cpp`; §13). Selected slot `0x474fde`.

| Button (ID) | Handler | Effect (L) |
| --- | --- | --- |
| SAVE (0x1d) | `0x438263` | naming mode `0x474fdc` := 1, edit field on the slot (`0x437e62`, := 2) |
| ACCEPT (0x34) | `0x43868a` | save `0x40e933(slot, name)`, refresh statistics `0x430ec4` |
| CANCEL (0x33) | `0x4384aa` | restore slot texts, naming mode 0 |
| RESTORE (0x1e) | `0x438393` | load `0x40eaaa(slot, 1)`, save it as slot 10, `0x477946` := 0, service bay |
| EXIT (0x1f) | `0x438424` | back to the main menu (`0x48d424` = 0) or to the bay (8) |

Save `0x40e933` writes slot name/flag to `sav\gamefile.str` (`0x40e6cd`,
`0x40e503`), then `sav\<slot>`: inventory, selection + copies
`data\script.dat`→`sav\script%d.dat`, `data\mission.str`→`sav\missn%d.str`,
`data\player.mec`→`sav\player%d.mec` (`0x413095`), roster, team, hangar,
nine-word bank, salvage total, bank, final word, 20 bytes `0x4833ac`. Load
`0x40eaaa` reads the same and **always copies the three launch files back to
`data\`** (`0x4131e3`, `0x413285..0x4132eb`); the flag argument only decides
whether the briefing texts are recomposed (`0x4132f6` → `0x4135bb`). It sets
`0x4826ee` := 1 and rebuilds the map object (`0x40e75f`). `mission.var` is
not part of a slot. The slot table, the row edit rules, the enabling of SAVE
and RESTORE and the absence of a delete: §9; the screen: §26.

### 4.4 Practice missions (`0x44b310`, frames `[0x48dbfc]`, `[0x48dc00]`, `[0x48dc08]`)

Eight entries 0xf2..0xf9 (Basic Training 1..3, Flyer, Strike, Escort, Recon,
Scramble), list items `[0x48dc5c+4i]`; entry handlers `0x44caa3`,
`0x44cb04`, …, `0x44cd4a` call `0x44c40c(i)`, which stores the CAREER
group-0 position `0x479c54` := i and sets prefs +0x28 from the table
`0x479c56` (`0x44c53d`; §15.2). Parameter buttons step prefs bytes
(`0x40de5b`/`0x40de88`; a left click steps forward, a right click back,
§15.2), label = base ID + value:

| Button (ID) | Handler | Prefs byte (mode-0 control) | Values |
| --- | --- | --- | --- |
| Ammo (0xfc) | `0x44c676` | +0x25 (c1, script header word 5) | Limited/Unlimited (0x12a+v) |
| Damage (0xfb) | `0x44c5b9` | +0x26 (c2, word 6) | Vulnerable/Invulnerable (0x128+v) |
| Mission Difficulty (0xfd) | `0x44c733` | +0x27 (c3, word 7 and player rank) | ROOKIE..ELITE (0x35+v), mod 4 |
| Herc Type (0xff) | `0x44c8ad` | +0x28 (c4, preset branch) | Outlaw..Razor (0x6e+v); the button cycles 4..7 only (§15.2) |
| Time of Day (0xfe) | `0x44c7f0` | +0x29 (c5, word 9) | Day/Night (0x12c+v) |
| Main Menu (0x100) | `0x44c96a` | — | hide, main menu |
| Begin Mission (0x102) | `0x44ca26` | commit + write prefs.cfg | new game `0x40e8a5("TRAINEE", c3)` in mode 0 → DEBUG dialog auto click → preparation of (0, `0x479c54`) → exit 2 (§4.11) |

This names the raw controls c1..c5 of
[Environment and shell mode](vshell_preparation.md#environment-and-shell-mode)
(L, string arithmetic `0x44c5f9`, `0x44c6b6`, `0x44c773`, `0x44c830`,
`0x44c8ed`). Instant action uses group-0 positions 8..10 (prefs +0x2e + 8;
DEMO is (0,8) per vshell_preparation). The screen: §25.

### 4.5 Preferences (`0x435598`, shown by `0x436d45`)

Repair/weapons-building radio buttons and Accept/Cancel are in
[shell options](vshell_preparation.md#shell-options). The other controls
(L, own tests §11.4):

| Control (ID) | Handler | Prefs byte |
| --- | --- | --- |
| Window (0x124) | `0x437597` | +6 := 0 (`0x437200`) |
| Full Screen (0x125) | `0x437608` | while windowed: shows the Alert box (0x126/0x127), whose ACCEPT `0x437678` sets +6 := 1 (§22.2, §27.3) |
| Music (0x116), check box | `0x437351` | +0 toggled (`0x436ed1(0)`); ramps the shell's WAV music down/up (`0x42f7f0`/`0x42f894`); the `cdaudio` calls are dead code (audio.md "Shell audio in short") |
| Sound Effects (0x117), check box | `0x4373b2` | +1 toggled (`0x436ed1(1)`) |
| High Res / Low Res (0x121/0x122), radio pair | `0x437758` / `0x4376f7` | +4 := 0 / 1 (`0x43714c`) |

The handlers sit in the panels of their labels: Audio/Speech Options (0x106)
holds Music, Sound Effects and the two check boxes; Game Resolution (0x120)
holds the radio pair. Byte +2 is changed by no control. Resources
`dba\chk_box.dba`. DBSIM reads bytes +0..+0xe
([where the values end up](vshell_preparation.md#where-the-values-end-up-and-dbsim)),
so these choices reach DBSIM through `prefs.cfg`; formats/cfg.h names the
same bytes from DBSIM's side (+0/+1 audio switches, +4 resolution: 1 =
320x240). The blocks and their commit/revert: §11.4; the screen: §27.

### 4.6 Service bay (`0x43afd4`, frame `[0x48d520]`, bar `[0x48d528]`, background `[0x48d524]`)

Resources `dba\mnu_bttn.dba`, `dba\online.dba`. Shown by `0x43b7f2(8)` +
`0x43b758`. `0x43b758` enables the bar's five editor buttons by mode
(`+0x49`): mode 0 enables WEAPONS and CREW and disables REPAIR, BUILD and
ARMORY; mode 1 enables all five. MAIN MENU, SAVE, MISSION and the online
image are not touched (§28.1). Background image by CAREER group and the
page word `0x481108` (`0x43b877`).

| Button (ID) | Handler | Effect (L) |
| --- | --- | --- |
| online image | `0x43a5bd` | help (`0x431e79`, §21) |
| MAIN MENU (0x13) | `0x43a66c` | leave screen, hide bay, save slot 10, main menu |
| SAVE (0x14) | `0x43a768` | Saved games, screen word 1 |
| WEAPONS (0x15) | `0x43a862` | squad panel (2), `0x43fbd8` |
| REPAIR (0x16) | `0x43a962` | squad panel (3), `0x43397c` |
| BUILD (0x17) | `0x43aa62` | squad panel (4), `0x446f9d` |
| ARMORY (0x18) | `0x43ab62` | `0x449b87` |
| CREW (0x19) | `0x43ac5a` | squad panel (6), `0x442091` |
| MISSION (0x1a) | `0x43ad5a` | `0x43aee7(page, 0)`: page 0 if the group movies were not shown (`0x477946`) and position = 0, else page 1 |

Squad panel (`0x43d029`, `wsquadi.cpp`; hot spots `gam\arm_hots.dat`,
`gam\rpr_hots.dat`, never shown: §36.3): eight bay selectors
`[0x48d59c+4i]`, Pilot/Skill/Condition labels, "Squad Inventory". Selecting a
bay (`0x43dcdd`, §17.1) stores the UI selection `0x482bc9` (hangar object
+0x22, not serialized by the hangar writer). In CREW it also calls
`0x4426e5` (assignment, §17.3). The screen: §28.

### 4.7 Hangar screens

| Screen | Constructor / show / leave | Controls → handler | Writes (L) | Reads |
| --- | --- | --- | --- | --- |
| WEAPONS (`warmingi`) | `0x43ebbc` / `0x43fbd8` / `0x43fc62` | weapon list rows `0x440990`… → `0x43fdac`; guidance buttons ARM/ARH/SARH/EO `0x440626`, `0x44068b`, `0x4406f0`, `0x440755` (`0x4403f9` → `0x43e346`: they set the mounted item's guidance mode, they do not filter the list); slot prev/next `0x4408d4`, `0x440932` (`0x43e3d9`, `0x43e399`: they step the weapon slot, not the bay); description `0x4407ba` (no mount) | mount `0x43e2d4` → `0x411b10`: hangar slot item, slot condition, secondary, inventory lists ([hangar rules](vshell_preparation.md#hangar-rules-of-the-vshell-ui)); guidance mode = item word +8; details §10.2 | availability +0x16, ARM tables, `dba\arm_weap.dba`, `gam\arm_weap.dat`, `<lang>\wpn_desc.bin` |
| REPAIR | `0x4326c7` / `0x43397c` / `0x433a7b` | External/Internal tabs `0x4346ec`, `0x434d95`; REPAIR `0x4351bd`; REPAIR ALL `0x4352e9`; SCRAP `0x4353a5`; CANCEL `0x435403` | REPAIR: salvage −= cost, the condition words of the row's group (`0x411a78`, `0x4122e1`; §12.1, §12.2); REPAIR ALL: salvage −= `0x41401b(L,100)`, `0x4119d3(L,100)`, without a budget test (§12.3); CANCEL restores salvage and conditions from the entry snapshot (`0x48d33c`, `0x48d340`); SCRAP opens the Herc scrap warning | repair option +0x2c (mode text "Auto/Manual Repair", `0x433995`) |
| BUILD (Herc construction) | `0x445de8` / `0x446f9d` / `0x446fde` | Build `0x4475ce`; Scrap `0x447570` | Build: `0x40eed4(type)` → `0x410fa6` new hangar record under construction, salvage −= cost (§12.4) | nine-word bank (`0x446ece`, `0x4471a8`), HERC_INF |
| Herc scrap warning | `0x4479b8` | ACCEPT `0x448326`, CANCEL `0x4482c8` | `0x40ed0f`: salvage += `0x410f46(bay)`, Herc removed, its pilot's bay −1 and order deactivated (`0x41094b`; §12.5) | — |
| ARMORY (`warmoryi`) | `0x4484c4` / `0x449b87` / `0x449c46` | rows `0x449d2f`; Clear `0x44a584`; Scrap `0x44a508`; +/− `0x44a63b`, `0x44a694` (empty handlers, §17.4) | five-entry build bank `0x46fa14/0x46fa16[5]`: add to the first free entry (`0x412c0b`), Clear removes every entry of the type (`0x412c31`); rows edit only with weapons building = manual (`0x449d3a`, `0x44a077`) | costs (`0x412baa`), inventory counts, `<lang>\wpn_info.bin` |
| Weapons scrap warning | `0x447e3e` | ACCEPT `0x448447`, CANCEL `0x4483e9` | `0x40ed6a(item)`: salvage += `0x412b79(item)`; then `0x412a37` (§17.5) | — |
| CREW (`wcrewi`) | `0x441548` / `0x442091` / `0x44218a` | pilot lists (three roster groups) `0x4427e1`, `0x44288c`, `0x442937` → `0x442548(g)`; CLEAR `0x4429e2` → `0x442624` | pilot bay/order (`0x40ec8f`, `0x4108e2`), player bay (`0x40ec80`), activation/count (`0x41098a`); CLEAR: bay −1, order −1; the assignment rule §17.3 | roster, `dba\star.dba`, `dba\c_pilots.dba` |

Leaving WEAPONS, REPAIR or BUILD and confirming a weapons scrap run
`0x412a37`, the [weapons-building step](vshell_preparation.md#results-path)
of the results path: AutoBuild refills the build bank by priority, Manual
drops entries the salvage cannot pay. The screens: §29 to §32.

### 4.8 Mission screen (`0x442bc4`, `wmissini`; open `0x43aee7(page, click)` → `0x444873(page)`; leave `0x445095`)

`0x43aee7` stores the page in `0x481108`, leaves the current screen, sets the
screen word 7, and with `click` = 1 posts a synthetic click on the MISSION
button. Map object built by `0x40e75f` (`shellmap.cpp`: `data\script.dat`,
`data\mission.str`, `data\maplabel.str`, `data\player.mec`, `data\mforms.dat`,
zone DAT/DBA, `dba\mis_icon.dba`, `dba\mapshp.dba`, `dfn\map.dfn`; read only).
Images `dba\miss_arw.dba`, `dba\terradef.dba`, `dba\th_earth.dba` /
`dba\th_moon.dba`.

| Page | Content (L) | Movie |
| --- | --- | --- |
| 0 | title: the sector name, then "Sector" (0x7c) (§16.1); campaign text `<lang>\campaign.str` (`0x40fd2d`) in `[0x48d8fc]` | first visit while `0x477946` = 0: `c<group>.avi` (0x46+group−1), theater movie (0x4b+group−1) |
| 1 | "Mission Map" (0xb3); texts from `data\mission.str` (`0x4135bb`): briefing `0x46fc5c` (30 references at list-4 record +0x16) in `[0x48d900]`, objectives `0x46fc60` (10 at +2) in `[0x48d908]`, intelligence `0x46fc64` (30 at +0x52) in `[0x48d90c]` | once per `0x477947`: briefing movie = list-4 word +0x8e (`0x48419c`, `0x4134f2`) |
| 4 | "Mission Report" (0xb9); debrief text `0x46fc68` (`0x4139f6`) in `[0x48d904]`; statistics (Mission Outcome, Salvage/Weapons Recovered, Kills/Losses for You/Squad: 0x145..0x152; §14, §16.3) | once per `0x477948`: debrief movie `0x48419e` ([debrief selection](vshell_preparation.md#debrief-selection)) |

| Button (ID) | Handler | Effect (L) |
| --- | --- | --- |
| Mission Briefing (0xb4) | `0x445a51` | text panel = page (1 briefing, 4 debrief) (`0x445343`) |
| Mission Objectives (0xb6) | `0x445ac7` | panel 2 |
| Intelligence Report (0xb7) | `0x445b30` | panel 3 |
| Telecomm panel | `0x4454b8` | inert: no movie, no state change (§14.4) |
| Rock & Roll > (0xb8) | `0x445b99` | launch checks, then `0x40f68c`, exit 2, quit flag |

Launch checks of Rock & Roll, in order (L; details and own code §11.1),
each failure shows the warning box `0x44d90c(n)` and stays:

1. player bay `0x482b82` = −1 → n = 2 "You have no herc assignment" (0x140);
2. `0x4110c1`: bay record present, build progress +0x4a = 100 (`0x411088`) and
   functional (`0x411ca5`: family-1 condition words 0, 1, 5, 8 all > 50) → else
   n = 0 "Your herc is not functional" (0x13c);
3. `0x411135` → `0x411d10`: some slot item ID < 29 → else n = 1 "unarmed"
   (0x13e);
4. `0x40fc7e`: for orders 1..team capacity−1, every active selected pilot's
   Herc passes 3 → else n = 3 "One or more hercs of your squad is unarmed"
   (0x142).

The screen: §33; the texts §16; the statistics §14; the map §34.

### 4.9 After a mission (results path `0x40f09f`, entry `-X3`/`-X4`)

UI branches of the results path (L, `0x40f475..0x40f685`), arithmetic in
[results path](vshell_preparation.md#results-path):

| Outcome | UI |
| --- | --- |
| player killed (final word 3) | Replay box `0x44d0e7(3)` "You were killed…" |
| mode ≠ 1 (practice, instant action) | main menu |
| advancement 1 (end of campaign) | final word 1, save slot 10, `victory.avi` + `credits.avi`, main menu |
| advancement 0 | final word 0, Replay box `0x44d0e7(0)` "The war is lost…" |
| advancement 2 | page `0x481108` := 4 (`0x40f62d`), `0x413305` → DEBUG dialog auto click (§4.11): next CAREER mission prepared, service bay, mission screen page 4 (debrief) |

Replay box (`0x44cdac`, §11.2): **Yes** `0x44d1d4` loads slot 10 (pre-mission
state and launch files, since slot 10 was saved when VSHELL exited for
DBSIM), exit 2, quit; **No** `0x44d24d` saves the post-mission state to slot
10 and shows the main menu. Replay does not rewrite `data\mission.var` (L: no
`0x40f68c` on that path), so DBSIM gets the `mission.var` it wrote itself
(**H:** harmless if DBSIM rewrites every variable it reads; not checked).

Own code: `es2_shell_flow_results` (`flow.c`), the host's results path
(`host.c`), `es2_shell_results` (`campaign.c`).

### 4.10 Boxes

| Box | Constructor | Shown by | Buttons (L) |
| --- | --- | --- | --- |
| Quit (0x12) | `0x431fa8` | main window procedure `0x405289`, key handler `0x409c6c` | ACCEPT `0x432237` quit flag; CANCEL `0x4321c1` hide |
| CD prompt (0x153) | `0x4322a8` | movie player `0x41ede0` | Continue `0x4324fa`; Quit `0x432569` quit flag (§20) |
| End of game (0x12e) | `0x44d2bc` | CONTINUE GAME with final word ≠ 2 (`0x44d55f`: 0 "war is lost" 0x130, 1 "cybrids were defeated" 0x131, 3 "killed" 0x12f; fixed second line 0x132; §11.3) | OKAY `0x44d60b` hide |
| Replay mission (0x134) | `0x44cdac` | results path | Yes `0x44d1d4`, No `0x44d24d` (§4.9, §11.2) |
| Launch warning (0x13b) | `0x44d66c` | Rock & Roll | OKAY `0x44da94` hide (§11.1) |

The boxes' shows, keys and repaint: §22. Own code: `es2_shell_flow_box`,
`es2_shell_flow_box_close` (`flow.c`), `shell/src/gui/boxes.c`.

### 4.11 The DEBUG mission-file dialog (`0x44dd38`)

Title "DEBUG", label "Enter Mission File Name", edit field `[0x48dcf4]`,
"Default:" and the default name `[0x48dcec]`, buttons Use Default
`[0x48dcfc]` (`0x44dbea`) and ACCEPT (`0x44dc4d`). **This dialog is the
mission-start mechanism** (L):

- `0x413305` (after the results path, `0x40f647`, and at the end of every new
  game, `0x41308e`) shows it (`0x44e1b5`) and, unless the cheat byte
  `0x482368` (`-@`) is set, posts two synthetic click events on Use Default
  (`0x41335d..0x4133d0`). The posted clicks run in the same event drain,
  after the calling handler returns (§15.1, §39).
- Use Default → `0x44db5c`: prepares the CAREER entry at (group `0x46fc58`,
  position `0x46fc5a`) (`0x41cd61`, [preparation](vshell_preparation.md)).
  Mode 1: map object `0x40e75f`, briefing texts `0x4135bb`, slot-10
  statistics, service bay, mission screen `0x43aee7(0x481108, 1)`. Mode 0:
  `0x40f68c`, exit 2, quit flag (practice and instant action never show the
  bay).
- ACCEPT → `0x44dc4d`: prepares `msn\<typed text>.msn` (format `0x44dc75`,
  the first `^` becomes `_`, §15.6), then the same follow-up as Use Default:
  mode 0 `0x40f68c` + exit 2 + quit flag, mode 1 the bay (§39).

With `-@` the dialog stays open and the typed name is used. This explains the
live assertion `msn\ASASASAS.msn` recorded in
[launcher.md](launcher.md#-sprunknown-debugcheat-mode)
(**H:** key presses reached the edit field); the two queued objects of that
section are the clicks on Use Default. Instant action (§4.1) posts the clicks
inside its handler; they run in the same drain before the loop ends, so the
preparation and a second `0x40f68c` run before the exit (§15.1, §15.3).

Own code: §15.6 and §39 (`es2_shell_flow_debug`,
`es2_shell_flow_debug_answer`, the box in `boxes.c`).

## 5. Which screen writes which field

SAV field names as in [mission.md](../formats/mission.md#sav-field-values)
and the `shell/` API. W = writes, R = only reads (L unless marked).

| Field | Written by (UI) | Only read by |
| --- | --- | --- |
| CAREER position (selection words 0/1) | new game `0x413053`; practice entry (via `0x479c54`); results advancement | Use Default preparation, mission screen, bay background |
| Briefing selection (148 bytes) | preparation `0x4134f2` (list-4 record) | mission screen pages 1/4 |
| Variable bank | new game clear `0x40eef1`; results path; preparation (words 1..6) | — |
| Roster, roster selection | new game; results replacement `0x410173`; CREW | squad panel, crew lists |
| Team words, player record | new game (name, skill → rank); CREW (bay, order, activation); results (promotion, `0x40ec80` hooks) | Rock & Roll checks, MEC writer |
| Hangar records | WEAPONS (mount), REPAIR (conditions), BUILD (new record), Herc scrap, results (losses, repair, construction) | squad panel, Rock & Roll checks, MEC |
| Inventory lists | WEAPONS (mount/unmount), weapons scrap, results (salvage items, builds) | WEAPONS list, ARMORY |
| 33 availability flags | new game (WEAPONS.DAT, `0x4125e8`); results unlocks `0x412ce2`. No hangar screen writes them (our `es2loadout` sets them for mounted IDs, which the UI never does) | WEAPONS list refresh (`0x44025b`) |
| Five-entry build bank | ARMORY (manual), `0x412a37` on leaving WEAPONS/REPAIR/BUILD and after weapons scrap, results | ARMORY |
| Nine-word bank | results `0x411ee9` | BUILD (`0x446ece`, `0x4471a8`), repair |
| Salvage total `0x482bd8` (SAV "tally") | new game (100,000 + 1,000·draw); REPAIR; BUILD; Herc/weapons scrap; results | saved-games statistics, REPAIR/BUILD/ARMORY "Salvage Available" (**H:** label) |
| Final word `0x4826f2` | new game (2); results (0/1/3) | CONTINUE GAME |
| Slot name/flag, `sav\gamefile.str` | Saved games ACCEPT, every save | Saved games list |
| prefs.cfg +0..+2, +4, +6 | Preferences; ONLINE MANUAL (+6 := 0) | DBSIM |
| prefs.cfg +0x25..+0x29, +0x2e | Practice; INSTANT ACTION (+0x28 via `0x44c40c`, +0x2e) | preparation (mode 0) |
| prefs.cfg +0x2a, +0x2c, +0x2d | main menu entries; Preferences ([shell options](vshell_preparation.md#shell-options)) | results path, REPAIR, ARMORY |
| `data\script.dat`, `data\MISSION.STR` | preparation (Use Default / ACCEPT); debrief rewrites MISSION.STR; every load copies script/STR/MEC back | map object, mission screen texts |
| `data\player.mec`, `data\mission.var` | `0x40f68c`: Rock & Roll, mode-0 Use Default, INSTANT ACTION, inside the preparation (`0x41d058`); load copies `player.mec` only | — |
| `sav\script%d.dat`, `sav\missn%d.str`, `sav\player%d.mec` | every save (`0x413095`) | every load |

## 6. Transitions to and from DBSIM

| Exit code | Set at (L) | Launch files then in `data\` |
| --- | --- | --- |
| 2 | Rock & Roll `0x445c21`; Use Default in mode 0 `0x44dbdc`; DEBUG ACCEPT `0x44dce2`; INSTANT ACTION `0x4319b1`; Replay Yes `0x44d1f7` | prepared four files (Replay: script/STR/MEC from slot 10, `mission.var` from DBSIM) |
| 5 | VIEW DEMO `0x431c24` | DBSIM `-D` demo; returns 6 |
| 0 | reset at start-up (`0x401bce`) and left unchanged by QUIT, the Quit box and the CD prompt's Quit, which only set the quit flag | — |

Every exit with a loaded game saves slot 10 (11 in mode 0) first (§1), so the
SAV that the restarted `-X3` VSHELL loads is the state at Rock & Roll,
including copies of the launch files. The next start is decided by ES.EXE
([launcher.md](launcher.md#start-loop)).


## 7. Coverage by the own shell

Each part of VSHELL and where the own shell implements it. "Library" parts
are compared with VSHELL's own code in vitro (V) unless marked; the screens
are drawn by the toolkit and compared with VSHELL's captured frames where §41
and §42 say so.

| Part | Sections | Own code |
|---|---|---|
| Launcher contract: exit codes 2/5/0, `-X` dispatch, intros | §2, §6, §15.5, §18, §19 | `launcher/` (`ES2_LAUNCH_SHELL`), `flow.c`, `host.c`; the process frame in vshell_process.md and vshell_presentation.md |
| Mission start: Use Default, launch checks, launch files | §4.11, §10.3, §11.1, §39 | `es2_shell_prepare`, `es2_shell_launch_check`, `es2_shell_launch_files`, `es2_shell_engine_prepare_mission` |
| Results, debrief, Replay and End of game boxes | §4.9, §11.2, §11.3, §14, §16 | `es2_shell_results`, `es2_shell_debrief_select`, `es2_shell_flow_results`; launcher.md "Shell state engine" (the practice return) |
| Save slots and slot statistics | §9, §13 | `slots.h`, `es2_shell_stats_read`/`_refresh`/`_show`; screen `saved_games.c` (§26) |
| New career (both modes) | §9.4, §15.4 | `es2_shell_new_game`, `es2_shell_practice_new_game`; screen `registration.c` (§24) |
| Main menu, ONLINE MANUAL | §4.1, §21, §23 | `flow.c`, `shell/src/win/help.c`; screen `main_menu.c` |
| Practice and instant action | §15 | `practice.h`; screen `practice_screen.c` (§25) |
| Service-bay editors: mount, REPAIR, BUILD, Herc scrap, CREW, ARMORY, weapons scrap | §10, §12, §17 | `hangar.h`, `editors.h`, `campaign.h`; screens §28 to §32, squad pictures §36 |
| Mission screen: texts, page-4 widgets, map, movies, CD prompt | §14, §16, §19, §20, §33, §34 | `texts.h`, `movies.h`, `mission.c`, `mission_map.c`, `mci_player.c` |
| Preferences | §11.4, §27 | `es2_shell_prefs_session_open` … `_accept`/`_cancel`; screen `preferences.c` |
| Fonts, images, palettes, widgets, layout table, boxes and keys, sounds, global state, RNG | §22, §40 | vshell_widgets.md, audio.md, vshell_state.md, `boxes.c`, `events.c` |

## 8. Open items

The open points of every section, in one list; the section holds the
detail.

**VSHELL behaviour not settled**

- DBSIM's use of prefs bytes `+0` and `+1` (VSHELL's side is §11.4: the
  gates of the music ramp and of the effects) is not observed live.
- Whether DBSIM honours the guidance word (§10.4).
- CONTINUE with an unflagged slot 10: the final word 0 after a fresh start
  is **H**, not observed (§9.2).
- Replay does not rewrite `mission.var`; harmless is **H** (§4.9).
- The war-lost Replay (advancement 0) is not run through the menu (§11).
- The kill pairing of the slot panel (§13.4, §26.5) and the visibility of
  the page-4 statistics after leaving and returning (§14.2) are **H**.
- Enter over ACCEPT in the Quit box (§22.4) is **H**.
- Whether every button class ignores a press with `+0x49` = 0 (§12.8).
- Which picture the results path leaves for the outcomes no slot,
  rejected and other advance values; whether `0x4133eb` returns anything
  but 0..2 (§23.6).
- The role of prefs byte `0x2f` (§23.6, §35).
- `0x477944` has no reader in the listing (§33.7).
- Whether the data error of `0x414a3c` is reachable (§36.7).
- Which input VSHELL takes the language from when `LANGUAGE.CFG` and
  `-G` disagree (§41.2).
- A click during the fly-over: only VSHELL's skip is read from a capture
  (§42.4).

**Not run in vitro**

- The load's own refresh `0x40ebff` and a failed open in `0x4309f0`
  (§13.6); the REPAIR show's bay replacement and the bay selectors
  (§12.8); the `0x43dcdd` WEAPONS case's `0x43fdac(0)` (§17.8).
- The screens' handlers and window effects (§22.8, §27.6); the mode-1
  follow-up of the DEBUG dialog (§39.6); the modifier hot keys (§40.6).
- Steps 6, 7 and §19.4 of the movie path (MCI, the CD prompt, the intro
  error) are L only (§19.6); §20 is not checked in vitro or live.

**Not compared with VSHELL's frames**

- Compared (V, §41): main menu after CONTINUE, the service bay, the
  shown Preferences and Saved games, Registration open and start, the
  WEAPONS frame with squad panel and Herc picture, mission page 0, page 1
  and its three texts. Every other state listed in §22.7, §23.5, §24.7,
  §25.4, §26.4, §27.5, §28.6, §29.4, §30.5, §31.5, §32.6, §33.6 and §36.6
  is not compared; nor are the palette-1 condition of MAIN MENU and SAVE
  (§28.7), ARMORY's label clip (§38), the other `main_menu` snaps of the
  tour and the frame after the movies with movies on (§41.4), and the
  per-rectangle present counts other than those of the second MISSION's
  tab bar, map area and full map frame (§42.4).

**Manual checks (live)**

- Movie playback, the skip by click, Esc and Space, the restart and the
  picture's position in full screen (§19.6, §37.4).
- ONLINE MANUAL on a Windows 10/11 machine and WinHelp over a full-screen
  display (§21.5).
- The windowed hold of the fly-over's first paint (§42.4).

**Own deviations kept**

- The text menu's practice RNG starts from the start-up seed (§15.8); the
  return of practice and instant action is the shell state engine's
  (§15.8).
- The text menu's Full Screen shows the Alert text and stores byte 6 := 1
  (§11.5, §27.6).
- es2shell logs the data error of `0x414a3c` and goes on (§36.5).
- es2shell's movie loop leaves no words in the key ring (§37.2).
- Modifier hot keys give no event without the executable's bytes (§40.4).

## 9. Save slots and new career

**L** from the listing; **V** from the shell-invitro slot mode, which runs
VSHELL's own save, load and new-career functions in the mapped original
([shell_invitro.md](../reference-game/shell_invitro.md#slot-mode)).
Own code: `shell/include/es2/shell/slots.h` (`shell/src/slots.c`),
`es2_shell_loaded_save` / `es2_shell_prepared_save` / `es2_shell_new_game`
(`es2/shell/save.h`, `shell/src/save.c`); the text menu that uses them:
[launcher.md "Saved games"](launcher.md#saved-games).
Regression: `//shell:slots_real_test`, `//shell:menu_real_test`.

### 9.1 Slot table

The table at `0x4826f4` holds twelve records of `0x5e` bytes: file name (13
bytes), text (80 bytes at `+0x0d`), flag (`+0x5d`). Slots 0..9 are the user
slots, 10 `GAME_R.SAV`, 11 `GAME_T.SAV`.

- **Reader `0x40e380`** (game init `0x40e736`, once per start; L): per group a
  count word and that many entries (length word, the raw bytes into the
  record, one tag byte; group 0's tag is dropped, group 1's is the flag). A
  text whose fifth byte is zero gets ESTEXT entry 0x21 appended with
  `strcat` (`0x40e482..0x40e4ab`): "LEER", "EMPTY", "VIDE" in the three
  catalogues. No bound checks (a count above 12 or a long string overruns the
  records). A text that is still shorter than five bytes gets the entry again
  at every start.
- **`0x40e6cd(slot, text)`** (L): for slot < 10 `strcpy(text)` into the
  record, then flag := 1, then the writer `0x40e503`
  ([mission.md](../formats/mission.md#sav-editing-and-building), GAMEFILE
  paragraph: u32 size, both groups with twelve entries each, non-truncating,
  size = file length - 4). V: a NULL text for a slot below 10 faults in
  `strcpy`; the UI never passes one.
- V: the CD's table (278 bytes, empty texts) is held in memory as
  " 1. LEER" … "10. LEER" (German), so the first save of any slot writes 318
  bytes. Saving slots 10 and 11, then slot 0 as " 1. S1" and slot 1 as
  " 2. Z1" reproduces the measurement clones' table byte for byte, including
  its four-byte stale tail
  ([mission.md](../formats/mission.md#sav-editing-and-building)).

Own code: `es2_shell_slots_load`, `es2_shell_slots_mark`,
`es2_shell_slots_write`, `es2_shell_slot_gamefile`.

### 9.2 Save `0x40e933(slot, text)` and load `0x40eaaa(slot, flag)`

- Save (L, V): nothing while `0x4826ee` = 0; slot 10 becomes 11 when the mode
  word `0x4826f0` is 0; the slot table update above; the SAV through
  `0x44e9d8` (open `0x8102`, no truncation) at `"sav\" + file name`
  (`0x40e708`); the writers; then `0x413095` copies `data\script.dat`,
  `data\mission.str`, `data\player.mec` to `sav\script%d.dat`,
  `sav\missn%d.str`, `sav\player%d.mec` with `0x40da8d` (destination open
  `0x8301`, truncating; a missing source stops at the assertion in
  `src\fileutil.cpp`, V).
- Load (L, V): slot 10 becomes 11 in mode 0; a slot whose flag is 0 loads
  nothing (`0x40eae3`); otherwise the readers, `0x4131e3` copies the three
  companions back to `data\` and, with flag 1, composes the briefing texts
  from the copied `MISSION.STR` (`0x4135bb`); loaded flag := 1; in mode 1 the
  slot statistics are refreshed in memory (`0x430ec4`); the map object is
  rebuilt (`0x40e75f`).
- **The inventory reader `0x412781` prepends every record it reads** (B2,
  vshell_preparation.md), so after a load each of the 33 item lists is in
  reverse file order and the next save writes it reversed. V: load +
  original writers = `es2_shell_loaded_save` for GAME_R, GAME_T, a new career
  and a results-path SAV whose lists hold distinct records. A SAV written
  after a results path is therefore not a load/save fixed point; GAME_R and
  GAME_T are (their lists hold equal records).
- CONTINUE GAME `0x431a73` (L): `0x40eaaa(10, 1)`, then the final-word test on
  memory. With the flag of slot 10 at 0 (the CD's table) nothing is loaded and
  the End of game box shows the final word in memory (**H:** 0 after a fresh
  start, "The war is lost"; not observed).

Own code: `es2_shell_slot_effective` (10 → 11 in mode 0),
`es2_shell_slot_companion` (the three companion names),
`es2_shell_loaded_save`, `es2_shell_restore_save`.

### 9.3 Saved games screen (`0x438c40`)

- Rows (L): ten edit fields (`0x40c1a4`, text at `+0x45`). A row shows the
  slot text (`0x4381cd(slot, NULL)`) or, while naming, `"%2d. %s"` with
  slot + 1 and the typed name. The edit set `0x475869` is `^`, digits,
  letters and space (`0x40c237`); a character is taken while length + 1 <
  0x5a and text width + character width + 6 < row width (`0x40c382..0x40c3c7`:
  a pixel limit that depends on the font). Backspace keeps the
  four-character prefix (`+0xb7` := 4 at `0x438f95`).
- Selecting a row (`0x437fef`, L): SAVE is enabled when not naming, the slot
  is below 10 and a game is loaded (`0x43807b`); RESTORE when the slot is
  below 10 and its flag is set (`0x438126`).
- SAVE `0x438263` starts naming (`0x437e62`); ACCEPT `0x43868a` calls
  `0x40e933(slot, row text)` (`0x438239`), then `0x430ec4`; CANCEL `0x4384aa`
  restores the row text; RESTORE `0x438393` calls `0x40eaaa(slot, 1)`,
  `0x40e933(10, NULL)`, clears `0x477946` and shows the service bay; EXIT
  `0x438424` returns to the caller.
- **No delete** (L): the constructor builds SAVE (0x1d), RESTORE (0x1e), EXIT
  (0x1f), ACCEPT (0x34), CANCEL (0x33); no handler removes a file, and the
  only writer of a slot flag is `0x40e6cd` (value 1). The own shell has no
  delete either.
- Statistics (L): at start-up `0x4309f0` reads every slot's SAV into the
  records at `0x48cebc + 0x43·slot` (§13); `0x430ec4` copies the memory state
  after a save or load. Display only, no file.

Own code: `es2_shell_slot_row`, `es2_shell_slot_row_typed` (VSHELL's width
rule; `ES2_SHELL_SLOT_NAME_MAX` is the fallback without a font); the screen
§26.

### 9.4 New career `0x40e8a5` and the state after a preparation

- V: `es2_shell_new_game` equals `0x40e8a5` in mode 1 (SAV from the original
  writers, RNG state, salvage, final word 2, position (1, 0), loaded flag, one
  call of the DEBUG dialog `0x413305`) for names of 1 to 29 characters, ranks
  0..3, seeds 0..127 and the three languages (slots_real_test, 6 cases).
  Mode 0 (practice, instant action) is `es2_shell_practice_new_game` (§15.4):
  `0x410b11` builds no HERCS hangar there.
- V: `0x40e8a5` leaves the briefing selection (`0x4840cc..`, 148 bytes), the
  nine-word bank and the 20 bytes at `0x4833ac` as they were (§15.4); a new
  career after an earlier load in the same VSHELL process carries that game's
  selection (139 differing bytes after GAME_R, none after GAME_T) until the
  preparation that follows overwrites it.
- A mode-1 preparation (L, V) changes the SAV state in exactly four places:
  the CAREER selection (as prepared); the variable bank, which is the
  `mission.var` it writes (`0x40ef83` writes the bank itself); the briefing
  selection, derived by `0x4134f2` (called at `0x41ce89` with a copy of the
  first retained list-4 record) as three lists (10 words at record `+2`, 30
  at `+0x16`, 30 at `+0x52`), each stored after its count of entries other
  than -1, then the word at `+0x8e`; and the team capacity word with the
  selected pilots' activation (`0x410923` → `0x41098a`). V:
  `es2_shell_prepared_save` equals the original preparation followed by the
  writers in 300 cases (50 campaign missions × GAME_R, GAME_T and a new
  career × seeds 0 and 71) and in the 82 cases of slots_real_test, which also
  covers a results-path SAV.

Consequence for the own shell: a save writes VSHELL's memory state, which is
the prepared state after a new career or a results path, and the reversed
lists after a load; the results path starts from slot 10 as written at the
exit for DBSIM. The own shell keeps its campaign SAV equal to that memory
state.

## 10. Service bay and WEAPONS

**L** from the listing; **V** from the shell-invitro service-bay operations,
which run VSHELL's own functions in the mapped original
([shell_invitro.md](../reference-game/shell_invitro.md#service-bay-operations)).
Own code: `shell/include/es2/shell/hangar.h` (`es2_shell_service_bay`, in
`shell/src/campaign.c`) and `es2_shell_launch_files`
(`shell/include/es2/shell/prepare.h`); the text menu screens are
[launcher.md "Service bay and WEAPONS"](launcher.md#service-bay-and-weapons);
the graphical screens §28 and §29. Regression: `//shell:bay_real_test`,
`//shell:menu_real_test`, `//shell:campaign_test`.

### 10.1 Bay buttons and the screen word

- The bay is shown by `0x43b7f2(8)` + `0x43b758`, which stores −1 in the
  screen word `0x4758b8` (`0x43b7e8`). Every bar button first compares the
  screen word with its own screen and does nothing when they are equal
  (WEAPONS `0x43a8ce`, REPAIR `0x43a9ce`, MISSION `0x43aded`, SAVE `0x43a7d4`);
  otherwise it leaves the current screen once (`0x43a537`) and stores its
  word. Pressing WEAPONS while WEAPONS is shown therefore does not run
  `0x412a37`. MAIN MENU (`0x43a66c`, skipped while the word is 0): leave,
  hide the bay, `0x40e933(10, 0)` (`0x43a705`), main menu, word 0. SAVE
  (`0x43a768`) opens Saved games without a mode store. MISSION (`0x43ad5a`)
  leaves through `0x43aee7` only (one leave). (L)
- Leaving WEAPONS (`0x43a537`, word 2): `0x43d779(2)` and `0x43fc62` (UI
  only), then `0x412a37`: option byte `0x4825c9` (prefs.cfg +0x2d) 0 →
  `0x412965` (refill the five-entry bank by priority), else `0x4129de` (drop
  entries the salvage cannot pay). (L; V for the operation, not the button)

Own code: the flow's screen word (`ES2_FLOW_WORD_*`, `flow.c`); the bar's
screen side §28.3.

### 10.2 WEAPONS

- Show `0x43fbd8`: the bay selection `0x482bc9` is kept when its bay holds a
  record (`0x411088`), else `0x41124e` picks one (`0x43dcdd`); the slot
  selection `0x476658` becomes −1; `0x43fdac(0)` then mounts nothing (no
  slot). (L)
- Slot prev/next `0x43e3d9`/`0x43e399`: the slot selection cycles modulo the
  record's capacity (+0x4c). (L)
- List refresh `0x440256`, per row of the 27 IDs at `0x476a4c`: the count
  text (+0x17), then the row is **disabled** when the availability byte
  (+0x16) is zero (`0x4402e7..0x44033c`), else enabled when `0x41501f(item)`
  accepts it and greyed **and disabled** otherwise (`0x4403a3..0x4403e3`).
  `0x41501f`: true without a selected slot; else the ARM table of the bay
  record's **vehicle word** (+0, `0x411164`; tables indexed by it × `0xc6`
  from `0x484702`) must hold a record for (item, slot + 2) (`0x4142f0`) and
  the availability byte must be nonzero. (L; V: 2,277 answers of `0x41501f`
  for every vehicle, slot and item equal the own ARM rule)
- A row click (`0x440990` and the following row handlers) sets `0x476df4`
  := 1 around `0x43fdac(row)`; only with that flag does `0x43e2d4` mount, so
  the description button `0x4407ba` (`0x43fdac` without the flag) never
  mounts. `0x43fdac` with a slot selected calls `0x43e2d4(item)` when the
  item is 0, **or the slot item's word +8 (5 for an empty slot) equals the
  item ID** (`0x43fe39..0x43fe56`; compares the guidance word with an ID,
  sic), or the item's list count is not zero, or the slot already holds that
  ID; otherwise nothing. `0x43e2d4` mounts only when the bay has a record.
  (L; UI code, not run in vitro: the rule is unit-tested in `campaign_test`)
- Mount `0x411b10(record, slot, item)`: a slot item goes back to the head of
  its ID's list (`0x412521` prepends) and the occupied count +0x4e drops;
  item 0 empties the slot and sets condition word 23 + slot to 100
  (`0x4122e1(rec + 8, 2, slot, 100)`); any other item takes the head of its
  list (`0x4124eb`), and if there was one, condition word 23 + slot := its
  word +4, count + 1, word +8 := 1 for IDs 13..15, 5 otherwise; an empty list
  leaves the slot empty and the condition word as it was. (L, V)
- After a click on rows 13..16 the guidance panel is shown (`0x43fc87`) and
  `0x4403f9(slot item's word +8, 0)` re-stores the same value (no change).
  The buttons call `0x4403f9(mode, 1)`, which stores the mode in `0x476df8`
  and `0x476dfc` and calls `0x43e346(mode)`: the selected slot's item word
  +8 := mode. Modes: SARH 0 (`0x4406f0`), ARH 1 (`0x44068b`), ARM 2
  (`0x440626`), EO 3 (`0x440755`); their texts are `wpn_desc.bin` entries
  0x63 + 3 × mode, the row texts entries 3 × item. So the "secondary" word
  of a mounted item is its missile guidance mode, 1 (ARH) after a mount of
  a 6/8/10 rack, 5 (no mode) for everything else. (L, V)

Own code: `es2_shell_service_bay` (mount, guidance, rebalance),
`es2_shell_mount_secondary`, `es2_shell_arm_parse`; the screen §29.

### 10.3 Launch files at Rock & Roll

`0x40f68c` (L, V): `0x40ef83` writes `data\mission.var` = the 2,000-byte bank;
then `data\player.mec`: word 0, the team count word (`0x482b5e`), the
player's variant and rank and his bay's record (`0x410cdb`), then for orders
1 .. capacity − 1 (`0x482b5c`) the selected pilot with that order
(`0x4108fa`) if active (+0x24): variant, rank, its bay's record; then 33 and
the 33 availability bytes (`0x412877`). No activation pass (that is
`0x410923` inside the preparation). V: for the SAV after each of 57
preparations (all 50 campaign missions on a new career, every seventh on
GAME_R) `es2_shell_launch_files` equals the preparation's `player.mec` and
`mission.var`, and the original `0x40f68c` for 8 of them and for 30 states
after service-bay changes.

### 10.4 Verification of the bay operations

V (`//shell:bay_real_test`): 74 sequences of clicks (with the own click rule
and ARM answer), mounts (all IDs, including lists that run empty and item 0),
guidance modes and rebalance with options 0, 1 and 7, on GAME_R, GAME_T, a
new career, a results-path SAV and three prepared states, give the SAV the
original writers emit after the same `0x411b10`/`0x43e346`/`0x412a37` calls,
byte for byte (356 mounts, 68 from empty lists, 29 with item 0, 97 guidance
stores, 62 rebalances, 8 of them changing the bank). The text menu's
sessions (`//shell:menu_real_test`) write `SAV` and `DATA` files equal to
the original for the same choices (launcher.md).

Open: whether DBSIM honours the guidance word (a live question; launcher.md
has the request that shows the loadout in a mission).

## 11. Launch checks, Replay and End of game boxes, preferences blocks

**L** from the listing; **V** from the shell-invitro slot mode running
VSHELL's own handlers and functions in the mapped original (the operations
are listed in `tools/reference-game/shell-invitro/README.md` and at
`run_slots` in its `main.c`). Own code: `es2_shell_launch_check` and
`es2_shell_prefs_session` (`shell/include/es2/shell/campaign.h`, in
`shell/src/campaign.c`); the text menu `shell/src/menu.c`; the flow's boxes
(`flow.c`). Regression: `//shell:launch_real_test`,
`//shell:menu_real_test`, `//shell:campaign_test`.

### 11.1 Launch checks of Rock & Roll (`0x445b99`)

- Order (L): the player's bay word `0x482b82` = −1 → box 2; `0x4110c1`
  fails → box 0; `0x411135` fails → box 1; `0x40fc7e` fails → box 3;
  otherwise `0x40f68c`, exit code 2 (`0x408d1a` stores `0x46e348`) and the
  quit flag. The first failing rule decides; the later ones are not called.
- Memory record of a bay (reader `0x4116e6`, L): +0 vehicle, +2 second word,
  +8 the 33 condition words, +0x4a build progress, +0x78 countdown, +0x4c
  capacity, +0x4e occupied count, +0x50 ten slot pointers. In the SAV these
  are the fixed bytes 0, 2, 4..69, 70, 72, 74 of the hangar record.
- `0x4110c1(hangar, bay)` (L): the record pointer is not null, its progress
  +0x4a is 100 (`0x411088`), and `0x411ca5`: the family-1 condition words 0,
  1, 5 and 8 (`0x41232a(rec + 8, 1, k)` reads rec + 0x22 + 2k, i.e. condition
  words 13, 14, 18 and 21) are each greater than 50 (signed). A player bay
  without a record is therefore "not functional", not "no Herc".
- `0x411135(hangar, bay)` → `0x411d10` (L): some of the **ten** slot
  pointers (not only those below the capacity) holds an item whose ID word is
  below 29 (signed). A slot record with ID 0 counts as armed.
- `0x40fc7e` (L): for the orders 1 .. capacity − 1 (team word `0x482b5c`,
  signed compare) `0x4108fa` gives the first roster group 0..2 whose selected
  pilot (pointer at team + 0x3f + 4g) has that order word (+0x27); none:
  next order. An active one (+0x24) needs `0x411135` for its bay (+0x22).
  **Trap:** an active pilot with bay −1 makes `0x411135` read the word before
  the hangar array, the third selected-pilot pointer `0x482ba3`, as a record
  (and ten dwords at +0x50 of a 0x3b-byte pilot). The own check refuses such
  a state (`ES2_ERR_OFFSET`) when Rock & Roll reaches rule 4; the shell does
  not launch it.
- Warning box `0x44d90c(n)` (L): title 0x13b, the two lines 0x13c + 2n and
  0x13d + 2n (0 "not functional", 1 "unarmed", 2 "no herc assignment", 3 "one
  or more hercs of your squad is unarmed"), OKAY `0x44da94` hides it.
- V (`//shell:launch_real_test`): the answers of `0x4110c1`, `0x411135` and
  `0x40fc7e` and the box sequence of the real handler `0x445b99` equal the
  own ones for 72 corpus states (GAME_R, GAME_T, a new career, the SAV after
  the C1_01 results path, the states after 57 campaign preparations, 11
  service-bay states with one Herc emptied) and for 599 of 600 seeded edits
  of them (player bay, progress, the four condition words and others, one or
  all slot item IDs 0/5/28..32, emptied records, capacity 0..4, the selected
  pilots' activity, order and bay), every box kind reached. The one other
  edit reaches the trap above (refused by the own check, not run in the
  original). The worker's state preflight bounds the capacity to 0..4.

### 11.2 Replay box (`0x44cdac`)

- Shown by the results path (L): player killed (player +0x2b, `0x482b8b`,
  is 0 at `0x40f475`; final word 3; in every mode) → `0x44d0e7(3)`;
  advancement 0 → final word 0, `0x44d0e7(0)`. Title 0x134; lines 0x135/0x136
  ("You were killed. Do you / want to replay the mission?") for 3,
  0x137/0x138 ("The war is lost. ...") for 0, no text otherwise; buttons Yes
  0x139 and No 0x13a. Victory (advancement 1) shows no box: final word 1,
  slot 10 saved, `victory.avi` and `credits.avi`, main menu.
- The box and `dbm\bay2a_84` behind it are drawn in palette 1, which the
  start-up `0x401d37` installed (the gate is already set in modes 3 and 4);
  the killed path selects no palette (vshell_presentation.md "Palette").
- Yes `0x44d1d4` (L, V): hide, `0x40eaaa(10, 0)` (no briefing texts; slot 11
  in mode 0), exit code 2, quit flag; the loop end saves slot 10 again. The
  load copies the slot's script/STR/MEC to `data\`; `data\mission.var` is the
  one DBSIM wrote.
- No `0x44d24d` (L, V): `0x40e933(10, 0)` (the state after the results
  path), hide, main menu `0x431718` unconditionally (`0x44d26e`), also when
  the presenter already counts the main menu as the screen on display.
  es2shell's Replay-No hook shows the main menu again
  (`shell/src/gui/app.c` `box_screen`); without it the frame stayed frozen
  after `-X3`/`-X4` and the menu took no clicks. Checks:
  `//shell:gui_route_real_test` part d (the recorded c1_01-success output with
  the player's condition word 22 := 0, Replay box, No, QUIT); the original's
  call is the oracle's `ui_calls` `[0x431718]` in `//shell:menu_real_test`
  part f.
- V (`//shell:menu_real_test`, part f): a mission in which the stand-in DBSIM
  kills the player (condition word 22 of his Herc 0, own `mission.var`):
  after Yes the SAV directory and the four DATA files equal the worker's
  `final:3`, `replay-yes`, `save:10` on the same files and state (exit code
  2, DBSIM's `mission.var` kept); DBSIM then gets the slot-10 copies; the
  replayed mission's results path starts from the loaded state. After No the
  SAV directory equals `final:3`, `replay-no`, `save:10`.

### 11.3 End of game box and CONTINUE (`0x431a73`)

- L: busy cursor, mode store `0x40ec56(1)` (prefs read-modify-write, §11.4),
  `0x40eaaa(10, 1)`, selected slot `0x474fde` := 10; then the **final word in
  memory**: 2 → service bay; otherwise `0x44d55f(final word)`: 0 → 0x130, 1 →
  0x131, 3 → 0x12f, any other value sets no text (the label keeps its
  previous one); title 0x12e, the fixed line 0x132 "Restore or start a new
  game.", OKAY 0x133 (`0x44d60b` hides). **Trap:** the caller pushes `ecx`
  after loading only `cx` (`0x431ac8`): the argument's high half is whatever
  ecx held (V); the callee reads the word.
- A slot 10 without flag loads nothing (`0x40eae3`): the final word is the
  one in memory (0 in the image until a game is created or loaded, so "The
  war is lost."), the loaded flag stays as it was and the exit saves nothing
  (V: `continue` on the CD's table records `0x44d55f(0)`, loaded flag 0, no
  SAV written). With a flagged slot 10 the game is loaded (V: `continue`
  after Replay No records `0x44d55f(3)`, loaded flag 1) and the exit rewrites
  slot 10 (V: equal to the text menu's files).

### 11.4 Preferences blocks

- Blocks (L): current `0x48259c` (54 bytes; every control writes it, every
  reader reads it, e.g. the results path's +0x2c/+0x2d), committed
  `0x4825d2` (54 bytes), per-byte callbacks `0x482608` (cleared by
  `0x40dc44`; their setter `0x40e0dd` has no caller, so no store has side
  effects).
- Start-up `0x40dc44` → `0x40dc5b`: current := 0, up to 54 bytes of
  `data\prefs.cfg` when it opens, committed := current.
- Store `0x40ddfe(i, v, call)`, toggle `0x40de35` (byte ^ 1), commit
  `0x40dd6e` (committed := current), revert `0x40ddb6` (current := committed,
  every byte that differs).
- Writer `0x40dd0a`: `data\prefs.cfg` opened with 0x8301 (`0x44f100`,
  truncating), the 54 bytes of the current block.
- Read-modify-write `0x40df90` → `0x40dfa3(count, indices)` (L, V): the image
  is a copy of the **current block** (`0x40dfe4`); only when the read stream
  opens are up to 54 file bytes read over its start and the listed bytes
  taken from the current block again (`0x40dfec..0x40e022`); the 54 bytes
  are written (truncating). A missing file gets the whole current block, a
  short file keeps the current block's bytes after its end. The main menu's
  mode store `0x40ec56` uses it for +0x2a. **Trap:** the comment at
  `es2_prefs_overlay` in formats/cfg.h ("uninitialised; here zero") does not
  describe VSHELL; the shell does not use that function.
- Controls of `0x435598` (L, V): Music check box `0x437351` → `0x436ed1(0)`:
  byte 0 toggled, the shell's WAV music ramped down (`0x42f7f0`) or up
  (`0x42f894`); Sound Effects check box `0x4373b2` → `0x436ed1(1)`: byte 1
  toggled; High Res `0x437758` / Low Res `0x4376f7` → byte 4 := 0 / 1;
  Window `0x437597` → display switched back when full screen, byte 6 := 0;
  Full Screen `0x437608` → the Alert box (0x126, 0x127) only when windowed,
  its ACCEPT `0x437678` switches and stores byte 6 := 1 (§22.2); repair radios
  `0x437413`/`0x437474`/`0x4374d5` → +0x2c := 0/1/2; weapons-building radios
  `0x437536`/`0x437696` → +0x2d := 0/1. The show `0x436d45` re-stores +0x2c,
  +0x2d, +4 and +6 with their own values. `0x436ed1` cases 2 and 3 cycle byte
  2, but only the two check boxes call it: no control changes byte 2.
- ACCEPT `0x4372e1`: commit, writer, hide, main menu. CANCEL `0x437220`:
  revert, display and music re-synchronised, hide, main menu, no write. The
  revert covers every byte stored since the last commit, also the mode byte
  +0x2a a main-menu entry stored (V: `mode:1`, a toggle, `cancel` returns
  +0x2a to the start-up value; a later ACCEPT writes it).
- V (`//shell:launch_real_test`): 40 seeded sequences (240 operations: every
  control, ACCEPT, CANCEL, the mode store) on synthetic files (missing, 10,
  54, 60 bytes): both blocks and the file equal the own session's.

Own code: `es2_shell_prefs_session_open`, `es2_shell_prefs_store`,
`es2_shell_prefs_control`, `es2_shell_prefs_accept`,
`es2_shell_prefs_cancel` (`campaign.c`); the screen §27.

### 11.5 The text menu's boxes and preferences

The text presenter (`shell/src/menu.c`, §18.3):

- Rock & Roll (the bay's MISSION and the main menu's shortcut): the checks
  on the campaign's current SAV; a failure shows WARNING! with the two lines
  and OKAY and stays (V, part f: an emptied Herc gives box 1 here and in the
  original handler).
- After a lost mission (results reason `stop-player-lost` or
  `stop-return-0`) the Replay box: Yes loads slot 10 (as RESTORE's load:
  DATA copies, the loaded state), the backend's `replay` command makes the
  played mission pending again as a new launched record (`replay_of`, its
  SAV the loaded state, the four DATA files as handed to DBSIM, the chain's
  RNG), exit 2, the exit save; No saves slot 10 and shows the main menu.
  Victory saves slot 10 at once (then the end text instead of the movies).
- CONTINUE shows the End of game box from the final word the menu holds for
  the process (0 at start, 2 after a new career, the slot's after a load,
  the results path's).
- Preferences: numbers 1..5 (repair, weapons building, display, Accept,
  Cancel) plus 6 Music and 7 Sound effects (check boxes), 8 Resolution; full
  screen prints the Alert text before it stores; all over
  `es2_shell_prefs_session` for the whole menu process.
- Deviations (not observable in the files): no display switch; an End of
  game final word outside 0/1/3 shows no line.

## 12. Service-bay editors: REPAIR, BUILD, Herc scrap

**L** from the listing; **V** from the shell-invitro slot mode with the
editor operations, which run VSHELL's own handlers and functions in the
mapped original (the operations are listed in
`tools/reference-game/shell-invitro/README.md` and at `run_slots` in its
`main.c`). Own code: `es2_shell_editors`
(`shell/include/es2/shell/editors.h`, in `shell/src/campaign.c`); the
screens §30 (REPAIR) and §31 (BUILD). ESTEXT texts are the English ones,
cited by ID. Regression: `//shell:editors_real_test`,
`//shell:editors_test`.

### 12.1 REPAIR rows and the condition words (the External/Internal mapping)

- Two row lists. Every row handler (`0x434745`, `0x4347aa`, … for tab 0
  rows 0..15; `0x434e30`, … for tab 1 rows 0..8) calls `0x434549(tab, row)`.
  Family and index of a row: `0x433aa0` (tab ≠ 0 → family 1; tab 0: row > 5
  → family 2, else 0) and `0x433ac1` (family 2: row − 6, else the row). (L)
- **External** (tab 0, label 0x3e): rows 0..5 are the six part groups
  (family 0; value = signed average of the group's words, `0x41224d`):
  Cockpit {0, 1}, Left Torso {2, 4}, Right Torso {3, 5}, Chassis {6}, Left
  Leg {7, 9, 11}, Right Leg {8, 10, 12} (labels 0x4e..0x53 from the word
  table `0x47409c`, groups from `0x46f9e4`). Rows 6..15 are hardpoints 0..9
  (family 2, condition word 23 + slot; label `0x7e + item ID` or "--Empty--"
  0x7d and the slot number; rows at or above the capacity get ESTEXT 1, a
  blank, `0x434042`). (L)
- **Internal** (tab 1, label 0x3f): rows 0..8 are condition words 13..21
  (family 1; labels 0x54..0x5c: Left Leg Servos, Right Leg Servos, Sensor
  Array, Targeting Computer, Shield Generator, Engine, Hydraulics,
  Stabilizers, Life Support). Vehicle 8 takes its labels from `0x4740ba`
  (Left/Right Nacelle, Fuselage, Left/Right Wing and their servos instead of
  torso, chassis and leg names) (`0x43436f`). (L)
- Word 22 (overall condition) and hardpoints at or above the capacity are no
  row; REPAIR never changes them alone. (L)
- The tab buttons change nothing the operations read: External `0x4346ec`
  has an empty body; Internal `0x434d95` only swaps the bay panels
  `[0x48d59c + 4·bay]` / `[0x48d1f8 + 4·bay]` while the selected row is on
  tab 0. The selection's tab is the clicked row's (`0x474094`). (L)
- Selection `0x434549(tab, row)`: nothing when (tab, row) equals the stored
  `0x474094`/`0x474098`; a hardpoint row only when its slot is below the
  selected bay's capacity and holds an item (`0x434585..0x434618`);
  otherwise tab, row, family and index are stored (`0x474094`, `0x474098`,
  `0x474096`, `0x47409a`) and the screen refreshed. All four words are 0 in
  the image, i.e. (0, 0) = Cockpit. (L, V: after every row click the four
  globals equal the own ones)

### 12.2 One repair: cost `0x411a78` and the new condition

- `0x411a78(rec, family, index)` → `0x413f8c(rec +0, family, index or the
  slot's item ID, c, 1)` with c = `0x41232a(rec + 8, family, index)`. Weight:
  `A[rec +0][index]` (family 0), `A[rec +0][13 + index]` (family 1) or the
  item value `V[item]` (family 2; tables of
  [vshell_preparation.md](vshell_preparation.md#runtime-tables)). **Trap:**
  the row is the **vehicle word +0**, as in `0x414174`, while REPAIR ALL's
  `0x41401b` uses +2. (L)
- `0x413e95(weight, c, 1)`: band b = `0x413e40(c)` (first threshold 90, 80,
  60, 30, 1, 0 that c reaches, else 0); cost = (threshold[b − 1] − c) ·
  weight / 100, for b = 0 (level[0] − c) · weight / 100: the cost of raising
  c to the next band limit (100, 90, 80, 60, 30 or 1). Signed 32-bit
  product, `cdq`/`idiv` by 100 (truncation). (L, V)
- New condition (`0x4351bd`): `0x413e5c(0x43e05f(c))`. `0x43e05f` gives the
  first i with c > limit[i] for the five words 89, 79, 59, 29, 0 at
  `0x47665c`, else 5; `0x413e5c(i)` is 100 for i = 0, else level[i] + 1
  (levels 100, 89, 79, 59, 29, 0 at `0x46feb8`). So 90..100 → 100, 80..89 →
  90, 60..79 → 80, 30..59 → 60, 1..29 → 30, 0 → 1. (L, V)
- Values outside 0..100 (L, V): a negative c is band 0 for the cost
  (`0x413e40` finds no threshold) but band 5 for the value, so it costs
  (100 − c) · weight / 100 and becomes 1; c above 100 gives a negative cost
  (salvage grows) and becomes 100.
- Family 0 writes the value into every word of the group (`0x41229c`);
  families 1 and 2 write one word (`0x4122e1`). (L)
- Handler order `0x4351bd`: cost (0 without a record), salvage −= cost
  (32-bit), c (100 without a record), the write (only with a record), row
  text, refresh. **Trap:** it pushes `ebx`/`esi` after loading only `bx`/`si`
  (`0x4351cf`, `0x4351d6`); the callees read words. (L)

### 12.3 REPAIR screen: show, bay, snapshot, REPAIR ALL, CANCEL, enable rules

- Bar button `0x43a962`: nothing while the screen word is 3; otherwise the
  current screen is left (`0x43a537`), the show `0x43397c` runs, then the
  word becomes 3 (`0x43aa0d`, after the show). (L)
- Show `0x43397c` (L): the mode text by prefs +0x2c (0 "Auto Repair" 0x42,
  1 "Manual Repair" 0x41, other values no text; the option changes nothing
  else on this screen: the only other readers of `0x4825c8` are the results
  path and Preferences); a bay selection that is −1, without a record or
  unfinished (`0x411088`: progress +0x4a = 100) is replaced by
  `0x43dcdd(0x41124e(hangar))` (the first bay with a finished Herc, −1 when
  none); refresh `0x43433f`; the snapshot `0x433f86`; `0x434549(0, 0)`.
  **Trap:** `0x43dcdd` runs the case of the **previous** screen word, since
  the word is stored after the show; for CREW (word 6) that case also checks
  `0x477778`, vehicle 8 and runs `0x4426e5` (§17.1).
- Snapshot `0x433f86`: `0x48d33c` := salvage; with a bay selection ≠ −1,
  `0x435547` copies the record's 33 condition words (66 bytes, +8..+0x49) to
  `0x48d340` (without a record it leaves its buffer unwritten and the caller
  copies stack garbage; not reachable, see below). (L, V)
- Bay selector `0x43dcdd`, screen word 3 (`0x43dd1c..0x43ddf2`): nothing for
  the selected bay, a bay without a record or an unfinished one; else the
  selection, `0x434549(0, 0)`, the labels, the refresh and **the snapshot
  again**. CANCEL therefore restores the state at the last bay change;
  repairs of a bay left before stay paid. (L, V)
- REPAIR ALL `0x4352e9`: salvage −= `0x41401b(rec, 100)` (0 without a
  record), then `0x4119d3(rec, 100)`: words 0..21 := 100, each slot below
  the capacity 100 (with or without an item); word 22 and slots above the
  capacity stay. No budget test in the handler. (L, V)
- CANCEL `0x435403`: salvage := `0x48d33c`; with a record, its 33 words :=
  `0x48d340`. Always enabled. (L, V)
- Enable rules, refresh `0x433ad5` (L; V: the original functions' answers
  before 289 applied and 5 disabled repairs and 104 + 9 REPAIR ALL equal the
  own ones, and the own operation applies exactly when the rule holds): with
  available = salvage − `0x412baa()` (the five-entry bank's cost, unsigned):
  REPAIR (`[0x48d2b0]`) when available ≥ the row's cost, REPAIR ALL
  (`[0x48d2c4]`) when available ≥ `0x41401b(rec, 100)`, both unsigned (a
  negative cost disables); SCRAP (`[0x48d2d0]`) as in §12.5. The flag is the
  widget's +0x49; the button code tests it before a press is handled
  (`0x40a4bb`, `0x40a4e9`, `0x40a542`, one button class). Every state change
  of the screen is followed by a refresh, so the flag always matches the
  state.
- **Trap:** without a finished Herc the selection is −1, and
  `[4·(−1) + 0x482ba7]` is the third selected-pilot pointer (`0x482ba3`),
  which REPAIR, REPAIR ALL, CANCEL, the scrap and a hardpoint row would use
  as a record. The own library refuses these (`ES2_ERR_OFFSET`); the
  External/Internal part rows, the show and leaving work.

Own code: `es2_shell_editors` operations OPEN, BAY, REPAIR_PICK, REPAIR,
REPAIR_ALL, REPAIR_CANCEL (§12.7); the screen §30.

### 12.4 BUILD

- Bar button `0x43aa62` as REPAIR's; show `0x446f9d`: rows 0..8 enabled when
  the nine-word bank entry of HERC_INF record i is not 0 (`0x446ec5`), then
  `0x4472cb(0)`: the selected type `0x478780` (−1 in the image) becomes 0
  **even when row 0 is disabled**. Leave `0x446fde` (UI) + `0x412a37`. (L; V:
  11 builds of type 0 after the show; the disabled row 0 only in
  `editors_test`)
- Type row `0x44764f`, `0x4476b0`, … → `0x4472cb(row)`: nothing for the
  selected type, else stored (`0x447376`). Bay selector in BUILD
  (`0x43df0b`): every bay. (L)
- Refresh `0x447064` (L, V as REPAIR's): a bay **without** record: Scrap
  disabled, Build enabled when available > HERC_INF[type] +8 · 1000
  (unsigned, **strict**, `0x4470ea`); a bay with a record: Build disabled,
  Scrap as §12.5.
- Build `0x4475ce` → `0x40eed4(type)`: salvage −= `0x410fa6(hangar, type)`
  (L; V: 18 builds, the SAV equal): a 0x7a-byte record into the selected bay
  (`0x410fa6` overwrites the pointer; the Build button is disabled for a bay
  with a record), hangar count + 1, constructor `0x41139e` (33 words 100,
  slots empty), then `0x41163d`: +0 := type, +2 := `0x411354(type)` (index
  of the type in the nine words at `0x46f868`; the CD's table is 0..8),
  capacity +0x4c := `0x411378(type)` (nine words at `0x46f87a`: 3, 5, 5, 8,
  9, 9, 10, 4, 7), progress +0x4a := 0, countdown +0x78 := HERC_INF[+2] +12;
  the cost HERC_INF[+2] +8 · 1000 is returned. The results path then
  advances the construction (`0x41104f`,
  [vshell_preparation.md](vshell_preparation.md#runtime-tables)).
- Texts `0x44738a`/`0x447501` (§31.1): HERC_INF +2 (with "TONS" 0xc6), +4
  ("KPH" 0xc7), +6, +8 ("TONS"), and (salvage − bank cost) / 1000 unsigned
  with "TONS". (L; the field meanings are **H**)

Own code: operations BUILD_TYPE and BUILD; the screen §31.1
(`shell/src/gui/build_armory.c`).

### 12.5 Herc scrap

- Scrap in REPAIR (`0x4353a5`) and in BUILD (`0x447570`) shows the warning
  `0x447da1`: "<`0x411a33`> tons of salvage." (0xcd) with `0x411a33` =
  `0x414174(rec)` / 1000. CANCEL `0x4482c8` hides it; ACCEPT `0x448326`:
  hide, `0x40ed0f`, squad panel; screen word 4: BUILD refresh; word 3:
  `0x43dcdd(0x41124e(hangar))` (new bay selection and snapshot, §12.3; −1
  when no finished Herc remains), REPAIR refresh. (L, V: 72 scraps, 38 in
  REPAIR with the selected bay equal to the original's `0x43dcdd` argument)
- Enabled (`0x433e50..0x433e9f`, `0x44717f..0x4471b2`): the selected bay has
  a record, **not** exactly one bay of the hangar is functional (`0x411101`
  counts `0x4110c1`, the launch check of §11.1, over bays 0..7: so the scrap
  is allowed with none or two functional Hercs), and the nine-word bank
  entry of the record's vehicle word +0 is not 0. (L, V)
- `0x40ed0f` (L, V): salvage += `0x410f46(hangar)`: for the selected bay's
  record `0x411a56` = `0x414174(rec)` (the lost-Herc value of
  vshell_preparation.md: for progress < 100 it is ((100 − progress) / 100) ·
  HERC_INF[+0] +8 · 1000 in integers, so the full price at progress 0 and
  nothing above; V: 8 unfinished), then `0x411db9` (items with condition ≥ 80
  back to the head of their list, the others freed), the record destroyed,
  hangar count − 1. Then `0x410844(team, bay)`: the player when his bay is
  that bay, else the first selected pilot (groups 0..2) with it; that
  pilot's bay := −1; `0x4108fa(team, its order word)` gives the **first**
  group whose selected pilot has that order (not necessarily the same
  pilot), and `0x41094b(team, g, 0)` deactivates it (team count `0x482b5e`
  − 1 when it was active). (V: 28 player bays, 8 count changes)

Own code: operation SCRAP (with ACCEPT); the warning §30.3.

### 12.6 Leaving and the screen word

The bar buttons store the screen word after the show and leaving does not
reset it (`0x43a537` reads it; stores only at `0x43a712..0x43af5a`,
`0x43b7e8`, `0x41ef92`). Leaving WEAPONS, REPAIR or BUILD runs `0x412a37`
with prefs +0x2d (§10.1); the leave routines of ARMORY `0x449c46` and CREW
`0x44218a` only hide panels, MISSION's `0x445095` clears `0x477944` and frees
its texts and bitmaps: no SAV state. (L, V: 511 opens and closes with their
rebalances)

### 12.7 Implementation and in-vitro acceptance

- `es2_shell_editors(vfs, tables, editor_tables, save, session, ops, count,
  out, view, error)`: the SAV as for `es2_shell_service_bay`, the results
  tables, the 46 editor table bytes (`0x47665c`, `0x46f868`, `0x46f87a`, read
  from the checked EXE at run time), and an in/out session with the globals
  the screens keep (screen word, bay selection, the REPAIR row and its
  family/index, the snapshot, the BUILD type). Operations: OPEN (bar button
  of REPAIR/BUILD, with the rebalance of the screen left), CLOSE (another bar
  button), BAY, REPAIR_PICK, REPAIR, REPAIR_ALL, REPAIR_CANCEL, BUILD_TYPE,
  BUILD, SCRAP (with ACCEPT); a press on a disabled control is `applied` 0.
  The view returns what the screens show and decide on. The CREW and ARMORY
  operations and session fields of §17 follow these.
- Harness (slot mode): `select:B`, `screen:W`, `herc-type:T`,
  `repair-pick:T:R`, `repair-snapshot`, `repair-one`, `repair-all`,
  `repair-cancel`, `build:T`, `scrap`, `editor-answers`, `editor-state`,
  `emit` (every original handler with its UI calls trapped; `0x43dcdd`
  recorded).
- V (`//shell:editors_real_test`): 209 sequences on GAME_R, GAME_T, a new
  career, the SAV after the C1_01 results path, three prepared states and
  150 seeded edits of them (conditions from −5 to 120, build progress,
  salvage, nine-word and five-entry banks, pilot orders and activity), plus
  targeted cases (every row of every GAME_R Herc, REPAIR ALL and CANCEL,
  Build into an empty bay, scrap from both screens): the SAV after every
  sequence equals the original writers' byte for byte. Own unit test
  `//shell:editors_test` (synthetic state, every refusal). Six one-line
  mutations of the library are each caught by the real test.

### 12.8 Open points

- The REPAIR show's bay replacement and the bay selectors are UI code (they
  write unbuilt widgets) and are not run in vitro; their inputs (`0x41124e`,
  `0x411088`) are checked through the answers and the rules are unit-tested.
- Whether every button class ignores a press with +0x49 = 0
  ([vshell_widgets.md](vshell_widgets.md)).
- Nothing here is checked live.

## 13. Slot statistics

`stats.cpp`: the records behind the statistics panel of the Saved games
screen (§4.3, §9.3). **L** from the listing (`0x4309f0..0x430ec3`,
`0x430ec4..0x430fc1`, `0x410520..0x410631`, `0x4377bc..0x437bd0`); **V** by
`//shell:stats_real_test` (§13.5). Own code: `es2_shell_stats_read`,
`es2_shell_stats_refresh`, `es2_shell_stats_show`
(`shell/include/es2/shell/slots.h`, `shell/src/slots.c`); the panel §26.1.

### 13.1 Records

- One record of 0x43 bytes per slot 0..10 at `0x48cebc + 0x43·slot` (L: the
  pilot reader writes from `0x48cebc + 0x43·slot`, the last field ends at
  `0x48ceff + 0x43·slot`); `0x48cec0` is the name at +4. The records are
  zero at start-up; slot 11 (training) has none.
- Layout (L): +0 word, +2 word, +4 name (the SAV's length bytes, no NUL
  added), +0x22 word, +0x24 byte, eleven words +0x25..+0x39, +0x3b salvage
  dword, +0x3f sector word, +0x41 mission word. +0..+0x3a mirror the player
  pilot `0x482b60` (+0x25 = `0x482b85` skill, +0x29 rank). Kill words:
  +0x2d Herc, +0x2f Flyer, +0x31 Base (this mission; §16.3), +0x33, +0x35,
  +0x37 the totals (**H**, §13.4).

### 13.2 Start-up read `0x4309f0`

Called by game init `0x40e736` after the slot table (`0x40e74c`). For every
slot 0..10 whose flag (`0x482751 + 0x5e·slot`) is set (L): open the slot's
SAV (`0x40e708`, `0x44eeac`; a failed open is reported through `0x44e560`
and the reads go on), then seek and read relative to the current position:

1. 33 times: skip 1 byte, read a count word n, skip 10·n;
2. skip 22, read the sector word into +0x3f and the mission word into +0x41
   (the CAREER position);
3. skip 2, 0x14, 2, 0x3c, 2, 0x3c, 2 (148 bytes);
4. 36 roster pilots: skip 4, read a length word n, skip n, then 2, 1, 0x16;
5. skip 6, 6, 2, 2;
6. the pilot reader `0x410520` into the record: +0, +2, a length word, that
   many name bytes at +4, +0x22, byte +0x24, eleven words +0x25..+0x39;
7. a record count word c; c times skip 2, 2, 2, 0x42, 2, 2, 2, read a word
   m, skip 2·m and 10·m (a negative c reads nothing: `jge` at `0x430d3f`);
8. skip 0x12, read the salvage dword into +0x3b.

**Trap:** the counts are sign-extended (`movsx`), so a negative block count
seeks backwards. The own reader refuses a seek or read outside the file and
a name length that is negative or would write past +0x3e (up to 59 bytes
are written raw and overwritten by the later fields, as in the original).

### 13.3 Refresh `0x430ec4(slot)`

Callers: the load `0x40eaaa` in mode 1 (`0x40ebff`, with the effective
slot), ACCEPT of the Saved games screen (`0x4386c0`, selected slot
`0x474fde`), `0x43c5d7`, `0x44dbaf`, `0x44dcb5`. It copies (L) the name with
strcpy from `0x482b64`, the words `0x482b85` (+0x25), `0x482b89` (+0x29),
`0x482b8d..0x482b97` (+0x2d..+0x37), the salvage dword `0x482bd8` (+0x3b) and
the CAREER words `0x46fc58`/`0x46fc5a` (+0x3f/+0x41). +0, +2, +0x22, +0x24,
+0x27, +0x2b and +0x39 keep their values.

### 13.4 Panel `0x4377bc`

Shown for the selected slot `0x474fde` when it is below 10 with its flag
set, or slot 10 while a game is loaded (`0x4826ee`); otherwise the fields
get ESTEXT 0 (L). Values (L, formats at `0x4752e2..0x475303`): name `%s`;
skill ESTEXT 0x35 + (+0x25) (ROOKIE..ELITE); rank ESTEXT 0x39 + (+0x29);
six kill words `%d`; salvage `%d %s` with (+0x3b) / 1000 (signed `idiv`) and
ESTEXT 0x2f; sector name ESTEXT 0x76 + (+0x3f) (the English entries from
0x76 are "Razor", "Alpha", …; not `<lang>\missions.bin`); mission `%d` with
(+0x41) + 1. The six kill words go to the widgets `0x48d4b8`, `0x48d4bc`,
`0x48d4c4`, `0x48d4c8`, `0x48d4d0`, `0x48d4d4` in the order +0x2d, +0x33,
+0x2f, +0x35, +0x31, +0x37; read with the labels 0x27..0x2b (Herc, Flyer,
Base kills; Current, Total) as Herc current/total = +0x2d/+0x33, Flyer
+0x2f/+0x35, Base +0x31/+0x37 (**H**: pairing from the widget order, not
from the geometry).

### 13.5 Implementation and in-vitro acceptance

- `es2_shell_stats_read` (one slot of §13.2 over an existing record),
  `es2_shell_stats_refresh` (§13.3; the memory state laid out as a record),
  `es2_shell_stats_show` (§13.4). Unit cases in `//shell:slots_test`
  (synthetic SAV-shaped bytes, every refusal).
- Harness (slot mode): `stats`, `stats-refresh:K`, `stats-dump`.
- V (`//shell:stats_real_test`): GAME_R, GAME_T and a new career, each in
  slots 0 and 10, then 60 seeded cases (1..11 flagged slots, SAVs of the
  corpus or edits of them: name length 0..30 with and without NUL, the
  eleven pilot words, +0/+2, +0x22/+0x24, salvage, CAREER words); the game of
  one slot restored by `--save`, then `0x430ec4` for that slot and a random
  one. Both dumps (after the read and after the refreshes) equal the own
  ones byte for byte. Two mutations caught.
- Text menu: the Saved games rows print these statistics (reading each used
  slot's SAV when the screen is shown; after a save VSHELL's refresh gives
  the values of the SAV just written).

### 13.6 Open points

- The load's own refresh (`0x40ebff`) is not run in vitro: a load in mode 1
  needs a valid `missn%d.str` companion; the refresh is run on the game
  restored by `--save` instead. A failed open in `0x4309f0` is not run.
- A name of 30 bytes without NUL: the panel's `%s` would run on into +0x22;
  the own panel shows the 30 bytes.
- The kill pairing of §13.4 is a hypothesis until the panel geometry is
  checked. Nothing here is checked live.

## 14. Debrief page, Telecomm panel and dropship

**L** from the listing of `VER95\VSHELL.EXE`; the statistics words are also
**V** through §16.3. The results path itself is
[vshell_preparation.md](vshell_preparation.md#results-path)
("Sequence of `0x40f09f`"); the page table is §4.8; text drawing
[vshell_text.md](vshell_text.md); show/hide
[vshell_widgets.md](vshell_widgets.md) 2.5. Own code: the words
`es2_shell_debrief_words_*` and texts `es2_shell_debrief_texts`
(`shell/include/es2/shell/texts.h`, `shell/src/texts.c`, §16); page 4 of
the mission screen `shell/src/gui/mission.c` (§33); the movies
`shell/src/movies.c` (§19).

### 14.1 Statistics widgets (created by the mission screen `0x442bc4`)

Twenty labels, each `0x40b828` (factory size 0xc9) with parent
`[0x48d8b8]` and font `[0x46de00]`, created at `0x443840..0x44444f` (L).
Rectangles are (left, top, right, bottom). The text argument is
`0x4087f0([0x46ddf8], id)`; value labels are created with ID 0. Captions
are the English ESTEXT entries (§16.3).

| Global | Rect | Created with | Filled by `0x40f904` with |
| --- | --- | --- | --- |
| `0x48d918` | 0x19,0x2e,0xba,0x3c | ESTEXT 0x145 (Mission Outcome) | — |
| `0x48d920` | 0x19,0x41,0xba,0x4f | ESTEXT 0x148 (Salvage) | — |
| `0x48d928` | 0x19,0x53,0xba,0x61 | ESTEXT 0x14a (Weapons Recovered) | — |
| `0x48d91c` | 0xc6,0x2e,0x127,0x3c | ID 0 | ESTEXT 0x146 + outcome word |
| `0x48d924` | 0xc6,0x41,0x127,0x4f | ID 0 | `"%d %s"`: tally / 1000, ESTEXT 0x149 |
| `0x48d92c` | 0xc6,0x53,0x127,0x61 | ID 0 | `"%d"`: weapons recovered |
| `0x48d930` | 0x19,0x74,0x65,0x82 | ESTEXT 0x14c (Kills) | — |
| `0x48d934` | 0x66,0x74,0xa8,0x82 | ESTEXT 0x150 (Hercs) | — |
| `0x48d938` | 0xa9,0x74,0xe7,0x82 | ESTEXT 0x151 (Bases) | — |
| `0x48d93c` | 0xe8,0x74,0x127,0x82 | ESTEXT 0x152 (Flyers) | — |
| `0x48d940` | 0x19,0x8c,0x65,0x9a | ESTEXT 0x14e (You) | — |
| `0x48d944` | 0x19,0xa2,0x65,0xb0 | ESTEXT 0x14f (Squad) | — |
| `0x48d948` | 0x19,0xc3,0x65,0xd1 | ESTEXT 0x14d (Losses) | — |
| `0x48d94c` | 0x66,0x8c,0x9a,0x9a | ID 0 | You, column 1 |
| `0x48d950` | 0x9b,0x8c,0xd9,0x9a | ID 0 | You, column 2 |
| `0x48d954` | 0xda,0x8c,0x118,0x9a | ID 0 | You, column 3 |
| `0x48d958` | 0x66,0xa2,0x9a,0xb0 | ID 0 | Squad, column 1 |
| `0x48d95c` | 0x9b,0xa2,0xd9,0xb0 | ID 0 | Squad, column 2 |
| `0x48d960` | 0xda,0xa2,0x118,0xb0 | ID 0 | Squad, column 3 |
| `0x48d964` | 0x66,0xc3,0x9a,0xd1 | ID 0 | `"%d"`: lost count |

Label arguments after the text (L): ESTEXT labels `1, 0x1a, 0, 0`; the
three outcome/salvage/weapons values `0, 0x17, 0, 0`; the table values
`1, 0x17, 0, 0` (their meaning: the label `0x40b828` in vshell_text.md).

### 14.2 Words feeding the widgets: `0x40f904`

Called once, at `0x40f524`, in step 9 of the results path (mode 1 only),
after `0x412ce2` and `0x412a37` and before `0x4133eb` (L). Pushed
(`0x40f510..0x40f51e`): dword `[0x482bcd]`, dword `[0x482bd1]`, and the
word `[ebp−0x2e] + bx` of the results path, bx = the return value of
`0x412ce2` (16-bit add). The callee reads them as: `ebp+8` word = outcome
`0x482bcd`; `ebp+0xa` dword = bytes `0x482bcf..0x482bd2` (the results.dat
tally); `ebp+0xe` word = lost count `0x482bd3`; `ebp+0x10` word = the sum.
`[ebp−0x2e]` is the pair count *P* (**H** in vshell_preparation.md,
"statistics widgets"; the sum is checked in §16.3), so weapons recovered =
*P* + the items `0x412ce2` added.

Kills (L, `0x40f90d..0x40fa84`): You = the player record (`0x482b60`)
words +0x2d (`0x482b8d`), +0x31 (`0x482b91`), +0x2f (`0x482b8f`): this
mission's three words written by `0x410632` (results path step 4). Squad
starts with the same three words and adds, for each order *i* = 1 ..
`[0x482b5c]` − 1 (team word 0, signed 16-bit compare), the pilot index
`0x4108fa(0x482b5c, i)` if it is not −1 and its record `[4*idx + 0x482b9b]`
has byte +0x24 nonzero: that record's words +0x2d, +0x31, +0x2f (16-bit
wrapping adds). The index < 3 checks (`0x44e560`, `squad.h`) are debug
assertions.

Mapping word → widget → format (L, `0x40fa8a..0x40fc6c`, each through
setText `0x40b8bb(w, text, a, 0x29, 1)`, a = 0 for orders 1..3, 1 for the
rest), in call order:

| Order | Widget | Text |
| --- | --- | --- |
| 1 | `0x48d91c` | ESTEXT (0x146 + signed word `0x482bcd`) |
| 2 | `0x48d924` | `sprintf("%d %s", dword 0x482bcf / 1000 unsigned, ESTEXT 0x149)` |
| 3 | `0x48d92c` | `"%d"` of the signed word sum (*P* + `0x412ce2`) |
| 4 | `0x48d94c` | `"%d"` player +0x2d |
| 5 | `0x48d958` | `"%d"` Squad sum of +0x2d |
| 6 | `0x48d950` | `"%d"` player +0x31 |
| 7 | `0x48d95c` | `"%d"` Squad sum of +0x31 |
| 8 | `0x48d954` | `"%d"` player +0x2f |
| 9 | `0x48d960` | `"%d"` Squad sum of +0x2f |
| 10 | `0x48d964` | `"%d"` lost count `0x482bd3` (signed word) |

So column 1 (x 0x66, Hercs) shows +0x2d, column 2 (x 0x9b, Bases) +0x31
and column 3 (x 0xda, Flyers) +0x2f. Outcome values other than 0 and 1
would index 0x148 and beyond (not expected). Within VSHELL only the
creation, `0x40f904` and the hide routine `0x444fa4` reference these twenty
globals (L, grep of the listing), so the texts stay until the process ends.

Visibility (L): `0x444fa4` hides all twenty; it runs on page 0
(`0x444a3c`), page 1 (`0x444b6e`) and in the leave `0x445095`
(`0x4450a3`). Page 4 never shows them one by one; it shows their parent
`[0x48d8b8]` (`0x444e11`). By vshell_widgets.md 2.5 a child hidden
directly stays hidden when its parent is shown. **H:** the statistics are
visible on page 4 only while `0x444fa4` has not run since the screen was
built (the results-path entry straight to page 4); after a visit to page
0/1 or a leave they stay hidden. Reference: the debrief capture (§33.6).

### 14.3 Page 4 in `0x444873(4)` and the debrief movie

`0x444d1f..0x444e0a` (L): `0x40d231([0x48d904], 0x44524b())` sets the
debrief report (`0x44524b` → `0x413bf8` → `[0x46fc68]`, built by
`0x4139f6`); `[0x46c26a]` := theatre − 1 + 10 (si = `[0x46fc58]` − 1 from
`0x444a12`); the map frame `[0x48d8f0]`: its +0x55 object's +0xb5 and its
own +0x4d := 0x26, redraw, +0x49 := 0; title ESTEXT 0xb9 into `[0x48d8b8]`
(`0x444dc0`). Then:

- **Once-flag `0x477948`** (byte, 0 in the DATA image). If it is 0:
  `0x41e8c0(0x445de1(), rect [ebp−0x50..−0x44], word [0x46c26a], 0, 0)`,
  then the flag := 1 (`0x444e0a`). `0x445de1` → `0x413c05` returns the word
  `0x48419e`, stored by `0x4139aa` from the debrief record's +0x3e
  (`0x4139ea`; record family: [mission.md](../formats/mission.md), "list 5,
  the 64-byte debrief family", passed by `0x41d8e7` from `[0x470784]`). The
  last argument (queue record +0x14) is 0: no theatre/dropship sequence
  follows the debrief movie.
- Writers of `0x477948` (L, complete for the listing): := 0 by the new
  career `0x40e8a5` (`0x40e8b8`) and by the load `0x40eaaa` at `0x40ec0d`
  (together with the briefing flag `0x477947`, after the file is read); := 1
  only at `0x444e0a`. The results path loads slot 10 first (§2), so a
  `-X3`/`-X4` VSHELL process starts with the flag 0 and queues the debrief
  movie on its first page-4 visit; later page-4 visits in the same process
  (and after a save without load) do not; a load or new career re-arms it.
- The queue's own skip rules (`0x41e88f`, flag `0x482359`) still apply
  (§19.2). The word argument goes to queue record +0x12 (`0x48575a`), the
  palette word of §19.1.
- Text panel: `0x445343(4)` selects panel 2 (`[0x48d904]`) through the
  table at `0x44541a` and stores the page in `0x478140` (L).

### 14.4 Telecomm panel `0x4454b8`: inert

`0x4454b8` is the click handler given to the widget built at
`0x442d29..0x442d5a` (`0x40bc48`, rect 0xa,0x14,0xf9,0x100, parent
`[0x48d8b4]`). After the prologue its first instruction is `jmp 0x445572`
(`0x4454bf`), the epilogue; no reference in the listing targets
`0x4454c4..0x44556f` (L). The unreachable body would enqueue, for page
`[0x481108]` = 1, the briefing movie (`0x445ddb` → `0x413bfe` → `0x48419c`)
and, for page 4, the debrief movie (`0x445de1`), with word theatre − 1 + 5 /
+ 10 and rect 0x15,0x56,0xf0,0xb4, ignoring the once-flags. Clicking the
Telecomm panel does nothing; the own shell does not replay.

### 14.5 Dropship animation `0x44472f` and its trigger

Trigger (L): the player `0x41e98c`, after a movie whose queue record +0x14
(`0x48575c`, written from the enqueue's last argument at `0x41e94a`) is
nonzero, runs the teardown `0x445095` and then `0x44472f`
(`0x41ef5c..0x41efaa`). The enqueue with that argument 1 is the theatre
movie of the first mission-screen visit (page 0 while `0x477946` = 0):
`0x41e8c0(0x4b + theatre − 1, rect [ebp−0x60..−0x54], −1, 0, 1)` at
`0x444aad..0x444ad5`, queued after the group movie `0x46 + theatre − 1`
(`0x444a7e..0x444aa5`, word 3, or 4 for theatre ≥ 5, last argument 0); then
`0x477946` := 1. The other enqueues (page 4, Telecomm, `0x4447ad`) pass 0;
the complete enqueue-site list is §19.2.

`0x44472f` (L): bx = `[0x46fc58]` − 1. If bx = 4 (theatre 5): enqueue
`dropship.avi` `0x41e8c0(0x55, rect 0x20,0x3c,0x240,0x168, −1, 0, 0)`, then
`0x43a430(2)` (the palette window, §28.2), `0x42f7f0`, `0x42f6a8`
(fade-out, focus lost: audio.md), no theatre picture. Otherwise load the
picture of the table `0x478068[bx]` (vshell_presentation.md), assign it to
`[0x48d8c0]` +0x5d, show `[0x48d8c0]`, palette 0xf + bx
(vshell_process.md step 5). So the dropship plays once per first
mission-screen visit in theatre 5, right after that theatre's movie, under
the same `0x477946` rule as the group movies (cleared by ACCEPT and
RESTORE, §4).

### 14.6 Consumers

- Texts (§16.3): outcome text = ESTEXT 0x146 + outcome; salvage = `"%d %s"`
  of tally / 1000 (unsigned) and ESTEXT 0x149; the remaining eight numbers
  of §14.2 as `"%d"` of signed 16-bit words; the debrief report
  `[0x46fc68]`.
- Page 4 of the mission screen (§33): title 0xb9, text panel 2, the twenty
  widgets of §14.1 with their rectangles, visibility per §14.2, the movie
  per §14.3, the Telecomm panel inert (§14.4).

### 14.7 Open points

- Visibility of the statistics after leaving page 4 and returning (§14.2,
  **H**). Nothing in §14 itself is run in vitro or live; the reference is
  the debrief capture.

## 15. Practice missions, INSTANT ACTION, VIEW DEMO, the DEBUG dialog

**L** from the listing; **V** from the shell-invitro slot mode with the
practice operations, which run VSHELL's own handlers in the mapped original
(the operations are listed in `tools/reference-game/shell-invitro/README.md`
and at `run_slots` in its `main.c`). Own code:
`shell/include/es2/shell/practice.h` (`shell/src/practice.c`); the text
menu's entries 7..9 (`shell/src/menu.c`); the screen §25. ESTEXT texts are
the English ones, cited by ID. Regression: `//shell:practice_real_test`,
`//shell:practice_test`.

### 15.1 Text buttons, the event drain and the posted click

- The text button `0x40a004` (vtable `0x46e9b0`, event method `0x40a0bf`,
  base `0x409d8a`) takes type-0x20 events. Byte +0x24 is the mouse transition
  the input layer records (`0x409111..0x4091d5`): 2 left button down, 1 left
  up, 4 right down, 3 right up, 0 a move. The base ignores the event when the
  widget's +0x49 (enabled) is 0 or the movie player's flags `[0x470eb0]`,
  `[0x470fb0]` (`0x41e786`, `0x41ede8`) are set; a press (2 or 4) sets the
  pressed flag +0x45 and redraws; a release (1 or 3) with +0x45 set calls the
  handler (`0x41fc28` → the widget's +0x3d with the release event) and clears
  +0x45. So **a text button runs its handler on the release of either mouse
  button**, and the handler sees 1 (left) or 3 (right) in +0x24. `0x40a0bf`
  adds the click sound `0x42f547` on a press whose +0x25 is 0 (not a posted
  one) when `[0x482356]` is set. (L)
- The image button `0x40a2c4` (vtable `0x46ea5c`, `0x40a3a2`) is another
  class: it calls the handler on the press (2) unless its +0x61 is set
  (auto-repeat); the release only clears +0x45. Not used by these screens
  (vshell_widgets.md). (L)
- The drain `0x469ee4` pops the queue `[0x5ddcb0]` until it is empty
  (`0x469fd7..0x469fe5`) and does not test the quit flag; `0x46a274` delivers
  an event to the first widget, from its target +0x20 up through the parents
  (`0x46a2f8`), whose mask +0x39 takes the event type, through that widget's
  vtable +0. The post `0x468780` appends events other than raw mouse events
  with `0x469ea8`. So a button handler runs inside the drain, and events it
  posts are delivered by the same drain before the main loop tests the quit
  flag. (L)
- `0x413305` (unless the cheat byte `0x482368`) posts a press (2) and a
  release (1) on Use Default `[0x48dcfc]` (type 0x20, +0x25 = 1), a text
  button: the release runs `0x44dbea` → `0x44db5c`. After INSTANT ACTION's
  handler has set exit code 2 and the quit flag, the same drain delivers the
  two events, Use Default prepares the CAREER entry and in mode 0 calls
  `0x40f68c` again, exit code 2, quit flag. The DATA files DBSIM gets are the
  preparation's. (L; V: the handler followed by `use-default` gives the own
  files byte for byte.)
- INSTANT ACTION's `0x402266` (twice, around the bay background's show/hide,
  or after `0x4077ee` in full screen) is a frame pump: up to 24 rounds of
  `0x406328`, the input poll `0x41e0bf`, the dispatch `0x46848c` (a nested
  drain), `0x407c1b` and the present `0x41fe49`, while `[0x46c288]` is set
  and the quit flag is not. It runs before the new game, when no click is
  posted yet. (L)

### 15.2 Practice screen (`0x44b310`; shown by PRACTICE MISSIONS `0x431f3a`)

- PRACTICE MISSIONS `0x431f3a`: hide the main menu (`0x4317c3`), show the
  screen `0x44c322` (two panels, then `0x44c40c(0)`), then the mode store
  `0x40ec56(0)` (mode word and prefs +0x2a through the read-modify-write,
  §11.4). (L, V)
- Entry `0x44c40c(i)` (handlers `0x44caa3`, `0x44cb04`, `0x44cb65`,
  `0x44cbc6`, `0x44cc27`, `0x44cc88`, `0x44cce9`, `0x44cd4a` for entries 0..7;
  their table `0x48dc7c` is filled by the constructor): **nothing at all when
  i is already the selection** `0x479c54` (`0x44c41c`); otherwise the old
  list item is drawn normal, for i < 8 the new one highlighted and the Herc
  Type button `[0x48dc1c]` disabled for i < 4 (+0x49 := 0) and enabled for
  4..7; then prefs +0x28 := the low byte of word i of `0x479c56` (`0x44c533`,
  store `0x40ddfe`) and the selection := i. The table holds 0, 0, 1, 8, 4, 2,
  7, 3 for the entries and 5, 7, 3 for positions 8..10 (read from the checked
  EXE at run time, not embedded). A second show keeps a Herc Type set by hand
  because entry 0 is already selected. (L, V)
- The five parameter buttons are text buttons (`0x40a004`, §15.1): Ammo
  `0x44c676` (+0x25, modulus 2, labels 0x12a + v), Damage `0x44c5b9` (+0x26,
  2, 0x128 + v), Mission Difficulty `0x44c733` (+0x27, 4, 0x35 + v), Herc
  Type `0x44c8ad` (+0x28, 9, 0x6e + v), Time of Day `0x44c7f0` (+0x29, 2,
  0x12c + v). **A left click steps forward** (`0x40de5b`: v + 1, and 0 only
  when that equals the modulus), **a right click back** (`0x40de88`: v − 1,
  and modulus − 1 only when that is 0xff), on the current block only; a byte
  already above the modulus grows (forward) or shrinks (back) without
  wrapping. Herc Type is pressable only for entries 4..7. The value label is
  ESTEXT base + byte, whatever the byte. (L, V)
- Main Menu `0x44c96a`: hide (`0x44c343`), main menu `0x431718`; **no
  revert**: the controls stay uncommitted in the current block, so a later
  Preferences CANCEL drops them and an ACCEPT writes them (V: both in the
  seeded sequences).
- Begin Mission `0x44ca26`: commit (`0x40dd6e`), the writer `0x40dd0a` (54
  bytes of the current block), new game `0x40e8a5("TRAINEE" at 0x479f70,
  prefs +0x27)` in mode 0, whose `0x413053(0)` sets the CAREER position (0,
  `0x479c54`) and posts the click; Use Default then prepares that entry with
  c1..c5 = prefs +0x25..+0x29, the preset index `0x479c54` and the preset
  flag `0x473784` (the preset loadout path c4 only when index > 3 and flag 0,
  [vshell_preparation.md](vshell_preparation.md), `0x41cbb1`). (L, V)
- **Traps:** the constructor draws all five value labels at start-up, after
  the prefs blocks are loaded; a control byte whose label index lies beyond
  the catalogue makes `0x4087f0` assert ("Text::getString: Index out of
  bounds", `src\ctxt.cpp`), in vitro for the handlers (V) and statically for
  the constructor (`0x44bc06..`, L), so VSHELL does not start with such a
  `prefs.cfg`. `0x44c40c` with a previous selection 8..10 would take the
  handler table `0x48dc7c` for list items and write into the code;
  unreachable, because only INSTANT ACTION selects 8..10 and the process ends
  right after it. (L)

Own code: `es2_shell_practice_show`, `es2_shell_practice_entry`,
`es2_shell_practice_control`, `es2_shell_practice_begin`.

### 15.3 INSTANT ACTION (`0x431935`)

In order (L, V): preset flag `0x473784` := 1 (never cleared: for the rest of
the process practice entries 4..7 lose their preset loadout); the mode store
`0x40ec56(0)` (read-modify-write of +0x2a); `0x44c58b`: `0x44c40c(prefs
+0x2e + 8)` (16-bit add on the byte), +0x2e stepped forward modulo 3
(`0x40de5b`), commit, the writer; the frame pumps (§15.1); new game
`0x40e8a5("TRAINEE" at 0x47396b, prefs +0x27)`; `0x40f68c` on the new game's
state; exit code 2; quit flag; then (§15.1) the posted Use Default: the
preparation of (0, 8 + v) = DEMO, DEMO_01, DEMO_02 and `0x40f68c` again.
Uncommitted practice controls are committed and written here (V). A
prefs.cfg with +0x2e above 2 reads past the table and prepares a position
group 0 does not have (11 entries): the own code refuses it before anything
changes. With `-@` no click is posted: VSHELL exits with the new game's
`mission.var` (the cleared bank) and `player.mec` (word 0, team count, the
player's variant and rank, no hangar record because `0x410cdb` writes
nothing for an empty bay, no squad, 33 and the flags) and the old
`script.dat`/`MISSION.STR` (V).

Own code: `es2_shell_instant_action`, `es2_shell_practice_new_game_files`.

### 15.4 The mode-0 new game and the SAV after the launch

- `0x410b11(hangar, mode)` clears the hangar (`0x410aaf`: count, selection,
  records) and loads `HERCS.DAT` only in mode 1; everything else of
  `0x40e8a5` is mode-independent. The mode-0 new game is the mode-1 one
  without hangar records, with the same RNG draws. (L, V)
- `0x40e8a5` writes neither the briefing selection (`0x4840cc`, 148 bytes),
  nor the nine-word bank (`0x483c46`, HERC_INF +14 per vehicle, changed by
  the results path), nor the 20 bytes at `0x4833ac`: a new game carries them
  from the game the process held before (V: a launch after `restore:N` of
  SAVs with changed values). In a fresh process they are zero, HERC_INF's
  words and zero. This holds for the mode-1 new career too (§9.4).
- The state after the mode-0 preparation, as the exit save writes it: the
  team words, the roster's and the player's bay, active byte and order, the
  roster selection and every hangar record from the preparation (vehicle,
  its table index, 33 condition words, progress 100, countdown 0, capacity;
  per occupied slot: slot, item ID, list position, 100, condition,
  secondary), the bank = the prepared `mission.var`; the CAREER position
  stays (0, i) and the briefing selection is not derived in mode 0. An
  authored item −1 (TRAIN3, slot 4 of the player's Herc) is stored as −1
  with list position 0xffff. (V: 1,280 combinations and the seeded runs, SAV
  byte-equal)
- DATA files: `script.dat` and `MISSION.STR` are rewritten without
  truncation (a longer file's tail stays; V), `mission.var` and `player.mec`
  replaced. The loop end's `0x40e933(10)` writes slot 11 (`GAME_T.SAV`, mode
  0) and copies the three files to `script11.dat`, `missn11.str`,
  `player11.mec` (V, menu sessions).

Own code: `es2_shell_practice_new_game`, `es2_shell_practice_prepare`,
`es2_shell_practice_run`, `es2_shell_mode0_prepared_save`.

### 15.5 VIEW DEMO (`0x431bfe`) and the launcher

`0x431bfe`: in full screen the display switch-back `0x4077ee` and a frame
pump, exit code 5 (`0x431c24`), quit flag; no prefs or mode store (L, V).
The loop end saves slot 10 (11 in mode 0) when a game is loaded (§1).
ES.EXE starts DBSIM `-X5 -R<n> -D`
([launcher.md](launcher.md#start-loop)); the demo end returns 6,
VSHELL `-X6` shows the main menu without a readback. `es2launch` does the
same for a shell replacement: `es2l_after_replacement` passes 5 (VIEW DEMO)
as well as 2; any code but 2 and 5 ends the loop (`launch_chain_test` case
H: replacement 5 → `dbsim.EXE -X5 -R0 -D` → 6 → replacement `-X6`).

### 15.6 The DEBUG dialog's ACCEPT (`0x44dc4d`, `-@`)

ACCEPT hides the dialog, formats `msn\%s.msn` (`0x47a342`) of the edit text
(widget +0x45 of `[0x48dcf8]`), replaces its first `^` by `_` (`0x46b994`,
`0x44dc8f`) and prepares that file (`0x41cd61`); the CAREER position stays
as the new game set it. Mode 1: the map, the briefing texts, the bay and the
mission screen (as Use Default); mode 0: `0x40f68c`, exit 2, quit. (L; V:
`demo^01`, `DEMO`, `TRAIN1`, `TRAIN8`, `C1_01` from the practice screen,
byte for byte.) The own code accepts 1..31 letters, digits and `_` after the
replacement (a missing file asserts in the original). Mode 1 in detail:
§39.

Own code: `es2_shell_debug_mission`.

### 15.7 Implementation and in-vitro acceptance

- `es2/shell/practice.h`: the process state (`es2_shell_practice`:
  selection, preset flag, the Herc Type button), the screen
  (`es2_shell_practice_show`, `_entry`, `_control`, `_begin`),
  `es2_shell_instant_action`, `es2_shell_debug_mission`, the launch
  (`es2_shell_practice_new_game`, `_prepare`, `_run`: the mode-0 new game
  with the carried state, the preparation, `es2_shell_mode0_prepared_save`)
  and INSTANT ACTION's own files (`es2_shell_practice_new_game_files`); the
  tables (Herc types, the two names) and the other EXE data are read from
  the checked `VSHELL.EXE` (`es2_shell_exe_span`,
  `es2_shell_practice_tables`). No file I/O.
- Harness (slot mode): `practice-show`, `practice-entry:I`,
  `practice-control:K:B`, `practice-state`, `practice-menu`,
  `practice-begin`, `instant-action`, `use-default`, `debug-accept:TEXT`,
  `view-demo`, `practice-reset`, `practice-emit`; the widgets the handlers
  write are blocks of the worker's own memory, the drawing calls are
  trapped.
- V (`//shell:practice_real_test`): 1,707 records byte-equal (the four DATA
  files as left on the memory disk, `prefs.cfg`, the SAV, selection, flag,
  exit code, quit flag, mode, position, loaded flag): every entry with every
  control combination (1,280, left and right clicks), INSTANT ACTION over
  +0x2e 0..2, its rotation, after the practice screen, with `-@` and without
  a prefs file, VIEW DEMO, 12 seeded sequences (Preferences ACCEPT/CANCEL
  after Main Menu, ACCEPT of typed files, three languages, prefs files of 0,
  10, 54, 60 bytes), and launches after a held game. Five menu sessions: the
  DATA and SAV directories after PRACTICE MISSIONS, INSTANT ACTION, `-@`
  ACCEPT and `-@` INSTANT ACTION equal the worker's after the same
  operations and the exit save; VIEW DEMO through `es2launch` gives the `-D`
  chain. `//shell:practice_test` (every refusal). Mutations: five of six
  one-line changes caught (the sixth, the item record's +6 from the
  condition word, is equivalent in this domain: every mode-0 condition is
  100).

### 15.8 Open points

- The return of a practice or instant-action mission (`-X3`/`-X4` in mode
  0: VSHELL loads slot 11 and runs the results path, main menu or the
  Replay box when the player was killed) is handled by the shell state
  engine ([launcher.md](launcher.md#shell-state-engine)), not by the
  text menu, whose `main()` reads results only for the backend's campaign.
- The text menu's practice RNG starts from the start-up seed (as its new
  career); VSHELL's continues the process stream.
- The image button's press-time handler and the click sound
  (vshell_widgets.md). Nothing here is checked live.

## 16. Mission-screen texts: pages 0, 1 and 4

**L** from the listing of `VER95\VSHELL.EXE`; **V** also equal in vitro
(the shell-invitro slot mode, `//shell:texts_real_test`, §16.6). Own code:
`shell/include/es2/shell/texts.h` (`shell/src/texts.c`); the screen §33
(`shell/src/gui/mission.c`). No text or ESTEXT string is quoted here beyond
single-word captions.

### 16.1 Page 0: title and campaign text

- **Title** (L, `0x444ae4..0x444b47`, every page-0 visit, after the
  first-visit movies): `sprintf("%s %s", ESTEXT(0x77 + si), ESTEXT(0x7c))`
  with si = CAREER group word `[0x46fc58]` − 1 (`0x444a12`, 16-bit; the ID
  is sign-extended from `di`), set with setText on the title label of
  `[0x48d8b8]` (+0x69). So the sector name comes first and the word 0x7c
  ("Sector" in English) second. Groups 1..5 give IDs 0x77..0x7b (the slot
  statistics use the same names as 0x76 + group, §13.4). Not run in vitro
  (inline in `0x444873`). Own: `es2_shell_sector_name_id`,
  `es2_shell_sector_title`.
- **Campaign text** `0x40fd2d(si)` (L, V): the path is `strcat`ed into the
  stack buffer `[ebp−0x15c]`: the language prefix of `[0x48235e]` (0 `eng\`,
  1 `fre\`, 2 `ger\`, other: none) and `campaign.str`
  (`0x40fd72..0x40fdd5`). **Trap:** the buffer is not initialised before the
  first `strcat`; the path is right only when its first byte happens to be 0
  (the in-vitro op clears the stack below its frame first). The stream
  (`0x402f24`/`0x4032b1`, VOL-aware) is read as u32, i16 N, then per string
  i16 L, L bytes (allocated L + 8, zeroed, and read only for the string
  whose ordinal equals si), byte E, E bytes skipped (sign-extended,
  `0x40fef1..0x40ff12`). The result `[0x46f520]` (the previous one is freed
  first) is passed to the page-0 panel `[0x48d8fc]` by `0x40d231`
  (`0x444a42..0x444a4f`). For si outside 0..N−1 (mode 0's group 0, or a
  sixth group) the pointer stays null and `0x40d231` faults in `strlen` (V:
  both return null for 5 and 65535). The CD file has one group of five
  strings. Own: `es2_shell_campaign_text`.

### 16.2 Page 1: briefing, objectives, intelligence (`0x4135bb`)

(L `0x4135bb..0x413951`, V.) Called from three sites: the load `0x40eaaa`
when its flag asks (`0x4132fa`, §9.2) and the DEBUG dialog's Use Default
and ACCEPT after their preparation (`0x44dba8`, `0x44dcae`); the page shows
the three results through `0x445343`'s panels.

1. `0x413952` frees the three previous texts.
2. `data\mission.str` is loaded (`0x428521`); `0x42863a` returns the
   **low byte** of the group count, sign-extended; an array of that many
   pointers is allocated and filled by the group iterator `0x4286e2` with a
   null aux pointer (E is a plain byte, nothing skipped; no line feed is
   stripped, unlike DBSIM's reader). If the iterator's count differs from
   the byte, the assertion at `0x413679` fires: a group above 127 strings is
   fatal (and overruns the array first).
3. For each list, in this order: briefing (30 references at `0x4840e4`,
   into `[0x46fc5c]`), objectives (10 at `0x4840ce`, `[0x46fc60]`),
   intelligence (30 at `0x484122`, `[0x46fc64]`): size = 1 + the sum of
   `strlen` of every reference ≠ −1; `0x40870a` returns a zeroed block;
   reference 0 is `strcpy`ed if ≠ −1, references 1..n−1 `strcat`ed if ≠ −1.
   So the text is the concatenation of the referenced strings in reference
   order, also when the first reference is −1. References are not bounds
   checked (a value ≠ −1 outside 0..count−1 reads outside the array).
4. The file object is released (`0x42860d`) and the array freed.

The stored counts (`0x4840cc`, `0x4840e2`, `0x484120`) are not read. The
selection block 0x4840cc..0x48419f (148 bytes: count + 10, count + 30,
count + 30 references, the movie word) is the one the preparation derives
(`es2_shell_output.briefing`, `0x4134f2`) and the SAV stores after the two
CAREER words. The debrief text `[0x46fc68]` of `0x4139f6` (page 4) follows
the same concatenation rule (vshell_preparation.md "Debrief selection", `campaign.c`).

Own: `es2_shell_briefing_texts_compose` (+ `_free`),
`es2_shell_briefing_selection`.

### 16.3 Page 4: the statistics as text

The mapping of §14.2 holds (L, V): ten setText calls in the order outcome,
salvage, weapons recovered, You/Squad of column 1 (+0x2d), column 2
(+0x31), column 3 (+0x2f), lost. Captions (English ESTEXT, checked at run
time): 0x14c Kills, 0x14d Losses (the lost-count row), 0x150 Hercs, 0x151
Bases, 0x152 Flyers; this agrees with §13.1's word names (+0x2d Herc, +0x31
Base, +0x2f Flyer). Outcome 0 and 1 select 0x146/0x147 (Failure, Success);
the ID is `outcome + 0x146` as a 16-bit sum, sign-extended
(`0x40fa90..0x40fa99`). Further details (L):

- **Trap:** the salvage text is formatted into the 40-byte buffer
  `[ebp−0x34]`; a unit text (ESTEXT 0x149) longer than 39 − digits would
  overwrite the six column sums at `[ebp−0xc..−2]` before they are printed.
  The CD's 0x149 is short; the library refuses the overlong case.
- The outcome text goes to setText unchanged (assertion above 100 bytes).
- Words after a results path (`es2_shell_debrief_words_after`): outcome,
  tally and *P* from results.dat +0/+2/+6; lost = bank word 9 of the SAV the
  path wrote (step 6 stores it; later steps clear only the unlock variables
  50..59, table `0x46fbc2`, and 60..63, `0x411ee9`); the items `0x412ce2`
  added = the sum of the positive bank words 21..49 (one item per unit, those
  words are not cleared), 16-bit. Checked against the own results path's
  `lost`/`added` (V, §16.6).

Own: `es2_shell_debrief_words_from_save`, `_from_results`, `_after`,
`es2_shell_debrief_outcome_id`, `es2_shell_debrief_texts_compose`.

### 16.4 Text panels: line and page split

`es2_shell_report_layout` implements `0x40d686` and the page count of
`0x40d231` (vshell_text.md "Text pages"), with the glyph widths and the
panel rectangle from the caller (the font data of vshell_text.md). The
breaker in detail (L, `0x40d686..0x40d7ec`):

- At a space the line (up to that space) is measured; width ≤ W moves the
  last break there; otherwise the byte at the last break becomes NUL only if
  it is a space, the next line starts one byte after the last break, and the
  assertion requires that line (up to the current space) to be **narrower**
  than W (`jl`, `0x40d75b`).
- At the end, a last line wider than W gets a NUL at the last break
  **whatever byte it holds** (`0x40d7d2`, no space test) and a further line
  after it. With no break yet the last break is the text start: the first
  line becomes empty and the second starts at the second byte.
- Lines are C strings in the panel's copy and may overlap (the duplicate
  case of vshell_text.md). Each line becomes a label: a line above 100 bytes
  asserts in setText.

L only: no in-vitro case (it needs a font object) and no frame.

### 16.5 RNG: no draws

None of `0x40fd2d`, `0x4135bb`, `0x40f904`, `0x40d231`/`0x40d686` calls the
RNG entries `0x465c90`, `0x465cbc`, `0x465ce8` or `0x465d2c` (L, call lists
of each range); the library draws nothing. The texts still depend on the
RNG indirectly: `MISSION.STR`, the briefing selection and the debrief
selection come from draws of the preparation and of the debrief reader
(vshell_preparation.md, draw table).

VSHELL resets `0x482409` once per process and advances it by n =
`GetTickCount() & 127` draws (`0x40796c`), so every results process
(`-X3`/`-X4`) starts from one of 128 states. The shell state engine
restarts the RNG at every process start in the same way
([launcher.md](launcher.md#shell-state-engine), "RNG convention").
Where an own chain instead carries the 114-byte state from a results step
into the next preparation (vshell_preparation.md "The own chain: a
documented deviation"), from the second transition on the type-1 condition
draws and template choices of the debrief reader and of the next
preparation, the bank words 4..6, `script.dat`, `MISSION.STR`, the briefing
selection and therefore the page-1 and debrief texts may differ from every
run VSHELL could produce; roster, player variant and starting salvage are
unaffected (they are drawn at a new career).

### 16.6 Implementation and in-vitro acceptance

`shell/src/texts.c` (`es2/shell/texts.h`): `es2_shell_sector_name_id`,
`es2_shell_sector_title`, `es2_shell_campaign_text`,
`es2_shell_briefing_texts_compose` (+ `_free`),
`es2_shell_briefing_selection`, `es2_shell_debrief_words_from_save`,
`_from_results`, `_after`, `es2_shell_debrief_outcome_id`,
`es2_shell_debrief_texts_compose`, `es2_shell_report_layout` (+ `_free`).
No file I/O; refusals where VSHELL faults or asserts (index outside the
file, count above 127, references outside the group, overlong texts).
`//shell:texts_test`: unit checks on synthetic STR files and SAV images.

Worker ops (shell-invitro README): `briefing`, `briefing:N`, `campaign:N`,
`debrief-stats:O:T:L:S` (stand-in label objects with the no-draw bit,
`0x40b9fb`). V (`//shell:texts_real_test`), per language ENG/FRE/GER: page
1 for the 50 campaign missions of groups 1..5 (the 11 training missions of
group 0 are excluded: their mode-1 preparation asserts at `0x41ce61`) plus
40 seeded selections: 90 cases, all equal; page 0 for groups 0..6: 7 equal
(null for 0 and 6); five titles composed (own only); page 4: 336 word sets
on 14 states (GAME_R, GAME_T, a new career, nine seeded edits, the SAVs the
own results path writes for two oracle goldens): all equal. Results words:
the 2 of 10 goldens whose results.dat advances from GAME_R give the own
path's lost and P + added. Mutations caught: 4 of 4. Text menu: the service
bay's MISSION prints the title, the campaign text and the three page-1
texts; a continuing results path prints the page-4 values after the debrief
text; `//shell:menu_real_test` checks both in the bay-mission transcript
against the worker's `briefing` and `debrief-stats` ops.

### 16.7 Open points

- The title order and composition, the report layout and the page-4
  visibility (§14.2) are L only; the screen reference is the debrief
  capture (§33.6).
- Page 4 from a results path is in vitro for two goldens only (the others
  end before the advancement from GAME_R).

## 17. Service-bay editors: CREW, ARMORY, weapons scrap

**L** from the listing; **V** from the shell-invitro slot mode with the
CREW/ARMORY operations, which run VSHELL's own handlers in the mapped
original (`tools/reference-game/shell-invitro/README.md`, `run_slots` and
`s17_op` in its `main.c`). Own code: `es2_shell_editors`
(`shell/include/es2/shell/editors.h`, in `shell/src/campaign.c`); the
screens §31.2 (ARMORY, `shell/src/gui/build_armory.c`) and §32 (CREW,
`shell/src/gui/crew.c`). Regression: `//shell:editors2_real_test`.

Team words: capacity `0x482b5c`, count `0x482b5e`, player record `0x482b60`
(bay word `0x482b82`), selected-pilot pointers of the three roster groups
`0x482b9b + 4g`; pilot record +0x22 bay, +0x24 active byte, +0x27 order.

### 17.1 The squad panel's selection `0x43dcdd(k)`

The only writer of the bay selection `0x482bc9` (L: the four stores
`0x43ddd1`, `0x43dee5`, `0x43df65`, `0x43e042`). Nothing when k is the
selected bay; otherwise by the screen word `0x4758b8` (L):

| Word | Rule |
| --- | --- |
| 2 WEAPONS (`0x43df86`) | k = -1 or a finished Herc (record and `0x411088`); slot selection `0x476658` := -1, `0x43fdac(0)` (WEAPONS list), then the selection |
| 3 REPAIR (`0x43dd1c`) | as §12.3: k = -1 or finished; selection, `0x434549(0, 0)`, labels, refresh, snapshot |
| 4 BUILD (`0x43df0b`) | any k; selection; refresh `0x447064` when k ≠ -1 |
| 6 CREW (`0x43ddf7`) | k = -1 or finished; **and** for k ≠ -1 with a wingman order selected (`0x477778` ≠ 0) not a Herc of vehicle 8 (`0x411164`: the record's vehicle word +0); selection; then, only when the selector flag `0x47665a` is set, the assignment `0x4426e5` and `0x43e167` |
| others (`0x43dfe8`) | any k; selection |

All cases end with the panel redraw (`0x43da1a(k)`, UI). The flag
`0x47665a` is 1 only inside the eight bay-selector handlers (`0x43e477`, …,
each `0x43dcdd(k)` between `:= 1` and `:= 0`) and inside CLEAR
(`0x44268e..0x4426db`); every other caller (shows, order rows, the scrap)
runs the case without the assignment. (L, V)

The shows of WEAPONS (`0x43fbd8`) and REPAIR (`0x43397c`) replace a
selection that is -1, without a record or unfinished by
`0x43dcdd(0x41124e(hangar))`. **Trap:** their bar buttons store the screen
word **after** the show (`0x43a90d`, `0x43aa0d`), so the case is the
previous screen's; after CREW the vehicle-8 rule can keep an invalid
selection (V: the targeted vehicle-8 cases). The own library supports the
REPAIR show after CREW. The CREW bar button `0x43ac5a` stores 6 **before**
its show (`0x43ad00`, then `0x442091`). ARMORY's `0x43ab62` stores 5 after
`0x449b87`. (L)

Own code: the BAY operation (one model of `0x43dcdd` for WEAPONS, REPAIR,
BUILD and CREW); the panel §28.

### 17.2 CREW: globals, show and order rows

- Order row selection `0x477778` (word, 0 in the image) and the pilots of
  orders 1..3 as pointers `0x47777c`, `0x477780`, `0x477784` (`0x477778 +
  4o`, 0 in the image). Leaving CREW (`0x44218a`) only hides, so both
  persist. (L)
- Show `0x442091` (L, V): colours of the four order rows by the capacity
  (`0x441ee7`: +0x59 of `[0x48d824 + 4o]` 0x25 below the capacity, 0x10
  from it; a colour field only, no +0x49 write, so the rows stay clickable,
  §32.2), then `0x442198`: for o = 1..3 the pointer := the selected pilot of
  the first group with order o (`0x4108fa`), else 0; the three group images;
  the order rows `0x442215(0)`, `(1)`, `(2)`, `(3)` and again `(0)`; the
  texts `0x4422df`; then `0x43dcdd(k)` for every bay k with a record
  (`0x411164` ≠ -1). The selection after the show is therefore the last
  finished bay when one exists. No SAV state changes. (V: 227 shows)
- Order row `0x442215(o)` (handlers `0x442a40`, `0x442aa1`, `0x442b02`,
  `0x442b63`): colours, then `0x43dcdd` of the player's bay (o = 0) or of
  the order pilot's bay (-1 without one) **with the order selected before**,
  then `0x477778` := o. (L, V)

### 17.3 CREW: pilot lists, assignment, CLEAR

- Team helpers (L): `0x4108fa(team, o)` the first group 0..2 whose selected
  pilot has order o, else -1; `0x4108e2(team, g, o)` that pilot's order :=
  o; `0x41094b(team, g, f)` active byte := f, the count +1 / -1 when it
  changes; `0x41098a(team, o)`: for g = `0x4108fa(o)` (nothing for -1)
  active iff o < capacity (signed) and the pilot's bay ≠ -1; `0x40ec80(k)`
  player bay := k; `0x40ec8f(g, k)`: pilot bay := k, then k = -1 →
  `0x41094b(g, 0)`, else `0x41098a(team, g)`. **Trap:** the group number is
  passed as the order (`push esi` at `0x40ecfb`); CREW calls it only with
  -1, so the trap is not reached from the UI.
- Pilot list `0x442548(g)` (handlers `0x4427e1`, `0x44288c`, `0x442937` for
  g = 0, 1, 2; L, V): nothing for order 0; else the group holding the order
  loses it (order -1, **bay and active byte kept**), group g gets it, g's
  pointer is removed from the other order slots and becomes the order's
  pointer, then `0x4426e5`. The lists choose a group's selected pilot, not a
  roster member.
- Assignment `0x4426e5` for the selection k (L, V): nothing when a wingman
  order has no pointer; the player's bay := -1 when it is k; every selected
  pilot with bay k gets `0x40ec8f(g, -1)` (bay -1, deactivated; with k = -1
  every pilot without a bay); then order 0 → player bay := k, else the order
  pilot's bay := k and `0x41098a(team, order)`. CREW thus also writes the
  team count (`0x41094b`).
- CLEAR `0x4429e2` → `0x442624` (L, V): nothing for order 0 or when no group
  has the order; else that pilot gets `0x40ec8f(g, -1)` and order -1, the
  order's pointer := 0, and `0x43dcdd(-1)` with the flag set (selection -1;
  the assignment returns at once).
- Reachable pairs (L, V): any selected pilot of the three groups can take
  any wingman order (also one at or above the capacity: then inactive), and
  any finished bay through the selectors, except a vehicle-8 bay while a
  wingman order is selected. A pilot list click moves the selected bay to
  the new order pilot. Pilots that lost their order but stay active with a
  bay are reachable states.

Own code: operations CREW_ORDER, CREW_PILOT, CREW_CLEAR; the screen §32.

### 17.4 ARMORY: rows, add, remove, Clear

- 26 rows; the category of row r is the word `0x476a4c[r]` (1, 2, 3, 4, 5,
  6, 22, 7, 23, 18, 8, …, 31, 32; read at run time; also the WEAPONS list's
  table). Row widgets `[0x48db28 + 4r]`, enabled (+0x49) iff the category's
  flag +0x16 is set (`0x4499b9`, at the show and after a weapons scrap).
  Each row handler (`0x44a746`, `0x44a7ba`, …) calls `0x449d2f(r)` when the
  event's button byte +0x24 is 1, else `0x44a06e(r)`. Add and remove are
  thus the two mouse buttons on a row. (L)
- Row selection `0x479210`: -1 in the image; the show `0x449b87` selects row
  0 (`0x449d2f(0)`), the leave `0x449c46` sets -1. (L, V)
- `0x449d2f(r)` (L, V): with weapons building manual (`0x4825c9` ≠ 0) on the
  selected row: when the bank's free count `0x46fa14` ≠ 0 and `0x412baa()` +
  the category's cost (+0x14, unsigned word) < salvage (unsigned, `jae` at
  `0x449d8b`): `0x412c0b(category)`: the first entry `0x46fa16[i]` that is 0
  takes it, free count - 1. Otherwise (auto, or another row) the row is
  selected. (V: salvage = cost and cost + 1 for every distinct cost.)
- `0x44a06e(r)` (L, V): with manual building on the selected row: n =
  `0x412c66(category)` - 1 (entries equal to it), Clear (`0x449ccc`), then n
  times `0x449d2f(r)` (each with the add rule): one entry fewer, re-added
  into the first free entries. Otherwise the row is selected.
- Clear `0x44a584` → `0x449ccc`: with a row selected, `0x412c31(category)`:
  every entry equal to it := 0, free count + 1 each. The Clear button
  `[0x48dad8]` is enabled only with manual building (`0x449b87`). (L, V)
- The "+/−" handlers `0x44a63b`, `0x44a694` (and `0x44a5e2`, `0x44a6ed`)
  have empty bodies; their widgets `[0x48dadc]`, `[0x48dae0]` carry texts
  (`0x44a33b`). (L)

Own code: operations ARMORY_ROW, ARMORY_CLEAR; the screen §31.2.

### 17.5 Weapons scrap

Scrap `0x44a508`: with a row selected, the warning `0x448227(category)`
(category in `0x48da50`, text 0xcd with `0x412c8e`). ACCEPT `0x448447`:
hide, `0x40ed6a(category)`: salvage += `0x412b79` = 1000 · `0x412c8e`
(signed), where `0x412c8e` is 0 for an empty list, else (cost / 1000 ·
count) / 10 (signed `idiv`) in 16 bits, at least 1; then `0x4125c1` frees
**every** item of the category; then `0x412a37` with `0x4825c9`, the row
enables and texts. CANCEL `0x4483e9` hides. The Scrap button has no enable
write. (L, V: 220 scraps, a single-item list per category for the minimum
1)

Own code: operation ARMORY_SCRAP; the warning §31.3.

### 17.6 The difficulty word `0x4826ea`

No code writes it: the only reference in the whole image is the read at
`0x40ef37` (its address bytes occur once in the file), no instruction has
an immediate in `0x4826e0..0x4826ed`, the neighbouring indexed arrays end
below it (the prefs callbacks `0x482608 + 4i`, i < 54 (`0x40dc50`), end at
`0x4826e0`; prefs indices are below 54), and no SAV or slot block covers it.
It is BSS, so bank[3] is always 0 in VSHELL; the own shell passes
difficulty 0 to the preparation (`es2_shell_prepare_options.difficulty`,
the default of `es2prep.request`). (L; V: the word is 0 in all 2,000+
`s17-state` records, after restored SAVs and every editor operation of the
test; the state inventory of vshell_state.md reports the same.)

### 17.7 Implementation and in-vitro acceptance

- `es2_shell_editors`: OPEN takes 2..6 (WEAPONS: only the selection step;
  ARMORY: row 0; CREW: the show), CLOSE leaves any editor (ARMORY: row -1),
  BAY works on WEAPONS, REPAIR, BUILD and CREW through one model of
  `0x43dcdd`, and the kinds ARMORY_ROW, ARMORY_CLEAR, ARMORY_SCRAP,
  CREW_ORDER, CREW_PILOT, CREW_CLEAR. The session holds the order, its three
  pilots (as roster groups), the ARMORY row and the weapons-building byte;
  the view holds the ARMORY numbers. The editor tables are 98 bytes (the 46
  of §12.7 and `0x476a4c`). A press on a disabled row or the disabled Clear
  is `applied` 0 (**H:** the button class ignores it, as §12.3).
- V (`//shell:editors2_real_test`): 218 sequences over the corpus of §12.7
  (GAME_R, GAME_T, a new career, the SAV after C1_01, three prepared
  states), 150 seeded edits (those of §12.7 plus pilot bays and activity,
  vehicle 8, capacity 1..4, bank entries and free count, small salvage) and
  targeted cases (a CREW tour of every order, pilot and bay, the vehicle-8
  shows, ARMORY tours with both building values, a weapons scrap per row on
  every corpus state and on single-item lists, the add boundary, capacities
  1..3): the SAV after every sequence equals the original writers', and
  after every CREW and ARMORY operation the selection, order, order pilots,
  ARMORY row and free count equal the original's globals; the warned
  category equals the own one. Eight one-line mutations of the library are
  each caught.
- Coverage: with §9–§13, §15 and this section every state-changing control
  of the service bay and the menus is a library function equal to the
  original in vitro. Not compared byte for byte with VSHELL: the RNG
  between steps (§16.5) and the `GAMEFILE.STR` flag rewrite (own model
  only).

### 17.8 Open points

- The `0x43dcdd` WEAPONS case's `0x43fdac(0)` is not run in vitro (§10.2's
  list); the trap of `0x40ec8f` is unreachable from the UI.
- The drawing (order row colours, panel images) is §32. Nothing here is
  checked live.

## 18. Shell flow as a state machine

The screen logic of sections 1 to 4 and 9 to 17 without its drawing is one
module, `es2/shell/flow.h` (`shell/src/flow.c` in `//shell:flow`): which
top-level screen and which modal box are shown, the screen word `0x4758b8`
with the leave dispatch `0x43a537`, the handlers' effects on the process
state, the boxes' decisions, the entry dispatch `0x40188a` and the exit save
of the loop end `0x402197`. It reads and writes no file and prints nothing.
`es2menu` (`shell/src/menu.c`) is its text presenter and its host; the
graphical screens of §23–§33 are a second presenter of the same flow.
Regression: `//shell:flow_test`, `//shell:menu_real_test`.

### 18.1 State

| Field | VSHELL | Initial value (how verified) |
|---|---|---|
| `mode` | mode word `0x4826f0` | prefs.cfg +0x2a at game init `0x40e751` (§9, vshell_state.md) |
| `final_word` | `0x4826f2` | 0 (vshell_state.md) |
| `game` | loaded flag `0x4826ee`, with the own menu's distinction campaign (final word 2 in the bay) / slot (loaded without a campaign) | none |
| `prefs` | the two prefs blocks `0x48259c` / `0x4825d2` (§11.4) | both from prefs.cfg (`0x40dc44`) |
| `screen` | which top-level screen is shown (§3) | main menu |
| `bay_word` | screen word `0x4758b8` | −1 (image word read at VA `0x4758b8`) |
| `caller` | caller mark `0x48d424` | 0 (BSS) |
| `slot` | selected slot `0x474fde` | 10 (image word) |
| `naming` | naming mode `0x474fdc` | 0 (image word) |
| `skill` | Registration skill `0x476248` | 0 (image word; never reset, §24.2) |
| `bay_select` | bay selection `0x482bc9` (hangar word +0x22) | 0 after a new game or load |
| `page` | mission-screen page word `0x481108` | 0 (Registration ACCEPT 0, §24.5; the results path's advance 2: 4, `0x40f62d`) |
| `box`, `box_value` | the shown box of §4.10 and its text selector | none |
| `debug_cheat`, `debug` | cheat byte `0x482368` (`-@`) and the DEBUG dialog `0x413305` waiting for its answer on a mode-1 path (§39) | 0, none |
| `quit`, `exit_code` | quit flag `0x46c268`, exit code `0x46e348` | 0, 0 (reset `0x401bce`) |

The image values were read from the reference `VSHELL.EXE`
through its section table (DATA raw data ends at VA
`0x481200`; `0x48d424` lies in the BSS). The game state itself stays the
engine's memory image (launcher.md "Shell state engine"); the editor state
is `es2_shell_editor_session` (§12, §17), the once-flags
`0x477946..0x477948` are `es2_shell_movies` (§19).

### 18.2 Events

An event is a VSHELL handler, named by its address (the `handler` fields of
`shell/data/vshell_layout.json`): `es2_shell_flow_event(flow, handler,
text)`. Its answer is DONE (the handler ran), IGNORED (VSHELL does nothing:
same bar button, disabled control, a control of a screen or box not shown),
REFUSED (the own menu's refusal, `flow.refusal`: no game, no campaign, the
slot holds no game) or FAILED (a host callback failed and reported why).
While a box is shown only its buttons and the Quit box's ACCEPT are taken.
Effects with I/O are host callbacks: `store_mode` (`0x40ec56` over
`0x40dfa3`), `write_prefs` (`0x40dd0a`), `save` (`0x40e933`), `load`
(`0x40eaaa`), `new_game` (`0x40e8a5` and Use Default), `campaign` (a loaded
slot with final word 2 becomes the campaign), `launch` (`0x445b99` checks
and `0x40f68c`), `rebalance` (`0x412a37`), `results` (`0x40f09f`),
`prepare_next`, `replay` (evidence log), `full_screen`, `debug_prepare`
(§39).

| Handler | Flow effect |
|---|---|
| INSTANT ACTION `0x431935` | mode 0 (the practice library's writes and the launch are the host's; `es2_shell_flow_game_created`, `es2_shell_flow_dbsim`) |
| START NEW GAME `0x431a08` | mode 1 stored; Registration |
| CONTINUE GAME `0x431a73` | mode 1 stored; selected slot 10; load slot 10: final word 2 → campaign, bay show; other → End box with that word, the game stays loaded; slot without flag → the final word in memory decides (End box, or the bay when a campaign is held) |
| SAVE/RESTORE `0x431b27` | mode 1 stored; caller 0; Saved games |
| PREFERENCES `0x431b9b` | Preferences |
| VIEW DEMO `0x431bfe` | exit 5, quit flag |
| QUIT `0x431db6`, Quit box ACCEPT `0x432237` | exit 0, quit flag (the box from every screen) |
| PRACTICE MISSIONS `0x431f3a` | mode 0; Practice (the show's prefs write is the host's) |
| SKILL `0x43c6ad` | skill := (skill + 1) mod 4 |
| Registration CANCEL `0x43c728` / ACCEPT `0x43c78b` | main menu / new game with the skill as rank: campaign, final word 2, page 0, bay show; a failure keeps what the engine loaded |
| row `0x437fef` (`es2_shell_flow_select_slot`) | selected slot |
| SAVE `0x438263` | enabled when not naming, slot below 10 and a game loaded (`0x43807b`): naming 1 |
| ACCEPT `0x43868a` / CANCEL `0x4384aa` | save(slot, row text), naming 0 / naming 0 |
| RESTORE `0x438393` | enabled for slot below 10 with its flag (`0x438126`), not while naming: load, save as slot 10, selected slot 10, bay show for every final word (2: the campaign; other: loaded without one) |
| EXIT `0x438424` | not while naming; selected slot 10; caller 0 → main menu; 8 → bay show (word −1); other → nothing |
| Preferences controls (`es2_shell_flow_prefs_control`) | the current block; Full Screen `0x437608` shows the Alert box only when windowed, its ACCEPT `0x437678` stores byte 6 (§22.2, §27.3) |
| Preferences CANCEL `0x437220` / ACCEPT `0x4372e1` | current := committed / commit and write the 54 bytes; main menu |
| Practice MAIN MENU `0x44c96a` | main menu (no revert) |
| bay MAIN MENU `0x43a66c` | ignored while the word is 0; leave, word 0, save slot 10, main menu |
| bay SAVE `0x43a768`, WEAPONS `0x43a862`, REPAIR `0x43a962`, BUILD `0x43aa62`, ARMORY `0x43ab62`, CREW `0x43ac5a`, MISSION `0x43ad5a` | ignored when the word equals the button's (1..7); otherwise leave once, store the word; SAVE also caller 8 and Saved games |
| leave `0x43a537` | words 2, 3, 4: `0x412a37` with prefs +0x2d of the current block; 5, 6, 7: no state |
| Rock & Roll `0x445b99` | campaign required; launched → exit 2, quit flag; refused → warning box with the index |
| End OKAY `0x44d60b`, warning OKAY `0x44da94` | hide the box |
| Replay Yes `0x44d1d4` / No `0x44d24d` | load slot 10 (campaign), exit 2 / save slot 10, main menu |
| ONLINE MANUAL `0x431e1b`, bay online `0x43a5bd` | `ES2_FLOW_ONLINE_MANUAL`, `ES2_FLOW_BAY_ONLINE`: prefs byte 6 := 0, the write, then the host's `online_manual` (§21.3) |

The bay bar is enabled by mode (`0x43b758`, §4.6): mode 0 enables WEAPONS
and CREW only (`es2_shell_flow_enabled`). `es2_shell_flow_start(flow,
state)` is the dispatch `0x40188a`: −X3/−X4 load slot 10 (11 in mode 0) and
run the results path with its decisions (`flow.outcome`: no slot, rejected,
Replay box with 3 or 0, next mission and bay, End box with 1 after the
slot-10 save, other mode, other return value), −X6 and the others the main
menu. `es2_shell_flow_finish` is the loop end: slot 10 saved when a game is
loaded; the exit code.

Read in the listing for the flow: EXIT `0x438424` (`0x438439..0x43845b`:
`movsx [0x48d424]`, 0 → `0x431718`, 8 → `0x43b7f2(8)` + `0x43b758`, other
values fall through); RESTORE `0x438393` (`0x4383a3..0x4383d5`:
`0x40eaaa([0x474fde], 1)`, `0x40e933(10, 0)`, `0x43a3f6`, `0x477946` := 0,
`0x43b7f2(8)` + `0x43b758`, no test of the final word `0x4826f2`). Every
other row restates §1 to §4.10, §9 to §12 and vshell_state.md (L there).

### 18.3 The text presenter

`menu.c` shows `flow.box` before `flow.screen`, one step per prompt, and
maps its numbered choices to the same events. Own-menu behaviour:

- RESTORE follows VSHELL: a slot with final word 2 becomes the campaign,
  any other is a loaded game without a campaign in the bay (Rock & Roll
  then refuses as for every non-campaign). `flow_test` covers final words
  0, 1, 3, 9; no `menu_real_test` session restores such a slot.
- Rock & Roll from the main menu (the text menu's shortcut) is taken on the
  main menu as well as on the mission screen.
- The end of the scripted input is the Quit box's ACCEPT on the main menu,
  the bay and Practice; CANCEL on Registration and Preferences; CANCEL and
  EXIT on Saved games; `es2_shell_flow_box_close` on the Replay box (the
  next main-menu prompt then quits). A RESTORE whose slot file cannot be
  read is answered with EXIT.
- Pressing a bar button equal to the word changes nothing in the flow; the
  text menu then shows that sub-screen's prompts again (WEAPONS, MISSION).
- CREDITS plays through the movie queue (§19; the text menu's choice 10);
  the CD prompt is `es2_shell_cd_prompt` (§20.2).

### 18.4 For the graphical presenter

A screen feeds the handler of the clicked control from
`shell/data/vshell_layout.json` into `es2_shell_flow_event` (Preferences
controls and the slot rows through their two functions), then draws
`flow.screen`, `flow.bay_word`, `flow.box` / `box_value`, `flow.skill`,
`flow.naming`, `flow.slot` and the prefs block; it stops at `flow.quit` and
returns `es2_shell_flow_finish`. The process frame (§35) owns the host
callbacks over `engine_io.h`. The boxes and the DEBUG dialog are §22, the
bay screens share the editors' session (§28–§32).

### 18.5 Open points

- Nothing here is checked live; the flow's decisions are as good as the
  sections they restate.

## 19. Movie queue, selection and player

Read from the listing of the reference `VSHELL.EXE` (`0x41e48c..0x41f1af`,
the enqueue sites, `0x404fb8`'s mouse branches, `0x40180c`, `0x43ae49`,
`0x444873`, `0x44472f`). In this section **V** = equal in vitro by
`//shell:movies_real_test` (the original functions on the mapped image),
**L** = listing only. Own code: `es2/shell/movies.h`
(`shell/src/movies.c`), the player `shell/src/win/mci_player.c`, driven by
`shell/src/gui/app.c`; input during playback and placement in es2shell:
§37.

### 19.1 The queue `0x41e8c0` and its records (V)

- Ring of ten 28-byte records at `0x485748`: +0x00 index (word, −1 =
  empty), +0x02..+0x11 four dwords of the rectangle, +0x12 palette word
  (−1 = none), +0x14 sequence dword, +0x18 callback dword. Write index
  `0x470fac`, play index `0x470fae` (words, both 0 in the image).
- Static initialiser `0x41f11e` (an init-table entry, no direct caller):
  the vector constructor `0x46b898` runs `0x41e848` on each record (index
  −1, rectangle `[0x47110c..0x471118]`, palette −1, +0x14/+0x18 := 0); the
  empty rectangle `0x47110c` is built from (0,0)-(0,0) by min/max, so zero.
- `0x41e8c0(index, x, y, w, h, palette, callback, sequence)` (dwords; the
  stored index and palette are the low words): nothing when the movie flag
  `0x482359` is 0 or `0x41e88f` finds the low word in **any** of the ten
  records. **Trap:** because empty records hold −1, an index −1 (or 0xffff)
  is never stored while one record is empty. Otherwise the record at the
  write index is overwritten without a full-ring check and the index
  advances modulo 10 (`inc`, `movsx`, `idiv 10`). `[ebp+0x24]` goes to
  +0x14, `[ebp+0x20]` to +0x18 (§14 calls +0x14 the "last argument").
- Every enqueue site passes callback 0; the player would call a nonzero
  +0x18 (`0x41f01c`) after the movie.
- The 86 names: the pointer table `0x470fb4` (indices 0..0x55, names
  `avi\pt1.avi` … `avi\dropship.avi`; V against the hash-checked EXE). An
  index above 0x55 would read past the table (no check; the library refuses
  it). The first player call (`0x471128` = 0) patches byte 2 of the two
  intro names (pointers `0x4710c4`, `0x4710c8`) to `i`, `f` or `g` by the
  language `0x48235e` (0, 1, 2): `avi\`, `avf\`, `avg\intr_pt1.avi`; the CD
  has `AVF\` and `AVG\` with exactly these two files. The path is the CD
  root `0x4823b9` + the name (`0x40d9e1`, drops a leading `X:`).

### 19.2 Selection rules (L; flags V)

| Event | Site | Enqueued (index, rectangle, palette, sequence) | Player |
|---|---|---|---|
| Start-up, entry mode `0x482362` not 3, 4, 6 (so `-X0/1/2/5`, no `-X`, and any other value) | `0x401955`, `0x401983` | 0x44 `intr_pt1`, 0x45 `intr_pt2`; full; −1; 0 | `0x41e98c(0)` at `0x40198f`, then pump, clear `0x4077ee`, palette 1 |
| CREDITS `0x431c7b` | `0x431d1a` | 0x54 `credits`; full; −1; 0 | `0x41e98c(0)` |
| Results path, end of campaign (`0x4133eb` = 1, `0x40f554`) | `0x40f58f`, `0x40f5bd` | after the slot-10 save: 0x53 `victory`, 0x54 `credits`; full; −1; 0 | `0x41e98c(1)` |
| MISSION `0x43ad5a` → page (`0x43ae49`): 0 while `0x477946` = 0 **and** position `0x46fc5a` = 0, else 1 | — | — | — |
| Mission screen page 0 while `0x477946` = 0 (`0x444a69`) | `0x444aa5`, `0x444ad5` | 0x46 + g − 1 `c<g>` (panel, palette 3, or 4 when g − 1 ≥ 4, sequence 0); 0x4b + g − 1 theatre (theatre rectangle, −1, sequence 1); then `0x477946` := 1 | main loop `0x41e98c(1)` |
| Page 1 while `0x477947` = 0 (`0x444cd6`) | `0x444d10` | briefing word `0x48419c`; panel; g − 1 + 5; 0; then `0x477947` := 1 | main loop |
| Page 4 while `0x477948` = 0 (`0x444dc8`) | `0x444e02` | debrief word `0x48419e`; panel; g − 1 + 10; 0; then `0x477948` := 1 | main loop |
| Theatre step `0x44472f` after a sequence record, g − 1 = 4 | `0x4447af` | 0x55 `dropship`; full; −1; 0; then `0x43a430(2)`, fade-out, focus lost | the same player run |
| Telecomm `0x4454b8` | `0x445527`, `0x44556a` | unreachable (§14.4) | — |

g is the CAREER group word `0x46fc58` (16-bit arithmetic, `si` = g − 1).
Rectangles after the sites' min/max normalisation, handed to `MoveWindow`
as x, y, width, height in the main window's client area (`0x41e5fa`): full
0x20,0x3c,0x240,0x168 (576 × 360 at 32,60, centred horizontally); panel
0x15,0x56,0xef,0xb3 (`0x444882`); theatre 0x122,0x43,0x127,0xe2
(`0x4448e6`). The once-flag is set even when the queue refused the movie
(movie flag 0, word −1, duplicate). The briefing word is the last word of
the SAV's selection state (mission.md, `0x48419c`), so a load restores it
(V: after `0x40eaaa` of GAME_R and GAME_T the word equals the own one, 1
and 0); the debrief word is the selected debrief record's +0x3e
(vshell_preparation.md "Debrief selection"), set by the results path and not saved.

Once-flag writers (L; the load V): `0x477946` := 0 by RESTORE `0x4383c6`
and ACCEPT `0x43c7a5`; `0x477947`/`0x477948` := 0 by the new game
`0x40e8b1`/`0x40e8b8` and every successful load `0x40eaaa`
(`0x40ec06`/`0x40ec0d`; a load of a slot without its flag returns before
them). The in-vitro load case passes the original `0x40eaaa` with the flags
set (both stay equal to the own ones); the new-game writers are L only (the
worker's new game cannot run after a movie op, §19.5).

Own code: `es2_shell_movies_intro`, `_credits`, `_victory`,
`_mission_page`, `_mission`, `_theatre`, `_new_career`, `_load`.

### 19.3 The player `0x41e98c(build_palette)` (V for the control flow)

1. Movie flag 0 → return. First call: the language patch (§19.1).
2. Record at the play index empty → `0x470fb0` := 0, return. Otherwise
   `0x470fb0` := 1 (stays 1 after the run), fade-out `0x42f7f0`, sound focus
   lost `0x42f6a8`, esi := 0.
3. Per record until an empty one: index 0x55 → esi := 0; `0x47111c`,
   `0x471124` := 0, `0x471124` := 1 for 0x44/0x45 (intro), edi := 1 for
   0x54 (credits); palette ≠ −1 and neither intro nor credits →
   `0x407b62(palette)`, `0x407c1b`.
4. Not the dropship and the bay bar `[0x48d528]` visible (+0x11 bit 1
   clear): the bay buttons `0x48d530..0x48d548` +0x45 := 0, `0x48d54c` := 1
   with redraw; show the trees `[0x48d8e0]` (page 1 only), map frame
   `[0x48d8b4]`, title `[0x48d8b8]`, `[0x48d8bc]`; drain `0x402266`;
   repaint trees (page 1), map frame, title; `0x445343(page)`; drain.
5. Busy cursor on. A latched intro skip (`0x471120`, intro record) skips
   the movie; otherwise `0x41e51b(build_palette)`: with 1 a palette of
   screen entries 10..245 (`0x41e48c`, `CreatePalette` → `0x485744`, the old
   handle never deleted), then `open avivideo`; failure skips the movie
   silently.
6. Until a movie window opened or the quit flag: `0x41e640` (§19.4).
   Failure of an intro: leave full screen `0x4077df`, prefs +6 := 0 and
   write, `error.str` message 5 (`0x401a81(5, 0, 0x30)`), sound off
   `0x42f7c2`, quit flag, return (the record stays). Failure of any other
   movie: the CD prompt `[0x48d1e8]` is shown, `0x470fb0` := 0, drain until
   Retry (`0x473bcc`, set at `0x432516`) or the quit flag, `0x473bcc` := 0,
   `0x470fb0` := 1, retry (§20.2, hook `ES2_SHELL_MOVIE_CD_PROMPT`). Quit
   flag → return without fade-in.
7. `close avivideo` (`0x41e5bb`); full screen (`0x46d878`) and intro or
   credits → clear `0x4077ee`.
8. Busy cursor off, pump `0x406328`, pump until the focus flag `0x46c288`.
9. Bay bar visible: repaint trees (page 1), map frame, title,
   `0x445343(page)`; the dropship adds `0x43a430(1)` and a repaint of the
   root. Bar hidden and not full screen: hide and show the root.
10. Credits → palette 1. Sequence record: teardown `0x445095`, MISSION
    button `0x48d54c` +0x45 := 0 with redraw, screen word `0x4758b8` := −1,
    focus gained `0x42f5d3`, fade-in `0x42f894`, esi := 1, theatre step
    `0x44472f` (which queues the dropship in theatre 5 behind the current
    record, so the same run plays it).
11. Neither intro nor credits → drain. Theatre panel `[0x48d8c0]` visible →
    `0x4022c5(2)` (two seconds, no pumping), `0x44483e`, repaint the bay
    `[0x48d520]`, palette 1. Callback +0x18 if nonzero.
12. The record is emptied (index −1, rectangle `0x47110c`, palette −1, 0,
    0), the play index advances modulo 10.
13. After the loop: esi = 0 → focus gained `0x42f5d3`, fade-in `0x42f894`
    (so a run ending with the theatre picture gets no second fade-in; the
    dropship clears esi again).

V with the device trapped closed (`0x41e51b` returns 0, so steps 6 and 7
are not reached) over 44 runs: the ring, indices, flags and the order and
arguments of the recorded calls (`0x42f6a8`, `0x407b62`, `0x407c1b`,
`0x445343`, `0x41e51b`, `0x406328`, `0x43a430`, `0x445095`, `0x42f5d3`,
`0x44472f`, `0x4022c5`, `0x44483e`) equal the library's. Steps 6 and 7 and
§19.4 are L only.

Own code: `es2_shell_movies_play` (the actions as VSHELL addresses; the
caller draws, plays sound and MCI).

### 19.4 One movie `0x41e640` and the window procedure (L)

`wsprintfA("open %s alias mov style child parent %d", path, [0x481f38])`,
`mciSendStringA`; failure → assertion level 3 (`src\AVI.CPP` line 0xed),
return 0. `status mov window handle` into a 500-byte buffer: success →
`_atol`; **trap:** the `_scanf` call after it passes the reply as the format
and reads nothing; failure → `mciGetErrorStringA` and `MessageBoxA(main,
text, NULL, 0x30)`, the handle stays uninitialised. `MoveWindow(handle, x,
y, w, h, TRUE)`. `setvideo mov palette handle to %d` with `[0x485744]` (0
until a player run with 1 built one); failure → box titled "string palette
setting error". Then `0x470ea0` := 0, busy cursor off, `SetCapture(main)`,
`0x470eb0` := 1, `play mov window notify` with the main window as callback,
`0x40586d` on the keyboard device `[0x48e244]` (empties the key ring, §37)
and `0x46a7e4`, and loop: pump; key ring code (low byte) 1 (Esc) or 0x39
(Space) → `0x470ea0` := 1 and, during an intro, `0x471120` := 1. `close
mov` (error box "close avi "), `ReleaseCapture`, `0x470eb0` := 0, return 1.

The window procedure ends the movie on `MM_MCINOTIFY` with
`MCI_NOTIFY_SUCCESSFUL` (`0x4057f2`) and on `WM_LBUTTONDOWN` (`0x405145`)
and `WM_RBUTTONDOWN` (`0x4050e0`), which also latch the intro skip; with the
capture every click reaches the main window. Only the two button-down
messages set the skip flags; `WM_LBUTTONUP` (`0x4051d1`) and
`WM_RBUTTONUP` (`0x4051aa`) only check `0x470eb0` (vshell_process.md §7
says every button message sets them; the listing does not).

### 19.5 Implementation and acceptance

- `es2/shell/movies.h`: names and paths, the queue, the selection rules and
  the player's control flow `es2_shell_movies_play`. `//shell:movies_test`.
- `shell/src/win/mci_player.c`: the MCI part (`OPEN_DEVICE`, `PLAY`,
  `CLOSE_DEVICE`, `PUMP`, the CD-prompt and intro-error hooks) with the same
  command strings; before the first `open avivideo` it binds IV32 in the
  process (`es2_iv32_system_drivers`, `es2_iv32_bind`; no release; DBSIM is
  unaffected). `//shell:mci_player_test` covers the portable parts; no test
  opens a window or plays a movie. es2shell (`app.c`) drives it with its own
  window (§37).
- `//shell:es2movie` (manual tool): a window like VSHELL's (640 × 480
  client, style 0xce0000) and one event through the library and the player,
  every action printed.
- Text menu (`es2menu`): queues what VSHELL queues and prints `[movie
  <path>]` where the player would play: the intro before the entry
  dispatch, CREDITS (entry 10, ESTEXT 0xd), victory and credits for the flow
  outcome WON, MISSION's page-0 and page-1 movies (the text menu shows page
  1 right after page 0, so the briefing movie follows at once; the words
  from the engine's memory state), the debrief movie for the outcome NEXT
  (the engine's results output, `selected.aux`). The once-flag writers sit
  in the host's load and new-game callbacks and after a RESTORE the flow
  did. `-a` (VSHELL's flag) and `--no-movies` clear the movie flag.
  `menu_real_test` checks the intro, CREDITS, `-a`, `--no-movies` and the
  bay-mission transcript (intro, `c1`, `alph_th`, the briefing and the
  debrief movie).
- V (`shell-invitro` slot-mode ops `movie-init`, `movie-flag`, `movie-once`,
  `movie-enqueue`, `movie-play`, `movie-dump`): `movies_real_test` compares
  the name table, 4 fixed and 40 seeded runs (615 dumps, 1,056 recorded
  calls) and the once-flags/briefing words after the load. **Trap:** the
  busy cursor `0x408d2f` is `__stdcall` (`ret 4`) and the player has no
  `mov esp, ebp`: a quiet trap would leak four bytes, so `movie-play`
  replaces it by `ret 4` for the call. Every op first passes `s17_op`, which
  arms the editor traps (among them the ESTEXT lookup `0x4087f0`); a later
  `new:` then faults, so the new-game writers are not run in vitro. Five
  one-line mutations of `movies.c` are each caught.

**Deviations (platform).** The movie window is placed in the client area of
the caller's window through the caller's frame rectangle (§37; VSHELL's
window is 640 × 480 unscaled); the player waits for messages instead of
polling; a newly built palette replaces and deletes the previous one
(VSHELL leaks it). With the policy `cd_check = skip` (§20.3) an AVI that
does not open is skipped without the prompt.

### 19.6 Open points

- Playback, picture, sound, window position and the skip keys in the own
  player are checked by hand; nothing here plays a movie.
- Steps 6, 7 and §19.4 (MCI, the CD prompt, the intro error) are L only.

## 20. Start-up CD check and CD prompt

Own code: `es2_shell_cd_check`, `es2_shell_cd_prompt` and
`es2_shell_cd_policy_parse` (`es2/shell/movies.h`, `shell/src/movies.c`);
the box `es2_boxes_cd_prompt` (§22, `shell/src/gui/boxes.c`). Tests:
`//shell:movies_test` (synthetic probe and box, no CD needed) and three
`//shell:menu_real_test` sessions.

### 20.1 Start-up check (`0x401cac..0x401d28`)

Described in [vshell_process.md](vshell_process.md) row 13: VSHELL builds
the CD root (with its trailing backslash) plus `avi\pt1.avi` (the first name
of the movie table `0x470fb4`) and opens it with the VOL-aware read stream.
On failure it leaves full screen, shows `error.str` message 5 and quits with
exit code 0; on success the stream stays open until the main loop ends
(`0x40224e`). The own code takes a probe callback; `es2menu` opens the file
with `fopen` and keeps it open until its exit. The check comes before the
game init in both.

### 20.2 CD prompt (`0x4322a8`)

The box (ESTEXT 0x153 title and text, buttons 0x154 Continue and 0x155
Quit; layout in `vshell_layout.json` `cd_prompt`) is shown modally at
`0x4323c9`. Continue `0x4324fa` hides the box window `[0x48d1e8]`
(`0x41fabd`) and sets the done flag `[0x473bcc]` := 1; Quit `0x432569` sets
the quit flag `[0x46c268]` := 1 (L). The player `0x41ede0` shows it when a
non-intro AVI does not open and retries the open after Continue (§19.3 step
6, hook `ES2_SHELL_MOVIE_CD_PROMPT` at `0x41edda`); its wait ends on
`[0x473bcc]` or the quit flag (§22.1). `es2_shell_cd_prompt` returns 1 for
Continue and 0 plus the quit flag for Quit; the box drawing is §22.

### 20.3 Policy switch

`menu.ini` key `cd_check` in `[Menu]`: `original` (the default, VSHELL's
behaviour) or `skip` (installations whose videos live elsewhere,
[setup.md](../reference-game/setup.md#the-original-installer-and-the-portable-layout)):
no start-up check, and a movie that does not open is skipped without the
prompt and without the quit flag. Any other value makes `menu.ini` invalid.
`skip` is a documented deviation option.

### 20.4 Text menu

`es2menu` runs the check with `menu.ini`'s `cd_root`; without a `cd_root` it
has nothing to check and starts (deviation: the text menu also serves
installations without CD data). It plays no movie, so the prompt is never
reached there.

### 20.5 Open points

- Nothing here is checked in vitro or live.

## 21. ONLINE MANUAL and the help routine

The main menu's ONLINE MANUAL button (ID 0xf, handler `0x431e1b`, registered
at `0x4314ee`) and the service bay's online image (`0x43a5bd`) both run the
help routine `0x431e79`. **L** from the listing (`0x431e1b..0x431f39`,
`0x43a5bd..0x43a66b`, `0x407953`, `0x408d69`, `0x40dd0a`, `0x40dd6e`,
`0x40ddfe`, `0x40d9e1`); the prefs, path and WinHelp steps **V** (§21.4).
Own code: the flow events (§21.3), `shell/src/win/help.c`
(`es2_help_show`), the hosts `shell/src/menu.c` and `shell/src/gui/app.c`.

### 21.1 The handlers

| Entry | Address | Effect (L) |
| --- | --- | --- |
| ONLINE MANUAL | `0x431e1b` | frame with the exception record `0x473918`, `call 0x431e79`, then the event object it was given is destroyed (the same epilogue as every handler); **no state besides the routine's** |
| online image (bay bar) | `0x43a5bd` | loop-started flag `0x46c280` = 0: the object is destroyed and nothing else happens; otherwise `call 0x431e79` and the same epilogue. The flag is set in every loop iteration (`0x4020b8`), so the flow always takes the second branch |

The screen does not change: after the manual is shown VSHELL is on the
screen it was on (main menu, or whichever bay sub-screen with the word
`0x4758b8` unchanged) (L: neither handler writes a screen variable).

### 21.2 The routine `0x431e79`

In order (L):

1. `0x4077df`: when the display word `0x481f4c` is not 0, leave full screen
   (`0x407636`). Presenter work.
2. `0x40ddfe(6, 0, 1)`: preference byte 6 (`0x4825a2`, display) := 0. The
   callback table `0x482608` is all null, so nothing else runs (see
   [vshell_process.md](vshell_process.md), Alt+Tab).
3. `0x40dd6e(0)`: the committed block `0x4825d2` := the current block
   `0x48259c`, **all 54 bytes** (the loop compares and copies byte by byte,
   `0x36` iterations; with argument 0 no callbacks).
4. `0x40dd0a`: write the current block (54 bytes) to `data\prefs.cfg`
   (install root `0x40d99d`); a stream whose open failed is skipped and the
   routine goes on.
5. `0x42f8ff` (a stub for `0x42f6a8`): the sound focus is given up, music
   and effects stop ([audio.md](audio.md)).
6. `0x408d69`: read the first byte of `data\language.cfg` (relative to the
   process; the stream `0x44ed84`, one byte). An open failure reaches an
   assertion (`0x44e560`, severity 4, file `src\utility.cpp`, line 0xb7,
   text `data\language.cfg`) before the byte is used (see below).
7. The byte minus `0x45` selects (L `0x431eac..0x431efa`): `E` →
   `english\es2guide.hlp` (`0x47398d`), `F` → `french\es2guide.hlp`
   (`0x4739a2`), `G` → `german\es2guide.hlp` (`0x4739b6`). Any other byte,
   lower case included, reaches the assertion at `0x431efc`
   (`src\WMAIN.CPP`, line 0x104, text "online help not found.", severity 4).
8. `0x40d9e1(path)`: the CD root `0x4823b9` (drive.cfg's first token plus a
   backslash) concatenated with the path; `0x407953(path)`:
   `WinHelpA(window [0x481f38], path, 3, 0)` (import `WinHelpA`, thunk
   `0x46bbf2`; command 3 is `HELP_CONTENTS`). Nothing is done with the
   result.

**The severity-4 assertion** (L `0x44e560`, `0x44e7fc`, `0x44e6a8`,
`0x44e764`). A missing or invalid `language.cfg` is not a soft error in
VSHELL: `0x44e560` runs the notifier chain `0x44e53c`, then the report
`0x44e7fc` for severity 4 (`err`): the line `Assert::err: Line <N> in
"<file>"` with `: <text>` appended (format strings at `0x47a689` and
`0x47a6a5`) is appended to `assert.log` in the current directory
(`0x44e764`: open flags `0x901`; the first line of a process is preceded by
78 dashes and CR LF, every line ends in CR LF) and shown in a `MessageBoxA`
with the caption `Error` (import slot `0x5de41c`, type 0, no owner), then
`exit(1)` (`0x44e5b0`). The loop end's save of slot 10 (`0x402197`) does not
run. The launcher restarts VSHELL with `-X1` after exit code 1
([launcher.md](launcher.md#start-loop)). The two messages are
therefore fixed texts: `Assert::err: Line 183 in "src\utility.cpp":
data\language.cfg` (file did not open) and `Assert::err: Line 260 in
"src\WMAIN.CPP": online help not found.` (byte not `E`, `F`, `G`). An empty
`language.cfg` opens, the one-byte read finds nothing, and the byte is
whatever the stack held (not reproducible); the own shell treats it as not
`E`, `F`, `G`. The three manuals exist on the CD (`ENGLISH`, `FRENCH`,
`GERMAN`: `ES2GUIDE.HLP`, 9.0 MB each; listing of the extracted CD).

### 21.3 The flow and the own entry

`es2_shell_flow_event` takes `ES2_FLOW_ONLINE_MANUAL` (`0x431e1b`, main
menu) and `ES2_FLOW_BAY_ONLINE` (`0x43a5bd`, service bay); both run steps 2
to 4 itself (`es2_shell_prefs_control(WINDOW)`, `es2_shell_prefs_accept`,
host `write_prefs`; a failed write is reported as `ES2_FLOW_FAILED` but the
routine goes on, as VSHELL does) and then the host callback `online_manual`
(steps 5 to 8). A modal box blocks the events, as it blocks every control;
the main-menu button is ignored in the bay and the other way round.

The host uses `shell/src/win/help.c`: `es2_help_show` reads the language
byte, joins the path (CD root from `menu.ini` `cd_root`, a missing trailing
backslash added) and opens the file through the program registered for
`.hlp` (`ShellExecuteEx`, verb `open`, no error dialogs, the viewer is not
waited for). The text menu offers it as choice 11 (10 is CREDITS, §19).

**Deviation (platform).** Current Windows has no WinHelp viewer by default
(general platform knowledge, not checked here), so the own shell does not
call `WinHelpA`; it opens the file with whatever is registered and names the
file when that is not possible. The switch is `menu.ini` `[Menu]
online_manual = open | text | hide`, in the style of `cd_check` (§20.3;
lower case, absent or empty means *open*, any other value makes `es2menu`
refuse to start with the "menu.ini is missing or incomplete" answer):

| Policy | Entry | Effect |
| --- | --- | --- |
| `open` (default) | offered | prefs written, then the registered handler is started; the shell prints the file or why it could not be opened |
| `text` | offered | prefs written, the file is only named (the tests use it: nothing is ever started) |
| `hide` | not offered | no event, nothing written |

Calling `WinHelpA` itself is not offered: it needs `winhlp32.exe`, which is
not part of current Windows. The assertions of steps 6 and 7 are
**VSHELL's behaviour and reproduced, not deviations**: the own shell prints
the report line on the console (the text menu has no message box), appends
it to `assert.log` in the game directory exactly as `0x44e764` does, ends
the process with exit code 1 and skips the loop-end save. They apply to
every policy except `hide` (the language is read before the policy
matters). The leave of full screen (step 1) and the sound focus loss (step
5) belong to the presenter and are no-ops in the text menu; the graphical
presenter calls them around the flow event, the leave before it, and shows
the report in a message box.

### 21.4 Verification

- `//shell:help_test`: policy parsing, the language byte to the path
  (exactly `E`, `F`, `G`: lower case and every other byte refused), the
  root joins, every outcome (opened, no viewer, missing, text only, no file,
  bad language, too long, hidden) over injected operations, the two
  assertion texts; no viewer is ever started.
- `//shell:flow_test`: both events, the prefs write before the manual, a
  failed write, the committed block, the bay and main-menu variants, modal
  box, quit flag, missing callbacks.
- V (`//shell:help_real_test`, needs `ES2_DATA_ROOT`): slot-mode operation
  `help[:ROOT]` of `es2shellprep` calls `0x431e79` itself in the mapped
  original with `0x407953` and `0x42f8ff` trapped. 36 seeded sequences
  (prefs files missing, short, 54 bytes, longer; controls, Accept, Cancel,
  the display word; one or two help calls, the bay's variant on the own
  side) for `E`, `F`, `G` and five CD roots: both prefs blocks, the
  `data\prefs.cfg` bytes and every path handed to `0x407953` equal the own
  ones (55 calls). 3 of 3 mutants caught (byte 6 not cleared, committed
  block not updated, a wrong German path). The `WinHelpA` call inside the
  wrapper `0x407953` (window, command 3, data 0) is L only; the
  invalid-language assertion ends VSHELL and is not run in vitro (the report
  texts, the dash rule and the exit code are L only).
- `//shell:menu_real_test`: the text menu's choice 11 with the policy
  `text`: byte 6 cleared and the rest of the file unchanged (also a short
  file), the committed block, the three languages, a missing manual, the
  assertion for a language byte that is not `E`, `F`, `G` and for a missing
  `language.cfg` (console line, `assert.log` with the dash rule once, exit
  code 1, no slot save, prefs already written), policy `hide`, a refused
  policy value, the real CD's three manuals.

### 21.5 Open points

- Nothing here is live: the manual has not been opened by the own shell on
  a Windows 10/11 machine; the viewer for `.hlp` files on Windows 10/11 is a
  platform matter (manual check: choice 11 once).
- Whether the original's WinHelp shows its window on top of a full-screen
  display after step 1 was not tested.

## 22. Modal boxes, the DEBUG dialog and the keys

The boxes of §4.10 and the DEBUG dialog of §4.11 as graphical screens over
the flow (§18) and the toolkit ([vshell_widgets.md](vshell_widgets.md)).
Own code: `es2/shell/gui/boxes.h`, `shell/src/gui/boxes.c` (in
`//shell:screens`), the registration in `shell/src/gui/screens.c`, the
wiring in `shell/src/gui/app.c` (`es2_boxes_sync`, `es2_boxes_pass`,
`es2_boxes_system`, `es2_boxes_cd_prompt`). Static facts are **L**; nothing
here was run in vitro or live.

### 22.1 Shows and handlers

| Box | Show (L) | Buttons (L) |
| --- | --- | --- |
| Quit `0x431fa8` | Alt+F4 `0x405283..0x40528f` and Ctrl+Q `0x409c60..0x409c72`: capture released, modal show `0x40b7e9([0x48d1dc])` | CANCEL `0x4321c1`: modal hide `0x40b808` only. ACCEPT `0x432237`: quit flag only; the box stays shown and the exit code is not written |
| End of game `0x44d2bc` | `0x44d55f(w)`: label `[0x48dcbc]` := 0x130 (w = 0), 0x131 (1), 0x12f (3), unchanged otherwise, by `0x40b8bb(label, text, 2, 0x29, 1)`; modal show `[0x48dcb8]` | OKAY `0x44d60b` → `0x44d5fe`: modal hide |
| Launch warning `0x44d66c` | `0x44d90c(n)`: `[0x48dcd0]`/`[0x48dcd4]` := 0x13c + 2n / 0x13d + 2n for n = 0..3 (jump chain at `0x44d913`), unchanged otherwise; modal show `[0x48dccc]` | OKAY `0x44da94` → `0x44da87`: modal hide |
| Replay `0x44cdac` | `0x44d0e7(n)`: `[0x48dca4]`/`[0x48dca8]` := 0x137/0x138 (n = 0), 0x135/0x136 (3), unchanged otherwise; plain show `0x41f93a` of the bitmap `[0x48dc9c]`, then of the panel `[0x48dca0]` (no shield) | Yes `0x44d1d4`: hide `0x44d1b9` (bitmap, then panel), load, exit 2, quit flag. No `0x44d24d`: save, hide, main menu `0x431718` |
| CD prompt `0x4322a8` | movie player `0x41edda`: plain show of the **shield** `[0x48d1e8]` (the box is its pending child after the constructor's modal show `0x4323c9` and hide `0x4324e6`), gate `[0x470fb0]` := 0; then `0x402266` until `[0x473bcc]` or the quit flag; `[0x473bcc]` := 0, gate := 1 (`0x41ee08..0x41ee10`) | Continue `0x4324fa`: hide of the shield, `[0x473bcc]` := 1. Quit `0x432569`: quit flag only (the box stays; the player leaves at `0x41ee31`) |
| DEBUG `0x44dd38` | `0x413305`: `0x44e1b5` in both branches (default label `[0x48dcec]` := the CAREER entry name at `[[0x46fc54] + 8·group + 4] + 4·position`, NULL when the position is not below the group's count; plain show of the bitmap `[0x48dcdc]`, then of the panel `[0x48dce0]`); without `-@` two synthetic mouse events (codes 2, 1, +0x25 = 1) on Use Default `[0x48dcfc]` | Use Default `0x44dbea`: hide `0x44e217`, then `0x44db5c`. ACCEPT `0x44dc4d`: hide, then `msn\%s.msn` of the field text (§15.6) |

`0x402266` (L, `0x402266..0x4022c4`): up to 24 rounds; each first tests the
quit flag, pumps (WM_QUIT ends it) and, only while focused, polls input,
runs the event step `0x46848c`, applies a pending palette and flushes the
dirty rectangles. It never runs the movie or map steps or step 10.

### 22.2 The Preferences Alert box

Full Screen (`preferences.22`, handler `0x437608`) shows the shield
`[0x48d3bc]` (`preferences.01`; the ESAlert `preferences.02` is its pending
child) **only while windowed** (`[0x481f4c]` = 0, `0x437618..0x437628`); in
full screen it does nothing (L). The graphical handler asks the flow only
when it shows the box (the flow's rule, §18.2). The box has one button,
ACCEPT `0x437678` (`preferences.05`, label ESTEXT 16): hide of the shield,
the display toggle `0x407636` (only `[0x481f4c]` and the mode switch, no
prefs write), `0x437200(1)`: word `0x474d66` := 1, `0x40ddfe(6, 1, 1)`
(byte 6 of the current block), refresh `0x436e5d`. Window (`0x437597`)
leaves full screen through `0x407636` when set and stores 0 the same way
(§27.3).

### 22.3 Main-loop step 10: the Quit-box repaint

`0x402151..0x40217b` (L): when the Quit box's +0x11 bit 1 is clear, the
counter `0x46c294` (image value 0) is incremented; above 50 it is reset and
`0x41f3ed([0x48d1dc])` posts redraws for the box and its children: the
first repaint comes on the 51st pass with the box shown, and the counter
keeps its value while the box is hidden. Step 10 runs only in focused
passes that no earlier step ended. `es2_boxes_pass` has the shape of the
map hook (step 6, [vshell_process.md](vshell_process.md)); steps 7 to 9
between them have no effect in the own shell, so the order is the same.

### 22.4 Keys in the boxes

- No screen registers a key hook: `0x404d56` is called at `0x4093f4` (the
  hot-key hook `0x4094ee`) and `0x404b4e` (`0x404c03` with an empty byte
  list); `0x404db5` at `0x409424` (L). Command events (type 0x100) are in
  the event mask of the ESDialog class only
  ([vshell_widgets.md](vshell_widgets.md)), so in a box Esc, Tab, Ins, Del,
  Home, End, PgUp and PgDn do nothing; the arrows move the pointer by 8,
  Enter is a left click at the pointer (WinConsumer), Ctrl+Q and Alt+F4
  show the Quit box (showing a shown box does nothing). Enter over ACCEPT
  in the Quit box quits (unit-tested over the own toolkit; **H** for
  VSHELL).
- **Hot keys with Shift, Ctrl or Alt**: the hot-key hook tests only the low
  byte (`0x4053ab`, `and bl, 0xff`), the command byte is read at
  `0x46e5a9 + word` with the whole word (`0x409587`, `0x409602`). Read from
  the reference file for the 15 reachable hot keys, seven modifier sets,
  down and up (210 combinations; Alt+Esc, Ctrl+Esc, Alt+Tab and Alt+Enter
  never reach the hook, 202 words remain): 10 give 0xff (no event), 79 give
  0x00, 113 other bytes. The observable ones: Shift+Esc down and
  Shift+Ctrl+PgUp up give 0x05 (pointer up 8), Shift+Ctrl+Alt+Home up gives
  0x04 (pointer left 8 and, in an active edit field, the last character
  deleted); none gives 0x06, 0x07, 0x0a, 0x1d or 0x11. Every byte other
  than 0xff is also a command event that fires an edit field's handler when
  the field is the hover window (the Saved games rows `0x438873..`,
  Registration `0x43c47e`), as a plain hot key does. The bytes lie in DATA
  among RTTI names and vtables; no absolute write to them was found. The own
  shell reproduces this with the bytes read from the user's `VSHELL.EXE` at
  run time (§40).

### 22.5 Own choices

- Box texts, fonts and the background come from the host (`es2_boxes_host`:
  ESTEXT entries, the DEBUG dialog's literals of `VSHELL.EXE`,
  `dfn\font2.dfn`, `dbm\bay2a_84.dbm`); nothing of them is stored in code.
- The Quit box's ACCEPT and the CD prompt's Quit set the shell's quit flag;
  the flow gets `ES2_FLOW_QUIT_ACCEPT` (exit code 0) or `flow.quit` only.
  VSHELL leaves the exit code as it is; it is 0 in every state where these
  buttons can be clicked (a handler that sets 2 or 5 also sets the quit
  flag).
- `es2_boxes_sync` shows the End, warning and Replay boxes when `flow.box`
  names them (after CONTINUE, Rock & Roll, the results path). The Use
  Default preparation stays in the flow's `new_game` / `prepare_next`
  callbacks, so the DEBUG dialog's `debug_default` callback is the
  presenter's (empty when the flow already prepared). The `-@` path (dialog
  open, typed name) is the flow's `debug` state (§39).
- The CD prompt's wait is blocking (`es2_boxes_cd_prompt`, the answer of
  `ES2_SHELL_MOVIE_CD_PROMPT`, §20.2); the gate is left at 1, as VSHELL
  leaves it for the rest of the movie.

### 22.6 Verification

`//shell:boxes_test` (synthetic font): construction of the six screens and
their states, the registration, every button through clicks on the input
path (Alt+F4, Esc, CANCEL, Ctrl+Q, Enter over ACCEPT, the End and warning
texts for every value, Replay No and Yes with the flow's save and load, the
CD prompt's Continue and Quit with a scripted pump, the drain helper's 24
rounds, WM_QUIT and the focus gate, the DEBUG dialog with and without `-@`,
the Preferences Alert box windowed and in full screen), the repaint on the
51st pass. `//shell:screen_test --suite boxes`: synthetic cases and cases
with `ES2_DATA_ROOT` (the CD's font, English ESTEXT, the bay2a_84
background and palette.dpl); the cancelled Quit box and the closed End,
warning and CD boxes restore the frame exactly (ESAlert's save-under).
Mutations caught: the End line for word 3, the 51-pass threshold, the
warning's second line, the posted clicks with `-@`.

### 22.7 Reference capture states

States a capture of the original needs for the boxes (§33):

- `quit_box`: shown over the main menu by Alt+F4 and by Ctrl+Q; after
  CANCEL (equal to the main menu before).
- `end_of_game_box`: CONTINUE GAME on a slot 10 with final word 0, 1, 3
  (and without a flagged slot 10: word 0 from the image).
- `launch_warning_box`: Rock & Roll with n = 0, 1, 2, 3 (§11.1's states).
- `replay_box`: `-X3` after a mission the player lost by death (3) and by
  advancement 0 (0).
- `debug_dialog`: `-@` after START NEW GAME's ACCEPT (shown; after typing
  `TRAIN1`).
- `cd_prompt`: a movie whose AVI cannot be opened (needs a clone without
  the file).
- `preferences`: Full Screen while windowed (the Alert box shown).

The alert boxes are drawn over whatever screen opened them; the own real
cases draw them over a cleared frame, so only the box rectangles compare.

### 22.8 Open points

- Every claim above is static; the handlers' window effects are unit-tested
  on the own toolkit, not run in vitro (they need VSHELL's window objects).
- Enter over ACCEPT in the Quit box (§22.4) is **H** for VSHELL.

## 23. Main menu screen

The graphical main menu `0x430fc4` on the widget toolkit as a presenter of
the flow (§18). Own code: `es2/shell/gui/main_menu.h`,
`shell/src/gui/main_menu.c` (in `//shell:screens`, because the registration
table `screens.c` names its build function). Read in the listing:
`0x431718..0x431fa4`, the entry dispatch `0x4018c6..0x4019c5` and the
results-path ends `0x40f4ae..0x40f670` (**L**). Own tests:
`//shell:main_menu_test` and the `main_menu` suite of
`//shell:screen_test`. Nothing here was run in vitro.

### 23.1 Objects and the first appearance (L)

- The 25 controls ([vshell_layout.json](../../shell/data/vshell_layout.json)):
  `[0x48d1a4]` an ESWindow on the display root (only CREDITS shows it),
  `[0x48d1a0]` the ESAnim2 with the six frames and handler `0x431830`,
  `[0x48d1a8]` the full-screen ESBitmap of `[0x46de0c]` (`bay2a_84`),
  `[0x48d1ac]` the ESTitle (ESTEXT 2) with the ten ESButtonFonts and their
  labels.
- The main menu's first appearance is the **animation**, not the panel:
  `-X6` (`0x4018c6`: music fade-in, pump, clear, gate `0x46c274` := 1,
  palette 1, show `[0x48d1a0]`), the intro path (`0x40198f..0x4019bc`, the
  same after the two movies) and the results path's ends `0x40f5da` (end of
  campaign, after victory and credits and palette 1) and `0x40f662` (mode
  word not 1). Other screens return through `0x431718` (callers
  `0x437292`, `0x437303`, `0x43844c`, `0x43a70d`, `0x43c73d`, `0x44c97f`,
  `0x44d26e`) and get the panel at once.
- The callback `0x431830` (every event the ESAnim2 fires, i.e. each 500 ms
  tick after the frame advance): nothing once `[0x47374c]` is set; else
  `[0x473750]` = 0 → `0x42f58d` (lswitch2) and `[0x473750]` := 1; at frame
  index 5 (the second `bay2a_84`) `[0x47374c]` := 1, hide the animation
  (its frames are deleted, [vshell_widgets.md](vshell_widgets.md)),
  `0x431718`, and when prefs byte `0x2f` (`0x4825cb`) is 1: drain, leave
  full screen if active, `error.str` message 6 with caption 2
  (`0x401a81(6, 2, 0x30)`, §35), pump, re-enter full screen, byte `0x2f` :=
  2 (`0x40ddfe(0x2f, 2, 1)`), commit (`0x40dd6e(0)`), write prefs.cfg
  (`0x40dd0a`). This is the only `push 0x2f` before the setter `0x40ddfe`
  (the other four are assertion line numbers); the byte otherwise comes
  from prefs.cfg at start-up. The English `ERROR.STR` has no message 6 (six
  messages, [vshell_text.md](vshell_text.md)), the German has. So: lswitch2
  about 500 ms after the menu first appears, the panel 2.5 s after it (own
  test: frames 0..4 for five 100 ms passes each, the menu on the 25th pass,
  the sound at the first tick).
- The animation is shown once per process: every path that shows it runs
  once, and `[0x47374c]` blocks a second panel show. That the frames are
  deleted on hide therefore does not matter.

### 23.2 Show `0x431718` and hide `0x4317c3` (L)

Show: show `[0x48d1a8]`, show `[0x48d1ac]`; then CONTINUE GAME
`[0x48d1b4]` by slot 10's flag byte `0x482afd` (slot table `0x4826f4`,
record 10, `+0x5d`): set → label `+0xb5` := 0x29, button `+0x4d` := 0x22,
redraw tree, enabled := 1; clear → 0x26, 0x26, redraw tree, enabled := 0;
both end with the button's draw (vtable `+4`). A disabled CONTINUE takes no
click (toolkit rule). Hide: hide `[0x48d1a8]`, hide `[0x48d1ac]`.

### 23.3 The handlers' screen side (L)

What each handler does besides the flow event (§18.2):

| Button | Screen side |
|---|---|
| INSTANT ACTION `0x431935` | preset flag, mode store, `0x44c58b` (host, stage 0); full screen: clear + drain; windowed: show and hide the bay background `[0x48d524]` with drains (`0x431972..0x43198f`); new game, launch, exit 2 (host, stage 1). The menu is not hidden. |
| START NEW GAME `0x431a08` | hide, then mode 1 and Registration |
| CONTINUE GAME `0x431a73` | busy cursor on and off around it; hide only when the final word is 2 (bay); the End box appears over the menu |
| SAVE/RESTORE `0x431b27`, PREFERENCES `0x431b9b` | hide first |
| PRACTICE MISSIONS `0x431f3a` | hide, the practice show `0x44c322` (host), mode 0 |
| VIEW DEMO `0x431bfe` | full screen: clear + drain; exit 5 |
| CREDITS `0x431c7b` | gate := 0, show `[0x48d1a4]`, drain, clear, queue 0x54 and play `0x41e98c(0)`, gate := 1, clear, drain, hide `[0x48d1a4]`, redraw the root; two debug prints `0x46a834` around it. No flow event. |
| QUIT `0x431db6` | quit flag, then clear `0x4077ee` |
| ONLINE MANUAL `0x431e1b` | `0x431e79`: leave full screen `0x4077df` first, then the routine of §21.2 |

### 23.4 Implementation

`es2_screen_main_menu_build` (registration line in `screens.c`) builds the
table's screen with a resolver over the host's fonts, bitmaps (by file
name; `0x46de0c` is `dbm\bay2a_84.dbm`) and ESTEXT texts, binds the ten
buttons to one handler that runs `es2_main_menu_press(handler address)`
and the ESAnim2 to `es2_main_menu_animation_step`. `es2_main_menu_show` /
`_hide` are `0x431718` / `0x4317c3` for the other screens;
`es2_main_menu_start` is the first appearance; `es2_main_menu_enter(state)`
is the entry dispatch around `es2_shell_flow_start` with the intro movies
(§19). The host supplies sounds (`0x42f58d`, `0x42f894`;
[audio.md](audio.md)), the notice box, the practice and launch steps (the
practice library, §15), the movie player (§19) and a `changed` call after
every flow event; ONLINE MANUAL is fed to the flow by its address
(`0x431e1b`, §21.3). `es2_screen_main_menu_history` draws the menu before
a screen it hands over to (§41).

Own behaviour, documented as such: a PRACTICE MISSIONS whose host step
refuses shows the menu again (VSHELL has no refusal there); the drains
`0x402266` inside CREDITS and INSTANT ACTION are not run inside the handler
(the toolkit drains after it; the movie player presents itself); the
windowed INSTANT ACTION flash of the bay background is left to the host's
stage 1 (the bay background object belongs to the bay screen).

### 23.5 Verification and capture states

`//shell:main_menu_test`: build, animation timing and sound, the notice,
CONTINUE's looks, every button through the input path with its flow
outcome, CREDITS' gate, the entry dispatch; one-line mutations of
`main_menu.c`, each caught. `//shell:screen_test --suite main_menu`:
synthetic and real cases (CD fonts, `bay2a_80..84`, `palette.dpl`, the
English ESTEXT) with recorded frame and table hashes. The menu after
CONTINUE's path is equal to VSHELL's tour frame in
`//shell:gui_tour_real_test` (**V**, §41). States a capture should hold
(run `menu-bay` or a run with `-X6`, screen `main_menu`):

- `frame0` … `frame4`: the animation after the first show, one capture per
  500 ms step (the phase depends on the drain);
- `menu`: the panel after the animation, slot 10 without its flag
  (CONTINUE greyed), and `menu_continue` with GAME_R present and flagged;
- `pressed`: START NEW GAME held down (button down, two passes);
- `after_credits`: the menu after CREDITS (with `-a` the movie is skipped
  and only the clears and the root redraw remain);
- `after_quit`: the cleared screen after QUIT (if the capture can still
  grab it).

### 23.6 Open points

- Which picture the results path leaves for the outcomes no slot,
  rejected and other advance values is not traced; the own screen starts
  the animation for every main-menu end except the Replay box.
- At `0x40f53a..0x40f54f` every value of `0x4133eb` other than 0 and 1
  takes the branch of 2 (`0x40f62d`, page 4) (L); `flow.c` sends values
  other than 0, 1, 2 to the main menu (`ES2_FLOW_OUTCOME_STOPPED`). Whether
  `0x4133eb` returns anything but 0..2 is not checked.
- The role of prefs byte `0x2f` (who writes 1) stays open
  ([cfg.md](../formats/cfg.md)).
- The animation frames and the other states of §23.5 are not compared with
  VSHELL's frames.

## 24. Registration screen

Own code: `es2/shell/gui/registration.h`, `shell/src/gui/registration.c`
(in `//shell:screens`), registered as `ES2_UI_SCREEN_REGISTRATION` in
`shell/src/gui/screens.c`. Own tests: `screen_test` (suite
`registration`) and `flow_test`. Everything below is **L** unless marked.

### 24.1 Show `0x43c29a` and hide `0x43c349`

START NEW GAME `0x431a08` hides the main menu (`0x4317c3`), stores mode 1
(`0x40ec56(1)`) and calls `0x43c29a`: show `[0x48d574]` (background),
`[0x48d578]` (panel), `[0x48d57c]` (body); post a mouse event (`0x46903c`:
type 0x20, +0x25 := 0) with code 2 (left press) and target `[0x48d588]`
(the name field, so its edit starts when the queue is drained);
`0x469ffc(queue, field)` (leave `0x46a0b4`, then enter
`0x46a15c(field)`); capture byte `+0x1f` := 1. `0x43c349` hides the same
three frames. Neither touches the skill word `0x476248`, the field's text
or ACCEPT's state.

### 24.2 The skill is not reset

The only writes of `0x476248` are `0x43c6d2` (SKILL LEVEL `0x43c6ad`) and
`0x43c3ca` (the pilot_name screen's SKILL `0x43c3a5`; that screen is built
at `0x40184e` but its frames are never shown); there is no other reference
to the address in the code, and the image value is 0. So the skill, the
typed name and ACCEPT's enabled state stay from the last visit of the
screen in the same process. The flow steps `skill` from its current value
and keeps it (`flow_test` checks it after CANCEL and a second START NEW
GAME).

### 24.3 Constructor `0x43bd2e`

Besides the table's fields (`registration` controls in
`vshell_layout.json`) the constructor sets the skill label (`0x43c671`,
called at `0x43c0fe` right after `[0x48d58c]`'s fields): the label of
`[0x48d58c]` gets ESTEXT `(int16)(0x476248 + 0x35)` with its own alignment
and colour (23), redrawn. ACCEPT's label colour 38 and `+0x49` := 0
(`0x43c270..0x43c289`) are table fields; its frame colour stays 34 from
the constructor argument. The field frame `[0x48d584]` is a disabled text
button whose label is the one-space literal `0x476203`; the skill button
`[0x48d58c]` starts with the empty literal `0x476245`.

### 24.4 Handlers

| Handler | Effect (L) | Own code |
|---|---|---|
| name field `0x43c47e` (`+0x3d`) | event types 0x40 (key) and 0x100 (command) only: text `+0x45` empty → ACCEPT label `+0xb5` := 38, ACCEPT `+0x4d` := 38, `0x41f3ed` (redraw events), `+0x49` := 0, draw (vtable +4); else 41, 34, the same calls, `+0x49` := 1. Two trace calls `0x46a834(0, 5, …)` change no shell state (not modelled) | `on_name` |
| SKILL LEVEL `0x43c6ad` | `0x476248` := (v + 1) mod 4 (signed remainder, `and 0x80000003`), then `0x43c671`. No event test: the button fires it on the left and on the right release | flow `ES2_FLOW_SKILL`, then the label |
| CANCEL `0x43c728` | `0x43c349`, main menu `0x431718` | hide, flow CANCEL (the main menu's presenter shows it) |
| ACCEPT `0x43c78b` | `0x411e40` (re-reads `gam\herc_inf.dat`, [file_io.md](file_io.md)), `0x43c349`, `0x477946` := 0, `0x40e8a5(field +0x45, (int16)0x476248)`, page word `0x481108` := 0 if the CAREER position word `0x46fc5a` is 0, else 1 (`0x43c7c6..0x43c7dc`) | hide, flow ACCEPT with the field text; the reload and `0x477946` belong to the host's new game (the text menu's host clears the once-flag, §19) |

ACCEPT is a text button, so the toolkit fires it only while `+0x49` is set:
with an empty name it does nothing.

### 24.5 Page word in the flow

`es2_shell_flow.page` is `0x481108` (image 0; it is also the start of the
range `0x401011` zero-fills at start-up). ACCEPT stores 0 after a
successful new game: the mode-1 new game sets the CAREER position to
(1, 0) (`0x413053`, §4.2), and ACCEPT is only reachable after START NEW
GAME stored mode 1, so the test at `0x43c7c6` always sees position 0. A
failed new game (an own-menu case without VSHELL counterpart) leaves the
word. The other writers are the results path (`0x40f62d` := 4, §18.2),
`0x43aee7` (§33) and the DEBUG dialog (§39).

### 24.6 Presenter

The screen feeds the flow and reads `flow.skill`; the show after START NEW
GAME and the main menu after CANCEL are the presenter's reaction to
`flow.screen` (§23, §35). The host supplies the fonts
`[0x46ddfc]`/`[0x46de00]`, the background `[0x46de0c]` and the ESTEXT
lookup (fields of `es2_screen_registration`).

### 24.7 Verification and capture states

`screen_test --suite registration`: synthetic cases (start; a tour of
typing, SKILL, backspace to empty, CANCEL; CANCEL and a second show
keeping name and skill, then ACCEPT; a behaviour case checking colours,
enabled flags, the field's capture, the flow's new game with name and
rank, page 0 and the bay) and a real case (CD fonts, `bay2a_84`,
`palette.dpl`, English ESTEXT), all with recorded frame hashes; a mutation
of the enable rule is caught. The real case's `open` and `start` snaps
after START NEW GAME equal VSHELL's tour frames, caret blink included
(**V**, §41). States a capture should still hold: after typing a name,
after one and after three SKILL LEVEL clicks (REGULAR, ELITE), with the
name deleted again (ACCEPT greyed, frame 38), and after CANCEL and START
NEW GAME again (name and skill kept).

### 24.8 Open points

- The states of §24.7 other than `open` and `start` are not compared with
  VSHELL's frames.

## 25. Practice screen

The graphical practice screen `0x44b310` on the widget toolkit as a
presenter of the flow (§18) over the practice library (§15). Own code:
`es2/shell/gui/practice_screen.h`, `shell/src/gui/practice_screen.c` (in
`//shell:screens`). Read in the listing: the constructor
`0x44b310..0x44c321`, show `0x44c322`, hide `0x44c343`, the entry
`0x44c40c..0x44c58a` and the handlers `0x44c5b9..0x44cdab` (**L**). Own
tests: `//shell:practice_screen_test` and the `practice` suite of
`//shell:screen_test`. The state rules are §15.2's (V there); the screen
side here was not run in vitro.

### 25.1 Objects (L)

The 76 controls (`practice` in `vshell_layout.json`): `[0x48dbfc]` the
full-screen ESBitmap of `[0x46de0c]` (`bay2a_84`), `[0x48dc00]` the ESTitle
(ESTEXT 0xf0) with two region fills, `[0x48dc04]` (the rows, heading 0xf1)
and `[0x48dc08]` (the parameters, heading 0xfa); five parameter
ESButtonFonts `[0x48dc0c..0x48dc1c]` (0xfb Damage `0x44c5b9`, 0xfc Ammo
`0x44c676`, 0xfd Difficulty `0x44c733`, 0xfe Time `0x44c7f0`, 0xff Herc
Type `0x44c8ad`), each followed by a one-character literal (`0x479c31`,
`…33`, `…35`, `…37`, `…39`) and a value button `[0x48dc38..0x48dc48]`
built **disabled** (+0x49 := 0) whose label (+0xc1 := 1, +0xb5 := 0x17) is
`0x4087f0`(base + prefs byte) at construction time (Damage `+0x26`, Ammo
`+0x25`, Difficulty `+0x27`, Time `+0x29`, Herc `+0x28`); Main Menu
`[0x48dc4c]` (0x100) and Begin Mission `[0x48dc54]` (0x102); eight ESQuad
rows `[0x48dc5c + 4i]` (ESTEXT 0xf2 + i in column 0, three empty literal
columns), handler from the table `0x48dc7c`, column 0 colour 0x27,
enabled. So only the five name buttons take clicks, not the values. A
ninth handler `0x44c9cd` (it only frees its event) has no reference in the
listing. The fonts `[0x46ddfc]` and `[0x46de00]` are two loads of
`dfn\font2.dfn` ([dfn.md](../formats/dfn.md)).

### 25.2 Show, hide, entry, handlers (L)

- Show `0x44c322`: show `[0x48dbfc]`, `[0x48dc00]`, then `0x44c40c(0)`.
  Hide `0x44c343`: hide the same two.
- Entry `0x44c40c(i)`: nothing when i is the selection; else the old row
  (when not −1) column 0 := 0x27, its +0x4d := 0x10, redraw tree; for i < 8
  the new row column 0 := 0x29, +0x4d := 0x10, redraw tree; Herc Type for
  i < 4 label +0xb5 := 0x26, button +0x4d := 0x26, redraw tree, +0x49 := 0,
  draw (vtable +4), for 4..7 0x29, 0x22, redraw tree, +0x49 := 1, draw;
  then +0x28 := the table byte, the Herc value label := `0x6e + byte` with
  its own alignment and colour, redrawn; selection := i.
- Parameter handlers: `movsx` of the event's +0x24, `dec`, `jne`: **1 (left
  release) steps forward, every other value back**; then the value label
  of the matching value button := base + byte, redrawn
  (`0x40b8bb(..., 1)`).
- Main Menu `0x44c96a`: hide `0x44c343`, `0x431718`. Begin Mission
  `0x44ca26`: commit `0x40dd6e(1)`, write `0x40dd0a`,
  `0x40e8a5(0x479f70, +0x27)`; the screen is not hidden (the posted Use
  Default and the exit follow, §15.1).

### 25.3 Implementation and own choices

`es2_screen_practice_build` (registration line in `screens.c`) builds the
table's screen with a resolver over the host's fonts, the background (by
file name), ESTEXT texts and the EXE literals, then sets the five value
labels from `flow.prefs.current` (hidden, not drawn).
`es2_practice_screen_show` / `_hide` / `_entry` are `0x44c322` /
`0x44c343` / `0x44c40c`; the state change of an entry, a parameter step
and Begin Mission's commit are `es2_shell_practice_entry`, `_control`,
`_begin` (§15). PRACTICE MISSIONS' host step (the main menu's
`entry(0x431f3a, 0)`, §23) calls `es2_practice_screen_show` and then the
mode store `0x40ec56(0)` (the library's `es2_shell_practice_show` is this
entry plus that store). Begin Mission writes through the flow host's
`write_prefs`, then the screen host's `launch` (new game, posted Use
Default, files: `es2_shell_practice_run`), then
`es2_shell_flow_game_created` and `es2_shell_flow_dbsim`. The `-@` path
(DEBUG dialog instead of the posted click) is the host's (§22, §39).

Own behaviour: a failed write or launch returns to the main menu (hide,
`0x431718`, the flow's Main Menu event), as the text menu does (VSHELL has
no failure path there); a value label whose ESTEXT index the host cannot
give is empty (VSHELL asserts, §15.2); a previous selection 8..10 is not
recoloured (unreachable, §15.2). No flow event was added: Main Menu is the
flow's `0x44c96a`, Begin Mission uses the existing launch calls.

### 25.4 Verification and capture states

`//shell:practice_screen_test` (synthetic font, image, palette and a
synthetic Herc type table): the build, the show, every entry boundary, all
five parameters with left and right releases and a byte above the
modulus, Herc Type while disabled, Main Menu without revert, PRACTICE
MISSIONS from the main menu, Begin Mission launched, refused and failed;
one-line mutations of `practice_screen.c`, each caught.
`//shell:screen_test --suite practice`: synthetic and real cases (CD font,
`bay2a_84`, `palette.dpl`, the English ESTEXT, the table and literals from
the checked `VSHELL.EXE`) with recorded frame and table hashes. States a
capture should hold (a run with `-X6`, PRACTICE MISSIONS clicked, screen
`practice`, prefs.cfg with Damage 0, Ammo 0, Difficulty 1, Time 0 to match
the real cases):

- `shown`: the screen after PRACTICE MISSIONS (entry 0, Herc Type greyed);
- `entry_strike`: after a click on the fifth row (Strike, Herc Type
  enabled);
- `damage_left`, `difficulty_right`: after a left click on Damage and a
  right click on Mission Difficulty;
- `pressed`: Begin Mission held down (two passes);
- `after_main_menu`: the main menu panel after Main Menu.

### 25.5 Open points

- Pixel equality with VSHELL's frames is unchecked (no capture of the
  practice screen is compared).

## 26. Saved games screen

The graphical Saved games screen `0x438c40` on the toolkit as a presenter
of the flow (§18). Own code: `es2/shell/gui/saved_games.h`,
`shell/src/gui/saved_games.c` (in `//shell:screens`). Read in the listing:
`0x4377bc..0x438c3f` and `0x43a19c..0x43a42e` (**L**). Own tests:
`//shell:saved_games_test` and the `saved_games` suite of
`//shell:screen_test`. Nothing was run in vitro. Adds to §4.3 and §9.3.

### 26.1 Show, hide, selection, panel (L)

- Controls (`saved_games` in `vshell_layout.json`): rows
  `saved_games.04..13` = `[0x48d434 + 4·slot]` (ESDialog, handlers
  `0x438873 + 0x61·k`, each only `0x437fef(k)`; set `0x475869`, `+0xb7` 4,
  `+0xbb` 0x27, `+0xbf` 0, `+0xb3` 0), their region `[0x48d430]` (`.03`),
  CANCEL `.14`, ACCEPT `.16`, SAVE `.18`, RESTORE `.20`, EXIT `.22`, the
  statistics region `[0x48d498]` (`.24`) with the twelve value labels
  `.26 .28 .30 .32 .33 .35 .36 .38 .39 .43 .45 .47`, and a second region
  `[0x48d4f8]` (`.48`) that every show hides.
- Show `0x43a19c`: the ten rows get the slot texts (`0x4381cd(k, NULL)`:
  setText `0x40c294`, which does not draw); show background, panel,
  statistics; hide `.48`; the rows' region `+0x49` := 0; ACCEPT and CANCEL
  off; SAVE on when the selected slot is below 10 and a game is loaded;
  the selected row (below 10) colour 0x29 and drawn; RESTORE on when the
  slot is below 10 with its flag; the panel. EXIT is not touched (it keeps
  the state of the last ACCEPT/CANCEL). A button "on" is label `+0xb5`
  0x29, button `+0x4d` 0x22, tree redraw `0x41f3ed`, `+0x49` 1, draw; "off"
  0x26, 0x26, `+0x49` 0.
- Hide `0x43a3f6`: `0x437fef(10)`, then hide panel, background,
  statistics, `.48`. Called by RESTORE and EXIT.
- Selection `0x437fef(k)`: returns for the selected slot unless the naming
  byte `0x474fdc` is 1, and always while it is 2; the old row (below 10)
  colour 0x27, the new one 0x29 (each drawn `0x40c6ff`); SAVE and RESTORE
  by §9.3's rules (both also need naming ≠ 1); selected := k; the panel.
- Panel `0x4377bc`: as §13.4, written with setText `0x40b8bb` keeping each
  label's alignment and colour, drawn at once; the text buffer is 20 bytes
  on the stack (`ebp-0x14`; a longer ESTEXT entry would overrun it, not
  reproduced). Otherwise ESTEXT 0 into all twelve labels
  (`0x437bd5..0x437e5d`).

### 26.2 The buttons (L)

| Button | VSHELL |
|---|---|
| SAVE `0x438263` | naming := 1; SAVE, RESTORE, EXIT off; `0x437e62` |
| `0x437e62` | only with naming ≠ 0: post a left-button-down event to the selected row (`0x4086b7(0x29)` record, type 0x20, code 2, target row; `0x468780`); `0x469ffc(queue, row)` (leave, enter the row) and capture := 1; row `+0xbf` := 1, `+0xb3` := 1; row text `"%2d. "` (`0x4381cd(slot, "")`, `""` at `0x475306`); CANCEL and ACCEPT on; naming := 2; redraw `[0x48d430]` |
| ACCEPT `0x43868a` | `0x40e933(slot, row +0x45)` (`0x438239`; `""` at `0x47530f` without a row); `0x430ec4(slot)`; panel; CANCEL, ACCEPT off; SAVE, RESTORE, EXIT on; naming := 0. No selection change, the typed row stays |
| CANCEL `0x4384aa` | row text back (`0x4381cd(slot, NULL)`); CANCEL, ACCEPT off; redraw `[0x48d430]`; SAVE, RESTORE, EXIT on; naming := 0; `0x437fef(10)` (which turns SAVE and RESTORE off again) |
| RESTORE `0x438393` | `0x40eaaa(slot, 1)`, `0x40e933(10, 0)`, hide `0x43a3f6`, `0x477946` := 0, bay show `0x43b7f2(8)` + `0x43b758`; no test of the final word (§18.2) |
| EXIT `0x438424` | hide `0x43a3f6`, then caller `0x48d424`: 0 → `0x431718`, 8 → bay show, other → nothing |

No handler clears `+0xbf`: a row named once stays editable for the rest of
the process (typed characters change its picture, not the slot; the next
show resets the text). Reproduced. The flow (§18.2) carries these enabling
rules: SAVE, RESTORE and EXIT are refused while naming, ACCEPT and CANCEL
only while naming; CANCEL, RESTORE and EXIT leave slot 10 selected.

### 26.3 Own choices

- ACCEPT of a row of 80 bytes or more is refused (nothing saved, naming
  stays): the 80-byte slot record cannot hold it and VSHELL's `strcpy`
  (`0x40e6ea`) would run into the flag and the next record; the row can
  reach 89 bytes only with glyphs narrower than about 4 pixels (the text
  menu refuses the same, §9).
- The host owns the slot table and the statistics records; the screen
  reads them and asks the host for `0x430ec4` after ACCEPT. The bay show
  after RESTORE and EXIT (caller 8) is the host's (bay screen); EXIT with
  caller 0 shows the main menu (§23) when the screen has it.
- A RESTORE the own shell refuses (the slot file cannot be read) or whose
  campaign preparation fails keeps or leaves the screen as the flow says
  (§18); VSHELL has neither case.

### 26.4 Verification and capture states

`//shell:saved_games_test`: build, show without and with a game, selection
and the panel's values, naming through the input path with typing,
backspace and the four-character prefix, ACCEPT, CANCEL, the refusals,
RESTORE for final words 2, 0, 1, 3, 9, EXIT to both callers.
`//shell:flow_test`: RESTORE for final words 0, 1, 3, 9 to the bay; the
naming rules. `screen_test --suite saved_games`: synthetic and real cases
(CD font, `bay2a_84`, `palette.dpl`, the English ESTEXT) with recorded
hashes; `saved_games.real_shown` equals VSHELL's tour frame (**V**, §41).
States a capture should still hold (screen `saved_games`; a run with
GAME_R and two user slots, one flagged):

- `shown_game`: after CONTINUE and the bay's SAVE (slot 10's statistics);
- `used` / `empty`: a flagged row and an empty row clicked;
- `naming`: SAVE on a row with a game loaded (row `"%2d. "`, caret),
  `typed` after typing a name, `accepted` after ACCEPT, `cancelled` after
  CANCEL;
- `after_exit`: the frame after EXIT to the main menu, and the bay after
  RESTORE of a flagged slot.

### 26.5 Open points

- The posted left-button-down of `0x437e62` and the focus/capture are
  modelled with the toolkit's event path; whether VSHELL's caret starts on
  the first blink tick is not compared with a capture.
- The panel's kill pairing remains §13.6's hypothesis.
- The states of §26.4 other than `real_shown` are not compared with
  VSHELL's frames.

## 27. Preferences screen

The graphical Preferences screen `0x435598` on the widget toolkit as a
presenter of the flow (§18). Own code: `es2/shell/gui/preferences.h`,
`shell/src/gui/preferences.c` (in `//shell:screens`). Read in the listing:
the constructor's shows `0x435733..0x435880`, the show `0x436d45`, hide
`0x436da7`, the radio refreshes `0x436e20..0x4371df`, the setters
`0x43712c`, `0x43714c`, `0x4371e0`, `0x437200` and the handlers
`0x437220..0x4377b8` (**L**). Own tests: `//shell:preferences_test` and the
`preferences` suite of `//shell:screen_test`. Nothing here was run in
vitro.

### 27.1 Show, hide and the radio groups (L)

- PREFERENCES `0x431b9b` is the main menu's hide `0x4317c3`, then the show
  `0x436d45`: the check boxes `0x436e20`, the repair radios `0x436fbe`, the
  weapons-building radios `0x43716c`, then the four setters with the bytes'
  own values (`0x43712c(+0x2c)`, `0x4371e0(+0x2d)`, `0x43714c(+4)`,
  `0x437200(+6)`; each stores its word shadow `0x474d60`/`0x474d64`/
  `0x474d62`/`0x474d66`, re-stores the byte with `0x40ddfe(i, v, 1)` and
  redraws its group), then show `[0x48d384]` (background, `bay2a_84`) and
  `[0x48d388]` (the panel). Hide `0x436da7`: hide the same two.
- A radio is set by writing `+0x69` and calling its draw (vtable `+4`). The
  check boxes take bytes 0 and 1 as they are (`movzx`, no 0/1 test). The
  groups set exactly one radio for the values 0..n−1 (repair 0, 1, 2 →
  `[0x48d404]`/`[0x48d408]`/`[0x48d40c]`; +0x2d 0, 1 → `[0x48d41c]`/
  `[0x48d420]`; +4 0 High Res `[0x48d3e4]`, 1 Low Res `[0x48d3e8]`; +6 0
  Window `[0x48d3b0]`, 1 Full Screen `[0x48d3b4]`); any other value leaves
  the group as it was (`sub al, 1; jb; je; [dec; je]; ret`).
- The constructor's modal show `0x435739` of the ESAlert and the hide
  `0x43587b` of its shield leave the box pending under a hidden shield (the
  table's after_construction states, applied by the toolkit).

### 27.2 Handlers (L)

| Handler | Effect |
|---|---|
| Music `0x437351` → `0x436ed1(0)` | byte 0 set: ramp down `0x42f7f0`, then toggle `0x40de35(0, 1)`; clear: toggle, then ramp up `0x42f894`; then `0x436e20` |
| Sound Effects `0x4373b2` → `0x436ed1(1)` | toggle byte 1, `0x436e20` |
| repair `0x437413`/`0x437474`/`0x4374d5` | `0x43712c(0/1/2)` |
| weapons building `0x437536`/`0x437696` | `0x4371e0(0/1)` |
| Low Res `0x4376f7` / High Res `0x437758` | `0x43714c(1/0)` |
| Window `0x437597` | `[0x481f4c]` ≠ 0 → `0x407636` (leave full screen); `0x437200(0)` |
| Full Screen `0x437608`, Alert ACCEPT `0x437678` | §22.2 |
| CANCEL `0x437220` | revert `0x40ddb6(0)`; `0x407636` when byte 6 ≠ 0 while windowed or byte 6 = 0 in full screen; byte 0 ≠ 0 → ramp up `0x42f894`, else toggle byte 0, ramp down `0x42f7f0`, toggle back; hide `0x436da7`; main menu `0x431718` |
| ACCEPT `0x4372e1` | commit `0x40dd6e(0)`, writer `0x40dd0a`, hide, main menu; no display or music call |

The ramps run only while prefs byte 0 is set (`es2_shell_sounds_ramp_begin`,
[audio.md](audio.md)), which is why both ramps are reached with byte 0 = 1:
the down ramp before the clear, the up ramp after the set, and CANCEL's
down ramp between two toggles. `0x436ed1`'s cases 2 and 3 (byte 2) have no
caller among the controls (§11.4).

### 27.3 The flow's Full Screen

`es2_shell_flow_prefs_control(ES2_SHELL_PREFS_FULL_SCREEN)` asks the
host's `full_screen` callback (the display word `0x481f4c`, last member of
`es2_shell_flow_host`) and answers IGNORED without the Alert box when it
is nonzero, as `0x437618..0x437628` does. `NULL` means windowed: the text
menu (`menu.c` passes NULL) always shows the Alert box. The display itself
stays the presenter's: the Alert ACCEPT (§22.2) and the screen's Window
and CANCEL toggle the frame's `shell.fullscreen` through
`platform.fullscreen` (`0x407636`). Own test: `flow_test`
(`full_screen_alert`).

### 27.4 Implementation

`es2_screen_preferences_build` (registration line in `screens.c`) builds
the table's 56 controls with a resolver over the host's fonts, bitmaps
(`dba\chk_box.dba` members 1 normal / 0 checked, `dbm\bay2a_84.dbm` for
`0x46de0c`) and ESTEXT texts; the ten own handlers run
`es2_preferences_press(handler address)`; Full Screen and the Alert ACCEPT
get `es2_boxes_handler` and are bound to the boxes with
`es2_boxes_bind_preferences`. The host's `music` callback receives the
ramp's address and the current block at the call; `changed` follows every
flow event. `es2_preferences_show`/`_hide` are `0x436d45`/`0x436da7`;
`es2_preferences_display_refresh` is `0x436e5d` for the boxes'
`prefs_refresh`. The presenter (§35) calls `es2_preferences_show` when the
flow's screen becomes Preferences after the main menu's PREFERENCES. Own
choices: none beyond §22.5.

### 27.5 Verification and capture states

`//shell:preferences_test`: build and binding, the show with in-range and
out-of-range bytes and the shadows, every control through the input path
with its byte, group and ramps, Window in full screen, Full Screen
windowed and in full screen with the Alert box and its ACCEPT, the panel
unreachable under the shield, CANCEL's display and music
re-synchronisation in both directions, ACCEPT with a good and a failed
write; one-line mutations of `preferences.c` and `flow.c`, each caught.
`//shell:screen_test --suite preferences`: synthetic and real cases (CD
font, `bay2a_84`, `chk_box.dba`, `palette.dpl`, English ESTEXT) with
recorded frame and table hashes; `preferences.real_shown` equals VSHELL's
tour frame (**V**, §41). States a capture should still hold (screen
`preferences`, from a prefs.cfg with byte 0 = 1, +0x2c = 1, +0x2d = 0,
+4 = 0, +6 = 0, run windowed):

- `music_off`: after a click on the Music check box;
- `repair_2`: after a click on the third repair radio;
- `alert`: after Full Screen (the Alert box over the screen), and
  `full_screen`: after its ACCEPT (needs a capture of the full-screen
  display);
- `after_cancel` / `after_accept`: the main menu after CANCEL / ACCEPT.

### 27.6 Open points

- The handlers' window effects are unit-tested on the own toolkit, not run
  in vitro (they need VSHELL's window objects).
- The text menu keeps its deviations of §11.5 (no display: Full Screen
  always shows the Alert text and stores byte 6 := 1).
- The states of §27.5 other than `real_shown` are not compared with
  VSHELL's frames.

## 28. Service bay and squad panel

Own code: `es2/shell/gui/service_bay.h`, `shell/src/gui/service_bay.c` (in
`//shell:screens`), registered as `ES2_UI_SCREEN_SERVICE_BAY` and
`ES2_UI_SCREEN_SQUAD_PANEL` in `shell/src/gui/screens.c`. Own tests:
`screen_test` (suite `service_bay`). Read in the listing, ranges
`0x43a430..0x43b8ed` and `0x43cfa5..0x43e7ee` (**L**); nothing was run in
vitro.

### 28.1 Show and hide (L)

The bay show is `0x43b7f2(8)` (show the frame `[0x48d520]`) and
`0x43b758`: show the bar `[0x48d528]`; with the mode word `0x4826f0` = 0
WEAPONS and CREW get `+0x49` := 1 and REPAIR, BUILD, ARMORY := 0,
otherwise all five := 1; the word `0x4758b8` := -1. MAIN MENU, SAVE,
MISSION and the online image are not touched (enabled since construction;
ARMORY is built with `+0x49` = 0). The flow's `es2_shell_flow_enabled`
follows this rule. Every way into the bay stores mode 1 first (START NEW
GAME, CONTINUE, SAVE/RESTORE; the mode-0 results path ends in the main
menu), so mode 0 in the bay is not reachable in VSHELL. The hide
`0x43b8cd` hides bar and frame and selects palette 1 (`0x407b62(1)`).

### 28.2 The palette window `0x43a430(p)` and `0x43b7f2(word)` (L)

`0x43a430` shows the ESPal `[0x48d524]` (its draw fills (0, 30)–(639, 479)
with 0x10 and presents at once), stores p in its word `+0x45` and hides it
(which selects and applies palette p). `0x43b7f2` (jump table `0x43b80a`):
0, 1 nothing; 2, 4, 5, 6 palette 2; 3 palette 1; 7 `0x43b877` (by the
CAREER group `0x46fc58` and the page word `0x481108`,
`es2_palette_for_bay_screen`); 8 the frame.

### 28.3 Bar handlers (L)

Every handler first tests the loop-started flag `0x46c280` and does
nothing while it is 0 ([vshell_state.md](vshell_state.md); it is set at
`0x4020b8`, after the first iteration's movie step), then compares the
word with its own (MAIN MENU: with 0) and does nothing when equal. The
image buttons fire on the left press (toolkit) and stay drawn pressed.

| Button | Sequence after the tests |
|---|---|
| MAIN MENU `0x43a66c` | `0x43a45b` (all nine buttons `+0x45` := 0 and drawn), leave `0x43a537`, palette 1 unless the word was -1, `0x43b8cd`, `0x40e933(10, 0)`, main menu `0x431718`, word 0, bptlt2 `0x42f501` |
| SAVE `0x43a768` | `0x43a45b`, leave, palette 1 unless -1, `0x43b8cd`, caller `0x48d424` := 8, Saved games `0x43a19c`, word 1, bptlt2 |
| WEAPONS `0x43a862`, REPAIR `0x43a962`, BUILD `0x43aa62` | `0x43a45b`, own button `+0x45` := 1 and drawn, leave, `0x43b7f2(w)`, squad panel `0x43d677(w)`, the editor's show (`0x43fbd8`, `0x43397c`, `0x446f9d`), word w, bptlt2 |
| ARMORY `0x43ab62` | as above without the squad panel: `0x43b7f2(5)`, `0x449b87`, word 5 |
| CREW `0x43ac5a` | as WEAPONS with `0x43b7f2(6)`, `0x43d677(6)`, then word 6 **before** `0x442091` |
| MISSION `0x43ad5a` | `0x43a45b`, own button pressed, `0x43aee7(page, 0)` with page 0 when `0x477946` = 0 and `0x46fc5a` = 0, else 1: page word := page, leave, `0x43b7f2(7)`, `0x444873(page)`, word 7 (a non-zero second argument would post a press to MISSION, `0x43af63..0x43afb2`); then bptlt2 |
| online image `0x43a5bd` | `0x431e79` (§21; full screen left first, `0x4077df`) |

The leave `0x43a537` (table `0x43a54a`) calls the squad panel's hide
`0x43d779` with 2, 3, 4 for words 2, 3, 4 and **with 2 for CREW (6)**,
then the editor's leave UI, then `0x412a37` for 2, 3, 4 (the flow's
rebalance); 5 and 7 only the editor's leave; -1, 0, 1 nothing. The own
code feeds the flow event at the leave, so the flow's rebalance runs where
VSHELL's does; the screen keeps its own copy of the word
(`session.screen`) with VSHELL's timing for the squad panel's decisions.

### 28.4 Squad panel (L)

Shown only with WEAPONS, REPAIR, BUILD and CREW (not on the bay alone, not
with ARMORY or MISSION). Show `0x43d677(w)`: `0x43cfa5(w)` (the pictures,
§36.1; host hook `panel`), labels `0x43da1a(selection)`, Herc column
`0x43e0d7`, pilot column `0x43e167`; selection -1: hide and show the ninth
grid `[0x48d5bc]`, else show grid `[0x48d59c + 4k]`; show the seven labels
`[0x48d5c0..0x48d5d8]`; with a selection every row `[0x48d5dc + 4k]` gets
`+0x4d` := 0x29 (selected) or 0x10 and a redraw. Hide `0x43d779(w)`:
`0x43cfea(w)` (§36.1), hide the grid of the selection, for w = 3 also
REPAIR's `[0x48d1f8 + 4k]`, hide the seven labels.

- Labels `0x43da1a(k)`: the pilot of bay k (`0x410844(0x482b5c, k)`: the
  player if his bay word `+0x26` is k, else the first of the three
  selected roster pilots whose `+0x22` is k; none for -1). Pilot value
  `[0x48d5c4]` := its name `+4`, or ESTEXT 0 (k = -1 or no record) / 0x6d
  (a record without pilot), colour 0x17; skill `[0x48d5cc]` := ESTEXT
  `0x35 + skill(+0x25)` or ESTEXT 0, colour 0x17; condition `[0x48d5d4]`
  (right-aligned): k = -1 ESTEXT 0 in 0x17; no record ESTEXT 0 in the band
  colour of 100; under construction (`0x411088` false) `"%d%s"` of `+0x4a`
  and ESTEXT 0x6c in 0x20; else ESTEXT `0x68 + band` in the band colour.
- Bands `0x43e05f(v)`: the index of the first of the five limits at
  `0x47665c` (89, 79, 59, 29, 0) that v exceeds (signed), 5 if none;
  colours `0x43e09f`: the six words at `0x476666` (14, 13, 12, 11, 10, 39).
  Condition v is `0x4121f8(record + 8, record +0x4c)` (§36.4), 100 without
  a record.
- Rows: `0x43e0d7` sets column `+0x59` to the record's name (pointer at
  `+4`) or the empty literal `0x476a44`, in the band colour; `0x43e167`
  sets column `+0x61` to `"%d%s"` (progress, ESTEXT 0x6c) for a record
  under construction, else `0x410890` (the pilot's name or ESTEXT 0),
  colour 0x27. Columns 0 and 2 are the table's `"%d."` row number and
  `"-"`.
- Selection `0x43dcdd(k)` (§17.1's rules), its UI part: REPAIR, CREW,
  WEAPONS refuse a bay without a finished Herc (CREW also vehicle 8 with a
  wingman order); the old grid hidden (REPAIR with tab `0x474094` ≠ 0:
  REPAIR's own grid), the new shown, row colours 0x10 / 0x29, the
  selection stored, labels `0x43da1a(k)` (not in REPAIR); the editor parts
  (WEAPONS `0x476658` := -1, `0x40bdf4(grid, 12)`, `0x43fdac(0)` before;
  REPAIR `0x434549(0, 0)`, `0x43436f`, `0x43433f`, `0x433f86` after; BUILD
  `0x447064` for k ≠ -1; CREW `0x4426e5` and `0x43e167` with the selector
  flag) are the host's `selected` hook; every case ends with the title's
  redraw. The eight row handlers `0x43e477 + 0x6f k` set the flag
  `0x47665a` around `0x43dcdd(k)`; the title's handler `0x43e41e` does
  nothing.
- Selection -1 quirks (reproduced where they touch this screen): the row
  index -1 reads `[0x48d5d8]`, the title, whose `+0x4d` then changes; the
  grid index -1 reads `[0x48d598]`, Registration's ACCEPT (not this
  screen's: the own code does nothing there), so the hide after CREW's
  CLEAR leaves the ninth grid shown.

### 28.5 Host and hooks

The host (the process frame, §35) supplies fonts `[0x46de00]` (font2) and
`[0x46de04]` (black), the DBA members, ESTEXT, bptlt2, the CAREER words, a
view of the eight bays (`es2_squad_bay`), the two band tables read from
the executable, and sets `loop_started` at main-loop step 5; it calls
`es2_service_bay_show` whenever the flow shows the bay and
`es2_service_bay_game_loaded` after a new game or a load (selection 0,
[vshell_state.md](vshell_state.md)). The editors (§29–§32) and the mission
screen (§33) hang off `screen(word, show)`, `selected`, `panel` and
`repair_grid`; the pictures are §36; the shared state is
`es2_shell_editor_session` (selection, CREW order, REPAIR tab).

### 28.6 Verification and capture states

`screen_test --suite service_bay`: synthetic cases (the bay shown; a tour
WEAPONS held and released, row 2, BUILD, CREW, ARMORY, MISSION, MAIN MENU;
a behaviour case checking the loop-started gate, the word timing of
WEAPONS/BUILD/CREW, the hook order, the rebalances, the selection rules
and labels, SAVE, MAIN MENU's save, the online image and the band
function; mode 0's enable rule) and a real case (CD fonts, `mnu_bttn.dba`,
`online.dba`, `bay2a_84`, `palette.dpl`/`arming.dpl`, English ESTEXT;
synthetic bays), all with recorded frame hashes. Of the mutations, the
palette-1 condition of MAIN MENU/SAVE is not caught (not visible in the
recorded frames). `service_bay.real` and `//shell:gui_tour_real_test`'s
bay, WEAPONS and MISSION snaps equal VSHELL's tour frames (**V**, §41).
States a capture should still hold, after a new career: WEAPONS held
down; another bay row selected; BUILD; CREW; ARMORY; the main menu after
MAIN MENU; and SAVE (Saved games from the bay).

### 28.7 Open points

- The editors' shows and leaves are hooks; their screens are §29–§33.
- The palette-1 condition of MAIN MENU and SAVE is not covered by a frame.

## 29. WEAPONS screen

The graphical WEAPONS screen `0x43ebbc` on the widget toolkit over §10's
operations through the shell state engine
([launcher.md](launcher.md#shell-state-engine)). Own code:
`es2/shell/gui/weapons.h`, `shell/src/gui/weapons.c` (in
`//shell:screens`). Read in the listing: the constructor
`0x43ebbc..0x43fbd7`, show `0x43fbd8`, hide `0x43fc62`, the panel
`0x43fc87`/`0x43fcd4`, `0x43fd87`, `0x43fdac`, the list refresh
`0x440256`, the guidance `0x4403f9`, the handlers `0x440626..0x4414f5`,
`0x43e242`, `0x43e2d4`, `0x43e346`, `0x43e399`, `0x43e3d9`, `0x41501f`,
`0x411088`, `0x41124e` (**L**). Own tests: `//shell:weapons_screen_test`
and the `weapons` suite of `//shell:screen_test`. The mount and guidance
rules themselves are §10.2's (V there); the screen side was not run in
vitro.

### 29.1 Objects and globals (L)

- The 161 controls (`weapons` in `vshell_layout.json`): ESTitle
  `[0x48d674]` (0x9f), the list ESTitle `[0x48d678]` (0xa0), the ESArm
  `[0x48d67c]` with the record images, the ESRect `[0x48d6f8]` (frame flag
  0: draws nothing), the guidance buttons `[0x48d6fc]` ARM, `[0x48d700]`
  ARH, `[0x48d704]` SARH, `[0x48d708]` EO, the description button
  `[0x48d70c]`, three description lines `[0x48d710..0x48d718]`, 27 ESQuad
  rows `[0x48d71c + 4r]` (handler `0x440990 + 0x6f r`, each
  `0x476df4 := 1; 0x43fdac(r); := 0`), the label 0xa5 and the slot buttons
  `[0x48d800]` (`0x4408d4` → `0x43e3d9`) and `[0x48d804]` (`0x440932` →
  `0x43e399`). The text table `[0x48d808]` is `<lang>\wpn_desc.bin`
  (`0x408bb5` at `0x43fba6`).
- Row r is item `0x476a4c[r]`; its name column is ESTEXT `0x7e + ID`.
- The record images (`0x43ee2a..0x43f082`): list 1 stored at `0x48d680 +
  4·0x43fd87(ID)` (by row), list 2 at `0x48d6e8 + 4·ID` (by guidance
  mode); the arrays are adjacent (`0x48d680[27]`, `0x48d6e8[4]`,
  `[0x48d6f8]`, `[0x48d6fc]`), and the ID-0 row (no list-1 record on the
  CD) reads list 2's mode-0 pointer: leaving that row hides the SARH image
  (L; reproduced and covered by the own test).
- Globals at their image values (read from the EXE's data): slot
  `0x476658` −1, row `0x476df6` −1, guidance image `0x476df8` −1, previous
  mode `0x476dfc` −1, panel flag `0x476a82` 0.

### 29.2 Behaviour (L)

- Show `0x43fbd8`: show the three top windows; a bay selection that is −1,
  without a record or not finished (`0x411088`: record +0x4a = 100) is
  replaced by `0x43dcdd(0x41124e)` (first finished bay or −1); slot := −1;
  `0x43fdac(0)`. Hide `0x43fc62` hides the three.
- `0x43fdac(r)`: returns at once when r is the selected row, no guidance
  image is shown and no slot is selected (so a revisit with row 0 does not
  refresh the counts). With a slot and ID ≠ 0 it also returns (no UI
  change) unless the slot's word +8 equals the ID (§10.2's sic), the list
  count is not 0 or the slot holds the ID. Otherwise: `0x43e2d4` (mount
  with the flag; then the squad panel's `0x415bff(slot)` whenever a slot
  is selected, §36.2), the list refresh, the previous row restored **by
  `0x41501f` alone** (with no slot an unavailable row comes back enabled
  in 0x27), its frame 0x10 and image hidden, a shown guidance image
  hidden, the new row columns and frame 0x29, enabled, its image (or
  `[0x48d6f8]` for ID 0) shown, the lines `wpn_desc[3·ID .. 3·ID + 2]`;
  for IDs 13..16 the description label := ESTEXT `0x7e + ID`, the panel
  shown and, with a slot, `0x4403f9(slot word, 0)`; otherwise the panel
  hidden (the four buttons' frame 0x22) and `0x476df8 := −1`; the ESArm
  redrawn; row := r.
- List refresh `0x440256`: count column "  " (`0x4774c1`) for ID 0, else
  `"%d"` of the signed count word, set (align 1, colour 0x27) only for an
  available item; unavailable rows get colour 0x10 and are disabled
  without a draw; others 0x27 enabled or 0x25 disabled by `0x41501f` (true
  without a slot).
- `0x4403f9(mode, flag)`: hide the shown guidance image; the previous
  mode's button frame 0x22, the new one 0x20 (switch order SARH 0, ARH 1,
  ARM 2, EO 3); with the flag: the row image hidden, the mode image shown,
  the lines `wpn_desc[0x63 + 3·mode ..]`, `0x476df8 := mode`; then
  `0x476dfc := mode`, `0x43e346(mode)`, ESArm redrawn.
- Description `0x4407ba`: `0x43fdac(0x476df6)` without the flag (never
  mounts). Slot next `(slot + 1) mod capacity` (+0x4c), prev `slot > 0 ?
  slot − 1 : capacity − 1`; `0x43e242(s)`: nothing for the selected slot,
  else slot := s, `0x43fdac(0x43fd87(slot item ID))` and `0x415bff(slot)`.
  The squad panel's hot spots (`0x43e7ef + 0x61k`, table `0x48d61c`) also
  call `0x43e242(k)`, but they are never shown and never take a click
  (§36.3): slots are selected only with the two arrows.

### 29.3 Implementation and own choices

`es2_screen_weapons_build` (registration line in `screens.c`) builds the
table's screen, then one ESBitmap per `gam\arm_weap.dat` record from the
host's frames, moved to the ESArm's oldest end (VSHELL builds them before
`[0x48d6f8]`), and the rows' enabled state from the availability bytes.
State: the engine's memory SAV (bay view, ARM answers, `MOUNT` and
`GUIDANCE` of `es2_shell_service_bay`); the bay selection `0x482bc9` is a
pointer the host shares (`es2_shell_editor_session.bay`), changed only
through `host.select_bay` (`es2_squad_panel_select`, §28.4). The service
bay's `selected(2, …)` hook maps to `es2_weapons_screen_bay_changed`, its
`screen(2, 1/0)` to show/hide, its WEAPONS hot spots to
`es2_weapons_screen_select_slot`; the leave's rebalance is the flow's.

Own (VSHELL reads outside its objects or crashes there): a bay selection
of −1 counts as "no record" (VSHELL reads a pilot pointer); slot prev/next
and the slot selection without a record are refused (division by zero); a
slot item ID of no row does not run `0x43fdac(−1)`; a record whose array
index is outside the arrays is skipped; a guidance store the library
refuses (a non-rack slot: no path of the screen reaches one) is not made.
Each counts in `refused`.

### 29.4 Verification and capture states

`//shell:weapons_screen_test` (synthetic state); one-line mutations of
`weapons.c`, each caught. `//shell:screen_test --suite weapons`: synthetic
and real cases (CD font, `arm_weap.dba/.dat`, `arming.dpl`, English ESTEXT
and `wpn_desc.bin`, the row table and ARM files from the checked
`VSHELL.EXE`, a new career of the engine); the screen is drawn without the
bar and the squad panel. The full WEAPONS frame after the bay's WEAPONS
(no slot, row 0, bar and squad panel) equals VSHELL's tour frame in
`//shell:gui_tour_real_test` (**V**, §41). States a capture should still
hold (a new career, service bay, WEAPONS clicked; screen `weapons`):

- `slot_next`: after one click on `>` (slot 0);
- `slot_rack`: a slot holding a missile rack (guidance panel shown);
- `guidance_sarh`: after SARH on that slot;
- `mount`: after a click on an enabled row with an empty slot selected.

### 29.5 Open points

- The states of §29.4 after the first show are not compared with VSHELL's
  frames.

## 30. REPAIR screen and the Herc scrap warning

The graphical REPAIR screen `0x4326c7` and the warning `0x4479b8` on the
widget toolkit over the editor library (§12). Own code:
`es2/shell/gui/repair.h`, `shell/src/gui/repair.c` (in `//shell:screens`,
two registration lines). Read in the listing: show `0x43397c`, leave
`0x433a7b`, refresh `0x433ad5`, `0x433f43`, the grid swap `0x433fcd`, the
row texts `0x434042`, `0x43433f`, the names `0x43436f`, the row colours
`0x4344cc`, the selection `0x434549`, the handlers `0x434693..0x435403`,
the warning `0x447da1`, `0x447e25`, `0x4482c8`, `0x448326`, and the REPAIR
case of `0x43dcdd` (`0x43dd1c..0x43ddf2`) (**L**). Own tests:
`//shell:repair_screen_test` and the `repair` suite of
`//shell:screen_test`. Every state change is §12's library (V there); the
screen side was not run in vitro.

### 30.1 Objects (L)

The 168 controls (`repair` in `vshell_layout.json`): eight hidden ESGrids
`[0x48d1f8 + 4k]` (REPAIR's picture of bay k, swapped with the squad
panel's grid `[0x48d59c + 4k]`), the ESTitle `[0x48d218]` (0x3d, handler
`0x434693`: empty) holding the External panel `[0x48d21c]` (0x3e,
`0x4346ec`: empty) with sixteen ESQuad rows `[0x48d220 + 4r]` (handlers
`0x434745 + 0x65 r`), the Internal panel `[0x48d260]` (0x3f, `0x434d95`)
with nine rows `[0x48d264 + 4r]` (`0x434e30 + 0x65 r`), each row with four
columns (the name from x 2, an empty one 148..148, the slot number
148..171, the condition 171..200); the display-only buttons `[0x48d28c]`
(the mode text), `[0x48d294]` (salvage), `[0x48d2a4]` (cost),
`[0x48d2ac]` (condition word), `[0x48d2c0]` (REPAIR ALL's cost) and
REPAIR `[0x48d2b0]`, REPAIR ALL `[0x48d2c4]`, SCRAP `[0x48d2d0]`, CANCEL
`[0x48d2d4]`. The warning: a display-sized shield `[0x48da10]`, the
ESAlert `[0x48da14]` (0xc9), the text label `[0x48da24]`, CANCEL
`[0x48da28]`, ACCEPT `[0x48da2c]`.

### 30.2 Drawing from the state (L)

- Row text `0x434042(tab, row)`: c = `0x41232a` (100 without a record),
  the text `"%d%%"` (`0x4746f3`), the colour `0x43e09f(c)` = the word of
  band `0x43e05f(c)` in `0x476666` (0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x27).
  Part and system rows: only column 3 (align 1). Hardpoint rows: a slot at
  or above the capacity (`jl`, signed; 0 without a record) gets ESTEXT 1
  in columns 0, 2 and 3 (align 1, colour 0x27); otherwise column 0 :=
  `0x7e + item ID` (item word +0) or 0x7d without an item (align 0, 0x27),
  column 2 := `"%d"` of slot + 1, column 3 := the condition, or `" "`
  (`0x4746f8`) without an item, in the band colour.
- Refresh `0x433ad5`: `[0x48d2a4]` := `"%ld %s"` (the cost, ESTEXT 0xc8),
  `[0x48d2ac]` := ESTEXT `0x68 + band(c)` (the band colour is stored in the
  label's +0xb5 and overwritten by the call's 0x17 at once: a dead store),
  `[0x48d2c0]` := REPAIR ALL's cost, `[0x48d294]` := salvage − bank cost,
  all align 2, colour 0x17, drawn; then the three enable rules of §12.3,
  each as label +0xb5 0x29/0x26, frame +0x4d 0x22/0x26, redraw tree,
  +0x49, draw; then the squad panel's `0x43da0c`, `0x43e0d7` and the
  diagram `0x414cbe` (§36.2). The amounts go through a 12-byte stack
  buffer: seven digits or more with the unit write past it (not modelled;
  the own code uses a larger buffer).
- `0x43433f`: the 16 + 9 row texts, then `0x433ad5`. `0x433f43` (called by
  the squad panel's `0x43d80c` on word 3) writes the salvage as `"%ld"`
  without the unit.
- The names `0x43436f` (part and system names from `0x47409c`, for vehicle
  8 from `0x4740ba`, colour 0x27) run **only** from `0x43dcdd`'s REPAIR
  case, not from the show: a bay kept at the show keeps the names of the
  last bay REPAIR selected. They, and a hardpoint row's text, reset column
  0's colour to 0x27: after a bay change, or a repair of a hardpoint row,
  only the row's frame shows the selection.
- Selection `0x434549`: the old row frame 0x10 and column 0 0x27, the new
  row 0x29 and 0x29, then `0x433fcd(old tab, new tab)` (0 → 1: hide the
  squad grid of the bay, show REPAIR's; 1 → 0 the reverse; with no bay
  selected it reads the words before both arrays), then `0x433ad5`. The
  image's selection is (0, 0), so the first show colours no row.
- Internal panel `0x434d95`: on word 3 with tab 0 the swap as 0 → 1; the
  selection is unchanged.
- REPAIR's rows are selected only by the rows: the squad panel's hot spots
  never take a click (§36.3).

### 30.3 Show, handlers, the warning (L)

- Show `0x43397c`: show `[0x48d218]`, `[0x48d21c]`; the mode text into
  `[0x48d28c]`'s label without redraw (its own alignment and colour); show
  `[0x48d260]`; the bay check (§12.3, `0x43dcdd` in the previous word's
  case); `0x43433f`; the snapshot; `0x434549(0, 0)`. Leave `0x433a7b`
  hides the three panels.
- REPAIR: the operation, the selected row's text, `0x433ad5`. REPAIR ALL
  and CANCEL: the operation, `0x43433f`. SCRAP: the warning.
- Warning `0x447da1`: `[0x48da24]` := `"%d %s"` (`0x478c41`) of `0x411a33`
  (`0x414174` / 1000, `cdq`/`idiv`) and ESTEXT 0xcd, drawn; show the
  shield, then `0x40b7e9` (the shield again, the box). `0x447e25` hides the
  shield, then `0x40b808`. CANCEL `0x4482c8`: hide only. ACCEPT
  `0x448326`: hide, `0x40ed0f`, `0x43d80c`; word 4: `0x447064`; word 3:
  `0x43dcdd(0x41124e)` and `0x433ad5`; then the redraw trees of
  `[0x48d21c]`, `[0x48d2ac]`, `[0x48d5d4]`. BUILD's SCRAP (`0x447570`)
  shows the same warning (§31): it calls `es2_herc_scrap_box_show` with
  this screen's object; ACCEPT on word 4 hands the refresh to
  `host.squad(0x447064)`.
- REPAIR's part of `0x43dcdd` after the squad panel's: `0x434549(0, 0)`,
  `0x43436f`, `0x43433f`, `0x433f86`.

### 30.4 Implementation and own choices

The host keeps the SAV of the memory state (`*save`), the shared
`es2_shell_editor_session` (the service bay's `b->session`, §28.5) and the
executable tables; each handler runs one `es2_shell_editors` operation and
redraws from the library's view (a row's condition: the view of a session
copy with that row selected). The show opens the library with the flow's
previous word, a WEAPONS or BUILD word given as −1 (the flow ran their
rebalance; the bay decision is the same). The squad panel's parts go
through `host.squad(routine, a, b)`: grid hide/show `0x41fabd`/`0x41f93a`
of `[0x48d59c + 4k]`, the selection `0x43dcdd` (called with the session's
bay still the old one; a host with the service bay calls
`es2_squad_panel_select`, which stores the bay and on word 3 calls back
`es2_repair_screen_selected`; without it the screen stores the bay and
draws REPAIR's case itself), `0x43da0c`, `0x43e0d7`, `0x414cbe`,
`0x43d80c`, `[0x48d5d4]`'s redraw and BUILD's `0x447064`. The service
bay's hooks: `host.screen(3, 1/0)` → `es2_repair_screen_show(rs,
b->session.screen)` / `_hide`, `host.selected(3, …)` →
`es2_repair_screen_selected`, `host.repair_grid(k)` →
`es2_repair_screen_grid`. The grids' Herc pictures come through the
`panel` hook and `host.squad`'s `0x414cbe` (§36). Own behaviour: what
reads a pilot as a record without a bay selection is refused by the
library (REFUSED, nothing drawn); the grid swap without a bay does
nothing.

### 30.5 Verification and capture states

`//shell:repair_screen_test` (the synthetic state `repair_fixture.h`):
build, show and mode texts, every row kind, the band limits 89 and 0, rows
through the input path, REPAIR, an empty slot, REPAIR ALL, CANCEL, no
salvage left, the grid swap, the bay selector with vehicle 8's names, the
warning's CANCEL and ACCEPT twice, a squad-panel host that stores the bay
itself (the same SAV and session as without), no finished Herc; one-line
mutations of `repair.c`, each caught. `//shell:screen_test --suite
repair`: synthetic and real cases (CD font, `palette.dpl`, the English
ESTEXT over the synthetic state) with recorded hashes; only this screen is
drawn (no bay background, bar or squad panel). States a capture should
hold (a campaign in the bay, REPAIR clicked with a finished Herc in the
selected bay, screen `repair`):

- `shown`: after the REPAIR bar button;
- `external_row`: after a click on External row 2 (Right Torso);
- `repaired`: after REPAIR on that row; `repair_all`: after REPAIR ALL;
  `cancel`: after REPAIR, then CANCEL;
- `internal_row`: after a click on Internal row 2 (the grids swapped);
- `scrap_box`: the warning after SCRAP; `scrap_accept`: after its ACCEPT;
- `no_herc`: REPAIR with no finished Herc (bay −1).

### 30.6 Open points

- Pixel equality with VSHELL's frames is unchecked; the screen side was
  not compared in vitro.

## 31. BUILD and ARMORY screens, weapons scrap warning

The graphical BUILD screen `0x445de8`, ARMORY screen `0x4484c4` and the
weapons scrap warning `0x447e3e` on the widget toolkit. Own code:
`es2/shell/gui/build_armory.h`, `shell/src/gui/build_armory.c` (in
`//shell:screens`). Every state change is the editors library (§12, §17)
on the loaded game's SAV; the screen side was read in the listing:
`0x446ec5..0x4479b7`, `0x447e3e..0x4484c3`, `0x449804..0x44b2fe`,
`0x414340..0x4144b4` and `0x415dc1..0x415fa0` (**L**). Own tests:
`//shell:build_armory_test` and the `build_armory` suite of
`//shell:screen_test` (synthetic data). Nothing here was run in vitro.

### 31.1 BUILD (L)

- Show `0x446f9d`: rows `0x446ec5` (HERC_INF record t's word +0x0e, the
  nine-word bank from the SAV, ≠ 0: the four column colours 0x27, `+0x49`
  1, drawn; else `+0x49` 0, colours 0x10), texts `0x44738a`, enables
  `0x447064`, the Herc pictures `0x415dc1`, show `[0x48d968]` (panel),
  `[0x48d9bc]` (lower panel), `[0x48d96c]` (grid of type 0), then
  `0x4472cb(0)`. Leave `0x446fde`: `0x415f4c` (every grid's layers off,
  the nine DBAs freed), hide the two panels and `[0x48d96c + 4·type]`; the
  type is kept.
- `0x4472cb(t)`, nothing for the selected type: the old row's column 0
  colour 0x27, frame `+0x4d` 0x10, tree redraw, its grid hidden; the new
  row's column 0 and frame 0x29, tree redraw, its grid shown; store, then
  `0x447064` and `0x44738a`. The type rows' handlers are
  `0x44764f + 0x61·t`.
- Texts: `[0x48d9e4..0x48d9f0]` = HERC_INF[type] +2 "%d %s" (ESTEXT 0xc6),
  +4 "%d %s" (0xc7), +6 "%d", +8 "%d %s" (0xc6) (signed words; HERC_INF in
  memory at `0x483c38`, 16 bytes per record); `0x447501`: `[0x48d9f8]` =
  (salvage − `0x412baa`) / 1000 unsigned, "%ld %s" (0xc6). Each label
  keeps its own alignment and colour and is drawn.
- `0x447064` writes the two buttons as: on = label colour 0x29, frame
  0x22; off = 0x26 / 0x26; then the tree redraw, `+0x49`, the draw. Scrap
  is `[0x48da08]`, Build `[0x48da0c]`; the rules are §12.4/§12.5.
- Build `0x4475ce`: `0x40eed4(type)`, the squad panel's `0x43e0d7`,
  `0x43e167`, `0x43da0c`, then `0x447501`, `0x447064`; it reloads no
  squad-panel picture (the BUILD pictures are `0x41547f`, §36.1). Scrap
  `0x447570`: the Herc scrap warning `0x447da1` (§30.3; host
  `herc_scrap`, whose ACCEPT ends in `es2_build_refresh`). The squad
  panel's BUILD case of `0x43dcdd` runs `0x447064` only.
- The pictures: `0x414340` reads at start-up the nine files
  `gam\rpr_{outl,rapt,toma,sams,colo,apoc,ogre,mavr,razr}.dat` (names at
  `0x46ffd4`; type t = file t): a word count, then per record a word
  layer, a dword x, a dword y, a word frame and a word mirror (more data
  follows, the squad panel's). `0x415dc1` loads `dba\rpr_*.dba` (names at
  `0x470124`) and sets each record as layer (layer word) of the type's
  ESGrid: x, y, the DBA frame, mirror, the first recolour pair 0x0e → 0x0e
  (`0x415e78`, `0x415f17`). The mirror is read as a dword whose high word
  the loader never writes (**H**: taken as 0).

### 31.2 ARMORY (L)

- The constructor's rows `0x44981a..0x449981`: 26 ESQuads (8, 0x3c + 12r,
  0xf8, 0x48 + 12r) under `[0x48da5c]`, columns "[   ]", the weapon name
  (the weapon record's name pointer `+0x10`, set at the table load from
  `<lang>\weapons.bin` entry ID: `0x41267b..0x412690`), "", and the cost
  word / 1000 "%d"; frame off. Then `<lang>\wpn_info.bin` into
  `[0x48dbf8]`. The weapon record of category c is `0x483cc8 + 0x1d·c`
  (cost `+0x14`, flag `+0x16`, count `+0x17`).
- The images `0x448712..0x448853`: per record of the first list of
  `gam\arm_weap.dat` an ESBitmap under the ESArm `[0x48da60]` with (x, y,
  x + frame word +6, y + frame word +4) of `dba\arm_weap.dba`, colour 0x27,
  frame off, stored at `[0x48da64 + 4·0x43fd87(ID)]` (`0x43fd87`: first
  index of the ID in the 27 words at `0x476a4c`, −1 if none).
- Show `0x449b87`: Clear `[0x48dad8]` on/off by `0x4825c9` (as
  `0x447064`'s buttons), rows `0x4499b9`, texts `0x44a33b`, show
  `[0x48da58]`, `[0x48da5c]`, `0x449d2f(0)`. Leave `0x449c46`: hide
  `[0x48da64 + 4·row]`, the two panels, row := −1.
- `0x4499b9`: rows whose flag is set: `+0x49` 1, column 0 "[ %1d ]" with
  `0x412c66` or "[   ]" (left, 0x27, drawn), column 2 the item count "%d"
  (right, 0x27, drawn), the four colours 0x27; others `+0x49` 0, colours
  0x10.
- `0x44a33b`: the label of `[0x48dadc]` (the "+" button) := salvage −
  `0x412baa` "%ld %s" (ESTEXT 0xc8), of `[0x48dae0]` ("−") := `0x412baa`
  "%d %s", `[0x48dae8]` := the free count `0x46fa14` "%d", `[0x48daf0]` :=
  5 − free; all centred, colour 0x29, drawn; with a row selected its
  column 2 := the count (right, 0x29) and its tree redraw. So the two
  "+/−" buttons (empty handlers, §17.4) are the salvage and bank-cost
  displays.
- The selection (in `0x449d2f` and `0x44a06e` when not editing): the old
  row's columns 0x27, drawn, its image hidden; the new row's columns 0x29,
  drawn, its image shown; the five labels `[0x48dafc..0x48db0c]` :=
  `wpn_info.bin` entries 5s .. 5s + 4 with s = `0x43fd87(category)`; the
  ESArm's tree redraw; store. The edit: column 0's mark (left, 0x29) and
  `0x44a33b` (add: after `0x412c0b`; remove: Clear, then the re-adds, each
  with its mark). Clear `0x449ccc`: with a row, `0x412c31`, "[   ]",
  `0x44a33b`.

### 31.3 Weapons scrap warning (L)

Scrap `0x44a508` with a row selected → `0x448227(category)`: `0x48da50` :=
category, `[0x48da44]` := `0x412c8e` "%d %s" (ESTEXT 0xcd), show the
shield `[0x48da30]`, then the modal show `0x40b7e9` of `[0x48da34]`. Hide
`0x4482ad`: hide the shield, then `0x40b808`. CANCEL `0x4483e9`: hide.
ACCEPT `0x448447`: hide, `0x40ed6a`, `0x412a37`, `0x4499b9`, `0x44a33b`
(the selected row stays selected but is drawn in 0x27).

### 31.4 Own choices

- The first BUILD show reads HERC_INF[−1] (type −1) for its texts and
  enables before `0x4472cb(0)` rewrites them; the own screen skips those
  four texts (same end state).
- Without a bay selection VSHELL reads `hangar[−1]` in `0x447064`; both
  buttons are off here (`editors.h`).
- Item IDs of `arm_weap.dat` found only as the 27th word of `0x476a4c`, or
  nowhere, get no image here (VSHELL stores them over `[0x48dacc]` /
  `[0x48da60]`); a later record of the same row replaces the earlier
  image.
- **Double leave.** The session is the library's, shared with the service
  bay (`es2_build_armory_game.session` → `es2_service_bay.session`); the
  show runs the library's OPEN, which also runs the previous screen's
  leave (`0x412a37` after WEAPONS, REPAIR, BUILD). The flow's
  `host.rebalance` runs the same step for the bar's event, so a graphical
  host must run only one of them: `app.c` (`open_from`) opens the library
  from the bay word −1 when the previous word is 2, 3 or 4, as REPAIR's
  show does (§30.4).

### 31.5 Verification and capture states

`build_armory_test` and the `build_armory` suite of `screen_test`
(synthetic cases, reference fields NULL); one-line mutations of
`build_armory.c`, each failing one of the two tests. No real-data case:
the screens need a loaded campaign SAV. States a capture should hold, all
from a campaign in the bay (mode 1):

| State | Action |
|---|---|
| `build:shown` | BUILD from the bay, bay 0 selected |
| `build:type1` | then the second type row |
| `build:empty_bay` | a bay without a record selected |
| `build:after_build` | then Build (enough salvage) |
| `armory:shown_auto` | ARMORY with weapons building auto |
| `armory:shown_manual` | ARMORY with weapons building manual |
| `armory:row` | another row (left click) |
| `armory:add`, `armory:remove` | manual: left, then right click on the selected row |
| `armory:scrap_box`, `armory:after_scrap` | Scrap, then ACCEPT |

### 31.6 Open points

- Not compared with VSHELL's frames; the Herc picture layer format and the
  `arm_weap.dat` geometry are listing readings only.

## 32. CREW screen

Own code: `es2/shell/gui/crew.h`, `shell/src/gui/crew.c` (in
`//shell:screens`), registered as `ES2_UI_SCREEN_CREW` in
`shell/src/gui/screens.c`. Own tests: `crew_test` and `screen_test` (suite
`crew`). Read in the listing, range `0x441548..0x442bc3` (**L**); the
state rules are §17's (V there); the screen side was not run in vitro.

### 32.1 Constructor `0x441548` (L)

Opens `dba\star.dba` (`[0x4774c8]`) and `dba\c_pilots.dba` (`[0x4774cc]`),
asserting both (`0x44e560`, source `src\wcrewi…`). The 40 controls
(`crew` in `vshell_layout.json`); the run-time parts are pictures: the
three group images `[0x48d810 + 4g]` start with c_pilots member 0, frame
colour `+0x4d` 0x21; the order-row images `[0x48d834 + 4o]` share their
row's handler, row 0 (the player) shows star member 0 for good, rows 1..3
start empty. Clicking the centre of an order row hits one of its labels
(no handler); its image is the clickable part.

### 32.2 Show `0x442091` (L)

In order: `0x441ee7` (row o below the capacity `0x482b5c`, signed: ESArm
`+0x59` 0x25, captions `+0xb5` 0x19, values `+0xb5` 0x16 and `+0xc5`
0x25; else 0x10, 0x1a, 0x17, 0x10; fields only); `0x442198` (order
pointers, §17.2); each group image `+0x5d` := c_pilots member *[selected
pilot +0]* (the word at the record's start, the SAV prefix's first word)
and drawn; order rows `0x442215(0..3)` and `(0)`; texts `0x4422df`;
`0x43dcdd(k)` for each bay with a record; the frame `[0x48d80c]` shown.
The leave `0x44218a` only hides the frame.

### 32.3 Texts `0x4422df` (L)

Every value is set by `0x40b8bb(label, text, 0, 0x29, 1)`, so the text
colour is 0x29 whatever `0x441ee7` wrote. Row 0: the player's name
(`0x482b64`), ESTEXT `0x35 + word +0x25` (a 16-bit sum: the word holds
the skill byte and the byte after it), vehicle ESTEXT 0x7e when
`0x411164(player bay)` is -1, else `0x6e + vehicle`. Rows 1..3: without a
pointer the image `+0x5d` := 0 (drawn), the three values ESTEXT 0 and the
row redrawn; else the image is c_pilots member *[pilot +0]*, name `+4`,
skill and vehicle as for row 0 with the pilot's `+0x25` and bay `+0x22`.

### 32.4 Handlers (L)

Group images `0x4427e1`, `0x44288c`, `0x442937`: the image's `+0x4d` :=
0x29 and the other two 0x21 (redrawn, also with order 0), then
`0x442548(g)`, which after the assignment runs `0x43e167`, `0x43da0c`
(labels of the selection) and `0x4422df` once more. CLEAR `0x4429e2` →
`0x442624`; order rows and their images `0x442a40`, `0x442aa1`,
`0x442b02`, `0x442b63` → `0x442215(0..3)`, whose row and image colours are
0x21 for the previous order and 0x29 for the new one. The assignment
`0x4426e5` ends in `0x4422df` except on its early return (a wingman order
without pointer). `0x442198`, `0x442548` and `0x4426e5` assert a group
below 3 (`0x44e560`), which `0x4108fa` guarantees.

### 32.5 Own structure

The editors library (`es2_shell_editors`, §17) is the state: the screen
asks the host to apply OPEN 6 (on a copy of the session with word -1, so
no leave runs twice, §31.4), CREW_ORDER, CREW_PILOT, CREW_CLEAR, and for a
selector the BAY operation (on a copy whose selection is -1, since the
squad panel has already stored k, §28.4); the UI order above runs around
those calls with the squad panel's functions, and the library's session
replaces the screen's afterwards. `crew_test` checks that a click sequence
leaves the same SAV bytes and session as the same operations given to the
library directly. The selection -1 with the flag set and a pilot (state
change of `0x4426e5` for pilots without a bay) is not reachable (only the
eight selectors and CLEAR set the flag; CLEAR empties the order first) and
is not reproduced.

### 32.6 Verification and capture states

`crew_test`: build, show, capacity colours, leave, orders, pilot lists,
selector refusal and assignment, CLEAR, library equivalence. `screen_test
--suite crew`: a synthetic and a real tour (CD fonts, `c_pilots.dba`,
`star.dba`, `mnu_bttn.dba`, `bay2a_84`, palettes, English ESTEXT;
synthetic team) with recorded frame hashes, references NULL; the
mutations are caught. States a capture should hold, from a new career's
bay: CREW shown; order row 3 selected (click its image); a group image
clicked (that pilot gets the order); bay 0's selector (assignment); CLEAR;
order row 0; WEAPONS after CREW.

### 32.7 Open points

- Not compared with VSHELL's frames.

## 33. Mission screen, pages 0, 1 and 4

The graphical mission screen `0x442bc4` (`src\wmissini.cpp`) on the
widget toolkit. Own code: `es2/shell/gui/mission.h`,
`shell/src/gui/mission.c` (in `//shell:screens`). Read in the listing: the
constructor `0x442bc4..0x4445c2`, the images `0x4445c3..0x44483d`, the
open `0x444873..0x444fa3`, `0x444fa4`, the leave `0x445095`,
`0x445289..0x4454b6`, the handlers `0x4454b8..0x445de6`, ESREPORT
`0x40d1bc..0x40d834`, `0x41f243`, `0x43aee7`, the results path `0x40f62d`
and Use Default `0x44db97..0x44dbd4` (**L**). Own tests:
`//shell:mission_screen_test` and the `mission` suite of
`//shell:screen_test`. §4.8, §14 and §16 hold the texts and statistics;
this section adds the screen.

### 33.1 Objects (L)

- The 55 controls (`mission` in `vshell_layout.json`): the Telecomm frame
  `[0x48d8b4]` (ESTitle 0xb0) with the terradef bitmap `[0x48d8c4]`
  (handler `0x4454b8`); the page frame `[0x48d8b8]` (ESTitle, label " ":
  the page title) with the six map arrows `[0x48d8c8..0x48d8dc]`
  (`dba\miss_arw.dba` frames 1/0, 3/2, 10/8, 11/9, 7/6, 5/4; auto-repeat
  `+0x61` = 1; handlers `0x445577`, `0x445646`, `0x445715`, `0x4457e4`,
  `0x4458b3`, `0x445982`), the theatre-image ESGrid `[0x48d8f4]`, an
  unused ESPal `[0x48d8f8]` and the twenty statistics labels of §14.1; the
  text frame `[0x48d8bc]` (ESTitle 0xaf) with the page arrows `[0x48d910]`
  (`0x445c79`) and `[0x48d914]` (`0x445d2a`); the button row `[0x48d8e0]`
  with Mission Briefing 0xb4, Mission Objectives 0xb6, Intelligence Report
  0xb7, Rock & Roll 0xb8; the whole-display ESBitmap `[0x48d8c0]` for the
  theatre picture.
- Five ESREPORT objects with FONT2 under `[0x48d8bc]`, not in the layout
  table (`0x443686..0x44383b`, in this order): `[0x48d8fc]` (10, 21)-(613,
  168), then `[0x48d900]`, `[0x48d904]`, `[0x48d908]`, `[0x48d90c]` with
  (10, 21)-(575, 115). `0x445343`'s two jump tables (`0x44536e`,
  `0x44541a`) name them by panel number: 0 campaign `[0x48d8fc]`, 1
  briefing `[0x48d900]`, 2 objectives `[0x48d908]`, 3 intelligence
  `[0x48d90c]`, 4 debrief `[0x48d904]`.
- Globals: text panel `0x478140` (−1), the state word `0x477944` (1 on
  page 0, 2 on page 1, 0 at the leave; no reader in the listing), the map
  animation pending `0x46c269` and its palette `0x46c26a` (main-loop step
  6, `0x4019c6`), the image objects `0x47805c` (terradef), `0x478060`
  (th_earth/th_moon), `0x478064` (theatre picture).

### 33.2 Open `0x444873(page)` (L)

1. `0x4445c3(page)`: `dba\terradef.dba` loaded **on every open**, its
   first frame into `[0x48d8c4]` +0x5d and drawn; page 0 also
   `dba\th_earth.dba` (group word `0x46fc58` < 5) or `dba\th_moon.dba` as
   layer 0 of `[0x48d8f4]` at (0, 0) with `0x40bd58`'s defaults (sources
   0x0e, targets 0x10, no mirror).
2. `0x41f243([0x48d8bc], (7, 307)-(632, 423))`: the text frame's
   rectangle, the subtree's absolute rectangles (`0x41f599`) and a redraw.
3. Page 0: statistics hidden (`0x444fa4`), the campaign text set, the
   frame enlarged to (7, 307)-(632, **476**), the group and theatre movies
   (§19.2), the title `"%s %s"` (§16.1), the theatre image shown,
   `0x477944` := 1.
4. Page 1: statistics hidden; objectives, intelligence, briefing set (in
   that order); `0x46c26a` := group − 1 + 5; Objectives, Intelligence and
   Rock & Roll get label colour 0x29, frame 0x22, a redraw, `+0x49` := 1
   and a draw (Mission Briefing is not touched); title ESTEXT 0xb3;
   `0x477944` := 2; `0x46c269` := 1; the briefing movie.
5. Page 4: the debrief text; `0x46c26a` := group − 1 + 10; **Rock & Roll
   label colour 0x26, frame 0x26, `+0x49` := 0**; title ESTEXT 0xb9; the
   debrief movie. The statistics are not touched (§14.2's rule).
6. Every page: show `[0x48d8b8]`, `[0x48d8b4]`, `[0x48d8c4]`,
   `[0x48d8bc]`; the page arrows hidden on page 0, shown otherwise; on
   page 1 the button row, its four buttons and the six map arrows shown
   and `0x4452d9(1)`, on every other page all of them **hidden** (so Rock
   & Roll is not reachable on pages 0 and 4); finally `0x445343(page)`.

The first MISSION after CONTINUE opens page 0 and sets `0x477946` := 1;
the second opens page 1, also with `-a` (no movie needed; V by the tour,
§41.1).

### 33.3 Text panels (L)

- `0x40d231` (setText) starts with the free `0x40d3db`, which sets the
  visible word +0x18 and the current page +0x1e to 0: a new text is hidden
  until `0x445343` shows it, on its first page.
- `0x445343(p)`: redraw of `[0x48d8bc]`; the panel in `0x478140` gets
  visible := 0 and `0x40d492` (its lines hidden); panel p visible := 1 and
  `0x40d492`; `0x478140` := p (also for p outside 0..4, which shows
  nothing).
- `0x4452d9(p)`: `0x445289` sets the frames of Briefing, Objectives and
  Intelligence to 0x22 with a redraw; p = 1 or 4 Briefing, 2 Objectives, 3
  Intelligence then 0x20. Mission Briefing `0x445a51` uses the page word
  `0x481108` for both calls; Objectives `0x445ac7` 2; Intelligence
  `0x445b30` 3.
- Page arrows `0x445c79` / `0x445d2a`: redraw of `[0x48d8bc]`, then the
  previous / next page (`0x40d7ed`, `0x40d80e`; only while visible) of the
  panel in `0x478140` for **1..4 only**: the campaign text of page 0 never
  turns (its arrows are hidden there anyway).
- The leave `0x445095`: `0x477944` := 0; `0x4446d1` (the grid's layers
  off, both images freed; `[0x48d8c4]` +0x5d keeps the freed terradef
  pointer); statistics hidden; panels 1..4 freed (**not** the campaign
  panel); `[0x48d8b8]`, `[0x48d8b4]`, `[0x48d8c4]`, `[0x48d8bc]`,
  `[0x48d8e0]`, `[0x48d8f4]` hidden.

### 33.4 Handlers and entries (L)

- Rock & Roll `0x445b99`: §11.1's checks with the warning box
  `0x44d90c(n)`, else `0x40f68c`, exit code 2 (`0x408d1a(2)`), quit flag;
  the flow's `ES2_FLOW_ROCK_AND_ROLL`.
- Telecomm `0x4454b8`: inert (§14.4).
- The map arrows call the map object `[0x46f3a4]` (vtable +0x0c, +0x10,
  +0x14, +0x18, …) one to four times by the button's repeat count +0x65
  (< 3, < 6, < 9, else) and `0x406264`; the screen hands the address to
  the map (§34). `0x445121` (no direct caller) hides the grid and selects
  palette group + 4 (§34).
- **Results path:** advance 2 stores the page word 4 at `0x40f62d` before
  the DEBUG dialog; Use Default (mode 1) builds the map object, the page-1
  texts, the slot-10 statistics, shows the bay (`0x43b7f2(8)`,
  `0x43b758`) and calls `0x43aee7(0x481108, 1)`: page word, the leave of
  word −1 (nothing), `0x43b7f2(7)`, `0x444873(4)`, word 7 and a synthetic
  press and release posted to MISSION (which the bar handler ignores, word
  7). The flow sets `page` := 4 there (`flow_test`, §18.1);
  `es2_mission_screen_open(page, 1)` is the screen's part, called by the
  process frame (§35, §39).
- Theatre step `0x44472f` (the player's, §14.5): pictures `0x478068` =
  `dba\alph2.dba`, `dba\delt1.dba`, `dba\omic1.dba`, `dba\brav1.dba` for
  group 1..4 (frame 0 into `[0x48d8c0]` +0x5d, shown, palette 0xf +
  group − 1); group 5 the dropship; other groups read past the four-entry
  stack copy (refused). `0x44483e` hides `[0x48d8c0]` and frees the
  picture.

### 33.5 Implementation and own choices

`es2_screen_mission_build` (registration line in `screens.c`);
`es2_mission_screen_show`, `_leave`, `_panel`, `_buttons`, `_set_text`,
`_page`, `_statistics`, `_hide_statistics`, `_open`, `_theatre`,
`_theatre_hide`, `_press`. Movies go into the queue with
`es2_shell_movies_mission` (once-flags there, §19); the host plays them.
Texts come from the host (`texts.h`, §16). Own: each text panel is one
report window ([vshell_widgets.md](vshell_widgets.md)) that holds no
layout while hidden (VSHELL's hidden line labels do not draw, also on a
redraw queued before the hide, `0x40b9fb`); the class draws VSHELL calls
directly (vtable slot 1 on `[0x48d8c4]` and the buttons) are made only on
shown windows (a hidden one is drawn by its show before anything is
presented); refused (counted): a null text (VSHELL faults in `strlen`),
the breaker's assertion, a line or title of 100 bytes or more, a theatre
group outside 1..5, `_open` without the bay or with an editor's word, a
statistics widget outside the twenty.

### 33.6 Verification and capture states

`//shell:mission_screen_test`; one-line mutations of `mission.c`, each
caught. `//shell:screen_test --suite mission`: synthetic and real cases
(CD `font2.dfn`, `miss_arw.dba`, `terradef.dba`, `th_earth.dba`,
`cam_er.dpl`, `br_w2.dpl`, `db_w2.dpl`, English ESTEXT and
`campaign.str`; page-1/4 texts generated), drawn without the bay under the
screen. On the CD `br_w2.dpl` and `db_w2.dpl` decode to the same table.
Against VSHELL's tour frames (§41, §42): page 0 of Alpha sector over the
bay equals in `//shell:gui_tour_real_test` (**V**); page 1, objectives
and intelligence are §42's. States a capture should still hold (screen
`mission`, movies skipped):

- `briefing_next`: page 1, one click on the lower page arrow;
- `debrief`: a `-X3`/`-X4` results-path start with advance 2 (the page-4
  entry through Use Default: the statistics visible, §14.2);
- `warning`: Rock & Roll with an unarmed Herc (the box of §22 over page 1).

The suite's `debrief_again` case (a page-4 revisit after a leave) has no
capture: page 4 opens only through `0x43aee7` from the DEBUG dialog
(results path, or ACCEPT with the page word still 4), and MISSION always
stores 0 or 1; the case documents the own reading of §14.2 only.

### 33.7 Open points

- `0x477944` has no reader in the listing (a run-time pointer is not ruled
  out).
- The page-4 and warning states are not compared with VSHELL's frames.

## 34. Mission map

The map of MISSION page 1 is not a widget: `shell/src/gui/mission_map.c`
(`es2/shell/gui/mission_map.h`, in `//shell:screens`) reproduces the map
object `0x40e75f`, its terrain image `0x427634`, the paint `0x425a5e`, the
fly-over `0x4262cf` with the skip `0x425a43` and the six arrows, drawing
into the frame inside (291, 67)-(585, 292). The contract (the
rasterizers, the projection, the state-4 correction, the refusals) is in
[vshell_mission_map.md](vshell_mission_map.md#14-own-implementation)
section 14; `//shell:mission_map_real_test` compares it with VSHELL's own
code in vitro (**V**). The mission screen (§33) calls
`es2_mission_map_build`, `_step` (main-loop step 6), `_skip`,
`_arrow_steps`/`_arrow_step` and `_paint`; no flow handler changes. The
fly-over's timing in the graphical shell is §42.

## 35. Presenter details of the graphical shell

The graphical shell `es2shell` (`shell/src/gui/main.c`, `shell/src/gui/app.c`)
is the process frame around the screens of §22–§34: it owns the flow's
host callbacks, the shared host `shell/src/host.c` and the main loop.
Facts below are **L**; own test `//shell:gui_route_real_test` (es2shell
against es2menu).

- **Language word.** WinMain `0x406ab8` loads `prefs.cfg` (`0x40dc44` at
  `0x406b28`) before it calls the start-up `0x401aa4` (`0x406bd5`), whose
  defaults `0x40796c(0)` (`0x401b15`) copy prefs byte +0x2b, zero-extended,
  into the dword `0x48235e` (`0x4079b5..0x4079bd`); the parser then lets
  `-f`/`-g`/`-e…` overwrite it. `0x408bb5` prefixes `eng\` for 0, `fre\`
  for 1, `ger\` for 2 and nothing for any other value
  (`0x408c07..0x408c17`). Own: `es2_host_language_dir` (`main.c`; own
  test: five cases).
- **The 0x2f notice** (§23.1) is `0x401a81(6, 2, 0x30)`: error.str's
  group-0 pointer 6 and group-1 pointer 2 as the start-up reader stored
  them ([vshell_process.md](vshell_process.md) §4 step 7); slots the file
  does not fill stay NULL, so the English file gives an empty text. Own:
  `es2_host_error_text` (`host.c`; own test: with and without message 6).
- **DEBUG dialog with `-@`** (§4.11, §22): es2shell shows it after the
  practice launch's new game and launches on its Use Default or ACCEPT;
  on the mode-1 paths (the end of a new career, `0x41308e`, and the
  results path's `0x40f647`, advance 2) it shows it through
  `es2_host_debug_ask` / `es2_host_debug_answer` (§39).

## 36. The squad panel's Herc pictures, the diagram and the condition

The picture parts of the squad panel's show and hide (§28.4: `0x43cfa5`,
`0x43cfea`), REPAIR's Herc diagram `0x414cbe`, WEAPONS' slot marker
`0x415bff` and the condition `0x4121f8`. Own code:
`es2/shell/gui/squad_pictures.h`, `shell/src/gui/squad_pictures.c` (in
`//shell:screens`) and `es2_shell_overall_condition` /
`es2_shell_record_condition` / `es2_shell_condition`
(`es2/shell/campaign.h`). Read in the listing: `0x4121f8`, `0x4142f0`,
`0x414340`, `0x4146cd..0x414a3b`, `0x414a3c`, `0x414bd9`,
`0x414cbe..0x41501e`, `0x41508f`, `0x41547f..0x415a14`, `0x415a15`,
`0x415bff..0x415dc0`, `0x43c830..0x43ce79`, `0x43ce7a`,
`0x43cfa5..0x43d028`, `0x43df86..0x43dfe7`, `0x41f287..0x41f598`,
`0x41f1b3..0x41f242`, `0x46a05c`, `0x4086b7` (**L**; the condition also
**V**, §36.4). The tables are
[herc_pictures.md](../formats/herc_pictures.md). DBA names are the
executable's tables `0x4700b4` (`dba\<x>_bod.dba`, entry 9
`dba\mt_3qtr.dba`), `0x4700dc` (`_wep`), `0x470100` (`_out`), `0x47001c`
(`dba\rpr_<herc>.dba`, entry 9 `dba\rpr_wpns.dba`) and `0x470044`
(`dba\<herc>_int.dba`).

### 36.1 The loads and unloads (L)

`0x43cfa5(w)`: 2 → `0x41547f`, `0x43c830(2)`; 3 → `0x4146cd`,
`0x43c830(3)`; 4, 6 → `0x41547f`. `0x43cfea(w)`: 2 → `0x415a15`,
`0x43ce7a`; 3 → `0x414bd9`, `0x43ce7a`; 4, 6 → `0x415a15` (CREW's leave
passes 2, §28.3). A layer is set with `0x40be7f(grid, index, record)` (the
whole 0x38-byte record is copied); the record is initialised once per call
by `0x40bd58` (ten pairs 0x0e → 0x10, i.e. off) and its fields carry over
between the layers a call sets.

- `0x41547f` (WEAPONS, BUILD, CREW): the panel without a selection
  `[0x48d5bc]` gets layers 0 and 1: `mt_3qtr` frames 0 at (1, 1) and 1 at
  (1, 0x8c). Per bay k: the panel `[0x48d59c + 4k]` is moved to (5, 0x2b)
  (`0x41f471`: clamped into the parent, size kept, then `0x41f599` and a
  redraw), width 0xe7 (`0x41f287`), height 0x105 (`0x41f33a`; the table
  builds it 0x106 high), grid lines `+0x6e5` := 0. Without a record
  (`0x411164` = -1) the same two `mt_3qtr` layers. Otherwise the body and
  outline records (`0x484444`, `0x48452e`): finished (`0x411088`: `+0x4a`
  = 100) the record's frame of `<x>_bod`; progress 0 `mt_3qtr` frame 0 /
  1; progress up to 50 (`jg 0x32`, signed) `_bod` frames 2 / 3, above 4 /
  5; the record's point and mirror. Then for each slot s below the
  capacity `+0x4c` whose item ID (slot pointer `+0x50 + 4s`, 0 for null)
  is neither 0 nor -1: the arm list record of (ID, key s + 2)
  (`0x4142f0`), unless missing or its frame is -1: layer = the record's
  layer word, `<x>_wep` frame, point, mirror. A Build (`0x4475ce`) does
  not reload these pictures (§31.1).
- `0x4146cd` (REPAIR): per bay the panel is moved to (0x10, 0x2f), width
  0xd0, height 0x100, grid lines 1. With a record: each rpr part layer
  (`0x484e0c`) from `rpr_<herc>` with pair 0's source 0x0e; each mounted
  slot's rpr list record (key s + 6) from `rpr_wpns` with source 0x0f;
  then REPAIR's panel `[0x48d1f8 + 4k]` gets the internal record's layer
  from `<herc>_int` with mirror 0 (pair 0's source as the last layer left
  it); type 8 also layer 1 = `razr_int` frame 1 at (0x1d, 0x0e). At the
  end `0x414cbe`.
- `0x415a15` clears `[0x48d5bc]` and every bay's panel (`0x40c182`),
  freeing the DBAs; `0x414bd9` clears every bay's squad and REPAIR panel
  (not `[0x48d5bc]`).

### 36.2 The diagram `0x414cbe`, the marker `0x415bff` (L)

- `0x414cbe`, for the selected bay `0x482bc9` (nothing when `0x411164` is
  -1, also for a bay below 0): with REPAIR's tab `0x474094` = 0, per rpr
  part layer L the condition `0x41232a(rec + 8, 0, L < 16 ? L : L - 16)`
  (100 without a record), its band colour `0x43e09f` stored in pair 0's
  target of layer L of the squad panel (`+0x75 + 0x38 L`; asserted
  0 ≤ L < 30); per slot s below the capacity family 2's word s into layer
  s + 6; then the panel's draw (vtable +4). With tab 1: REPAIR's panel
  layer 0 gets nine pairs, sources the nine words at `0x46ffc0`, targets
  the band colours of family 1's words 0..8; then its draw.
- `0x415bff(s)` (after a mount attempt `0x43e2d4` and a slot change
  `0x43e242`, each followed by the panel's redraw): nothing without a
  record; the slot's item ID (0 for null); the arm list record of (ID,
  s + 2). Missing or frame -1: layer s + 2 reset (`0x40bdf4`). Else the
  slot's weapon layer (ID ≠ 0) or layer s + 2 reset (ID 0), then layer 12
  := the `<x>_out` frame of the record at its second point with pair 0's
  source 0xba, and the panel's `+0x315` (layer 12's first target) := 0x63.
  Other slots' markers are not removed: there is one marker layer.
- WEAPONS' case of the selection (`0x43dfc8`) resets layer 12 of the old
  selection's panel before `0x43fdac(0)` (with selection -1 it writes into
  `[0x48d598]`, Registration's ACCEPT: own code does nothing there).

### 36.3 The hot spots are never shown (L)

`0x43c830` builds, per bay, ESRects (`0x4086b7(0x55)`,
`0x40aca4(..., 0x22)`) in the bay's panel: WEAPONS one per slot below the
capacity with `gam\arm_hots.dat`'s rectangle s of the type and the handler
`0x43e7ef + 0x61 s` (`0x43e242(s)`); REPAIR the type's `gam\rpr_hots.dat`
rectangles with the External rows' handlers `0x434745 + 0x65 s`, then per
mounted slot the rectangle `0x414a3c` computes (the rpr list record's
point and the `rpr_wpns` frame's size) with the handler of External row
6 + s. Each gets frame colour 0xe, `0x41f3ed`, the cursor `[0x481188]` and
frame flag 0. But the window constructor sets the hidden bit
(`or [this + 0x11], 2` at `0x41f239`; the allocator's pool path zeroes the
object first, `0x4086ed`), a redraw (`0x41f3ed`) of a hidden window does
nothing, a parent's show (`0x41f93a`) shows only pending children, and no
code shows these windows before `0x43ce7a` deletes them. The hit test
`0x46a05c` skips hidden windows. So **the hot spots never draw and never
take a click**: WEAPONS selects its slots only with the two arrows, REPAIR
its rows only with the rows. The own code reads both files and keeps the
rectangles (`es2_squad_hot_spots`), but builds no window.

`0x414a3c` asserts (severity 4, `src\hercdisp.cpp` line 0xe9, text `Data
Error: Herc: %s   Socket: %d \tWeapon: %s` with ESTEXT 0x6e + type, the
slot and ESTEXT 0x7e + ID; `0x44e560` ends the process with exit code 1,
§21.2) when a mounted item has no rpr list record for its slot. On the CD
every arm (item, slot) pair with a picture has one; whether a mount
outside the arm lists is reachable was not checked.

### 36.4 The condition `0x4121f8` (L, V)

`0x4121f8(rec + 8, capacity)`: `cx` sums words 0..12, 13..21 and
23 .. 22 + capacity (16-bit adds, loops signed against the capacity word),
then `movsx`, `cdq`, `idiv` by capacity + 0x16; the caller takes `ax`. The
squad panel (`0x43e0d7`, `0x43da1a`) passes `[rec + 0x4c]`, the capacity
(SAV fixed bytes 74). **V**: `//shell:condition_real_test` compares
`es2_shell_record_condition` with the original function in the mapped
image (slot-mode op `squad-condition`) over `editors_real_test`'s corpus
and seeded edits (words over the whole signed range, records with a
wrapping sum, capacities raised up to 10): all equal.

### 36.5 Own structure and choices

- The service bay's `panel` hook is `es2_squad_pictures_panel`; the host
  passes the files' bytes, the DBA frames, each bay's record (vehicle,
  progress, capacity, slot item IDs, the 33 words), the part groups (the
  results tables' first 24 words, `0x46f9e4`) and `0x46ffc0`'s nine words.
  The geometry setters are the own file's (the toolkit has none); the
  hover update of `0x41f287`/`0x41f33a` (cursor beyond the new edge) is not
  modelled.
- Refused and counted (`refused`), where VSHELL reads outside its tables:
  an item ID outside 0..32, a type outside 0..8, a capacity outside 0..10,
  a frame the DBA lacks, a layer index outside 0..29, a part group the
  table does not terminate, a slot outside 0..9, the marker reset with
  selection -1, more WEAPONS slots than rectangles.
- The data error of `0x414a3c` goes to the host (`data_error`); es2shell
  writes it to `shell.log`; the process does not end (deviation, open).

### 36.6 Verification and capture states

`//shell:squad_pictures_test`: condition helpers with wrapping and
refusals, the readers with every truncation, the arm and rpr loads,
unloads, the diagram on both tabs, the data error, the marker and its
reset, the hot-spot rectangles. `//shell:screen_test --suite
squad_pictures`: synthetic cases (WEAPONS, another bay, BUILD with a bay
under construction, CREW; REPAIR, another bay, WEAPONS after REPAIR; the
marker on two slots; REPAIR's internal tab) and real cases (the CD's
picture files and DBAs, CD fonts, palettes, English ESTEXT, synthetic
records) with recorded hashes; `//shell:condition_real_test`; one-line
mutations, each caught. The WEAPONS frame of the tour, squad panel and
Herc picture included, equals VSHELL's (**V**, §41). States a capture
should still hold, from a new career's bay (screen `squad_panel`):
`weapons_slot` (WEAPONS, then `>` twice: the marker), `repair` (REPAIR
clicked), `repair_internal` (an Internal row: REPAIR's panel), `build`,
`crew`, and a campaign bay with a Herc under construction (BUILD, that bay
selected).

### 36.7 Open points

- Whether the data error of `0x414a3c` is reachable (a mount outside the
  arm lists) is not checked; es2shell logs it and goes on instead of
  ending with exit code 1.

## 37. Movies in the graphical shell: input during playback and placement

Read in the listing (**L**); the SDL behaviour from SDL 3.4's source and a
scratch check on a hidden window. Own code: the MCI player
(`es2_mci_player`, `es2_mci_player_place`) and its use in
`shell/src/gui/app.c`; own test `mci_player_test`.

### 37.1 Input during playback (L)

`0x41e640` sets `0x470ea0` := 0 (`0x41e774`), `SetCapture(main)`
(`0x41e781`), `0x470eb0` := 1 (`0x41e786`), sends `play`, pumps once
(`0x41e7a5`), empties the key ring (`0x40586d`: read index `0x4816a4` :=
write index `0x4816a0`, bit 1 of the device word cleared) and calls
`0x46a7e4`. The loop `0x41e7b8..0x41e7f9` pops **one** word per pass
(`0x404d0d`), compares its low byte with 1 (Esc) and 0x39 (Space) (key-up
words carry 0x80 and never match), then pumps (`0x406328`) and ends when
`0x470ea0` is set. In the window procedure only the two button-down cases
set the skip flags (`0x471120`, `0x470ea0`, `0x46c270`): `0x4050e0`
(right down) and `0x405145` (left down); the two button-up cases
`0x4051aa` (right up) and `0x4051d1` (left up) only test `0x470eb0`. All
four post nothing while `0x470eb0` is set (tests at `0x40512c`,
`0x405191`, `0x4051b8`, `0x4051e1`). So neither the skipping click nor a
key pressed during a movie reaches a screen; a button up after the movie
finds no pressed button. (§19.4.)

### 37.2 The own player

The player skips on a button down on the main window or any window inside
it and sets the capture back after each message; the host empties the
ring and clears `0x470ea0` before `PLAY`, and after it runs the shell's
pump once while the movie flag is set (buttons dropped by the input path
`0x4050bd..0x4051f5`, keys into the ring), then empties the ring.
Deviation: VSHELL can leave the words that arrive in the pump of its last
pass in the ring (one pop per pass); es2shell leaves none.

Traps this order avoids: SDL's window procedure queues SDL events for
every message the player dispatches, and converted after the movie flag
is cleared they become a down and an up on the main menu, whose buttons
fire on the up after a down (`0x409d8a`), so the skipping click would
press a button. SDL's mouse auto-capture (`SDL_HINT_MOUSE_AUTO_CAPTURE`,
on by default) captures on SDL's button down and calls `ReleaseCapture`
when the buttons go up, so without setting the capture back the player
loses it after one click (an image button fires on the down, `0x40a3a2`,
and leaves its up to the movie) and later clicks on the picture go to the
MCI child window. Scratch check (hidden SDL window, posted
`WM_LBUTTONDOWN`/`UP` and Space): the capture is lost after the up; with
the conversion after the flag 2 button records and 2 ring words remain
after the movie, with the own order none.

The player's pump (`PUMP`, `0x406328` at `0x41ee67` and in the wait for
the focus) is the shell's pump, so the focus word (`0x46c288`,
`0x405455`/`0x405443`) changes while the player waits; the wait idles up
to 10 ms per pass (own; VSHELL polls).

### 37.3 Placement (deviation)

`MoveWindow` (`0x41e5fa`) gets the movie rectangle mapped through the
presenter's layout of the client area (`es2_gui_layout_compute`: the
largest integer scale that fits, or the shrunk frame, centred):
`es2_mci_player_place` maps both edges,
`x0 = frame.x + floor(x · frame.width / 640)`. Unscaled 640 × 480 gives
VSHELL's rectangles; full screen 1920 × 1080 puts the full rectangle
`(0x20, 0x3c, 0x240, 0x168)` at `(384, 180, 1152, 720)`. The picture
follows the window's size (VSHELL's 576 × 360 window already relies on
that). `mci_player_test` checks the placement.

### 37.4 Open points

- Playback, the skip by click, Esc and Space, the restart and the
  picture's position in full screen are checked by hand; nothing here
  plays a movie.

## 38. BUILD's squad refresh, ARMORY's texts and the archive order in es2shell

Read in the listing (**L**) and checked with es2shell headless on the CD
data (`//shell:gui_bay_real_test`).

- **The Herc list after a Build.** `0x4475ce` refreshes the squad panel
  (`0x43e0d7`, `0x43e167`, `0x43da0c`) inside the handler (§31.1). The own
  panel reads the engine's memory state, while a Build replaces the
  screen's SAV and frees the old buffer; `app.c`'s `host.squad` therefore
  hands the state back to the engine before the panel is drawn (trap: done
  only after the handler, the panel draws the freed old state).
- **The cost column of ARMORY.** The constructor writes each row's fourth
  column at `0x449847..0x449863`: the unsigned word `+0x14` of the weapon
  record `0x483cc8 + 0x1d·c` (`movzx`), `idiv` 1000, `"%d"` (`0x4791d7`),
  right-aligned at x `0xc9` through `0x40a8c0`. The weapon table is filled
  at start-up: WinMain `0x401aa4` calls `0x4125e8` at `0x401d4e`, before
  the screens' construction `0x40180c` at `0x401ff8`; so VSHELL shows the
  costs with or without a loaded game. The own screens are built at
  start-up without a SAV, so the costs are read from `GAM\WEAPONS.DAT`
  alone.
- **"Erf. aterial" is VSHELL's own drawing.** The label `[0x48db24]`
  (`0x4496b3`) is ESTEXT 220 (German "Material", 45 pixels in FONT2),
  right-aligned (align 1) in the rectangle (220, 37)–(257, 49) of
  `[0x48da5c]` (`0x4496d8` back to `0x449625..0x449654`: left `0xdc`, top
  `0x25`, right = the panel's width − 9, bottom `0x31`), 37 pixels wide, so
  the string starts at x = 37 − 45 = −8. Every draw clips to the widget's
  absolute rectangle (`0x41fbd9` → `0x41fe84` copies `window+0x15`, the
  absolute rectangle, as the clip rectangle, clip mode 1): the M (9
  pixels) falls outside, in VSHELL as in the own shell. The label draw
  itself is equal in vitro (**V**: `text_real_test`'s sweep has
  right-aligned labels wider than their rectangle). English ESTEXT 220
  ("req.") fits. A capture of `armory:shown_*` (§31.5) would show it.
- **The archive order.** VSHELL registers `vol\*.vol` with mask `0x100`
  and the archives' priority ([file_io.md](file_io.md#vshell-call-sites),
  [vol.md](../formats/vol.md)): `SHELL1.VOL` (priority 10) wins over
  `SHELL0.VOL` (5). Of the members of LANG0, SHELL0, SHELL1, ZONES and
  SHLSOUND only the four fonts (`dfn\font2`, `black`, `font`, `map`) occur
  twice, with different bytes (SHELL1's FONT2 has 223 glyphs, SHELL0's
  217; the widths of the codes both have are equal). es2shell opens its
  four archives (LANG0, SHELL0, SHELL1, ZONES, the directory's name order)
  with `es2_vfs_open_dbsim_order(…, 0x100, …)` (`app.c` `open_vfs`). That
  lookup does not fold `/`, so the language files are named with
  backslashes (`GER\ESTEXT.BIN` …). The samples' archive
  `VOL\SHLSOUND.VOL` is opened separately ([audio.md](audio.md)).

Open: nothing here was compared with VSHELL's frames.

## 39. The DEBUG dialog on the mode-1 paths

Read in the listing (**L**); own tests name the real tests that compare
the own presenters with each other. Own code: the flow
(`flow.debug_cheat`, `flow.debug`, `es2_shell_flow_debug_answer`), the
host (`es2_host_debug_ask`, `es2_host_debug_answer` in `shell/src/host.c`),
the box of §22.

### 39.1 Where it waits (L)

`0x413305` tests the cheat byte `0x482368` (`0x413315..0x41331d`): set, it
only shows the dialog (`0x4133d8`: `0x44e1b5`); clear, it shows it and
posts the two clicks on Use Default (`0x413323..0x4133d0`). The posted
clicks run after the calling handler returns, so the order is the same
with and without `-@`: everything after the call runs first. New career:
Registration ACCEPT `0x43c78b` calls `0x40e8a5` (`0x43c7be`), which sets
the final word 2 (`0x40e8a8`), builds the game, calls `0x413053(mode)`
(`0x40e900`; mode 1: CAREER (1, 0) at `0x41307c..0x413085`, then
`0x413305` at `0x41308e`), the salvage (`0x40e906..0x40e924`) and the
loaded flag (`0x40e92a`); back in the handler the page word `0x481108` :=
0 (`0x43c7c6..0x43c7da`). The results path's advance 2 sets the page word
4 (`0x40f62d`) before `0x40f647`.

### 39.2 The answer in mode 1 (L)

Use Default `0x44dbea` hides the dialog (`0x44e217`) and calls `0x44db5c`:
the CAREER entry's name (NULL when the position is not below the group's
count, `0x44db5c..0x44db8e`) to `0x41cd61`; mode word 1
(`0x44db97..0x44dba1`): `0x40e75f`, `0x4135bb`, `0x430ec4(10)`,
`0x43b7f2(8)`, `0x43b758`, `0x43aee7(page word, 1)`, return. ACCEPT
`0x44dc4d` formats `msn\%s.msn`, replaces the first `^` (§15.6), calls
`0x41cd61`, and in mode 1 runs the same six calls and jumps over the
launch (`0x44dc9c..0x44dcd9` → `0x44dcef`). ACCEPT calls `0x40f68c` and
sets exit code 2 and the quit flag only in mode 0 (`0x44dcdb..0x44dce8`);
in mode 1 it ends in the bay like Use Default (§4.11).

### 39.3 Flow (own structure)

`flow.debug_cheat` is the cheat byte (the host sets it from the command
line); `flow.debug` names a waiting dialog (`ES2_FLOW_DEBUG_CAREER`,
`ES2_FLOW_DEBUG_NEXT`). With the byte, Registration ACCEPT stores the
final word, the game and page 0 and waits (the host's `new_game` creates
the game only: `eio_new_game(..., defer)`); advance 2 stores page 4 and
outcome NEXT and waits. `es2_shell_flow_debug_answer(flow, text)` calls
the host callback `debug_prepare` (NULL: Use Default; else the field's
text, which the host turns into the stem with `es2_shell_debug_mission`,
then `es2_shell_engine_prepare_mission`: the named file with the CAREER
position left as it is) and shows the bay; a failure (a refused name, a
failed preparation) leaves the main menu (outcome NEXT_FAILED on the
results path; own, VSHELL asserts on a missing file).

### 39.4 Presenters

`es2_host_debug_ask` (after every flow event; the text menu before each
screen and after the results path) gives the dialog the CAREER name of the
memory state as the default; the text menu asks (1 Use Default, 2 a name;
end of input: the dialog stays, the process ends, exit 0); es2shell shows
the box of §22 and answers through `es2_host_debug_answer` (on the results
path then the bay with the mission screen's page 4 and its statistics, as
without `-@`). The map object (`0x40e75f`) is built after the answer.

### 39.5 Verification

`flow_test` (`debug_dialog`); `gui_route_real_test` part h: new career
with `-@` (default name C1_01) answered with Use Default, ACCEPT `C1^01`
and ACCEPT `C1^02` (VSHELL's key table has no `_`; the `^` becomes `_`),
then MISSION and Rock & Roll: es2shell's DATA and SAV files equal
es2menu's; ACCEPT of the default's own name gives Use Default's launch
files, C1_02 others; the -X3 results path with `-@` (default C1_02)
answered with Use Default and ACCEPT `C1^02`: the bay with page 4, the
files equal. The Use Default preparation itself is `menu_real_test`'s
(es2chain and the original preparation).

### 39.6 Open points

- Not compared with VSHELL in vitro: the mode-1 follow-up of
  `0x44db5c`/`0x44dc4d` draws screens the slot-mode worker does not build.
- A typed mission other than the CAREER entry is checked only between the
  own presenters.

## 40. The modifier hot keys

Hot keys with Shift, Ctrl or Alt (§22.4; the input path is
[vshell_widgets.md](vshell_widgets.md) 7.2). **L** = read in the listing;
**V** = checked by `//shell:gui_tables_real_test` against the file under
`ES2_DATA_ROOT`; own unit tests on synthetic bytes are named. Nothing here
is checked in vitro or live.

### 40.1 What VSHELL does

- The key word is the key index plus Shift `0x800`, Ctrl `0x400`, Alt
  `0x200` and, for a key-up, `0x80`. Alt+Esc, Ctrl+Esc, Alt+Tab and
  Alt+Enter are system words checked before the up flag is added; Alt+F4
  is the one system word that still reaches the hook (§22.4).
- The hot-key hook `0x4094ee` takes the word (`0x4094f4`) and queues it
  with a time stamp through `0x409491` (`0x409526..0x409534`, L). Whether
  the hook takes a key is decided on the **low byte** alone
  (`and bl, 0xff`, `0x4053ab`, V: the three code bytes), so Shift, Ctrl and
  Alt never stop a hot key.
- The keyboard poll `0x409545` reads the command byte of a queued word
  from `0x46e5a9[word]` with the **whole 16-bit word** (`movzx edx, di` /
  `movsx ecx, byte [edx+0x46e5a9]` / `inc ecx` / `je` at
  `0x409587..0x409592`: `0xff` means no event; otherwise `movzx ecx, di` /
  `mov al, [ecx+0x46e5a9]` / `mov [ebx+0x24], al` at `0x409602..0x40960b`
  and the post at `0x409621`, L). The table has 256 bytes; for a word with
  modifier bits the read lies past it, in unrelated data of the executable
  (largest word `0xed3` of the set below, so `0xed4` bytes in all; the
  bytes lie in DATA among RTTI names and vtables, no absolute write to
  them was found).
- The words that can reach the read: 15 hot keys with a virtual-key code
  (Esc, Backspace, Tab, Ins, Del, PgUp, PgDn, Home, End, Enter, Up, Down,
  numpad 5, Left, Right; the minus key of the list has none), down and up,
  seven modifier sets = 210, minus the eight words of Alt+Esc, Ctrl+Esc,
  Alt+Tab and Alt+Enter = **202** (V: enumerated over the file). Of these
  10 read `0xff`, 79 read `0x00` and 113 read other values (V: the
  counts).

### 40.2 The WinConsumer by command code (L, V)

The consumer `0x409996` dispatches on the event type
(`0x4099df..0x4099f3`: mouse `0x20`, key `0x40`, command `0x100`;
anything else is a trace only). For a command it reads the byte at +0x24
**sign-extended** (`0x409a2e`), sends everything above `0x1d` (every byte
from `0x80` up is negative, so it is included) to the trace-only default
`0x409c7a`, and otherwise goes through the byte table `0x409a5e` and the
jump table `0x409a7c` (V: both tables, entry by entry). Every case ends by
posting the event on (`0x409c9e`, `0x468780`).

| Code | Handler | Effect |
| --- | --- | --- |
| 4, 5, 6, 7 | `0x409bae`, `0x409c0a`, `0x409bdc`, `0x409c35` | pointer left, up, right, down by 8 (`0x46863c` read, `0x468564` set) |
| 0x0a, 0x1d | `0x409aea`, `0x409b4c` | left down, left up (mouse codes 2 and 1) at the hover window |
| 0x11 | `0x409c60` | releases the capture and calls `0x40b7e9` on `[0x48d1dc]` (the Quit box, §22.4) |
| 0, 1, 0x13 and every other value | `0x409aa8`, `0x409ad4`, `0x409abe`, `0x409c7a` | trace only (V: each starts with the trace call) |

### 40.3 What the 202 words do

- Only **three** words give a code the consumer acts on: Shift+Esc down
  (`0x801`) and Shift+Ctrl+PgUp up (`0xcc9`) give 5 (pointer up by 8),
  Shift+Ctrl+Alt+Home up (`0xec7`) gives 4 (pointer left by 8). No word
  gives 1, 6, 7, `0x0a`, `0x11` or `0x1d` (V: the enumeration over the
  file finds exactly these three words among the codes 1, 4..7, `0x0a`,
  `0x11`, `0x1d`). The pointer moves on every screen, since the consumer
  is global.
- **Edit fields.** The ESDialog handler `0x40c45f` reads the code as a
  byte (`0x40c567`): code 1 (`0x40c5c0`) and code 4 (`0x40c5a6`) delete
  the last character through `0x40c406` while the field is capturing, code
  10 ends the edit (`0x40c578`); every command is then passed on
  (`0x40c5d8`, `0x41fc28`). So `0xec7` (code 4) deletes a character in an
  active field; no word gives 1 or 10.
- Because every byte other than `0xff` is a command event, the 192 words
  with such a byte also run the hover window's handler, as a plain hot key
  does: the Saved games rows select their row (`0x438873 + 0x61 k`), the
  Registration name handler `0x43c47e` runs. The Saved games row is the
  only visible one besides the three pointer moves and the one deletion.
- No effect: the 10 words that read `0xff`; the eight system words (never
  read); the minus key (no virtual key); every other non-`0xff` byte at
  the consumer (trace only).

### 40.4 Own implementation

- `es2_gui_input_set_hot_bytes` and `es2_gui_input_hot_command`
  (`shell/src/gui/events.c`, `events.h`): the input keeps a copy of the
  executable's `0xed4` bytes at `0x46e5a9` and `es2_gui_poll` reads the
  command byte of a queued word from it. Words below `0x100` keep the
  built-in table. The consumer in `widgets.c`, the edit fields, the Saved
  games rows and Registration act on the command code, and the modifier
  words arrive as the same events.
- **The bytes are not in the source** (they are bytes of the executable).
  `es2_app_build` reads them from the hash-checked `VER95\VSHELL.EXE` the
  host already holds (`es2_shell_exe_span`, the way the DEBUG dialog's
  literals are read) and hands them to the input. Without the executable
  (synthetic tests, a host without it) a word with modifier bits gives no
  event (a data availability limit).

### 40.5 Verification

- `//shell:gui_test`: a synthetic block: refusal of a short or null block,
  no event without the block, the three observable words, byte 0 and a
  byte above `0x7f` posted as they are, the queue order, words below
  `0x100` unaffected, the system words never reaching the hook, Alt+A and
  Ctrl+Q on their old paths, `-k`.
- `//shell:widgets_test`: pointer moves through the consumer (up, up, left
  by 8), byte 0 and `0xff` without effect, an active edit field's last
  character deleted by code 4 and code 1, the handler fired by every byte.
- `//shell:saved_games_test`: a row under the pointer selected by
  Shift+Tab (byte 0) only with the block, Shift+Esc (`0xff`) ignored, the
  naming row's character deleted by `0xec7`.
- V `//shell:gui_tables_real_test`: the enumeration of §40.1 with the
  counts 202 / 10 / 79 / 113, the three observable words, the case-index
  and jump tables of §40.2 entry by entry, the trace start of the four
  log-only cases, the low-byte compare of the hook.
- Mutations caught: the poll reading the table one word early
  (`gui_test`, `widgets_test`, `gui_tables_real_test`, `saved_games_test`),
  the poll using the built-in table only (`gui_test`, `widgets_test`), a
  one-byte-short block accepted (`gui_test`).

### 40.6 Open points

- Not run in vitro or live. A journal case in a capture would settle it:
  Shift+Esc (key down with Shift held) on any screen moves VSHELL's
  pointer up by 8 pixels if this section is right.
- Which byte lies at +0x25 of such an event is the event constructor's;
  the readers of command events read the single byte at +0x24 (L:
  `0x409a2e`, `0x40c567`, `0x40c5f3`), so it is not observable.

## 41. The tour's screen differences closed

The real cases of `shell/tests/screen_known.c` compared with VSHELL's tour
frames (the tour
in [vshell_presentation.md](vshell_presentation.md), "the tours"). **V** =
equal index and colour-table hashes against VSHELL's recorded frames;
**R** = read in the recorded capture and journal. None of
the differences was the shell's drawing; the table of known differences is
empty.

### 41.1 Causes and the cases' setup

| Case | Cause | Setup |
| --- | --- | --- |
| `service_bay.real` bay | frame history: VSHELL does not clear the frame when the main menu hands over; the bay background's 371 index-0 pixels (bounding box (13,46)..(84,215)) keep the menu's pixels | the case draws the main menu (30 passes) and clicks CONTINUE GAME before the bay, `es2_screen_main_menu_history` |
| `preferences.real_shown` | the same history; then the clone's `DATA\PREFS.CFG`: byte 1 (Sound Effects) 1, byte 0x2c (repair) 0 | history (PREFERENCES), the two bytes set in the case |
| `saved_games.real_shown` | the same history; then the clone's `SAV\GAMEFILE.STR`: " 1. S1" and " 2. Z1" used, " 3. LEER" .. "10. LEER" empty (a German installation's slot names) | history (SAVE/RESTORE), the ten rows set in the case |
| `registration.real` start | the same history; then the caret: ESDialog's 500/500 ms blink timer (`0x40c45f`) runs in real time; VSHELL's first present after START NEW GAME has the caret on (R) and the snap after the journal's 3000 ms wait has it off | history (START NEW GAME), a 100 ms clock per main-loop pass (the timer hook), snaps `open` after one pass (= VSHELL's first present, V) and `start` after 29 (V) |
| `weapons.real_shown` | the case draws the WEAPONS panel alone over a cleared frame (its area was already equal); VSHELL's frame also holds the bay's tab bar (WEAPONS pressed), the Herc picture and the squad panel of the clone's career | reference in `//shell:gui_tour_real_test` (V); the case keeps its own hash |
| `mission.real_page0` | the case draws the page alone (and Delta sector); VSHELL's frame is page 0 of Alpha sector over the bay with the tab bar (MISSION pressed) | reference in `//shell:gui_tour_real_test` (V); the case keeps its own hash |

### 41.2 The tour test

`//shell:gui_tour_real_test` runs es2shell headless twice in a temporary
game directory (the CD's `PREFS.CFG`, `GAMEFILE.STR`, `VOL\LANG0.VOL`,
English, arguments `dummy -eggplant -X1 -a` as the tour's `game_r` run):

- First run: a new career "Z1" with the menu.ini seed 12, under which the
  three roster groups' selected pilots are DUGGAN, OSAKI and BROWN as in
  the clone's GAME_R (12 is the first seed of 1..1199 whose groups 0 and 1
  start with DUGGAN and OSAKI, and its group 2 has BROWN); CREW order 1 =
  group 0 in bay 1 and order 2 = group 1 in bay 2; BUILD a Tomahawk in the
  sixth bay; the shell saves slot 10 at the end.
- Second run, the tour's path: main menu, CONTINUE GAME, the bay, WEAPONS,
  MISSION, with a snap of each; all four equal VSHELL's `game_r` frames
  (index and table, V).

So the clone's state that the frames show is: the player Z1 in bay 0,
DUGGAN and OSAKI in bays 1 and 2, the Razor of a new career and a Tomahawk
at 0 % in bays 4 and 5, campaign position 0 of group 1 (Alpha), the group
movies not yet seen. The same path on the clone's own SAV and DATA gives
the same four hashes; with `-G` the frames are German (the clone's
`LANGUAGE.CFG` is 'G', yet VSHELL drew English without `-G`, as es2shell
does; which input VSHELL takes the language from is not checked).

Mutations, each caught: the bay button's pressed state not set
(`gui_tour_real_test`: WEAPONS and MISSION); the caret one pixel narrower
(`screen_test`: `registration.real` open and five other cases); the
history copy removed (`screen_test`: the four real cases above, 371 pixels
each).

### 41.3 Mission page 1 in the tour

The tour's clicks `mission.23`, `mission.25`, `mission.21` (Objectives,
Intelligence, Briefing; centres (232,458), (376,458), (89,458) in
`shell/data/vshell_layout.json`: ESButtonFont children of the row
`mission.20` (7,433)-(632,473), handlers `0x445ac7`, `0x445b30`,
`0x445a51`) are the right controls, but the row is hidden on page 0
(§33.2), and the first MISSION after CONTINUE opens page 0 (§4.8: MISSION
`0x43ad5a` → page 0 while `0x477946` = 0 and the position word is 0; the
page-0 open sets `0x477946` := 1). The tour therefore clicks WEAPONS and
MISSION again before the three buttons.

**Result (V).** The second
MISSION opens page 1 with `-a`: the page-0 open sets the flag, no movie is
needed (es2shell does the same). Page 1's first entry runs a map
animation (355 presents) before it takes input; its last frame
is the `page1` snap (8000 ms wait) and also the `briefing`
snap; `objectives` and `intelligence` each show
their text. es2shell differs on all three: no animation, and English
summary text where VSHELL draws German (the clone's `LANGUAGE.CFG` 'G',
English buttons); §34 and §42 handle the map and the text.

### 41.4 Open points

- With movies on (no `-a`), es2shell's frame after page 0's `c1.avi` and
  `alph_th.avi` (headless, `dummy`) shows the bay with no tab pressed
  instead of the mission page; not compared with VSHELL (the tour skips
  movies). A capture with movies would settle it.
- The other `main_menu` snaps of the tour (after Preferences, after Saved
  games, from the bay: the menu over the bay, whose index-0
  pixels keep the bay's, the same frame history) are not compared by a
  case; `gui_tour_real_test`'s first run returns to the menu from the bay
  and could snap it.

## 42. Mission page 1 in the tour: text language and fly-over

The tour's mission page 1 (§41.3) against VSHELL's recorded `game_r` run.
**V** = equal index and colour-table hashes against its recorded frames, or
equal present records against its recorded capture; **L** = read in the
listing of `VSHELL.EXE`. The headless script has two verbs for this
(`shell/src/gui/main.c`): `clock MS` (the virtual clock's step per pump
pass, 0 = the default 20 ms) and `trace 1|0` (a `present N kind= rect=
tick= frame=` report line per present).

### 42.1 Where the German mission text comes from

The tour's page-1 frames draw English buttons and German mission texts.
They do not come from two language inputs at run time; the text is German
in the slot's files (L unless marked):

- The language word `0x48235e` is PREFS.CFG byte `+0x2b` (read at
  `0x40796c`, stored at `0x4079b5..0x4079bd`), replaced by `-f`, `-g`,
  `-e` and the like on the command line. The CD's and the clone's
  PREFS.CFG hold 0 there, so the run `dummy -eggplant -X1 -a` draws English
  buttons (es2shell: `loop.c` default; V on all eight snaps).
- `data\language.cfg` (`0x46e489`, `0x46e4ab`) is referenced only at
  `0x408d7f` and `0x408d9d`, in the help routine `0x408d69`; it does not
  pick the mission text.
- Page 1's texts are composed by `0x4135bb` from `data\mission.str` (the
  push of `0x46fe1d` at `0x4135c2`).
- CONTINUE GAME's load copies `sav\missn%d.str` to `data\mission.str`
  (`0x4132bc..0x4132c5`, through the stream copy `0x40da8d(src, dst)`,
  which does not look at the language; the save copies the other way at
  `0x4131aa..0x4131af`).
- The file is produced by the mission preparation `0x417cb0`
  (msn_gen.cpp, called at `0x418786`): it copies the mission name to
  `0x4855cc`, finds the '.' with strrchr and appends `.eng`, `.fre` or
  `.ger` by `[0x48235e]` (`0x417cf2..0x417d2f`, strings `0x4708f4`,
  `0x4708f9`, `0x4708fe`), opens that member through the VOL-aware stream
  (`0x4032b1` at `0x417d3b`) and writes `data\mission.str` (`0x41803e`,
  string `0x470933`).
- The squad panel's second entry comes from `data\player.mec`, written by
  `0x40f68c` on Rock & Roll, launch, Use Default and Instant Action
  (strings `0x46f649`, `0x46f659`, used at `0x40f6a4`, `0x40f6bc`); the
  load copies `sav\player%d.mec`. The clone's file has 2 entries (215
  bytes); a career never launched has 1 (127 bytes), visible as a
  68-pixel squad icon difference at (384..393,195..204).

So the clone's career was prepared under `-G` (or a PREFS byte 1) and
launched once before the capture. `gui_tour_real_test`'s career run does
the same, with `-G`: MISSION, WEAPONS, MISSION, Rock & Roll (`mission.27`,
exit code 2). Its `SAV\missn10.str`, `player10.mec`, `script10.dat` and
`DATA\MISSION.STR`, `player.mec`, `script.dat` are byte-equal to the
clone's copies; `GAME_R.SAV` (4753 against 4763 bytes) and `mission.var`
differ, and no frame of the tour shows them. The tour run itself has no
`-G`.

### 42.2 The fly-over's contract

- Ticks: the step `0x4262cf` sets `[0x471dc4]` from `0x465d5c` =
  (timeGetTime through `0x46baf0` − the base `[0x5ddcac]`) >> 4, so one
  tick is 16 ms of wall clock ([vshell_mission_map.md](vshell_mission_map.md)
  §9; L).
- The loop: main-loop step 6 (`0x4019c6`) runs one map step, one present
  and one message pump per pass. A click, Esc or Space skips to state 0x11
  through `0x425a43`; the 60-tick zoom-out still plays (L).
- The first paint (`+0x182` = 1) busy-waits until the clock reaches
  `[0x471dc4]` + n (L; §42.6). es2shell counts the waits in `busy_wait()`
  (`mission_map.c`), which adds to `g->tick` and `m->wait_ticks`.
- The capture (`every=0`) writes a present record only when the frame or
  colour-table hash changed; each record's `presents` counts the presents
  since the previous one. The click on MISSION from WEAPONS and its wait
  give 355 records: 3 for the screen change (the tab bar
  (557,4)-(632,28), the map area (279,43)-(633,300), the full frame),
  then 352 full-frame (0,0)-(640,400) records, the first after 48
  presents, then mostly 3 to 6 presents per record, the last the `page1`
  snap. VSHELL presents several times per tick and shows every tick's
  frame, so the intermediate frames are checkable in order, one per
  distinct record.
- es2shell with `clock 16` (each tick visited) and `trace 1` around the
  second MISSION gives 507 presents; with consecutive repeats dropped they
  are exactly the capture's 355 hashes, in order (V). With the default
  20 ms per pass ticks are skipped and frames are missing, so the test
  sets the clock.

### 42.3 Test and mutations

`//shell:gui_tour_real_test`:

- The career is extended as in §42.1.
- The tour path: after `mission:opened` it clicks WEAPONS and MISSION
  again and snaps `mission:page1`, then OBJECTIVES (`mission.23`),
  INTELLIGENCE (`mission.25`) and BRIEFING (`mission.21`). All eight snaps
  equal `game_r`'s index and colour-table hashes (V).
- The second MISSION's traced sequence equals the capture's 355 records
  (V); skipped when the session has no `capture.jsonl`.
- The present record checks of §42.6.

Mutations, each caught: one PAINT left out in the zoom to the squad view
(`mission_map.c` case 2, tick timer−30: 354 frames against 355, only the
sequence check fails); the career run without `-G` (four text snaps and
the sequence); the tour run with `-G` (all eight snaps, German buttons,
and the sequence).

### 42.4 Open points

- A click during the fly-over: VSHELL skips to the zoom-out (about
  63 presents after the click), and the clicked button acts after the
  animation ends. es2shell sets `map_skip` on a mouse-down (`events.c`),
  but no test compares the rest of that path with the capture.
- `GAME_R.SAV` (10 bytes) and `mission.var` still differ from the clone's;
  no frame of the tour shows them.
- Present counts per rectangle (§42.6) are compared only for the second
  MISSION's tab bar, map area and full frame (9, 10, 4). On the first
  MISSION (journal step 27) and the next tab change (step 31) the capture
  has 1 where es2shell's record has 9 and 3: not followed.
- The fly-over's counts per frame (VSHELL 5 or 6 presents per 16 ms tick,
  51 before the first flight frame; es2shell headless 1 per tick, 61) are
  the loop's speed per tick, not a contract: step `0x4262cf`, present
  and pump repeat as fast as the host runs, and the frame changes only on a
  new tick. They cannot be matched headless (the clock advances only in
  the pump) and are not compared; the frames and their order are (355).
- The windowed hold of es2shell is not measured: run `tour_windowed.py run`
  three times; expected map area held about 816–840 ms (VSHELL 831–848),
  map area to the first flight frame about 960–990 ms (VSHELL 973–989).

### 42.5 Time per present (record version 2, qpc)

The capture's record file has two versions, named by the `schema` field
of the start record.

- Version 1 (default, `qpc` absent or 0): no times. The instrument is
  byte-identical to a proxy without the option (V: `vshell_capture_off_test` and
  the exact schema-1 start prefix in `vshell_capture_capture_test`).
- Version 2 (`qpc=1` in the `[vshell_capture]` section of the capture INI;
  `capture-vshell-screens.ps1 -Qpc`): the start record has `"schema":2`
  and `"qpc_frequency"` (counts per second, QueryPerformanceFrequency);
  every record has `"qpc"` right after `state`, the QueryPerformanceCounter
  value. For a present or clear record it is read on entry to the hooked
  BitBlt/StretchBlt, before the frame is hashed and the call forwarded, so
  hashing and the file write do not move it; other records (step, snap,
  pointer, …) are stamped when written. The reading adds one
  QueryPerformanceCounter call per record and nothing else (V: stamps of
  the fixture's fake counter, `vshell_capture_qpc_test`).

Readers:

- `scripts/vshell_present_times.py` takes `capture.jsonl` or one run of a
  `frames.json` and reads both versions (`--steps`, `--hold-hash`,
  `--model-ticks`, `--json`). Version 1 gives the record table without
  times and the verdict `no-times`. An unknown `schema`, a version-2 file
  with a missing `qpc` or `qpc_frequency`, or stamps that go back are
  refused; a line the proxy did not finish is skipped. Unit test
  `//tools/reference-game/scripts:vshell_present_times_unit_test`.
- `capture-vshell-screens.ps1` copies `qpc` into each frame entry of
  `frames.json` when the record has it; `frames.json` stays `schema` 1,
  kind `es2-vshell-screens`, and the run's `capture_start` carries the
  record version. `shell/tests/screen_harness.c` and
  `test_gui_tour_real.py` read by key and ignore the new fields (V:
  `//shell:screen_test` has a frames.json of a version-2 log;
  `lane_vshell_screens` of `suite_scripts_test` summarises a schema-1 and a
  schema-2 log).
- `frame_dumps.py` reads ES2F dumps (header version 1, no time). DBSIM's
  presentation capture (`proxy/presenter.h`, `capture.ndjson`, start
  `schema` 1, read by `dbsim_frames.py`) is a separate record and has no
  times.

**Result (V; three `-Qpc` runs on a clone, VSHELL exit 0, record version 2;
MISSION from WEAPONS):**

| Record | Presents before | Gap before (ms) | Hold (ms) |
|---|---:|---:|---:|
| map area | 10 | 25 | 831 / 848 / 847 |
| full frame | 4 | 831 / 848 / 847 | 142 / 141 / 142 |
| first flight frame | 51 / 50 / 51 | 142 / 141 / 142 | 16–17 |

- The median time per present elsewhere is 2.76–2.77 ms; the flight
  records come every 13–18 ms with 4–6 presents, the 16 ms tick, so the
  runs were not loaded.
- The wait sits **before** the full frame: between the map-area record and
  the full frame 831–848 ms pass with only 4 presents; map area to the first
  flight frame is 973–989 ms = 61 ticks (976 ms). After the wait the
  48–51 presents of the full frame come fast. (A rule on the hold of
  the full frame alone, 142 ms, would wrongly read "no wait".)
- So VSHELL spends about 0.85 s of wall time in the first paint's
  busy-wait, with the screen showing the map-area state.

### 42.6 The first paint's wait in wall time

**What VSHELL does (L).** The first paint of the fly-over (step 0 of
`0x4262cf`) draws in three parts, each ending with a busy-wait
`while target > [0x471dc4]: [0x471dc4] := 0x465d5c()` (`0x465d5c` =
(timeGetTime − base) >> 4, the 16 ms tick; `[0x471dc4]` the last tick
read; the compare is unsigned, `ja` / `jb`).

| Part | Function | Wait | Ticks per wait |
|---|---|---|---|
| background | `0x425b73` | `0x425be3..0x425c04` | `[0x471d44]` = 10 |
| terrain | `0x425c0b` | `0x425f20..0x425f41` | `[0x471d50]` = 10 |
| grid lines | `0x425f4a` | four loops, one wait per line (e.g. `0x426055..0x426082`, `0x4260d6..0x426103`; the target goes through `0x471f40`) | `[0x471d58]` = 1 |

- The wait loops call only `0x465d5c`: no present (`0x406264`), no pump
  (`0x406328`), no key reader. Nothing is drawn to the screen and no
  Windows message is handled meanwhile; the screen keeps what was
  presented before the step, the map-area state.
- The constants are es2shell's `busy_wait` counts (10, 10, one per grid
  line). For the tour's mission es2shell counts W = 52 ticks in all
  (`map->wait_ticks`).
- Where the wait sits: main-loop step 6 (`0x4019c6`, called at `0x4020d0`
  after the UI step `0x41fe49`, while `[0x46c269]` is set by the mission
  screen at `0x444ccf`). Step 6 clears `[0x46c269]`, applies the palette
  request `[0x46c26a]` (`0x407b62`, `0x407c1b`), flushes the key ring
  (`0x40586d`), sets `[0x46c26c]` := 1 and `[0x46c270]` := 0, then loops:
  step `0x4262cf` (reads the clock into `[0x471dc4]` at `0x426315`),
  present `0x406264`, key `0x404d0d` (Esc and Space skip via `0x425a43`),
  click skip, pump `0x406328`, until the step returns 0. The first step
  does the first paint with its waits; the present after it shows the
  full frame.
- Step 0 sets the step timer to t0 + 60 before the waits, from the tick t0
  it read, so the first flight frame comes 61 ticks after the map area
  whether the waits took W ticks or none.

**The capture agrees (§42.5's three runs; ms after the click):**

| Interval | Run 1 | Run 2 | Run 3 |
|---|---:|---:|---:|
| tab bar presented | 2.2 | 2.4 | 2.4 |
| palette upload | 26.6 | 26.5 | 26.6 |
| map area presented | 27.9 | 27.7 | 27.9 |
| second upload (same table) after the map area | 9.9 | 10.6 | 10.0 |
| second upload to the full frame | 821.3 | 837.2 | 837.3 |
| map area held | 831.2 | 847.8 | 847.3 |
| map area to the first flight frame | 973.2 | 989.0 | 989.5 |

- The second upload is most likely step 6's palette request (**H**). The
  wait starts inside the tick current then, so it lasts between W − 1 and
  W ticks plus a few ms of drawing: W = 52 gives 816–837 ms, matching
  821–837 ms; W = 51 or 53 does not fit both values.
- Between the map-area present and the full-frame present VSHELL presents the unchanged frame 3
  times (all from site `0x406e57`). They are the tail of the UI flush
  `0x41fe49` that presented the map area: the flush calls `0x41fe2c` once
  per dirty rectangle (list at `+0x19`, count `+0x659`), each a BitBlt
  through `0x406bfb`; the map area is the change at its first rectangle,
  the three others show nothing new. The flush runs before step 6 in the
  same pump pass (17254370 for all three records); the waits and step 6's
  palette request (`0x407b62`, `0x407c1b` → `0x45414c` → `0x4071fe`:
  SetDIBColorTable, no BitBlt) present nothing, so the fourth present is
  step 6's `0x406264` with the full frame (L, and V by the counts below).
- The list holds four rectangles (V by the count; the map frame first,
  as the record's `dst` shows; the rest in es2shell's order): the map
  frame (279, 43)–(632, 299), the squad frame (7, 43)–(275, 299), the
  text frame (7, 307)–(632, 423) and the button bar (7, 433)–(632, 473).
  The page's text lines are labels (ESREPORT `0x40d37d`, vshell_text.md),
  and a label's draw `0x40b9e9` queues only its row (`0x41fbd9` → display
  slot 0 `0x41fe84`); the shown rows lie inside the text frame, so the add
  (`0x41fef9..0x420042`) drops them. A panel drawn as one window queuing
  its whole rectangle would add a fifth, (17, 328)–(620, 475) for the
  campaign panel, which no entry contains (four unchanged presents); so
  es2shell's panel window queues the rows of the page's lines only
  (`draw_report` in `shell/src/gui/widgets.c`).

**Own implementation (`shell/src/gui/app.c`, `mission_map.c`).**

- `map_animation` calls `map_wait(a, now)` after the step and before its
  present when the step counted waits (`map.wait_ticks` > 0, the first
  paint only). `map_wait` spends the ticks in wall time through
  `es2_mission_map_wait(now + wait_ticks, clock, context)`, which reads
  the host clock (ms) until ms / 16 is no longer below the target. As in
  VSHELL nothing is painted, presented or pumped meanwhile, so the screen
  keeps the map area for about W ticks, then shows the full frame; the
  first flight frame follows 61 ticks after the map area.
- This applies only with a clock that runs by itself (windowed:
  `SDL_GetTicks`). The headless clock (`clock 16` and the default)
  advances only in the pump, so `map_wait` does not wait on it; the traced
  headless sequence is VSHELL's 355 distinct frames with the full frame held
  for 61 presents.
- During the wait Windows gets no messages from es2shell, as from VSHELL
  (about 0.85 s, well under the "not responding" timeout).
- `map_present` presents (0, 0)–(639, 399), the rectangle `[0x481920]`
  that the map build binds for `0x406264` (vshell_presentation.md; the
  capture's `dst` (0, 0, 640, 400) on every fly-over present).

**Present record (off by default).** `ES2_SHELL_PRESENT_LOG=<file>` makes
`es2_app_create` wrap the shell's platform with a recorder (removed by
`es2_app_destroy`). The file has the capture's version-2 shape:

- A start record: `"kind":"start"`, `"schema":2`, `"source":"es2shell"`,
  `"every":0`, `"headless"`, `"qpc_frequency"`.
- A `present` or `clear` record when the presented frame's index or colour
  table changed: `qpc` (QueryPerformanceCounter on entry, before
  forwarding), `presents` (platform presents since the previous record),
  `blits` (rectangles since the previous record, the change counted at the
  first rectangle of its present: VSHELL's `presents`, one per BitBlt),
  `dst`, `rects`, `index_sha256`, `palette_sha256` (the hashes as in the
  headless report).
- One `wait` record per first paint: `qpc`, `qpc_end`, `tick`, `ticks`
  (W), `wall` (1 when the wait was spent) and `reads`.
- Each line is flushed. Without the variable nothing is written and the
  platform is not wrapped.

Readers:

- `python -B shell/tests/tour_windowed.py summary <file>...` prints, for
  es2shell's record or VSHELL's `capture.jsonl`, the map area's hold, the
  full frame's hold, the interval from the map area to the first flight
  frame and the host's wait (the summary code is in
  `test_gui_tour_real.py`). On a headless es2shell tour: 7 ms, 421 ms (61
  presents), "host wait 52 ticks, wall=0".
- `scripts/vshell_present_times.py` reads the same records.
- `tour_windowed.py run <es2shell.exe> <out> [--runs 3]` builds
  `gui_tour_real_test`'s career headless in a directory under `<out>`,
  then starts es2shell windowed N times with the record on
  (`present-<n>.jsonl`), restoring the career's state before each run.
  The person clicks the tour path; scripts need `--headless`, so the
  windowed run cannot be scripted.

**Tests and mutations.**

- `//shell:mission_map_test`: the first step counts the waits
  (`wait_ticks` > 20, `g.tick` = t + `wait_ticks`), the second counts
  none; `es2_mission_map_wait` stops on the first read whose tick reaches
  the target (276 reads from 1607 ms in 3 ms steps to tick 152), reads
  once when the target is already reached or passed, and returns 0
  without a clock.
- `//shell:gui_tour_real_test` (against VSHELL's recorded capture): the tour
  runs with the record on;
  the record has a version-2 es2shell start, headless; the 355 traced
  frames are a run of its frame records; the map area, one wait record (52
  ticks, `wall` 0), the full frame and the first flight frame follow in this order;
  with a version-2 capture VSHELL's 837.3 ms before its full frame lie
  within W − 1..W + 1 ticks; the tab bar, map area and full frame
  records' `blits` equal the capture's `presents` of the step (9, 10, 4);
  the animation check (355 = 355).
- Mutations, each caught: `>` changed to `>=` in `es2_mission_map_wait`
  (one tick too long; `mission_map_test`); the `map_wait` call disabled
  (`gui_tour_real_test`'s record check; the animation check still passes,
  which is why the record check exists); the whole panel rectangle
  queued again in `draw_report` (full frame 5 blits against 4,
  `gui_tour_real_test`).

## Evidence

The tests that keep each section's contract (the analysis scripts and their
mutation logs are not part of the repository):

| Sections | Regression form |
| --- | --- |
| 9 | `//shell:slots_real_test`, `//shell:menu_real_test` |
| 10 | `//shell:bay_real_test`, `//shell:menu_real_test`, `//shell:campaign_test` |
| 11 | `//shell:launch_real_test`, `//shell:menu_real_test`, `//shell:campaign_test` |
| 12 | `//shell:editors_real_test`, `//shell:editors_test` |
| 15 | `//shell:practice_real_test`, `//shell:practice_test`, `//launcher:launch_chain_test` |
| 16 | `//shell:texts_real_test`, `//shell:texts_test`, the mission-texts check of `//shell:menu_real_test` |
| 17 | `//shell:editors2_real_test`, `//shell:editors_test` |
| 18 | `//shell:flow_test`, `//shell:menu_real_test` |
| 19 | `//shell:movies_real_test`, `//shell:movies_test`, `//shell:mci_player_test`, the movie checks of `//shell:menu_real_test` |
| 21 | `//shell:help_test`, `//shell:help_real_test`, `//shell:flow_test`, the ONLINE MANUAL block of `//shell:menu_real_test` |
| 23 | `//shell:main_menu_test`, `//shell:screen_test` (suite `main_menu`) |
| 27 | `//shell:preferences_test`, `//shell:flow_test`, `//shell:screen_test` (suite `preferences`) |
| 33 | `//shell:mission_screen_test`, `//shell:flow_test`, `//shell:screen_test` (suite `mission`) |
| 36 | `//shell:condition_real_test`, `//shell:squad_pictures_test`, `//shell:screen_test` (suite `squad_pictures`) |
| 40 | `//shell:gui_tables_real_test`, `//shell:gui_test`, `//shell:widgets_test`, `//shell:saved_games_test` |
| 41, 42.1–42.3 | `//shell:gui_tour_real_test`, `//shell:screen_test` |
| 42.5 | `//tools/reference-game/scripts:vshell_present_times_unit_test` |
| 42.6 | `//shell:mission_map_test`, `//shell:gui_tour_real_test` |

Sections without a row cite their evidence in place (in-vitro tests,
captures, the listing addresses).
