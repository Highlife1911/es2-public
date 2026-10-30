# Original VSHELL preparation

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

`--op OP` (repeatable) selects the slot mode: VSHELL's own save
`0x40e933`, load `0x40eaaa` and new game `0x40e8a5` on a memory disk filled
with `--file NAME PATH` (`sav\gamefile.str`, slot files, `data\` launch
files); every original write lands on that disk, which returns in an `ES2D`
packet with the metadata and, with `--emit-save`, the SAV of the final
state. `--emit-save-after` makes a preparation emit the SAV after the
preparation. `slots_oracle.py` is the client used by `//shell:slots_real_test`
and `//shell:menu_real_test`; see
[shell_invitro.md](../../../docs/reference-game/shell_invitro.md#slot-mode).

Slot-mode operations for the service bay: `mount:B:S:I`
(`0x411b10`), `guidance:B:S:M` (`0x43e346`), `rebalance:O` (`0x412a37`),
`arm:B:S:I` (`0x41501f` after the ARM loader; answers in the metadata) and
`launch` (`0x40f68c`, writes `data\mission.var` and `data\player.mec` to the
memory disk); up to 512 `--op`. `//shell:bay_real_test` drives them; see
[shell_invitro.md](../../../docs/reference-game/shell_invitro.md#service-bay-operations).

Slot-mode operations for Rock & Roll, the boxes and Preferences:
`checks` (answers of `0x4110c1`, `0x411135` for the player's bay and of
`0x40fc7e`), `rockroll` (the handler `0x445b99`; a refusal records the
warning box `0x44d90c(n)`), `restore:N` (the N-th `--state SAV` through the
original readers, up to 512 states per run), `final:W`, `replay-yes`
(`0x44d1d4`), `replay-no` (`0x44d24d`), `continue` (`0x431a73`), `mode:M`
(`0x40ec56`), `display:V`, `prefs-load` (`0x40dc44`) and `prefs:NAME` (the
Preferences controls, ACCEPT with the writer `0x40dd0a`, CANCEL). The UI
functions they reach are trapped like the results path's (recorded: the
boxes, the main menu and the bay show; quiet: cursor, show/hide, control
refreshes, CD music, display switch); `_memset`/`_memcpy` are bound for the
prefs loader. The metadata adds both prefs blocks, the exit code and the quit
flag. `//shell:launch_real_test` and `//shell:menu_real_test` drive them; see
docs/reverse-engineering/vshell_ui.md, section 11.

Slot-mode operations for the service-bay editors: `select:B` and
`screen:W` (bay selection `0x482bc9`, screen word `0x4758b8`; 65535 is -1),
`herc-type:T` (`0x478780`), `repair-pick:T:R` (`0x434549`),
`repair-snapshot` (`0x433f86`), `repair-one` (`0x4351bd`), `repair-all`
(`0x4352e9`), `repair-cancel` (`0x435403`), `build:T` (`0x4475ce`), `scrap`
(the warning's ACCEPT `0x448326`; in REPAIR its `0x43dcdd(bay)` is recorded),
`editor-answers` (the numbers the enable rules read, from `0x41124e`,
`0x411101`, `0x412baa`, `0x41232a`, `0x411a78`, `0x41401b`, `0x414174` and
the bank words), `editor-state` (selection and REPAIR snapshot globals) and
`emit` (the SAV from the original writers now, appended to the memory-disk
file `editors.emit`, so one run serves many sequences). The REPAIR/BUILD
refreshes, row texts, squad-panel refreshes, the warning's hide and the
redraw `0x41f3ed` are trapped quietly. `//shell:editors_real_test` drives
them; see docs/reverse-engineering/vshell_ui.md, section 12.

Slot-mode operations for the slot statistics: `stats` (the
start-up read `0x4309f0` of every flagged slot 0..10), `stats-refresh:K`
(`0x430ec4(K)`, from the memory state, e.g. the game restored by `--save`)
and `stats-dump` (the eleven 0x43-byte records at `0x48cebc` appended to the
memory-disk file `stats.emit`). `//shell:stats_real_test` drives them; see
docs/reverse-engineering/vshell_ui.md, section 13.

Slot-mode operations for the practice screen, INSTANT ACTION,
VIEW DEMO and the DEBUG dialog: `practice-show` (PRACTICE MISSIONS
`0x431f3a`), `practice-entry:I` (the entry handlers, `0x44c40c`),
`practice-control:K:B` (the five parameter handlers with a release event of
button byte B, 1 left or 3 right; Herc Type only while its button is
enabled, as the text button `0x409d8a` decides), `practice-state`
(`0x479c54`, `0x473784` and that button's +0x49), `practice-menu`
(`0x44c96a`), `practice-begin` (`0x44ca26`), `instant-action` (`0x431935`),
`use-default` (the click `0x413305` posts: `0x44dbea`), `debug-accept:TEXT`
(`0x44dc4d`), `view-demo` (`0x431bfe`), `practice-reset` (the image values of
`0x479c54` and `0x473784`, i.e. a new process) and `practice-emit` (the DATA
files, the SAV from the original writers and eight globals, appended to the
memory-disk file `practice.emit`). The practice screen's and the dialog's
widget globals point to blocks of the worker's own memory, and `0x40ab3b`,
`0x40b8bb`, `0x402266`, `0x4077ee` are trapped quietly.
`//shell:practice_real_test` drives them; see
docs/reverse-engineering/vshell_ui.md, section 15.

Slot-mode operations for the mission-screen texts: `briefing`
(`0x4135bb` on the memory state and `data\mission.str`), `briefing:N` (the
same after record N of the memory-disk file `texts.in`, `[u32 n][n bytes
MISSION.STR][148 bytes selection]` repeated, became `data\mission.str` and
`0x4840cc`), `campaign:N` (`0x40fd2d(N)`; the op clears the stack below its
frame first, because the function's path buffer is not initialised) and
`debrief-stats:O:T:L:S` (`0x40f904` with the words the results path pushes,
on the memory state; the ten value labels are stand-in objects whose
no-draw bit is set). Each appends its texts to the memory-disk file
`texts.emit` (`[u32 size][bytes]`, size 0xffffffff for a null pointer).
`//shell:texts_real_test` and `//shell:menu_real_test` drive them; see
docs/reverse-engineering/vshell_ui.md, section 16.

Slot-mode operations for CREW, ARMORY and the weapons scrap:
`building:V` (`0x4825c9`), `s17-reset` (the image values of the CREW order
`0x477778`, its pilots `0x47777c[3]` and the ARMORY row `0x479210`),
`crew-show` (`0x442198`, the order rows `0x442215(0..3, 0)`, `0x43dcdd` per
bay with a record), `crew-order:O` (`0x442215`), `crew-pilot:G`
(`0x442548`), `crew-bay:K` (`0x43dcdd` with the selector flag `0x47665a`),
`crew-clear` (`0x4429e2`), `show-select` (the REPAIR/WEAPONS shows'
`0x43dcdd(0x41124e)`), `armory-show` (`0x449b87`), `armory-leave`
(`0x449c46`), `armory-row:R:B` (`0x449d2f`, `0x44a06e`), `armory-clear`
(`0x44a584`), `armory-scrap` (`0x44a508`; the warning `0x448227` is trapped
and recorded with its category), `armory-accept:T` (`0x448447`) and
`s17-state` (metadata array `s17`: selection, order, order pilots as roster
groups, ARMORY row, bank free count and entries, the difficulty word
`0x4826ea`). For these operations `0x43dcdd` runs for real, the empty widget
pointers of the squad panel, CREW and ARMORY point to a zeroed stand-in
object with do-nothing methods, and the text, drawing and string-table
functions (`0x4422df`, `0x43da1a`, `0x40b8bb`, `0x40ab3b`, `0x4087f0`,
`0x44a33b`, `0x4499b9`, `0x40b808`, `0x40b7e9`) are trapped; these traps are
armed only once one of these operations runs. `//shell:editors2_real_test` drives
them; see docs/reverse-engineering/vshell_ui.md, section 17.

Slot-mode operations for the image primitives: `blit` (every
record of the memory-disk file `blit.in`, a frame and a bitmap with position,
clip and mirror, through the blitter `0x45a09c` into a memory frame of the
record's size; the blitter's bounds `[0x47c4a6]`, `[0x47c4aa]` and row table
`[0x47c482]` point at that frame for the call and are put back; the frames
are appended to `blit.emit`) and `unpack` (every record of `unpack.in`, kind
and stored bytes, through the decompressor `0x453a50` on a stand-in bitmap
object; `[u32 U][U bytes]` appended to `unpack.emit`, the buffer freed with
`0x408760`). `//shell:draw_real_test` drives them; see docs/formats/dbm.md,
section "images and palettes on the frame".

The slot-mode operation `text-draw`: the cases of the memory-disk
file `text.in` (canvas size, then per case kind, font, alignment, colour,
flags, background colour, character set, prefix, rectangle, pen, width,
seed and text) run VSHELL's own string draw `0x401355`, label draw
`0x40b9e9` or edit field (`0x40c294`, `0x40c382`, `0x40c406`, `0x40c6ff`)
into a memory canvas bound as the screen, with the fonts of `loadFont`
`0x4088da` after the GL class registration `0x453048`; `0x41fbd9` returns
at once and the context it would set is built by the op. Each case appends
the canvas SHA-256 (and the edit field's length, width and text) to
`text.emit`. `//shell:text_real_test` drives it; see
docs/reverse-engineering/vshell_text.md, sections "Own implementation" and "In-vitro acceptance".

The slot-mode operation `widget-draw`: the 56-byte cases of the
memory-disk file `widget.in` (canvas size, then per case kind, flags,
rectangle, frame colour, the class fields `+0x55..+0x71`, the grid colour and
a seed) run VSHELL's own widget draws on a zeroed object: the erase
`0x409f61`, the ESRect frame `0x40acd6`, ESButtonFont `0x40a129`, ESRegionFill
`0x40af56`, ESTitle `0x40b1d7`, ESArm `0x40cac3`, ESGrid `0x40bf2c`, ESQuad
`0x40abb0` and ESPal `0x40d0f0` (its display flush runs on a zeroed stand-in
display), into the canvas and context of `text-draw` (`0x41fbd9` returns at
once). Each case appends the canvas SHA-256 (flag 0x80: and the canvas) to
`widget.emit`. Its dispatch branch sits before `s17_op`, as
`text-draw`'s. `//shell:widgets_real_test` drives it; see
docs/reverse-engineering/vshell_widgets.md, section "widget toolkit".

The slot-mode operation `help[:ROOT]`: VSHELL's help routine
`0x431e79` (ONLINE MANUAL) runs on the memory disk with WinHelpA's wrapper
`0x407953` and the sound-focus stub `0x42f8ff` trapped. `ROOT` is stored as the
CD root `0x4823b9` first; `data\language.cfg` is a `--file`; the prefs.cfg
write lands on the disk as for Preferences, and every path handed to the
wrapper is appended (NUL-terminated) to the memory-disk file `help.path`. Only
the language bytes `E`, `F` and `G` can run: another byte reaches the
severity-4 assertion that ends VSHELL. Its dispatch branch sits before
`s17_op`. `//shell:help_real_test` drives it; see
docs/reverse-engineering/vshell_ui.md, section 21.

The movie operations: `movie-init` (the static initialiser
`0x41f11e`, indices and player flags 0, movie flag 1, once-flags 0),
`movie-flag:V`, `movie-once:A:B:C` (`0x477946..0x477948`),
`movie-enqueue:I:X:Y:W:H:P:C:F` (`0x41e8c0` with the eight dwords),
`movie-play:A:BAR:PANEL:FULL:PAGE` (`0x41e98c(A)` with stand-in objects for
the bay bar, the theatre panel and the other panels; the avivideo open
`0x41e51b` is trapped and fails, so no MCI is reached; the `__stdcall` busy
cursor `0x408d2f` becomes a plain `ret 4` for the call) and `movie-dump`
(304 bytes into the memory-disk file `movies.emit`: ring, indices, player
flags, movie flag, once-flags, the selection words `0x48419c`/`0x48419e`).
The player's callees are trapped once a `movie-play` runs, added to the
extra list of `s17_op`. `//shell:movies_real_test` drives them; see
docs/reverse-engineering/vshell_ui.md, section 19.

Slot-mode operations for the mission map: `map-raster` (VSHELL's
drawing primitives on records of `map.in` into a memory canvas: the
triangle `0x457de8`, polygon `0x455ad8`, textured quad `0x45399c`, line
`0x455624`, ellipse `0x45886c`, pixel `0x459cdc`, rectangle `0x4576a4`,
bitmap `0x4537e8`, scaled bitmap `0x453908`; one SHA-256 per record in
`map.emit`) and `map-build:ID`, `map-step:T`, `map-arrow:K`, `map-skip`,
`map-paint`, `map-frame` (VSHELL's map object `0x40e75f` on the memory disk
from `maps.in` plus `--file` copies of the CD's `DATA\MFORMS.DAT`,
`MAT0.DAT` and `MAPLABEL.STR`, the fly-over `0x4262cf` with the tick
`0x465d5c` patched to a counter, the arrow slots, the skip `0x425a43`, the
paint; a 64-byte probe per operation). Set-up done by the worker (the game
does it at start-up): a stand-in current screen `[0x48e25c]` and context
`[0x48e264]`, the frame header `[0x481948]` over a 640 x 480 buffer, the
identity matrix `0x5d8088` (`0x465ab8`) and its pointers, the static
initializer `0x420839` (camera angles), `0x48110a` reset, the shared tables
`0x424408`; `0x40e75f` is untrapped for the call and `0x4290b3` (the 3D
terrain's per-cell pass, never drawn by the map) returns at once.
`//shell:mission_map_real_test` drives them; see
docs/reverse-engineering/vshell_mission_map.md, section 14.1.

The slot-mode operation `squad-condition`: per bay of the hangar
`[0x482ba7 + 4k]` a record flag and `0x4121f8(record + 8, record word
+0x4c)` (100 without a record), 24 bytes per call appended to the
memory-disk file `squad.condition`. `//shell:condition_real_test` drives it
after `restore:N`; see docs/reverse-engineering/vshell_ui.md, section 36.4.
