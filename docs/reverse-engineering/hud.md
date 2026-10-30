# Cockpit panel, HUD and message ports (`0x431000`–`0x43dfff`)

The cockpit panel, the HUD, the MFD and HDD, the message ports and the
widgets of `DBSIM.EXE`, and the own code that implements them. Addresses
refer to the reference `VER95/DBSIM.EXE` (SHA-256 `8561aa47…c4ad`,
[reference_version.md](../reference_version.md)) at image base
`0x400000`; every contract was read from its `llvm-objdump -d -M intel`
listing.

Status words:

- **Confirmed** means read from the instructions. For own code it also
  means equal to the original in vitro (the named differential test, on
  synthetic inputs) and, where the text says "validated live", equal in
  the oracle suites with the hook replaced.
- **Hypothesis** marks role names and field meanings that the bytes do not
  prove. Role names in the tables are hypotheses unless stated otherwise.
- The rows of the own process (`sim/src`, `sim/dispatch/services*`) are
  confirmed in vitro only; none of them has been checked live.

Notation used throughout: H = `[0x49b088]`, the cockpit object; P =
`[H+0x203]`, the player vehicle; W = `[P+0x202]`, its weapon system; GC =
`[0x6c5f70]`, the current graphics context; D = `[0x4cf99c]`, the display
record (mode `D+0x14`, byte `D+0x1c`).

## Classification of the live functions

Evidence: `<es2-data>\logs\d-04-20261008` (`hud-region-431000-43e800.asm`,
`outside-callers.asm`, `summary.txt` = per-function calls, writes and
globals, `region.txt`, `callers.json` = grep-level call graph including
jump-table bodies, `vtable_classes.txt`, `rtti_strings.txt`, `tools/`).
Live statements come from the entry-probe counts of the
[DBSIM map](../reference-game/dbsim_map.md) (the six-suite `map.json` of `dbsim-map-20261008`/`-20261010`, which es2-data no longer holds; the current map has 40 suites; saturated at 1000, order a60/c1s/c1c/c3_09/c4_08/c3_10; "n/p" = not
probeable). Class names are the Borland RTTI type names: the dword at
`vtable − 12` points to a type descriptor in CODE whose name offset is the
word at `+6`.

The region holds 223 map entries: 99 live, 107 never entered in the six
suites, 17 not probeable. The live set is classified below; dead ones are
listed only where a live function reaches them. The map undercounts one
function with a jump table, `0x436abc` (120 → 141 instructions); the HUD
key handler `0x432bc8` uses compare chains, not a table.

### Result in short

1. **The cockpit target `[H+0x210]` is simulation state.** Writers:
   `0x43349c` (next target), `0x4333c8` (nearest), `0x4332dc`
   (select/clear) and the rendering frame itself: the cockpit panel frame
   `0x4327ac`, called by `0x45fca8` at `0x45fd4f`, sets it to 0 when
   `0x433174` declares the target invalid (`0x432899..0x4328b3`). Readers:
   `0x41b130` (copies it into the player's target `P+0x1a4`, also from the
   rendering frame, `0x45fdbf`, after `0x4327ac`); the predicate
   `0x411e80(A, B, flags)` (logic frame, callers `0x411fa0` and
   `0x41e0e0`; **hypothesis:** AI target eligibility), which returns 0 for
   B = the cockpit target when A's byte `+0x9a` is set
   (`0x411f75..0x411f8f`); the MECH order handler `0x420ad4` (vehicle
   virtual `+0x28`, `0x420b52`, `0x420c67`); and HUD/MFD draws.
2. **More simulation writes start in this region:** HUD weapon selection
   `0x432a50` (selected weapon `W+0x1d` through `0x4106ac` → `0x410708`),
   wingman orders `0x431610` (wingman virtual `+0x28`), and the HUD key
   handler `0x432bc8` (target keys, weapon keys, `0x41b468`, `0x40e4ac`).
3. **Observable side effects without simulation fields.** Many
   presentation functions draw RNG stream B (`0x492ee0` on `0x4d260b`,
   compared as `rng_b_hex`) or call the clock wrapper `0x467828`, which
   moves the intra-frame virtual clock ([audio.md](audio.md),
   [timing.md](timing.md)). Two of them run inside the logic frame:
   `0x434010` (from `ground_collide`) and `0x434144` (from
   `ground_effects`). A replacement must reproduce the draws and clock
   calls in count and order ([the HUD line contract](#the-hud-line-contract--coupling-set-and-frame-writes)).
4. **The rendering frame `0x45fca8` is not presentation-only.** Besides
   the target clear, its call `0x41b130(player)` writes the player's
   throttle set point `P+0x290` (compared as `throttle_raw_q10`), the
   target and its reference counts, weapon and shield state
   ([`0x41b130(P)`](#0x41b130p)). [main_loop.md](main_loop.md) calls
   `0x45fca8` "world and HUD rendering"; it also contains these writes.

### Legend

| Code | Meaning |
|---|---|
| **D** | draws: calls the graphics library `0x480000`–`0x48cfff` (the `GL*` classes, RTTI names `GLPalette` `0x48237a`, `GLFont` `0x482c39`, `GLBitmap` `0x4836f0`; e.g. `0x4816a8`, `0x482634`, `0x4824ec`, `0x486704`, `0x487e60`, `0x488012`/`0x488143`, `0x483a04`, cursor `0x48992e`/`0x486e70`), the graphics-context push/pop `0x4311e0`/`0x431210`, or a draw virtual |
| **U** | presentation state: writes only cockpit/HUD/widget/port fields, text buffers or globals that only presentation code reads |
| **P** | pure helper: result only (locals) |
| **S** | changes simulation state |
| **Q** | cockpit message queue and ports ([mission_runtime.md](mission_runtime.md)) |
| +B | draws RNG stream B (directly, through `0x438d6c`, or through the sound rows `0x462830`/`0x4628e0` under the audio.md rule) |
| +C | calls the clock wrapper `0x467828` (GetTickCount-based, virtual clock) |
| +A | audio call (`0x462830`, `0x462940`, `0x463114`; [audio.md](audio.md)) |
| +H | allocates from the game heap `0x47749c` |

"Callers" are calling functions from the grep-level call graph (direct
`call`/tail `jmp` in a linear listing, so jump-table bodies are included);
"vt" gives the vtable slot address for virtual methods.

### Objects

| Object | Class (RTTI) | Constructed | Fields used here (confirmed offsets; meanings hypotheses) |
|---|---|---|---|
| H = `[0x49b088]` | cockpit panel (no vtable at `+0`) | `0x431008` (player `+0x203`, cycle cursor, target-list blocks from `0x4774f0`, wingman slots `0x4d03c8` via `0x431098`) and `0x431bf8` (ports and sub-objects), both called from `0x429834`; both never entered after the proxy arms | `+0x00` panel object (`0x452c6c`, `0x4530fc`, `0x4530bc`); `+0x04` graphics canvas (clip rect `+0x210..+0x21c`, origin `+0x220`); `+0x70[10]` panel elements (`+0xbd` weapon, `+0x8c` powered); `+0x98` element count; `+0x1e1`, `+0x1e5`, `+0x1e9`, `+0x1ed` (MFD, mode `+0xc1`), `+0x1f1` (throttle gauge), `+0x1f5` HUD, `+0x1f9` panel sub-objects; `+0x203` player vehicle; `+0x207` DialogPort/CommandPort; `+0x20b` EventPort; `+0x20f` panel-off byte; **`+0x210` target**; `+0x214`/`+0x218` target-cycle cursor; `+0x21a`/`+0x21e`/`+0x222` ScrollTriggers; `+0x226..+0x23a` cursor state; `+0x23e` init done; `+0x241` startup clock, `+0x245` startup done; `+0x246[..+0x25a]` widgets; `+0x25e` nav marker (12 bytes), `+0x26a` active, `+0x26b` armed; `+0x26c` target copy, `+0x270` aim point, `+0x27c`/`+0x27e` |
| `[H+0x207]` | `DialogPort` (vtable `0x49bad4`, constructor `0x435bf0`) or `CommandPort` (`0x49baa8`, constructor `0x436244`) | at `0x431d84` / `0x431d34`, chosen by word `0x4aa728` (0 → DialogPort) | in all six suites a DialogPort: its per-frame slot `0x4361cc` runs 1000×5/937, CommandPort's `0x4365d0` never |
| `[H+0x20b]` | `EventPort` (vtable `0x49ba7c`, constructor `0x4369a4`) | at `0x431e1e` | radio/voice port; its receive slot `0x436a3c` copies the record through `0x434e8c` |
| base | `MsgPort` (vtable `0x49bb00`) | | slot `+0x10` = `0x435610` |
| `[H+0x1f5]` | `HUD` (vtable `0x49c138`) | `0x43c82d` | record at `+0xb1` (0x26 bytes), gadgets `+0xd7[..+0xff]`, `+0xdb`, `+0xdf`, `+0xe3`, `+0xe7`, `+0xeb`, `+0xf3` |
| panel scroll zones | `ScrollTrigger` (vtable `0x49b7b4`) | `0x433770` | rect at `+0` |
| widgets | `SystemGadget` (`0x49b774`), `LEDBarGraphH/V`, `LEDBMBarGraphH/V`, `BarGraphH/V`, `PanelGauge` (`0x49bc6c`) | | |
| HUD gadgets | `HUDCrosshairGunsight` (`0x49c124`), `HUDLockingGunsight` (`0x49c1c4`), `HUDRovingGunsight` (`0x49c1b0`), `HUDRovingGunsightGadget`, `HUDPipper` (`0x49c174`), `HNavGauge` (`0x49c154`), `HRevRotationGauge` (`0x49c1ec`), `HFixRotationGauge` (`0x49c22c`), `VFixRotationGauge` (`0x49c20c`), bases `RotationGauge`, `HUDGunsight`, `HUDGauge` | `0x43b3xx`–`0x43cbxx` | |

The type-name strings of the cockpit module (`PanelGraphicSetting`,
`CommandPort`, `DialogPort`, `EventPort`, `EnergyWeaponGauge`,
`ProjectileWeaponGauge`, `TogglePodGauge`, `PodWeaponGauge`,
`EnergyPodGauge`, `WeaponRangeGauge`, `EnergyPoolGauge`, `ShieldsGauge`,
`MFDGauge`, `ThrottleGauge`, `HUD`, `HDDGauge`, `PanelAmbience`,
`ObjectiveAlert`, `ScrollTrigger`, `SystemGadget`) stand at
`0x434acd..0x434db1`, the HUD gadget names at `0x43e25d..0x43e71f`.

### Order inside the rendering frame (confirmed)

`0x45fca8` (main loop step 6, [main_loop.md](main_loop.md)) runs, among
the world drawing: `0x4327ac(H)` at `0x45fd4f` → … → `0x41b130(player)` at
`0x45fdbf` → HUD draw `[H+0x1f5]` virtual `+8` = `0x43d704` at `0x45fdd4`
→ DialogPort frame `0x433158()` virtual `+0x10` = `0x4361cc` at
`0x45fde0` → EventPort frame `[H+0x20b]` virtual `+0x10` = `0x436f70` at
`0x45fdf2` → widget hover/draw `0x434520` at `0x45fe28`.

### Classification table

**Cockpit panel core (`0x4311e0`–`0x434a24`)**

| VA | Role | Instr. | Calls per suite | Callers | Class | Evidence |
|---|---|---:|---|---|---|---|
| `0x4311e0` | push graphics context | 13 | 1000×6 | 29 draw functions | D | `0x4d03ec[++byte 0x49b106]` ← `[0x6c5f70]`; `0x480d44([H+4])` selects the panel canvas and returns it |
| `0x431210` | pop graphics context | 7 | n/p | same 29 | D | `0x480d44(0x4d03ec[byte 0x49b106--])` |
| `0x43122c` | text in a rectangle | 140 | 83/1000×5 | 26 (panel, MFD, `0x461608`) | D | skips when word `0x4d2566` or `!0x431410(rect)` or byte `[0x4cf99c]+0x1c`; driver pointers `0x4a57ec`, `0x4a57f0`, `0x4a5824`, `0x4a5828`, `0x487e60` |
| `0x431410` | rectangle visible in the panel view | 84 | 1000×6 | 34 draw functions | P | 0 if byte `H+0x20f`; corner tests of the argument rect (minus `0x4d2556/0x4d255a`) against `[H+4]+0x210` + `+0x220` and size `0x4d253e/0x4d2542`; writes locals only |
| `0x431500` | panel off | 8 | 0/0/0/1/1/1 | `0x429e60` | U | `arg+0x20f` ← 1 if 0; the byte makes `0x431410` false, `0x432bc8` return 0, `0x4327ac` draw only `[H+0x1f9]` |
| `0x431610` | wingman order | 33 | 0/0/0/4/0/0 | `0x44a1a0` | **S**+Q | wingman `w = 0x4d03c8[slot]` (≠ `[H+0x203]`, `w+0x99` = 0): record `+2` ← player, `w` virtual `+0x28(w, rec, 0)` (`0x43164d`; MECH `0x420ad4`, which reads `[H+0x210]`); then `0x435ac8([H+0x207], w, 0x22)` |
| `0x43271c` | scroll the panel | 57 | 0/0/0/128/0/0 | `0x429e60`, `0x42d4a8`, `0x42d624` | U+D | `0x452bec`; canvas origin `[H+4]+0x220` −= delta (`0x432753`); `0x433968`; widgets `0x452434` |
| `0x4327ac` | cockpit panel frame | 107 | 1000×5/937 | `0x45fca8` | D+**S** | returns at byte `0x49b104`; once `0x433770` (`H+0x23e`); `0x432924` until `H+0x245`; `0x43408c`, `0x434194`; panel draws (virtual `+8` of `H+0x70[10]`, `+0x1e1`, `+0x1e5`, `+0x1e9`, `+0x1f1`, `+0x1ed`, `+0x1f9`) inside `0x4311e0`/`0x431210`; cursor `0x433c54`; **`[H+0x210]` ← 0 when `0x433174(H, [H+0x210])` = 0 (`0x4328b3`)**; `0x4530fc`; `0x4349ac` |
| `0x4328cc` | arm the startup sequence | 22 | n/p | `0x461608` | U+C+A | `H+0x241` ← clock unless `H+0x245`; sound `0x13`; flyer chassis: sound `0x2d`, `0x463114(0x2d, 0xa410)` |
| `0x432924` | startup sequence | 104 | 70/70/70/71/0/70 | `0x4327ac` | U+C+Q | elapsed = clock − `H+0x241`; element i powered (`0x438ddc`) when elapsed > word `0x49b05a[i]` (20, 40, … 200); `+0x1e9`, `+0x1f5`, `+0x1ed` when elapsed > 0; past 200: `H+0x245` ← 1 and message `0x22` to the EventPort if `condition_ratio(P+0x206, 0, i)` (via `0x41b514`) is nonzero for some i < 10 (the following `0x438700` class test, < `0x5a`, always passes), else `0x21` |
| `0x432a50` | weapon selection from the panel | 21 | 0/0/0/15/0/0 | `0x432bc8`, `0x440f18`, `0x4414dc` | **S** | `0x4106ac([P+0x202], elem)`: weapon virtual `+0x58` search; on a hit `0x410708` sets the selected index `W+0x1d` and word `0x4d2528` ← 0, then `W+0x18` ← 1 (`0x4106ee`) |
| `0x432b14` | HUD part of the input record | 64 | 1000×5/937 | `0x460870` | U | `0x433b20` when `H+0x23e`; record bytes `+0x1e..+0x21` → `0x42a3f4` modes 1/0/5/4; latch `0x49b550`; `[H+0x1f9]` virtual `+0xc(0xf)` |
| `0x432bc8` | HUD key handler | 417 | 1000×5/937 | `0x45feb8`, `0x460870` | mixed, **S** | key table below |
| `0x433134` | wingman slot of an actor | 16 | 0/10/5/96/0/33 | 9 | P | index of the argument in `0x4d03c8[0..2]`, else −1 |
| `0x433158` | message port getter | 7 | n/p | 16 (`trigger_fire`, `0x45fca8`, MFD) | P (Q) | `[[0x49b088]+0x207]`, 0 without cockpit |
| `0x433174` | target valid | 73 | 0/1000/0/1000/692/0 | `0x4327ac`, `0x4332dc`, `0x4333c8`, `0x43349c` | P | contract below |
| `0x433250` | target in the forward arc | 53 | 0/10/0/79/14/0 | `0x4332dc`, `0x43349c` | P | contract below |
| `0x43349c` | next target | 253 | 0/4/0/17/5/0 | `0x432bc8`, `0x4332dc`, `0x446fb0`, `0x460870` | **S** | contract below |
| `0x433770` | create the scroll zones | 158 | 1/1/1/1/1/1 | `0x4327ac` | U+H | three `ScrollTrigger` (0x24 bytes, `0x47749c`) into `H+0x21a/+0x21e/+0x222`, registered with `0x452c6c` |
| `0x433968` | move the scroll zones | 110 | 0/0/0/128/0/0 | `0x43271c` | U | trigger rectangles only |
| `0x433b20` | cursor zone test | 94 | 1000×5/937 | `0x432b14` | U | gated by byte `0x49b105`; tape playback (`0x4d24d6`) reads the cursor from `0x45b750`; `H+0x22e` ← 0, 3, 4, 5 or 6 by zone and display mode |
| `0x433c54` | cursor image | 93 | 1000×5/729 | `0x4327ac` | D | `0x49b08c[H+0x22e]` → `H+0x232`, `+0x226`, `+0x22a`; `0x48992e`/`0x486e70` |
| `0x433d90` | cursor image (HUD) | 48 | 1000×5/729 | `0x43c204` | D | `H+0x236`, last `0x49b62c` |
| `0x433e3c` | cursor image (MFD) | 48 | 0/0/0/14/0/0 | 4 MFD functions | D | `H+0x23a`, last `0x49b630` |
| `0x434010` | panel jolt (damage) | 37 | 0/0/0/5/0/0 | `ground_collide` `0x418f74`, `0x4188c8` | U+**C+B** | nothing in display mode 4 (`[[0x4cf99c]+0x14]`); `0x42ea94`/`0x42d624` if active; `0x49b0fc` ← clock + 60; if `0x49b100` = 0: `0x49b100` ← clock + B%10 (`0x434053`), `0x42ea44`, `0x42d2f8(5 << [0x4d253c])` |
| `0x43408c` | panel jolt, per frame | 61 | 1000×5/937 | `0x4327ac` | U+**C+B** | mode 4 clears `0x49b0fc`; while clock < `0x49b0fc`: past `0x49b100` → `0x42ea70`, re-arm with B%10 (`0x4340d1`); `0x42d4a8(B%5)` (`0x4340ef`); afterwards `0x42ea94`, `0x42d624` |
| `0x434144` | panel bump | 23 | 60/229/132/348/0/25 | `ground_effects` `0x4195c8` | U+**C** | not in mode 4: `0x42d854`, `0x49b638` ← 0, clock, then (always, since the end was just cleared) `0x49b634` ← clock, `0x49b638` ← clock + 60, `0x42d820(10 << [0x4d253c])` |
| `0x434194` | panel bump, per frame | 51 | 1000×5/937 | `0x4327ac` | U+**C** | clock; word table `0x49b046[(t − 0x49b634)·10/60]` → `0x42d82c`; end `0x42d854` |
| `0x434228` | | 10 | 1000×5/549 | 4 | P | `(x / 1000) · 6` |
| `0x434260` | | 13 | 0/2/1/18/0/7 | 2 | P | `x/4 + 1` (rounded toward 0), 3 → 4 |
| `0x434280` | | 7 | 0/2/1/25/0/6 | 4 | P | `word 0x4d3b7c[word 0x49b040[i]]` |
| `0x434298` | | 11 | 0/2/1/17/0/6 | `0x435d0c`, `0x45e208` | forwards | `0x44b928([H+0x1f9], x)` (outside the region, not classified) |
| `0x4342b8` | | 2 | n/p | `0x435d0c` | P | `[0x4d13ac]` |
| `0x4342e0` | weapon range class | 19 | 1000×6 | 6 | P | `0x438700(0x41b514(P, 0, n))` (`condition_ratio`) |
| `0x434310` | panel element of the selected weapon | 35 | 1000×5/704 | `0x44718c` | P | element of `H+0x70[0..H+0x98)` whose `+0xbd` = `[W+4][W+0x1d]` |
| `0x434520` | widget hover and draw | 47 | 1000×5/937 | `0x45fca8` | U+D | widgets `H+0x246[0..H+0x25a)`: `+0x1b` ← 0 or 3 by cursor row, draw virtual `[+0x17]` |
| `0x434604`, `0x434634` | widgets disable / restore | 22, 22 | 0/0/0/19/1/1 | `0x42a4c4`, `0x42a9c0` | U+D | through `0x434928` (`+0x5c` ← `+0x1b`, `+0x1b` ← 3) and `0x434944` (restore), each with a draw |
| `0x434748` | `SystemGadget` draw | 157 | 1000×6 | vt `0x49b774` (thunk `0x434dcd`) | D | `0x488012`, `0x4816a8`, `0x488143` |
| `0x43495c` | nav marker getter | 7 | n/p | `0x43c40c`, `0x44e334` | P | `H+0x25e` if byte `H+0x26a`, else 0 |
| `0x4349ac` | nav marker arrival | 35 | 1000×5/729 | `0x4327ac` | U+Q | `dist2d_point(P+0x26, H+0x25e)`; arms `H+0x26b` above 10000; armed and below 10000: `H+0x26a` ← 0, message `0x1d` to the EventPort |
| `0x434a24` | target and aim point for the HUD | 21 | 1000×5/937 | `0x41b130` | U | `H+0x26c` ← T, `+0x270` ← 12-byte aim point, `+0x27c`, `+0x27e`; read only by the HUD (`0x43d7e7`) and the MFD status draw `0x43a5a0` |

**Message ports and the cockpit message queue (`0x434e8c`–`0x437598`)**

Queue Q = port `+4`: ten entries of `0x31` bytes at `Q+0`, in-use bytes
`Q+0x212[10]`, an order list of entry indices at `Q+0x1ea`, count word
`Q+0x21c`. Entry: `+0` code word, `+2` actor, `+6` 12 bytes, `+0x12`,
`+0x16`/`+0x17` bytes (priority `+0x17`), `+0x18` text, `+0x1c`/`+0x20`
durations ×60, `+0x24`/`+0x28` start/end clock, `+0x2c..+0x30` flags. Port
state: current entry `+0x49a`, showing `+0x49e`, phases `+0x4c9..+0x4cb`,
pause clock `+0x4cc`, pause depth `+0x4d0`, hidden `+0x4d2`, blink
`+0x4d3`. The full layout is in
[the message queue, the ports and the gauge blink](#the-message-queue-the-ports-and-the-gauge-blink).
The scroll globals `0x4d1290`/`0x4d1294` are referenced only by port
methods (literal cross-reference). The queue and port fields are reached
through `[H+0x207]`/`[H+0x20b]`; the callers outside the port code
(`0x433158`'s callers, `0x45fca8`, `0x4349ac`, `0x432924`, `0x41b468`,
`0x431610`) only call the port's virtual slots or pass the pointer to the
port functions of this table, so the queue is presentation state
(**hypothesis**: field accesses through copied pointers were not searched
for).

| VA | Role | Instr. | Calls per suite | Callers | Class | Evidence |
|---|---|---:|---|---|---|---|
| `0x434e8c` | queue insert | 152 | 1/13/3/54/13/17 | `0x435c48`, `0x4362e4`, `0x436a3c` | Q+**C** | drops one entry when 10 (`0x4351c0`); first free slot; clock; copies record `+2..+0x15` ([mission_runtime.md](mission_runtime.md)) and the text record; inserts by priority; the `sprintf` to `0x4d123f` is jumped over |
| `0x435074` | queue head | 18 | 1000×6 | `0x435610` | P (Q) | entry of `order[0]` or 0 (its `sprintf` loop is unreachable: `0x43507b` jumps to the test) |
| `0x435140` | remove entry i | 27 | 1/12/3/46/4/8 | `0x4351ec`, `0x435240`, `0x435610` | Q | frees the slot, closes the gap, count − 1 |
| `0x4351ec` | expire | 36 | 1000×6 | `0x435610` | Q+**C** | removes entries whose end clock `+0x28` lies before now, order positions count−1 down to 1 (the head is not tested) |
| `0x435240` | remove by (code, actor) | 38 | 0/2/0/13/0/0 | `0x435ac8` | Q | |
| `0x435298` | shift clocks | 35 | 0/16/0/2/2/2 | `0x435b80` | Q+**C** | adds the pause length to future start/end clocks |
| `0x435574` | move the port rectangle | 22 | 0/0/0/18/0/0 | `0x429e60` | U | `+0x4a7`, `+0x4af` |
| `0x435610` | port frame (`MsgPort` slot `+0x10`) | 122 | 1000×6 | `0x4361cc`, `0x4365d0`, `0x436f70` | Q+U+**C** | unless paused: `0x4351ec`, clock, current ← head; phase machine with the port's virtuals `+4`/`+0xc` and the callbacks `+0x4b9[+0x4b7]`; reads the player's dead byte `[0x4d24e6]+0x99` |
| `0x435a74` | text variant record | 34 | 0/0/0/3/0/0 | `0x435c48`, `0x436a3c` | U | string table `0x453a10`/`0x453a4c`; record in `0x4d0ce6..0x4d0cee` |
| `0x435ac8` | cancel a message | 33 | 0/2/0/16/0/0 | `0x41b468`, `0x431610`, `0x441fa4` | Q | current entry: set phase `+0x4cb`; else `0x435240` |
| `0x435b14`, `0x435b38` | phase flags | 11, 11 | 0/2/1/17/0/6, 0/10/1/18/1/7 | `0x44b514`, `0x44b620`, `0x45464c` | Q | `+0x4ca`, `+0x4cb` |
| `0x435b58`, `0x435b80` | pause / resume | 12, 36 | 0/16/0/2/2/2 | `0x45464c`, `0x45f0d4` / `0x4548c8`, `0x45f108` | Q+**C** | first pause stores the clock; last resume shifts all clocks (`0x435298`) and `0x4d1294` |
| `0x435c48` | DialogPort receive (slot 0) | 80 | 0/2/1/27/0/8 | vt `0x49bad4` (via `0x433158`: `trigger_fire` etc.) | Q+**B** | text tables `0x4d0444`/`0x4d0464`/`0x4d08ed` by wingman slot (`0x433134`); B draw when a row has > 1 variants (`0x435cba`); `0x434e8c` |
| `0x435d0c` | DialogPort text (slot 2) | 56 | 0/2/1/17/0/6 | vt `0x49badc` | U | speaker name + text into `0x4d0cef` |
| `0x435d9c` | DialogPort show (slot 1) | 284 | 0/2/1/17/0/6 | vt `0x49bad8` | D+A | voice file `0x45ef9c` → `0x462940`; text box `0x437164`; `0x4d1f3a` = 1 → no draw |
| `0x436130` | port erase (slot 3) | 52 | 0/10/1/19/1/7 | vt `0x49bab4`, `0x49bae0` | D | callbacks `+0x4c1[+0x4b8]`, `0x488143`, `0x437164` |
| `0x4361cc` | DialogPort frame (slot 4) | 40 | 1000×5/937 | vt `0x49bae4` | Q+U+**C** | phase flag, draw slot `+0x18`, then `0x435610` |
| `0x436a0c` | EventPort text (slot 2) | 16 | 1/11/2/19/4/1 | vt `0x49ba84` | U | `strncpy` into `0x4d0d3f` |
| `0x436a3c` | EventPort receive (slot 0) | 56 | 1/11/2/27/13/9 | vt `0x49ba7c` | Q+**B** | tables `0x4d0454`/`0x4d0aaf`; B draw for rows with > 1 variants (`0x436a6c`); `0x434e8c` |
| `0x436abc` | EventPort show (slot 1) | 120 (141) | 1/11/2/225/65/208 | vt `0x49ba80` | Q+U+**C**+A | clock; repeats within 300 ticks are cancelled; `0x4d1290`, `0x4d1294`; sound rows `0x19`/`0x1a` (`0x462830`, B rule); voice with `0x4d1f3b` |
| `0x436cec` | EventPort draw (slot 6) | 161 | 1000×5/938 | vt `0x49ba94` | D (+C) | clock only for blinking messages (`+0x4d3`, `0x436f27`) |
| `0x436f70` | EventPort frame (slot 4) | 32 | 1000×5/937 | vt `0x49ba8c` | Q+U+**C** | scroll `0x4d1290` from clock − `0x4d1294`; draw slot `+0x18`; `0x435610` |
| `0x436fd0` | EventPort erase (slot 3) | 34 | 1/10/2/19/4/1 | vt `0x49ba88` | D | |
| `0x437094` | Dialog/CommandPort draw (slot 6) | 63 | 0/54/27/457/0/174 | vt `0x49bac0`, `0x49baec` | D | |
| `0x437164` | port text box | 82 | 2/25/6/73/8/14 | 6 | D | |

**Widgets (`0x438700`–`0x43a211`)**

| VA | Role | Instr. | Calls per suite | Callers | Class | Evidence |
|---|---|---:|---|---|---|---|
| `0x438700` | condition class | 27 | 1000×6 | `0x432924`, `0x4342e0`, `0x43b288` | P | `v = (0x100 − x)·100 >> 8` → 0 (≥ 90), 1 (≥ 74), 2 (≥ 51), 3 (≥ 1), 4 |
| `0x438884` | label layout | 59 | 0/56/0/10/6/6 | 26 | U | label struct only |
| `0x438920` | draw a label | 93 | 1000×6 | 39 | D | text position `0x4d17b4`/`0x4d17b8`, `0x4824ec`, `0x486704`, `0x482634` |
| `0x438a94`, `0x438aac` | draw label / with highlighted character | 10, 46 | 0/0/0/90/0/0 | `0x438aac`; 4 | D | |
| `0x438bc0` | blink animation of a gauge | 146 | 0/0/0/1/1/1 | 6 | U+**C+B** | clock; periods `0x438d6c(+0x7c, +0x80)`/`(+0x84, +0x88)`; shape animation calls `0x471de0`, `0x471e10`, `0x471e88`, `0x471f24` |
| `0x438d6c` | random in [lo, hi) | 23 | 0/0/0/1/1/1 | `0x438bc0` | **B** | `lo + B % (hi − lo)` unsigned (`0x438d84`); lo when equal |
| `0x438d9c` | press button i | 27 | 0/4/0/41/14/0 | 10 | U | unless state 2: widget virtual `+8`, `0x4530bc` |
| `0x438ddc` | power-on start | 10 | 6/6/6/13/0/8 | `0x431bf8`, `0x432924` | U+**C** | `+0x8c` ← 1, `+0x90` ← clock |
| `0x438e3c`, `0x43904c` | widget click / event | 26, 24 | 0/4/0/17/5/0, 48/88/62/446/50/70 | 3, 2 | U | handler virtuals only |
| `0x439014` | empty method | 4 | 48/88/62/446/50/70 | 7 vtables | P | |
| `0x4390b8` | getter `+4` | 6 | 1000×5/921 | 4 | P | |
| `0x4395e8` | `LEDBarGraphH` draw | 73 | 1000×6 | vt `0x49bd30` | D | `0x439758`, `0x486704` |
| `0x439758` | LED segments | 84 | 1000×6 | `0x4395e8`, `0x4396a8` | D | `0x483a04` |

**HUD (`0x43b420`–`0x43dd98`)**

| VA | Role | Instr. | Calls per suite | Callers | Class | Evidence |
|---|---|---:|---|---|---|---|
| `0x43b420` | `RotationGauge` set value (slot 3) | 27 | 0/1000/0/688/0/32 | vt `0x49c218`, `0x49c238`, `0x49c258` | U | `+0x3a` −= x, clamped to `+0x24..+0x26` |
| `0x43b4cc` | `HFixRotationGauge` draw | 60 | 1000×5/729 | vt `0x49c22c` | D | |
| `0x43b59c` | `VFixRotationGauge` draw (empty) | 4 | 1000×5/729 | vt `0x49c20c` | P | |
| `0x43b67c` | `HRevRotationGauge` set (slot 3) | 56 | 1000×5/729 | vt `0x49c1f8` | U | heading strip offsets via `0x47e08d` |
| `0x43b704` | `HRevRotationGauge` draw | 61 | 1000×5/729 | vt `0x49c1ec` | D | rewrites the canvas clip `+0x210/+0x218/+0x220` and restores it; `0x45c8bc` |
| `0x43b7cc` | slot 1 → slot 0 | 9 | 1000×5/729 | vt `0x49c1f0` | D | |
| `0x43b808` | `HUDCrosshairGunsight` draw | 115 | 1000×5/729 | vt `0x49c124` | D | `0x48c444(0)` … `0x48c444(saved [0x6c5ff8])`, `0x48c57c/5fc/6d0`, `0x4816a8` |
| `0x43b978` | `HUDLockingGunsight` draw | 597 | 1000×5/729 | vt `0x49c1c4` | D | `dist2d_point`, `dist3d_delta`, `angle_ratio`; `0x48c444(0)` at `0x43ba35`, `0x48c444(local copy)` at `0x43c13a` (restore: hypothesis); writes only its own `+0x4a..+0x56` |
| `0x43c204` | `HUDRovingGunsight` draw | 41 | 1000×5/729 | vt `0x49c1b0` | D | cursor `0x433d90(inside)` |
| `0x43c288` | `HUDPipper` draw (empty) | 4 | 1000×5/729 | vt `0x49c174` | P | |
| `0x43c3f8` | `HNavGauge` shift (slot 3) | 8 | 128/804/18/1000/1000/184 | vt `0x49c160` | U | `+0x24`, `+0x26` += x |
| `0x43c40c` | `HNavGauge` draw | 336 | 1000×6 | vt `0x49c154` | U+D | point: nav marker (`0x43495c`), or the player's target with `0x4d2a6c`, or the next route point (`route_point`); `distance_display`, bearing `0x49290c`, `0x434228`; own fields `+0x3a..+0x4d`; text |
| `0x43d704` | `HUD` draw (slot 2) | 199 | 1000×5/937 | vt `0x49c140` | D+U (+C) | not in display mode 4 without `[0x4cf99c]+0x1c`; `0x4342e0(2)` → `+0x74` and limits from `0x49be48`, then `0x438bc0`; the `0x4332dc` call (`0x43d7d2`) is dead: byte `0x49be74` is 0 in DATA and only ever cleared; copies `H+0x26c..` to `+0xb7..`; clock during the power-on sweep (`+0x8c`, `0x43d87b`); `0x43dcd4`, `0x43dca0`, `0x43e114`, `0x43dd98` |
| `0x43d9b4` | `HUD` set record | 158 | 1000×5/937 | `0x41b130`, `0x4332dc`, `0x4333c8`, `0x43349c` | U | stores the 0x26-byte record (`+6` target, `+0x24` flag) at `+0xb1`, `[+0xe7]+0x14`, `[+0xeb]+0x14`; gadget virtual `+0xc` with the change of words 0/2/4 |
| `0x43dbb0` | `HUD` record getter | 6 | 1000×5/937 | same four | P | `G+0xb1` |
| `0x43dca0` | speed text | 17 | 1000×5/729 | `0x43d5f0`, `0x43d704` | U | `itoa(hud_speed(player))` + suffix into `0x4d17ee` |
| `0x43dcd4` | mission time text | 70 | 1000×5/729 | `0x43d5f0`, `0x43d704` | U+**C** | clock every call; every 60 ticks the `MM:SS` digits at `0x4d17dc..` count up |
| `0x43dd98` | flyer altitude ladder | 284 | 1000×5/729 | `0x43d5f0`, `0x43d704` | D | only for a flyer chassis (`[[0x4d24e6]+0x1f2]+0x50`); `terrain_height` |

**Not probeable entries reached by live code:** `0x431210` (above),
`0x4328cc` (above), `0x433158` (above), `0x433ee8`/`0x433f7c` (cursor
variants, D), `0x4342b8` (P), `0x43495c` (P), `0x434dcd` (adjustor thunk,
`this − 0x20` → `0x434748`), `0x43757e` (empty), `0x437598` (258, only from
the dead `0x431098`), `0x4387ac` (label constructor), `0x438b20`
(`PanelGauge` constructor), `0x43907c` (`BarGraph` constructor),
`0x438e2c` (UI click callback: sound row `0x11`, B rule), `0x43a190`/
`0x43a211` (`BarGraphV/H` fill end by sign of `+0x14`, U), `0x43b360`
(empty destructor).

### The HUD key handler `0x432bc8`

cdecl (H, code). Returns 0 at once when byte `H+0x20f` is set. M =
`[[0x4cf99c]+0x14]` (display mode, set through `0x42a3f4`). The compare
chain at `0x432c03..0x432c84` dispatches on the low word of the code
(confirmed):

| Code | Key | Effect | Class |
|---|---|---|---|
| `0x00` | — | return 0 | |
| `0x01` | Esc | display mode by M (`0x42a3f4`) | U |
| `0x02..0x0b` | 1 … 0 | unless M = 1: panel element `H+0x70[code−2]`: `0x432a50` (weapon selection) and `0x438d9c(elem, 0, 1)` | **S** |
| `0x0f` | Tab | M = 1: `[H+0x1f9]` virtual `+0xc(0xf)`; else with a player weapon `P+0x30b`: `0x40e4ac(W, P+0x1a4)` → `W+0x7d` ← target virtual `+0x80(T, W+0x7d, W+0x86)` | **S** |
| `0x13` | R | `0x41b468(player)`: toggles `P+0x96` when `P+0xa3` (player flag) is set; sound rows `0x1a`/`0x1b`; cancels EventPort messages `0x2c`/`0x2d` and sends one of them | **S**+B+Q |
| `0x1c` | Enter | M = 4: return 0; M = 1: default; MFD mode `[[H+0x1ed]+0xc1]` 3 or 4: MFD button 9 or 7; else `0x43349c` (next target) | **S** |
| `0x27` | `;` | `0x4332dc(H, 0)`: clear the target | **S** |
| `0x3b..0x40` | F1–F6 | MFD button `code−0x3b`, then display mode by M | U |
| `0x41`, `0x42` | F7, F8 | panel `[H+0x1f9]` button `code−0x41`; display mode 0 unless byte `0x4d2538` = 1 | U |
| `0x43`, `0x44` | F9, F10 | display mode 5/6 resp. 4/6 by M | U |
| `0x57` | F11 | modal dialog: `0x47749c(0x3b8)`, `0x457538`, virtual `+8`, `0x4543b0(obj, 3)` | UI |
| `0x220` | Alt+D | `0x434974`: nav marker ← player position, active, not armed | U |
| other | | `0x446c38([H+0x1ed], code)` (MFD), then the panel gadgets | not followed |

The cases at `0x432c32` (`sub eax, 0x13` / `je 0x432f71`) and `0x432c3b`
(`sub eax, 9` / `je 0x432e2f`), with EAX = the code, are `0x13` (R →
`0x41b468`) and `0x1c` (Enter → target cycling); there is no `0x22` or
`0x2b` case ([input.md](input.md) carries this correction).

### H4 candidates

The simulation writers of the region and their own code (all confirmed in
vitro; live status in the sections named):

| Original | Role | Own code | Section |
|---|---|---|---|
| `0x433174`, `0x433250`, `0x43349c`, `0x4333c8`, `0x4332dc` | target valid, in arc, next, nearest, select | `core/src/cockpit_target.c` (hooks `cockpit_valid`, `cockpit_arc`, `cockpit_next`, `cockpit_nearest`, `cockpit_select`) | [Cockpit target cycling as own code](#cockpit-target-cycling-as-own-code) |
| `0x432a50` with `0x4106ac`/`0x410708` | panel weapon selection | `core/src/panel_weapons.c` | [Panel weapon selection as own code](#panel-weapon-selection-as-own-code) |
| `0x431610` | wingman order | `core/src/mech_order.c` `es2_wingman_order_registers` (hook `wingman_order`); called from the HDD executor `core/src/hdd_exec.c` | [mech_order.md](mech_order.md) |
| `0x432bc8` | HUD key handler | `core/src/hud_keys.c` | [HUD key handler as own code](#hud-key-handler-as-own-code) |
| `0x4327ac` | cockpit panel frame | `core/src/panel_frame.c` | [Cockpit panel frame as own code](#cockpit-panel-frame-as-own-code) |
| `0x41b130` | cockpit↔player synchronisation | `core/src/cockpit_sync.c` | [Cockpit↔player synchronisation as own code](#cockpitplayer-synchronisation-as-own-code) |
| `0x431410` | rectangle visible | `core/src/hud_helpers.c` | [HUD-region helpers](#hud-region-helpers) |

`0x4333c8` and `0x4332dc` are never entered in the suites; their evidence
is in vitro only. The pure helpers with presentation callers only
(`0x4342e0` with `condition_ratio`, `0x438700`, `0x434310`, `0x433134`,
`0x434228`, `0x434260`, `0x434280`, the getters) have no hooks; their
contracts above serve the presentation code. The presentation functions
(D, Q, the jolt and bump effects `0x434010`, `0x43408c`, `0x434144`,
`0x434194`, and the HUD record update `0x43d9b4`) have no hooks in the
proxy; their own code lives in the own process (`sim/src`), sections
below. `0x434010` and `0x434144` are called as original game code from
the own `ground_collide`/`ground_effects`.

### For H3 (presentation replacement)

A replacement of `0x45fca8`/`0x45fba4` must keep, per frame and in order:

1. The target clear in `0x4327ac` and the whole of `0x41b130` (or move
   them into the logic frame with the same frame-relative timing): they
   change `[H+0x210]`, `P+0x1a4`, `P+0x290`, reference counts, weapon and
   shield state that the next logic frame reads (`0x411e80`, the vehicle
   and weapon code).
2. Every stream-B draw: `0x43408c` (while a jolt is active), the message
   ports' receive paths `0x435c48`/`0x436a3c` (per message), the sound rows
   of `0x436abc`, `0x438bc0` through `0x438d6c`, and the UI sound callback
   `0x438e2c`.
3. Every clock call (`0x467828`; each one advances the intra-frame virtual
   clock by 1 ms under `virtual_time_intra_frame=1`). Per rendered cockpit
   frame without messages or jolts (measured per call site, [the coupling
   set measured](#the-coupling-set-measured)): one read each for
   `0x434194`, `0x43dcd4` (when the HUD draws) and the EventPort frame
   `0x436f70`; `0x4351ec` and `0x435610` once per port, so two each. Plus
   `0x432924` during the startup sequence, `0x43408c` during a jolt,
   `0x43d704` during the HUD power-on sweep, `0x434e8c`/`0x436abc` per
   message. The blink read of `0x436cec` (`0x436f27`) is never reached in
   the 40 suites; the radio panel and the options dialogs add reads
   outside the frame walk ([the lower counts](#the-lower-counts-the-extra-render-root-and-the-radio-panels-second-caller)).
4. The heap allocations of `0x433770` (first frame) and `0x432bc8`/F11
   (dialog), if the allocator's state is ever compared.
5. The view-transform scratch: `0x43b808` and `0x43b978` rebuild it with
   `0x48c444(0)` and restore it (open item 3 of [audio.md](audio.md)).

### Notes for other documents

- [input.md](input.md): HUD key codes `0x13`/`0x1c`, not `0x22`/`0x2b`;
  `0x41b468` is R, not G.
- [mission_runtime.md](mission_runtime.md): the two classes constructed
  into `[H+0x207]` are `CommandPort` and `DialogPort`, chosen by word
  `0x4aa728` ([Objects](#objects)).
- [player_movement.md](player_movement.md): the side effects of `0x434010`
  and `0x434144` are the clock (both) and one stream-B draw (`0x434010`,
  only when no jolt is pending and the display mode is not 4), plus
  presentation globals.
- The `ShieldsGauge` `[H+0x1e9]` (`0x443918`), the shield-balance write
  `0x413bc8`/`0x413af8` from `0x41b130`, the HDD map pick `0x44d888` and
  the queue insert `0x434e8c` (with two original defects) are classified in
  [cockpit.md](cockpit.md).
- The display-mode manager (`0x42a3f4`, `0x42a4c4`, `0x429e60`,
  `0x42a9c0`), the screen jolt `0x42d2f8`/`0x42d4a8`/`0x42d624` behind
  `0x434010`/`0x43408c`, the panel scroll `0x43271c`/`0x433968` and the
  mission-start sequence `0x45d85c` are classified in
  [display_modes.md](display_modes.md).

## Cockpit target cycling as own code

Own code: `core/src/cockpit_target.c`, adapter
`tools/reference-game/proxy/cockpit_target_hooks.h` (group
`cockpit_target`). Listing:
`<es2-data>\logs\h4-20-20261009\target-433174-433770.asm`.

| Hook | VA | Role | Live (a60/c1s/c1c/c3_09/c4_08/c3_10) |
|---|---|---|---|
| `cockpit_valid` | `0x433174` | target valid (pure) | 0/1000/0/1000/692/0 |
| `cockpit_arc` | `0x433250` | target in the forward arc (pure) | 0/10/0/79/14/0 |
| `cockpit_next` | `0x43349c` | next target (writes `[H+0x210]`, the list `[0x4d040c]` and the buckets `[0x4d0410]`) | 0/4/0/17/5/0 |
| `cockpit_nearest` | `0x4333c8` | nearest target | never entered: in vitro only |
| `cockpit_select` | `0x4332dc` | select/clear a target | never entered: in vitro only |

**Callers and the live path.** `0x43349c` has four call sites: the HUD key
handler `0x432bc8` (Enter, `0x432e93`), `0x4332dc` (`0x433304`), the input
dispatcher `0x460870` (`0x460de0`) and the eight-instruction wrapper
`0x446fb0` (`[0x49b088]` → `0x43349c`), which the MFD button handler
`0x446844` calls for buttons 7 and 9 when the MFD mode `+0xc1` is nonzero
(`0x4468ee..0x446906`). The map counts `0x446fb0` and `0x43349c` alike, so
every live call came through the MFD button, which the HUD key handler
presses on Enter in MFD modes 3/4. `0x433174` runs every rendered frame
from the panel frame `0x4327ac` while a target exists (`0x4328a5`);
`0x433250` only from `0x43349c` (and the never-entered `0x4332dc`).
`0x4333c8` is called from the cheat dispatcher `0x45feb8` (`0x460819`) and
the input dispatcher (`0x460e92`); `0x4332dc` from the HUD key `;`
(`0x432ea1`) and the dead HUD path `0x43d7d2`.

**Common facts.** All five are cdecl with one or two dword slots and no
uninitialized local reads, so no entry shim. H = the cockpit object, P =
`[H+0x203]`, S = the view subject `[0x4d2684]` when dword `0x49ef34` is
nonzero, else P. Actor list: dword `0x4a9af8`, count word `0x4a9afe`. Two
tiny callees are pure and computed in core: `0x426aec` (EAX ← 30000, plus
30000 unless word `0x4a9e5e` = 1; touches no other register) and
`0x43dbb0` (EAX ← argument + 0xb1). Every other call goes through the guest
bridge at the original address with the registers the original holds
there: `dist_gate` `0x49288c`, `heading_point` `0x492934`, `angle_ratio`
`0x47d32c`, `multiply_q10` `0x47e0b0`, the vehicle virtuals `+0x3c` (P)
and `+0x10` (T), the HUD record update `0x43d9b4` and, between the five,
`0x433174`, `0x433250` and `0x43349c` (so each keeps its own hook mode).
None of the five draws, calls the clock or draws RNG stream B itself;
`0x43d9b4` is the only presentation callee and keeps its place in the call
order. Result EDX:EAX is exact; ECX at return is outside the contract:
every direct caller writes ECX before reading it, except that `0x4332dc`
forwards the ECX of `0x433174` into `0x433250` and `0x43349c` forwards the
ECX of `0x433250` into `heading_point`, whose original never reads its
entry ECX (`0x492934` and `0x49290c` take only stack arguments).

**`0x433174(H, T)` → AL.** `dist_gate(S+0x26, T+0x26)` with EAX = S+0x26
and the entry EDX/ECX → d. r = dword `0x4d1c78`, R = `0x426aec()`. Then, in
this order (EDX at return in brackets): byte `T+0xa4` set → 0 [T+0x92];
byte `T+0x99` set → 0 [T+0x92]; the side bytes `[[T+0x45]+0x12]` and
`[[S+0x45]+0x12]` (both sign-extended, compared as words) equal → 0 [S's
side]; byte `T+0x95` set and d < r (signed) → 1 [T+0x92]; i = `movsx` word
`T+0x4b`: byte `S+0xc2+i` zero → 0 [i]; else d < R → 1, otherwise 0 [i].
EAX is the full 0/1.

**`0x433250(H, T)` → AL.** View subject (`0x49ef34` nonzero):
`heading_point(T+0x26, V+0x26)` with EAX = V+0x26 and the entry EDX/ECX,
then AX −= word `[[0x4d2684]+0x10]` (the pointer read again). Otherwise:
`heading_point(T+0x26, P+0x26)` with EAX = P+0x26, EDX = T+0x26 and the
entry ECX; x = EAX with AX −= word `P+0x10` (P read before the call);
then P' = `[H+0x203]` (read again), virtual `+0x3c(P')` (twist, MECH
`0x415488`) with EAX = P''s table, EDX = heading_point's EDX, ECX = P';
AX ← its AX + x's low word (high word from the virtual's EAX). Result EAX
= EDX = 1 iff −9000 < AX < 9000 (signed words), else 0.

**`0x43349c(H)` → `[H+0x210]`.** (1) old head = `[[0x4d040c]]`, old
target = `[H+0x210]`. (2) For each of the four buckets at `[0x4d0410]` (12
bytes: count, actor array pointer, distance array pointer): count ← 0,
then the four distances ← 0x7ffffff, the distance pointer read again for
every write; then the four list entries `[0x4d040c][j]` ← 0, the list
pointer read again each time. (3) Actor loop over the list `0x4a9af8` with
a word counter compared (signed words) against word `0x4a9afe`, read each
time; a null entry ends it. Per actor T: `0x433174(H, T)` with EAX = T,
EDX = H, ECX = the list slot's address; AL = 0 → next. `0x433250(H, T)`
with EAX = H, EDX as `0x433174` left it, ECX = T; AL = 0 → next. P =
`[H+0x203]`; `heading_point(T+0x26, P+0x26)` with EAX = H, EDX = P+0x26,
ECX as left by `0x433250`; x = EAX with AX −= word `P+0x10`. P' =
`[H+0x203]`: virtual `+0x3c(P')` with EAX = P', EDX = its table, ECX = H;
b = x with BX ← x's BX + the virtual's AX. `dist_gate([H+0x203]+0x26,
T+0x26)` (P read a third time) with EAX = H, EDX = the first argument, ECX
= b → d (dword). |b| as a word: 0x8000 → CX ← 0x7fff in **dist_gate's
ECX**; negative → b with CX negated; else b. T's virtual `+0x10(T)` (MECH
`0x46b910`: shape radius, hypothesis) with EAX = T, EDX = T's table, ECX =
|b|. `angle_ratio(d, movsx AX)` with EAX/EDX as the virtual left them and
ECX = T; a = its AX zero-extended. `multiply_q10(0, a)` with EAX = EDX = a
and angle_ratio's ECX. b' = |b| − AX as a word, negative → 0; bucket k =
min(b' >> 10, 3). Insertion into bucket k: the first position c (0..3)
whose stored distance is greater than d (signed); none → not kept. m =
count − 1 clamped to at most 2 (signed); entries m … c move up by one
(actor and distance arrays, pointers read again per move); actor[c] ← T,
distance[c] ← d, count ← m + 2. (4) The new list: buckets 0..3 in order,
entries 0 … count−1 of each (count read again per comparison), at most
four; the rest of the list ← 0. (5) Choice: c = list[0]; if c is the old
head or the old target and `[H+0x210]` ≠ 0, the first i in 0..2 with
list[i] = `[H+0x210]` (read again per i) and list[i+1] ≠ 0 gives c =
list[i+1]. (6) If c ≠ old target (also when c is 0 because no actor
qualified, which clears the target): `[H+0x210]` ← c, record =
`[H+0x1f5]`+0xb1, record `+6` ← `[H+0x210]`, byte `+0x24` ← 1,
`0x43d9b4([H+0x1f5], record)` (pointer read again) with EAX = H, EDX = the
HUD object, ECX = the target. Result EAX = `[H+0x210]`; EDX = `0x43d9b4`'s
EDX after an update, else c. The list `[0x4d040c]` persists between calls
(blocks from `0x4774f0` stored at `0x431054`/`0x431063` during cockpit
construction; no other code references `0x4d040c`/`0x4d0410`).

**`0x4333c8(H)`** (nearest). Word counter loop like (3) above; EAX's low
word ← the counter, then ← word `T+0x1a8` (high word kept); only types 0
and 2 are considered: `0x433174(H, T)` with EDX = the slot address and ECX
as left by the previous call (the entry ECX at first); AL ≠ 0 →
`dist_gate([H+0x203]+0x26, T+0x26)` with EDX = T+0x26, ECX = P+0x26; the
smallest signed distance below 0x7fffffff wins (the first one on ties). A
winner ≠ `[H+0x210]` is stored and the HUD record is updated as in (6),
the call with EAX = record, EDX = target, ECX = the HUD object. Result EAX
= winner or 0; EDX = `0x43d9b4`'s after an update, else as left by the
loop.

**`0x4332dc(H, T)`** (select). T = 0: `[H+0x210]` ← 0 (EAX = 0) and the
record update below. Otherwise: no target → `0x43349c(H)` with the entry
registers (ECX = H afterwards). start = `[H+0x210]` (EDX = start). Loop:
cur = `[H+0x210]`; T = cur → `0x433174(H, cur)`, AL ≠ 0 → `0x433250(H, T)`,
AL ≠ 0 → leave. Else the cycle cursor: EAX = H+0x214, DX ← word
`H+0x218`, the word incremented; old index < word `0x4a9afe` (signed
words) → ECX = `[H+0x214]`, that dword += 4, next = `[ECX]`, else 0;
`[H+0x210]` ← next; a zero `[H+0x210]` (read again) resets the cursor
(word ← 0, dword ← `[0x4a9af8]` through EDX) with EAX = H+0x214; ECX =
`[H+0x210]`; leave when it equals start. After the loop (or for T = 0): if
T = `[H+0x210]`, the HUD record update as in (6) with EAX = record, EDX =
target, ECX = the HUD object, result AL = 1 over `0x43d9b4`'s EAX and its
EDX; else EAX = 0 and EDX as left. **Precondition** (the original loops
forever otherwise): start is 0 or is reached by the cursor walk, i.e. it
appears before the first null entry of the list within the count, or
between the current cursor and the end of the walk.

**Edges.** Every conditional branch of the five is reachable both ways
with suitable memory; the in-vitro generator reaches all of them
(`tools/reference-game/invitro/cockpit_target_cases.h`).

## HUD-region helpers

Own code: `0x431410` behind the hook `panel_visible` in the adapter group
`hud_helpers` (`core/src/hud_helpers.c`,
`tools/reference-game/proxy/hud_helpers_hooks.h`). Evidence (listing,
scanner, liveness output): `<es2-data>\logs\h4-41-20261010`.

### Classification re-checked

| VA | Class | From the bytes | Decision |
|---|---|---|---|
| `0x431410` | P | locals only; reads `[0x49b088]`, `H+0x20f`, `[H+4]+0x210..+0x227`, the dwords `0x4d253e`, `0x4d2542`, `0x4d2556`, `0x4d255a` and the argument rect; no call, no global or object write | own code (`panel_visible`) |
| `0x433b20` | U | writes the cursor zone `H+0x22e` (0, 3, 4, 5, 6), read only by the cursor image draw `0x433c54` (`0x433c67`); calls the getters `0x45b750` (EAX = `0x4d2306`, the tape cursor) and `0x429820` (EAX = `[0x4cf99c]`); compares the cursor against the three `ScrollTrigger` rectangles `[H+0x21a+4i]` | presentation state, not P |
| `0x43dcd4` | U+C | one clock call per entry, writes `0x4d1804` and the `MM:SS` digits `0x4d17dc..0x4d17e0`, rebuilds the text `0x4d17e4` with the inline strcpy and two calls of the RTL `strcat` `0x4967ea` | presentation: clock and text side effects |
| `0x439758` | D | LED segment rows: draws through `0x483a04` (two loops, odd and even columns) with the colours `arg+0x2c`/`+0x30`, temporarily replacing `[canvas+0x228]` of `[[0x6c5ed0]+0x44]` (restored at `0x439824`) | presentation: draws |
| `0x440d74` | U+D (outside the region) | slot `+0x10` of the `EnergyWeaponGauge` vtable `0x49c91c` (RTTI type descriptor `0x4430eb`; no direct caller): copies record fields into gauge fields `+0xc1`, `+0xc2`, `+0xc6`, `+0xd3`, `+0xd6..+0xd8`, `+0xdb`, `[+0x1c]+0x7a` and the words `+0x1b` of the two sub-gadgets `[+0x18]`/`[+0x1c]`, calls the sub-gadgets' virtuals `+0x14` and `+4` and the jolt offset getter `0x42d870` (first call sets byte `0x49add5`) | presentation |
| `0x45a4cc` | input (outside the region) | called only by the control-record builder `0x45a810`; writes the 18-byte block at its first argument from the key-state arrays `0x4d2394`/`0x4d23a3` and the table `0x49eb3d`/`0x49eb45`, sets `0x4d23b2[i]` ← 1 for every held key and clears `0x4d2394[i]`/`0x4d23b2[i]` where `0x4d23a3[i]` is set (`0x45a5c5`); its second argument is only tested (`0x45a510`, result unused) | simulation input; belongs with `0x45a810` ([input.md](input.md)) |

### Contract of `0x431410` (rectangle visible in the panel view)

cdecl, one argument: `rect` (four dwords x1, y1, x2, y2). H = `[0x49b088]`.

1. If byte `H+0x20f` (panel off) is nonzero: EAX = 0 (`0x43142c`), EDX
   unchanged from the entry (no instruction on this path writes it).
2. Otherwise, with C = `[H+4]` (the canvas): the view is
   x1 = `[C+0x210]` + `[C+0x220]`, y1 = `[C+0x214]` + `[C+0x224]`,
   x2 = x1 + `[0x4d253e]`, y2 = y1 + `[0x4d2542]` (the clip rect's
   `+0x218`/`+0x21c` are copied into the local and overwritten, so they are
   read but unused). The argument rect is moved by
   (`[0x4d2556]`, `[0x4d255a]`): rx1 = x1 − dx, ry1 = y1 − dy, rx2 = x2 − dx,
   ry2 = y2 − dy (`0x4d2556` is read twice, `0x43149b` and `0x4314aa`).
   All sums wrap at 32 bits; all tests are signed.
3. Corner (rx1, ry1) inside the view, edges inclusive (`jl`/`jg`/`jl`/`jle`
   at `0x4314bc`, `0x4314c1`, `0x4314c7`, `0x4314ce`, in the order y low,
   y high, x low, x high): EAX = 1 with EDX = `[0x4d2556]`.
4. Otherwise corner (rx2, ry2), same order (`0x4314d6`, `0x4314db`,
   `0x4314e2`, `0x4314ea`): EDX = ry2 (`0x4314d0`); after the y tests EDX
   = rx2 (`0x4314e4`) only when rx2 ≥ x1; EAX = 1 inside, else 0.

So EAX ∈ {0, 1} and EDX is one of: entry EDX, `[0x4d2556]`, ry2, rx2. ECX
at return is EAX on step 3/4 and `EBP−0x10` (a stack address) on step 1;
the flags are those of the last compare (step 1: `xor`). No uninitialized
read, no stack residue read, no call.

**Register liveness at the callers (confirmed).** The 38 call sites in 34
functions (all draw functions: panel text `0x43122c`, the MFD and gauge
draws `0x440cb8`…`0x45216c`, the HUD `0x43d5f0`/`0x43d704`, the
EventPort blink `0x436cec`), walked forward from the return address over
both edges of every conditional jump: every site continues with `pop ecx`
and `test al, al`, so ECX and the flags are written before any read or
call. EDX is not written before a read at several sites, so the hook
returns EDX exactly; it takes the entry EDX through the register context
for step 1 (`liveness-431410.txt` in the evidence folder).

**Hook.** `panel_visible`, cdecl (rect), `ES2_RESULT_PAIR` through the
context replacement (EDX:EAX exact, entry EDX for step 1), compare mode
refused (no spans or regions are declared). Prologue
`55 8b ec 83 c4 e0 a1 88 b0 49 00` (11 bytes), relocated 6 bytes (1, 2, 3).
The function is presentation-only: its result decides only whether a
gauge or text is drawn.

## Panel weapon selection as own code

The weapon selection that a panel element (MFD button gadget or HUD weapon
key) triggers: `0x432a50` → `0x4106ac` → `0x410708`. Own code:
`core/src/panel_weapons.c` (`es2_panel_weapon_registers`,
`es2_weapon_find_registers`, `es2_weapon_select_registers`, declared in
`es2/core/panel_weapons.h`; each returns EDX:EAX and stores ECX), hooks
`panel_weapon`, `weapon_find` and `weapon_select` in the adapter group
`panel_weapons` (`tools/reference-game/proxy/panel_weapons_hooks.h`). W is
the weapon system (`[P+202h]`, [weapons.md](weapons.md)): dword W+4 the
slot list, word W+8 the slot count, byte W+18h, signed byte W+1Dh the
selected slot.

### `0x410708` weapon_select(W, slot)

cdecl, two slots, 26 instructions, no call, no entry register read, no
local. EAX = W, EDX = slot (the whole dword), ECX = s16 slot.

1. s16 slot = s8 `[W+1Dh]` (32-bit compare of the two sign extensions):
   return at once, nothing written. EAX = W, EDX = slot, ECX = s16 slot.
2. E = `[[W+4] + 4·s16 slot]` (ECX = E). If byte `[E+4Bh]` ≠ 0:
   ECX = s8 `[[E+18h]+16h]`; when that is negative (16-bit `test cx,cx`)
   the low word of EDX gets it added (`add dx,cx`, high word kept).
3. Byte W+1Dh ← DL, word `0x4d2528` ← 0 (both after every read).

Result: EAX = W, EDX = the (adjusted) slot, ECX = E, or the sign-extended
definition byte when E+4Bh ≠ 0. A null E faults in the original (never in
the callers' domain).

Callers: 4106e3 (`0x4106ac`), 4107d3 (`select_step` 41074c, then POP ECX),
410ad5 (`0x410a3c`, then POP ECX) and 410c6d (`0x410b40`): there ECX and
EDX go on into `0x410970` and its weapon virtual +2Ch (the 498dcc body
copies ECX's high word into EDX, [weapons.md](weapons.md)). ECX is
therefore returned exactly (exit shim below). 410708, 4106ac and 432a50
read no uninitialized stack (no locals; 4106ac and 432a50 only save
registers).

### `0x4106ac` weapon_find(W, elem)

cdecl, two slots, 41 instructions. EBX = i = 0 (the whole register),
ESI = W. Loop while s16 i < s16 word W+8 (the word is reread every round):

1. EAX = s16 i, EDX = `[W+4]` (reread), E = `[EDX + 4·s16 i]`; E = 0: next.
2. `E.V58(E, elem)` (vtable `[E]`, slot +58h) with EAX = the vtable,
   EDX = the list pointer, ECX = elem. AX = 0 (16-bit test): next.
3. EAX = E+43h. Byte `[E+4Ch]` = 0: return EAX = 0 at once (the search
   stops at the first match even when it cannot be selected).
4. `0x410708(W, i)` (the whole EBX) with EAX = E+43h and V58's EDX/ECX;
   then byte W+18h ← 1, AX ← 1: EAX = ((W+14h) & FFFF0000h) | 1.

No match: EAX = 0. EDX at return is the last value written: `[W+4]` when
the last visited slot was null, else the last V58 call's EDX, or the entry
EDX when no slot was visited (count ≤ 0). ECX is the last V58 call's, or
the entry ECX when none was called. After a selection EDX and ECX are
410708's. The weapon V58 bodies (vtables 498a84 … 4992c0) are 40df00
(`xor eax,eax`) and 40df08/40df1c (EAX = (elem = `[E+77h]`), EDX = elem,
ECX kept); the search stays a guest call. i stays in 0..7FFFh, so the
pushed EBX equals s16 i.

### `0x432a50` panel_weapon(H, elem)

cdecl, two slots, 21 instructions. P = `[H+203h]`, W = `[P+202h]`;
`0x4106ac(W, elem)` with EAX = P, EDX = W, ECX = the entry ECX. Then
AX ≠ 0: AL ← 1 (EAX = (4106ac's EAX & FFFFFF00h) | 1), else EAX = 0.
EDX and ECX are 4106ac's. Callers: 432cb9 (HUD key handler `0x432bc8`;
ECX goes on into `0x438d9c`), 440f68 (`0x440f18`) and 44152b (`0x4414dc`),
the MFD gadget methods whose EAX/EDX/ECX escape through their returns
([weapons.md](weapons.md)). All three registers are returned exactly.

### Hooks

All three cdecl, `ES2_RESULT_PAIR` through the context replacement,
compare mode refused (weapon_find and panel_weapon call game code; the
exit shims make ECX part of the contract). Prologues
`55 8b ec 53 56 8b 55 0c` (410708), `55 8b ec 53 56 57 8b 75 08`
(4106ac), `55 8b ec 53 56 8b 5d 08` (432a50); 5 bytes relocated
(1, 2, 1, 1) each, no branch into them. Game calls go through the weapons
guest bridge and callee observer with the original's entry registers:
panel_weapon calls 4106ac (`call_4106ac`), weapon_find the weapon virtual
+58h (`slot_58`) and 410708 (`call_410708`), so the nested hooks keep
their own modes. **Exit shims:** the framework returns the entry ECX, so
each hook's entry jump goes to a shim that calls the framework's dispatch
with the two arguments copied and, when the replacement ran, loads the ECX
the core computed; EAX and EDX pass through. Chained as
`panel_weapons_capture_entry` after `sfx_capture_entry`. No game
instruction is embedded.

**Branch sites (8):** 410708: 41071c, 41072b, 410737; 4106ac: 4106c4,
4106d6, 4106df, 4106fd; 432a50: 432a74. All reachable both ways.

Validated live: `oracle-parallel-391hooks-20261010T223840Z` (all hooks
replaced, exit shims active), all six suites identical.

## HUD key handler as own code

The HUD key handler `0x432bc8` and the two button helpers below it. Own
code: `core/src/hud_keys.c`, hooks `hud_key`, `button_press` and
`button_release` in the adapter group `hud_keys`
(`tools/reference-game/proxy/hud_keys_hooks.h`).

| Hook | VA | Role | Live (a60/c1s/c1c/c3_09/c4_08/c3_10) |
|---|---|---|---|
| `hud_key` | `0x432bc8` | HUD key handler (417) | 1000/1000/1000/1000/1000/937 |
| `button_press` | `0x438d9c` | press a widget button (27) | 0/4/0/41/14/0 |
| `button_release` | `0x4530bc` | schedule a button release (19) | 0/12/0/42/14/0 |

### The split: compared state versus H3 routing

`0x432bc8` is a jump/compare chain over the low word of the key code that
**changes no compared simulation state of its own**. Every write to
simulation state happens inside a callee:

- **Compared (hooks, through the bridge):** `panel_weapon` `0x432a50`
  (weapon keys 1–0, writes the selected weapon `W+0x1d`), `cockpit_next`
  `0x43349c` (Enter, next target `[H+0x210]`), `cockpit_select` `0x4332dc`
  (`;`, clears the target). These keep their own hook modes.
- **Compared (guest calls):** the Tab lock `0x40e4ac` (`W+0x7d` through
  the target virtual `+0x80`), the R toggle `0x41b468` (`P+0x96`, EventPort
  messages), and — through `button_press`/`button_release` — the
  intra-frame virtual clock `0x467828`.
- **H3 routing (guest calls):** the display requests `0x42a3f4`, the F11
  modal dialog (`0x47749c`, `0x457538`, virtual `+8`, `0x4543b0`), the MFD
  forward `0x446c38`, the nav marker `0x434974`, the getter `0x429820`, and
  the eleven panel gadget virtual slots (`+0xc` offers for `[H+0x1d8]`,
  `+0x1e1`, `+0x1e5`, `+0x1e9`, `+0x1f1`, `+0x1f5`, `+0x1ed`, `+0x1f9`; the
  Tab `[H+0x1f9]+0xc`; the F11 dialog `+8`). They stay original behind the
  guest bridge.

The dispatch maps the low word of the code exactly as the compare chain at
`0x432c03..0x432c84` does ([key table](#the-hud-key-handler-0x432bc8));
everything not in the table → the default that offers the code to the MFD
`0x446c38` and then to the panel gadgets. The mode-5/6 Esc and F1–F6
request slots are read uninitialised in the original; the request value
reaches `0x42a3f4`, whose table changes nothing while the current mode is
5 or 6. Below the MFD forward, the group order `0x4231a4` reads its
priority word uninitialised for Alt+O (codes 3, 6, 9–0Eh); this residue
reaches compared state, which is why the hook runs at the original stack
coordinate ([input.md](input.md), "stack residue under the command
handler"). The entry EAX/EDX/ECX at the callees are the listing's (EAX at
`0x446c38` is the compare chain's remainder, not the code).

### The exception frame and the stack coordinate

`0x432bc8` sets up a Borland SEH frame: `0x49630c` (descriptor `0x49b571`)
links a record at `EBP-0x28` into `FS:0` and every exit restores `FS:0`
from the record's saved head `[EBP-0x28]` (also returned in EDX). The F11
try-state word is at `EBP-0x18` (record `+0x10`). The hook has the entry
shim `hud_keys_hud_key_entry` (installed by `commands_capture_entry`): the
core runs at the original stack coordinate with f = the original EBP,
`0x49630c` links the record at `EBP-0x28`, the caller's EBX/ESI/EDI at
`EBP-0x34`/`-0x38`/`-0x3C`, outgoing ESP `EBP-0x3C`, and the callees see
the original's frame, registers and return addresses. Direct and legacy
calls use the synthetic EBP `f = g->scratch + ES2_HUD_KEYS_FRAME`. The
adapter writes `FS:0 = *fs_head` on exit. The heap allocation `0x47749c`
and the dialog stay guest calls.

### Register contract and the caller audit

All three are cdecl and return the original's EDX:EAX. **ECX is dead at
every caller**, so there is no exit shim: `0x45feb8` reads only AL of
`0x432bc8` (`0x45fefc mov bl,al`) and `player_input` discards its result
(`jmp` to the button-loop tail); the callers of `0x438d9c`
(`vehicle_keys 0x4157c8`, the MFD handlers `0x4469e8`/`0x446xxx`,
`0x43ec08`, …) each `add esp,0xc` and then `jmp`/`pop` or overwrite the
registers before reading them; the callers of `0x4530bc` (`0x438d9c`,
`0x454de8`, `0x458fb8`) ignore the result. EAX of `0x432bc8` is `AL` (1
handled, 0 not) with the original's high bytes tracked through the
dispatch so the `mov al,1` exits match; EDX is the saved `FS:0` head.
`0x438d9c` returns the button virtual's / 0x4530bc's registers on the
active path, the sign-extended state word on the skip; `0x4530bc` returns
`clock()+0xa` in EAX and the clock call's EDX/ECX.

**Trap (original defect):** `0x4530bc` compares the pending-release count
word `+0x396` with 7 but has no conditional jump, so a ninth pending press
writes past the eight-entry array; the core reproduces the write exactly
(no bound check; also noted in [cockpit.md](cockpit.md)).

### Hooks

All three cdecl, `ES2_RESULT_PAIR` through the context replacement, compare
mode refused (game calls, the F11 allocation and the clock). Prologues
`55 8b ec 83 c4 d0 53 56 57 8b 7d 0c 8b 5d 08` (432bc8, 6 bytes relocated,
1/2/3), `55 8b ec 53 56 8b 75 0c 8b 5d 08` (438d9c, 5 bytes, 1/2/1/1),
`55 8b ec 53 8b 45 08 66 83 b8 96 03 00 00 07` (4530bc, 7 bytes, 1/2/1/3);
no branch into any relocated prologue. `button_press` calls
`button_release` and `0x432bc8` calls `panel_weapon`, `cockpit_next`,
`cockpit_select`, `button_press` through the bridge, so the nested hooks
keep their modes.

**Branch sites (56):** hud_key 55 (`0x432be8` … `0x433100`), button_press 1
(`0x438db2`); button_release has none. All reachable both ways except the
two F11 null-allocation edges `0x432f06` and `0x432f43`, which are
unreachable (the null path faults in the original at `0x432f36`;
`0x47749c(0x3b8)` always succeeds in the suites).

Validated live: `check-set3-20261011T005925Z`, all six suites identical.

## Cockpit panel frame as own code

`0x4327ac(H)` (107 instructions, cdecl, one slot; only caller the
rendering frame `0x45fca8` at `0x45fd4f`, no pointer to it in the image).
Own code: `core/src/panel_frame.c`, hook `panel_frame` (adapter group
`panel_frame`). Its own effects are two writes to the cockpit object:
`H+0x23e` ← 1 after the first scroll-zone setup and the target clear
`H+0x210` ← 0. Everything else is calls that stay original code (draws)
or are hooks (`cockpit_valid`).

### Sequence

1. `cmp byte [0x49b104], 0` (read before the argument): nonzero → return
   at once with the entry EAX/EDX/ECX. The byte is set only by the view
   code around its extra rendering call (`0x45d3e9` 1, `0x45fca8` and
   `0x45fba4` at `0x45d3f0`/`0x45d3f5`, `0x45d40d` 0): that rendering
   skips the panel. EBX ← H.
2. Byte `H+0x23e` zero: `0x433770(H)` (create the three scroll zones),
   then `H+0x23e` ← 1 (no re-read).
3. Byte `H+0x245` (read after step 2) zero: `0x432924(H)` (startup
   sequence; it sets `H+0x245` itself past 200 ticks).
4. `0x43408c(H)` (jolt), `0x434194(H)` (bump).
5. Byte `H+0x20f` (read after `0x434194`) nonzero (panel off): the
   virtual `+8` of `[H+0x1f9]` (object and vtable read now) and return
   with its EAX/EDX.
6. Otherwise the port virtual `+0x14` of `[H+0x207]` (ECX = the port,
   EAX = its vtable), the context push `0x4311e0()`, then ten times
   (EDI = i = 0..9, ESI = `H+0x70+4i`): element `[ESI]` read fresh; nonzero
   → its virtual `+8` (EAX = element, EDX = vtable). Then the virtual `+8`
   of `[H+0x1e1]`, `[H+0x1e5]`, `[H+0x1e9]`, `[H+0x1f1]`, `[H+0x1ed]`,
   `[H+0x1f9]` in that order, each object and vtable read just before its
   call (registers: ECX/EAX, EDX/ECX, EAX/EDX, ECX/EAX, EDX/ECX, EAX/EDX
   for object/vtable), no null check; the context pop `0x431210()`; the
   cursor `0x433c54(H)`.
7. T = `[H+0x210]` (read once). T nonzero: `cockpit_valid(H, T)`
   (`0x433174`, hook); AL zero → `H+0x210` ← 0 with EDX ← 0 (the only
   simulation write; T is not re-read).
8. `0x4530fc([H])` (button releases, `[H]` read now), `0x4349ac()` (nav
   marker). EDX:EAX at return are `0x4349ac`'s.

ECX: every one-slot call is followed by `pop ecx`, so ECX at the next call
is the argument slot as the callee left it; the loop virtuals, `0x4311e0`
and `0x4349ac` are entered with that ECX. The checked callees (`0x433770`,
`0x432924`, `0x43408c`, `0x434194`, `0x433c54`, `0x4530fc`, `0x433174`)
never write their argument slot, and in the whole image only `0x4307b0`,
`0x430d08`, `0x477d48`, a function at `0x48b9xx` and `0x49656c` write
`[ebp+8]` (none is a panel slot `+8`); `0x4311e0`, `0x431210`, `0x433c54`,
`0x4349ac` and `0x4530fc` write ECX before reading it. ECX at return is
dead: the caller pops it and `0x42841c` writes ECX, EAX and EDX before
reading them.

**Stack residue (trap).** The callees read uninitialized stack: the
EventPort messages of `0x432924` (only the word at `EBP−20h` written, the
rest of the record is residue) and `0x4349ac` (bytes 0..5 of a 18h-byte
local written) are copied by the EventPort receive `0x436a3c` →
`0x434e8c` (message bytes 2..15h) into the port's queue; the eleven panel
virtuals are a large draw tree that was not audited. Because residue under
a replaced frame can reach the simulation ([input.md](input.md)), the
replacement runs its game calls at the original stack coordinate (entry
shim, below): the callees see the original EBP (`T−4` for entry ESP `T`),
EBX/ESI/EDI, return addresses, argument slots and therefore the same
residue. At the original coordinate the core re-reads the argument slot
after every one-slot call (`pop ecx`), so a callee that changes its slot
is followed exactly; with the plain bridge (direct calls) ECX after a call
is the pushed argument.

### Hook

`panel_frame`, cdecl, one slot, `ES2_RESULT_PAIR` through the context
replacement (EDX:EAX exact, ECX the framework's entry ECX), compare mode
refused (game calls, draws). Prologue `55 8b ec 53 56 57 80 3d 04 b1 49 00
00 8b 5d 08`, 5 bytes relocated (1/2/1/1); no branch into it. Entry shim
`panel_frame_entry` (chained from `weapon_slots_capture_entry`): PUSH EBP;
the caller's EBX/ESI/EDI to `EBP−4/−8/−0Ch` (where the prologue pushes
them); 48 guard-probed pages (192 KiB) for the panel draw tree, the nested
hooks and the host frames; {EBP, outgoing ESP = `EBP−0Ch`} below the gap;
the slot copied for the framework's dispatch. Every game call (direct,
virtual, `cockpit_valid`) goes through the stack bridge of
`ai_behaviour_hooks.h` with EBX = H, the caller's ESI/EDI up to `0x4311e0`
and the loop's ESI/EDI after it, the original's EBP and return address.
Callee counters `hk_panel_frame_call_*` for the ten direct callees and
`slot_8`, `slot_14` for the virtuals.

**Branch sites (8):** `0x4327bc`, `0x4327c9`, `0x4327e0`, `0x4327fe`,
`0x43282d`, `0x43283d`, `0x4328a1`, `0x4328af`, all both ways in vitro.
The panel-off path (`0x431500`) is live only in c3_09/c4_08/c3_10.

Validated live: `check-r6-set1` (all hooks replaced), all sixteen suites
identical.

## Cockpit↔player synchronisation as own code

`0x41b130` and its three small callees. Own code: `core/src/cockpit_sync.c`
(`es2/core/cockpit_sync.h`), hooks in the adapter group `cockpit_sync`
(`tools/reference-game/proxy/cockpit_sync_hooks.h`). Class names follow
the Borland RTTI rule of the [legend](#legend).

| Hook | VA | Role | Map probes a60/c1s/c1c/c3_09/c4_08/c3_10 |
|---|---|---|---|
| `cockpit_sync` | `0x41b130` | cockpit↔player synchronisation (230 instructions) | 1000×5/937 |
| `throttle_record` | `0x447df8` | address of the `ThrottleGauge` record `G+0xb1` (6) | 1000×5/937 |
| `throttle_gauge` | `0x447da8` | `ThrottleGauge` record set, slider move (29) | 1000×5/937 |
| `hud_record` | `0x43dbb0` | address of the `HUD` record `G+0xb1` (6) | 1000×5/937 |

**Callers and references (confirmed).** `0x41b130` is called at
`0x45fdbf` (rendering frame `0x45fca8`, after the panel frame) and at
`0x461d02` (mission start `0x461608`, before the camera `0x4011a0`); both
pass P = `[X+0x2a]` (the player vehicle) and `pop ecx` right after, and
`0x45fdc5`/`0x461d08` write ECX, `0x45fdcb`/`0x45fdd2` EAX and EDX before
reading them; at `0x461d0c` EAX/EDX reach `camera_update` (a hook whose
original is entered with them), so the replacement returns EDX:EAX exactly.
`0x447df8` and `0x447da8` are called only at `0x41b1e4` and `0x41b280`;
`0x43dbb0` also from the three target hooks `0x4332dc`, `0x4333c8`,
`0x43349c`, whose cores compute it inline. None of the four addresses
occurs as a literal dword in the file (no vtable or pointer use).

### Objects

P = the vehicle (`[P+0x202]` = W the weapon system, `[P+0x1f2]` = C the
chassis record, `[P+0x1a4]` the target, `[P+0x30b]` a weapon object of the
player, words `P+0x10` heading, `P+0x28e` speed, `P+0x290` throttle set
point, `P+0x292` energy pool, `P+0x298` torso twist, `P+0x29a` torso pitch,
bytes `P+0x93`, `P+0x9b`, `P+0x9d`, `P+0xa4`, the countdown at `P+0x31b`
whose word is `P+0x31c`); H = `[0x49b088]` the cockpit: `[H+0x1e5]`
`EnergyPoolGauge` (vtable `0x49cabc`), `[H+0x1f1]` `ThrottleGauge`
(`0x49d1e0`), `[H+0x1f5]` `HUD` (`0x49c138`), `[H+0x210]` the cockpit
target. A target T keeps the number of vehicles targeting it in word
`T+0x1a2`. The throttle record `[H+0x1f1]+0xb1` holds two dwords: the speed
ratio (`+0xb1`) and the set point (`+0xb5`); the HUD record
`[H+0x1f5]+0xb1` is 0x26 bytes (words `+0`, `+2`, `+4` heading, twist,
pitch; dword `+6` target; byte `+0x24` flag, byte `+0x25`).

### `0x41b130(P)`

cdecl, one slot; `push ebp`/`mov ebp,esp`/`add esp,-40h`/`push ebx`/`push
esi`, so EBX is saved at EBP−44h, ESI at EBP−48h, the outgoing ESP is
EBP−48h; EDI is never written. Locals: the aim point A (12 bytes,
EBP−40h), the HUD record copy R (26h bytes, EBP−34h), the words a
(EBP−0Ah) and b (EBP−0Ch), the throttle pair L = {L0 at EBP−8, L1 at
EBP−4}; EBP−0Eh/−0Dh are never touched. EBX ← P at `0x41b138`. Entry EAX,
EDX and ECX are not read. Every global and object field is read where the
step names it (H and the vehicle fields are re-read, as the listing does).

1. `0x4a9ce8` ← (s16 `P+0x292` << 10) / 10000 (signed, truncation);
   `0x4450f8([H+0x1e5], 0x4a9ce8)`.
2. `shield_balance(P+0x222)` (`0x413bc8`); `pop ecx`; EAX ← 0.
3. T = `[P+0x1a4]`. T ≠ 0: if dword `0x49ef34` ≠ 0 and P = `[0x4d24e6]`:
   d = `dist_gate([0x4d2684]+0x26, T'+0x26)`, else
   d = `dist_gate(P+0x26, T'+0x26)`, T' = `[P+0x1a4]` read again; ESI ←
   T'+0x26 on both paths. T = 0: d = 0.
4. `weapon_frame([P+0x202], d)` (`0x410b40`).
5. `0x447df8([H+0x1f1])` → EAX = record r; `pop ecx`; L0 ← `[r]`, L1 ←
   `[r+4]` (read, write, read, write).
6. Byte `P+0x93` ≠ 0 (the input set the set point this frame,
   [input.md](input.md)): L1 ← s16 `P+0x290` (dword), byte `P+0x93` ← 0.
   Otherwise: C = `[P+0x1f2]`; word `C+0x50` ≠ 0 (flyer chassis): word
   `P+0x2d7` ← L1's low word; word `P+0x290` ← L1's low word.
7. s = s16 `P+0x28e` (read twice: the test and the value); the divisor
   q = s16(−word `[P+0x1f2]+4`) when s < 0, else s16(word `[P+0x1f2]+6`)
   (C read again; the negation is 16-bit); L0 ← (s << 10) / q (signed,
   truncation; q = 0 faults in the original: precondition, the chassis
   speed limits are nonzero).
8. `0x447da8([H+0x1f1], &L)`.
9. `0x43dbb0([H+0x1f5])` → EAX = record h; `pop ecx`; R ← h's 26h bytes in
   the listing's order (words `+0`, `+2`, `+4`, dword `+6`, words `+16h`,
   `+18h`, `+1Ah`, dwords `+1Ch`, `+20h`, bytes `+24h`, `+25h`, dwords
   `+12h`, `+0Ah`, `+0Eh`; each read before its write); then R+4 ← word
   `P+0x29a`; byte `P+0xa4` = 0: R+0 ← word `P+0x10`, R+2 ← word `P+0x298`.
10. CT = `[H+0x210]` (ESI ← CT). CT ≠ `[P+0x1a4]` (target changed): byte
    `P+0x9b` ← 0; a second compare of CT with `[P+0x1a4]` follows, which
    is always unequal (the byte write cannot change the dword: its taken
    edge `0x41b34d` is unreachable); old target OT = `[P+0x1a4]` ≠ 0: word
    `OT+0x1a2` −= 1; `[P+0x1a4]` ← CT; CT ≠ 0: word `[P+0x1a4]+0x1a2` += 1
    (re-read); byte `P+0x9d` ← 1; CT ≠ 0 and `[P+0x30b]` ≠ 0:
    `0x40e484([P+0x30b], CT)`; then `[P+0x1a4]` = 0 and byte
    `[[P+0x202]+0x14]` ≠ 0: word `P+0x31c` ← 4500.
11. `[P+0x1a4]` = 0 and byte `[[P+0x202]+0x14]` ≠ 0 (every frame without a
    target, W re-read): `timer_word_step(P+0x31b)` (`0x467a48`, counts the
    word `P+0x31c` down); its AX = 0: word `0x4d2504` ← 1 (torso
    centering, [input.md](input.md)).
12. R+6 ← CT, R+25h ← byte `P+0x9b`; `0x43d9b4([H+0x1f5], &R)`.
13. `[P+0x1a4]` ≠ 0: `aim_point(P, &A, &a, &b)` (`0x41b728`), which writes
    A and a (and, through `0x40e4dc`, possibly b).
14. `0x434a24([P+0x1a4], &A, x, y)` with x = EAX with AX ← word a and y =
    ECX with CX ← word b (EAX/ECX as #13 or, without a target, #12 left
    them). **Trap:** without a target nothing in this call writes A, a or
    b, so the HUD fields `H+0x270..0x27f` receive stack residue; with one,
    b is residue unless `0x40e4dc` writes it.
15. Result: EAX, EDX, ECX of `0x434a24`; EBX and ESI restored.

Registers at the twelve calls (EBX = P and EDI = the caller's EDI at
every call; "caller" = the caller's ESI; "slot" = the argument slot as the
callee left it, the `pop ecx` after a one-slot call):

| # | Call → return | Callee | EAX | EDX | ECX | ESI |
|---|---|---|---|---|---|---|
| 1 | `0x41b163` → `0x41b168` | `0x4450f8` | H | `[H+0x1e5]` | 10000 | caller |
| 2 | `0x41b172` → `0x41b177` | `shield_balance` | #1's | #1's | P+0x222 | caller |
| 3 | `0x41b1a7`/`0x41b1bf` → `0x41b1ac`/`0x41b1c4` | `dist_gate` | `[0x4d2684]`+0x26 or P+0x26 | #2's | slot of #2 | T'+0x26 |
| 4 | `0x41b1cf` → `0x41b1d4` | `weapon_frame` | d | W | #3's, or the slot of #2 without a target | T'+0x26 or caller |
| 5 | `0x41b1e4` → `0x41b1e9` | `0x447df8` | `[H+0x1f1]` | #4's | H | as #4 |
| 6 | `0x41b280` → `0x41b285` | `0x447da8` | H | `[H+0x1f1]` | q (sign-extended) | as #4 |
| 7 | `0x41b295` → `0x41b29a` | `0x43dbb0` | `[H+0x1f5]` | #6's | H | as #4 |
| 8 | `0x41b39b` → `0x41b3a0` | `0x40e484` | `[P+0x30b]` | P+0x92 | `[P+0x1a4]` (= CT) | CT |
| 9 | `0x41b3e1` → `0x41b3e6` | `timer_word_step` | W+0x14 | P+0x31b | last written (below) | CT |
| 10 | `0x41b41d` → `0x41b422` | `0x43d9b4` | H | `[H+0x1f5]` | EBP−34h | CT |
| 11 | `0x41b43b` → `0x41b440` | `aim_point` | EBP−0Ah | EBP−40h | EBP−0Ch | CT |
| 12 | `0x41b458` → `0x41b45d` | `0x434a24` | x | EBP−40h | `[P+0x1a4]` | CT |

ECX at #9 is the last of: `[r'+0x12]` with CX ← word `P+0x29a`, then CX ←
word `P+0x298` (byte `P+0xa4` = 0), OT (`0x41b358`), `[P+0x1a4]`
(`0x41b36f`), #8's ECX. After #9 `pop ecx` / `sete cl` / `and ecx,1`
make ECX = (AX = 0). The 16-bit writes keep the other half of the
register: after step 6 the `P+0x93` path leaves EAX = EDX = P+0x92, the
read-back path EAX = (P+0x92) with AX ← L1, EDX = C, ECX = the slot of #5
with CX ← L1 on a flyer chassis; step 7 overwrites all three before #6.
Likewise the halves that the record copy of step 9 keeps (#7's EDX, the
slot of #7) are overwritten before #8, and #9's EDX is overwritten at
`0x41b416` (`0x41b40a` writes only DL).

**Classification of the callees.**

| Callee | Effect | Class | In the replacement |
|---|---|---|---|
| `0x4450f8` | `EnergyPoolGauge` value `+0xb1` ← `[0x4a9ce8]`, redraw byte `+0xb0` ← 2 on a change | presentation ([cockpit.md](cockpit.md)) | guest call |
| `0x413bc8` | shield balance ([cockpit.md](cockpit.md)) | S, hook `shield_balance` | bridge |
| `0x49288c` | `dist_gate` | P, hook | bridge |
| `0x410b40` | player weapon system ([weapons.md](weapons.md)) | S, hook `weapon_frame` | bridge |
| `0x447df8` | record address `G+0xb1` | P | own hook `throttle_record`, bridge |
| `0x447da8` | throttle record and slider (below) | S | own hook `throttle_gauge`, bridge |
| `0x43dbb0` | record address `G+0xb1` | P | own hook `hud_record`, bridge |
| `0x40e484` | weapon word `W'+0x7d` ← 0 when word `T+0x1a8` = 0, else 0FFFFh | S | hook `weapon_target_word` ([the bridged leaves](#the-bridged-leaves)), bridge |
| `0x467a48` | `timer_word_step` | S, hook | bridge |
| `0x43d9b4` | HUD record set and gadget updates | U | guest call |
| `0x41b728` | `aim_point` | S, hook | bridge |
| `0x434a24` | `H+0x26c..0x27f` ← target, aim point, a, b | U | guest call |

**`0x447df8(G)` and `0x43dbb0(G)`** (byte-identical bodies): EAX ← `G` +
0B1h; EDX and ECX are the entry values; no memory access, no branch.

**`0x447da8(G, L)`** (cdecl, two slots, `push ebp/ebx/esi`, EBX = G, ESI =
L; entry EAX and ECX are not read, entry EDX only for the high half of the
pushed set point):

1. `[L+4]` ≠ `[G+0xb5]` (the set point changed): S = `[G+0x18]` (the
   slider gadget), K = `[S+0x17]` (its class table, unaligned), EDX ← entry
   EDX with DX ← word `[L+4]`; `call [K+0x14]` with the slots (S, EDX) and
   EAX = K, ECX = S. For the throttle slider built by `0x447e4c` (class
   table `0x49d12c`, handler `[S+0x42]` = G) slot `+0x14` is `0x45266c`: it
   clamps the value into the gadget's range `+0x24..+0x26`, sets the knob
   word `+0x28` and calls its slot `+8` = `slider_event` (`0x4483a0`),
   whose `slider_base` reaches the `ThrottleGauge` handler `0x447e08`
   (slot 0 of `0x49d1e0`: `G+0xb5` ← ±value; the lever word `0x49a06e`
   when the event's button bit is set). The other slider class built by
   `0x447bac` (`0x49d0e8`, when the gauge config's `+0x40` ≠ 1) has slot
   `+0x14` = `0x452584`, the same scheme. The map counts `0x45266c`
   48/88/62/446/50/70, exactly the `slider_event` counts.
2. `[L]` ≠ `[G+0xb1]` (EDX ← `[L]`): byte `[[G+0x18]+0x1f]` ← 2 (ECX ←
   `[G+0x18]`, read again).
3. `G+0xb1` ← `[L]`, `G+0xb5` ← `[L+4]` (each read again after the
   previous write).
4. Result: EAX = `[L+4]`, EDX = `[L]` (the value of step 2's read); ECX =
   `[G+0x18]` (step 2), else the virtual's ECX, else the entry ECX. ECX is
   dead at the only caller (`0x41b288` writes it), so the hook does not
   reproduce it.

Branch sites `0x447dbc` (je, step 1) and `0x447dd8` (je, step 2), both
reachable both ways. The set point read back next frame (step 5 of
`0x41b130`) is this record's `+0xb5`, so `throttle_gauge` is the writer
that closes the throttle loop between the input path, the slider and
`P+0x290`.

### Stack residue and the entry shim

`0x434a24` copies A, a and b into the HUD fields whether or not
`aim_point` wrote them, and the callees (`weapon_frame`'s weapon virtuals
and emitters, `0x43d9b4`'s gadget virtuals, the slider chain) are large
trees that were not audited for uninitialized reads; because residue under
a replaced frame can reach the simulation, `cockpit_sync` runs at the
original stack coordinate, as `panel_frame` and `logic_frame` do. Entry
shim `cockpit_sync_entry` (chain end `cockpit_sync_capture_entry`, chained
from `flyer_ai_capture_entry`): PUSH EBP; the caller's EBX/ESI to
EBP−44h/−48h (where the prologue pushes them); 112 guard-probed pages
(448 KiB: the weapon fire tree under `weapon_frame` with the emitter and
bullet shims, the HUD and slider trees, the nested hooks' host frames);
{EBP, outgoing ESP = EBP−48h, the caller's EDI} below the gap (EDI is not
saved in the frame); the slot copied for the framework's dispatch. The
core's locals are the original's (scratch = EBP−40h), and every game call
goes through `ai_behaviour_stack_call` with EBX = P, the ESI of the table
above, the caller's EDI, the original's EBP and return address, so the
callees find the same frame, registers and residue. After each one-slot
call (#2, #5, #7, #9) the core re-reads the slot (`pop ecx`). Without the
shim (direct calls) the adapter keeps the frame in a zeroed local buffer
and calls through `logic_frame_machine_call` (EBX/ESI/EDI as named, EBP =
the buffer's synthetic EBP, caller ESI/EDI 0).

**The three helper hooks** need no shim: no local, no uninitialized read.
`0x447da8`'s virtual runs from the adapter's host stack when the hook is
replaced, so the slider's residue then differs from the original's (as for
the slider hooks); the live run shows no difference.

### Hooks

All four cdecl, `ES2_RESULT_PAIR` through the context replacement (EDX:EAX
exact; ECX the framework's entry ECX, dead at every caller).
`cockpit_sync` and `throttle_gauge` refuse compare (game calls, virtual
calls, the entry shim); `throttle_record` and `hud_record` accept it (no
memory access: EAX = the slot + 0B1h, EDX = the entry EDX). Prologues:
`55 8b ec 83 c4 c0 53 56 8b 5d 08` (41b130, 6 bytes relocated, 1/2/3),
`55 8b ec 53 56 8b 75 0c 8b 5d 08` (447da8, 5 bytes, 1/2/1/1),
`55 8b ec 8b 45 08 05 b1 00 00 00` (447df8 and 43dbb0, 6 bytes, 1/2/3);
no branch into any relocated prologue. The nested hooks are called through
the bridge and keep their own modes. Callee counters
`hk_cockpit_sync_call_<va>` for the twelve callees and
`hk_throttle_gauge_slot_14` for the slider virtual.

**Branch sites (21):** `0x41b130`: `0x41b181`, `0x41b18a`, `0x41b192`,
`0x41b201`, `0x41b220`, `0x41b240`, `0x41b313`, `0x41b339`, `0x41b34d`,
`0x41b356`, `0x41b36d`, `0x41b388`, `0x41b391`, `0x41b3aa`, `0x41b3b8`,
`0x41b3ca`, `0x41b3d8`, `0x41b3f3`, `0x41b42c`; `0x447da8`: `0x447dbc`,
`0x447dd8`. All both ways except the taken edge of `0x41b34d`
(unreachable, step 10).

In vitro: `differential_cockpit_sync_test` (cases
`tools/reference-game/invitro/cockpit_sync_cases.h`; frame residue reads
filled nonuniformly, the installed hook through its entry shim, and a
real-callee pass). Validated live: `check-r6-set2` (all hooks replaced),
all sixteen suites identical; `cockpit_sync` = `throttle_record` =
`throttle_gauge` = `hud_record` = `hk_shield_balance_calls` in every
suite, `hk_throttle_gauge_slot_14` = `hk_slider_event_calls`.

## The bridged leaves

Small originals that own code reached through the guest bridge, as hooks
of the group `bridged_leaves` (own code `core/src/bridged_leaves.c`). The
pure swap `0x47df04` is computed in core by its own callers.

### Contracts

All cdecl; "ECX entry" means the function does not change ECX.

| Hook | VA | Arguments | Effect | Result |
| --- | --- | --- | --- | --- |
| `weapon_target_word` | `0x40e484` | W, T | word `W+0x7d` ← 0 when T ≠ 0 and the word `T+0x1a8` is 0, else 0FFFFh | EAX = W, EDX = T, ECX entry |
| `list_next` | `0x411e40` | L, X | none; k = (s16 `X+0x4b` + 1) rem s16 `L+6` (CDQ/IDIV) | EAX = `[[L]+4k]`, EDX = k, ECX = L |
| `list_prev` | `0x411e60` | L, X | none; k = (s16 `X+0x4b` + s16 `L+6` − 1) rem s16 `L+6` | as `list_next` |
| `r_key_toggle` | `0x41b468` | P | see below | EDX:EAX and ECX of the EventPort's slot 0 |
| `nav_marker_set` | `0x434974` | — | H = `[0x49b088]`, S = `[H+0x203]+0x26`: dwords S+8, S, S+4 (in that order) to `H+0x266`, `H+0x25e`, `H+0x262`; byte `H+0x26a` ← 1, `H+0x26b` ← 0 | EAX = H+0x25e, EDX = H, ECX = `[S+4]` |
| `throttle_handler` | `0x447e08` | G, gadget, value | walk k = 0, 1, … while k < `[G+0x68]` (signed, reread every step) over `[G+0x18+4k]`; at the first match: k = 0 → `[G+0xb5]` ← value (byte `G+0xc1` set) or −value, k > 0 → return k−1 without a write | EAX = the written value, k−1 or the count reached; EDX = G+0x18+4k; ECX = G |
| `view_pref_toggle` | `0x45fe38` | — | word `0x4d2518` ← 1 if it was 0, else 0; when `[[0x4d24ea]+0x32]` = `[0x4d24e6]` (camera subject = player) word `0x4d2520` ← 2 | EAX = the new word (whole register), EDX = `[0x4d24ea]`, ECX = `[EDX+0x32]` |
| `matrix_ctor` | `0x495132` | M | `0x495194(M)`, then `0x4953c4(M+0x14)` (EAX = M+0x14, ECX = M) | EAX = M, ECX = M+0x14, EDX of the callees |
| `rotation_ctor` | `0x495194` | R | `0x49514f(R)` with the entry registers | EAX = R, ECX = R, EDX of the callee |
| `rotation_base_ctor` | `0x49514f` | R | none | EAX = R, EDX and ECX entry |

`list_next`/`list_prev`: a zero count faults in the original (division by
zero), outside the contract; the callers' list `0x4a9af8` is never empty
while a view subject is cycled. `throttle_handler`'s arguments are the
handler object `[B+0x42]`, the slider gadget B and the event value as
`slider_base` (`0x43904c`) pushes them; it is slot 0 of the ThrottleGauge
table `0x49d1e0`.

**`0x41b468(P)`, the R key.** F = P+0x92 (EAX = P, EDX = ECX = F). If the
byte `F+0x11` (P+0xa3, the player flag) is set, the byte `F+4` (P+0x96) is
toggled (XOR 1). H = `[0x49b088]`, ESI = F, EBX = E = `[H+0x20b]` (the
EventPort), ECX = 0 and the dword `[EBP−0x16]` ← 0. The branch on `[F+4]`
is taken before any call: set → sound row `0x462830(0x1a)`, else
`0x462830(0x1b)`; POP ECX; then the word `[EBP−0x18]` ← 0x2c (set) or 0x2d.
Then `0x435ac8(E, 0, 0x2c)` and `0x435ac8(E, 0, 0x2d)` (cancel both
messages), and E's slot 0 (`[[E]]`, reread after the cancels) with (E,
EBP−0x18) and EAX = EBP−0x18, EDX = `[E]`. The result registers are those
of slot 0. **Trap:** the message record at EBP−0x18 has 0x18 bytes of
locals; only bytes 0..5 are written, and the EventPort receive `0x436a3c`
→ `0x434e8c` copies bytes 2..0x15 into its queue, so bytes 6..0x15 are the
residue of the frame.

**The swap `0x47df04(M)`** (ENTER 0,0; MOV CX,[M+2]; XCHG [M+4],CX; MOV
[M+2],CX; LEAVE): the words M+2 and M+4 exchanged; EAX = M, CX = the old
`[M+4]` over ECX's high word, EDX unchanged. Its only callers are
`convoy_follow` (`0x46aa60`, at `0x46ab46`), `collision_walk` (`0x427c68`,
at `0x427d13`) and `formation_steer` (`0x41fbb8`, at `0x41fd37`), all own
code that computes it in core (`cv_swap` in `convoy.c`, `ob_swap` in
`obstacle.c`, `ai_motion.c`); the first two observe it, so
`hk_convoy_follow_call_47df04` and `hk_collision_walk_call_47df04` count
it. No hook for `0x47df04`.

**The matrix constructors.** The chain writes nothing: `0x49514f` and
`0x49545b` (under the vector constructor `0x4953c4`) only return their
argument. They are the compiler's empty default constructors of a 32-byte
matrix (0x14-byte rotation, 12-byte vector), not an identity rotation. The
callers are the camera setup `0x48c2e4` and the terrain draw `0x4949e4`,
the mission-start setup `0x48dd08` (three static matrices `0x6c5fb4`,
`0x6c5fd4`, `0x6c5ffc`), and the array constructor `0x49667c` with
`0x495132` as the element constructor (mode 1, cdecl; `0x402be8`,
`0x40d81f`, `0x4788c6`, `0x490ecc`); `0x49514f` is also called by
`0x488b98`. No renderer state is involved; the hooks are own code with no
effect. `0x4953c4` stays a guest call (its verdict is the runtime's).

### Callers' registers

ECX after every hook is the framework's (the entry ECX). `0x40e484` and
`0x49514f` do not change ECX (so `cockpit_sync`, whose call of `0x467a48`
at `0x41b3e1` can receive the ECX that `0x40e484` passed through, sees the
same value). For the others no caller reads ECX before writing it:
`command_key` (`0x460255`, `0x460271`, `0x460496`: the loop and the exit
write ECX first; `0x4604f6` jumps to the exit), `hud_key` (`0x432f82` POP
ECX, `0x432f8d` to the exit, whose ECX is dead at its callers),
`slider_base` (`0x439060`: the second virtual `0x448b58` reads neither ECX
nor EDX, the third gets ECX = `[G+0x3e]`), `0x495132`/`0x495194` and every
direct caller of the constructors (POP ECX), and the array constructor
`0x49667c` (POP ECX in mode 1, ECX written or popped after the loop).

### Hooks

All ten cdecl, `ES2_RESULT_PAIR` through the context replacement (EDX:EAX
exact). Prologues relocated: `55 8b ec 8b 55 0c` (40e484, 1/2/3),
`55 8b ec 53 8b 4d 08` (411e40, 411e60, 1/2/1/3), `55 8b ec 83 c4 e8`
(41b468, 1/2/3), `53 8b 15 88 b0 49 00` (434974, 1/6), `55 8b ec 53 56`
(447e08, 1/2/1/1), `66 83 3d 18 25 4d 00 00` (45fe38, CMP WORD [moffs],0,
8 bytes, on the prologue whitelist), `55 8b ec 53 8b 5d 08` (495132,
495194), `55 8b ec 8b 45 08` (49514f, 1/2/3); no branch into a relocated
prologue. Game calls through the weapons guest bridge with the original's
EAX/EDX/ECX, so nested hooks (`sfx_play`, `rotation_ctor`,
`rotation_base_ctor`) keep their modes. Callee counters:
`hk_r_key_toggle_call_462830`, `_call_435ac8`, `_slot_20b` (the EventPort
slot 0, keyed by its offset in H because slot key 0 means a direct call);
`hk_matrix_ctor_call_495194`, `_call_4953c4`;
`hk_rotation_ctor_call_49514f`.

**Entry shim.** `r_key_toggle` runs at the original stack coordinate
(`bridged_leaves_r_key_entry`, chain end `bridged_leaves_capture_entry`,
chained from `assets_capture_entry`): EBP = T−4 for entry ESP T, the
caller's EBX/ESI where the prologue pushes them (EBP−0x1c, EBP−0x20; the
0x18 bytes of locals are not touched), 48 guard-probed pages,
`{EBP, EBP−0x20, EDI}` for the adapter; every game call runs through
`ai_behaviour_stack_call` with EBX = E, ESI = F, the caller's EDI and the
original return address (`0x41b4a7`/`0x41b4b7`, `0x41b4c8`, `0x41b4d5`,
`0x41b4e1`), and the POP ECX after the sound row rereads the slot. So the
record the EventPort copies carries the original's residue. Without the
shim (direct and legacy calls) the record lies in a zeroed scratch.

**Compare.** `r_key_toggle` refuses it (sound, messages, entry shim). The
others accept it: `weapon_target_word` (span: the word W+0x7d; EAX
compared relative to it), `throttle_handler` (span G+0..0xc1; EDX compared
relative to it; a gadget table beyond 0x2a entries would read past the
span), `nav_marker_set` (one-hop region: H+0x25e..0x26b of `[0x49b088]`),
`view_pref_toggle` (image region `0x4d2518..0x4d2521`), the list steps and
the constructors (no writes).

**Branch sites (9):** `0x40e48f`, `0x40e499`; `0x41b47f`, `0x41b49e`;
`0x447e1f`, `0x447e24`, `0x447e2d`, `0x447e46`; `0x45fe5b`. All both ways
in vitro.

In vitro: `differential_bridged_leaves_test` (cases
`tools/reference-game/invitro/bridged_leaves_cases.h`). Validated live:
`check-h4105` (all hooks replaced), 40 of 40 suites identical;
`hk_throttle_handler_calls` = `hk_slider_event_calls` =
`hk_slider_base_slot_42`, `hk_weapon_target_word_calls` =
`hk_cockpit_sync_call_40e484`, `hk_list_next_calls`/`hk_list_prev_calls`/
`hk_view_pref_toggle_calls` = the matching `hk_command_key_call_*`,
`hk_rotation_ctor_calls` = `hk_matrix_ctor_calls`,
`hk_rotation_base_ctor_calls` above them. `r_key_toggle` and
`nav_marker_set` are never entered in the 40 suites: in-vitro evidence
only.

## The HUD line contract — coupling set and frame writes

The cockpit and HUD code of DBSIM read as two layers per module: **(a)**
the control flow and the *coupling calls* the oracle compares (every read
of the clock wrapper `0x467828`, every draw of RNG stream B `0x4d260b`
through `0x492ee0`/`0x492f24`, every call into the sound API whose hooks
draw stream B or read the clock), with the state that decides them;
**(b)** the frame writes the frame hash compares. The gauges, MFD, HDD and
radio panel are in [cockpit.md](cockpit.md#gauges-mfd-hdd-and-the-radio-panel-in-the-hud-line),
the transitions, slides, jolt, bump and shield palette in
[display_modes.md](display_modes.md#transitions-slides-jolt-bump-and-the-shield-palette-in-the-hud-line).

**Method.** Call sites come from the listing and are **checked by a byte
scan** of the whole CODE section for `E8`/`E9` rel32 transfers to the
targets (clock wrapper, both RNG routines, the sound functions): every hit
is a listed instruction except one, `0x436c35` (below). Function
boundaries, areas, map counts and verdicts come from the generated index
[functions/](../functions/README.md) (map `dbsim-map-20261012`, counts
a60/c1s/c1c/c3_09/c4_08/c3_10, "1000" = saturated); vtable owners from the
Borland RTTI names. The tables are static (read from the bytes, address
given); the per-site live counts are in [the coupling set
measured](#the-coupling-set-measured).

### Result in short

1. **Sites (static, confirmed by the measurement).** The image has 64
   direct calls of the clock wrapper (byte scan = listing); 48 lie in the
   HUD area (`0x429660..0x42e700`, `0x4311e0..0x453824`), one of them
   inside the hook `button_release` (`0x4530ec`). Measured over the 40
   suites, 35 HUD clock sites read in frames (listed in the measurement);
   4 more read once per mission inside the cockpit construction
   (`0x4328da`, `0x445c27`, `0x449964`, `0x44c303`). **8 stream-B sites**
   (`0x434053`, `0x4340d1`, `0x4340ef`, `0x435cba`, `0x436a6c`,
   `0x438d84`, `0x44b165`, `0x44b3ae`); 7 of them draw, `0x436a6c` never.
   Sound: the byte scan finds **`0x436c35`** (`sfx_play(0x18)`) inside the
   jump table of the EventPort show `0x436abc`, which the linear listing
   decodes as data; with the per-frame lip queue `0x4632c4` (AUD area,
   called only by the radio panel) 16 HUD sound-API sites run in frames.
   The load adds 3 (`0x4328cc`).
2. **No transition, slide or the shield palette reads the clock, draws
   stream B or calls the sound API** ([display_modes.md](display_modes.md)):
   the only coupling-relevant edges of `0x42a4c4`/`0x42a9c0` are the
   EventPort draw calls, which run while the transition byte `D+0x1c` is
   still set and so return before their clock read.
3. **The per-frame coupling is a fixed walk of the render root**
   (`0x45fca8`, own code) through `panel_frame` (hook), the panel's eleven
   draw virtuals, the HUD draw and the two port frames, plus the
   presentation frame's palette cycler; every call is gated by a small set
   of state bytes and clocks ([the per-frame
   order](#the-per-frame-order-and-the-state-that-decides-it)). Floor of a
   cockpit frame with no message, jolt, blink, sweep, button or radar blip:
   **9 HUD clock reads and no stream-B draw** (bump 1, lip queue 1, radio
   panel 1, mission time 1, DialogPort 2, EventPort 3), plus the palette
   cycler's 1 (`0x4306b6`): 10 reads per tick with the HUD drawn, 9
   without (confirmed by the measurement).
4. **Coupling also enters from outside the frame**: hooks of the logic
   frame call `0x434010`/`0x434144` and post port messages (stream B on
   receive, one clock read per queued entry); the HUD key and button
   hooks press widget buttons (one clock read each, inside
   `button_release`); the mouse-device callback `0x452cdc` reads the
   clock per call; dialogs pause and resume the DialogPort (clock). These
   are listed with their callers below.
5. **Headless ([own_process.md](../reference-game/own_process.md) §13 question 4):** every
   draw-only entry may be headless *except* the coupling functions of the
   site table and the control flow that reaches them ([May be
   headless](#may-be-headless-own_processmd-13-question-4)).

### The coupling sites of the HUD line

"Live" = map entry count of the function; "Status": **hook** (own code, the
call stays a guest call through the bridge), **H3** (original, verdict H3),
**—** (original, verdict in function_verdicts.md's Verdicts table). C = clock read, B = stream-B draw, A =
sound-API call. The gate is the condition under which the call happens,
read from the instructions of the function named (cockpit.md and
display_modes.md give the function-level detail).

| Site | Kind | Function | Live (map) | Status | Gate (state read before the call) |
|---|---|---|---|---|---|
| `0x4328da` | C | `0x4328cc` arm start-up | 1 (load) | H3 | `H+0x245` = 0 |
| `0x4328ed` | A `sfx_play(0x13)` | `0x4328cc` | 1 (load) | H3 | same |
| `0x43290e`, `0x43291b` | A `sfx_play(0x2d)`, `sfx_rate(0x2d, 0xa410)` | `0x4328cc` | 1 (load) | H3 | flyer chassis: word `[[P+0x1f2]+0x50]` ≠ 0 (P = `[H+0x203]`) |
| `0x43293d` | C | `0x432924` start-up sequence | 70/70/70/71/0/70 | H3 | called by `panel_frame` while `H+0x245` = 0; reads only when `H+0x241` ≠ 0 |
| `0x438dea` | C | `0x438ddc` power-on start | 6/6/6/13/10/8 | — | every call (`G+0x8c` ← 1, `G+0x90` ← clock) |
| `0x434036` | C | `0x434010` jolt on damage | 0/0/0/5/0/0 | H3 | display mode `D+0x14` ≠ 4 |
| `0x434053` | B | `0x434010` | same | H3 | mode ≠ 4 and `0x49b100` = 0 |
| `0x4340b0` | C | `0x43408c` jolt per frame | 1000×5/937 | H3 | `0x49b0fc` ≠ 0 after the mode-4 clear |
| `0x4340d1` | B | `0x43408c` | same | H3 | now < `0x49b0fc` and now > `0x49b100` |
| `0x4340ef` | B | `0x43408c` | same | H3 | now < `0x49b0fc` |
| `0x434161` | C | `0x434144` bump | 60/229/132/348/0/25 | H3 | mode ≠ 4 |
| `0x4341af` | C | `0x434194` bump per frame | 1000×5/937 | H3 | every call (mode 4 only clears `0x49b638` first) |
| `0x434ecd` | C | `0x434e8c` queue insert | 1/13/3/54/13/17 | H3 | at the first free slot of `Q+0x212[10]` (after the drop at 10 entries; hypothesis: always found) |
| `0x4351f6` | C | `0x4351ec` expire | 1000×6 | H3 | every call |
| `0x435635` | C | `0x435610` port frame | 1000×6 | H3 | port not paused: dword `port+0x4cc` = 0 (then `0x4351ec` first) |
| `0x4352a1` | C | `0x435298` shift clocks | 0/16/0/2/2/2 | — | every call (from `0x435b80`) |
| `0x435b68` | C | `0x435b58` pause | 0/16/0/2/2/2 | — | `port+0x4cc` = 0 (first pause) |
| `0x435b9a` | C | `0x435b80` resume | 0/16/0/2/2/2 | H3 | pause depth `port+0x4d0` reaches 0 |
| `0x435cba` | B | `0x435c48` DialogPort receive | 0/2/1/27/0/8 | H3 | the message's text row has more than one variant (byte row+4 > 1) |
| `0x436a6c` | B | `0x436a3c` EventPort receive | 1/11/2/27/13/9 | H3 | same rule, EventPort table |
| `0x435e50` | A `0x462940` (voice) | `0x435d9c` DialogPort show | 0/2/1/17/0/6 | H3 | player alive, `D+0x1c` = 0, entry byte `+0x2c` ≠ 0 (the second test is constant, quirk 2) |
| `0x436adf` | C | `0x436abc` EventPort show | 1/11/2/225/65/208 | H3 | player alive (`[[0x4d24e6]+0x99]` = 0) |
| `0x436c35`, `0x436c3f`, `0x436c49`, `0x436c53`, `0x436c64`, `0x436c6e` | A `sfx_play(0x18/0x19/0x1a/0x1a/0x1a/0x1a)` | `0x436abc` | same | H3 | exactly one per shown message: byte `0x4d1f3b` ≠ 1, mode ≠ 4, not a repeat of the same code within 300 ticks; row by message code (jump table `0x436be2`/`0x436c1b`, below) |
| `0x436cda` | A `0x462940` (voice) | `0x436abc` | same | H3 | byte `0x4d1f3b` ≠ 0, on the shown and the hidden path (not after a cancelled repeat) |
| `0x436f27` | C | `0x436cec` EventPort draw | 1000×5/938 | H3 | `D+0x1c` = 0, showing `+0x49e`, not hidden `+0x4d2`, rectangle visible (`0x431410`), blink byte `+0x4d3` ≠ 0 |
| `0x436f78` | C | `0x436f70` EventPort frame | 1000×5/937 | H3 | every call |
| `0x438bde` | C | `0x438bc0` gauge blink | 0/0/0/1/1/1 | H3 | condition class word `G+0x74` ≠ 0 |
| `0x438d84` | B | `0x438d6c` random in [lo, hi) | 0/0/0/1/1/1 | — | from `0x438bc0` when the deadline `G+0x78` = 0, and lo ≠ hi |
| `0x438e31` | A `sfx_play(0x11)` | `0x438e2c` UI click callback | 0/12/0/51/15/1 | — | every call (callback pointer in 15 widget tables) |
| `0x43d87b` | C | `0x43d704` HUD draw | 1000×5/937 | H3 | HUD drawn (below) and power-on sweep running: `+0x8c` ≠ 0, `+0x8d` = 0 |
| `0x43dce4` | C | `0x43dcd4` mission time | 1000×5/730 | H3 | every call (from the HUD draw when it draws) |
| `0x43ed34` | C | `0x43ec08` radar blip scan | 1000×5/544 | H3 | per actor of the list `[0x4a9af8]` that is not the player, not dead/removed, group `+0x14` = 0, not a hidden building (word `+0x1a8` = 1 with `[[+0x1f2]+0x1e]` ≠ 0), byte `+0x95` = 0 and side byte `[[+0x45]+0x12]` = 1 |
| `0x440eb3` | C | `0x440eac` EnergyWeaponGauge sweep | 19/19/19/95/0/57 | — | gauge drawn and powered, sweep not done (`+0x8d` = 0) |
| `0x441db7` | C | `0x441db0` EnergyPodGauge sweep (slot `+0x1c`) | 0/0/0/19/0/19 | — | same pattern |
| `0x4437d3` | C | `0x4437cc` ShieldsGauge sweep | 176/176/176/306/0/176 | — | same pattern |
| `0x446401`, `0x446433`, `0x446767` | C | `0x446350` MFD draw | 1000×5/729 | H3 | sweep (two paths) and the page refresh of pages 0 and 4 (cockpit.md) |
| `0x449dc0` | C | `0x449bf8` HDD draw | 1000×5/937 | H3 | HDD page `+0x109` = 1 and the HDD drawn (cockpit.md) |
| `0x44b631` | C | `0x44b620` radio panel | 1000×5/944 | H3 | every call (after `0x4632c4`) |
| `0x44b62c` | A `0x4632c4` lip queue | `0x44b620` | same | H3 | every call |
| `0x44b754` | A `sfx_voice` | `0x44b620` | same | H3 | per wingman slot in state 2 entered this frame |
| `0x44b814` | A `sfx_play(0x1c)` | `0x44b620` | same | H3 | per slot whose voice just ended (not speaker `0x25`), row `0x1c` not playing (`0x462af0`, driver time) |
| `0x44b350` | C | `0x44b344` | 0/0/0/0/0/38 | H3 | every call (speaker `0x25` in state 2) |
| `0x44b3ae` | B | `0x44b344` | same | H3 | the slot's animation byte `+0x148` changed this call |
| `0x44b165` | B | `0x44b148` | 0/54/27/407/0/152 | H3 | slot byte `+0x133` ≠ 0 |
| `0x44b12a` | A `0x462b9c` (voice start) | `0x44aff0` | 0/2/1/18/0/7 | H3 | from the DialogPort start callback `0x44b514` |
| `0x44b559` | C | `0x44b514` radio voice start | 0/2/1/18/0/7 | H3 | the voice started (`0x44aff0` AL ≠ 0) |
| `0x44b57f` | A `sfx_play(0x1c)` | `0x44b514` | same | H3 | same, row `0x1c` not playing (`0x462af0`) |
| `0x44c9a3` | C | `0x44c988` HDD map frame | 0/0/0/736/0/0 | H3 | every call (map page shown) |
| `0x44e9fc` | C | `0x44e954` map icon | 69/69/69/1000/45/42 | H3 | on map-redraw frames (byte `0x49d6ae`), per icon whose actor is not dead-and-sided, byte `+0x95` = 0 and (side ≠ 0 or group `+0x14` ≠ 0) |
| `0x44cee4`, `0x44ceee` | A `sfx_play(0x1a/0x1b)` | `0x44cc68` HDDMap key method | 0/0/0/8/0/0 | hook (`core/src/hdd_map_keys.c`) | by key (jump table `0x44cccb`) |
| `0x445c27` | C | `0x445240` MFD constructor | 1 (load) | H3 | construction (`+0x100` ← clock + 30) |
| `0x449964` | C | `0x448cf0` HDD constructor | 1 (load) | H3 | construction (`+0x530` ← clock + 30) |
| `0x44c303` | C | `0x44c28c` HDDMap constructor | 1 (load) | H3 | construction (`+0x149` ← clock + 30) |
| `0x452ced` | C | `0x452cdc` mouse callback | 0/0/0/48/2/6 | platform | byte `0x4d1dd6` ≠ 0 (set at the end of the load) |
| `0x45311a` | C | `0x4530fc` button releases | 1000×6 | H3 | once per entry of the panel object's release list (`[H]+0x356`, count word `[H]+0x396`) |
| `0x4530ec` | C | `0x4530bc` `button_release` | hook | **hook** | every button press (`0x438d9c` = `button_press`, hook) |
| `0x471e1f` | C | `0x471e10` animation start (R3D area) | 1/1/1/1/0/1 | H3 | from `0x438bc0` and the MFD power-on (`0x446350`) |
| `0x471ea9` | C | `0x471e88` animation step (R3D area) | 23/23/23/23/0/23 | H3 | animation state word = 1 |

Not live in the six map suites (static only): `0x43a659`/`0x43a668`
(`0x43a5a0` radar page draw), `0x43b24e`/`0x43b261` (`0x43b238`),
`0x43fe74`/`0x43fe8c`/`0x4402bb` (`0x43fe44` MFD camera view) and
`0x4403ee` (`0x440370`), `0x4520a7` (`0x451ebc`), `0x45213d` (`0x4520d8`),
the CommandPort's voice `0x436991` (`0x43660c`; the CommandPort is built
only when word `0x4aa728` ≠ 0). Of these, `0x43b24e`/`0x43b261`,
`0x43fe74`/`0x43fe8c` and `0x436991` run in some of the 40 suites ([the
coupling set measured](#the-coupling-set-measured)).

**The EventPort sound rows (jump table, confirmed from the DATA bytes of
CODE).** Index byte table `0x436be2` (57 entries, codes `0x00..0x38`),
dword table `0x436c1b` (6 targets); codes above `0x38` take the default.
Code `0x00`, `0x13` → `sfx_play(0x19)`; `0x0c`, `0x0f`, `0x10`, `0x14`,
`0x15` → `sfx_play(0x18)` (the site the listing misses); `0x17`, `0x19`,
`0x1d..0x1f`, `0x2a..0x2f`, `0x34` → `sfx_play(0x1a)`; `0x36` →
`sfx_play(0x1a)` and the blink byte `+0x4d3` ← 1; `0x37`, `0x38` and every
other code → `sfx_play(0x1a)`. So each shown EventPort message makes
exactly one `sfx_play`, and only code `0x36` blinks (which makes the
EventPort draw read the clock every frame while it shows).

### The per-frame order and the state that decides it

The main loop runs, per active pass, the logic frame `0x45f570`, the
camera step `0x4011a0`, the render root `0x45fca8` (`0x45f49b`, own code) and the
presentation frame `0x45fba4` (`0x45f4a0`). The HUD's coupling calls of a
pass happen in the order below. Each row: the state that gates it (guest
addresses; H = `[0x49b088]`, D = `[0x4cf99c]` with mode `D+0x14` and
transition byte `D+0x1c`, P = `[H+0x203]`), the coupling calls in their
original order, and where the detail is. "cond" = `0x4342e0(2)`, the
condition class (`0x438700`) of `condition_ratio(P+0x206, 0, 2)` (hook);
it is evaluated anew by each gauge.

| # | Caller → callee | Gate | Coupling calls, in order |
|---:|---|---|---|
| 1 | render root step 7 → `panel_frame(H)` (hook) | byte `0x49b104` = 0 | — (children below) |
| 1a | → `0x432924` | `H+0x245` = 0; inside: `H+0x241` ≠ 0 | C; then per element i = 0..9 with `[H+0x70+4i]` ≠ 0, word `0x49b05a[i]` < elapsed (unsigned) and `+0x8c` = 0: `0x438ddc` (C each); the same for `[H+0x1e9]`, `[H+0x1f5]`, `[H+0x1ed]` when elapsed > 0; past 200 ticks one EventPort post (row 9) |
| 1b | → `0x43408c` | mode = 4 clears `0x49b0fc`; then `0x49b0fc` ≠ 0 | C; now < `0x49b0fc`: [now > `0x49b100`: `0x42ea70`, B, `0x49b100` ← now + B%10], B (→ `0x42d4a8(B%5)`); now ≥ `0x49b0fc`: `0x49b0fc` ← 0. With `0x49b0fc` = 0 and `0x49b100` ≠ 0: the end path, no coupling |
| 1c | → `0x434194` | — | C (mode 4 clears `0x49b638` first) |
| 1d | panel off (`H+0x20f` ≠ 0) | | only `[H+0x1f9]` slot 8 = the HDD draw (row 1g), then `panel_frame` returns: rows 1e, 1f, 1h, 1i do not run |
| 1e | the ten panel elements `[H+0x70+4i]` slot 8 (weapon gauges) | per gauge: visible (`0x431410(G+4)`) or `D+0x1c`; powered `+0x8c` | sweep not done: the class's sweep (C for EnergyWeapon/EnergyPod; none for Projectile/Toggle/PodWeapon); else after the first full redraw: cond ≠ 0 → `0x438bc0` (C; B when `+0x78` = 0 and lo ≠ hi; animation C's) |
| 1f | `[H+0x1e1]` WeaponRange, `[H+0x1e5]` EnergyPool, `[H+0x1e9]` Shields, `[H+0x1f1]` Throttle draws | Shields: visible and powered | Shields: sweep not done → `0x4437cc` C; the other three: none |
| 1g | `[H+0x1ed]` MFD draw `0x446350` | mode ≠ 4, visible `0x431410(G+0xeb)`, powered | sweep: page 3 → animation (`0x471e88` C, or `0x471e10` C + C); other pages → C; then cond ≠ 0 → `0x438bc0`; then unless blink-off or an animation runs: page refresh C for pages 0 and 4 while the page flag `G+0xe5[page]` is set (cockpit.md); page 3 is the radar (`[G+0xd9]` = `[G+0xcd+4·3]`, built by `0x43e734`), whose refresh calls its slot `+4` `0x43eedc` = the blip scan of row 2c |
| 1g' | `[H+0x1f9]` HDD draw `0x449bf8` | always (also mode 4) | `0x44a1a0` (posts DialogPort messages on HDD actions: row 10); `0x44b620` radio panel: lip queue `0x4632c4` (C, + `sfx_lip_due` when due), C, then per wingman slot 0..2 the state machine (cockpit.md: `sfx_voice`, `0x44b344` C/B, `0x44b148` B, `sfx_play(0x1c)`); mode 4 → return; cond ≠ 0 → `0x438bc0`; visible: page 1 → C; redraw → page slot `+4` (map page: `0x44c988` C; when the map redraw byte `0x49d6ae` is set, the map draw with `0x44e954` C per icon) |
| 1h | `cockpit_valid` (hook), then `0x4530fc([H])` | | C per pending release entry |
| 1i | `0x4349ac` nav marker | `H+0x26a`, `H+0x26b`, distance < 10000 | arrival: EventPort post `0x1d` (row 9) |
| 2 | render root step 17 → HUD draw `0x43d704` | mode ≠ 4 or `D+0x1c` | cond → `+0x74`; ≠ 0: `0x438bc0`; then blink-off phase `+0x76` ≠ 0 or not visible → return; sweep (`+0x8c` and not `+0x8d`): C; `0x43dcd4` C; `0x43e114` → `0x43f2d8` |
| 2c | `0x43f2d8` → `0x43ec08` | the MFD's current page object `[MFD+0xcd+4·[MFD+0xc1]]` is not the radar `[MFD+0xd9]` (page 3) | per qualifying actor C (blip test above); before the scan, when the player byte `+0x96` (R key) no longer matches the radar's `+0x34`, one `button_press` (button 11 or 12 of `0x445234`'s object; C in `button_release`) |
| 3 | render root step 19 → DialogPort frame `0x4361cc` | — | draw slot `+0x18` (`0x437094`, none); `0x435610`: unless paused: `0x4351ec` C, C, then the phase machine: show (slot `+4` = `0x435d9c`: voice `0x462940` → `0x462b9c` C, `sfx_voice`), start callbacks (`+0x4b9[..+0x4b7]`, e.g. `0x44b514`: `0x44aff0` → `0x462b9c` C; C; `sfx_play(0x1c)`; a full extra `0x44b620`), expiry |
| 4 | render root step 20 → EventPort frame `0x436f70` | — | C; draw slot `+0x18` = `0x436cec` (C when blinking); `0x435610`: `0x4351ec` C, C, show = `0x436abc` (C; one `sfx_play`; voice `0x462940` when `0x4d1f3b` ≠ 0) — the EventPort is never paused |
| 5 | presentation frame `0x45fba4` | | `0x42a9c0` (none), palette cycler `0x4306ac` (C at `0x4306b6`), `0x42a4c4` (none) |

**Message posts** (row 9/10, inside whatever calls them): EventPort slot 0
= `0x436a3c` (B when the row has variants; insert: C), DialogPort slot 0 =
`0x435c48` (the same). Posters: EventPort — `0x432924` (`0x21`/`0x22`),
`0x4349ac` (`0x1d`), `0x420a68` (callers `building_part`, `ecm_sync`,
`vehicle_part`, `mech_part_hit`, `mech_hit_test`, `player_route` and one
more, all hooks), `r_key_toggle` (hook, from `hud_key`); DialogPort —
`0x420a98` (callers `mech_contact`, `mech_kill`, `mech_alert`,
`mech_attacked`, `mech_order`, hooks), `trigger_fire` (hook), `0x44a748`
(HDD wingman selection, from `0x44a1a0` and `0x44d82c`).

**Entries outside the render walk:**

| Entry | Called from | Coupling |
|---|---|---|
| `0x434010` jolt | `ground_collide` (`0x4195a3`), `mech_part_hit` (`0x4189c6`), logic frame | mode ≠ 4: C; `0x49b100` = 0: B |
| `0x434144` bump | `ground_effects`, logic frame | mode ≠ 4: C |
| port posts | the hooks above, logic frame and keys | B (variants), C (insert) |
| `button_press` `0x438d9c` (hook) → `button_release` `0x4530bc` (hook) | `hud_key`, `hdd_key`, `mfd_button`, `mfd_click`, `map_areas`, `0x4421c8`, `0x4469e8`, `0x446e60`, `0x43ec08`, `0x44cc68` | button state `+0x1b` ≠ 2: C (release at clock + 10) |
| `0x438e2c` UI click | widget click dispatch (15 table slots) | `sfx_play(0x11)` |
| `0x44cc68` HDDMap keys | `hdd_key` path | `sfx_play(0x1a/0x1b)` by key |
| `0x452cdc` mouse callback | registered by `0x452ae4` (mission load step 10) through `0x480848` on the device of `0x45bd8c` (priority `0x1e`); when the device calls it (message path or poll) was not traced | `0x4d1dd6` ≠ 0: C; then `mouse_queue` (hook) on a button change or when the clock passed `0x49dc20` |
| `0x435b58` / `0x435b80` | dialog open/close `0x45464c`/`0x4548c8`, focus `0x45f0d4`/`0x45f108` (window procedure `0x466034`) | DialogPort only: first pause C; last resume C + `0x435298` C |

**Load (once, inside `mission_root`'s guest calls, mission_load.md §30):**
step 19 `0x429660` → `0x429834` → `0x431bf8` (the panel constructor) makes,
in this order, the gauges, the MFD (`0x4324c8` → `0x445240`: C at
`0x445c27`), the throttle, the HUD (`0x4325c0` → `0x43c800`), the HDD
(`0x432634` → `0x448cf0`: C at `0x449964`, → `0x44c28c`: C at `0x44c303`);
then, **for a flyer chassis only** (word `[[P+0x1f2]+0x50]` ≠ 0), sets
`H+0x245` ← 1 and powers every element, the Shields gauge, the HUD and the
MFD at once (`0x438ddc`, C each, sweep byte `+0x8d` ← 1). Step 26
`0x4328cc`: `H+0x245` = 0 → C (`H+0x241`) and `sfx_play(0x13)`; flyer →
`sfx_play(0x2d)`, `sfx_rate(0x2d, 0xa410)`. Step 10's `0x44f6f4` (bitmap
and graphics-context set-up), step 16's `0x4544c0` (HUD fonts), step 20's
`0x437c8c` (per-MECH display object) and step 26's `0x433f7c` (cursor)
have no coupling call (no site in their direct-call closure; `reach.py`).
The drop sequence `0x45d85c` (step 28, word `G+0x54` ≠ 0, never in the
suites) is display_modes.md's.

### Frame writes (layer b)

The frame hash covers the DIB at `[0x4d3028]` with the page origin and the
last colour table at every present ([presentation.md](presentation.md)). What the HUD line
contributes:

| Module | Writes into the frame | Through |
|---|---|---|
| panel frame `0x4327ac` and its eleven draw virtuals | the panel canvas `[H+4]` (pixels of the cockpit panel) | graphics-context push/pop `0x4311e0`/`0x431210`, the `GL*` library `0x480000..0x48cfff`, text `0x43122c` |
| gauges, MFD, HDD, radio panel | panel pixels; redraw only when the gauge's counter `+0xb0` is 1 or 2 (cockpit.md) | same |
| HUD draw `0x43d704` and gadgets | pixels over the 3D view (gunsights, rotation gauges, nav gauge, radar, altitude ladder, texts) | same; gunsights rebuild the view transform `0x48c444(0)` and restore it |
| ports `0x437094`, `0x436cec`, `0x437164` | message boxes | same; the EventPort scroll `0x4d1290` |
| shield colours `0x443918` | **palette entries `0x42..0x47`** (colour table, not pixels) | `0x4303c4`, flushed by `0x43048c` in `0x45fba4` |
| jolt `0x42d2f8`/`0x42d4a8`/`0x42d624` | **the page origin S** of the present and a saved strip | display_modes.md |
| bump `0x42d82c`/`0x42d854` | the context origin y `+0x224` for the frame's later draws | display_modes.md |
| damage palette `0x42ea44`/`0x42ea70`/`0x42ea94` | swaps the current palette object | `0x4303b0` |
| transitions `0x42a9c0` | slide frames: **extra presents** at the slide sites (frame-hash `site` ≠ `0x45fbe4`) | `0x464a14` inside the slide loops |
| cursor `0x433c54`/`0x433d90`/`0x433e3c`/`0x433f7c` | cursor shape only; in GDI mode no sprite enters the DIB (presentation.md "Cursor sprite") | `0x48992e`, `0x486e70` (role not read here) |

### May be headless (own_process.md §13, question 4)

A headless entry is one whose call can be replaced by "do nothing" without
changing a compared value. From the tables above:

- **Must run (control flow, without drawing):** `0x432924`, `0x43408c`,
  `0x434194`, `0x4530fc`, `0x4349ac`, the gate parts of every gauge draw
  (visible test, power, sweep, cond, `0x438bc0`'s timer and deadline), the
  MFD draw's sweep, page-refresh and radar-page dispatch, the HDD draw's
  `0x44a1a0`, the radio panel `0x44b620` with `0x44b344`/`0x44b148`'s
  timer and stream-B parts, the HDD page-1 timer and the map frame
  `0x44c988` with the per-icon clock test of `0x44e954`, the HUD draw's
  gates, sweep, `0x43dcd4` and the blip scan `0x43ec08`, both port frames
  with the queue (`0x434e8c` … `0x435610`), both receives and shows (the
  sound calls), `0x436cec`'s blink test, `0x438ddc`, `0x435b58`/`0x435b80`.
  The visibility tests `0x431410` (hook `panel_visible`) feed the gates and
  must keep their inputs (rectangles, canvas clip and origin, which the
  jolt and bump move).
- **May be headless:** the pixel work of all of them, the gunsights
  (`0x43b808`, `0x43b978`; but see `0x48c444` in item 5 of "For H3"), the
  rotation, nav and altitude gauges, `0x434520` (widget hover/draw), the
  cursor, `0x443918` and `0x444a90`, the slide loops of `0x42a9c0` (but not
  their mode, panel-off and key-capture writes: display_modes.md), the
  palette flush.
- **Not decided here:** the presentation side (`0x45fba4`'s present and
  the palette cycler) and the redraw counters `+0xb0`: they gate
  only drawing in the functions read here (cockpit.md), so a headless HUD
  may skip them, but drawing code must start from the same values.

### State the own process must model (guest addresses)

Cockpit H: `+0x241`, `+0x245`, `+0x20f`, `+0x26a`/`+0x26b`/`+0x25e`, the
element table `+0x70[10]`, the sub-objects `+0x1e1..+0x1f9`, the ports
`+0x207`/`+0x20b`; per gauge G: `+0x74`, `+0x76`/`+0x77`, `+0x78`,
`+0x7c..+0x88` (blink periods), `+0x8c`, `+0x8d`, `+0x8e`, `+0x90`, `+0xb0`,
`+0x6c` (animation); MFD `+0xc1`, `+0xe5[]`, `+0x100`, `+0x331`, `+0x345`,
`+0x328`; HDD `+0x109`, `+0x10d[]`, `+0x10f[]`, `+0x530`, `+0x101[]`, the
three radio records at `+0x12d + 0x14e·i` (`+0x133`, `+0x137`, `+0x13b`,
`+0x13f`, `+0x143`, `+0x147..+0x149`, `+0x14a`, channel `+0x12f`); HDDMap
`+0x149`, `+0x14d`; ports `+0x49a`, `+0x49e`, `+0x4c9..+0x4cb`, `+0x4cc`,
`+0x4d0`, `+0x4d2`, `+0x4d3`, `+0x4e4`/`+0x4e8` (repeat filter), the queue
`+4`; globals `0x49b0fc`, `0x49b100`, `0x49b634`, `0x49b638`, `0x49b104`,
`0x4d1804` (mission-time deadline), `0x4d1290`/`0x4d1294`, `0x4d1f3a`,
`0x4d1f3b`, `0x4d1dd6`, `0x49d230` (HDD), `0x49cbc6`; the actor list
`0x4a9af8`/`0x4a9afe` with the bytes the blip and icon tests read; the
panel object's release list `[H]+0x356`/`+0x396`; D `+0x14`, `+0x1c`.
Inputs that are not guest memory: the driver answer of `0x462af0` (row
`0x1c` playing; audio.md "What can change the oracle's comparison" item 4) and the lip files behind `sfx_voice`.

### Original quirks found (keep them)

1. `0x4530bc` compares the release-list count with 7 and ignores the
   result: a ninth pending press writes past the eight-entry list
   (`0x4530c3`, no branch).
2. `0x435d9c` tests the third character of its voice-name template
   (`0x49b7d0`, `'_'`) against `'0'` (`0x435e45`); the buffer is not
   rewritten before the test (`0x492b90` copies to `0x6cbc0c`), so the voice
   call depends only on entry byte `+0x2c` (static).
3. `0x435c48` tests the zero-extended wingman index against −1
   (`0x435c78..0x435c7f`), which never matches: a DialogPort message whose
   actor is not a wingman indexes the text table with 255 (static; whether
   any poster sends one was not traced).
4. `0x434144` compares the clock with the end it has just cleared
   (`0x434166`), so the bump always restarts; `0x434194`
   undoes the bump only in a pass whose clock equals the end exactly
   ([display_modes.md](display_modes.md)).
5. The HUD's radar and the MFD's radar page share the blip scan: at most
   one of `0x43f2d8` (HUD, when the MFD is not on page 3) and `0x43eedc`
   (MFD page 3's slot `+4`, from the MFD refresh) runs it in a frame
   (`0x43f2ff`); neither runs it when the HUD is not drawn and the MFD
   refresh is skipped (cockpit.md).

### The coupling set measured

Evidence: `<es2-data>\logs\t58-20261020` (the probe list
`plan/batch-t58.txt` with its generator `entries.py`, the report script
`t58_report.py`, its outputs `out/t58.json`, `out/t58_table.txt` and
`out/t58-tables.md`): every hook original, early arming at the
mission-load call `0x45f42b`, entry probes in count mode, all 40 suites
identical to their goldens.

**What was probed.** 78 entries: the clock wrapper `0x467828`, both RNG
routines `0x492ee0`/`0x492f24` and the 13 sound-API entries of the site
table (`0x462830`, `0x4628e0`, `0x46297c`, `0x46299c`, `0x462940`,
`0x462ac4`, `0x462af0`, `0x462b9c`, `0x463114`, `0x4632c4`, `0x463400`,
`0x4634a0`, `0x4634b0`) with the return-address histogram (`rcv=ret`, 128
slots), plus the 62 original functions of the site table as plain counts.
**A return address is turned into a call site** as `ret − 5` only where the
image has `E8` there and its rel32 lands on the probed entry; all 169
return addresses seen passed (none came from a jump or an indirect call).
RNG sites take their stream from the site table's argument column
(`0x4d260b` = B, `0x4d2599` = A); the call inside `rng_bounded`
(`0x492f2c`) is that routine's own draw and is not counted again.
**Load** is the count of the first-tick report (`probes-load.json`: from
the arming to the first frame boundary `0x45f4ad`, so the mission load and
the first pass of the loop body); **run** is the rest; "per tick" divides
run by the ticks after the first boundary.

**Results.**

1. **The HUD's running clock sites: 35.** The static sites
   `0x436f27` (EventPort draw, blink byte `+0x4d3`) and `0x446433` (MFD
   draw, second sweep path) never read the clock in any suite, although
   their functions run every frame; `0x446401` reads in the load (33
   suites) and only 4 times after it. Four sites that the six-suite map
   counts show as dead do read: `0x43b24e`/`0x43b261` (`0x43b238`;
   c1_01-display-keys, c5_05-moon-missiles) and `0x43fe74`/`0x43fe8c`
   (`0x43fe44`; c3_10-hunt-targ, c5_05-moon-missiles). The AUD-area
   `0x4632c7` of the lip queue reads exactly as often as the radio panel
   `0x44b631` that calls it. Outside the HUD further sites read: the palette
   cycler `0x4306b6`, the dialogs' `0x454ac0` and `0x454b51` (17 suites, up
   to 3.3 per tick while a dialog is open), the voice/lip timeline
   `0x462c08`, `0x463419`, `0x4634ba`, the renderer's `0x471e1f` (load) and
   `0x471ea9`, the hook `button_release`'s `0x4530ec` and the `ready_tone`
   hook's `0x41b0d7`.
2. **The four cockpit-construction sites read once per mission load**
   (`0x445c27`, `0x449964`, `0x44c303` in 40 suites; `0x4328da` in 37, not
   in the three c4_08 suites) and never afterwards. The mission time `0x43dce4` reads 3 times in the load
   window of every suite, one more than the two render passes there, and
   the blip scan `0x43ed34` reads 72 times in the load window over the
   suites.
3. **Every-frame sites:** `0x4341af` and `0x436f78` exactly once and
   `0x4351f6`, `0x435635` exactly twice per tick in 38 suites; the palette
   cycler `0x4306b6` exactly once per tick in those 38 (1.009 in the two
   below). The radio panel `0x44b631` and its lip queue `0x4632c7` read once
   per tick plus 1 to 64 more in exactly the 15 suites in which a wingman
   slot speaks (the same 15 as `0x44b754` and `0x44b165`): the panel is
   also called outside the frame walk there (the message-start callback
   `0x44b514`, [the lower counts](#the-lower-counts-the-extra-render-root-and-the-radio-panels-second-caller)). In the two options-dialog suites (c1_01-options-dialog,
   c3_10-detail-options) the render-root sites run 1.85 to 2.3 times per
   tick while the palette cycler stays at 1.009: the render root runs more often
   than the presentation frame while the dialog is open (the options
   dialog's idle callback `0x45cfc8`, [the lower counts](#the-lower-counts-the-extra-render-root-and-the-radio-panels-second-caller)). `0x435635` runs fewer extra times there than
   `0x436f78`, as the DialogPort's pause gate predicts.
4. **The static floor holds.** A tick with the HUD drawn and no message,
   jolt, sweep, button or blip reads the clock 10 times: the 9 HUD reads
   (bump, lip queue, radio panel, mission time, DialogPort 2, EventPort 3)
   and the palette cycler. Without the HUD (free camera: `0x43dce4` in 28 %
   of the ticks) it is 9. The lowest suite average is 10.02 reads per tick
   (c1_01-free-camera, with 0.51 blip-scan reads); the C1_01 suites average
   10.0 to 15.5. timing.md's "9 to 16 non-limiter calls" matches the reads
   that advance the clock (all but the first of a frame); the per-suite
   totals do not decide it per frame.
5. **Messages:** `0x434ecd` reads once per queued message (458 in the 40
   suites, one of them in the load). The EventPort show reads `0x436adf`
   160 times; per shown message exactly one of the sound rows `0x436c35`
   (10), `0x436c3f` (3), `0x436c49` (28), `0x436c6e` (89) plays, and the
   voice call `0x436cda` runs 132 times. `0x436c53` and `0x436c64` never
   run.
6. **Stream B: 7 of the 8 HUD sites draw.** The EventPort receive
   `0x436a6c` never draws (no EventPort text row with more than one variant
   is reached); the jolt sites `0x434053`/`0x4340d1`/`0x4340ef` draw in 3
   suites (c3_09, as predicted), `0x44b165` in 15. Outside the HUD stream B
   is drawn by `sfx_play_at`'s variant draw `0x462908` (741, 14 suites) and
   by `explosion_create` at `0x4080cf` (4,484, 27 suites). `sfx_play`'s
   variant draw `0x46285c` never draws in the 40 suites.
7. **Sound: 16 HUD sound-API sites run in frames.**
   Live: `0x436991` (`0x43660c` voice, train1-timers only), the four
   EventPort rows and the voice call above, `0x438e31`, `0x44b12a`,
   `0x44b573`, `0x44b57f`, the lip queue `0x44b62c` (every frame),
   `0x44b754`, `0x44b808`, `0x44b814`, `0x44cee4`, `0x44ceee`; plus the
   hook `map_click`'s `0x44d78f`/`0x44d80b`. Never: the DialogPort show's
   voice call `0x435e50` (its entry byte `+0x2c` is never set in the
   suites) and the two EventPort rows of item 5. `0x436c35` is live in 4
   suites. The load adds the arm start-up's three (`0x4328ed` in 37 suites,
   `0x43290e`/`0x43291b` in 3).
8. **The site list for `//sim:hud_test`.** `out/t58_table.txt` holds the
   113 C/B/A sites that ran in the load or in frames in at least one suite
   (`site kind` lines). `//sim:hud_test` with `ES2_T58_TABLE` set to it
   passes: 19 traced calls, each listed with the same kind.

The site tables count calls over all 40 suites ("Load" = from the arming
to the first frame boundary `0x45f4ad`, "Run" = the rest); they show
which sites run and how often relative to the tick.

Kind C (clock):

| Site | Function | Area | Load (40 suites) | Run (40 suites) | Suites | Per tick (min..max where it ran) |
|---|---|---|---:|---:|---:|---|
| `0x41b0d7` | `0x41b0bc` ready_tone | SIM | 0 | 479 | 4 | 0..0.038 |
| `0x4306b6` | `0x4306ac` | PRES | 40 | 72,288 | 40 | 1..1.009 |
| `0x4328da` | `0x4328cc` | HUD | 37 | 0 | 0 | 0..0 |
| `0x43293d` | `0x432924` | HUD | 37 | 2,521 | 37 | 0.008..0.34 |
| `0x434036` | `0x434010` | HUD | 0 | 12 | 3 | 0.001..0.002 |
| `0x4340b0` | `0x43408c` | HUD | 0 | 237 | 3 | 0.012..0.04 |
| `0x434161` | `0x434144` | HUD | 0 | 2,267 | 28 | 0.007..0.074 |
| `0x4341af` | `0x434194` | HUD | 80 | 73,782 | 40 | 1..2.302 |
| `0x434ecd` | `0x434e8c` | HUD | 1 | 458 | 40 | 0.001..0.045 |
| `0x4351f6` | `0x4351ec` | HUD | 160 | 144,584 | 40 | 2..2.023 |
| `0x4352a1` | `0x435298` | HUD | 0 | 70 | 19 | 0..0.01 |
| `0x435635` | `0x435610` | HUD | 160 | 144,584 | 40 | 2..2.023 |
| `0x435b68` | `0x435b58` | HUD | 0 | 70 | 19 | 0..0.01 |
| `0x435b9a` | `0x435b80` | HUD | 0 | 70 | 19 | 0..0.01 |
| `0x436adf` | `0x436abc` | HUD | 0 | 160 | 40 | 0..0.037 |
| `0x436f78` | `0x436f70` | HUD | 80 | 73,786 | 40 | 1..2.305 |
| `0x438bde` | `0x438bc0` | HUD | 0 | 1,152 | 7 | 0..0.314 |
| `0x438dea` | `0x438ddc` | HUD | 129 | 178 | 37 | 0..0.015 |
| `0x43b24e` | `0x43b238` | HUD | 0 | 3 | 2 | 0..0.001 |
| `0x43b261` | `0x43b238` | HUD | 0 | 3 | 2 | 0..0.001 |
| `0x43d87b` | `0x43d704` | HUD | 33 | 523 | 9 | 0..0.187 |
| `0x43dce4` | `0x43dcd4` | HUD | 120 | 63,005 | 40 | 0.282..1.011 |
| `0x43ed34` | `0x43ec08` | HUD | 72 | 499,691 | 35 | 0.233..24.341 |
| `0x43fe74` | `0x43fe44` | HUD | 0 | 7 | 2 | 0..0.001 |
| `0x43fe8c` | `0x43fe44` | HUD | 0 | 80 | 2 | 0.003..0.009 |
| `0x440eb3` | `0x440eac` | HUD | 0 | 1,501 | 36 | 0.003..0.095 |
| `0x441db7` | `0x441db0` | HUD | 0 | 247 | 13 | 0.002..0.032 |
| `0x4437d3` | `0x4437cc` | HUD | 33 | 6,752 | 37 | 0.021..0.875 |
| `0x445c27` | `0x445240` | HUD | 40 | 0 | 0 | 0..0 |
| `0x446401` | `0x446350` | HUD | 33 | 4 | 4 | 0..0.001 |
| `0x446767` | `0x446350` | HUD | 0 | 510 | 2 | 0.033..0.299 |
| `0x449964` | `0x448cf0` | HUD | 40 | 0 | 0 | 0..0 |
| `0x449dc0` | `0x449bf8` | HUD | 0 | 204 | 1 | 0.025..0.025 |
| `0x44b350` | `0x44b344` | HUD | 0 | 38 | 1 | 0.041..0.041 |
| `0x44b559` | `0x44b514` | HUD | 0 | 193 | 15 | 0..0.015 |
| `0x44b631` | `0x44b620` | HUD | 80 | 73,975 | 40 | 1..2.302 |
| `0x44c303` | `0x44c28c` | HUD | 40 | 0 | 0 | 0..0 |
| `0x44c9a3` | `0x44c988` | HUD | 0 | 4,961 | 8 | 0.04..0.452 |
| `0x44e9fc` | `0x44e954` | HUD | 694 | 14,822 | 8 | 0.06..1.701 |
| `0x452ced` | `0x452cdc` | HUD | 0 | 522 | 9 | 0.001..0.054 |
| `0x4530ec` | `0x4530bc` button_release | HUD | 40 | 300 | 28 | 0..0.027 |
| `0x45311a` | `0x4530fc` | HUD | 80 | 1,487 | 40 | 0.001..0.137 |
| `0x454ac0` | `0x454ac0` | DLG | 0 | 115 | 19 | 0..0.044 |
| `0x454b51` | `0x454acc` | DLG | 0 | 28,216 | 17 | 0.142..3.329 |
| `0x462c08` | `0x462b9c` | AUD | 0 | 334 | 40 | 0.001..0.015 |
| `0x4632c7` | `0x4632c4` | AUD | 80 | 73,975 | 40 | 1..2.302 |
| `0x463419` | `0x463400` sfx_voice | AUD | 0 | 332 | 40 | 0.001..0.015 |
| `0x4634ba` | `0x4634b0` sfx_lip | AUD | 0 | 2,029 | 40 | 0.001..0.148 |
| `0x471e1f` | `0x471e10` | R3D | 33 | 5 | 5 | 0..0.001 |
| `0x471ea9` | `0x471e88` | R3D | 0 | 817 | 37 | 0.002..0.11 |

Kind B (stream B):

| Site | Function | Area | Load (40 suites) | Run (40 suites) | Suites | Per tick (min..max where it ran) |
|---|---|---|---:|---:|---:|---|
| `0x4080cf` → `0x492f24` | `0x407f1c` explosion_create | SIM | 0 | 4,484 | 27 | 0.001..0.18 |
| `0x434053` → `0x492ee0` | `0x434010` | HUD | 0 | 10 | 3 | 0.001..0.002 |
| `0x4340d1` → `0x492ee0` | `0x43408c` | HUD | 0 | 98 | 3 | 0.005..0.017 |
| `0x4340ef` → `0x492ee0` | `0x43408c` | HUD | 0 | 227 | 3 | 0.012..0.038 |
| `0x435cba` → `0x492ee0` | `0x435c48` | HUD | 0 | 36 | 7 | 0..0.003 |
| `0x438d84` → `0x492ee0` | `0x438d6c` | HUD | 0 | 29 | 7 | 0..0.014 |
| `0x44b165` → `0x492ee0` | `0x44b148` | HUD | 0 | 4,267 | 15 | 0.009..0.355 |
| `0x44b3ae` → `0x492f24` | `0x44b344` | HUD | 0 | 10 | 1 | 0.011..0.011 |
| `0x462908` → `0x492f24` | `0x4628e0` sfx_play_at | AUD | 0 | 741 | 14 | 0.001..0.052 |

Kind A (sound API):

| Site | Function | Area | Load (40 suites) | Run (40 suites) | Suites | Per tick (min..max where it ran) |
|---|---|---|---:|---:|---:|---|
| `0x4328ed` → `0x462830` | `0x4328cc` | HUD | 37 | 0 | 0 | 0..0 |
| `0x43290e` → `0x462830` | `0x4328cc` | HUD | 3 | 0 | 0 | 0..0 |
| `0x43291b` → `0x463114` | `0x4328cc` | HUD | 3 | 0 | 0 | 0..0 |
| `0x436991` → `0x462940` | `0x43660c` | HUD | 0 | 9 | 1 | 0.006..0.006 |
| `0x436c35` → `0x462830` | `0x436abc` | HUD | 0 | 10 | 4 | 0..0.002 |
| `0x436c3f` → `0x462830` | `0x436abc` | HUD | 0 | 3 | 3 | 0..0.003 |
| `0x436c49` → `0x462830` | `0x436abc` | HUD | 0 | 28 | 10 | 0..0.003 |
| `0x436c6e` → `0x462830` | `0x436abc` | HUD | 0 | 89 | 37 | 0..0.006 |
| `0x436cda` → `0x462940` | `0x436abc` | HUD | 0 | 132 | 40 | 0..0.01 |
| `0x438e31` → `0x462830` | `0x438e2c` | HUD | 0 | 455 | 29 | 0..0.027 |
| `0x44b12a` → `0x462b9c` | `0x44aff0` | HUD | 0 | 193 | 15 | 0..0.015 |
| `0x44b573` → `0x462af0` | `0x44b514` | HUD | 0 | 193 | 15 | 0..0.015 |
| `0x44b57f` → `0x462830` | `0x44b514` | HUD | 0 | 18 | 15 | 0..0.002 |
| `0x44b62c` → `0x4632c4` | `0x44b620` | HUD | 80 | 73,975 | 40 | 1..2.302 |
| `0x44b754` → `0x463400` | `0x44b620` | HUD | 0 | 191 | 15 | 0..0.015 |
| `0x44b808` → `0x462af0` | `0x44b620` | HUD | 0 | 187 | 15 | 0..0.015 |
| `0x44b814` → `0x462830` | `0x44b620` | HUD | 0 | 2 | 2 | 0..0.001 |
| `0x44cee4` → `0x462830` | `0x44cc68` | HUD | 0 | 15 | 2 | 0.002..0.002 |
| `0x44ceee` → `0x462830` | `0x44cc68` | HUD | 0 | 2 | 1 | 0.001..0.001 |
| `0x44d78f` → `0x462830` | `0x44d6e0` map_click | HUD | 0 | 7 | 2 | 0..0 |
| `0x44d80b` → `0x462830` | `0x44d6e0` map_click | HUD | 0 | 20 | 4 | 0..0.003 |
| `0x46291a` → `0x46299c` | `0x4628e0` sfx_play_at | AUD | 0 | 12,871 | 36 | 0.004..0.45 |
| `0x462953` → `0x462b9c` | `0x462940` | AUD | 0 | 141 | 40 | 0..0.01 |
| `0x462970` → `0x463400` | `0x462940` | AUD | 0 | 141 | 40 | 0..0.01 |
| `0x462990` → `0x46299c` | `0x46297c` sfx_refresh | AUD | 0 | 28,313 | 16 | 0.047..1.954 |
| `0x463321` → `0x4634a0` | `0x4632c4` | AUD | 0 | 1,697 | 15 | 0.004..0.134 |
| `0x463422` → `0x4634b0` | `0x463400` sfx_voice | AUD | 0 | 332 | 40 | 0.001..0.015 |
| `0x4634a7` → `0x4634b0` | `0x4634a0` sfx_lip_due | AUD | 0 | 1,697 | 15 | 0.004..0.134 |
| `0x46b538` → `0x462830` | `0x46b48c` fire_create |  | 0 | 24 | 15 | 0..0.002 |
| `0x46b5a9` → `0x462ac4` | `0x46b574` fire_dtor |  | 0 | 21 | 6 | 0..0.001 |
| `0x46b82d` → `0x46297c` | `0x46b678` fire_update |  | 0 | 24,049 | 15 | 0.047..1.177 |

SIM sound sites (A, not listed): 23 sites, 22,164 calls in frames.

### Open

- The meaning of `0x4d1f3a`/`0x4d1f3b`
  (message option bytes, hypothesis: voice-only modes) and of the actor
  byte `+0x95` that decides the blinking blips.
- The not-live sites listed above were not read beyond their gates.
- Whether `0x48992e`/`0x486e70` (cursor) touch anything the presentation
  compares in DirectDraw mode (not the oracle's GDI mode).

## The HUD line in the own process

The HUD and cockpit construction of the mission load and the HUD's
per-frame walk, run headless in the own process (state yes, drawing no;
the HUD's pixels are in [the HUD's pixels](#the-huds-pixels)), with the
coupling calls in the original order on [the HUD line
contract](#the-hud-line-contract--coupling-set-and-frame-writes). The
per-function contract (listing range, every write, every call, EAX/EDX/ECX
where a caller reads them) is the comment at each function in
`sim/src/hud_*.c`. Confirmed in vitro only (`//sim:hud_test` on a
synthetic image); not checked live.

### What is built

| Piece | File | What |
|---|---|---|
| the interface | `sim/include/es2/sim/hud.h` | `es2_sim_hud` (loop, coupling trace, headless count, refused VA), `es2_sim_hud_init`/`_bind`/`_bound`; unbound, the rows use an own record over the bound main loop |
| shared helpers | `sim/src/hud_internal.h` | guest view, calls through the dispatch on the loop's call area, the coupling helpers `hud_clock`, `hud_rng_next`, `hud_rng_bounded`, `hud_sound` (each records kind, original call site, target, argument, EAX) |
| load: map, cameras, fonts | `sim/src/hud_load.c` | `0x44f6f4` map set-up (both surfaces; the height colours and the triangles headless), `0x48bd84`/`0x48bda4` cameras, `0x4544c0` HUD fonts with `0x453630`, the GLFont and holder persistent classes, the detail rows `0x45d490`/`0x45d50c` |
| load: the cockpit | `sim/src/hud_cockpit.c`, `sim/src/hud_gauges.c` | `0x429660` display, `0x431bf8` panel and every element it constructs: gauges, weapon gauges (energy, projectile, pod `0x4321d4`), MFD with its pages, throttle, HUD with its gadgets, HDD with map, status and radio records, the ports, the layout scaling, the palette entries |
| display modes | `sim/src/hud_display.c` | `0x42a1f0`/`0x429e60` (mode set and switch, mode 4), `0x42d624` jolt end, `0x42da08`, `0x429de4`, panel move/slide/hide/show, `0x4316c0` the panel's full draw, the BMP_8 driver's page origin `0x48990e`/`0x4649d8` and its empty slots |
| full draws | `sim/src/hud_draw.c` | slot `+4` of every panel element, the HUD's `0x43d5f0` with the mission time `0x43dcd4`, the radar `0x43f2d8` and blip scan `0x43ec08`, the widget, bar, gadget, MFD, HDD and map-page draws, the label text `0x438920`, the map draw's tail (below) |
| per-frame state | `sim/src/hud_state.c` | widget events (UI click `0x438e2c`), MFD and HDD page switches, the panel copy gate `0x43122c`, canvas push/pop, the syncs the logic frame's hooks call, the power-on start `0x438ddc` |
| per-frame walk, row 1 | `sim/src/hud_frame.c` | the panel frame's children (below) |

Rows: `sim/dispatch/services/hud_load.h`, `hud_map_draw.h`. The sound API is
called through its hooks (`sfx_play` etc.) and the audio rows; stream B
through `0x492ee0`/`0x492f24`, the clock through `0x467828`. The rest of
the walk (the ports, the gauges, MFD and HDD) is in the sections that
follow.

### Static findings

1. **The layout set of the load draws the whole panel, with coupling.**
   Root step 21 `0x42a1f0` → `0x4316c0` calls slot `+4` (the full draw) of
   every element; the HUD's `0x43d5f0` reads the clock in the mission time
   (`0x43dce4`) and runs the radar's blip scan (`0x43ed34` per enemy, the
   radar-key `button_press`) before the first logic frame. A direct-call
   reach misses these (the calls are virtual); the measurement confirms
   them (`0x43dce4` reads 3 times in every load window, one more than the
   two render passes there). The blip scan's radar-key `button_press`
   (hook) is made before the scan's clock reads, as in the original.
2. **`0x431098`'s `_memset` is a zero-length call**: pushed `0`, `0x284`,
   `H+0xa20` (`0x431183..0x431190`), i.e. `memset(H+0xa20, 0x284, 0)`. It
   writes nothing (it is not a 0x284-byte overrun).
3. `0x445234` calls `0x447919` through EAX, code inside the RTTI data
   bytes after `0x447900`: `mov eax, [0x49b088]; mov eax, [eax+0x1ed]; ret`
   (the MFD object).
4. Many draws leave GC `+0x22c` (and `+0x228`) pointing at a dead local
   (`0x42a372`, `0x42da29`, `0x444f7d`, `0x44620e`, `0x449aeb`, `0x44c8d3`,
   …); kept: the pointers are written as the original writes them. The bar
   draws write their colours through `[[V+0x44]+0x228]+4` and restore them
   before returning (no net write).
5. `0x48990e` pops its two arguments with `add sp, 0xc` (16-bit SP), which
   `leave` undoes; no effect.
6. `0x42a1f0` at 640×480 (byte `0x4d2538` = 2) and `0x45c8bc` for a scaled
   descriptor (byte `+0x7b` = 3) call `0x4817c8`; not reached in the suites.

### Headless decisions (platform deviation: no display)

The pixel work of these rows is not done: blits (`0x4816a8` is the load's
headless row), fills `0x486704`, rectangle blits `0x488143`, the rotated
blit `0x48185c`, lines, ellipses and text pixels; the shield palette
`0x443918`, the nav gauge `0x43c40c` and the roving gunsight `0x43c204`
(allowed by [May be headless](#may-be-headless-own_processmd-13-question-4)).
Everything outside the pixels is kept: object fields, texts and their
positions (`0x4d17b4`/`0x4d17b8`), save-under rectangles, knob and bar
rectangles, the GC fields, the canvas stack, the map icon list (including
its deletes), the counters. Each skipped piece is counted in
`es2_sim_hud.headless` and named in its function's comment.

### Fault paths and refusals

Paths into an unset local or a NULL allocation written through are
refused, not stepped over: a pod-gauge kind without a gauge (`0x43231a`),
an MFD page above 5 (`0x44621f`). Neither is reachable. The pod-gauge
factory `0x4321d4` is called only by the five creators
`0x40e3d4..0x40e474`, whose class constructors set the kind `W+0x77` to
0x12, 0x1f, 0x1d, 0x1e and 0x20 (`0x40e2a4..0x40e3a9`). The MFD page is −1
after the constructor (`0x4459f3`) and 3 after the panel set-up
(`0x432530`) before any draw; the other writers cycle 0..5 (`0x446e3c`),
set 5 (`0x4471f7`) or restore the saved word `0x49cbc4` (`0x447233`,
initially 3). The radar's `idiv` by zero (`0x43f4ab`,
`0x43f4db`, `0x43f5a2`, `0x43ef5e`) ends the process with the original's
code (`0xc0000095`, [own_process.md](../reference-game/own_process.md#the-originals-fault-paths));
a NULL `[R+0x14]` in the radar draw is refused at `0x43ef82`.

### The map draw's tail, the load root's last steps, the radar draw and the per-frame row-1 walk

| VA | Row | File | Contract (short) |
|---|---|---|---|
| `0x44e334` | `map_draw` | `hud_draw.c` | frame 0xc4: the grid (four loops on M+0x24/M+0x28 against O+0x220/O+0x224, base minus `step sar 4` for the second and fourth; refused after 2^20 lines), the terrain `0x45030c`, the icons through `0x44e954` with the held icon (V equal to R, or the player without R; the last such icon wins, drawn last), the order lines (stride 42; byte +a6 picks the start, +a5 the colour minus 1 and the end; kind 0 a point of M, kind 1 an object's +0x26 or +0x1aa), the selected icon `0x4d03c8`; pixels headless |
| `0x45030c` | `map_terrain` | `hud_draw.c` | frame 0x6c: the corners through `0x48c57c`/`0x48c5fc`, the blit headless |
| `0x44e954` | `map_icon` | `hud_draw.c` | the clock at site `0x44e9fc` (kind C), the blink gate bit 0x20, the order line colour from `0x434280` |
| `0x44f1bc` | `icon_draw` | `hud_draw.c` | the icon blit (headless) |
| `0x44c250` | `map_to_screen` | `hud_draw.c` | frame 0x14 |
| `0x434280` | `wing_colour` | `hud_draw.c` | `sx16(0x4d3b7c + 2 * sx16(0x49b040 + 2r))` |
| `0x45d490` | `detail_levels` | `hud_load.c` | load root step 22: two searches over five signed-word thresholds (`0x49efcc`, `0x49efe0`) set `[0x497368]`, `[0x49736c]`, `[0x4a9868]`; EAX/EDX/ECX as the listing leaves them |
| `0x45d50c` | `detail_pools` | `hud_load.c` | step 23: thresholds `0x49effe`, values `0x49f004` (the tables overlap in the image) into `[0x4a95b4]` and `[0x4a9dc4]` |
| `0x43eef4` | `radar_draw` | `hud_draw.c` | MFDRadar slot 0, frame 0x68: scale R+0x2c, the player's slot +0x3c, the descriptor blits, the ring past 0x222e0, the blips (dots byte `0x4d253a`), the target marker, the four labels (range in units of `0x434228`, four digits, '0'-padded) |
| `0x434228` | `radar_units` | `hud_draw.c` | `(v idiv 1000) * 6`; EDX the remainder, ECX 1000 |
| `0x432924` | `power_on_walk` | `hud_frame.c` | row 1a: clock at `0x43293d`; ten elements past their delay (signed word `0x49b05a[i]`, unsigned compare) and `+0x1e9`, `+0x1f5`, `+0x1ed` start (`0x438ddc`); past 200: byte H+0x245, the parts' conditions (`0x41b514`) until the first damaged one, event 0x22 or 0x21 through `[[H+0x20b]]` |
| `0x43408c` | `jolt_frame` | `hud_frame.c` | row 1b: mode 4 clears `[0x49b0fc]`; clock `0x4340b0`; past the end: clear, EAX 1; else past `[0x49b100]` the flash toggles (`0x42ea70`) and stream B at `0x4340d1` sets the next toggle (`% 10`), then stream B at `0x4340ef` gives the jolt step (`0x42d4a8`, `% 5`); with no end but a toggle time: `0x42ea94`, `0x42d624`, both cleared |
| `0x434194` | `bump_frame` | `hud_frame.c` | row 1c: clock `0x4341af`; index `((now - [0x49b634]) * 10) / 60`, the curve word `0x49b046[i]` `sar 1` (word `0x4d253c` zero) or `shl` by that word minus 1, into the GC bias `0x42d82c`; at the end time itself `0x42d854` |
| `0x4349ac` | `nav_marker_check` | `hud_frame.c` | row 1i: distance `0x4928d0`; armed and below 10000: disarm and post event 0x1d; unarmed and above: arm |
| `0x438700` | `damage_class` | `hud_frame.c` | `((256 - r) * 100) sar 8` into the classes 0..4 (90, 74, 51, 1) |
| `0x41b514` | `part_condition` | `hud_frame.c` | `condition_ratio(o + 0x206, a, b)` (hook `0x4152ec`) |
| `0x42ea70`, `0x42ea94` | (internal) | `hud_frame.c` | the palette swap `0x4303b0` with `[0x49aef8]` set; flip byte `0x49aef3`. The service rows of these VAs are `scene_jolt_flicker`/`scene_jolt_off` (`scene.c`); the per-frame walk calls its internal copies |
| `0x42d82c`, `0x42d854` | `gc_bias_set`, `gc_bias_clear` | `hud_frame.c` | GC+0x224 and `[0x4cfcb8]` |

The coupling sites of these rows (`0x44e9fc`, `0x43293d`, `0x4340b0`,
`0x4340d1`, `0x4340ef`, `0x4341af`) are in the site table with the same
kind.

**Deviations from the original's stack.** The EventPort records of
`0x432924` and `0x4349ac` set only their word (and for `0x4349ac` the
following dword); the rest of the record is stack residue in the original
and zero here (`panel_frame_hooks.h` notes the residue; see [the panel
frame](#cockpit-panel-frame-as-own-code)). The handler `0x436a3c` reads the
first byte; what `0x434e8c` reads beyond it is not audited (open).
The order line of an icon whose start kind (byte +0xab) is neither 0 nor 1
projects an uninitialised local in the original; here that local is 0.

**Verification.** `//sim:hud_test` (synthetic image): the power-on clock,
the UI click sound, the mission time (wrap 09:59 → 10:00, the deadline),
the blip scan (key press before the clocks, blink half, range), the HUD
full draw's clock order (mission time before radar), display mode 4, the
jolt's clock and two stream-B draws in order, the bump curve and the bias
clear, the power-on walk's part order and the 0x22/0x21 event, the damage
classes, the nav marker's post and arming, the detail rows and the radar
units; every traced site is in the static site table with the same kind,
and, with `ES2_T58_TABLE` set to `out/t58_table.txt` of
`<es2-data>\logs\t58-20261020` (lines `site kind`), in the measured
table too (without it the test reports that comparison as skipped).
`es2sim` counts the coupling calls without keeping the trace.

## The message queue, the ports and the gauge blink

Own code: `sim/src/hud_widgets.c`, 19 service rows (`hud_queue_*`,
`hud_port_*`, `hud_dialog_*`, `hud_event_*`, `hud_gauge_blink`,
`hud_blink_random`; fragment `sim/dispatch/services/hud_queue.h`). Each
function names its listing range of the reference DBSIM.EXE.

### Layout (from the listing, confirmed in vitro)

- Queue at b = port + 4: ten entries of 0x31 bytes at b, the order list
  (ten dwords, entry indices) at b+0x1ea, the used bytes at b+0x212, the
  count word at b+0x21c.
- Entry: code word +0, key +2, the record's dwords +6/+0xa/+0xe/+0x12,
  the row's bytes +0x16 and priority +0x17, the descriptor value +0x18,
  four times +0x1c/+0x20/+0x24/+0x28 (row bytes times 60; the last two
  plus the clock), five row bytes from +0x2c (+0x2c: auto-done).
- Port: current entry +0x49a, shown +0x49e, bitmap +0x4a3, box +0x4a7
  (four signed dwords), +0x4af (the EventPort's clock base), start
  callbacks +0x4b9 (count +0x4b7), hide callbacks +0x4c1 (count +0x4b8),
  started +0x4c9, done +0x4ca, cancelled +0x4cb, pause +0x4cc, hidden
  +0x4d2, save-under pixels +0x4d3. Vtable slots +4 (ask to restart),
  +0xc (agree to end), +0x18 (draw).
- Descriptors (9 bytes: value dword, variant count byte, row pointer):
  DialogPort without sender 0x4d08ed + 9*code with the table 0x4d0444;
  with a sender, its wingman index w (0x433134, low byte) selects
  0x4d0464 + 387*w + 9*code and the table 0x4d0414 + 16*w; EventPort
  0x4d0aaf + 9*code with the table 0x4d0454. A picked variant goes to
  0x4d0ce6 (0x453a10 n times, then 0x453a4c).

### Coupling sites, kept in order

Clock 0x467828 at 0x434ecd (insert), 0x4351f6 (expire), 0x435635 (port
step), 0x436f78 (event frame), 0x438bde (gauge blink). Stream B 0x492ee0
at 0x435cba (dialog receive), 0x436a6c (event receive), 0x438d84 (blink
random). The in-vitro test checks that every own coupling call carries
one of these sites.

### The original's slips (kept)

- 0x434e8c: the priority shift copies from count-1 down to j+1, so the
  old last order entry is overwritten (lost from the list, its used byte
  stays set).
- 0x4351c0: clears the used byte of list[list[9]] (list[9] is an entry
  index, used as an order position).
- 0x4351ec: never expires order position 0.
- 0x435c48: the compare of the wingman index (its low byte) with -1
  never matches.
- 0x436f70: both ports' resume `0x435b80` (DialogPort `[[0x49b088]+0x207]`
  and EventPort `[[0x49b088]+0x20b]`) add their paused time to
  `[0x4d1294]`, so the paused total grows by twice the pause and passes
  the clock once a pause is longer than the time since the clock's start
  (focus and resume trace
  `<es2-data>\logs\o33-20261022\focus-trace-repro4.log`); the
  elapsed time is then negative. **This never faults.** `imul esi`
  (`0x436f93`) leaves the signed product of the elapsed time and
  35 << [0x4d253a] in EDX:EAX, but `xor edx, edx` (`0x436f9a`) clears the
  high dword before `div ecx` by 60 (`0x436f9c`), so only the product's
  low dword is divided (unsigned) and the quotient always fits:
  `[0x4d1290]` ← `[p+0x4af]` − (low dword) / 60, after such a pause a
  wrapped scroll value. es2sim computes the same on every path, headless
  and windowed; no guard and no fault path (confirmed in vitro with
  negative products and products past 60 << 32, Verification below; the
  refusal in `focus-trace-repro4.log` was es2sim's former fault model, not
  the original's).
- 0x435a74: the loop runs while the decremented count byte is positive
  as a signed byte (0x81..0xff: one call).
- The debug sprintf blocks in 0x434e8c, 0x435074, 0x4350d4, 0x435140 and
  0x435610 are jumped over and not reproduced.

### Verification

`//tools/reference-game/invitro:differential_hud_widgets_test`
(`--hud-widgets`): random sequences of 1..8 row calls on a synthetic port,
queue, descriptor rows, stream-B state and gauge; DATA and heap, EAX (by
mask), the stub log and the coupling sites compared against the original's
machine code. Confirmed in vitro; not checked live. The world's paused
total `[0x4d1294]` is 0, below the clock, past it, or far below it, and the
scale `[0x4d253a]` 0..7, so `0x436f70` runs with plain products, negative
products and products past 60 << 32 (coverage "event frame plain product",
"negative product", "product past 60 << 32"; 6000 cases: 656 / 119 / 15),
and the original's machine code returns normally in all of them.
`//sim:hud_test` `event_pause`: headless, windowed and with a process record
that has `process_fault`, such products give `[p+0x4af]` − (low dword) / 60,
no refusal and no fault. Mutation: the quotient of the full 64-bit product
(no `xor edx, edx`) fails both.

### Open

- `0x435920` (the port box overlap test: shown, not hidden, and the
  rectangle overlaps the box, signed, edges inclusive) has no static
  reference (no relocated pointer, no rel32 call or jump; `//sim:table_test`
  requires entry evidence): no row, not reproduced.

## The DialogPort and EventPort show, text, draw and restore

Own code in `sim/src/hud_widgets.c`, 9 service rows (fragment
`sim/dispatch/services/hud_ports.h`). Each function's doc comment gives
its listing range and contract.

### What is own code

- `0x435d9c` DialogPort show (`hud_dialog_show`): gate on the globals
  byte +0x99; the text built by vtable slot +8 (`0x435d0c`); the box
  centred on half of `[0x4d253e]`, filled and edged in the wingman colour
  (`0x434280`) or the plain one; the voice file through `0x45ef9c` and
  played at site `0x435e50` unless `[0x4d1f3a]` asks for no voice or the
  wingman's voice byte is `'0'`; the redraw `0x437164(p, 1)`.
- `0x435d0c` DialogPort text (`hud_dialog_text`): sender name (self,
  `[0x4d13ac]`, or the roster row `[[0x49d230] + 334*w + 0x264]` of the
  wingman index `0x433134`, which stays a hook), then `strcat`/`strncat`
  of the message text (at most 0x4a) into `0x4d0cef`. `0x434298`,
  `0x4342b8` and the roster lookup `0x44b928` are inline here (no rows).
- `0x436abc` EventPort show (`hud_event_show`): clock site `0x436adf`;
  direct when `[0x4d1f3b]` is 1 or the display mode (+0x14) is 4; a repeat
  of the same code inside 0x12c ticks is dropped; otherwise text slot +8,
  slot +0x18, redraw, and the sound from the 0x39-byte index table at
  `0x436be2` (word code above 0x38 takes index 0): 0x1a at `0x436c6e`,
  `0x436c64`, `0x436c53` (also sets the blink byte +0x4d3), `0x436c49`;
  0x18 at `0x436c35`; 0x19 at `0x436c3f`. Voice at `0x436cda` when
  `[0x4d1f3b]` is non-zero.
- `0x436a0c` EventPort text (`hud_event_text`): `strncpy` 0x50 into
  `0x4d0d3f`.
- `0x436cec` EventPort draw (`hud_event_draw`), `0x436fd0` hide
  (`hud_event_hide`), `0x437044` restore (`hud_port_restore`: canvas push
  `0x4311e0`, restore `0x488143(p+0x4a3, p+0x4a7, [p+0x4d3])`, canvas
  pop), `0x437094` DialogPort draw (`hud_dialog_draw`), and the voice path
  `0x45ef9c` with its helper `0x45ef84` (`hud_voice_path`; language byte
  `[0x4d2536]`, output `0x4d293a`).
- The event draw's local graphics context goes through the internal
  `0x480c34`/`0x480d44` helpers. On early exits its EAX is the saved FS:0
  of the original's exception frame; the test compares EAX only on the
  full path. Blink clock site: `0x436f27`.

### Not reproduced (no row)

- `0x4352ec` and `0x435458` have no reference in the listing (no call,
  jump or relocated pointer). `0x435408` is called only from `0x4352ec`
  (at `0x43530c`). All three are dead.
- The queue ctor `0x434e50` is called only from the port base `0x435490`
  (at `0x4354a2`/`0x4354af`). It is inline in `hud_cockpit.c`
  (`port_queue`).

### Coupling sites

Clock: `0x436adf` (event show) and `0x436f27` (event draw blink), next to
the five of [the message queue](#the-message-queue-the-ports-and-the-gauge-blink). Sounds: `0x435e50` and `0x436cda` (voice, path `0x4d293a`),
`0x436c35`, `0x436c3f`, `0x436c49`, `0x436c53`, `0x436c64`, `0x436c6e`.
The test fails on any clock or sound site that is not in its list, or
whose argument differs from the expected one.

### Verification

`//tools/reference-game/invitro:differential_hud_widgets_test`, with the
nine ops; the world adds a graphics context, clip, font, roster, messages
and colour words, and the original side's string imports (`_strcat`,
`_strncat`, `_strncpy`) run the sim's RTL rows. Confirmed in vitro; not
checked live.

## The CommandPort, the CD-root path, the phrase table and the voice select

Own code in `sim/src/hud_widgets.c` (fragment
`sim/dispatch/services/hud_command_port.h`).

### What is own code

| VA | Row | Listing | Contract |
|---|---|---|---|
| `0x4362e4` | `hud_command_receive` | `..0x436317` | The CommandPort's receive. Row d = `0x4d08ed` + 9 * byte [rec]; when [d] and [d+5] are both non-zero, it queues `0x434e8c(p+4, rec, d)`. There is no variant draw. |
| `0x436318` | `hud_command_text` | `..0x4365cf` | The entry's phrases, word-wrapped into the 0x78-byte lines at `0x4d0d8f`. The limit is 0x50, or 0x3c when word [0x4d253a] is zero. All compares are signed. A split is made at a space or at the last character. The next phrase comes from `0x453a10(0x4d0444, phrase)`, called through the dispatch. Height +0x4e7 = (8 << [0x4d253c]) * (count + 1). Width +0x4e3 = `0x4824ec(font, longest)`, or 0. EAX is `0x4d0d8f`. With no entry, the line count +0x4eb is set to 0. |
| `0x4365d0` | `hud_command_frame` | `..0x436609` | If shown and not hidden: draw slot +0x18. If started: done = 1. Then the step `0x435610`. |
| `0x43660c` | `hud_command_show` | `..0x4369a1` | Gated by the globals' end-all byte and by [0x4cf99c]+0x1c. With no box (`[0x4d1f3a] == 1`), shown and hidden are set. Otherwise: the text slot +8, the box centred on gc+0x220, the save-under `0x488012`, the two fills, the lines `0x482634`, the redraw `0x437164(p, 1)`, the box saved into +0x4d7, and the driver sync unless the display is mode 4. Voice: see below. AL is 1. |
| `0x45ee60` | `hud_cd_path` | `..0x45eebc` | Copies the CD root `0x4d280c` to `0x4d28ac`, then strcats the path without its drive (two bytes are skipped when path[1] is ':'). EAX is `0x4d28ac`. |
| `0x453a10` | `hud_phrase_next` | `..0x453a4a` | Bounds check: v below [t] + [t+8] (unsigned). Then a = v + sext word [v-2], and the result is a + 1 + sext byte [a] + 2. Out of bounds: 0. |
| `0x453a4c` | `hud_phrase_row` | `..0x453a82` | Same bounds check. a = v + sext word [v-2]. The result is 0 when byte [a] is zero, else a + 1. |
| `0x471de0` | `hud_voice_select` | `..0x471e0d` | r = [v+0x16]. When [r] >= n (unsigned), it sets [v+0x12] to [r+4] + 8n and word [v+2] to n; AL is 1, and EAX's upper bytes are v's. Otherwise EAX is 0. The original's off-by-one is kept: n == count passes. |

The show's voice:
- The flag byte is [0x4d1f3a] when word [0x4aa728] is set, else [0x4d1f3b].
- The template is `0x49b7f8`. Byte 2 is set to the low byte of [0x4aa728] + '0'. Bytes 6/7 are the entry's code word + 1 as two decimal digits.
- The path is `0x45ef9c(name, 0x49ba68)`. It goes through `0x45ee60` unless name[2] is '0'.
- It is spoken by `0x462940(path, 100)` at **sound site `0x436991`**.

In the in-vitro harness the stubs N, R and S at `0x453a10`, `0x453a4c` and
`0x471de0` are `row=1`: after the seal these rows replace them.
`0x45ee60` sits next to `path_root 0x45ee00`, a separate function.

### Coupling sites

The CommandPort adds one sound site: `0x436991` (voice, `0x462940`). The
argument is either `0x4d293a` (the path without the CD root) or `0x4d28ac`
(the path under the CD root). The test fails on any site outside its list
or any differing argument. No clock site is added.

### Widget-leaf verdicts (`0x434520`, `0x438d9c`, `0x438df8`, `0x4390xx..0x439c48`, `0x4520c8..0x452cb4`)

Method: the `llvm-objdump` listing of a scratch copy of `DBSIM.EXE`. Every
function start in the ranges was checked against three things: direct
`call`/`jmp` targets, dword data references (vtables and tables), and
immediates (computed calls).

- **Dead** (no direct call, no data reference, no immediate): `0x4390b0`
  (empty), `0x439ae8`, `0x452bd4`, `0x452be4` (empty), `0x452c8c`,
  `0x452cb4`.
- **Rows elsewhere:** `0x438d6c`, `0x438ddc`, `0x438e3c`,
  `0x439014`, `0x43901c`, `0x439118`, `0x439398`, `0x4395c4`, `0x4395e8`,
  `0x439c40`, `0x452367`, `0x45236c`, `0x452434`, `0x45256c`, `0x452584`,
  `0x4525d0`, `0x452650`, `0x45266c`, `0x4526bc`, `0x45275c`, `0x452ad5`,
  `0x452ada`, `0x452adf`, `0x452bbc`, `0x452bec`, `0x452c04`, `0x452cdc`.
- **Live** (at the time of the verdict without a row; the rows that
  followed are in [the live widget leaves](#the-live-widget-leaves)). The
  number of direct callers is given in brackets, and vtable or table slots
  by address:
  - `0x434520` (1)
  - `0x438d9c`: the button press (hook `button_press`), called through the
    dispatch from `hud_state.c`/`hud_draw.c`.
  - `0x438df8` (9): already reproduced inline as `scale_layout` in
    `hud_cockpit.c`, but without a row.
  - `0x438e70` (10; code reference `0x4594e4`)
  - `0x43904c` (2; `0x49d1a4`)
  - `0x4390b8` (10)
  - `0x4390c4` (4)
  - `0x4391e0` (`0x49bd64`)
  - `0x439344` (4)
  - `0x439460` (`0x49bd4c`)
  - `0x4396a8` (`0x49bd34`)
  - `0x439758` (4)
  - `0x439834` (1)
  - `0x439858` (`0x49bd18`)
  - `0x439918` (`0x49bd1c`)
  - `0x439a0c` (4)
  - `0x439b8c` (`0x49bd00`)
  - `0x439c48` (2)
  - `0x4520c8` (`0x49db60`)
  - `0x4520d8` (`0x49db64`)
  - `0x45216c` (2)
  - `0x452240` (1)
  - `0x45229c` (1)
  - `0x4523b0` (2)
  - `0x452454` (1)
  - `0x4524a0` (16)
  - `0x4524d0` (4)
  - `0x452520` (vtables `0x49c7b0`, `0x49d100`, `0x49df58`, ...)
  - `0x452600` (`0x49d144`, ...)
  - `0x4526ec` (2)
  - `0x4527c8`
  - `0x452898`
  - `0x45297c`
  - `0x452a28` (3)
  - `0x452a70` (1)
  - `0x452ab8` (8)
  - `0x452c6c` (23)

### Verification

`//tools/reference-game/invitro:differential_hud_widgets_test`, with eight
ops (command receive, text, frame and show, CD path, phrase next, phrase
row, voice select); each op runs one VA for real on both sides
(`wg_real`), the others stay stubbed. The world adds a CommandPort, the
phrase table `0x4d0444` and the rows `0x4d08ed`, the CD root, `[0x4aa728]`
from {0, 1, 2, 0x100, 0x1234}, `[0x4d253c]` and a voice record; the
original's `_strlen` import runs the sim's RTL row. Confirmed in vitro; not
checked live.

## The live widget leaves

Own code: `sim/src/widget_leaves.c`, 22 service rows (fragment
`sim/dispatch/services/widget_leaves.h`; all CDECL, EAX). The contracts, with
listing ranges, are the comments in the file.

### What is own code

| VA | Row | What |
| --- | --- | --- |
| `0x434520` | `widget_panel_rows` | panel rows: state word 0 or 3 by y, each row's slot 0, inside the canvas push/pop |
| `0x438e70` | `widget_three_shape_destroy` | three-shape destructor (counter, delete `+0x2c`, shapes' RTTI count and slot `+0x18`, flag bit 0 deletes) |
| `0x4390b8` | `widget_plus4` | `x + 4` |
| `0x4520c8` | `widget_clock_draw` | mission clock slot 0 (`0x49db60`): the tick |
| `0x4520d8` | `widget_clock_frame` | mission clock slot `+4` (`0x49db64`): box copy, clock at `0x45213d`, tick every 30 |
| `0x45216c` | `widget_clock_tick` | colon toggle, minute/hour digits (signed byte compares), labels |
| `0x452240` | `widget_time_text` | `hh` + separator `0x49db4e` + `mm` into `0x4d1dbc` |
| `0x45229c` | `widget_layout_scale4` | five points shifted by `[0x4d253a]`/`[0x4d253c]` (the first two through `0x438df8`, inline) |
| `0x4523b0` | `widget_hit` | hit test, square or round (abs dx + abs dy not above the radius) |
| `0x452454` | `widget_rect_place` | rectangle moved to a point, size kept |
| `0x4524a0` | `widget_init` | widget base constructor (table `0x49dbb6`) |
| `0x4524d0` | `widget_slider_init` | slider constructor (table `0x49db8a`, kind 1) |
| `0x452520` | `widget_slider_h` | horizontal slider position clamp |
| `0x452600` | `widget_slider_v` | vertical slider position clamp |
| `0x4526ec` | `widget_root_init` | root constructor (table `0x49db7a`, GC `0x480c34` inline, counter + 1) |
| `0x4527c8` | `widget_root_press` | press: find, state 1, kind-1 slot `+0x18`, redraw |
| `0x452898` | `widget_root_release` | release: find, slots `+0x10`/`+8` on the same item or with `0x49db6c` bit 1, state 0, redraw |
| `0x45297c` | `widget_root_move` | move while pressed: state 1/0 on and off the pressed item |
| `0x452a28` | `widget_root_find` | first hit item whose state is not 2, else -1 (16-bit signed loop) |
| `0x452a70` | `widget_root_draw` | full draw: root slot 0, each item's slot 0 |
| `0x452ab8` | `widget_item_redraw` | `+0x227 <- 1`, item slot `+4` |
| `0x452c6c` | `widget_root_add` | item stored at the signed count, count + 1 |

Registers beyond EAX: `0x452454`, `0x4524a0`, `0x4524d0`, `0x4526ec` and
`0x452c6c` also reproduce EDX (and ECX where the comment says so). ECX/EDX of
`0x4523b0`, the sliders and `0x434520` are leftovers of the original and are
not reproduced. `0x438e70` returns the restored FS:0 (not compared). `0x45297c`
with no item pressed returns its entry EAX.

`0x45229c` scales five points (`+0..+0x24`); the first two go through
`0x438df8`.

### Coupling sites

Only the clock read of `0x4520d8` at `0x45213d` (`ES2_SIM_HUD_CLOCK`). The
graphics-context construction `0x480c34` (reads `[0x6c5ed0]+0x40`) and the
layout scale `0x438df8` are inline. Every other callee goes through the
dispatch.

### Not taken

- `0x43904c` is already the hook `slider_base` (throttle group, RVA
  `0x3904c` in `hook_registry.h`). A row would duplicate the VA, so it was
  reused and not rowed.
- The bar/gauge family (vtable slots `0x49bd00..0x49bd64` and their
  targets) has no rows (open; `0x439758` is a row in `panel_leaves.h`): the constructors `0x43907c`,
  `0x4390c4`, `0x439344` and `0x439834`, the slots `0x4391e0`, `0x439460`,
  `0x4396a8`, `0x439858`, `0x439918` and `0x439b8c`, and their helpers
  `0x439758`, `0x439a0c` and `0x439c48`.
- `0x438d9c` (hook `button_press`) and `0x438df8` (inline
  `scale_layout` in `hud_cockpit.c`) have no rows.
- `0x431410` is a hook without a row. The rows call it through the dispatch.
- Dead code: `0x4390b0`, `0x439ae8`, `0x452bd4`,
  `0x452be4`, `0x452c8c` and `0x452cb4`.

### Verification

`//tools/reference-game/invitro:differential_widget_leaves_test`
(`--widget-leaves`, `widget_leaves_cases.h`): random sequences of 1..8 rows
on a seeded synthetic world (root, items, panel, clock, digits, scroll and
shift globals, three-shape widget, surface). The original runs its own
code, including the real `0x452a28`, `0x4523b0` and `0x452ab8` inside the
press; other callees are recording stubs on both sides. Compared: DATA and
heap, the stub log, EAX by mask, EDX for the constructors/place/add, and
the coupling sites. Confirmed in vitro; not checked live.

## The gunsights and gauges as own code

Own code in `sim/src/gadgets.c` (private header `gadgets_internal.h`;
fragment `sim/dispatch/services/hud_gadgets.h`). Each function names its listing
range in the source.

### What is own code

- The fixed gunsight `0x43b808(D)` and the locking gunsight `0x43b978(D)`:
  the rows in `hud_draw.c` delegate to gadgets.c. The fixed sight picks
  bitmap 0, 1 (byte D+0x39) or 2 of the HUD table `[[0x49be78]+8]` from the
  target's screen position (the view rows `0x48c444`, `0x48c57c`,
  `0x48c5fc`, `0x48c6d0`; the box is +-5 shifted by the words
  `0x4d253a`/`0x4d253c`) and blits it centred on (D+0x42, D+0x46). The
  locking sight adds the bracket (half size `(sar1 [D+0x30] << [[0x4d24ea]+0x1a])
  idiv dist2d(P+0x26, D+0x1e)`, the span clamped to 25..75, the rectangle
  stored at D+0x4a..D+0x56, bitmap 7 or 3, with word D+0x2c = 0 the four
  corners with flags 0/2/1/3 and the four edge marks) and the arrow towards
  a target outside the rectangle D+4..D+0x10 (dist3d, angle_ratio + 0xc000,
  sincos, three points through rotate_2d in a vector of three `0x49667c`,
  the fill record `{0, colour}` put into the current context's +0x22c, the
  colour word `0x4d3b8e` with D+0x39 else `0x4d3b94`, the fill `0x4842bb`).
  The camera's 0x20 bytes are copied first and reselected at the end.
- The roving gunsight `0x43c204(D)` (mouse position into D+0x42, then the
  cursor shape 1 inside the rectangle, edges included, else 0), the cursor
  shape `0x433d90(i)` (only with byte `0x49b105` and i not `[0x49b62c]`;
  the entry `[0x49b08c + 4i]` into H+0x236; when H+0x232 is not [H+0x226]
  the cursor set `0x48992e` (word `0x4d2566` set) or `0x486e70`, both
  empty in this build; then `[0x49b62c]` <- i) and the mouse position
  `0x486ece(p)` (8 bytes from `0x4a3620`; EAX p).
- The fixed rotation gauge `0x43b4cc(D)` (the scale, bitmap 11, at D+0x2e;
  the needle x `((word D+0x3a - word D+0x24) & 0xffff) * [D+0x28] idiv
  word D+0x2c + [D+4]`, one more for a negative D+0x3a, plus `0xfffffff1 <<
  x shift`; bitmap 12 when the signed word D+0x3a is >= 300 or <= -300,
  else 13), the reverse gauge's step `0x43b67c(D, v)` (`0x47e08d(word v,
  [D+0x40])` idiv [D+0x28]; when the remainder is not below the step: one
  step back, wrapping at the count `[[0x49be70]+4]`; D+0x44, D+0x48,
  D+0x4c; EAX the table) and the redraw `0x43b7cc(D)` (slot 0 of [D]; EAX
  its EAX).
- The nav gauge `0x43c40c(D)`: the point (byte D+0x45: the marker
  `0x43495c` = H+0x25e when byte H+0x26a is set, else 0; otherwise with a
  route [P+0x45] the target [P+0x1a4]+0x26 when `[0x4d2a6c]`, else
  `route_point 0x423b0c(route+4, word [route+4] + 1)` with the waypoint
  number word), distance_display, vector_angle + 0xc000, the heading
  difference (0x0e38 < d < 0x8000 the right arrow D+0x4d, 0x8000 <= d <
  0xf1c8 the left arrow D+0x4c, else the centre diamond), the fill
  `0x483eb8` with three or four points (sizes `0x4d1808..0x4d1824`), and
  without D+0x45 the text `[0x4d1654]` + waypoint + `0x49c0f9` + radar
  units + `0x49c0fc` to the label `0x438920([D+0x4e], text)`.
- Rows (7): `0x433d90`, `0x486ece`, `0x486e70`, `0x48992e` (bare
  returns), `0x43495c`, `0x43b67c`, `0x43b7cc`.

### Deviations and choices

- The 2D fills `0x483eb8` and `0x4842bb` (rows in `fill_layer.h`) are
  called through the dispatch; without a row `gadgets.c` counts them in
  the HUD's `headless` ([May be
  headless](#may-be-headless-own_processmd-13-question-4); pixels only).
  The gauge's other writes are done either way.
- An integer divide fault of the original (divisor 0 or INT_MIN / -1):
  es2sim ends the process with the original's exception code at the row's
  IDIV ([own_process.md](../reference-game/own_process.md#the-originals-fault-paths));
  without a process record (unit tests) the row refuses.
- The stack locals the original passes to callees live in a frame of the
  call area at the listing's offsets; recorders and comparisons hash the
  bytes behind them, never their addresses.

### Verification

`//tools/reference-game/invitro:differential_gadgets_test` (`--gadgets`,
`gadgets_cases.h`): fixed, target, roving, cursor/mouse/marker/redraw,
rotation, step and nav cases against the original's machine code
(recorder hashes of every view-row, blit, fill, label, cursor-set and
draw-slot call with the bytes passed, DATA, the world, EAX of `0x486ece`,
`0x43495c`, `0x43b67c`, `0x43b7cc`). Not checked in vitro: the headless
path of the two fills and the pixels of the fills and the blit
(recorders). Confirmed in vitro; not checked live.

### Notes

- `0x486e70`, `0x48992e` and `0x486ece` lie in the graphics library's
  address range; if the graphics code claims them, the rows should merge.

## The scroll zones and the radar page

Own code in `sim/src/panel.c` (fragment `sim/dispatch/services/panel_scroll_radar.h`:
`0x433770` scroll zones, `0x43eedc` radar update). The rows `0x43f2d8` and
`0x43eef4` in `hud_draw.c` delegate to `panel.c`.

- `0x433770` (create the scroll zones): three `ScrollTrigger` objects
  (0x24 bytes each, from `0x47749c`) into `H+0x21a/+0x21e/+0x222`, each
  registered with `0x452c6c`; a failed allocation leaves its slot zero.
- `0x43eedc` (MFDRadar slot +4): the blip scan `0x43ec08(R)`, then R's
  vtable slot 0 with `R`; EAX is the slot's result, ECX is `R`.
- `0x43eef4` (MFDRadar page draw, frame 0x68): the rect fill of `R+0x48`;
  `R+0x30` ← the range `[[R+8]+0xc]`; `R+0x2c` ← (range idiv `[R+0x28]`)
  sar the shift in the low bits of word `0x4d253a`. The sweep angle is the
  heading from the player's slot +0x3c with its low word plus 0xa000 (the
  high word kept). The sweep bitmap goes through `0x45caa4` with the sizes
  in the words `B+4`/`B+6` (shifted when byte `0x4d2537` = 3 and byte
  `0x4d256c` = 0), then the background `0x45c8bc`; the range ring ellipse
  only when `[R+0x34]` = 1 and the range exceeds 0x222e0; the MFD bitmaps
  +0x44 and +0x48 of `[[0x49cbcc]+8]`; each blip a 1-pixel rect fill when
  the low byte of word `0x4d253a` is non-zero, otherwise a point; the
  target bitmap +0x40; the two labels `[0x4d1630]`/`[0x4d1634]` with
  four-digit numbers (itoa, zero-padded to the right of the label).
- `0x43f2d8` (the HUD radar, frame 0x74): returns at once when R is the
  page the MFD shows (`[M+0xcd+4*[M+0xc1]]`); else the blip scan, the
  circle, the two view lines (sincos of heading + 0xe000 and + 0x2000,
  rotated by `0x47ffe8`, drawn with `0x483a04`), the frame bitmap +0x48,
  `R+0x2c` ← `[R+0x30]` idiv (height sar 1), the blips as two ellipses or
  a point, the target bitmap +0x40.
- A zero divisor (the original's #DE): es2sim ends the process with the
  original's code there ([own_process.md](../reference-game/own_process.md#the-originals-fault-paths)).
- `0x45caa4` (rotated bitmap, `mfd.c`) and `0x483a04` (the line,
  `fill_layer.h`): `panel.c` calls them through the dispatch when a row
  exists and counts them headless otherwise.

Verification: `//tools/reference-game/invitro:differential_panel_test`
(`--panel`): scroll zones, radar draw, HUD radar and radar update; calls
(with the bytes behind their pointers) and every write compared against
the original's machine code; the GC fields +0x228/+0x22c (stack addresses)
are compared as changed/unchanged. Confirmed in vitro; not checked live.

## Gauge and panel gadget constructors

Own code: `sim/src/hud_gadgets.c`, 21 service rows in
`sim/dispatch/services/gadget_methods.h`, all CDECL with EAX. The contracts and
their listing ranges are the comments in the file; the class tables were
read as dwords from DATA of the verified copy.

### Verdict per VA

The constructors and factories are own code, inline in the rows of
`hud_gauges.c` and `hud_cockpit.c`. The bytes show no other way to reach
them:

- No dword in any section points at them.
- Every `call` site lies inside a function that is now an own row.

So they get no row of their own. The own code for each is a static function
in `sim/src/hud_gauges.c` or `sim/src/hud_cockpit.c`.

| VA | Verdict | Own code | Only caller (listing site → own row) |
| --- | --- | --- | --- |
| `0x440a90` | inlined in an own row | `energy_gauge` (hud_gauges.c) | `0x4320ed` in `0x432074` |
| `0x440fa0` | inlined in an own row | `projectile_gauge` | `0x43219d` in `0x432124` |
| `0x44154c` | inlined in an own row | `pod_gauge` | `0x4322bf` (`0x4321d4`, row `pod_factory`), `0x4419d9` (`0x4419c0`) |
| `0x4419c0` | inlined in an own row | `pod_gauge_plain` | `0x43227a` (`0x4321d4`), `0x441a84` (`0x441a5c`) |
| `0x441a5c` | inlined in an own row | `pod_gauge_energy` | `0x432301` (`0x4321d4`) |
| `0x441df8` | inlined in an own row | `range_gauge` (hud_cockpit.c) | `0x432383` in `0x432340` (row `element_range`) |
| `0x432018` | inlined in an own row | `gauge_file` | `0x432108`, `0x4321b8`, `0x432324` (the three factories) |
| `0x432074` | inlined in an own row | `gauge_factory` (energy) | `0x40e0fc` in `0x40e0e0` (row `energy_gauge_new`) |
| `0x432124` | inlined in an own row | `gauge_factory` (projectile) | `0x40e224` in `0x40e20c` (row `projectile_gauge_new`) |
| `0x442204` | inlined in an own row | `weapon_toggle` | `0x4415d4` (`0x44154c`), `0x4424d8` (`0x4424b0`) |
| `0x4424b0` | inlined in an own row | `weapon_button` | `0x440b16` (`0x440a90`), `0x4410fc` (`0x440fa0`) |
| `0x442978` | inlined in an own row | `power_slider` | `0x440c2a` (`0x440a90`) |
| `0x442c28` | inlined in an own row | `range_toggle` (hud_cockpit.c) | `0x441eb3` (`0x441df8`) |
| `0x447e4c` | inlined in an own row | `throttle_slider` (vertical) | `0x447c80` in `0x447bac` (row `throttle_gauge`) |
| `0x4483e8` (outside the catalogue) | inlined in an own row | `throttle_slider` (horizontal) | `0x447ccc` in `0x447bac` |
| `0x442e20` | inlined in an own row | `scale_layout` (hud_cockpit.c) | `0x442e68`, `0x442ea8`, `0x442f02` (rows of `scale_layout`) |
| `0x442f40` | inlined in an own row | `scale_layout` | `0x43235f` in `0x432340` |

The rest of `0x4420a4` … `0x442f40` are bodies stored in the gadgets' method
tables. The own constructors install those tables, so the slots are reached
from own code through the dispatch. They had no own code before; they are the
rows:

| VA | Row | Table, slot | What |
| --- | --- | --- | --- |
| `0x4420a4` | `gadget_range_items` | range gauge `0x49c85c` `+8` | slot `+0xc` of each of the four items `[G+0x18..]` |
| `0x4421c8` | `gadget_range_key` | range gauge `+0xc` | key word `0x26`: press `0x438d9c(G, 1, 1)`, `0x29`: `(G, 0, 1)`, then AL 1 over the press's EAX; else EAX 0 |
| `0x44244c` | `gadget_toggle_redraw` | weapon toggle `0x49c81c` `+0xc` | redraw byte `+0x1f` 1: panel copy `0x43122c(0, B, 0)`, 2: B's slot 0; either then counts the byte down |
| `0x442480` | `gadget_toggle_changed` | weapon toggle `+8` | owner `[B+0x24]` slot 0 `(owner, B, v)`, B's slot `+4`, event base `[B+0x20]` slot `+8` |
| `0x4428fc` | `gadget_button_redraw` | weapon button `0x49c7dc` `+0xc` | byte 1: copies `(0, B, 0)` and `(0, B+0x56, 0)`; 2: `0x4423bc(B)`, then B's slot 0; then counts the byte down |
| `0x442948` | `gadget_button_changed` | weapon button `+8` | same body as `0x442480` |
| `0x442c10` | `gadget_slider_redraw` | power slider `0x49c798` `+0xc` | byte 2: S's slot 0 (no count-down); else EAX S |
| `0x442dbc` | `gadget_range_toggle_redraw` | range toggle `0x49c758` `+0xc` | same body as `0x44244c` |
| `0x442df0` | `gadget_range_toggle_changed` | range toggle `+8` | only for `v & 1`: event base slot `+8`, then owner slot 0 `(owner, B, v)` |
| `0x4433c7` … `0x443435` | `gadget_base_<va>` (12) | the event-base tables `0x49c77c`, `0x49c7c0`, `0x49c800`, `0x49c840` | `add dword [esp+4], -n; jmp target` with n `0x20`, or `0x3e` for the power slider's `0x4433e5`/`0x4433ef`/`0x4433f9`; targets `0x442df0`, `0x442dbc`, `0x442cb0`, `0x43901c`, `0x442c10`, `0x442b60`, `0x442948`, `0x4428fc`, `0x4426e8`, `0x442480`, `0x44244c`, `0x4423bc` |

Facts:

- **Table layout.** The tables were read from the copy's DATA:
  - The constructors store the main table at `+0x17` and the event base's
    table at `+0x20`. The power slider stores it at `+0x3e`, and its owner sits
    at `+0x42` (listing `0x442275`/`0x44227c`, `0x4424dd`/`0x4424e4`,
    `0x442a2e`/`0x442a35`, `0x442c95`/`0x442c9c`).
  - The range gauge stores `0x49c85c`, whose `+0`/`+4` are the hook
    `gauge_button` `0x442154` and the row `0x442080`.
  - Slot `+0x10` of every table points into RTTI data, not code.
- **Naming mismatch.** The row names in `hud_load.h` do not match the
  constructor that installs each table:
  - `weapon_button_draw` (`0x4423bc`) is slot 0 of the table that the weapon
    toggle `0x442204` installs.
  - `weapon_toggle_draw` (`0x4426e8`) is slot 0 of the weapon button
    `0x4424b0`'s table.
  - The rows are correct; only the names are swapped relative to the
    constructors.
- **Slots that already had rows.** These were not changed: `0x455540`,
  `0x4556ec`, `0x43901c`, `0x439014`, `0x438e2c` and the slider slots
  `0x45256c`/`0x452584`/`0x452520`/`0x4525d0`. The hooks `0x438d9c`
  (`button_press`) and `0x442154` (`gauge_button`) stay hooks.

### Registers

- `0x4421c8` reproduces EAX in full: 0, or the press's EAX with AL 1.
- `0x442c10` and the bodies that end in a call return the last callee's EAX.
- On the "nothing" path, `0x44244c`, `0x442dbc` and `0x4428fc` leave AL =
  redraw byte − 2 over the entry EAX.
- `0x442df0` with an even value leaves the entry EAX.
- EDX and ECX of the bodies are leftovers (pushed arguments, callee values)
  and are not reproduced.
- The entries pass EAX, EDX and ECX through to the target and return its
  registers, as `base2_thunk` does.

### Coupling sites

None. Every callee goes through the dispatch:

- the panel copy `0x43122c`;
- the press hook `0x438d9c`;
- `0x4423bc`;
- every method-table slot.

### Verification

`//tools/reference-game/invitro:differential_panel_gadgets_test`
(`panel_gadgets_cases.h`, suite `panel_gadgets` in `suites.bzl`): random
sequences of 1..8 rows on a seeded world (six gadgets with redraw bytes 0,
1, 2, 3 and `0xff`; two method tables, two event-base tables, two owners; a
range gauge whose items are gadgets; key words `0x26`/`0x29` with and
without high bits; odd and even values). The callees `0x43122c`,
`0x4423bc`, `0x438d9c`, `0x442cb0`, `0x43901c`, `0x442b60` and `0x4426e8`
are recording stubs on both sides; they change the redraw byte, a gadget's
table, its event base's table or its owner, so a read placed before a call
instead of after it shows. Compared: DATA and heap, the stub log, EAX by
mask (full; AL where the original computes only AL; nothing where it leaves
the entry EAX), the coupling trace (empty). Confirmed in vitro; not checked
live.

### Open

- The inline constructors and factories of the first table have no
  in-vitro comparison of their own (unit test and listing only; the
  `cockpit_rest` suite stubs the factories `0x432340..0x432634`). A
  comparison would drive `0x40e0e0`, `0x40e20c`, `0x4321d4`, `0x432340`
  and `0x447bac` against the original with stubs for `new`, the bitmap
  loads and the label code.

## The panel weapon gauges' frame draws

The per-frame draws (element slot `+8`) of the weapon gauges, the small
methods on their path and the gauge base, as service rows in
`sim/src/hud_gauges.c` (fragment `sim/dispatch/services/weapon_gauges.h`). Every
contract was read from the listing of the reference `DBSIM.EXE`
(`llvm-objdump -d -M intel --start-address`, the ranges below) and checked
in vitro (Verification). Notation: `vis(R)` is AL of `0x431410(R)`, D is
`[0x4cf99c]`, W18/W1c the widgets `[G+0x18]`/`[G+0x1c]` (vtable at
`W+0x17`: `+4` redraw, `+0xc` frame).

The panel's per-frame caller (`0x4327xx`) calls slot `+8` of the ten
elements `[H+0x70+4i]`, then `[H+0x1e1]` (range gauge `0x4420a4`),
`[H+0x1e5]` (`0x445008`), `[H+0x1e9]` (`0x443770`), `[H+0x1f1]`
(throttle `0x447d94`), `[H+0x1ed]` and `[H+0x1f9]`, and discards EAX each
time; the frame rows therefore leave EAX undefined.

### Vtables (data at the addresses, read with a dword dump)

| vtable | class | slot 0 | +4 | +8 (frame) | +0x10 | +0x1c (sweep) |
|---|---|---|---|---|---|---|
| `0x49c91c` | energy | `0x440f18` | `0x440c90` | `0x440cb8` | `0x440d74` | |
| `0x49c8fc` | projectile | `0x4414dc` | `0x4411dc` | `0x441290` | `0x44141c` | |
| `0x49c8a4`, `0x49c8d0` | pod | | | `0x441878` | | `0x443444` |
| `0x49c878` | energy pod | `0x441c04` | `0x441c3c` | `0x441da0` | | `0x441db0` |
| `0x49d1e0` | throttle | `0x447e08` | `0x447d60` | `0x447d94` | | |

### Contracts

- **The common head** (`0x440cb8..0x440ce4`, the same at
  `0x441290..0x4412c0` and `0x441878..0x4418a4`): the frame runs only when
  `vis(G+4)` or byte `D+0x1c`, and byte `G+0x8c`.
- **The first full draw** (`0x440cfc..0x440d1e`): when byte `G+0x8e` is
  clear and `vis(G+4)` (a second call), `[[G]+4](G)`, `G+0x8e <- 1`, and
  the frame ends.
- **The blink step** (`0x440d20..0x440d37`): word `G+0x74 <-` AX of
  `0x4342e0(2)`; when AX is nonzero `0x438bc0(G)`. "The blink came on" is
  byte `G+0x76 != G+0x77` with `G+0x76 == 0`, read after the call.
- **The busy test** (`0x44133e..0x441362`, `0x44190e..0x441931`): byte
  `G+0x76`, or `[G+0x6c]` set and EAX of `0x471f24([G+0x6c])` nonzero.
- `0x440cb8(G)` energy frame (`0x440cb8..0x440d73`): the head; byte
  `G+0x8d` clear: the sweep `0x440eac(G)`, set: the first full draw. Then
  the blink step; when the blink came on, W18 and W1c slot `+4`; then W18
  and W1c slot `+0xc`.
- `0x441290(G)` projectile frame (`0x441290..0x441419`): the head; bytes
  `G+0x8d <- 1`, `G+0xb0 <- 2` (unconditionally, before the first-draw
  test); the first full draw; the blink step; when the blink came on, W18
  slot `+4` and `G+0xb0 <- 2`; W18 slot `+0xc`; busy ends the frame; byte
  `[W18+0x1f]` set: `G+0xb0 <- 2`. By `G+0xb0`: 2: `vis([G+0xdb]+4)`
  clear ends the frame without the count down; otherwise, when byte
  `G+0xd3` is clear, `_itoa([G+0xc2], an 8-byte local, 10)`, `[[G+0xdb]] <-
  [0x49b0ac + 4 * (G+0xd6 ? 10 : 11)]` and `0x438920([G+0xdb], local)`;
  then `G+0xb0 - 1`. 1: when `G+0xd3` is clear `0x43122c(0, [G+0xdb]+4,
  0)`; `G+0xb0 - 1`. Other values: nothing. Since `G+0xb0` is set to 2 on
  entry, state 1 is reached only when a callee (the widget frame) changes
  it. A count of eight digits or more overruns the original's 8-byte
  buffer; the own code has the same 8-byte frame block.
- `0x441878(G)` pod frame (`0x441878..0x44196f`): the head; byte `G+0x8d`
  clear: `[[G]+0x1c](G)` (the sweep), set: the first full draw; the blink
  step; when the blink came on `G+0xb0 <- 2`; busy ends the frame;
  `G+0xb0` 2: `[[G]+4](G)`, then `- 1`; 1: `0x43122c(0, [G+0xca]+4, 0)`,
  then `- 1`.
- `0x441da0(G)` (`0x441da0..0x441dae`): `0x441878(G)`; the row calls it
  through the dispatch.
- `0x440eac(G)` energy sweep (`0x440eac..0x440f14`): `t =` clock (site
  `0x440eb3`) `- [G+0x90]`; `t > 50` unsigned: `[G+0xc2] <- [G+0xdb]`,
  byte `G+0x8d <- 1`; otherwise `[G+0xc2] <- 25t` when `[G+0xdb] > 25t`
  (signed), else `[G+0xdb]`. Then `[[G+0x1c]+0x7a] <- [G+0xc2]`, W18 slot
  `+4`, W1c slot `+4`; EAX the last call's.
- `0x441db0(G)` energy pod sweep (`0x441db0..0x441df7`): the same with the
  clock site `0x441db7`, `[G+0xe2] -> [G+0xc6]` and no redraws; EAX
  `[G+0xe2]` on the long path, else `t`.
- `0x443444(G)` (`0x443444..0x443452`): byte `G+0x8d <- 1`; EAX G.
- `0x441c04(G, w)` energy pod click (`0x441c04..0x441c3b`): for `i` from 0
  while `i < [G+0x68]` (signed `jl`): when `w == [G+0x18+4i]` and `i == 0`
  (`sub 1; jae` at `0x441c1c`), byte `G+0xc2 ^= 1` and `G+0xb0 <- 2`. EAX
  the final `i` (`max(n, 0)`), EDX G, ECX `G+0x18+4i`.
- `0x441fa4(G)` range lamp (`0x441fa4..0x442051`), W = `[G+0x20]`: word
  `W+0x1b == 2`: EAX 0. Otherwise byte `G+0xb3 ^= 1`; now set: when byte
  `W+0x40` is clear, `W+0x40 <- 1` and W slot `+4`; now clear: `W+0x40 <-
  0` when it was set, and W slot `+4` in both cases. P =
  `[[0x49b088]+0x20b]`; a 6-byte local event (word `G+0xb3 ? 0x26 : 0x27`,
  dword 0 at `+2`); `0x435ac8(P, 0, 0x26)`, `0x435ac8(P, 0, 0x27)`,
  `[[P]](P, &event)`. EAX: AL 1 over the upper bytes of the last call's
  EAX.
- `0x447d94(G)` throttle frame (`0x447d94..0x447da6`): `[[W+0x17]+0xc](W)`
  with W = `[G+0x18]`; EAX the callee's.
- `0x440834(G, rect, H, W)`: the weapon gauge base (`weapon_gauge_base`,
  listing `0x440834..0x440a39`), a row, EAX G.

### Coupling sites

Only the two clock reads `0x440eb3` and `0x441db7`
(`ES2_SIM_HUD_CLOCK`). Every other callee goes through the dispatch:
`0x431410` (a hook), `0x4342e0`, `0x438bc0`, `0x471f24`, `0x496820`,
`0x438920`, `0x43122c`, `0x435ac8`, and the vtable slots.

### Verdicts

- Rows: `0x440cb8`, `0x441290`, `0x441878`, `0x441c04`, `0x441da0`,
  `0x441fa4`, `0x447d94`, `0x440834`, `0x440eac`, `0x441db0` and
  `0x443444`.
- `0x440834` is also inlined in the factory rows (the constructors
  `0x440a90`, `0x440fa0`, `0x44154c` call `weapon_gauge_base` directly).
- `0x4420a4` (range gauge frame) is in `gadget_methods.h`; `0x445008` and
  `0x443770` are in `panel_leaves.h`.

### Verification

`//tools/reference-game/invitro:differential_gauges_test` (`--gauges`,
`gauges_cases.h`), nine parts on a seeded synthetic world: a fixture gauge
(not a constructed one), its vtable, three widgets, a label, an animation
word, D, the panel, its port and the label colours `[0x49b0ac+40/+44]`.
Callees outside these rows are recorders on both sides with scripted
results (`vis`, the alert value, the blink byte, the clock, the load, the
byte the widget frame leaves in `G+0xb0`); the pods' slot `+0x1c` is the
real `0x443444` or `0x441db0` in part of the cases. Compared: the recorder
hash and count, DATA and heap, the world, and the registers the contract
defines. Trap: the colour table `0x49b0ac` is filled at run time and is 0
in the pristine DATA; the suite presets both entries, or a swapped colour
index goes unseen. Confirmed in vitro; not checked live.

## The HUD's slot 2 draw, the gadgets' slot 1 thunks and the altitude ladder with pixels

Code: `sim/src/hud_frame.c` (the block at the end of the file). Rows:
`sim/dispatch/services/hud_frame.h`. Suite:
`tools/reference-game/invitro/hud_frame_cases.h`.

### 0x43d704(H): the per-frame draw (vtable slot at 0x49c140, listing 0x43d704..0x43d9b0)

The render root 0x45fca8 and the drop descent call it through `[H]+8`. In order:

1. If the display record 0x429820() has mode `+0x14` == 4 and byte `+0x1c` of
   `[0x4cf99c]` is 0, it returns at once. Registers: EAX 4, EDX `[0x4cf99c]`,
   ECX as the display call left it.
2. Word `H+0x74` <- the damage alert 0x4342e0(2). When nonzero, `H+0x7c`,
   `+0x80`, `+0x84` and `+0x88` get the signed words of the tables 0x49be48,
   0x49be52, 0x49be5c and 0x49be66 indexed by it, then the blink 0x438bc0(H)
   runs.
3. With byte `H+0x76` clear it returns with the damage call's (or the blink's)
   EAX/EDX and ECX 2 (or H). When the rectangle of `[H+0xd7]` is not visible
   (0x431410, the panel_visible hook) it returns with that call's EAX/EDX and
   ECX = the rectangle.
4. The canvas is pushed (0x4311e0).
5. With byte 0x49be74 set and 0x4282c8() true, the picked object 0x4282c0()
   goes to 0x4332dc(`[H+0x14]`, it), and the byte is cleared. This branch is
   **dead** in the reference binary: 0x49be74 is 0 in DATA, and the only
   references are the test at 0x43d7b4 and the clear at 0x43d7dc. Our code
   keeps the branch, and 0x4332dc gets no row.
6. The target `[[0x49b088]+0x26c]` is copied to `H+0xb7..0xd1`, including the
   signed word `+8` of its shape record `[[+0x34]+4]`. Without a record the
   original reads address 8 and faults; our code refuses at 0x43d83b.
7. The speed word `[[[0x49b088]+0x203]+0x10]` is limited while the power-on
   ramp runs (byte `H+0x8c` set, `H+0x8d` clear): it may not exceed
   (clock - `[H+0x90]`) * 50, in 16 bits, and the ramp ends once the value is
   reached. The clock is read at site 0x43d87b. The word is negated in 16 bits
   and passed to the method at offset 0xc of `[H+0xdb]`, as (obj, value).
8. The method at offset 4 is called for each of the `[H+0xff]` gadgets
   `[H+0xd7+4i]`. The count is signed.
9. The rest of the draw, in order:
   - the reverse bitmap and label (as in 0x43d5f0);
   - the mission time 0x43dcd4 and the speed text 0x43dca0;
   - the four labels 0x438920;
   - the forward radar 0x43e114;
   - the altitude ladder 0x43dd98, entered with EAX/EDX from the previous
     call and ECX = H;
   - canvas_pop 0x431210, also entered with ECX = H.

   The draw returns the registers canvas_pop leaves.

### The slot 1 thunks

0x43e60f, 0x43e61d, 0x43e62b, 0x43e643, 0x43e651, 0x43e69f and 0x43e6ad have
the same body as 0x43b7cc ([the gunsights](#the-gunsights-and-gauges-as-own-code)). Each one:

- calls slot 0 `[[D]](D)` with EAX = `[D]`;
- returns slot 0's EAX/EDX;
- returns in ECX the argument slot as read back after the call (`pop ecx`).

They are the gadgets' slot 1 for the fixed, roving and locking gunsights, the
nav tape, the tape and the vtables 0x49c174 and 0x49c20c. 0x43b360 (the pip
widget's empty slot 2) is a bare `ret` and keeps the entry registers.

**Listing alignment.** The listing is misaligned around 0x43e60f and 0x43e69f,
and neither address appears as a function start. The bytes after the
"HUDRovingGunsightGadget" RTTI record are code: the thunks 0x43e60f, 0x43e61d
and 0x43e62b, the empty bodies 0x43e639 and 0x43e63e, then the thunks 0x43e643
and 0x43e651. From 0x43e65f on, the bytes are data (the "HUDGunsight" RTTI
record, with a pointer to 0x43e6ff), followed by the thunks 0x43e69f and
0x43e6ad. This makes **0x43e65f and 0x43e680 RTTI data, not functions**. The
vtable 0x49c1c4 entry read earlier as 0x43e65f is doubtful (it is probably an
RTTI field) and needs checking before anyone gives it a row.

### 0x43dd98(H): the altitude ladder with pixels (listing 0x43dd98..0x43e110, frame 0x78)

The row 0x43dd98 (`altitude_ladder`, hud_load.h) delegates to
`es2_sim_hud_altitude_ladder`, and so does the ladder call in 0x43d5f0's own
code (`hud_draw.c`). The ladder works only for a flyer chassis, which it tests
with the word `[[[0x4d24e6]+0x1f2]+0x50]`. For any other chassis it returns at
once with EAX = `[0x4d24e6]`, EDX = its `+0x1f2` and ECX unchanged.

Every point and rectangle it hands to a callee lives in the frame, and our code
reads it back from there wherever the original does. The steps:

1. **Colour records.** GC+0x22c <- &L0c and GC+0x228 <- &L14, the colour
   records {0, 0x4b} and {0, 0x49}. Both pointers are left aimed into the dead
   frame.
2. **Inputs.**
   - the bitmaps `[[0x49be78]+8]` `+0x3c`, `+0x40`, `+0x44` and `+0x48`:
     scale top, scale bottom, marker and the scrolling tape;
   - the terrain floor `[0x4a0be0]+0x110` and its ceiling `+0x114` + 5000;
   - the ground under the player, 0x46e180(terrain, &{P+0x26, P+0x2a}) (the
     terrain_height hook), and the player's height P+0x2e.
3. **Scale and ground bar.**
   - top = `[H+0x117]` + the top bitmap's height;
   - bottom = (0x24 << sy) + `[H+0x117]` - the bottom bitmap's height;
   - both scale bitmaps are blitted;
   - the ground bar is two lines (0x483a04) and a fill (0x486704), drawn from
     the clamped ground position down to the bottom.
4. **Marker.** Drawn at the clamped height position (its x carries a
   `+ 1`, the `inc ecx` at 0x43dfc1).
5. **Tape.** The GC clip rectangle and origin (+0x210..+0x224, +0x208) are set
   to the tape column. The tape bitmap is drawn twice: at (height / 50) mod
   (0x24 << sy), then one bitmap height higher. The GC fields are restored in
   the order 0x210, 0x214, 0x218, 0x21c, 0x220, 0x224, 0x208.

The products are truncated to 32 bits (`imul`, `cdq`). An `idiv` faults in the
original when its divisor is 0 or the division is INT_MIN / -1. That happens
when the terrain range is 0, or when the tape period is 0 because sy >= 30, and
our code refuses in each case (es2sim ends the process with the
original's code, [own_process.md](../reference-game/own_process.md#the-originals-fault-paths)).
On exit, EAX = the old GC+0x208, EDX = the old GC+0x224 and ECX = the old GC+0x21c.

### Coupling

The clock wrapper 0x467828 is read at site 0x43d87b and traced as
`ES2_SIM_HUD_CLOCK`. The terrain_height hook 0x46e180 and the panel_visible hook
0x431410 are called through the dispatch, as are all other callees.

### Verdicts for the VAs of the HUD draw

| VA | Verdict |
|---|---|
| 0x43d704 | row `hud_frame_draw` (`hud_frame.h`) |
| 0x43e60f, 0x43e61d, 0x43e62b, 0x43e643, 0x43e651, 0x43e69f, 0x43e6ad | rows `*_slot1` (`hud_frame.h`) |
| 0x43b360 | row `empty_43b360` (`hud_frame.h`, bare return) |
| 0x43dd98 | row `altitude_ladder` (`hud_load.h`), which delegates to the ladder above |
| 0x43b3e8, 0x43b420, 0x43b488, 0x43b604, 0x43b7dc | rows in `hud_load.h` (`gadget_range`, `gadget_clamp_step`, `tape_layout`, `tick_layout`, `tick_range`) |
| 0x43b4cc, 0x43b704, 0x43b808 | rows in `hud_load.h` (`rotation_fixed`, `rotation_reverse`, `gunsight_fixed`) |
| 0x43b67c, 0x43b7cc | rows in `hud_gadgets.h` (`rotation_step`, `gadget_redraw`) |
| 0x43b594, 0x43b59c, 0x43e639, 0x43e63e | empty rows in `mission_load.h` |
| 0x43b36c, 0x43b3a0, 0x43b460, 0x43b574, 0x43b5a4, 0x43b950, 0x43c148, 0x43c268, 0x43c290, 0x43d5cc | inlined in the own row `hud_ctor` 0x43c800 (`hud_cockpit.c`) |
| 0x43e140 | inlined (scale_run) in the own row `element_hud` 0x4325c0 (`hud_cockpit.c`) |
| 0x4332dc | dead (the 0x49be74 branch above); no row |
| 0x43e65f, 0x43e680 | not code: RTTI data |

### Verification

`//tools/reference-game/invitro:differential_hud_frame_test`
(`hud_frame_cases.h`), three parts on seeded synthetic worlds: the draw
0x43d704 (EAX, EDX compared), the seven thunks and 0x43b360 (EAX, EDX,
ECX), the ladder 0x43dd98 (EAX, EDX, ECX). The other callees are recording
stubs on both sides (on the original's side a `jmp` patched over the
original entry, on ours a row at the same VA). The line, fill and blit
recorders overwrite the point or rectangle they are given, which checks
that both sides read the frame back where the original does. Compared:
DATA, the heap and the recorded calls, with GC+0x228 and GC+0x22c masked
(they point into the dead frame). Not compared: the draw part's ECX, and
the EAX that slot 0 receives from the thunks. Confirmed in vitro; not
checked live.

## The HUD's pixels

Own code: `sim/src/hud_draw.c`. The full draws make the real calls through
the graphics rows: the blits `0x4816a8`/`0x4817c8`; the fills, lines,
points and ellipses `0x486704`, `0x488143`, `0x48817c`, `0x483a04`,
`0x4895ec`, `0x482cec` and `0x488012`; the rotated blit `0x48185c`; and the
text `0x482634` (string) and `0x482534` (char). Nothing in the file counts
`headless`. The row named `es2_sim_service_hud_draw_headless` only forwards
`0x43c40c`/`0x43c204` to the own gadgets in `src/gadgets.c`.

Every gate, coupling call and canvas push/pop of [the HUD line in the own
process](#the-hud-line-in-the-own-process) is kept, with the data the
callees read laid out as in the original:

- **Records in guest locals.** Each rectangle, point and colour record is
  built in a guest local (`hud_frame_push`), laid out as in the original's
  frame. It stays alive while the callee runs. The callees read the colour
  through GC+0x22c (fills, ellipses) or GC+0x228 (lines, points), as a
  record {style, colour}. Some callees change their rectangle argument. The
  GC fields keep pointing at the dead local after the return, as the
  original's do.
- **The shield palette and the altitude ladder.** These are other files'
  rows, reached through the dispatch: `0x443918` (src/display.c) and
  `0x43dd98` (src/hud_frame.c).
- **The radio records and the HDD title.** These are also reached through
  the dispatch: `0x44aea0`/`0x44b3dc` and `0x44a704` (src/mfd.c).

Each function's contract and listing range is in the comment above it in
the file:

- labels: `0x438920`..`0x438a3f` and `0x438aac`..`0x438b1b`;
- system button: `0x434748`..`0x43490e`;
- weapon button and toggle: `0x4423bc`..`0x442448` and `0x4426e8`..`0x4428f8`;
- range toggle: `0x442cb0`..`0x442db9`;
- power slider: `0x442b60`..`0x442c0d`;
- throttle sliders: `0x4480c8`..`0x4481c1` and `0x448660`..`0x448737`;
- HDD button and HDD: `0x44bb60`..`0x44be17`, `0x449a78`..`0x449bf6` and `0x449ee4`..`0x449f61`;
- gauges:
  - `0x4411dc`..`0x44128f`, `0x441744`..`0x441877`, `0x441c3c`..`0x441d9c`;
  - `0x442080`..`0x4420a2`, `0x444f48`..`0x445005`;
  - `0x446160`..`0x44634d`, `0x447d60`..`0x447d90`;
- HUD: `0x43d5f0`..`0x43d701`, `0x43dcd4`..`0x43dd95` and `0x43ec08`..`0x43eeda`;
- reverse rotation gauge: `0x43b704`..`0x43b7cb`;
- the map:
  - draw: `0x44e334`..`0x44e952`;
  - HDD map slot 0: `0x44c8bc`..`0x44c986`;
  - keys: `0x44de14`..`0x44defc`;
  - icons: `0x44df00`..`0x44e0a6` and `0x44f1bc`..`0x44f5cd`;
  - context and clamp: `0x44d2c4`..`0x44d36f` and `0x44d188`..`0x44d24a`;
  - terrain picture: `0x45030c`..`0x450456`.

All are from the `llvm-objdump` listing of a scratch copy of `DBSIM.EXE`
(SHA-256 `8561aa47…c4ad`, checked on the copy).

Facts, each checked in the listing and in vitro:

- **The labels' first dword `[L]` is a font pointer, not a colour.**
  `0x438920` passes it to the text width `0x4824ec` and to the string
  `0x482634`. `[L+0x21]` is the second font, the one the key letter is drawn
  in. The tables that the panel draws store into `[L]`/`[L+0x21]` therefore
  hold fonts:
  - `0x49b0ac`;
  - the entries `0x49b0b0`, `0x49b0b4`, `0x49b0b8`, `0x49b0bc`, `0x49b0d4` and `0x49b0d8`.

  They are 0 in the pristine DATA (the suite fills them with fonts).
- **The key label `0x438aac` reads its index n as a dword.** It compares
  `ax` (the `strlen` result, `0x496766`) with `di` (the low word of n),
  signed, and then indexes the text with `movsx di`. Its callers push a byte
  zero-extended (`movzx`, for example `0x44deaf`). The row
  `es2_sim_service_label_key` passes the whole slot (not masked to a
  byte). An n with the low word `0xffff` therefore marks `text[-1]`, as the
  original does: the byte is cut to 0, the width is measured, and the byte
  is put back.
- **The string and the char draw the glyphs through slot 26 of the driver
  table `0x4a57ac` (`0x4899ac`).** They read the glyph offsets at
  `[font+0x26]` and the pixel base at `[font+0x22]`. The `//sim:hud_test`
  fixture font now has a zero glyph table and that slot.
- **The width `0x4824ec` calls `0x482498`, which calls `sprintf`
  (`0x496796`).** The call has no lasting effect.

### Verification

`//tools/reference-game/invitro:differential_hud_pixels_test` (`hud_pixels_cases.h`):
label, key label, system button, vertical, horizontal and power slider,
weapon toggle, range toggle, HDD button, HDD and weapon button, 1..6 per
case on the same random world. The graphics callees (`0x486704`,
`0x482634`, `0x482534`, `0x4816a8`, `0x4817c8`, `0x488012`, `0x488143`),
the other files' rows (`0x438884`, `0x44a704`, `0x44aea0`, the canvas
`0x4311e0`/`0x431210`), the visibility `0x431410` and the slot hosts are
recording stubs on both sides; each records its arguments and the colour
record GC+0x22c of both GCs, with pointers into the stack tagged and
GC+0x228/+0x22c tagged after each call. `_sprintf` and `_strlen` are
patched (`ir_patch_import`). Compared: the stub sequences, DATA and the
heap; registers are not part of the contract. The key-label byte mask
above was found by this suite (n = `0xffff`). Confirmed in vitro; not
checked live.

### Frame comparison

Replays of `acceptance-60s`, `c1_01-cockpit-keys` and `c1_01-keys` with
`--frames`, compared at the main present site `0x45fbe4` with the reference
capture `<es2-data>\logs\dbsim-frames-20261021`: the replays are
identical to their goldens and the palettes equal in every record, but no
frame's index hash equals the reference (first difference at record 0).
The capture holds hashes only, so the HUD region cannot be compared on its
own; a region-level comparison needs a capture with pixel data or a region
hash in the frame records (open).

## The MFD and HDD frames and pages as own code

Own code in `sim/src/mfd.c` (fragment `sim/dispatch/services/mfd_frames.h`, 24
rows). Each function names its listing range (`llvm-objdump -d -M intel`)
in the source, where the full behaviour is described next to the code.

### What is own code

- The MFD frame `0x446350(G)` (MFDGauge vtable `0x49cfa0` slot +8). It does
  nothing in display mode 4 (`0x429820`), when G+0xeb is not visible
  (`0x431410`) or when byte G+0x8c is clear.
  - Power-up (byte G+0x8d clear): on page 3 the radar sweep animation
    [G+0x331] steps or starts at [G+0xd9]+0x18. G+0xd9 is the slot of page
    3's object (G+0xcd + 4*3), so the start point lies in the radar page.
    On the other pages G+0x8d is set once the clock passes G+0x345. After
    power-up the animation and three bitmaps are deleted, the counter
    [0x6cbee4] is raised and `0x48349c(…, 3)` is called.
  - Alert: word G+0x74 <- damage class `0x4342e0(2)`. When it is non-zero,
    the alert set `0x446ddc` and the blink `0x438bc0` run.
  - The buttons are drawn from the page's row of the 13-byte table
    `0x49cbd8`.
  - The GC is saved (0x81 dwords at +4, and +0x208) and loaded from G+0x104,
    then the automatic page switch `0x44718c` runs.
  - The alert page draws from M = [[0x49b088]+0x1f9]+0x766: the fill, the
    bitmaps through `0x45caa4` and the label. Otherwise the full draw or
    panel copy runs from state byte G+0xb0.
  - Page dispatch on page = [G+0xc1]: pages 0 and 4 are clocked (G+0x100,
    30 ticks); pages 2, 3 and 5 always run; page 1 and pages above 5 clear
    their byte G+0xe5+page. The page frame is slot +4 of [G+0xcd+4*page].
    Pages 2 and 5 also set the title label from `0x4d135c`.
- MFD helpers:
  - `0x4342e0` (part_condition of the player, damage_class)
  - `0x446ddc` (alert tables `0x49cb34`, `0x4d1c88`, `0x49cb94`/`a0`/`ac`/`b8`)
  - `0x44718c`, using `0x434310`, `0x440a3c` and `0x440a64` (the state words
    `0x49cbc4`/`6`/`8`)
  - the animation `0x471e10` (start), `0x471e88` (step) and `0x471f24` (state)
  - the rotated blit at a point `0x45caa4`. Its flags dword goes out with
    the argument's low word over the high word of [D+0x9a], the register
    the origin was loaded into (listing 0x45cac9..0x45cad8).
- MFD pages:
  - the status frame `0x43b238` (request 100, 30-tick flag X+0x34)
  - the target frame `0x440314` (state byte X+0x43; `0x49c394`,
    [[0x49b088]+0x1f5]+0xd6)
  - the orders page complete: draw `0x43f7cc`, frame `0x43f8a0` and the
    order marker `0x43fa5c` (bitmap 0xd, or 0xb + word [[O+0x40]+0x1b], of
    [[0x49cbcc]+8]; button numbers above 6 are reduced by 3)
- The HDD frame `0x449bf8(H)` (HDDGauge `0x49d64c` slot +8):
  - the player's target with the H+0x55e texts `0x4d14e4`/`0x4d14e8` and
    the change mark against `0x49d3dc`
  - the button executor `0x44a1a0`
  - the radio records `0x4d03c8` (V+0x99, S+0x147, S+0x13b with S =
    H+0x12d + 0x14e*k)
  - the radio panel `0x44b620`
  - the alert word and blink
  - the full-draw mark H+0x10d+page
  - the buttons
  - the map edge scroll on page 0 (`0x44cff8`/`0x44d05c`, limits
    `0x4aa640`/`0x4aa648` with ±60000, shift `0x49d6bc`, flag `0x49d6ae`)
  - the page 1 refresh (H+0x530, 30 ticks)
  - state byte H+0xb0, which runs the page draw `0x449ee4` plus title, the
    page frame `0x449f64`, or the port `0x433158` plus title
  - the page frame `0x449f64`, the title `0x44a704`, the button record
    `0x44a174` (slot 0) and the HDD status page wrappers `0x450b54` and
    `0x450b74`
- H+0x109 is the page index. H+0x101 + 4*2 is the same dword, so page 2
  has no page object. The original then only reads marks H+0x10f/H+0x111
  and clears H+0x111.

### Verification

`//tools/reference-game/invitro:differential_mfd_test` (`--mfd`,
`mfd_cases.h`): frame, helpers, pages and HDD on synthetic worlds against
the original's machine code. Every callee that is not ours is a recorder
hashed with the bytes behind every pointer and the current context's 0x208
bytes at each call (the clock, part_condition, panel_visible, delete,
`0x48349c`, blink, fill, rotated blit, both labels, panel copy, blit, page
switch, executor, radio panel, port, status body, HDD page draw and the
four vtable slots); DATA, the world and EAX of every helper are compared.
The clock, part_condition and panel_visible are scripted near their
thresholds. The drawing primitives are recorders, so the call stream with
its bytes is compared, not the rendered pixels. Confirmed in vitro; not
checked live.

### Callees with their own sections

- HDD: the button executor `0x44a1a0` and its leaves `0x44a748`,
  `0x44d3cc`, `0x44d24c` ([the HDD button
  executor](#the-hdd-button-executor-and-its-leaves)); the radio panel
  `0x44b620` ([below](#the-radio-panel-0x44b620)); the HDD status page body
  `0x450c7c` ([the status and target
  draws](#the-mfd-status-and-target-draws-the-hdd-status-body-the-radio-records-and-the-entry-steps));
  the HDD key handler `0x449ff4` (hook `hdd_key`).
- MFD pages: the status page draw `0x43a5a0` and the target draw
  `0x43fe44` (the same section), the camera page frame `0x44060c`
  ([below](#the-camera-page-frame-0x44060c)). The MFD press `0x446844` is
  the hook `mfd_button`; the key handler `0x4469e8` is [below](#the-mfd-keys-0x4469e8).
- The map page frame and keys `0x44c988` and `0x44cc68` are hooks
  (`core/src/hdd_map.c`, `core/src/hdd_map_keys.c`). The radar page
  (`0x43f2d8`, `0x43eef4`, `0x43eedc`) is in [the scroll zones and the
  radar page](#the-scroll-zones-and-the-radar-page).
- The title `0x44a704` is the row in `mfd.c`; `hud_draw.c` calls it
  through the dispatch.

## The HDD button executor and its leaves

Own code in `sim/src/mfd.c` (fragment `sim/dispatch/services/hdd_buttons.h`,
16 rows). Every function's comment names its listing range of the
reference DBSIM.EXE.

### Contract

- `0x44a1a0(H)` (`hdd_execute`), the button executor the HDD frame runs.
  The request is the sign-extended byte `H+0x528` (set by `0x44a174`); -1
  does nothing. With byte `H+0x76` set (the HDD minimised) only requests 0
  and 1 run; any other request returns and stays pending. Otherwise the
  case runs and `H+0x528` becomes `0xff`; requests above 14 (unsigned) run
  no case. GC (`[0x6c5f70]`) is read at entry. The 15 cases (jump table
  at `0x44a1f7`), with p = `[H+0x109]`, S = `[H+0x101]`, X = `[H+0x105]`:
  - 0/1: `hdd_page(H, 0/1)`.
  - 2/3: p 0 scrolls the map down/up (`0x44d0c0`/`0x44d124`, the vertical
    twins of `0x44cff8`/`0x44d05c`: every operand 4 bytes further on) and
    marks `H+0x10f`; p 1 steps the status mode (`0x450bf4`/`0x450c18`) and
    marks `H+0x10e`. A mark also sets byte `H+0xb0` to 2.
  - 4/5: p 0 scrolls left/right; p 1 steps the target
    (`0x44ba08`/`0x44b9b0`, result into `H+0x524`) and runs the lamp
    block: lamp byte `H+0x51f` follows "target set and its byte `+0xa3`
    set"; a change fills `H+0xf1` with colour `[0x4d3b96]` (off) or
    `[0x4d3b9a]` (on) when that colour is nonzero, and copies the
    rectangle (third dword + 1) to the panel.
  - 6/7: p 0 only, zoom in `0x44cf90` / out `0x44cfc4`. 8: nothing. 9: p 1
    only, status next.
  - 10..12: p 0, or p 1 after `hdd_page(H, 0)`: radio j = i - 10 unless
    it is the selection, its record `[0x4d03c8 + 4j]` is null or its byte
    `+0x99` is set: `0x44a748(H, j)`, then the cursor `0x433e3c(2)` when
    byte `S+0x103` is set.
  - 13: p 0 only. Byte `S+0x104` clear: the label `0x44dc6c(S, 0, 1)`.
    Otherwise commit `0x44db9c(S)` and, with a selection,
    `wingman_order([H+0x14], 0x4d03d4, ECX)`, where the record's word is
    `S+0x5d` and its point/target come from `0x44db4c`. The third
    argument's low word is `[H+0x517]`; its high word is `0x44db4c`'s
    exit ECX (the original only replaces CX). Then the cursor (0),
    `order_cancel(S)` and `0x44a748(H, -1)`.
  - 14: `order_cancel`, cursor (0), `0x44a748(H, -1)`.
- `0x44a748(H, i)` (`hdd_select`): the old lamp goes out (its slot `+4`
  runs either way), `H+0x52b` takes the old selection, `H+0x517` takes i.
  For i != -1 the new lamp comes on, and the DialogPort (`0x433158`, slot
  0) receives a message of 0x16 bytes at `EBP-0x18`. The original writes
  only word `0x22` at `+0` and the radio record at `+2`. Bytes `+6..+0x15`
  are whatever was on the stack; `0x434e8c` copies them into the queue
  entry. No reader of them was found (the listing of `0x435d0c..0x4360ff`
  and the reads in `hud_widgets.c`). The own code writes them as 0 (a
  zero-filled frame), the only difference from the original, and an
  unavoidable one: the original's bytes depend on the caller's earlier
  stack use. Last `wingman_select([H+0x101], [H+0x517])`; its registers
  are the exit registers.
- `0x44d24c(M, src, dst)` (`map_point_screen`): dst = src + the map
  rectangle's origin + half its size, as read-modify-writes of dst, so
  aliasing behaves as in the original. Exit EAX M, EDX dst, ECX the last
  half (the computed ECX that reaches callers).
- `0x44d3cc(M, gadget, pt)` (`map_drag_start`): the drag record at
  `M+0x14d..+0x15b`, minus the negated shifts `[0x4d2556]/[0x4d255a]`;
  the HDD `[[0x49d6b0]+8]` gets marks `+0x10f` and `+0xb0`. Exit EAX the
  HDD, EDX = ECX = `-[0x4d255a]`.
- The helpers' exit registers, which callers in the executor chain read,
  are reproduced: `0x44db9c`, `0x44db4c` (all three, per path), the
  target steps (the recursion's registers, ECX = H after a repeated
  step), `0x44dd14` (EAX/EDX canvas_pop's, ECX the colour), the status
  steps (EAX n). Each function's comment in `mfd.c` gives them.
- `0x44dc6c` with a null text, byte `S+0x103` set and a map mode outside
  `0xb..0xe` indexes `0x4d1638` with the entry EAX, which the original
  never sets on that path. The row takes the entry EAX; the executor
  passes 0, the value the original's EAX holds there.

`0x449ff4` (HDD keys) is the hook `hdd_key` and `0x446844` (MFD press) the
hook `mfd_button`; they need no row. The name
`map_to_screen` already names `0x44c250` (`hud_draw.c`), so `0x44d24c`'s
row is `map_point_screen`.

### Verification

`//tools/reference-game/invitro:differential_mfd_rest_test`
(`--mfd-rest`, `mfd_rest_cases.h`) on synthetic worlds built on
`mfd_cases.h`'s HDD world: the executor runs unpatched on side 0, with
recorded callees `hdd_page`, `status_mode`, the fill (rectangle and fill
record), panel copy, port, the port's receive (the 6 written bytes), the
buttons' slot `+4`, `wingman_select`, `wingman_order` (all three dwords
and the record `0x4d03d4`), `order_cancel`, canvas push/pop, label (the
text), both cursor setters, zoom bar and zoom out. Compared: recorder hash
and count, DATA, the world (fill record pointers masked) and the exit
registers per function as above. Confirmed in vitro; not checked live.

### The radio panel `0x44b620`

- `0x44b620(H)` (`hdd_radio`), run by the HDD frame. It calls `0x4632c4()`,
  then reads the clock (site `0x44b631`). For each radio record k < 3
  (S = `H+0x12d + 0x14e k`) that has an actor `[S+0x137]`:
  - It clears the speaker `[[0x49d230]+0x766]` when the speaker is that
    actor and either byte `S+0x147` is set or the state `[S+0x13b]` is 0.
  - It then acts on the state:
    - 0: when the previous state `[S+0x13f]` or byte `0x49d4f4` is set,
      it draws `0x44aea0(H, k)` and copies to the panel `0x43122c(1,
      S+0x1d, 0)`. Byte `0x49d4f4` takes the copy's AL xor 1.
    - 3: it waits for `[S+0x143]` (unsigned, frame `0x44b3dc(H, S)`), then
      goes to state 0.
    - 1: it waits for `[S+0x143]` and for byte `S+0x147` to clear (frame
      meanwhile), then goes to state 2.
    - 2: on entry, the port `0x435b14` is told, and the voice test
      `0x463400` (AL) decides whether the lip value `0x46356c` is read
      (-1 otherwise). A 0x25 record sets `S+0x143` = now + 5. If the
      state was already 2, only the lip value is read. When the lip value
      is not word -1, it runs `0x44b344(H, S)` (0x25 records) or
      `0x44b148(H, S, e)`. Otherwise it calls the port's `0x435b38`.
      0x25 records then go to state 1 with byte `S+0x147` set. Others go
      to state 3 with `S+0x143` = now + 0x14, the sound `0x462830(0x1c)`
      unless `0x462af0(0x1c)` reports it playing, and byte `S+0x147` set
      when `[0x4d03c8+4k]+0x99` is set. Both then run the frame.
  - It ends by setting `S+0x13f` to `[S+0x13b]`.
- `0x44aea0`, `0x44b3dc`, `0x44b344` and `0x44b148` are called through
  the dispatch (rows in [the status and target
  draws](#the-mfd-status-and-target-draws-the-hdd-status-body-the-radio-records-and-the-entry-steps)).
- Verification: `differential_mfd_rest_test` part "radio". Every callee
  above is a recorder with scripted returns: the clock next to `S+0x143`;
  AL of the voice test and of the copy; the lip value's low word -1 or
  not; the sound state.

### The camera page frame `0x44060c`

- `0x44060c(X)` (`mfd_camera_frame`, MFD page slot +4) does nothing when
  byte `[0x4cf99c]+0x1c` is set. Otherwise it runs these steps (the code
  comment in `mfd.c` has the exact read and write order):
  1. Fill `[X+4]`, using the fill record `{0, [0x4d3ba2]}` in the frame,
     which goes into GC+0x22c.
  2. Copy into the view V = `[X+0x10]` the rectangle `X+0x18..+0x27`,
     less GC's origin, then GC's 0x208 context bytes. Then `gc_select(V)`
     and `camera_current`.
  3. Write to the camera `[X+0x14]` the player's point with z = 200000
     and the word `[0x49c454]`. Then `view_setup(C)`.
  4. Call `map_terrain` with `(C & 0xffff0000) | word [P+0x10]`. The high
     word is C's, left over from the listing's POP ECX.
  5. Set V+0x20c to 1, with the fill record `{0, [0x4d3b9c]}` in V+0x228.
     Call `0x483a04` twice with the shifted axis points of
     `[0x4d253a]`/`[0x4d253c]`.
  6. Call `gc_select(GC)`, then the panel copy of `[X+4]`.
- `0x483a04` is called through the dispatch (row in `fill_layer.h`).
- Verification: `differential_mfd_rest_test` part "camera". The recorders
  hash:
  - V's selected context, 0x208 bytes;
  - the camera's 0x16 bytes;
  - both points of each `0x483a04` call, and V's fill record.

### The MFD keys `0x4469e8`

- `0x4469e8(G, key)` (an MFD vtable slot; hook `mfd_key`, [cockpit.md](cockpit.md#mfd-keys)) is the
  MFD's key handler. Listing `0x4469e8..0x446c36`, with the byte table
  `0x446b78` and the jump table behind it.
- If `panel_visible 0x431410(G+0xeb)` returns AL = 0, the handler returns
  EAX 0.
- Otherwise it switches on the key's low word. The page is
  p = `[G+0xc1]`, and the button table is `0x49cbd8 + 13 p`:
  - `0x20`, `0x213`, `0x214`, `0x2d`: if the table byte for button 7, 8,
    9 or 10 is set, call `button_press 0x438d9c(G, n, 1)`. For `0x213`
    with the byte clear, call `mfd_range 0x446ff0(G)` instead.
  - `0x33` / `0x34`: on page 1, call `0x447070(G)` / `0x44703c(G)`, then
    set byte `G+0xe5+p`. p is read again after the call. On any other
    page the key is still taken (AL = 1).
  - Any other key: only on page 1, and only these keys call
    `0x447158(G, [G+0xfb], n)`:

    | Key | n |
    |---|---|
    | `0x12`, `0x2e` | 4 |
    | `0x18` | 3 |
    | `0x1e` | 0 |
    | `0x21` | 5 |
    | `0x22` | 1 |
    | `0x23` | 2 |

    Every other key, and every key on another page, returns EAX 0.
- A taken key returns AL = 1. The upper bytes of EAX are whatever the
  listing leaves there: the callee's EAX; the page for `0x20`, `0x214`
  and `0x2d` when no button is pressed; panel_visible's EAX for `0x213`,
  `0x33` and `0x34`.
- `0x447070`, `0x44703c` and `0x447158` are rows in [the status and
  target draws](#the-mfd-status-and-target-draws-the-hdd-status-body-the-radio-records-and-the-entry-steps),
  called through the dispatch.
- Verification: `differential_mfd_rest_test` part "keys", with the full
  EAX compared. The six callees are recorders in this part, and
  panel_visible's AL is scripted; keys with the high word set and pages
  0..6 are included.

## The MFD status and target draws, the HDD status body, the radio records and the entry steps

Method: the `llvm-objdump` listing of a scratch copy of `DBSIM.EXE` (the
verified file from `es2\build\extracted\cd`), with REA used read-only on
the same copy. Every claim below is confirmed in vitro only (see
Verification); nothing has been validated live.

### What is own code (`sim/src/mfd.c`, fragment `mfd_status_draws.h`, 10 rows)

| VA | Row | Slots | EAX |
|---|---|---|---|
| `0x44703c` | `mfd_entry_up` | 1 | the new index |
| `0x447070` | `mfd_entry_down` | 1 | the new index |
| `0x447158` | `mfd_order_key` | 3 | the callee's, or the order |
| `0x44aea0` | `radio_draw` | 2 | untouched |
| `0x44b3dc` | `radio_frame` | 2 | untouched |
| `0x44b148` | `radio_line` | 3 | untouched |
| `0x44b344` | `radio_talk` | 2 | untouched |
| `0x43a5a0` | `mfd_status_draw` | 1 | untouched |
| `0x43fe44` | `mfd_target_draw` | 1 | untouched |
| `0x450c7c` | `hdd_status_body` | 2 | untouched |

The draw rows leave EAX as the dispatch hands it over. Their values in EAX
are not compared in vitro.

### Contract

- **Entry steps `0x44703c`/`0x447070`(G).** Starting from i = [G+0xb1],
  step to the next (up: 6 wraps to 0) or previous (down: -1 wraps to 5)
  entry whose byte E+0x2c+i (E = [G+0xd1]) has bit 0 clear. Then
  `0x4470a4(G, i)`. EAX = i.
- **Order key `0x447158`(G, o, n).** If o = [G+0xfb] and the page
  [G+0xc1] is 1, it calls `0x4470a4(G, n)` (EAX from that call) and sets
  byte G+0xe6. Otherwise EAX = o.
- **Radio records** (H the HDD gauge, S = H+0x12d+0x14e k, V = [0x4d03c8 +
  4k]):
  - `0x44aea0` draws record k when H+0xc1 is visible. If byte V+0x99 is
    set it goes to the frame. Otherwise it fills the record's rectangle
    S+0x1d less the 640x480 offsets, then draws five labels: the name, the
    class text `0x44ae1c` and the order symbol `0x41bac8`.
  - `0x44b3dc` draws the explosion strip frame byte S+0x12e (which
    advances mod 5), rotated through `0x45caa4` at 0x49d240/0x49d244.
  - `0x44b148`(H, S, e) draws the talking bitmap. When byte S+0x133 is
    set, a stream-B `rng_next` call is made whose value only feeds a dead
    read, and the bitmap comes from e directly. Otherwise it comes through
    the word table 0x49d1f6.
  - Both of the last two give the HDD view the bitmap and the point when
    S's name [S+0x137] is the HDD's [H+0x766].
  - `0x44b344` alternates line and frame on the clock (site `0x44b350`).
    A state change re-arms S+0x143 = now + max(5, sx16
    `rng_bounded`(B, 20)) (site `0x44b3ae`).
- **Target draw `0x43fe44`(X).** It runs in this order:
  1. The blink: [0x49c398] against X+0x45, and the clock against X+0x46.
     While blinking it toggles X+0x20, fills `0x4d1a44` and returns.
  2. The target [0x49c394] is searched in the list [0x4a96c2] with
     `0x471c70`. If it is not found, [0x49c394] is cleared.
  3. If found:
     - The point t+0x26 goes to the locals in the order x, y, z at
       EBP-0x60/-0x5c/-0x58. It is moved by 500 along the angle through
       `0x47dff0`/`0x47e0c0`, then stored to X+0x21.
     - The view V = [X+0x10] is set up from the GC and the camera C =
       [X+0x14].
     - The scene is drawn (`0x42841c`, `0x46cb9c`, `0x428140`) with
       [0x4d24ea], [0x4aaaa8] and [0x4aaaac] swapped and restored.
     - The cross hairs `0x483a04` and the ellipse `0x48817c` are drawn.
  4. Without a target: the fill and the lock labels `0x440370`. The
     flashing colour depends on the clock bit 5 (site `0x4402bb`).
- **Status draw `0x43a5a0`(X).**
  1. The frame fill.
  2. The 300-tick hold of a dead target (X+0x39/+0x3a, clock sites
     `0x43a659`/`0x43a668`).
  3. The target kind (word O+0x1a8):
     - Kind 0 (a mech) is the main case:
       - the hull bitmap;
       - the part values `0x41b4ec`;
       - the name: self, a wingman via `0x44b928`, or `strupr` of the
         type name;
       - per part, the average of the values its type sums (the jump
         tables at `0x43a8fe`/`0x43a99d`, picked by the damage flag
         D2+0x50), passed through `0x438624` to the bar `0x437e94`;
       - the condition state from byte O+0x92+7, the twelve values at
         buf+0x28 and byte O+0x92+0x1e, given to slot +0x40 and
         `0x43b288`;
       - the highlight of the selected part (the map `0x43acd5`).
     - Kinds 1, 2 and 3 are a centred bitmap and labels.
     - Any other kind gives labels only.
  4. The range label when byte [O+0x45]+0x12 is 1: `strcat` of
     `0x49be26` when hidden, else `0x49288c` → `0x434228` → `itoa`.
- **HDD status body `0x450c7c`(X, draw).**
  - Minimised (byte H+0x76): a fill and an optional panel copy.
  - No target (H+0x524 = 0): the legend `0x44ba54` and the label
    H+0x55e, drawn only when the target or the mode changed.
  - Otherwise, per mode X+0x7c:
    - 0: 19 parts, tables 0x4d13e4/0x4d1430;
    - 1: 12 parts, tables 0x4d147c/0x4d14ac;
    - 2: the weapons, with names `0x40e18c` copied into 0x4d1d38 and the
      table 0x4d1d10.
  - The values are compared with the old ones at X+0x94 by `memcmp`.
  - On a change it redraws the hull, the parts `0x451de0` and the bars,
    then draws per visible row (X+0x84 .. +0xd, limited by X+0x90 and
    [L]) the part bitmap or bar, the name and the percentage labels,
    coloured by the condition class {1, 3, 9, 2, 7}[state].
  - The old values are copied back. The canvas driver slot [0x4a57ec] is
    called while [0x6c5ed0] is the driver.

### Refusals (the own code stops; the original faults or runs on)

- The entry steps when none of the six entries is free: the original
  loops forever. The own code refuses after 64 steps. Unreachable: the
  menu (the page-1 object of `0x43f600`) clears the flags of all six
  entries at `0x43f799`; the bit-0 setters `0x43fa1c`/`0x43fa3c` have no
  reference and the `xor` at `0x43fa0e` toggles bit 1 only.
- The target list walk refuses after 65,536 steps (a cyclic list).
- `0x43a5a0`:
  - a part whose type sums no value: the original divides by 0 at
    `0x43aae2`;
  - no part at all: it divides by 0 at `0x43ab79`;
  - a range text of 0x100 bytes or more, which would overflow the own
    frame.

  The two divisions are unreachable by data: every view-2 status selector
  of the 22 vehicle PDGs has a non-empty status offset list for its DAT
  flag byte (`+0x4e`, runtime `+0x50`): flag 0 allows {0, 4..8, 13, 14},
  flag 1 {0, 4..8}.
- `0x450c7c`:
  - a mode above 2;
  - the mode changed above 2 by a callee during the draw;
  - a condition state above 4 before any class was set.

  In each of these the original reads undefined locals. All three are
  unreachable. `X+0x7c` starts at 0 (`0x450a30`) and is written only by
  `0x450b88` with 0, 1, 2 or its own value (`0x450db3`, `0x451dc8`) and by
  the cycle `0x450bf4`/`0x450c18`, which stays in 0..2. `0x438624` returns
  states 0..4 for the part kinds 0..3 (above, it returns the entry EDX),
  and every PDG part has kind 0.

### Verification

`//tools/reference-game/invitro:differential_mfd_rest2_test`
(`mfd_rest2_cases.h`, `--mfd-rest2`) runs the original against the rows:
entries, radio, target, status and HDD. Compared: every callee through
recorders on both sides, including the clock and stream-B sites, the fill
records behind GC/canvas/view +0x22c/+0x228, and the label texts; DATA;
both worlds; the entry and order-key EAX. The suite's case counts are kept
low enough for the loaded full test run (a larger count timed out at
600 s). The target point locals are stored in the order x at EBP-0x60, y
at -0x5c, z at -0x58 (upwards); this suite catches the downward order.

### Open (not exercised in vitro)

- The refusal paths above. Open by the listing: whether the target list
  can become cyclic. The range text (the pointer `0x4d14f4` has no direct
  writer) is string 2 of group 20 in `strings0.str`, 7 to 8 bytes in every
  language, and every string group fits its `0x437598` table
  (`domain_hud_strings.py`).
- The "keeps the previous class" branch of `0x450c7c` (a state above 4
  after a class was set).
- `0x487acc`, `0x486ee8`, `0x46bc44` (rows in `gouraud_fill.h`),
  `0x48cce4` and `0x42e8e8` (rows in `shade_ranges.h`) are called through
  the dispatch.

## The MFD pages, the HDD map and status page, and the map's button and icon leaves

Every constructor of the MFD pages, the HDD map with its icons and the HDD
status page is own code inside the rows `mfd_ctor` and `hdd_ctor`
(`src/hud_cockpit.c`), so none gets a row of its own. Nine small leaves
that own code reaches are rows (fragment `sim/dispatch/services/hdd_map_leaves.h`,
bodies at the end of `sim/src/mfd.c`): the ones the `hdd_orders` and
`hdd_map` hooks bridge, and the method-table targets of the map icon and
the drag widget.

### Verdicts

| VA | What | Verdict |
| --- | --- | --- |
| 0x43f600, 0x43faf4, 0x4404bc | the MFD orders, target and camera pages | own code inside the row `mfd_ctor` 0x445240 (`src/hud_cockpit.c`) |
| 0x44c28c | the HDD map page | own code inside the row `hdd_ctor` 0x448cf0 |
| 0x44f158 (with 0x44bebc inlined) | a map icon (method table 0x49d9b0) | own code inside the map page constructor of `hdd_ctor` |
| 0x44f678 | the map drag widget (method table 0x49d944) | own code inside `hdd_ctor` |
| 0x4507c4 (with the base 0x448cd4) | the HDD status page | own code inside `hdd_ctor` |
| 0x44d288 | the map's point-to-cell step | computed inline by its callers: the `map_pick` hook (`core/src/map_pick.c`) and the own icon draw in `src/hud_draw.c`; no row |
| 0x44ede0 | the button lamp | row `map_lamp` |
| 0x44ee00, 0x44ee24 | the eight button bytes on and off | rows `map_buttons_on`, `map_buttons_off` |
| 0x44d370 | the map tick | row `map_tick` |
| 0x44f5d0, 0x44f630, 0x44f5f4 | the map icon's slots +4, +8, +0xc | rows `icon_mark`, `icon_release`, `icon_frame` |
| 0x44f644 | the wingman button's blink | row `map_blink` |
| 0x44f6d4 | the drag widget's slot +8 | row `drag_press` |

The method-table slots were read as dwords from the reference DBSIM.EXE. Table
0x49d9b0 holds 0x44f1bc, 0x44f5d0, 0x44f630, 0x44f5f4, and the drag widget's
table 0x49d944 holds 0x44f6d4 at +8. The constructors were matched to their own
code by the VAs that `src/hud_cockpit.c` names.

### Row contracts

All addresses are from the `llvm-objdump` listing of the reference DBSIM.EXE.
All nine rows are cdecl. "The caller's" register means the register the row was
entered with.

- **`map_lamp` 0x44ede0(M, i)** (0x44ede0..0x44edfc). B = [[M+0x31] + 4i].
  Byte B+0x36 is XORed with 1. Then B's slot +4 ([B+0x17]+4) is called with (B).
  On entry to that call, EAX is B, EDX is i and ECX is the method table.
  Exit registers: EAX and EDX are the slot's, and ECX is the argument slot as
  the slot left it (`pop ecx`).
- **`map_buttons_on` 0x44ee00(M) / `map_buttons_off` 0x44ee24(M)**
  (0x44ee00..0x44ee20, 0x44ee24..0x44ee44). Bytes M+0x131..M+0x138 are set to 1
  (on) or 0 (off), in ascending order. Then byte M+0x139 is set to 1 in both.
  Exit registers: EAX M+0x139, EDX 8, ECX M.
- **`map_tick` 0x44d370()** (0x44d370..0x44d3cb):
  - bytes 0x49d6ac and 0x49d6ae are set to 1, and byte 0x49d6ad is XORed with 1;
  - G = [[0x49d6b0]+8]: byte G+0x10f is set to 1 and byte G+0xb0 to 2;
  - M = [0x49d6b0], read again: if word M+0x13e is non-zero, it is
    decremented; when it reaches 0, M is read again and byte M+0x144 is set to 1.
  Exit registers: EAX M; EDX and ECX the caller's.
- **`icon_mark` 0x44f5d0(I)** (0x44f5d0..0x44f5f1). Byte I+0x1f is set to 2.
  Then byte G+0x10f is set to 1 and byte G+0xb0 to 2, with G as in the tick.
  Exit registers: EAX G; EDX and ECX the caller's.
- **`icon_frame` 0x44f5f4(I)** (0x44f5f4..0x44f62f). panel_visible 0x431410(I)
  is called with the caller's registers. Nothing more happens unless AL is set
  and byte I+0x1f is 2. If both hold:
  1. byte I+0x37 is set to 1;
  2. I's slot 0 is called with (I), entered with visible's EAX (AL is 0
     there), EDX the method table and ECX the popped argument;
  3. byte I+0x37 is set to 0;
  4. the panel copy 0x43122c(1, I, 0) is called, entered with the slot's
     EAX/EDX and ECX the popped argument;
  5. byte I+0x1f is set to 0.

  Exit registers:
  - after the full path: the panel copy's EAX, EDX and ECX;
  - on an early return: visible's EAX and EDX, and ECX the popped argument.
    If AL was set, AL is byte I+0x1f minus 2.
- **`icon_release` 0x44f630(I, f)** (0x44f630..0x44f641). If bit 0 of f is set,
  byte I+0x37 is set to 0. Exit registers: EAX I; EDX and ECX the caller's.
- **`map_blink` 0x44f644(B)** (0x44f644..0x44f658). If byte B+0x37 is 0, B's
  slot 0 is called with (B), entered with EAX the method table and the caller's
  EDX/ECX. Exit registers:
  - if B+0x37 is set: EAX B;
  - after the call: the slot's EAX and EDX, and ECX the popped argument.
- **`drag_press` 0x44f6d4(W, f)** (0x44f6d4..0x44f6f3). If bit 0 of f is set,
  map_drag_start 0x44d3cc([W+0x28], W, 0x4d1de3) is called (row in `mfd.c`),
  entered with EAX [W+0x28] and the caller's EDX/ECX. Exit registers: EAX W, or
  the drag start's EAX, EDX and ECX.

### Verification

`//tools/reference-game/invitro:differential_hdd_map_leaves_test`
(`invitro/hdd_map_leaves_cases.h`): a synthetic world (the map page and its HDD,
sixteen buttons and four icons with every state byte, the countdown word
at 0, 1, 2, 0xffff or 0x8000, the drag widget with [W+0x28] the page, the
blink and scroll globals), then 1..8 random row calls with random entry
EAX, EDX and ECX. The original side runs through a register thunk, with
0x44d3cc for real; panel_visible 0x431410, the panel copy 0x43122c and the
slot targets are recording stubs on both sides that log their entry
registers and arguments and leave seeded registers. Compared: EAX, EDX and
ECX of every call, DATA and the heap, the stub log, an empty coupling
trace. Confirmed in vitro; not checked live.

### Notes

- 0x44d370 and 0x44f644 are bridged by the `hdd_map` hooks
  (`HDD_MAP_CALL`) but do not appear among sim_gap.md's called roots; the
  script probably does not resolve that macro's token paste.

## The radio panel and the HDD leftovers

Own code: `sim/src/hud_radio.c`, 14 service rows in
`sim/dispatch/services/radio_panel.h`, all CDECL with EAX.

Each contract is the comment above its function in the file, with its
listing range. Each was written from the `llvm-objdump` listing of a scratch
copy of `DBSIM.EXE` (SHA-256 `8561aa47…c4ad`, checked on the copy) and
checked in vitro (below). The method tables were read as dwords from DATA of
the same copy:

- `0x49d12c` = {`0x4480c8`, `0x448b58`, `0x4483a0`, `0x4481c4`, …}, the vertical throttle slider;
- `0x49d0e8` = {`0x448660`, `0x4556ec`, `0x43901c`, `0x448738`, …}, the horizontal one;
- `0x49d60c` = {`0x44bb60`, `0x455540`, `0x44be78`, `0x44be18`, …}, the HDD button;
- `0x49cee0` = {`0x450524`, `0x450529`, `0x447658`, …}, the MFD select gadget;
- `0x448b58` is also the dword at `0x49d1a0`.

### Rows

| VA | Row | What it does |
| --- | --- | --- |
| `0x44ae1c` | `radio_class` | the condition class 0..4 of a record's actor: the mean of the 19 words that `condition_record` (hook `0x41b4ec`) writes at buf+2, `x = ((256 - mean) * 100) sar 8`, bounds 90/74/51/1 |
| `0x44aff0` | `radio_voice` | the voice of a radio message: builds the two names (`0x434260`'s number, the record's letter, the message's word and byte zero-padded) in 12-byte locals, `0x492bec`/`0x45ef9c` for the paths, `0x462b9c` for the voice, stored at S+0x12f; AL set when it is nonzero |
| `0x44b514` | `radio_start` | the radio port's start callback (installed through `0x4355a8`): the voice; then the timer S+0x143 (clock site `0x44b559` + 0x14), the beep `0x1c` unless it plays, `hdd_radio`, the HDD's speaker when it has none, S+0x14a; without a voice the port's drop and next steps |
| `0x44b5e8` | `radio_end` | the port's end callback (`0x4355dc`): clears byte S+0x1c of the speaker's record |
| `0x44b864` | `radio_context` | canvas push; [0x4d1cc8] <- the current context; the radio context's clip from S+0x1d less the context offset and the 640x480 offset; `gc_select` |
| `0x44b920` | `radio_context_end` | canvas pop |
| `0x44b928` | `radio_actor` | [record w + 0x137] (the first argument unused) |
| `0x44ba54` | `hdd_legend` | the HDD legend label: colour and text by the mode word H+0x55c (0, 4, else `0x434280(m - 1)`), then `label` with [H + 0x548 + 4m] |
| `0x44be18` | `hdd_button_redraw` | slot +0xc: by state 1 (kinds 10..12 through slot 0, else a panel copy) or 2 (slot 0), then the state decremented |
| `0x44be78` | `hdd_button_press` | slot +8: unless pressed and of kind 0 or 1, toggles B+0x40 and calls the owner's slot 0, its own slot +4 and the event base's slot +8 |
| `0x447658` | `mfd_select_press` | slot +8: with bit 0 of the flags, `mfd_click([W+0x24], W, 0x4d1de3)` (hook) |
| `0x4481c4` | `throttle_redraw_v` | slot +0xc, vertical: with state 2 and visible, restores the knob's background, takes the value, moves the knob, flips the driver page when the view is still current, saves the new background, draws the knob (flag 2 right of `0xa0 shl [0x4d253a]`) and copies the damage rectangle |
| `0x448738` | `throttle_redraw_h` | the same for the horizontal slider (flag 0, the axes swapped) |
| `0x448b58` | `throttle_dirty` | slot +4: byte W+0x1f <- 2 |

The comments state these facts and the suite checks them:

- Every pointer that the original reads again after a call is read again:
  the HDD gauge `0x49d230` after `hdd_radio`, `[0x6c5ed0]` after the knob
  save, and the rectangle record `0x4390b8`, fetched again when it wins.
- The context and the view of the throttle redraw are the ones at entry.
- The voice start builds its names in the original's order.

The rows' only coupling site is the clock call at `0x44b559`. `0x434260`
(row in `hud_mfd_leaves.h`) is called through the dispatch.

### Verdicts for the rest of the row

| VA | Verdict | Evidence |
| --- | --- | --- |
| `0x44730c`, `0x447444`, `0x4475fc` | inlined in own rows (`hud_cockpit.c`, the MFD buttons) | only callers `0x445623` and `0x445669` (`mfd_ctor` `0x445240`), and `0x4459a6` (`0x445699`, inlined there) |
| `0x447678`, `0x4488f4`, `0x44bef8` | own code (the element scale table in `hud_cockpit.c`) | only callers `0x4324e7`, `0x43256b`, `0x432653` (rows `element_mfd`, `element_throttle`, `element_hdd`) |
| `0x448cd4` | inlined (the HDD page base) | callers `0x44c2ae` and `0x4507e3` (`0x44c28c`, `0x4507c4`: own code inside `hdd_ctor` `0x448cf0`) |
| `0x44bad4` | inlined (the HDD button) | only caller `0x449225` (`0x4490ab`, inlined in `hdd_ctor`) |
| `0x44bebc` | inlined (the map icon) | only caller `0x44f168` (`0x44f158`, own code in `hud_cockpit.c`) |
| `0x4476f0` | own code | exit record x15 in `init.c`; its only reference is in DATA |
| `0x447919` | inlined | only caller `0x445237` (`mfd_object` `0x445234`, which `hud_draw.c` inlines) |
| `0x44b950` | dead | only caller `0x4342d4` in `0x4342c0`, which no call and no dword references |
| `0x44bac4` | inlined | only caller `0x45e8ee` (`0x45e758`, own code in `startup.c`, option `t`) |
| `0x44aea0`, `0x44b148`, `0x44b344`, `0x44b3dc`, `0x44b620`, `0x44b9b0`, `0x44ba08`, `0x44bb60`, `0x4480c8`, `0x448660`, `0x447380`, `0x447410`, `0x4474d0`, `0x44750c`, `0x4475c8` | rows elsewhere, reused | `hdd_buttons.h`, `mfd_status_draws.h`, `hud_load.h` |

Closed `sim_gap.md` roots:

- called: `0x44ae1c`, `0x44b864`, `0x44b920`, `0x44b928`, `0x44ba54`;
- virtual: `0x447658`, `0x4481c4`, `0x448738`, `0x448b58`, `0x44be18`, `0x44be78`;
- referenced only: `0x44b514`, `0x44b5e8` (and `0x44aff0` behind `0x44b514`).

### Verification

`//tools/reference-game/invitro:differential_hud_radio_test`
(`hud_radio_cases.h`), four parts on seeded synthetic worlds: radio
(`0x44b514`, `0x44b5e8`, `0x44aff0`, `0x44b928`), helpers (`0x44ae1c`,
`0x44b864`, `0x44b920`, `0x44ba54`), button (`0x44be18`, `0x44be78`,
`0x447658`, `0x448b58`) and slider (`0x4481c4`, `0x448738`). Side 0 runs
the original's machine code with every callee patched to a recorder; side
1 runs the rows with recorder rows at the same VAs. The recorders hash
their arguments and strings (a stack address as a mark plus its bytes);
itoa and strcat do the work; the canvas push may switch `[0x6c5f70]`, the
knob save `[0x6c5ed0]`, and `hdd_radio` may clear the speaker; `0x4390b8`
returns scripted records, so the re-reads above are exercised. Compared:
calls, DATA, heap, world, and EAX where the contract defines it (the
button slots' and the throttle redraws' EAX are not part of it; the
callers ignore it). Confirmed in vitro; not checked live.

Not covered: voice names longer than 11 characters. The original overruns
its 12-byte locals into its frame there; the own code refuses.

## The lower counts, the extra render root and the radio panel's second caller

The rows whose entry counts are lower with the hooks replaced than with
every hook original (the open points of [the coupling set
measured](#the-coupling-set-measured) and of
[dbsim_map.md](../reference-game/dbsim_map.md) "Map with all hooks replaced").
No difference from the original's behaviour is found.

**Method.**

- **Listing.** `llvm-objdump` listing of the verified `DBSIM.EXE` (SHA-256
  `8561aa47…c4ad`, [reference_version.md](../reference_version.md)). The
  static call graph comes from the listing: function starts from
  `probe_instruction_inventory.full_inventory`, direct calls, tail jumps,
  immediate pushes, and dword references in the data sections (vtable slots).
- **Counts.** Two maps: `<es2-data>\logs\dbsim-map-20261012\map.json`
  (every hook original, map **O** below) and
  `<es2-data>\logs\dbsim-map-20261020\map.json` (all hooks
  replaced, map **R** below); per-site counts from
  `<es2-data>\logs\t58-20261020` (`out/t58.json`, the site and
  function counts; `batch-t58/<suite>/probes.json`, the entry counts).
- **The 27 rows** are the rows present in both maps where, in some suite,
  R < O < 1000, i.e. O is unsaturated (R < O with R <
  1000 gives 41). Every "count equals" claim below holds over all 40 suites
  of both maps, saturated suites (N = 1000) skipped.

**Caveat for every comparison between maps.** Each batch of a map is a run of
its own, so two rows of one map can come from different runs.

Several pairs must be equal within one run, yet differ inside one map:

| Pair (must be equal in one run) | Map, suite | Counts |
|---|---|---|
| `0x45cfc8` vs `0x457480` | O, c3_10-detail-options | 889 vs 899 |
| `0x45cfc8` vs `0x457480` | R, c3_10-detail-options | 902 vs 897 |
| `0x464260` vs its only caller `0x454acc` | O, c4_08-joystick | 299 vs 296 |
| `0x473898` vs `0x464038` (single call `0x4640e6`) | R, c5_05-orders-flyer | 66 vs 63 |
| `0x495b2b` vs `0x4737d8` (unconditional call `0x4737e8`) | R, c1_06-cheat-disable | 393 vs 394 |
| `0x495e64` vs `0x473a5c` (at most one call, `0x473a81`) | O, c5_05-moon-missiles | 173 vs 105 |

So the number of modal-dialog passes, pumped messages and sound-driver
answers varies from run to run in the same configuration:

- by up to 10 passes in the options suites;
- by tens of calls in the sound driver.

This matches [oracle_regression.md](../reference-game/oracle_regression.md) "Pump-keyed answers": the replay counts
`PM_REMOVE` calls, and asynchronous SOS/MCI messages fall between them. The
compared state does not depend on it, which is why all suites are identical
to their goldens in both maps.

### The 27 rows

**Group 1: the caller is now own code (6 rows).** The difference is exact
in every unsaturated suite.

| Row | Original callers (call site) | Own code | Check |
|---|---|---|---|
| `0x406ee4` | `0x4076e4` light_pools: directly at `0x40773c` and through `0x406e44` (`0x406eb8`, reached at `0x407711`) | `es2_light_pools_registers` (`core/src/mission_load.c`) keeps the guest call to `0x406e44` and computes the direct `406ee4(table, 10, 2000)` inline | R = O / 2 in 40/40 suites (2 → 1) |
| `0x407048` | `0x407604` light_create (`0x407645`), `0x4076a0` light_level (`0x4076b8`) | `0x4076a0` is own code with `0x407048` inlined (`core/src/logic.c`); light_create still calls the guest (`core/src/effects.c`) | O = O(`0x407604`) + O(`0x4076a0`) in 37/37; R = O(`0x407604`) in 40/40 |
| `0x40735c` | `0x406f38` (`0x406fac`), `0x407048` (`0x407071`), `0x406ee4` (`0x406f2d`; the sums leave no calls for it) | follows `0x407048` | O = `0x406f38` + `0x407048` (37/37), R likewise (40/40) |
| `0x44d288` | `0x44d888` map_pick (`0x44d89d`); `0x44e334`, `0x44e954`, `0x44f1bc` | map_pick inlines it (`core/src/map_pick.c`) | O − R = O(`0x44d888`) in 38/38 (5 in c3_09-loadout-b, 6 in c3_09-wingman-orders) |
| `0x47a8a4` | `0x472f64` stream_destroy (`0x473016`), and 11 other call sites (e.g. `0x40fb13`, `0x47ad9a`) | stream_destroy runs the base destructor inline (`core/src/asset_load.c`, `VA_BASE_DTOR`) | O − R = O(`0x472f64`) in 40/40 (acceptance-60s: 282 − 21 = 261) |
| `0x47a904` | slot `+0x10` of vtable `0x4a16e8` (dword `0x4a16f8`), called in `0x47a8a4` at `0x47a8bf` | inlined with `0x47a8a4` (`VA_BASE_SLOT`) | count = `0x47a8a4` in both maps, 40/40 |

**Group 2: modal-dialog passes (pump-keyed message timing, 10 rows).** Each
of these rows counts a pass of a modal loop, or work done once per pass.
Its callers are all guest code in both maps. Each delta is 1 to 4 calls, up
to 6 for the terrain walker, in a suite with a dialog or message box. Each
row is also higher in other suites. Every delta is within the run-to-run
variation shown above.

| Row | What it is, how it is reached | Lower in |
|---|---|---|
| `0x454acc` | per-pass update of the modal loops: `0x456000` (`0x456060`), `0x45866c` (`0x458c5a`), `0x457b00` (`0x457b4d`) | keys-extra, scan, loadout-hercs (1–4); higher in 4 |
| `0x454e2c` | base dialog key handler, slot `+0x10` of the dialog vtables (dwords `0x49e044`, `0x49e130`, `0x49e1f4`, `0x49e3f4`, `0x49e4e0`), once per pass at `0x456039`, `0x456e86`, `0x458818` | 5 suites (1–4); higher in 4 |
| `0x455080` | gadget slot (dwords `0x49dfb8`, `0x49e000`), per gadget per pass through `call [edx+0xc]` at `0x45604f`, `0x4571c6` and directly at `0x4558d8` | 3 suites (1–4); higher in 4 |
| `0x45719c` | per pass of the options run loop `0x456d68` (`0x4570b5`) | options-dialog 1, detail-options 2 |
| `0x457480` | per pass of `0x456d68` (`0x4570ae`), calls the idle callback | the same |
| `0x45cfc8` | the options dialog's idle callback ([below](#the-extra-render-root-caller-in-the-options-dialog-suites)) | options-dialog 2; higher in 1 |
| `0x464260` | called from `0x454acc` (`0x454aee`) while `[0x49f8ec]` ≠ 0 | 5 suites (1–4) |
| `0x4809f8` | mouse message handler (messages 5, `0x200`–`0x202`, `0x204`, `0x205`), installed by `0x480694` through handler_add `0x465fec` (push at `0x48069c`) | options-dialog 2; higher in 33 |
| `0x472504` | terrain draw walker, called from `0x46d1a8` (`0x46d2cf`) under the render root | detail-options 6 |
| `0x474eb8` | TSPoly draw (vtable dword `0x4a5ed0`) under the render root | detail-options 4 |

The last two follow the render-root count, which in c3_10-detail-options
follows the dialog's idle passes ([below](#the-extra-render-root-caller-in-the-options-dialog-suites)).

**Group 3: Win32 message traffic (3 rows).** These are the import thunks
`0x49697c` DispatchMessageA (IAT `0x6cc44c`), `0x496988` DefWindowProcA
(`0x6cc454`) and `0x4969b8` TranslateMessage (`0x6cc474`). They are called
from the pumps `0x464c78` and `0x4659e8` and from the window procedure
`0x466034`. Each is lower by 9 only in c3_10-detail-options and higher in 32
to 33 suites. These are message counts, not state.

**Group 4: sound-driver answers (8 rows).** Own `core/src/sfx.c` calls the
same guest entry points the original calls: `0x464038`, `0x464118` and
`0x464618`. Every row below is reached only through guest code, and its
count depends on what the driver answers at wall-clock time.

| Row | Role | What the count depends on |
|---|---|---|
| `0x464038` | play; caller `0x462830` "play if not playing" (e.g. radio panel `0x44b57f` after `0x462af0(0x1c)`) | answer of `sosDIGISampleDone`. −1 in own-building-fire and orders-flyer; higher in 4 |
| `0x464618` | set volume, in the same play path | the same 2 suites; higher in 1 |
| `0x464118` | stop; callers `0x463c50` and the is-done query `0x4641e8` | done answers. 3 suites; higher in 5 |
| `0x473898` | start glue, single caller `0x464038` (`0x4640e6`) | follows `0x464038`. 1 suite (own-building-fire); higher in 3 |
| `0x4739ac` | stop glue; callers `0x464118`, `0x4648e0` | follows `0x464118`. 1 suite; higher in 3 |
| `0x495e64` | SOS thunk, caller `0x473a5c` (done query, `0x473a81`) | c3_09-loadout-b 7, c5_05-moon-missiles 41, never higher; within-map contradiction above |
| `0x495b2b` | SOS thunk, unconditional call in `0x4737d8` (`0x4737e8`). `0x4737d8` runs per dialog pass (`0x464260`) and through `0x4639e8` ← `0x462398` | dialog passes. c1_06-cheat-disable 1; higher in 1 |
| `0x474324` | returns `[0x4a0e50]`, the last driver status that `0x473f14`…`0x4742db` store | called at `0x473990` only when the start thunk `0x495e7c` (a call through the pointer `[0x6cbe28]`) returns nonzero |

`0x474324` is the one row that drops systematically: 26 suites lower,
never higher, e.g. c5_05-moon-missiles 857 → 419. The listing does not
place own code on its path:

- `0x464038`, `0x473898` and `0x495e7c` have equal counts in both maps in
  32 of the 38 unsaturated suites and differ by at most 7 in the others
  (c1_01-success: 714 everywhere; c3_09-orders-matrix: 659/659/658 against
  662/665/661), so the same starts take the same branch of `0x473898`.
- What changes is how often the driver's start call answers nonzero.

That answer is driver state (channel occupancy, wall time), not
reconstructed state; it follows the wall time per tick ([the driver
answers](#the-driver-answers-follow-the-wall-time-per-tick)).

### The extra render-root caller in the options-dialog suites

The caller is the options dialog's idle callback `0x45cfc8`, called once per
pass of the options run loop:

- `0x45cfc8` calls `0x401c74(camera, −0x40, 0, 0)`, camera_update
  `0x4011a0`, and then render_root `0x45fca8` at `0x45cfe9` (return
  `0x45cfee`).
- The dialog `0x45cff0` installs it: `push 0x45cfc8` at `0x45d2a7`, then
  `0x45746c`, which stores the pointer at `+0x3ba` (`0x457475`).
- The run loop `0x456d68` calls `0x457480` once per pass (`0x4570ae`).
  `0x457480` calls `call [ebx+0x3ba]` at `0x4574d1`.

This completes the oracle_suites.md note that "each iteration runs
`render_root` once more". `0x45cff0` itself calls render_root around the
dialog:

- Before it, at `0x45d1d8`/`0x45d256` (in a loop while `[0x4cf99c]+0x1c` is
  set) or once at `0x45d295` (view 4).
- After it, at `0x45d3f0` and `0x45d414`, or once at `0x45d436`.

That is at least 3 calls per opening, and both suites open the dialog twice
(O and R count `0x45cff0` = 2). The static callers of render_root are
listed in the table below.

| Caller (call site) | Count in the dialog suites |
|---|---|
| frame loop `0x45f160` (`0x45f49b`) | — |
| `0x45d85c` (4 sites) | — |
| mission_root `0x461608` (`0x461df1`) | — |
| `0x45cff0` (6 sites) | at least 3 per opening, 2 openings |
| `0x45cfc8` (`0x45cfe9`) | 590/592 (options-dialog), 889/902 (detail-options), in O/R |

The per-site run gives the totals (`probes.json` entries; 699 ticks):

| suite | `0x449bf8` (frame walk) = `0x44b620` entries | − ticks | idle passes left after ≥ 6 dialog frames | idle passes in O / R |
|---|---|---|---|---|
| c1_01-options-dialog | 1297 | 598 | ≤ 592 | 592 / 590 |
| c3_10-detail-options | 1611 | 912 | ≤ 906 | 889 / 902 |

The rate of 1.85 to 2.3 per tick is therefore one render root per tick, plus
one per idle pass, plus the dialog's own open and close frames. The
palette cycler `0x4306ac`, which runs once per presented frame, stays at 706,
because the idle callback renders without presenting.

The second dialog of c3_10-detail-options (action 9, `0x457d38`, run loop
`0x45866c`) calls `0x454acc` once per pass, and `0x454acc` = 0 in the per-site run: no
such pass ran in that run. The larger rate there comes from more idle
passes, not from a second caller.

Confirmed in one run with every hook original (count mode, evidence
`<es2-data>\logs\t-58b-probes-20261023`, `summary.md`):

| Suite | `0x45fca8` | from `0x45cfee` | `0x45cfc8` | `0x457480` | `0x45cff0` |
|---|---:|---:|---:|---:|---:|
| c1_01-options-dialog | 1299 | 590 | 590 | 590 | 2 |
| c3_10-detail-options | 1598 | 889 | 889 | 889 | 2 |

The other returns are the per-tick caller `0x45f4a0` (700), the dialog's
own frames `0x45d25b` (4), `0x45d3f5` (2), `0x45d419` (2), and `0x461df6`
(1): 700 + idle passes + 9. `0x45d1dd`, `0x45d29a` and `0x45d43b` were not
seen.

### The radio panel's caller outside the frame walk

`0x44b620`, the radio panel with its clock read at `0x44b631`, has two
static callers:

- the frame-walk draw `0x449bf8` (vtable dword `0x49d654`, call `0x449cf3`);
- `0x44b514`, at `0x44b58c`.

`0x44b514` is a message-start callback of the cockpit's message port:

- **Registration.** In cockpit construction `0x448cf0`:
  - `push 0x44b514` (`0x4498dd`), then `0x4355a8` on the port that
    `0x433158` returns (`[[0x49b088]+0x207]`, the port that `radio_notice`
    and `trigger_fire` post to).
  - `0x4355a8` appends the pointer to a list of at most 2 entries at port
    `+0x4b9`, with the count at byte `+0x4b7`.
  - Next to it, `push 0x44b5e8` → `0x4355dc` fills a second list at `+0x4c1`
    (count `+0x4b8`). The port erase `0x436130` calls that list
    (`0x436170`).
- **Firing.** The port's frame step `0x435610` (`hud_port_step`, MsgPort
  slot `+0x10`, called once per tick from the port frames `0x4361cc` and
  `0x436f70`; the third caller `0x4365d0` does not run) calls the `+0x4b9` list at `0x435908` with the current message
  `[+0x49a]`. It does so once per message: when the elapsed time passes the
  message's `+0x24`, with latch byte `+0x4c9` (`0x4358d5`…`0x435919`).
  The frame walk does not reach this path.
- **Effect.** `0x44b514` then:
  1. starts the wingman voice (`0x44aff0`);
  2. reads the clock (`0x44b559`);
  3. queries row `0x1c` (`0x462af0`, `0x44b573`) and plays it if it is idle
     (`0x462830`, `0x44b57f`);
  4. redraws the panel through `0x44b620` (`0x44b58c`).

**Counts (the per-site run).** In 38 of the 40 suites, `0x44b631`
run − ticks equals `0x44b573` run, the one unconditional call of
`0x44b514` before `0x44b58c`. The two exceptions are the options-dialog
suites, where the extra calls are the idle render roots (above). Exactly 15 suites have `0x44b573` > 0:

- c1_01-cheat-disable-late, c1_01-cheat-leader, c1_01-cockpit-keys,
  c1_01-collision, c1_01-joystick, c1_01-success, c1_01-tape-record;
- c3_09-loadout-b, c3_09-orders-matrix, c3_09-wingman-orders;
- c3_10-hunt-targ, c3_10-loadout-hercs, c3_10-own-building-fire;
- c5_05-moon-missiles, c5_05-orders-flyer.

The extra calls range from 1 to 64 (orders-matrix: 4263 − 4199 = 64). In
the two dialog suites, `0x449bf8` = `0x44b620` = 1297/1611 entries: there
the panel is reached only through the frame walk.

This is decided by the listing and the per-site counts; no probe is needed. The
caller is the message port's start callback (one per started message), not
a second frame path.

### Result

- The 6 group-1 rows are own code that inlines the original's calls.
- The extra render root and the radio panel's second caller are original paths that are not in the frame walk.
- The other 21 rows are dialog passes, message traffic and driver answers,
  within the run-to-run variation of one configuration; `0x474324` follows
  the wall time per tick (below).

### The driver answers follow the wall time per tick

With every hook original the driver start `0x495e7c` and `0x474324` count
714/541 and 714/548 in c1_01-success (two runs) and 1149/938, 1150/924 in
c5_05-moon-missiles; with all hooks replaced 714/327 and 1149/546. The
start is called equally often; its nonzero answers fall with the hooks
replaced, by more than the variation between original runs. The tick logs
are identical in every run (`<es2-data>\logs\t-58b-probes-20261023`).

Bisecting the replaced hooks (count mode, probes `495e7c,474324`,
c1_01-success, hooks `[lo,hi)` of the registry order replaced, the rest
original; every run identical to the golden, `0x495e7c` = 714 in every
run; per-tick wall time from the QPC stamp of each tick record):

| Hooks replaced | Bytes per tick record | Tick ms, median / mean | `0x474324` |
|---:|---:|---|---:|
| 0 | 1 074 | 1.166 / 1.196 | 541 |
| 99 `[296,395)` | 17 244 | 1.531 / 1.593 | 498 |
| 197 `[395,592)` | 26 864 | 1.575 / 1.607 | 497 |
| 197 `[592,789)` | 33 062 | 1.588 / 1.631 | 497 |
| 99 `[197,296)` | 19 510 | 1.598 / 1.654 | 494 |
| 197 `[0,197)` | 28 031 | 1.721 / 1.762 | 481 |
| 198 `[197,395)` | 35 471 | 1.857 / 1.894 | 468 |
| 394 `[395,789)` | 58 644 | 1.966 / 2.027 | 447 |
| 395 `[0,395)` | 62 215 | 2.450 / 2.555 | 391 |
| 789 | 119 564 | 3.223 / 3.294 | 318 |

Verdict: timing, no behaviour difference.

- Sorted by mean tick time, the count falls strictly; hook count orders the
  runs less well (the 99-hook runs sit between the 197-hook runs).
- No group carries the drop: the quarters' drops (−60, −73, −44, −44) add
  up to −221, against −223 for the whole set.
- The cost per tick grows with the number of replaced hooks: every tick
  record carries the replaced hooks' `hk_*` counters (about 120 KB per tick
  with all 789).
- The driver start `0x495e7c` runs on the driver's own clock, so its
  nonzero answers depend on the wall time between the game's starts.

Consequences: counts of driver answers (`0x474324`, the done query
`0x495e64`) are comparable between runs only at equal tick time;
count-mode differences in these rows between all-original and
all-replaced maps are not evidence of own behaviour.

Evidence: `<es2-data>\logs\t-58c-20261023` (`summary.md`,
`tick-timing.json`, `probes-summary.*` and the runner JSONs per run).

## Open questions

Questions that belong to one function are listed in its section; this
list collects the rest and points to the others.

- The meaning of the player bytes `P+0x96` (toggled by R), `P+0x9a`
  (gates the cockpit target in `0x411e80`), `P+0x9b`, `P+0x9d`, and of the
  display mode values of `[0x4cf99c]+0x14`.
- The MFD caller `0x446fb0` (outside the region) is not classified.
- The HUD line: the meaning of `0x4d1f3a`/`0x4d1f3b` and of the actor
  byte `+0x95`, the not-live sites, the cursor in DirectDraw mode ([the
  HUD line contract](#open)).
- What `0x434e8c` reads beyond the first word of the EventPort records of
  `0x432924` and `0x4349ac` (stack residue in the original, zero in the
  own process; [the row-1
  walk](#the-map-draws-tail-the-load-roots-last-steps-the-radar-draw-and-the-per-frame-row-1-walk)).
- `0x435920` (the port box overlap test) has no static reference and no
  row ([the message queue](#the-message-queue-the-ports-and-the-gauge-blink)).
- The bar/gauge family of the widget leaves (vtable slots
  `0x49bd00..0x49bd64`, the constructors `0x43907c`, `0x4390c4`,
  `0x439344`, `0x439834`, the slots `0x4391e0`, `0x439460`, `0x4396a8`,
  `0x439858`, `0x439918`, `0x439b8c`, the helpers `0x439a0c`, `0x439c48`)
  has no rows; `0x438df8` is inline only ([the live widget
  leaves](#the-live-widget-leaves)).
- The vtable `0x49c1c4` entry read as `0x43e65f` is probably an RTTI field
  and needs checking before it gets a row ([the slot 1
  thunks](#the-slot-1-thunks)).
- The inline gauge and gadget constructors have no in-vitro comparison of
  their own ([gauge and panel gadget
  constructors](#gauge-and-panel-gadget-constructors)).
- The refusal paths of the MFD status and HDD status draws are not
  exercised in vitro; all but the target list walk and the range text
  length are unreachable ([the status and target
  draws](#open-not-exercised-in-vitro)).
- Voice names longer than 11 characters in `0x44aff0` (the original
  overruns its 12-byte locals; the own code refuses).
- A region-level frame comparison of the HUD pixels needs a capture with
  pixel data or a region hash ([the HUD's pixels](#frame-comparison)).
- Whether the counters' formatting or the own code costs the extra wall
  time per tick with all hooks replaced ([the driver
  answers](#the-driver-answers-follow-the-wall-time-per-tick)); an
  all-replaced run with the counters off would decide it.
- None of the service rows of this document has been checked live; their
  evidence is in-vitro equality on synthetic inputs, the unit tests and
  the listing.

## The HUD's remaining virtual targets

This section covers the HUD entries of the `sim_gap.md` section "Virtual:
targets of the function-pointer tables that sim/src names" that have
neither a row nor a hook. None of them gets a row; each is refused. The
reading method, the own slot-call helpers and the SIM, R3D and FILE entries
are in [own_process.md, "Remaining virtual
targets"](../reference-game/own_process.md#remaining-virtual-targets).

### Refusals

| VA | Table (slot) | Reason | Evidence |
|---|---|---|---|
| `0x437583` | `0x49ba7c`, `0x49baa8`, `0x49bad4` (7) | no `+0x1c` call on a port | The function returns P+0x4a7. Own port calls (display.c `port_call`) use only `+0x14` and `+0x18`. The original's 28 `call [reg+0x1c]` sites are none on a port: `0x4418b6` is on the pod gauge; `0x4429b6`, `0x442b41`, `0x447e8a`, `0x4480a8`, `0x448426`, `0x448640`, `0x45510a` and `0x455346` are on the gadget parts B+0x17. No `+0x1c` is loaded into a register for a later call |
| `0x438f14` | `0x49d5a4` (2) | base table overwritten | Each of the three writers of `0x49d5a4` writes the derived table a few instructions later: `0x442c7a` then `0x49c758` (hud_cockpit.c `range_toggle`), `0x447498` then `0x49cf20` (`mfd_button`), `0x44bb28` then `0x49d60c` (the HDD button). Between the two stores only byte B+0x40 and the counter change. Its thunk `0x44c0d8` (`add [esp+4], -0x20; jmp 0x438f14`) is slot 3 of the partner table `0x49d5c8`, which is overwritten in the same way |
| `0x4391e0`, `0x439460`, `0x4396a8` | `0x49bd60`, `0x49bd48`, `0x49bd30` (1) | slot 1 of a bar never called | Slot 1 is the bar's full redraw through `0x439758`. The bars sit at the throttle's +0x7e/+0x82, the energy pool's +0xb5 and the gauge fields built by `0x4395c4`, `0x439344` and `0x4390c4` (hud_cockpit.c, hud_gauges.c). Own code calls a bar only at slot 0 (panel.c `pn_pool_frame`) and through the non-virtual `0x4390b8` and constructors. Own `+4` calls go to the panel set and the later elements (hud_display.c `panel_draw`), the HUD list G+0xd7 (hud_frame.c: gadgets added by `hud_add`), the gauge itself (hud_gauges.c), dialogs, widgets, panel.c H+0x1e9, streams and scene objects. In the original's throttle code (`0x447694`..`0x44896a`) the bar fields are read directly or through `0x4390b8` |
| `0x43a190`, `0x43a211` | `0x49bd48`, `0x49bd18`; `0x49bd60`, `0x49bd30`, `0x49bd00` (2) | slot 2 of a bar never called | Slot 2 resets B+0x20 by the sign of B+0x14. No own `+8` call is on a bar: the own `+8` calls are on dialogs, HUD pages, the cockpit HUD, widgets, GLBitmapArray elements and streams |
| `0x4506b5` | `0x49d984` (6) | stack object, torn down inline | The only writer, `0x44f330` (map icon draw, after `0x4a3040` at `0x44f321`), builds the object at ebp-0x14, uses it non-virtually (`0x482cec`) and drops it with the counter (-3) without a destructor call. hud_draw.c does the same. Only the RTL unwinder reads the RTTI reference `0x450681` |

`0x43a190` and `0x43a211` are also slot 2 of the derived bar tables
`0x49bd18` (`0x439834`) and `0x49bd00` (`0x439ae8`). Their slot 1
(`0x439918`, `0x439c40`) is not part of this section; `0x439c40` is the
row `empty_439c40` (`mission_load.h`).

The evidence is the `llvm-objdump` listing of the reference DBSIM.EXE and
every slot call in sim/src. The live map has 0 hits for each of these
functions. This is a static reading; it has not been checked in vitro or
live.

## The ENERGY pod gauge creator 40e40c

`0x40e40c` is slot `+0x64` (the panel gauge creator) of the ENERGY class
table `0x498a84` ([weapon_classes.md](weapon_classes.md), ID 32). Its only
reference is the relocated dword `0x498ae8`; its only caller is the
indirect `call [ecx+0x64]` at `0x410660` in `weapon_gauges` `0x410644`
(own: hud_cockpit.c `es2_sim_service_weapon_gauges`, `HVCALL(x, 0x64)`).
It is the row `pod_gauge_new_40e40c` (`sim/dispatch/services/pod_gauges.h`)
on the own code of its four siblings (hud_gauges.c
`es2_sim_service_pod_gauge_new`).

### Contract

Listing `0x40e40c..0x40e433` (0x28 bytes, cdecl, one argument W):
`ebx` ← W, `esi` ← `[0x49b088]`; `0x4321d4(esi, movsx byte [[W+0x18]+7],
W)`; W+0x79 ← EAX; EAX stays the gauge G. The bytes are identical to those
of the four other pod creators except the class:

| Slot `+0x64` | Class (ID) | Creator | Factory case (kind word W+0x77) |
|---|---|---|---|
| `0x498cc8` | ECM (18) | `0x40e3bc` | 0x12: new(0xde) → `0x4419c0` |
| `0x498bd8` | TARG (29) | `0x40e45c` | 0x1d: new(0xde) → `0x44154c` |
| `0x498b60` | SHIELD (30) | `0x40e434` | 0x1e: new(0xde) → `0x44154c` |
| `0x498c50` | TURBO (31) | `0x40e3e4` | 0x1f: new(0xe6) → `0x441a5c` |
| `0x498ae8` | ENERGY (32) | `0x40e40c` | 0x20: new(0xde) → `0x44154c` |

The factory `0x4321d4` and the three constructors are described in
hud_gauges.c next to `pod_factory`. Slot `+0x64` of the other weapon tables
is `0x40e0e0` or `0x40e20c` (rows) or `0x496748`; `0x40e40c` was the only
slot `+0x64` target without a row.

### Why no suite reached it

The live map (`es2-data` `logs/dbsim-map-20261020/map.json`) has no hit of
`0x40e40c` in its 40 suites; `0x40e3bc`, `0x40e3e4`, `0x40e434` and
`0x40e45c` are hit in 12, 13, 6 and 1 suites, which is why they have rows.
No recorded suite builds the panel of an ENERGY item. DEMO2's player herc
does: the first own call of `0x40e40c` is its panel set-up (`0x410644` → `panel_ctor` `0x431bf8` →
`display_ctor` `0x429660`).

### Why sim_gap.py does not list it

`dbsim_map.py` lists `0x40e40c` as a function start (prologue `55 8b ec`,
sources `frame_pointers` and `pointer`, 18 instructions, one call to
`0x4321d4`). "unmapped" in the refusal means only that the dispatch has no
entry for it. `sim_gap.py` misses it for four reasons together:

- its roots are constant call sites and bridges; the only caller is the
  vtable call at `0x410660`, which the script does not follow;
- the own caller is the dynamic `HVCALL(x, 0x64)`, which names no VA;
- virtual targets come only from tables whose VA sim/src names, and sim/src
  names no weapon class table (`0x498a84` and its neighbours);
- sim/src does not mention `0x40e40c`, so it is not in the "referenced" list
  either.

### In vitro

`differential_pod_gauges_test` (`pod_gauges_cases.h`, built on
`gauges_cases.h`) calls one of the five creators on a random weapon (the
five kinds, slots 0..9 and -1 with and without a free panel entry, the flag
byte, the name from the table or the type, fresh or loaded bitmaps, the
allocations of G, of the damage animation and of the toggle failing) with
the factory, the constructors, `0x440834`, `0x442204` and `0x452c6c` as the
original's code against the rows through the full dispatch: 3000 cases
identical, every coverage slot non-zero. The mutation `bar + 0x30 ← 0x21`
in `pod_gauge_energy` fails it. Kinds without a pod gauge are not compared:
the original files an unset local, the own code refuses at `0x43231a`.

### Headless `-D` runs

CD install, `-eggplant -X2 -R0 -G -D`, frame limit above the demo length:

| `[run] time` | Demo | Ticks | Exit |
|---|---|---|---|
| 852076801 | DEMO2 | 947 | 6 (demo end; `WinMain` returned), no refusal |
| 852076802 | DEMO3 | 3349 | 6 (demo end), `0x45a810` read all 3660 tape entries |
| default 852076800 | DEMO1 | 920 | 70, refusal at `0x4530bc` (ninth pending press of the GeneralAlert) |

In DEMO2 `0x45a810` runs 953 times (the tape has 1901 entries) before the
demo ends; the
GeneralAlert `0x456000` runs once with three presses.

DEMO1 ends in the pending-press list of the GeneralAlert (`0x49e120`) at the
mission end. In each modal pass the tape's control record `0x4d22c6` has the
click byte +0xd set (DEMO1's click run from entry 919 is 32 entries long), so
the focused gadget 0 is pressed; `0x4530bc` adds an entry with release time
clock + 10 and `dialog_step` `0x454acc` holds the end back while the list is
not empty. Under the virtual clock the clock advances by about half a unit per
pass (entry times `0xa95..0xa98` over eight passes), so no entry expires.
`0x4530bc` compares the count word `+0x396` with 7 without a jump; a ninth
entry would land on the count word and `release_tick` `0x4530fc` would walk
past the array. The original faults there live at `0x453127` after 920 frames
of `c1_01-tape-demo` ([oracle_suites.md, "Tape playback
suites"](../reference-game/oracle_suites.md#tape-playback-suites)), so the
state is the original's, not the clock's. es2sim refuses the ninth append
(and a scan with a count above 8) with exit 70 instead of reproducing the
fault. Outside the dialogs the HUD's shield keys reach the same overrun
through the hook `0x4530bc`, in the outside view, where the release scan
does not run ([cockpit.md, "Open"](cockpit.md#open)); there the hook
writes as the original does and the scan refuses ([main_loop.md, "The modal dialogs in the own
process"](main_loop.md#the-modal-dialogs-in-the-own-process)).

### Open

- DEMO2's full weapon list has not been read out; the ENERGY item is
  inferred from the call of slot `+0x64` of `0x498a84`.
- None of these rows has been checked live.
