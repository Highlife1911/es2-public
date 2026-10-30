# shell-invitro: VSHELL in vitro

The 32-bit Windows worker maps the hash-checked CD executable privately and
calls its loaders and preparation functions. It does not start the game or
load original DLLs. CD resources are read through `formats/vfs`; all original
writes terminate in four memory buffers. The Python frontend seals those
buffers into a schema-1 launch set, refusing repository and installation paths.

See [the boundary and measured results](../../../docs/reference-game/shell_invitro.md)
for the input contract, commands, evidence and remaining uncertainties.

- `main.c`: private mapping configuration, original calls, IAT adapters, CLI.
- `memory.h`: bounded memory streams; no host file writing.
- `state.c`: independent SAV preflight; original readers perform restoration.
- `trace.h`: optional instruction-address/call-edge tracing, enabled with
  `ES2_SHELL_TRACE=1`; emits addresses to stderr, never original bytes.
- `es2shellprep.py`: worker protocol, actual process identity, immutable publisher.
- `audit.py`: opt-in two-fixture byte equality and the CAREER-order sweep.
- `boundary.py`: hash-gated static call/global-reference inventory, with optional
  dynamic resolution from trace logs. It reports unresolved transfers explicitly.

`--seed` is the advance count accepted by original RNG initializer `0x465cbc`,
0..65535 as a raw word: through 32768 it advances that many draws; larger
values advance none because the loop compares signed words. Language defaults
to German. Without a SAV, original CD loaders create
the inventory, roster, player and hangar; roster initialization uses seed zero,
then the requested preparation seed resets the RNG immediately before preparing.
`--preset-index` and `--preset-flag` expose the signed-word index and zero/nonzero
flag used by the alternative training loadout branch; c4 selects one of nine
INI paths. The default controls still use mission-authored equipment.
`--variables` must have exactly 2,000 bytes. The original preparation overwrites
its own reserved variables and, in mode zero, clears the bank.

The worker prints only state metadata and output hashes unless `--pipe` is used.
`--pipe` emits a private `ES2P` framing on stdout: a length-prefixed JSON block,
then four length-prefixed buffers in script/STR/var/MEC order. Lengths are LE u32.
Do not redirect that binary stream into the source repository. The frontend
consumes it in memory and is the supported publication interface.

No original bytes or assets are fixtures. `state_test`, `memory_test` and
`cli_test` use synthetic data; `real_test` prints SKIP without `ES2_DATA_ROOT`.
The real test needs the SAV companions beside `extracted/cd` (any measurement
clone under `installs/`); the reference files are pinned in `reference_pins.py`
(see shell/README.md). A CD without those fixtures cannot perform acceptance.

`sweep_compare.py` drives this worker and the portable preparer over baseline,
coverage, retained-state or configurable Cartesian matrices. The separate
`seed-initializer` profile invokes original seed initialization directly rather
than injecting reconstructed RNG bytes. It compares the
four files plus returned RNG and reset selectors, keeping buffers in memory;
JSON/Markdown reports contain hashes and mismatch locations only. `test_sweep.py`
checks the transport and an opt-in tiny original/portable matrix. See
[sweep commands and limits](../../../docs/reference-game/shell_invitro.md#differential-sweep).

`--emit-save` additionally runs the original SAV writers on the state handed
to preparation (restored SAV or CD defaults, with the explicit position and
bank) and returns that SAV as a sixth packet block; `--player-name` and
`--player-rank` feed the original new-career player constructor. The
`loadout` sweep profile and `shell/tools/es2loadout.py` use it to prove that
the original readers and writers reproduce each generated SAV
([loadout sweep](../../../docs/reference-game/shell_invitro.md#sav-emission-and-loadout-sweep)).

`--batch` is a private test transport shared with `shell/tools/es2prep.py`:
one 17-field control line, followed by state, SAV and runtime-table hex lines.
It returns consecutive ES2P packets. Independent requests restore CD/SAV state;
retained requests keep roster/hangar state and accept returned bank/RNG/selectors.
Each request uses fresh output sinks. Batches are bounded to contain original
loader allocations. A fatal assertion terminates the worker; the sweep records
the concrete assertion site and restarts remaining independent cases.

`--results FILE --mission-var FILE --save SAV` runs the original results path
`0x40f09f` instead of a preparation: start-up loaders (catalogue, HERC_INF,
DAMAGE, WEAPONS, CAREER), the SAV restore, mode/option bytes and RNG state,
then `0x40f09f` with its eleven UI/disk entries replaced in private memory by
breakpoints that record the call and return 0. The SAV emitted by the original
writers, the `MISSION.STR` written by the debrief reader and the composed
debrief text return in an `ES2R` packet (metadata with position, final word,
debrief record and UI calls). `--results-batch` repeats this per request from a
snapshot of the image's DATA section taken after start-up (frees are deferred
for the batch). `ES2_SHELL_TRACE=exec` prints the executed-address set without
call edges. `results_compare.py` drives it against `es2prep_core --results`;
see [shell_invitro.md](../../../docs/reference-game/shell_invitro.md#results-path).

`--op OP` (repeatable, up to 512) selects the slot mode: VSHELL's own save
`0x40e933`, load `0x40eaaa` and new game `0x40e8a5` on a memory disk filled
with `--file NAME PATH` (`sav\gamefile.str`, slot files, `data\` launch
files); every original write lands on that disk, which returns in an `ES2D`
packet with the metadata and, with `--emit-save`, the SAV of the final
state. `--emit-save-after` makes a preparation emit the SAV after the
preparation. `slots_oracle.py` is the client used by `//shell:slots_real_test`
and `//shell:menu_real_test`; see
[shell_invitro.md](../../../docs/reference-game/shell_invitro.md#slot-mode).

The other operations run further original functions on the memory disk and the
worker's memory state. Original functions that reach the UI, drawing, sound
or MCI are trapped, recorded or quiet, as in the results path; some trap sets
(CREW/ARMORY, the movie player) are armed only once an operation of their group runs. Inputs of an
operation are `--file` copies and memory-disk files (`*.in`); results are
appended to memory-disk files (`*.emit`, `squad.condition`, `help.path`) that
return in the `ES2D` packet. Operation arguments, record layouts, traps and
results are in the section of the document named in the last column. The tests are
`//shell:<name>`.

| Operations (original VA) | Test | Document |
| --- | --- | --- |
| service bay: `mount:B:S:I` `0x411b10`, `guidance:B:S:M` `0x43e346`, `rebalance:O` `0x412a37`, `arm:B:S:I` `0x41501f`, `launch` `0x40f68c` | `bay_real_test` | [shell_invitro.md](../../../docs/reference-game/shell_invitro.md#service-bay-operations) |
| Rock & Roll, boxes, Preferences: `checks` `0x4110c1` `0x411135` `0x40fc7e`, `rockroll` `0x445b99`, `restore:N`, `final:W`, `replay-yes` `0x44d1d4`, `replay-no` `0x44d24d`, `continue` `0x431a73`, `mode:M` `0x40ec56`, `display:V`, `prefs-load` `0x40dc44`, `prefs:NAME` (writer `0x40dd0a`) | `launch_real_test`, `menu_real_test` | docs/reverse-engineering/vshell_ui.md, section 11 |
| bay editors: `select:B` `0x482bc9`, `screen:W` `0x4758b8`, `herc-type:T` `0x478780`, `repair-pick:T:R` `0x434549`, `repair-snapshot` `0x433f86`, `repair-one` `0x4351bd`, `repair-all` `0x4352e9`, `repair-cancel` `0x435403`, `build:T` `0x4475ce`, `scrap` `0x448326`, `editor-answers`, `editor-state`, `emit` | `editors_real_test` | docs/reverse-engineering/vshell_ui.md, section 12 |
| slot statistics: `stats` `0x4309f0`, `stats-refresh:K` `0x430ec4`, `stats-dump` | `stats_real_test` | docs/reverse-engineering/vshell_ui.md, section 13 |
| practice screen, INSTANT ACTION, VIEW DEMO, DEBUG dialog: `practice-show` `0x431f3a`, `practice-entry:I` `0x44c40c`, `practice-control:K:B`, `practice-state`, `practice-menu` `0x44c96a`, `practice-begin` `0x44ca26`, `instant-action` `0x431935`, `use-default` `0x44dbea`, `debug-accept:TEXT` `0x44dc4d`, `view-demo` `0x431bfe`, `practice-reset`, `practice-emit` | `practice_real_test` | docs/reverse-engineering/vshell_ui.md, section 15 |
| mission-screen texts: `briefing[:N]` `0x4135bb`, `campaign:N` `0x40fd2d`, `debrief-stats:O:T:L:S` `0x40f904` | `texts_real_test`, `menu_real_test` | docs/reverse-engineering/vshell_ui.md, section 16 |
| CREW, ARMORY, weapons scrap: `building:V` `0x4825c9`, `s17-reset`, `crew-show` `0x442198`, `crew-order:O` `0x442215`, `crew-pilot:G` `0x442548`, `crew-bay:K`, `crew-clear` `0x4429e2`, `show-select`, `armory-show` `0x449b87`, `armory-leave` `0x449c46`, `armory-row:R:B` `0x449d2f` `0x44a06e`, `armory-clear` `0x44a584`, `armory-scrap` `0x44a508`, `armory-accept:T` `0x448447`, `s17-state` | `editors2_real_test` | docs/reverse-engineering/vshell_ui.md, section 17 |
| image primitives: `blit` (blitter `0x45a09c`), `unpack` (decompressor `0x453a50`) | `draw_real_test` | [dbm.md](../../../docs/formats/dbm.md), "Images and palettes on the frame" |
| `text-draw`: string draw `0x401355`, label draw `0x40b9e9`, edit fields `0x40c294` `0x40c382` `0x40c406` `0x40c6ff` | `text_real_test` | docs/reverse-engineering/vshell_text.md, "Own implementation" and "In-vitro acceptance" |
| `widget-draw`: erase `0x409f61`, ESRect `0x40acd6`, ESButtonFont `0x40a129`, ESRegionFill `0x40af56`, ESTitle `0x40b1d7`, ESArm `0x40cac3`, ESGrid `0x40bf2c`, ESQuad `0x40abb0`, ESPal `0x40d0f0` | `widgets_real_test` | docs/reverse-engineering/vshell_widgets.md, section 9 |
| `help[:ROOT]`: the help routine `0x431e79` with WinHelpA's wrapper `0x407953` and the sound-focus stub `0x42f8ff` trapped | `help_real_test` | docs/reverse-engineering/vshell_ui.md, section 21 |
| movies: `movie-init` `0x41f11e`, `movie-flag:V`, `movie-once:A:B:C`, `movie-enqueue:...` `0x41e8c0`, `movie-play:...` `0x41e98c`, `movie-dump` | `movies_real_test` | docs/reverse-engineering/vshell_ui.md, section 19 |
| mission map: `map-raster` (triangle `0x457de8`, polygon `0x455ad8`, textured quad `0x45399c`, line `0x455624`, ellipse `0x45886c`, pixel `0x459cdc`, rectangle `0x4576a4`, bitmaps `0x4537e8` `0x453908`), `map-build:ID`, `map-step:T`, `map-arrow:K`, `map-skip` `0x425a43`, `map-paint`, `map-frame` (map object `0x40e75f`, fly-over `0x4262cf`) | `mission_map_real_test` | docs/reverse-engineering/vshell_mission_map.md, section 14.1 |
| `squad-condition`: `0x4121f8` per bay, run after `restore:N` | `condition_real_test` | docs/reverse-engineering/vshell_ui.md, section 36.4 |

Traps worth knowing before changing a group: `campaign:N` clears the stack below
its frame first, because the function's path buffer is not initialised; `help`
runs only with the language bytes `E`, `F` and `G`, any other byte reaches the
severity-4 assertion that ends VSHELL; `movie-play` fails the avivideo open
`0x41e51b`, so no MCI is reached, and turns the `__stdcall` busy cursor
`0x408d2f` into a plain `ret 4` for the call; the map operations patch the
tick `0x465d5c` to a counter and set up the screen, context, frame header,
identity matrix and static tables that the game builds at start-up.
