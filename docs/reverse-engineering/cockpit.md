# Cockpit gauges, the HDD map, the MFD and the message queue

The cockpit panel's gauges (shields, throttle, MFD, HDD and its map,
radio panel), the HDD map pick and order controls, the MFD order path and
the cockpit message queue: the original's behaviour, addresses, layouts
and registers, and the own code that implements each function. The panel
model, the HUD and the message ports as a whole are in [hud.md](hud.md);
this document uses its legend and object names.

## Method and evidence

- Read from the bytes of the reference `VER95/DBSIM.EXE` (image base
  `0x400000`, see [reference_version.md](../reference_version.md)) with `llvm-objdump -d
  --no-show-raw-insn -M intel` from the pinned LLVM-MinGW, plus reads of
  initialized DATA (file offset of DATA = `0x96200`) for vtables, Borland
  RTTI names (dword at `vtable − 12` → type descriptor, name offset word
  at `+6`, as in hud.md) and constants.
- Role names are **hypotheses** unless stated otherwise.
- Live call counts are the saturated entry-probe counts of the DBSIM map
  (order
  a60/c1s/c1c/c3_09/c4_08/c3_10, 1000 = at least 1000; "no row" = not in
  the probe set). Key counts of the suites come from the input scripts in
  `tools/reference-game/input-scripts/*.jsonl`.
- In-vitro tests compare the own code against the original's machine code
  (`tools/reference-game/invitro/`); mutations caught by them are in
  [MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md). Results
  that only the in-vitro tests show are marked "in vitro; not
  checked live".
- Hooks and their adapter groups are listed in
  [hooks.md](../reference-game/hooks.md#registry); the hooks below run in
  the oracle with every hook replaced and the suites identical.

## Legend

As in [hud.md](hud.md#legend): **D** draws, **U** presentation state,
**P** pure helper, **S** changes simulation state, **Q** cockpit message
queue and ports; +B stream-B draw, +C clock wrapper `0x467828`, +A audio,
+H game heap `0x47749c`. Added here: **I** input/UI logic whose result
feeds a simulation write elsewhere (the function itself does not write
simulation state).

## Objects

| Object | Class (RTTI) | Constructed | Fields used here (offsets; meanings hypotheses) |
|---|---|---|---|
| G = `[H+0x1e9]` | `ShieldsGauge` (vtable `0x49ca1d`: slot 0 `0x443834`, 1 `0x443758`, 2 `0x443770`, 3 `0x45236c`) | `0x432454` (`0xd4` bytes from `0x47749c`, ctor `0x443524`), called from the cockpit constructor `0x431bf8` at `0x431e85`; never entered after the proxy arms | `+0x18[2]` the two buttons, `+0x68` button count; `+0x8c` powered, `+0x8d` power-on sweep done, `+0x90` power-on clock (`0x438ddc`); `+0xb0` redraw counter (2 = redraw now and next frame); record `+0xb5..+0xc3` (15 bytes): `+0xb5` front value, `+0xb9` rear value (shield ×1024 / maximum), `+0xbd` balance, `+0xc1` byte (passed through), `+0xc2`/`+0xc3` balance-up/down requests; `+0xc4`/`+0xc8` the two number labels; `+0xcc`/`+0xd0` sweep targets |
| buttons `G+0x18[i]` | `ShieldsSelectGadget` (vtable `0x49c9dd`: slot 2 = widget click `0x438e3c`) | `0x444b14` from the gauge ctor (`0x40` bytes); owner G stored at `+0x24` | state word `+0x1b` |
| palette | object `[0x49b020]` | `0x430394` (load through `0x465f7c`, installed at `0x49b020`; [dts.md](../formats/dts.md)) | `+0x8` entry array (dwords); `0x49b028`/`0x49b02c` dirty range, byte `0x49b030` dirty, `0x49b024` last uploaded palette |
| M | `HDDMap` (vtable `0x49d9d8`: slot 0 `0x44c8bc`, 1 `0x44c988`, 2 `0x44cf50`, 3 `0x44cf10`, 4 `0x44cc68`) | ctor around `0x44c2b6`; three `0x2a`-byte records at `M+0x85` built with `0x49667c(M+0x85, 0x2a, 3, 1, 0x450617)` | `+0x14` view object (`+0x1a` shift count), `+0x18..+0x20` map centre (the dword `+0x20` is also the scale), `+0x5d` map mode, `+0x71..+0x7d` screen rectangle, `+0x85 + 0x2a·i` per-wingman order record, `+0x103`, `+0x104` record changed, `+0x105` selected wingman (−1 none), `+0x11c` click pending, `+0x11d` cursor point (2 dwords), `+0x15c` last picked actor, `+0x374..+0x37c` cursor in world coordinates; more fields under [HDD map order controls](#hdd-map-order-controls) |
| Q = port `+4` | message queue of `DialogPort`/`CommandPort`/`EventPort` | [hud.md](hud.md#classification-table) | entries `0x31` bytes at `Q+0`, in-use bytes `Q+0x212[10]`, order list `Q+0x1ea[10]` (dwords, slot indices), count word `Q+0x21c`; full layout in [hud.md](hud.md#layout-from-the-listing) |

**`0x49667c` is the runtime's array-construction helper (from
its bytes).** Arguments: array, element size, count, flags, element
constructor. A null array is allocated through `0x4774f0` (size·count,
plus 4 when flag `0x10` asks for a count prefix, which it then stores);
for a nonzero constructor it calls the constructor once per element,
choosing the calling form by `flags & 7` (unknown forms go to the error
routine `0x49676c`); flag `0x200` restores the dword at `fs:[4] − 4`. It
returns the array. It is RTL, not a numeric helper (its 69 callers are
construction sites). `0x40b804` calls it with a null array (`0x40b897`,
`0x40ba26`: 12-byte elements, count from SI, element constructor
`0x4953c4`), so it allocates from `0x4774f0` there.

## Classification table

| VA | Role | Instr. | Calls per suite | Callers (how reached) | Class | Evidence |
|---|---|---:|---|---|---|---|
| **`0x443918`** | shield colours → palette | 1210 | 176/1000/737/864/600/435 | `0x443770` (slot 2, per frame when `G+0xb0` = 2), `0x443758` (slot 1, full redraw), `0x443524` (ctor) | **D** (palette) | reads `G+0xb5`, `G+0xb9`, DATA `0x49c9cb..0x49c9d0`; 72 × `multiply_q10`; `0x4303c4(0x42, 6, local)`; no other memory write, no clock, no RNG |
| `0x47e0b0` | `multiply_q10` (hook) | 6 | n/p | 72 sites in `0x443918`, many others | P | EAX = low 32 bits of (a·b) >> 10, EDX = high half of the product; [player_movement.md](player_movement.md) |
| **`0x4303c4`** | write palette entries | 43 | 176/1000/737/864/600/435 | `0x429af3`, `0x429b77` (cockpit set-up `0x429834`), `0x430755` (`0x4306ac`, presentation), `0x444a7f`, `0x45d808`, `0x45d823`, `0x45dcf1`, `0x45dd94` | U | `[[0x49b020]+8][first..]` ← src; dirty range `0x49b028/0x49b02c/0x49b030` |
| `0x43048c` | palette flush | 22 | n/p | `0x429bc4`, `0x45fbda`, `0x45fc36` (presentation `0x45fba4`), `0x461dec`, `0x461e08` | D | new palette → `0x481fe0`; dirty → `0x465cc4` (upload, [audio.md](audio.md): presentation); clears the range |
| `0x443524` | `ShieldsGauge` ctor | n/r | no row | `0x432454` | U+H | `PanelGauge` base `0x438b20`; two buttons (`0x47749c(0x40)`, `0x444b14`), two labels (`0x4387ac`); dword `0x4d1a64` ← colour of `0x49c970..72`; record zeroed (`0x49681a(G+0xb5, 0, 15)`, memset); `0x443918(G)` |
| `0x443758` | slot 1: full redraw | 13 | no row | `0x4316c0` (whole panel, from the display-mode switch `0x42a1f0`), `0x4345a0` (`SystemGadget` action), `0x45f13c` (from `0x4667c8`) | D | `0x443918(G)`, `0x444a90(G)` |
| `0x443770` | slot 2: per-frame draw | 36 | 1000×5/729 | panel frame `0x4327ac` (`0x432862`, virtual `+8` of `[H+0x1e9]`) ← rendering `0x45fca8` | D (+C) | [contract](#bar-and-gauge-family-power-on-sweeps-balance-numbers) (`shields_frame`) |
| `0x4437cc` | power-on sweep | 29 | 176/176/176/306/0/176 | `0x443770` | U+**C** | [contract](#bar-and-gauge-family-power-on-sweeps-balance-numbers) (inline in `shields_frame`) |
| `0x443834` | slot 0: button pressed | 31 | no row | `0x438e3c` (button's owner call, `[button+0x24]` slot 0) | U (request) | button 0 → `+0xc2` ← 1, `+0xc3` ← 0; button 1 → `+0xc3` ← 1, `+0xc2` ← 0 |
| `0x443880` | set record | 42 | 1000×5/937 | `0x413bc8` | U | copies 15 bytes to `G+0xb5`; before the sweep is done moves the two values to `+0xcc/+0xd0` and zeroes them; any change of the three dwords → `+0xb0` ← 2 |
| `0x443908` | record getter | 6 | 1000×5/937 | `0x413bc8` | P | EAX = G + `0xb5` |
| `0x444a90` | balance numbers | 49 | 176/1000/737/864/600/435 | `0x443758`, `0x443770` | D | if `0x431410([G+0xc4]+4)`: labels `+0xc4`/`+0xc8` show a = `G+0xbd`·200 >> 10 and 200 − a (`_itoa` `0x496820`, `0x438920`; [contract](#bar-and-gauge-family-power-on-sweeps-balance-numbers)) |
| **`0x413bc8`** | apply balance requests, feed the gauge | 59 | 1000×5/937 | `0x41b130` (`0x41b172`) ← `0x45fca8`, `0x461608` | **S** | hook `shield_balance`; contract below; writes `P+0x226` |
| `0x413af8` | balance step | 21 | no row | `0x413bc8` (2 sites) | **S** | hook `shield_step`; word `S+4` ± `0x66`, clamped `0..0x400` |
| `0x438d9c` | press button i | 27 | 0/4/0/41/14/0 | 9 incl. `0x4157c8` (shield keys) | U+**C** (request) | hook `button_press`; `0x4530bc` calls the clock (release time = clock + 10), and for the shield buttons the owner call sets the request byte read by `0x413bc8` |
| **`0x44d888`** | HDD map: actor under the cursor | 123 | 0/0/0/5/0/0 | `0x44d6e0`, `0x44d82c` (both from `HDDMap` slot 1 `0x44c988` when `M+0x11c` is set) | **I** | hook `map_pick`; contract below; writes `M+0x374..+0x37c` only |
| `0x44d288` | point relative to the map rectangle centre | 25 | 0/0/0/948/0/0 | `0x44d888`, map draws `0x44e334`, `0x44e954`, `0x44f1bc` | P | out = p − rect origin − (rect size >> 1) per axis |
| `0x44d6e0` | map click: order target / point | 96 | 0/0/0/5/0/0 | `0x44c988` | I+**B** | hook `map_click`: by map mode an actor or the point into the selected wingman's record, sound `0x14`, label; [Map click 44d6e0](#map-click-44d6e0) |
| `0x44d82c` | map click: select a wingman | 33 | no row | `0x44c988` | I+Q | hook `wingman_pick`; picked actor not dead (`+0x99`) and a wingman (`0x433134`) → `0x44a748`: selection, DialogPort message `0x22` |
| **`0x434e8c`** | queue insert | 152 | 1/13/3/54/13/17 | `0x435c48` (DialogPort slot 0), `0x4362e4` (CommandPort slot 0), `0x436a3c` (EventPort slot 0) | Q+**C** | contract below; sim row in `sim/src/hud_widgets.c` |
| `0x4351c0` | drop one queued message | 9 | no row | `0x434e8c` | Q | frees slot `order[order[9]]`, count − 1 (see the slips) |

"n/r" = not relevant (constructor); "n/p" = not in the probe set. The
ShieldsGauge vtable methods `0x443758`, `0x443834` and the constructor
were not in the probe set.

## Shield colours `0x443918`

cdecl, one slot (G). Entry EAX/EDX/ECX are not read. Constants (DATA,
read only here; a literal cross-reference shows no other reference to
`0x49c9cb..0x49c9d0`): colour A = (`0x19`, `0x3b`, `0x17`) at
`0x49c9cb..cd`, colour B = (`0x3b`, `0x3b`, `0x17`) at `0x49c9ce..d0`
(channel order of the entry: byte 0, 1, 2). The per-channel differences
B − A are formed as words at entry (`0x22`, 0, 0).

For each value v (first `G+0xb5`, then `G+0xb9`) three entries are
computed, each from a factor t in Q10 (0x400 = 1.0):

- v ≤ 0x400 (signed): u = v; each channel c = q10(t, A.c) if that is > 0
  (signed), else 0 (fade from black to A).
- v > 0x400: u = v − 0x400; c = A.c + q10(t, B.c − A.c) if that sum is
  > 0, else 0 (fade from A to B). With the DATA constants the nine
  "else 0" edges of this branch (per value) are unreachable (A.c > 0,
  differences ≥ 0, t ≥ 0).
- Factors: t_lin = u; t_mid = 0x400 if u > 0x300, 0 if u < 0x100, else
  (u − 0x100)·2; t_late = 0x400 if u > 0x280, 0 if u < 0x180, else
  (u − 0x180)·4 (all signed compares).
- Each channel keeps only its low byte. The entry is the dword
  `0x01000000 | c2 << 16 | c1 << 8 | c0` (bytes c0, c1, c2, 1: the layout of
  a Win32 `PALETTEENTRY` with flag 1 = `PC_RESERVED`; **hypothesis**,
  consistent with the `AnimatePalette` path of `0x465cc4`).

Entries in order: `0x42` = t_late(v1), `0x43` = t_mid(v1), `0x44` =
t_lin(v1), `0x45` = t_late(v2), `0x46` = t_mid(v2), `0x47` = t_lin(v2).
All six locals are written on every path before `0x4303c4(0x42, 6, &local)`
reads them (no uninitialized read). q10 is `multiply_q10` (`0x47e0b0`):
per channel one call, plus a second one with the same arguments when the
first passed the "> 0" test. EDX:EAX at return are `0x4303c4`'s; none of
the three callers reads them (each pops ECX and overwrites EAX/EDX before
use).

Meaning (**hypothesis**): the shield indicator art uses the palette
entries `0x42..0x47` (three bands per side), so the bands light up from
black to green as the shield fills and turn yellow above 100 %. The
function runs once per frame in which the gauge record changed (during
the power-on sweep `0x4437cc`, once per frame).

It is presentation (no clock, no RNG, no heap, no simulation field). A
replacement must keep the palette entries, the dirty range and the order
relative to the flush in `0x45fba4`; the palette is not in the tick log.
Own code: its service row in `sim/src/display.c`; the shields gauge
constructor (`sim/src/hud_cockpit.c`) and the shields draws (`sim/src/panel.c`)
call it through that row ([MFD button draws and the panel
copy](#mfd-button-draws-and-the-panel-copy)).

## Palette entry writer `0x4303c4(first, count, src)`

cdecl, three slots. pal = `[0x49b020]`; copies `count` dwords from `src`
to `[pal+8] + 4·first` (none if count ≤ 0, signed). last = first + count
− 1. If byte `0x49b030` is 0: `0x49b028` ← first, `0x49b02c` ← last,
`0x49b030` ← 1; otherwise `0x49b028` ← first if smaller, `0x49b02c` ←
last if larger (signed). EAX = last; EDX = first (fresh range, or when
first lowered the minimum), else max(count, 0); ECX = src +
4·max(count, 0) (fresh range) or first. No clock, no RNG. Presentation
state.

Readers of the palette object found by a literal cross-reference of
`0x49b020`:

- `0x4303b0`, the palette **setter**: stores its argument in `0x49b020`
  and returns the previous palette; call sites `0x429b6a`, `0x429b80`
  (cockpit set-up), `0x42ea51`, `0x42ea7f`, `0x42eaa3` (the jolt's damage
  palette swap; [display_modes.md](display_modes.md#classification-table)).
- `0x430440`, `0x430474`: unused getters (no call site, no pointer in
  DATA).
- `0x43048c` (flush), `0x4306ac` and `0x430b34` (palette animation and
  fade, clock-driven), `0x430d08`/`0x430e34` (shade ramps for the
  rasterizer, `+0xc`/`+0x10`, not the entry array) and `0x45d548`.

All are palette or rendering code; no simulation reader was found
(**hypothesis** for copies of the pointer, which were not followed beyond
`0x45d548`'s local).

## The shield-balance chain

1. **Request.** Key code `0x1a` or `0x1b` in the vehicle input slot
   `0x4157c8` (`0x41580e..0x415814`, then `0x415ace`): `0x438d9c(G, i, 1)`
   with i = 0 for `0x1b`, 1 for `0x1a`. The keyboard table `0x4a10ec` maps
   VK `0xdd` → `0x1b` and VK `0xdb` → `0x1a` (`]`/`[` on a US layout).
   `0x438d9c` → button virtual `+8` = `0x438e3c(button, 1)` → owner
   `[button+0x24]` = G, slot 0 = `0x443834(G, button)` → `G+0xc2` (button 0)
   or `G+0xc3` (button 1) ← 1, the other ← 0. A mouse click on a button
   reaches `0x438e3c` too (hypothesis: through the panel's widget code).
2. **Apply (rendering frame).** `0x41b130(P)` → `0x413bc8(P+0x222)`: a
   set request moves the balance word `P+0x226` and is cleared.
3. **Feed back.** `0x413bc8` passes front/rear ratios and the balance to
   `0x443880`; on a change the gauge's redraw counter makes the next panel
   frame call `0x443918` (colours) and `0x444a90` (numbers).
4. **Use (logic frame).** `energy_accumulate(P+0x222, …)` (`0x413b38`,
   hook) moves the front shield word toward q10(`S+4`, front + rear)
   ([player_movement.md](player_movement.md)), so the balance is
   simulation state.

None of the six suite scripts presses VK `0xdb`/`0xdd` (no `keydown` or
`WM_KEYDOWN` with those codes). The counter `call_413af8` of
`shield_balance` and the hook `shield_step` count every applied step;
both stay 0 in the suites.

### `0x413bc8(S)` contract (simulation)

cdecl, one slot; S = `P+0x222` (the only caller passes it; ECX = S at the
call, not read). Entry EAX/EDX/ECX are not read.

1. H = `[0x49b088]`; G = `[H+0x1e9]`; `0x443908(G)` → EAX = G + `0xb5`
   (pure getter, EAX only); 15 bytes from there into the local record L
   (L+0 front, +4 rear, +8 balance, +0xc byte, +0xd up, +0xe down).
2. If byte L+0xd ≠ 0: `0x413af8(S, 1)`, L+0xd ← 0. Else if byte L+0xe ≠ 0:
   `0x413af8(S, 0)`, L+0xe ← 0. (Only one request per frame; with both set,
   the down request waits a frame. `0x443834` never sets both.)
3. d = s16 `S+8`. L+0 ← (s16 `S+0` << 10) / d, L+4 ← (s16 `S+2` << 10) / d
   (signed `idiv`, truncation toward zero; d = 0 faults in the original:
   precondition). L+8 ← s16 `S+4` (after step 2).
4. `0x443880([[0x49b088]+0x1e9], &L)` (H and G read again).
5. Result: EAX/EDX/ECX of `0x443880`: EAX = G; if `G+0x8d` ≠ 0: EDX = &L,
   ECX = 0; else EDX = 0, ECX = L+4. The caller pops ECX and clears EAX.

Reads: `[0x49b088]`, `H+0x1e9` (twice), G's record, `S+0`, `S+2`, `S+4`
(twice when a request is applied), `S+8`. Writes: `S+4` (through
`0x413af8`), the local record, and through `0x443880` the gauge (`+0xb0`,
`+0xb5..+0xc3`, `+0xcc`, `+0xd0`). No local is read before it is written
(the local byte L+0xf is never read).

**Callers and pointers.** The only reference to `0x413bc8` is
the call at `0x41b172` in `0x41b130`; `0x413af8` is called only at
`0x413bfe` and `0x413c15`. Neither address occurs as a literal dword (no
vtable or function-pointer use).

**Registers after the call (dead at the caller).** `0x41b177`
pops ECX and `0x41b178` clears EAX. EDX is next written at `0x41b1c8`
(path `[P+0x1a4]` = 0) or, on the other paths, inside `0x49288c` at
`0x49289f`; `0x49288c` and its callees `0x4953c4`/`0x49545b` do not read
EDX or ECX before writing them.

The balance step `0x413af8` is under [Throttle slider event and shield
step](#0x413af8s-up-shield_step).

### Shield balance as own code

Hook `shield_balance` (adapter group `shield`,
`tools/reference-game/proxy/shield_hooks.h`; core `core/src/shield.c`,
`es2/core/shield.h`).

| Original | Own code |
|---|---|
| `0x443908(G)` (pure getter, EAX = G + `0xb5`, no store) | computed in core; no game call |
| 15-byte copy `G+0xb5` → L (dwords `+0`, `+4`, `+8`, word `+0xc`, byte `+0xe`, read/write interleaved as `rep movsd; movsw; movsb`) | same order; L is 16 bytes of the adapter's scratch (byte `+0xf` is never written or read, as in the original) |
| `0x413af8(S, up)` | computed in core: `S+4` ← unclamped 16-bit sum (first store), reread, then the clamped value (second store), as the two `mov word [eax+4]` of the original; counter `call_413af8` per applied step (no game call) |
| `S+0`, `S+8`, `S+2`, `[0x49b088]`, `S+4`, `H+0x1e9` read in this order after the step | same order; signed `idiv` with truncation toward zero; d = 0 faults (#DE) as in the original (precondition) |
| `0x443880(G, &L)` with EAX = &L, ECX = G, EDX = H at the call | through the weapons guest bridge with those registers (presentation side stays original code); counter `call_443880` |

- **Result.** EDX:EAX of the bridged `0x443880` call (EAX = G; EDX = `&L`,
  the own record's address, when the sweep is done, else 0). ECX is
  outside the contract.
- **Branch sites (5).** `0x413bf9` (up request), `0x413c10` (down
  request) in `0x413bc8`; `0x413b07` (direction), `0x413b1c` (≥ `0x400`),
  `0x413b27` (> 0) in `0x413af8`, which the original runs in vitro and the
  core computes. All five are reachable with suitable memory; none is
  proved one-way.
- **Stack.** No local is read before it is written and no entry register
  is read, so there is no entry shim. The original's frame stays below
  `0x41b130`'s ESP as residue; with the hook the host frames lie there.
  `0x41b130`'s later locals are written before they are read
  (`[EBP-8]`/`[EBP-4]` at `0x41b1ec`/`0x41b1f2`); whether any of its later
  callees (`0x410b40`, `0x447df8`, `0x447da8`, `0x43dbb0`, …) reads
  uninitialized stack was not followed.
- **In vitro** (`differential_shield_test`): direct cases against a
  logging stub for `0x443880`, installed 31/a7 poison probes and a
  real-callee pass with the real `0x443880`.

## `0x44d888(M, p)`: HDD map pick (input)

cdecl, two slots; both callers pass p = `M+0x11d` (cursor point, two
dwords). Entry EAX/EDX/ECX are not read.

1. `0x44d288(M, p, &r)`: r.x = p.x − `M+0x71` − ((`M+0x79` − `M+0x71`)
   >> 1), r.y = p.y − `M+0x75` − ((`M+0x7d` − `M+0x75`) >> 1) (arithmetic
   shifts; writes r.x, r.y, then subtracts in place). Then r.y ← −r.y.
2. k = `M+0x20` (scale), s = low byte of `[[M+0x14]+0x1a]` (shift).
   `M+0x374` ← ((r.x·k) >> s) + `M+0x18`, `M+0x378` ← ((r.y·k) >> s) +
   `M+0x1c`, `M+0x37c` ← 0 + `M+0x20` (the three stores happen before the
   additions, which are done in place in the order z, x, y).
3. Half size: r.x −= (s16)(5 << word `0x4d253a`) >> 1, r.y +=
   (s16)(5 << word `0x4d253c`) >> 1 (16-bit shift, then sign-extended);
   x' = ((r.x·k) >> s) + `M+0x18`, h = `M+0x374` − x'. (The corresponding
   y' and z' are computed into locals and never used.)
4. Actor list `[0x4a9af8]`, word counter compared (signed words) with word
   `0x4a9afe` before each step, a null entry ends the walk (the same walk
   as `0x43349c`). For actor T with x = `T+0x26`, y = `T+0x2a`: hit iff
   `M+0x374` > x − h, `M+0x378` > y − h, `M+0x374` < x + h and
   `M+0x378` < y + h (signed dwords, 32-bit wrapping sums). The first hit
   is returned.
5. Result: hit: EAX = ECX = T, EDX = `M+0x378` (the read at `0x44d9a7`).
   No hit: EAX = 0, ECX = 0, DX = the counter word of the last loop test
   (the value before its increment); the high word of EDX is that of the
   list pointer `[0x4a9af8]` when no actor was tested, else of y + h of
   the last actor when its first test failed, else of `M+0x378`.

Details against the listing:

- **Shift counts.** The scale shift is `SAR r32, CL` with CL = the low
  byte of the dword `[[M+0x14]+0x1a]` (read once, at `0x44d8ae`, before
  the three stores), so the count is that byte masked to five bits; a
  zero count leaves the product unchanged. The half size is `MOV AX, 5 /
  SHL AX, CL` with CL from the word `0x4d253a`: a 16-bit shift, count
  masked to five bits, counts 16..31 give 0, count 15 gives `0x8000`
  (sign-extended to −`0x8000`, halved to −`0x4000`), count 14 `0x4000`.
  Only the x half size (word `0x4d253a`) is used.
- **Products.** All three products are the low 32 bits of the signed
  product (two-operand `IMUL`, and the one-operand `IMUL` at `0x44d942`,
  whose EDX is overwritten at `0x44d94d`).
- **Read order.** `0x44d288` reads p+0, p+4, then `M+0x71`, `M+0x79`,
  `M+0x71`, `M+0x75`, `M+0x7d`, `M+0x75`; the stores to `M+0x374`,
  `+0x378`, `+0x37c` (0) come before the in-place additions of `M+0x20`,
  `M+0x18`, `M+0x1c` (in that order); then the two shift words, `M+0x20`
  (unused z), the list pointer `[0x4a9af8]`, `M+0x18`, `M+0x1c` (unused
  y). The walk reads the count word `0x4a9afe` before each step, then the
  entry; per actor `M+0x374`, `T+0x26`, `T+0x2a` twice, `M+0x374`, and,
  as far as the tests get, `M+0x378` twice. The walk writes nothing, so
  only reads that alias the stored point `M+0x374..0x37f` (an actor
  record or the view object placed over it) see the stores.
- **Edges.** The four tests are strict (`JLE`/`JLE`/`JGE`/`JGE` to the
  next step), signed, with the same h on both axes. y − h is formed from
  the first read of `T+0x2a`, y + h from the second. h ≤ 0 makes every
  test fail.
- **Counter.** The counter is a word: a count word ≥ `0x8000` (negative)
  ends the walk at once, a positive one beyond the list is ended by the
  null entry.
- Reads: the M fields above, the two display shift words, the actor list
  and each tested actor's position. Writes: `M+0x374`, `+0x378`, `+0x37c`
  only. No uninitialized local; no branch is provably dead.

**Register flow at the callers.** Both callers return to `0x44c988`, which
executes `POP ECX` right after each call (`0x44ca2d`, `0x44ca36`), so ECX
is dead. EDX is overwritten by `0x44d6e0` on every path, but `0x44d82c`
returns `0x44d888`'s EDX unchanged on a miss and when the hit actor is dead
(`T+0x99` ≠ 0), and `0x44c988` passes it on to its own caller when
`[M+0x105]` = −1 and `[M+0xc]` − 1 is not 0 or 1 (`0x44ca3d` → `0x44ca8c`
→ `0x44ca95` → `0x44cc5f`). The own code therefore returns the exact EDX;
ECX is not reproduced. `0x433134` (called by `0x44d82c` with the picked
actor) reads only its stack argument and overwrites ECX/EDX.

**Where the result goes.** `0x44d6e0` stores the picked actor
or the world point `M+0x374` into the selected wingman's record
(M + `0x85` + `0x2a`·`[M+0x105]`: point `+0xc`, actor `+0x1c`, kind
`+0x26`), `0x44db9c` commits it (`+0`, `+0x18`, `+0x22`), `0x44db4c`
copies it into the order record `0x4d03d4` (`+6` point or `+0x12` actor;
`0x44a1a0` sets its word `+0` to the map mode `M+0x5d` first), and
`0x44a1a0` (`0x44a5a9`) calls `wingman_order` `0x431610(…, 0x4d03d4,
wingman)`. So the map pick decides the target or destination of a wingman
order (simulation input), while the pick itself is UI logic. The records
are also read by the map draw `0x44e954`.

### Map pick 44d888

Hook `map_pick` (adapter group `map_pick`,
`tools/reference-game/proxy/map_pick_hooks.h`; core `core/src/map_pick.c`,
`es2/core/map_pick.h`). The core keeps the read order above and computes
`0x44d288` inside; no entry register and no uninitialized local is read,
so there is no entry shim.

- **Why `0x44d288` has no hook.** Its ECX at return (the y half size) is
  not dead at every caller: after `0x44e8a5`/`0x44eb0c` and `0x44f1ea` the
  paths run through further calls (`0x434280`, `0x431210`, the epilogue at
  `0x44f5c7`) that were not followed to the end, and the hook framework
  returns only EDX:EAX. Its other five call sites lie in the map draws
  (`0x44e88e`, `0x44e8a5` in `0x44e334`; `0x44eafb`, `0x44eb0c` in
  `0x44e954`; `0x44f1ea` in `0x44f1bc`; presentation), two of them in
  place (r = p: `0x44e8a5`, `0x44eb0c`). Inside `0x44d888` its ECX is
  overwritten at `0x44d8a5`.
- **In vitro** (`//tools/reference-game/invitro:differential_map_pick_test`
  and `…_full_test`, `invitro/map_pick_cases.h`): direct cases (whole
  original with the real `0x44d288` against the core; full EDX:EAX, arena
  and the globals `0x4a9af8..0x4a9aff`, `0x4d2538..0x4d253f`), 256
  installed 31/a7 poison probes and 3,072 real-caller cases under the
  original `0x44d6e0`/`0x44d82c`. All six branch sites both ways, none
  proved one-way.
- **Stack.** The stack bytes below `0x44d6e0`/`0x44d82c` after the call
  hold the hook's frames instead of `0x44d888`'s locals; the later callees
  `0x462830`, `0x44dd14` and `0x44a748` and the rest of `0x44c988` were not
  walked for reads of uninitialized locals (`0x44dd14` writes its two
  locals before reading them).

## Map click 44d6e0

`0x44d6e0(M)`, hook `map_click` (adapter group `map_click`,
`tools/reference-game/proxy/map_click_hooks.h`; core
`es2_map_click_registers` in `core/src/map_pick.c`, `es2/core/map_pick.h`).
Role I+B: no simulation write of its own.

**Callers.** One call site, `0x44ca28` in the `HDDMap` input slot
`0x44c988`, reached when the click byte `M+0x11c` is set, `M+0x103` ≠ 0
and `M+0x105` ≠ −1 (otherwise `0x44d82c` runs). The address does not occur
as a literal dword (no pointer use).

**Entry.** cdecl, one argument M (`[EBP+8]`, kept in EBX). EAX is
overwritten before use; entry EDX and ECX are not read by `0x44d6e0`
itself but are still in EDX/ECX at the call to `0x44d888` (which does not
read them either). No local variables.

**Steps.**

1. `0x44d888(M, M+0x11d)` with EAX = M+0x11d. T = its EAX (0 = nothing
   picked); its EDX and ECX are used only as described under "Result".
2. Mode m = `[M+0x5d]` (dword). EDX = m − 0xb − 2; m − 0xb < 2
   (unsigned, m ∈ {0xb, 0xc}) → step 3. Else EDX −= 2 (m − 0xf);
   m − 0xd < 2 (m ∈ {0xd, 0xe}) → point store (step 5); any other mode →
   step 6 with nothing stored.
3. m ∈ {0xb, 0xc}: T = 0 → step 4. Else reads, in this order, `[T+0x45]`
   (the side record S), `[M+0x5d]` again, the byte `[S+0x12]`, which is
   sign-extended into EDX. Mode 0xc with DX = 0 → actor store. Otherwise
   `[M+0x5d]` is read a third time; mode 0xb with DX = 1 → actor store,
   else step 4. (The tests are 16-bit on the sign-extended byte, so they
   equal byte tests.)
4. `[M+0x5d]` is read again: 0xb → step 6 with nothing stored; else
   (0xc) → point store.
5. **Actor store** (`0x44d736`): `M+0x15c` ← T; i1 = `[M+0x105]`, dword
   `M + 0x2a·i1 + 0xa1` (record +0x1c) ← T; i2 = `[M+0x105]`, dword
   `M + 0x2a·i2 + 0xab` (record +0x26, kind) ← 1; i3 = `[M+0x105]`, byte
   `M + 0x2a·i3 + 0xa6` (record +0x21) ← 1; byte `M+0x104` ← 1.
   **Point store** (`0x44d7a4`): i1 = `[M+0x105]`, R = M + 0x91 + 0x2a·i1
   (record +0xc); R+8 ← `[M+0x37c]`, R ← `[M+0x374]`, R+4 ← `[M+0x378]`
   (each read directly before its write); i2 = `[M+0x105]`, dword
   `M + 0x2a·i2 + 0xab` ← 0; i3 = `[M+0x105]`, byte
   `M + 0x2a·i3 + 0xa6` ← 1; byte `M+0x104` ← 1. All index products wrap
   modulo 2^32 (the index is a dword; −1 addresses the record below the
   first one). The index is reread before each of the last two stores, so
   a store that lands on `M+0x105` redirects the following ones.
   Both stores then call the sound `0x462830(0x14)` with EAX = 21·i3,
   EDX = i3 and ECX = i2 (actor) or 0 (point), `POP ECX` (ECX = the pushed
   0x14: `0x462830` never writes its argument slot), and the label update
   `0x44dd14(M)` with EAX/EDX as `0x462830` returned them and ECX = 0x14.
   (`0x462830` with the sound off, `[0x49f8ec]` = 0, returns its entry
   EAX/EDX/ECX unchanged.)
6. `0x44d818`: byte `M+0x11c` ← 0 (click handled), byte `[0x49d6ae]` ← 1
   (map redraw request, read by `0x44c988` at `0x44cc23`).

**Writes.** Only the `HDDMap` object (`M+0x15c`, `M+0x104`, `M+0x11c`
and the record fields above, wherever the index puts them) and the byte
`0x49d6ae`. The record becomes a wingman order through
`0x44db9c`/`0x44db4c` ([Where the result goes](#0x44d888m-p-hdd-map-pick-input)).

**Result.** After a store EDX:EAX are those of `0x44dd14` and ECX = M
(`POP ECX`). Without a store EAX = T and EDX = m − 0xf (other modes),
m − 0xd (mode 0xb with T = 0), or the sign-extended side byte (mode 0xb
with T ≠ 0); ECX is `0x44d888`'s ECX (T = 0) or S. At the caller,
`0x44ca2d` pops ECX (dead) and `0x44ca37` reloads EAX; EDX is next written
at `0x44ca42` unless `[M+0x105]` is −1 after the call, in which case it
can reach `0x44c988`'s own caller. The own code returns the exact EDX:EAX
on every path and does not reproduce ECX. Flags are dead (`POP ECX`,
`JMP`, `MOV`, `CMP`).

**Stack.** No local is read and no entry register is read (EDX/ECX are
only passed on to `0x44d888`), so no entry shim. The residue below
`0x44c988` after the call is the hook's frames instead of the original
frames of `0x44d6e0`, `0x44d888`, `0x462830` and `0x44dd14`. `0x44c988`'s
own locals are written before they are read; its later direct callees
show no uninitialized read except slots filled through a pointer; deeper
callees were not walked.

**Own code.** `0x44d888`, `0x462830` and `0x44dd14` go through the
weapons guest bridge and callee observer with the registers above, so
`map_pick` and `sfx_play` keep their own modes and `0x44dd14` (label
drawing) stays original code. Every memory read of steps 2 to 5 is
repeated where the original repeats it. Counters (`hk_map_click_<suffix>`,
only while the hook is non-original): `call_44d888` (one per call),
`call_462830` and `call_44dd14` (one each per store).

**Branch sites (8).** `0x44d700` (m ∈ {0xb, 0xc}), `0x44d705`
(m ∈ {0xd, 0xe}), `0x44d712` (T = 0), `0x44d723` (mode ≠ 0xc),
`0x44d728` (side 0), `0x44d72e` (mode ≠ 0xb), `0x44d734` (side ≠ 1),
`0x44d7a2` (mode 0xb before the point store). All reachable; none proved
one-way.

**In vitro** (`//tools/reference-game/invitro:differential_map_click_test`
and `differential_map_click_full_test`, `invitro/map_click_cases.h`):
direct cases on the map-pick arena with the real `0x44d888` (modes,
indices whose stores land on M's own fields, side bytes, entry registers;
full EDX:EAX, ordered call log, arena, globals), 256 installed 31/a7
poison probes and 3,072 real-callee cases (`map_pick` and `map_click` in
both modes, the real `0x462830` with the sound off).

## `0x434e8c(Q, rec, row)`: queue insert (message ports)

cdecl, three slots: Q = port + 4, rec = the event record of the port's
receive slot (layout in [mission_runtime.md](mission_runtime.md)), row = a
text-table row (`+0` text pointer, `+5` pointer to a 12-byte descriptor
d). Entry EAX/EDX/ECX are not read. The receive slot `0x435c48` calls it
only when both row pointers are nonzero. Own code: the row in
`sim/src/hud_widgets.c` ([hud.md](hud.md#the-message-queue-the-ports-and-the-gauge-blink),
which also has the entry layout and the coupling sites).

1. Count word `Q+0x21c` = 10 → `0x4351c0(Q)`.
2. First free slot i (in-use byte `Q+0x212+i` = 0), i = 0..9. None (only
   possible when the in-use bytes disagree with the count, see the slips):
   return without further writes (EAX = Q + `0x212`, EDX = Q + `0x21b`).
3. now = clock `0x467828()` (one call per inserted message, before the
   copies; site `0x434ecd`). In-use ← 1. Entry E = Q + `0x31`·i: `E+2` ←
   rec+2 (dword), `E+6..+0x11` ← rec+6..+0x11, `E+0x12` ← rec+0x12,
   `E+0x18` ← row+0, word `E+0` ← d[0], `E+0x16` ← d[1], `E+0x17` ← d[2]
   (priority), `E+0x1c` ← 60·d[3], `E+0x20` ← 60·d[4], `E+0x24` ←
   60·d[5] + now, `E+0x28` ← 60·d[6] + now, `E+0x2c..+0x30` ← d[7..11]
   (bytes zero-extended where widened). The record's selector word is not
   copied.
4. Position j = the first order position whose entry's priority byte is
   greater than d[2] (unsigned values), else j = count. For j < count the
   positions j … count−2 move up by one (`order[count−1]` is overwritten);
   then `order[j]` ← i and the count word += 1. The debug `sprintf`
   ("addMsg: added %s (id %d) at pos %d" into `0x4d123f`) at `0x43503a` is
   unreachable (`0x435038` jumps over it).
5. Result: EAX = i. Appended (j = count): EDX = d[2], ECX = count (before
   the increment). Inserted: EDX = j; ECX = the old `order[j]` if anything
   moved, else the priority byte of entry `order[j]`. The receive slots
   return these registers to their caller (`0x435c48` ends with the call;
   the trigger dispatcher carries the registers of the slot-0 call into
   its next iteration, [mission_runtime.md](mission_runtime.md)).

**The original's slips (instructions; consequences are
hypotheses).** The own code keeps them
([hud.md](hud.md#the-originals-slips-kept)).

- *Mid insertion drops the last entry.* The shift loop starts at
  `order[count−2]` → `order[count−1]` (`0x434ffd..0x43501b`), so the
  previous last entry leaves the order list while its slot stays in use,
  and after the increment `order[count]` holds whatever the list held at
  that position before (a stale slot index). Every insertion in front of
  a queued message (a smaller priority byte) triggers this.
- *Drop-oldest indexes twice.* `0x4351c0` reads s = `order[9]` (dword
  `Q+0x20e`) and frees slot `order[s]`, not slot s, then decrements the
  count without touching the order list.
- *Expiry skips position 0.* `0x4351ec` never expires order position 0.
- Consequence (hypothesis): slots can leak (in use, never in the order
  list, never expired by `0x4351ec`, which walks order positions), and a
  slot can appear twice in the list. With enough leaks step 2 finds no
  free slot and messages are silently dropped.

## HDD map order controls

Four functions of the `HDDMap` object M (vtable `0x49d9d8`, fields in
[Objects](#objects)) are hooks of the adapter group `hdd_orders`
(`tools/reference-game/proxy/hdd_orders_hooks.h`; core
`core/src/hdd_orders.c`, `es2/core/hdd_orders.h`): `map_areas` `0x44d450`,
`map_mode` `0x44d9f4`, `wingman_select` `0x44da98`, `order_cancel`
`0x44dc10`. The HDD map input slot `0x44c988` is the hook `map_frame`
([HDD map frame](#hdd-map-frame)).

**Fields used (in addition to [Objects](#objects)).** `M+8` the HDD gauge
(argument of the button press `0x438d9c`), `M+0x31` the array of the three
wingman buttons (`0x44ede0`), `M+0x35` the mode-button panel gadget,
`M+0x39` the map-area gadget, `M+0x3d + 4·i` (i = 0..7) the eight mode
buttons (rectangle x0, y0, x1, y1 at button+4), `M+0x81` the previous map
mode, `M+0x131..+0x139` the button-enable bytes and their redraw byte
(`0x44ee00`/`0x44ee24`), `M+0x144` label redraw, `M+0x14d` event pending,
`M+0x14e` the event's gadget, `M+0x152` the event point (x, y), `M+0x15a`
"warp the cursor" (set by the key path `0x44d5c0`), `M+0x15b` the flag
`0x44d3cc` copies from `0x49dbe0`, `M+0x380`/`M+0x381` pick-mode and
wingman lamps. `0x44d3cc(M, gadget, point)` records an event (sets
`+0x14d`, `+0x14e`, `+0x152`, `+0x15b`); clicks and the map keys of
[oracle_suites.md](../reference-game/oracle_suites.md#order-keys-read-from-the-bytes)
(D/A/F/T/G/O/C/E and Enter through `0x44d5c0`) both arrive this way.
G = `[[0x49d6b0]+8]` (the map gadget; bytes `+0x10f` ← 1 and `+0xb0` ← 2
request its redraw).

**Callers and register flow.** All four are cdecl with M as the first
argument; no local is read before it is written and no entry register is
read except the ones passed on to callees below. ECX is dead after every
call site (followed up to the callers' callers):

Call sites: `0x44d450` at `0x44c99d` (only when byte `M+0x14d` ≠ 0);
`0x44d9f4` at `0x44d4ec`, `0x44dabd`, `0x44dc1a`, `0x44ee65`, `0x44eea7`;
`0x44da98` at `0x44a7d6` (last call of `0x44a748`), `0x44dc3d`;
`0x44dc10` at `0x44a5ba`, `0x44a5e3` (send and cancel paths of the HDD
button executor `0x44a1a0`).

EDX can leave `0x44c988` and `0x44a1a0` on some of these paths, so the
hooks return the exact EDX:EAX; ECX is not reproduced (no exit shim). No
entry shim: no uninitialized stack slot is read.

**`map_areas` `0x44d450(M)`: handle a pending gadget event.**

1. c = `[M+0x14e]` (read once into a local). c ≠ `[M+0x35]` → step 4 with
   EDX = entry EDX.
2. For i = 0..7 (EDX = i, ECX = M+0x3d+4i): A = `[M+0x3d+4i]` + 4; a hit
   needs, in this read order, `[M+0x156]` > `[A+4]`, `[M+0x156]` < `[A+0xc]`,
   `[M+0x152]` > `[A]`, `[M+0x152]` < `[A+8]` (signed, strict; the point
   is reread for every test). After eight misses EDX = 8 → step 4.
3. Hit on button i: dword `[M+0x105]` = −1 → label
   `0x44dc6c(M, [0x4d1638], 1)` (EAX = A, EDX = i, ECX = `[0x4d1638]`),
   then step 4. Else, with EAX = i+0xa: i+0xa ≠ `[M+0x5d]` →
   `map_mode(M, i+0xa)` (EDX = i+0xa, ECX = M+0x3d+4i); equal and the bytes
   `[M+0x15b]` and `[M+0x104]` both nonzero → the HDD button press
   `0x438d9c([M+8], 0xd, 1)` (the send; EDX = `[M+8]`, ECX as before);
   equal otherwise nothing (EDX = i). Then, if byte `[M+0x15a]` ≠ 0, the
   local L = (`[M+0x71]` + ((`[M+0x79]` − `[M+0x71]`) sar 1) + `[B+0x210]`
   − `[B+0x220]`, `[M+0x75]` + ((`[M+0x7d]` − `[M+0x75]`) sar 1)
   + `[B+0x214]` − `[B+0x224]`) with B = `[[0x49b088]+4]` (read order
   `M+0x71`, `M+0x79`, `0x49b088`, its +4, `M+0x75`, `M+0x7d`, B+0x210,
   B+0x214, B+0x220, B+0x224; wrapping 32-bit sums, arithmetic shift by
   one) and `0x486e7c(&L)` (sets the mouse position; EAX = `[B+0x224]`,
   EDX = &L, ECX = `[B+0x220]`); byte `M+0x15a` ← 0. Every hit leaves the
   loop.
4. EAX = c. c = `[M+0x39]` (read now) → dword `M+0x11d` ← `[M+0x152]`,
   `M+0x121` ← `[M+0x156]` (each read, then written), byte `M+0x11c` ← 1
   (click pending, handled by `map_click` from `0x44c988`), then G (read
   now) +0x10f ← 1, +0xb0 ← 2; EAX = G, EDX = `[M+0x156]`.
5. Byte `M+0x14d` ← 0. EDX:EAX as left by the steps (after a callee its
   EDX:EAX, unless step 4 overwrites them).

**`map_mode` `0x44d9f4(M, mode)`: set the map mode.** G is read first
(`[0x49d6b0]`, then +8). mode ∈ 0xb..0xe (the pick modes; `mode − 0xf`
borrows): cursor shape `0x433e3c(2)` (EAX = mode − 0xf, entry EDX/ECX),
label `0x44dc6c(M, [0x4d1638 + 4k], 0)` with k = 3 for mode 0xb, else 2
(EAX = k, EDX = the text, ECX = 2), then bytes `M+0x103` ← 1, `M+0x380` ← 1,
`M+0x104` ← 0. Other modes (also −1): `0x433e3c(0)` (same entry
registers), bytes `M+0x103` ← 0, `M+0x104` ← 1, `M+0x380` ← 0, then
`0x44dd14(M)` with `0x433e3c`'s EAX/EDX and ECX = 0. Finally old =
`[M+0x5d]`; mode ≠ old → `M+0x81` ← old, `M+0x5d` ← mode, G+0x10f ← 1,
G+0xb0 ← 2 (the G read at entry). Returns EAX = old, EDX = the label
call's EDX.

**`wingman_select` `0x44da98(M, index)`: select a wingman (−1 none).**
o = `[M+0x105]`. o ≠ −1: `0x44ede0(M, o)` (toggles the old wingman's
button; EAX = o, entry EDX/ECX); then, if `[M+0x5d]` ≠ −1:
`map_mode(M, −1)` with `0x44ede0`'s registers, e = `[M+0x105]` (reread),
byte `M + 0x2a·e + 0xa6` ← 0 (record byte +0x21), bytes `M+0x104` ← 0,
`M+0x381` ← 0, `M+0x144` ← 1, dword `M+0x15c` ← 0, byte `M+0x103` ← 0
(EAX = 0, EDX = 21·e, ECX = e). Then `M+0x105` ← index; index ≠ −1:
`0x44ede0(M, index)` (EAX = index, EDX/ECX as left), the enable-all
`0x44ee00(M)` with `0x44ede0`'s registers, byte `M+0x381` ← 1; index = −1:
the disable-all `0x44ee24(M)` (EAX = −1). Finally G (read now) +0x10f ← 1,
+0xb0 ← 2. Returns EAX = G, EDX = `[0x49d6b0]`. Index products wrap
modulo 2^32.

**`order_cancel` `0x44dc10(M)`: cancel the selected wingman's order.**
`map_mode(M, −1)` (entry registers), e = `[M+0x105]`, byte
`M + 0x2a·e + 0xa6` ← 0, `wingman_select(M, −1)` (EAX = 21·e, EDX = e,
ECX = `map_mode`'s), then bytes `M+0x104` ← 0, `M+0x381` ← 0, `M+0x144`
← 1, dword `M+0x15c` ← 0, byte `M+0x103` ← 0. Returns `wingman_select`'s
EDX:EAX.

**Writes.** Only M (the fields named above, the record byte wherever the
index puts it), G+0x10f/G+0xb0 and the 8-byte local. The guest callees
write more: `0x44dc6c`/`0x44dd14` M+0x140/+0x13e and draw, `0x44ede0` the
wingman button's +0x36 and its virtual +4 (redraw), `0x44ee00`/`0x44ee24`
M+0x131..+0x139, `0x486e7c` the graphics library's mouse position
(`0x4a3620` or `0x4a364a`), `0x433e3c` the cursor shape
(`[0x49b088]+0x23a`, `0x49b630`).

**Own code.** Every game call goes through the weapons guest bridge and
callee observer with the registers above: `map_mode` and `wingman_select`
keep their own modes when called from the other hooks of the group,
`0x438d9c` is the hook `button_press` (group `hud_keys`) and keeps its
mode; the label/lamp/cursor helpers `0x44dc6c`, `0x44dd14`, `0x44ede0`,
`0x44ee00`, `0x44ee24`, `0x433e3c` and the platform call `0x486e7c` stay
original code. Memory is read again wherever the original reads it (the
point, `M+0x39`, `M+0x105`, G), so callee writes and overlapping objects
are seen as by the original. `map_areas` keeps its 8-byte local in the
adapter's scratch (another address than the original's frame;
`0x486e7c` reads only the two dwords). Callee counters
(`hk_<hook>_<suffix>`, only while the hook is non-original): `map_areas`
`call_438d9c`, `call_44d9f4`, `call_486e7c`, `call_44dc6c`; `map_mode`
`call_433e3c`, `call_44dc6c`, `call_44dd14`; `wingman_select`
`call_44ede0`, `call_44d9f4`, `call_44ee00`, `call_44ee24`;
`order_cancel` `call_44d9f4`, `call_44da98`. All four refuse compare mode
(game calls).

**Branch sites (18).** `map_areas`: `0x44d471` (event not on the panel),
`0x44d487`, `0x44d493`, `0x44d49d`, `0x44d4a8` (the four rectangle
tests), `0x44d4b5` (no wingman), `0x44d4c1` (other mode), `0x44d4ca`,
`0x44d4d3` (send flags), `0x44d4fb` (cursor warp), `0x44d572` (loop),
`0x44d57e` (event not on the map); `map_mode`: `0x44da10` (pick mode),
`0x44da24` (mode 0xb), `0x44da7a` (mode unchanged); `wingman_select`:
`0x44daa8` (none selected), `0x44dab8` (map mode −1), `0x44db0d` (new
index −1); `order_cancel` has none. All reachable, none proved one-way.

**In vitro** (`//tools/reference-game/invitro:differential_hdd_orders_test`
and `…_full_test`, `invitro/hdd_orders_cases.h`): full tier, 1,001,024
direct cases per hook against logging stubs that rewrite the fields the
original rereads, 256 installed 31/a7 poison cases per hook and
real-callee passes against the all-original run (the logged ECX of
`0x44ede0`/`0x44ee24` is not compared: both write ECX before reading it).
All 18 branch sites both ways, none proved.

**Stack.** The stack bytes below `0x44c988`/`0x44a1a0` after the calls
hold the hook frames instead of the original frames; the callers' later
callees were not walked beyond the ones named above.

## Throttle slider event and shield step

Three functions are the hooks `slider_event` (`0x4483a0`), `slider_base`
(`0x43904c`) and `shield_step` (`0x413af8`) of the adapter group
`throttle` (`tools/reference-game/proxy/throttle_hooks.h`; core
`core/src/throttle.c`, `es2/core/throttle.h`). `0x4483a0` is the throttle
slider's slot `+8`; `0x413af8` is the balance step of
[`0x413bc8`](#0x413bc8s-contract-simulation).

**Where the functions are referenced.** `0x4483a0` occurs once
as a dword: slot `+8` of the class table at `0x49d12c` (slots `0x4480c8`,
`0x448b58`, `0x4483a0`, `0x4481c4`, then the four `0x4526xx` leaves shared
with the base class table). `0x43904c` occurs once as a dword (slot `+8`
of the base class table at `0x49d19c`: `0x455738`, `0x448b58`,
`0x43904c`, `0x455566`, the same four leaves) and is called directly only
at `0x4483ac`. `0x413af8` is called only at `0x413bfe` and `0x413c15` in
`0x413bc8`, whose hook (`shield_balance`) computes the step in core and
never calls it; `shield_step` therefore runs live only when
`shield_balance` is original. The slots are reached through a gadget's
class table at `G+0x17` by the widget library (`0x438dbc`, `0x454e0b`:
`push` event, `push` G, `mov eax,[edx+0x17]`, `call [eax+8]`; the activate
path `0x452898`); none of the call sites found reads ECX or EDX before
writing it, and the two that return (`0x4525c5`, `0x4526b1`) pass EAX on,
which the hooks reproduce.

### `0x43904c(G, event)` (slider_base)

cdecl, two slots; locals none; `EBX = G`. No entry register is read.

1. `H = [G+0x42]`, `T = [H]`; `call [T]` with the three slots (`H`, `G`,
   `event`) and EAX = event, EDX = H, ECX = T.
2. `K = [G+0x17]`; `call [K+4]` with the slot (`G`), EAX = K, EDX and ECX
   as the first call left them; the `pop ecx` afterwards is dead.
3. `S = G+0x3e`; `U = [S]`; `call [U+8]` with the slot (`S`), EAX as the
   second call left it, EDX = S, ECX = U.
4. Result: EDX:EAX of the third call. ECX after the return is `S` (the
   `pop ecx` of the pushed argument); the hook does not reproduce it.

`G+0x17` is read after the first virtual and `[G+0x3e]` after the second, so
virtuals that repoint either table are seen. No conditional branch.

### `0x4483a0(G, event)` (slider_event)

cdecl, two slots; `EBX = event` (kept through the first call). Entry EDX and
ECX only travel into the first callee.

1. `0x43904c(G, event)` (EAX = G, EDX/ECX = entry values); then ECX = G+0x3e.
2. `test byte [0x49db6c],1`; clear → return (EDX:EAX of step 1). The byte is
   the current event's button word ([input.md](input.md#mouse-event-queue-452d40--453078)).
3. `P = 0x477904()` (joystick summary, a guest call; it clears and refills
   its 8-byte block and returns its address in EAX). Byte `P+4` = 0 → return
   (EDX:EAX of the summary call). Byte `0x4d1f46` ≠ 1 → return likewise.
4. `MOV AX,1` keeps the high word of P; the signed `event > 0` adds −2 to
   EAX (`TEST EBX,EBX` / `JLE`), so AX = 1 for `event ≤ 0` and AX = 0FFFFh
   otherwise (and the high word of EAX is one lower in that case). Word
   `0x49a06e` ← AX (the throttle lever direction word, the same effect as
   input action 5 / Ctrl+1 at `0x460e39`/`0x4607de`, which test the same two
   bytes). Return EAX as formed, EDX from the summary call.

Branch sites: `0x4483bb`, `0x4483c6`, `0x4483cf`, `0x4483d7`.

### `0x413af8(S, up)` (shield_step)

cdecl, two slots. Step = `0x66` if the word `up` ≠ 0, else `−0x66`
(formed as `0x66 + 0xff34` in DX); `S+4` += step (16-bit, first store);
then `S+4` ← `0x400` if the sum ≥ `0x400`, ← 0 if ≤ 0, else unchanged
(signed words, second store).

- The word compare `cmp word [ebp+0xc],0` looks at the low half of `up`
  only, so `0x10000` is a down step.
- EAX = S. EDX: low word = the unclamped 16-bit sum (`mov dx,[eax+4]`
  reloads it); high word = the entry EDX's high word, minus one in the
  down case (the 32-bit `add edx, 0xffffff34` borrows from the high half).
- ECX is not reproduced (`xor`/`mov ecx,edx`/`mov cx,0x400`).
- `0x413bc8` overwrites all three before reading them.
- Branch sites `0x413b07`, `0x413b1c`, `0x413b27` are listed in
  `branch_sites.h` under `shield_balance`.

### Mapping to the replacement

| Original | Own code |
|---|---|
| `call [T]`, `call [K+4]`, `call [U+8]` in `0x43904c` | guest bridge with the original's slots and EAX/EDX/ECX; targets read when called; observer slots `0x42`, `0x17`, `0x3e` (counters `slot_42`, `slot_17`, `slot_3e`) |
| `call 0x43904c` in `0x4483a0` | guest bridge, so `slider_base` keeps its own mode; counter `call_43904c`; ECX is then forced to `G+0x3e` (what the original leaves), so the hook's ECX loss is invisible to `0x477904` |
| `call 0x477904` | guest bridge; counter `call_477904` |
| the three globals | read in the original's order (`0x49db6c`, `P+4`, `0x4d1f46`); `0x49a06e` written as a word |
| `0x413af8` word arithmetic | `S+4` ← 16-bit sum (first store), reread, clamped value (second store), EDX as above; no game call |

No local is read before it is written and no entry register is read, so
there is **no entry or exit shim**. The callers' residue below ESP differs
(host frames instead of the original frames of the three functions);
whether any later callee of a slot caller reads uninitialized stack was
not walked.

**Compare.** `shield_step` accepts compare (span `S[0..5]`; comparator:
EAX relative to the span base, low word of EDX, because the high word is
entry residue); `slider_event` and `slider_base` refuse it (guest calls /
virtual calls into arbitrary objects; [compare_mode.md](../reference-game/compare_mode.md)).

**In vitro** (`differential_throttle_test`, full tier): 1,001,024 direct
cases per hook against logging stubs for the three virtuals and
`0x477904`, 256 installed 31/a7 cases per hook, real-callee passes
(4,096 per slider hook, 3,072 for `shield_step`; the summary call's
logged ECX is not compared, since the hook does not reproduce the ECX
`0x43904c` leaves and the summary writes ECX first) and 512 installed
compare cases (exact replacement 0 mismatches, skewed caught every time).
The four sites of `0x4483a0` and the three `0x413af8` sites go both ways;
none is proved one-way.

**Live.** `slider_event` and `slider_base` run whenever the throttle
slider gets an event; `call_477904` stays 0 in the suites (no suite has
the joystick throttle bit); for `shield_step` see [the shield-balance
chain](#the-shield-balance-chain).

## HDD key method 449ff4

`0x449ff4(H, code)`, the HDD gauge's key method, is the hook `hdd_key` of
the adapter group `hdd_keys` (`tools/reference-game/proxy/hdd_keys_hooks.h`;
core in `core/src/hdd_orders.c`, `es2/core/hdd_orders.h`).

**Where it is reached.** The only image reference to `0x449ff4` is slot
+0Ch of the vtable `0x49d64c` (slots +0 `0x44a174`, +4 `0x449a78`, +8
`0x449bf8`, +0Ch `0x449ff4`), which only the HDD gauge constructor
`0x448d1c` stores. `hud_key` `0x432bc8` offers every key it does not
handle itself to `[H'+1F9h]` (H' = the HUD) through that slot: in display
mode 1 at `0x4330ee` (call return `0x4330fb`) and, with byte `0x4d2538` = 1,
at `0x4330cc` (return `0x4330d9`). Both sites read only AL (`test al,al`,
then AL = 1 or EAX = 0) and overwrite EDX; ECX reaches `hud_key`'s callers,
which read only AL ([hud.md](hud.md#hud-key-handler-as-own-code)). So the
method is input dispatch: it turns a key into an HDD button press, a
page/view change or the active page's own key handling (the map page
`0x44cc68`: map modes, picks, sends;
[oracle_suites.md](../reference-game/oracle_suites.md#order-keys-read-from-the-bytes)).
It draws nothing itself.

**Contract** (cdecl, two arguments; EBX saved; no local). EAX = `code`
(the full dword), EBX = H, EDX = k = the low word of `code` (`movzx`).
Dispatch on k (signed compares, k ≥ 0):

| k | Action | Registers at the call |
|---|---|---|
| 48h (up) | `438d9c(H, 2, 1)` | EAX = code, EDX = 48h |
| 4Ah, 4Bh, 4Dh, 4Eh, 50h (keypad −, left, right, keypad +, down) | jump table `0x44a05e`: `438d9c(H, b, 1)`, b = 6, 4, 5, 7, 3 | EAX = code, EDX = k − 4Ah |
| 02h..04h (1, 2, 3) | `438d9c(H, k + 8, 1)` (buttons 0Ah..0Ch, the wingman selection) | EAX = k + 8, EDX = k − 5 |
| 0Ch (−), 0Dh (=) | `438d9c(H, 6 / 7, 1)` | EAX = code, EDX = 0 |
| 1Fh (S), 17h (I), 11h (W) | `44a60c(H, 1)` (show page 1), then `450b88([H+105h], v)` with v = 0 / 1 / 2; `[H+105h]` is read after `44a60c` returns | first call: EAX = code, EDX = 0 / 0 / 11h; second: the registers `44a60c` returned with `[H+105h]` in ECX / EAX / EDX |
| any other (also 49h, 4Ch, 4Fh, ≥ 51h, 05h..0Bh, 0Eh..10h, 12h..16h, 18h..1Eh, 20h..47h, 0, 1) | `[[P]+10h](P, code)` with ECX = `[H+109h]` (the active page index), P = EAX = `[H + 4·ECX + 101h]` (32-bit address arithmetic), EDX = `[P]` | EDX:EAX of the slot is returned unchanged |

ECX at every direct call is the entry ECX. After a handled key the
function returns AL = 1 over the last callee's EAX (upper 24 bits kept)
and that callee's EDX; after the page slot, the slot's EDX:EAX. ECX after
the return is the last callee's (dead, see above). Memory: reads only
(`[H+105h]`, `[H+109h]`, the page array, the page's vtable and slot); the
function writes nothing itself. The jump table is the only indirect jump;
the ten conditional branches (`0x44a004` … `0x44a051`) are all reachable
both ways.

**Callees.** `button_press` `0x438d9c` is a hook (`hud_keys`, called
through the bridge, keeps its mode). `0x44a60c(H, i)` switches the HDD to
page i (hides the old page's lamp through `[H+18h+4·old]` slot +4 and the
old page's slot +8, stores `H+109h`, the new page's slot +0Ch, the lamp,
the button states of the table `0x49d24c + 15·i`, `H+10Dh+i` and `H+0B0h`
redraw bytes) and `0x450b88(P, v)` sets the status page's view (`P+7Ch` =
v, `P+84h` = 0, `P+88h` = −1, `P+90h` = 13h / 0Ch / the selected object's
word): both stay guest calls (presentation state of the HDD). The page
slot +10h stays a guest call.

**Stack.** No uninitialized read in `0x449ff4`. When `hud_key` runs at the
original stack coordinate and `hdd_key` is replaced, the callees run below
the adapter's host frame, not 12 bytes below `hud_key`'s outgoing ESP. The
known residue reader under `hud_key`, the group order `0x4231a4`, is not
below this method (the map's sends go through `wingman_order` `0x431610`
to the mech's slot +28h). The HDD map order suites (`c3_09-wingman-orders`,
`c3_09-orders-matrix`) stay identical with `hdd_key` replaced, so the
residue below the page slot does not matter there. No entry or exit shim.

**In vitro** (`invitro/hdd_keys_cases.h`): 100,000 direct cases (quick
tier) against logging stubs, 256 installed 31/a7 poison cases, 4,096
real-callee cases with the real `button_press`; all ten branch sites both
ways.

## MFD order path

Twelve functions of the MFD order path and of the mission-load set-up of
the order group are hooks of the adapter group `mfd_orders`
(`tools/reference-game/proxy/mfd_orders_hooks.h`; core
`core/src/mfd_orders.c`, `es2/core/mfd_orders.h`). Role names are
hypotheses; offsets, values, registers and orders come from the
bytes and by the in-vitro comparison with the original bodies
(`differential_mfd_orders_test`).

| Hook | VA | Reached from |
|---|---|---|
| `mfd_button` | `0x446844` | slot 0 of the `MFDGauge` table `0x49cfa0` (type descriptor `0x447711`; slot +0Ch is the key method `0x4469e8`): the owner call of a pressed button, `0x438e3c` (the button widget's click, `[B+24h]` slot 0 with three slots owner, button, flags), from `button_press` `0x438d9c`'s slot +8 (the keys D, X and the range key through `0x4469e8`) and from mouse clicks |
| `mfd_click` | `0x4470c0` | `0x447658`, slot +8 of the `MFDSelectGadget` table `0x49cee0` (`test byte [ebp+0Ch],1`, then `(W+24h, W, 4d1de3h)`, the cursor point) |
| `mfd_order` | `0x447248` | `0x446844` (button 10), `0x446c38` (the Alt order keys under `hud_key`), `0x4470c0` |
| `order_call` | `0x43166c` | `0x447248` only |
| `group_order` | `0x4231a4` | `0x43166c` only |
| `menu_code` | `0x43f9c0` | `0x447248`, `0x446c38` (Alt+C/Alt+E checks), `0x447380` (the menu draw, entry 10) |
| `menu_toggle` | `0x43f9f8` | `0x447248` only |
| `mfd_entry` | `0x4470a4` | `0x4470c0`, `0x447158`, `0x44703c`, `0x447070` |
| `mfd_target` | `0x446fb0` | `0x446844` only (buttons 7/9 outside MFD page 0; the D key presses button 7 through `0x4469e8`) |
| `mfd_view_next` | `0x446fc4` | `0x446844` only (button 7/9 on page 0) |
| `mfd_range` | `0x446ff0` | `0x446844` (button 8), `0x4469e8` (key 213h with the button disabled) |
| `order_setup` | `0x431530` | `0x461cc0` in the mission-load root `0x461608` |

**Objects** (offsets; meanings hypotheses). The gauge G:
`+14h` the cockpit (passed to `0x43166c`), `+18h + 4i` the button table
(count `+68h`, signed): buttons 0..5 switch the page (`0x446e60`), 7 and
9 step the status view or the cockpit target, 8 the radar range, 10 gives
the order, 11 and 12 are a radio pair whose objects are also `[G+44h]`
and `[G+48h]` (`G+18h + 4·11` and `+4·12`); `+B1h` the selected menu
entry, `+B9h` the mech the status page shows, `+BDh`/`+C5h` the radar
range value and index, `+C1h` the page (0..5), `+CDh + 4k` the page's
object (its word `+0Ch` is a timer, set to 64h), `+D1h` the menu,
`+E5h + k` the pages' redraw bytes, `+E6h` the entry redraw, `+308h`,
`+30Ch..`, `+318h`, `+31Ch` the status view list (player, wingmen, index,
count + 1). The menu: `+8` a pointer to the selected-entry dword, `+14h +
4i` the six entry gadgets (rectangle at `+4`: x0, y0, x1, y1), `+2Ch + i`
the flag bytes (bit 1: the item's second order, code + 3), `+32h` the
entry, `+36h` its redraw byte. The order record `0x4d03d4`: word code,
dword issuer at `+2`. An actor A: `+26h` position, `[A+4Dh] + 9` a byte
(signed) the attack codes read, `+45h` its group, `+96h` the hold byte,
`+99h` dead, `+A3h` set for the player's own mech (its group becomes the
order group), word `+29Ch`. A group: `+0Ch` the member table, `+10h` the
count (signed word).

### Contracts

All are cdecl; EBX/ESI/EDI are saved and restored where the prologue
pushes them, no other entry register is read unless named. Every game call
names its return address in the core (the shim's stack call writes it).

- **`menu_code` `0x43f9c0(menu)`**: e = `[menu+32h]` (EDX); code =
  `[menu+32h]` reread, plus 3 when bit 1 of byte `[menu+2Ch+e]` is set;
  EAX = ECX = code. Writes nothing.
- **`menu_toggle` `0x43f9f8(menu)`**: EDX = `[menu+32h]` − 6 (`add -4`,
  `sub 2`); for e − 4 below 2 unsigned (entries 4 and 5) ECX = `[menu+32h]`
  reread, byte `[menu+2Ch+ECX]` ^= 2, byte `menu+36h` = 1. EAX = menu.
- **`mfd_entry` `0x4470a4(G, i)`**: `[G+B1h]` = i when it differs. EAX =
  G, EDX = i, ECX the entry ECX.
- **`mfd_view_next` `0x446fc4(G)`**: `inc dword [G+318h]`, EDX = the
  reread value; equal to `[G+31Ch]` → ECX = 0, `[G+318h]` = 0. EAX = G.
- **`mfd_target` `0x446fb0(G)`**: G is not read; `cockpit_next`
  `0x43349c([49B088h])` with EAX = the cockpit, the entry EDX/ECX and the
  caller's EBX/ESI/EDI (return `0x446fbe`); `pop ecx`; its EDX:EAX.
- **`mfd_range` `0x446ff0(G)`**: i = `[G+C5h]` + 1, 0 when that is 3;
  `0x447014(G, i)` with EAX = G, EBX = i, the entry EDX/ECX (return
  `0x44700c`), which sets `[G+C5h]` = i and `[G+BDh]` = `[4D1C70h + 4i]`
  when i differs from `[G+C5h]` (always: PROVED below); EAX = i, EDX
  `0x447014`'s.
- **`mfd_order` `0x447248(G)`**: EBX = G, ESI = R = `0x4d03d4`, EDI = menu
  = `[G+D1h]`; `0x43f9c0(menu)` with the **entry EAX/EDX/ECX** (return
  `0x447262`), `pop ecx`; word `[R]` = AX; `0x43166c([G+14h], R)` with EAX
  = `[G+14h]`, EDX from `0x43f9c0`, ECX the popped slot (return
  `0x447270`); a nonzero EAX (all 32 bits) → `0x43f9f8(menu)` (return
  `0x44727d`). Returns the last callee's EDX:EAX.
- **`order_call` `0x43166c(H, R)`**: dword `[R+2]` = `[H+203h]` (the
  player), then `0x4231a4([49B0F8h], R)` with EAX = the player, EDX = the
  group, the entry ECX and the caller's EBX/ESI/EDI (return `0x43168b`);
  its EDX:EAX.
- **`group_order` `0x4231a4(Gp, R)`**: frame of 20h bytes: f−4 the best
  member, f−6 the result word s, f−10h..−7 ten done flags, f−12h the best
  index, f−18h the best distance, f−1Ah the best priority, f−1Ch the
  priority word w, f−20h the flag pointer. Word s = 0; memset
  `0x49681a(f−10h, 0, 10)` (EAX = f−10h, the entry EDX/ECX, the caller's
  EBX/ESI, EDI = R; return `0x4231c3`). Per pass: best distance
  7FFFFFFFh, best priority −1, best 0, flag pointer f−10h, i = 0; for
  `si < [Gp+10h]` (signed words; Gp is reread from its argument slot):
  skip a flagged member, a dead one (byte `+99h`) or the issuer `[R+2]`;
  then w by the code word `[R]` (reread per member) through the byte table
  `0x423239` and the jump table `0x423249`: codes 0, 2, 5 → w = (byte
  `[[B+4Dh]+9]` = 0), 1, 8, 0Fh → w = that byte (signed), 4 → w = (byte
  `B+96h` = 0), 7 → w = byte `B+96h` (signed); **3, 6, 9..0Eh and every
  code above 0Fh (`ja` at `0x42322a`) write nothing and use the word
  found at f−1Ch**. A member with w below the best priority is skipped;
  else `dist_gate` `0x49288c(B+26h, issuer+26h)` (EAX = the path's high
  half over w, EDX = issuer+26h, ECX = B+26h, EBX = B, ESI = i; return
  `0x4232b2`) and it becomes the best when its distance is below the best
  distance or its w above the best priority (then w, the distance, B and
  i are stored). After the pass: no best → end; else the best's slot +28h
  `(B, R, mode)` with the third slot = the high half of Gp over s (return
  `0x4232fc`), s = 2 when its AX is nonzero, else 1 when s was 0; the
  best's flag byte `[f−10h + s16 best index]` = 1; codes 0 and 2 with s =
  2 end the order, every other code starts the next pass. EAX = (s = 2),
  EDX as the last instruction left it (it can be the flag pointer, a
  frame address).
- **`mfd_button` `0x446844(G, button)`**: the index i of `button` in
  `G+18h[0..count)` (count reread per step); none → EAX = the count
  reached, EDX = the table slot; i above 0Ch (unsigned) → returned as
  well; the jump table `0x446873`: 0..5 → `0x446e60(G, i)` (EAX = ECX = i,
  EDX = the slot; returns `0x4468ae`, `0x4468bd` for 4, else `0x4468cc`);
  6 → nothing; 8 → `0x446ff0(G)`, byte `G + [G+C1h] + E5h` = 1; 7, 9 →
  `0x446fc4(G)` on page 0, else `0x446fb0(G)`, the redraw byte, dword
  `[[G + 4·[G+C1h] + CDh] + 0Ch]` = 64h; 10 → unless byte `0x49dbe0` is
  set `0x447248(G)` and the redraw byte; 11 → byte `[[49B088h]+203h] +
  96h` = 0, button 11 (`[G+44h]`) on when its byte `+40h` is clear (set,
  its slot +4 at `[[B+17h]+4]`), button 12 (`[G+48h]`): byte `+40h`
  cleared if set, its slot +4 in any case; 12 → the byte = 1, button 11's
  byte cleared if set and its slot +4, button 12 on when clear (with EBX
  = the button at that call). The caller's ESI/EDI reach the callees.
- **`mfd_click` `0x4470c0(G, W, P)`**: EDX = P, EBX = G; page `[G+C1h]` ≠
  1 → return (the entry EAX, P). Else for the six entries
  `[[G+D1h]+14h+4i]` the first whose rectangle (+4) holds P with
  **inclusive** signed edges (y first, then x; P reread for each test):
  with byte `0x49dbe0` set and i = `[G+B1h]` → `button_press(G, 0Ah, 1)`
  (EAX = the rectangle, EDX = P, ECX = the entry slot, ESI = i, EDI = P.x;
  return `0x44711d`), `0x447248(G)` (return `0x447126`), the page's
  redraw byte; else `0x4470a4(G, i)` (return `0x44713e`) and byte
  `G+E6h` = 1. No hit → EAX = the last rectangle, EDX = P.
- **`order_setup` `0x431530(H)`**: walks the actor list `[4A9B7Ah]`
  backwards with `pool_prev` `0x471c70(list, prev)` (EAX = ECX = prev, EDX
  = the list) to the first actor whose byte `+A3h` is set; its group
  `[A+45h]` → `0x49b0f8`; then its members `[[g+0Ch]+4i]` for `i <
  [g+10h]` (signed words, the group reread from the global each step) up
  to the first null: every member other than `[H+203h]` goes to the
  wingman table `0x4d03c8[n++]`, at most three. Then per wingman i
  `0x44a7e8([H+1F9h], i, x)` where x = the word `[W+29Ch]` over ECX's high
  half (the group pointer's or the previous call's; `0x44a7e8` reads only
  the word, `movsx` at `0x44a84f`/`0x44a85d`), and `0x4472bc([H+1EDh], n)`;
  its EDX:EAX. No local's address reaches a callee.

**ECX after the twelve.** Every caller pops or overwrites it before
reading it (`pop ecx` after `0x43f9c0`, `0x43f9f8`, `0x446fb0`,
`0x446fc4`, `0x446ff0`, `0x447248` in all their callers and after
`0x431530` at `0x461cc5`; `0x438e3c` reloads ECX after the owner call;
`0x447658` hands `0x4470c0`'s ECX to the widget library's slot +8 call
sites, which write ECX before reading it (see [Throttle slider event and
shield step](#throttle-slider-event-and-shield-step)); `0x43166c` passes
`0x4231a4`'s ECX to `0x447248`, which hands it only to `0x43f9f8` (written
before read) or to its callers' `pop ecx`), so the framework's entry ECX
is returned; EDX:EAX is exact.

**Branch sites** (47): the `BRANCH` lines of `invitro/branch_sites.h`
(section `mfd_orders`), all both ways except `0x447023` in `0x447014`,
which `mfd_range`'s direct cases run as real code: **PROVED**
fall-through only (i is `[G+C5h]` + 1, or 0 for 2, never the reread old
value).

### The residue word

Among the residue codes only 3 occurs: the MFD gives the entry, plus 3
with bit 1 of its flag byte, and bit 1 is written only by `0x43f9f8`
(entries 4 and 5; the only other writers, `0x43fa1c` and `0x43fa3c`, keep
or set bit 0), so from flag bytes that start without bit 1 the codes are
0..5, 7 and 8; code 3 is Alt+O (item 3) or a click on entry 3. With w ≥
−1 every eligible member gets the order in distance order, with w < −1
nobody. w is the word at 460870's EBP−1CCh on the Alt+O path
([input.md](input.md#the-residue-reader-the-group-order-0x4231a4)): what the logic frame's earlier rows and
`hud_key`'s earlier callees left there. **No static proof of its value is
possible**, so the hooks keep the original's stack instead:

- **Entry shims.** `group_order` runs at the original stack coordinate
  (frame shim): its locals are the original frame, w
  is read from f−1Ch and every local is written back where the original
  writes it (a later reader at the same depth finds the original's bytes).
  So are its callers on the path (`order_call`, `mfd_order`,
  `mfd_button`, `mfd_click`) and `mfd_target`, `mfd_range`: their game
  calls run through `ai_behaviour_stack_call` at the original outgoing ESP
  with the original's EBX/ESI/EDI, EBP and return address. The four pure
  leaves get gap shims (PUSH EBP, 4 probed pages): a hooked function's
  dispatch writes below its entry (the extended path saves the x87 image
  over EBP−48h of `0x447248`, the word `0x4231a4` reads later, when
  `0x43f9c0` is called just before the order; `0x43f9f8` after the order
  would leave it for the next reader); the gap moves the dispatch away in
  every mode.
- **Gap sizes** decrease along every nesting: 28 pages for `mfd_button`
  and `mfd_click`, 26 for `mfd_order`, `mfd_target`, `mfd_range`, 24 for
  `order_call`, 22 for `group_order` (88 KiB for the members' order
  methods and a nested hook in compare mode), 4 for the leaves; under
  `hud_key`'s 32 the probe of `mfd_order` (through `0x446c38`) or
  `mfd_button` (through `button_press`'s host frames) ends in hud_key's
  unused gap. **Trap:** with equal sizes an inner probe ends inside the
  outer shim's live host frames; nothing is stored between the gap's end
  and the floor (`es2_machine_floor`) for that reason
  ([main_loop.md "Shim stack budget"](main_loop.md#shim-stack-budget)).
- **Evidence.** In vitro, the chain pass (4,096 cases per entry over the
  real chain, hooks uninstalled or replaced at random) equals the
  all-original run including the 1,024 bytes below the caller's slots;
  code 3 is given over the 3131h fill and refused over A7A7h in every
  combination.
  Live, `c3_09-wingman-orders` and `c3_09-orders-matrix` (Alt+O, code 3)
  stay identical with the group replaced.
- **Not covered.** Callers without the original coordinate stay as they
  are: `button_press` calls the button's slot +8 through the plain bridge,
  so `0x438e3c` → `mfd_button` runs on host stack whenever `button_press`
  is replaced (the D/X keys, mouse clicks on MFD buttons), and the mouse
  dispatch reaches `0x447658` → `mfd_click` likewise; there the residue
  is the host's in both configurations (the shims do not change it). A
  shimmed hook in `original` mode runs its original body below its gap
  (the framework calls the trampoline from host frames), so a bisect step
  with this group `original` does not reproduce the residue; uninstalled
  hooks do (the same holds for `hud_key`, `command_key`, `player_input`).
- **Group size.** The ten done flags lie at f−10h..−7: the flags of members
  10 and 11 are the bytes of s (the core keeps the frame in memory, so this
  aliasing is the original's); from 13 members on, the flags of members
  12..15 are the best-member dword, which every pass clears, so the
  original can order such a member forever; 17 and more reach the saved
  EBP. Outside the contract; the player's group has at most four.

### Verdicts

- `0x446c38` (the MFD's Alt order handler: `hud_key` calls it directly
  for the codes 212h..22Eh; entry select `0x43f9e0`, the order `0x447248`,
  the MFD selection `0x447158`) and `0x4469e8` (the MFD key method) issue
  orders or select the cockpit target, so they are H4; both are hooks of
  the group `mfd_keys` ([MFD keys](#mfd-keys)).
- `0x431530`: H4 (it writes the order group `0x49b0f8` that `0x43166c`
  reads and the wingman table `0x4d03c8` that `0x433134` and
  `wingman_order` read).
- The bridged original callees: `0x4472bc` (the MFD status-view list
  `+308h..+31Ch`, read by `0x446fc4`, the page switch `0x446e60` and the
  per-frame draw `0x446350`) H3; `0x447014` (the radar range index and
  value `+C5h`/`+BDh`, read by the MFD radar filter `0x447284`) H3;
  `0x446e60`, `0x44a7e8` H3; the radio buttons' slot +4 (widget redraw)
  H3; memset `0x49681a` RTL. Verdict rows in
  [function_verdicts.md](../reference-game/function_verdicts.md#mfd-order-path).

### Validation

In vitro (`//tools/reference-game/invitro:differential_mfd_orders_test`,
quick tier, two shards): 100,000 direct cases per hook against logging
stubs (memset and `0x447014` real), 1,024 installed cases per shimmed hook
through the shims, 256 installed 31/a7 cases for `order_setup`, the chain
passes, `order_setup` over the real `pool_prev` (2,048) and 512 compare
probes per leaf; branch sites as above; the core unit test
`//core:mfd_orders_test`.

## Gauges, MFD, HDD and the radio panel in the HUD line

The per-module part of the HUD line contract; the overview, the
coupling-site table and the per-frame order are in
[hud.md](hud.md#the-hud-line-contract--coupling-set-and-frame-writes), the
own code of each draw in the hud.md sections linked below. Everything here
is read statically from the listing (byte scan of the call sites, vtables
by RTTI name). C = clock read `0x467828`, B = stream-B draw, A = sound-API
call; G = the gauge (`this`), H = `[0x49b088]`, D = `[0x4cf99c]`; "cond" =
`0x4342e0(2)` = `0x438700(condition_ratio(P+0x206, 0, 2))` (hook
`condition_ratio`), which every gauge evaluates anew. In the oracle suites
cond is 0 in nearly every frame (hud.md coupling table, row `0x438bde`);
the meaning of part (0, 2) was not read.

### The panel element classes

From the RTTI names and the vtables (slot = offset/4):

| Class | vtable | slot 0 (click) | slot 1 (full redraw) | slot 2 (draw, per frame) | slot 4 (`+0x10`) | slot 5 (`+0x14`) | slot 6 (`+0x18`) | slot 7 (`+0x1c`, sweep) |
|---|---|---|---|---|---|---|---|---|
| `EnergyWeaponGauge` | `0x49c91c` | `0x440f18` | `0x440c90` | `0x440cb8` | `0x440d74` | — | — | (inline `0x440eac`) |
| `ProjectileWeaponGauge` | `0x49c8fc` | `0x4414dc` | `0x4411dc` | `0x441290` | `0x44141c` | — | — | none |
| `TogglePodGauge` | `0x49c8a4` | `0x441a24` | `0x441744` | `0x441878` | `0x44343f` (empty) | `0x4419ec` | `0x4419a0` | `0x443444` |
| `PodWeaponGauge` | `0x49c8d0` | `0x4419b0` | `0x441744` | `0x441878` | `0x44343f` (empty) | `0x441970` | `0x4419a0` | `0x443444` |
| `EnergyPodGauge` | `0x49c878` | `0x441c04` | `0x441c3c` | `0x441da0` → `0x441878` | `0x44343f` (empty) | `0x441bac` | `0x4419a0` | `0x441db0` |
| `WeaponRangeGauge` `[H+0x1e1]` | `0x49c85c` | `0x442154` (`gauge_button`, hook) | `0x442080` | `0x4420a4` | — | | | |
| `EnergyPoolGauge` `[H+0x1e5]` | `0x49cabc` | `0x445130` | `0x444f48` | `0x445008` | | | | |
| `ShieldsGauge` `[H+0x1e9]` | `0x49ca1d` | `0x443834` | `0x443758` | `0x443770` | | | | (inline `0x4437cc`) |
| `ThrottleGauge` `[H+0x1f1]` | `0x49d1e0` | `0x447e08` | `0x447d60` | `0x447d94` | | | | |
| `MFDGauge` `[H+0x1ed]` | `0x49cfa0` | `0x446844` (`mfd_button`, hook) | `0x446160` | `0x446350` | | | | |
| `HDDGauge` `[H+0x1f9]` | `0x49d64c` | `0x44a174` | `0x449a78` | `0x449bf8` | | | | |

Slot 3 (`+0xc`) is the key method (`0x45236c` the common one; `0x4421c8`
WeaponRange, `0x4469e8` MFD (hook `mfd_key`), `0x449ff4` (hook
`hdd_key`)). The ten elements `[H+0x70+4i]` hold gauges of the first five
classes (hypothesis: one per weapon of the player, built by the gauge
factories of `0x431bf8`).

### The weapon-gauge draw (slot 2) and its coupling

`0x440cb8` (energy), `0x441290` (projectile) and `0x441878` (pods; the
EnergyPod slot `0x441da0` forwards to it) share one pattern; the full
contracts, including the sweeps `0x440eac`/`0x441db0`/`0x443444`, are in
[hud.md](hud.md#the-panel-weapon-gauges-frame-draws) (own code
`sim/src/hud_gauges.c`). Coupling: the gate is `0x431410(G+4)` (hook
`panel_visible`) or the transition byte `D+0x1c`, plus powered `G+0x8c`;
until the sweep is done (`G+0x8d`) the energy and EnergyPod sweeps read
the clock once per draw (elapsed time from `0x438ddc`'s `G+0x90`;
`0x443444` sets `G+0x8d` without a clock; the Shields sweep `0x4437cc` is
under [Bar and gauge
family](#bar-and-gauge-family-power-on-sweeps-balance-numbers)); then,
unless the first full redraw (slot 1, `G+0x8e` ← 1) ends the frame,
`G+0x74` ← cond and cond ≠ 0 → `0x438bc0(G)`. The
redraws by the blink phase `G+0x76` and the counter `G+0xb0` have no
coupling.

**Blink `0x438bc0(G)`** (`0x438bc0..0x438d6b`; own code `hud_gauge_blink`
in `sim/src/hud_widgets.c`,
[hud.md](hud.md#the-message-queue-the-ports-and-the-gauge-blink)):
`G+0x77` ← `G+0x76`. `G+0x74` = 0 → `G+0x76` ← 1, return (no clock;
unreachable from the six callers, which call only with cond ≠ 0). Else C
(= now), then by the phase `G+0x76` (nonzero = blanked) and the deadline
`G+0x78`:

- deadline 0: `G+0x78` ← now + `0x438d6c(G, lo, hi)` with (lo, hi) =
  (`+0x7c`, `+0x80`) while blanked, (`+0x84`, `+0x88`) while shown;
  `0x438d6c` returns lo when lo = hi, else lo + (B as unsigned 16 bits mod
  (hi − lo)) (one B draw);
- now > deadline, no animation (`G+0x6c` = 0): flip the phase, deadline ← 0;
- now > deadline with an animation: idle (`0x471f24` = 0) → select clip
  word `+0x72` (blanked) / `+0x70` (shown) with `0x471de0`, start `0x471e10(anim,
  G+4)` (C); running → `0x471e88(anim, 0)` (C while its state word is 1;
  state 2 → reset to 0 and AL = 0, which flips the phase and clears the
  deadline).

### `0x446350`: the MFD draw (slot 2), per frame

Read at `0x446350..0x446831`. F = the MFD gauge, page = `F+0xc1` (0..5:
objects `F+0xcd + 4·page`; page 3 = `F+0xd9` is the radar built by
`0x43e734`, pages 0 and 4 objects of `0x43a2e0`, 1 `0x43f600`, 2
`0x4404bc`, 5 `0x43faf4`, all from the constructor `0x445240`). Own code:
`sim/src/mfd.c` ([hud.md](hud.md#the-mfd-and-hdd-frames-and-pages-as-own-code)).

1. Mode `D+0x14` = 4, not visible (`0x431410(F+0xeb)`) or not powered →
   return.
2. Sweep not done (`F+0x8d` = 0): page 3 → the power-on animation
   `[F+0x331]`: running (dword `+6` ≠ 0 and word `+0` ≠ 0) → `0x471e88`
   (C in state 1); else `0x471e10(anim, [F+0xd9]+0x18)` (C) and C again
   (`F+0x345` ← now + 70); then done when the animation word is 0. Other
   pages: C; done when now > `F+0x345`. When the animation exists and the
   sweep is done its four objects are freed and `F+0x331` ← 0.
3. cond → `F+0x74`; ≠ 0: `0x446ddc(F)`, `0x438bc0(F)`.
4. Return in the blanked phase (`F+0x76` ≠ 0) or while the animation
   `[F+0x6c]` runs (`0x4464db..0x4464fe`).
5. The 13 buttons (no coupling), `0x44718c(F)` (page auto-switch for the
   selected weapon: `0x446e60` → `button_press`, C in `button_release`).
6. **Talking-head overlay**: word `0x49cbc6` = −1 and the HDD's current
   channel `[[H+0x1f9]+0x766]` ≠ 0 with frames (`+0x1c` ≠ 0) → draws the
   overlay (`0x45caa4`), sets `F+0x328`, and **skips step 7**.
7. Otherwise the page refresh, with the flag `F+0xe5[page]` (set after a
   full redraw, `0x446735`): pages 0 and 4: C; now > `F+0x100` → `F+0x100`
   ← now + 30, page 0 copies the counter-selected value
   `[F+0x308 + 4·[F+0x318]]`, page 4 the cockpit target, then the page's
   slot 1; pages 2, 3, 5: the page's slot 1 every frame (page 3:
   `0x43eedc` = the blip scan `0x43ec08` (C per qualifying actor), then the
   radar draw `0x43eef4`); page 1 and above 5: one slot-1 call, then the
   flag is cleared. Pages 2 and 5 also draw a label.

So the MFD reads the clock once per frame on pages 0 and 4 (refresh flag
set, no overlay), and the radar's blip clocks run from here only on page
3; the HUD's own radar (`0x43f2d8`) skips the scan exactly when the MFD
shows page 3 (hud.md, quirk 5 of [the original quirks](hud.md#original-quirks-found-keep-them)).

### `0x449bf8`: the HDD draw (slot 2), per frame

Read at `0x449bf8..0x449ee3`. E = the HDD gauge. Own code: `sim/src/mfd.c`
([hud.md](hud.md#the-mfd-and-hdd-frames-and-pages-as-own-code)).

1. Target bookkeeping (`E+0x544`, `E+0x55e`, `0x49d3dc`; no coupling) and
   **`0x44a1a0(E)`**, the HDD button executor (hook `hdd_execute`, [HDD
   button executor](#hdd-button-executor)); its DialogPort posts
   (`0x44a748`) draw B and queue (C).
2. The wingman flags: for each dead wingman i (`[0x4d03c8+4i]+0x99`): in
   mode 4 during a transition (`D+0x1c`) its record byte `+0x147` ← 1;
   with `+0x147` set its state `+0x13b` ← 1.
3. **`0x44b620(E)`**, the radio panel, every frame and in every mode.
4. Mode 4 → return. cond → `E+0x74`; ≠ 0 → `0x438bc0(E)`.
5. Not visible (`0x431410(E+0xc1)`) → return. The buttons.
6. Page `E+0x109`: 0 → with `D+0x1c` the map scroll helpers `0x44cff8`/
   `0x44d05c` and a redraw request; 1 → C; now > `E+0x530` → `E+0x530` ←
   now + 30 and a redraw request; other pages nothing.
7. Redraws by `E+0xb0` (2 → 1 → 0): `0x449ee4`/`0x44a704` (no coupling)
   or `0x449f64` = the page's slot 1; for the map page (`HDDMap`, vtable
   `0x49d9d8`) slot 1 is `0x44c988` (hook `map_frame`, [HDD map
   frame](#hdd-map-frame)).

**The map frame's coupling (`0x44c988`).** With `M+0x14d` → `map_areas`
(hook) first; then C (always); the blink counter word `0x49d6b8` (mod 4)
counts the rising edges of bit 4 of that clock value (latch `0x49d7e6`);
now > `M+0x149` → `M+0x149` ← now + 30 and `0x44d370`; then the click
handling (`map_click`, hook, or `0x44d82c`, hook `wingman_pick`), labels,
and, when the map redraw byte `0x49d6ae` is set (cleared afterwards), the
draw `0x44e334`, which calls `0x44e954` once per map icon: C when the
icon's actor A exists, is not (dead/removed with side ≠ 0), has byte
`A+0x95` = 0 and (side `[[A+0x45]+0x12]` ≠ 0 or group `[[A+0x45]+0x14]` ≠
0); bit 5 of that clock value hides the icon this frame.

### The radio panel: `0x44b620`, `0x44b514`, `0x44b344`, `0x44b148`

Read at `0x44b620..0x44b861`, `0x44b514..0x44b5e7`, `0x44b344..0x44b3db`,
`0x44b148..`. E = the HDD gauge (`[0x49d230]` in the callback), three
wingman records W_i = E + `0x12d` + `0x14e`·i with the channel `W+0x137`
(dword), the state `W+0x13b`, the previous state `W+0x13f`, the timer
`W+0x143`, flags `W+0x147`..`W+0x149`, the speaker byte `W+0x12d` and the
voice channel `W+0x12f`. Own code: `sim/src/mfd.c`, row `hdd_radio`
([hud.md](hud.md#the-radio-panel-0x44b620), which has the per-state
contract); `0x44aea0`, `0x44b3dc`, `0x44b344`, `0x44b148` are rows called
through the dispatch.

**Per frame (`0x44b620`)**: `0x4632c4` (the lip queue: C; when the newest
due entry's time has come, it is removed and `sfx_lip_due` → `sfx_lip`
(C) run for it), then C = now, then for i = 0..2 with a channel: the HDD's
current channel `E+0x766` is cleared when it is that record's actor and
byte `W+0x147` is set or the state is 0; then

| State | Action | Coupling |
|---|---|---|
| 3 (static after a message) | now < timer → `0x44b3dc`; else state ← 0 and on as 0 | — |
| 0 | `W+0x13f` ≠ 0 or byte `0x49d4f4` → `0x44aea0(E, i)`, `0x43122c(1, W+0x1d, 0)`; byte `0x49d4f4` ← the copy's AL xor 1 | — |
| 1 (waiting) | now < timer or `W+0x147` → `0x44b3dc`; else state ← 2 and on as 2 | — |
| 2 (speaking) | entered this frame: DialogPort phase flag (`0x435b14`), `sfx_voice(W+0x12f)` (hook: C, and `sfx_lip` C when its script is not empty), AL ≠ 0 → `0x46356c` (current lip frame); speaker `0x25` → timer ← now + 5. Already in state 2: `0x46356c`. Then lip frame ≠ −1: speaker `0x25` → `0x44b344` else `0x44b148(E, W, frame)`; −1 (voice over): `0x435b38`; speaker `0x25` → `W+0x147` ← 1, state 1; else state 3, timer ← now + 20, `0x462af0(0x1c)` = 0 → `sfx_play(0x1c)`, wingman i dead → `W+0x147` ← 1; then `0x44b3dc` | A, and the C/B of the callees |

then `W+0x13f` ← state. `0x44b344(E, W)`: C; by `W+0x148` (0: `0x44b148(E,
W, 0x1b)` and past the timer `+0x148` += 1; 1: `0x44b3dc` and past the timer
−= 1); when `+0x148` differs from `+0x149`: timer ← now + max(5,
`rng_bounded(B, 20)`) (one B); `+0x149` ← `+0x148`. `0x44b148(E, W,
frame)`: B when `W+0x133` ≠ 0 (a variant 0..2 of the mouth graphic), then
draws.

**DialogPort start callback `0x44b514(entry)`** (registered by the HDD
constructor, `0x4498dd`, into the port's callback list `+0x4b9`): the
entry's actor is a wingman i (`0x433134`): `0x44aff0` (voice start through
`0x462b9c`: C); started → C, timer ← now + 20, state ← 1, `0x462af0(0x1c)`
= 0 → `sfx_play(0x1c)`, **a full `0x44b620(E)`** (all of the above again,
inside the DialogPort frame, `0x44b58c`), the MFD overlay channel
`E+0x766` set when free; not started → the phase flags
`0x435b14`/`0x435b38`. This second caller of the radio panel is in
[hud.md](hud.md#the-radio-panels-caller-outside-the-frame-walk).

### `0x43ec08`: the blip scan

Read at `0x43ec08..0x43eeda`; own code in `sim/src/hud_draw.c`
([hud.md](hud.md#the-scroll-zones-and-the-radar-page)). Clears the blip
count `R+0x518`/`+0x51c`, reads the range `[R+8]+0xc`; player byte `+0x96`
(R key, `r_key_toggle`) against `R+0x34`: on a change `button_press` of
button 11 (byte 0) or 12 of `0x445234(R)`'s object (C in
`button_release`) and `R+0x34` ← it. Then per actor of `[0x4a9af8]` (count
word `0x4a9afe`; a null entry ends the list): skip the player, dead or
removed actors (`+0xa4`, `+0x99`), a group with `+0x14` ≠ 0, a hidden
building (word `+0x1a8` = 1 and `[[+0x1f2]+0x1e]` ≠ 0); **C when byte
`+0x95` = 0 and the side `[[+0x45]+0x12]` = 1**, and bit 5 of that value
skips the actor this frame; then the position (`+0x26` or the remembered
`+0x1aa` unless `+0xa7`), `dist_octagonal`, the range test and the blip
record. No B.

### The widget slots (classification)

The gauge-side slots the weapon syncs call
([weapon_classes.md](weapon_classes.md#the-weapon-gauge-syncs-the-laew-emission-and-the-empty-bodies)), with the vtables above:

| Function | Class / slot | Role (read from the bytes) | Coupling | Verdict |
|---|---|---|---|---|
| `0x4419a0` | pod gauges slot 6 (`+0x18`) | EAX = G + `0xc2` (the 8-byte state record) | none | H3 |
| `0x4419ec` | TogglePod slot 5 (`+0x14`) | bytes 0 or 1 of the record differ → copy both dwords to `G+0xc2`, redraw `G+0xb0` ← 2 | none | H3 |
| `0x441970` | PodWeapon slot 5 | byte 1 differs → copy, redraw | none | H3 |
| `0x441bac` | EnergyPod slot 5 | any of bytes 0, 1 or dword 4 differs → before the sweep is done the target `G+0xe2` ← dword 4; copy, redraw | none | H3 |
| `0x440d74` | EnergyWeapon slot 4 (`+0x10`) | gauge record from the sync: value `+4` → `G+0xc6` and the second bar (its slot `+0x14`); `+0` → `G+0xc2` (and the sweep target `+0xdb` before done); the eject byte `+0x11` → button states `+0x1b` (2 or 0) unless the jolt offset `0x42d870` is nonzero (then `G+0xc1` ← 1, retried later); bytes `+0x14..+0x16` → `G+0xd6..+0xd8`; redraws | none (`0x42d870` only reads the jolt) | H3 |
| `0x44141c` | Projectile slot 4 | the same without the second bar and the jolt test; redraw `G+0xb0` ← 2 | none | H3 |
| `0x44343f` | pod gauges slot 4 | empty body (`push ebp; mov ebp,esp; pop ebp; ret`) | none | H3 |
| `0x440e9c`, `0x4414cc` | getters of `beam_sync`/`gun_sync` | EAX = G + `0xc2` | none | H3 |
| `0x441a24`, `0x441c04` | TogglePod / EnergyPod slot 0 (click) | a click on button 0 toggles byte `G+0xc2` (bit 0) and requests a redraw: **the toggle the weapon syncs read through slot `+0x18`** | none | H3 (input path: an own HUD must write the same byte on the same click) |
| `0x443444` | Toggle/PodWeapon slot 7 | sweep: `G+0x8d` ← 1 at once | none | H3 |

None of them reads the clock, draws stream B or calls the sound API; they
write only the gauge and its widgets. For the HUD line they are state, not
coupling: the syncs (own code) call them every rendered frame from
`weapon_frame`, and the record they leave (`G+0xc2..`) is what the next
sync reads. A headless HUD may drop their redraw requests but must keep
the record writes (`G+0xc2`, `G+0xc6`, `G+0xe2`, `G+0xdb`, the button
states `+0x1b`). Own code: the rows in `sim/src/hud_gauges.c`.

### Verdict rows

In the `## Verdicts` table of
[function_verdicts.md](../reference-game/function_verdicts.md):
`0x435298`, `0x435b58`, `0x438d6c`, `0x438ddc`, `0x438e2c`, `0x43e114`,
`0x43eedc`, `0x440eac`, `0x441da0`, `0x441db0`, `0x443444`, `0x4437cc`,
`0x4421c8`, `0x4419a0`, `0x4419ec`, `0x441970`, `0x441bac`, `0x44141c`,
`0x44343f`, `0x440e9c`, `0x4414cc`, `0x441a24`, `0x441c04` (H3) and
`0x4632c4` (platform, audio). None is an H4 hook; the coupling ones are
part of the HUD line's state machine, whose own code is the sim (hud.md).

## HDD map frame

`HDDMap` slot 1 `0x44c988(M)` (vtable `0x49d9d8`, fields in
[Objects](#objects) and [HDD map order controls](#hdd-map-order-controls))
is the hook `map_frame` of the adapter group `hdd_map`
(`tools/reference-game/proxy/hdd_map_hooks.h`; core `core/src/hdd_map.c`,
`es2/core/hdd_map.h`). Contract read instruction by instruction from the
listing, range `0x44c988..0x44cc64`.

**Why a hook.** The function's own effects are not only drawing: it reads
the clock `0x467828` on every call (coupling C, hud.md coupling table row
`0x44c9a3`, "must run" in its headless list), it runs the click dispatch
into the hooks `map_areas` and `map_click` and the wingman pick
`0x44d82c` ([HDD map key path](#hdd-map-key-path)), and it clears the
selected wingman's order bytes when the order's target is dead (state the
wingman-order path reads). **Trap:** it stores the address of its own local
`[EBP−0xc]` in the graphics context, so a replacement on host stack would
put a host address there; the entry shim (below) runs the replacement at
the original stack coordinate, so the context holds the original's address
and the draws read the original's bytes. The drawing callees stay guest
calls (H3).

**Callers.** Only through the page slot call of `0x449f64` (`call [ecx+4]`
at `0x449fb8`, `POP ECX` at `0x449fbb`); `0x449f64` is called only by the
HDD draw `0x449bf8`, slot 2 of `HDDGauge`, which `panel_frame` calls as
the element virtual +8. In the oracle map `0x44c988` and `0x449f64` have
equal call counts in every suite. `0x449f64` saves and restores
`[ctx+4..+0x208)` of the graphics context around the call, not `+0x22c`.

**Frame.** `push ebp; mov ebp,esp; add esp,−0x20; push ebx; push esi`:
locals `EBP−0x20..EBP−1`, EBX at `EBP−0x24`, ESI at `EBP−0x28` (outgoing
ESP). EBX = M throughout; EDI is never written; ESI is the caller's until
the mode buttons (step 9). Locals: `−0x20..−0x14` the rectangle of step 9,
`−0xd` the current button's flag byte, `−0xc` dword 0, `−8` the map
path's clip word, `−4` the graphics context X read once from
`[0x6c5f70]`. The 12-byte record at `EBP−0xc` is what `[X+0x22c]` points
at after steps 9 and 11 (`0x44cb2e`, `0x44cc32`); the function never
restores the old pointer, so after it returns `[X+0x22c]` points into its
dead frame. On the button path (step 9 without step 11) `[EBP−8]` is not
written, so the draws there read the stack residue of earlier frames at
that address.

**Steps** (registers as the original holds them at each call; every
memory value read where the original reads it):

1. Byte `M+0x14d` ≠ 0 → `map_areas(M)` with the entry EAX/EDX/ECX
   (`0x44c99d`); ECX ← the argument slot.
2. The clock `0x467828()` (`0x44c9a3`), EAX = t. Bit 4 of t set: latch
   byte `0x49d7e6` = 0 → the word `0x49d6b8` is incremented, reread,
   sign-extended, reduced to its signed remainder modulo 4 (`and
   0x80000003`, for a negative value `dec; or −4; inc`), stored back
   (low 16 bits) and left in EDX; latch ← 1. Bit 4 clear: latch ← 0.
3. t > dword `M+0x149` (unsigned, `jbe` at `0x44c9ed`) → EAX = t + 30,
   `M+0x149` ← EAX, the tick `0x44d370(M)`.
4. ECX ← 0, EAX ← X = `[0x6c5f70]`, `[EBP−0xc]` ← 0, `[EBP−4]` ← X.
5. Byte `M+0x11c` ≠ 0 → byte `M+0x103` ≠ 0 and dword `M+0x105` ≠ −1:
   `map_click(M)`, else `0x44d82c(M)` (EAX = X, ECX = 0); ECX ← the slot.
6. e = `[M+0x105]` (EAX) ≠ −1: R = 42·e + M + 0x85 (EAX = EDX = R,
   32-bit wrap); dword `R+0x22` = 1 → (byte `R+0x20` ≠ 0 and byte
   `[R+0x18]+0x99` ≠ 0) or (byte `R+0x21` ≠ 0 and byte `[R+0x1c]+0x99` ≠
   0, tested only when the first does not hold) → bytes `R+0x20`,
   `R+0x21` ← 0. EDX = the last target + 0x92 read.
7. s = `[M+0xc]`, EAX = s − 3; s ∉ {1, 2} → return EDX:EAX.
8. Byte `0x49d6ac` ≠ 0: e = `[M+0x105]` ≠ −1 → `0x44f644(b)` with b =
   `[[M+0x31] + 4e]` (EAX = e, EDX = `[M+0x31]`, ECX = b), then
   `[M+0x31]` and `[M+0x105]` reread, the slot popped, byte `+0x1f` of
   the reread button ← 0; byte `0x49d6ac` ← 0.
9. Byte `M+0x139` ≠ 0: `0x44de14(M)`, the rectangle (`[[M+0x3d]+4]`,
   `[[M+0x3d]+8]`, `[[M+0x59]+0xc]`, `[[M+0x59]+0x10]`) into
   `EBP−0x20..−0x14`, `0x43122c(1, EBP−0x20, 0)` (EAX = `EBP−0x20`, EDX/ECX
   the last two words). Else, previous mode `[M+0x81]` ≠ mode `[M+0x5d]`:
   ECX = `EBP−0xc`, `[X+0x22c]` ← it (X reread from `[EBP−4]`); for the
   previous mode k ≠ −1 and then the current mode k ≠ −1 (each reread):
   ESI = k − 10, the enable byte `M+0x131+ESI` and the button B =
   `[M+0x3d+4·ESI]`; B's colour dwords `B+0`, `B+0x21` ← (`0x49b0b0`,
   `0x49b0b4`) enabled or (`0x49b0ac`, `0x49b0ac`) disabled for the
   previous mode, (`0x49b0b8`, `0x49b0b4`) or (`0x49b0ac`, `0x49b0ac`) with
   the flag byte `[EBP−0xd]` ← 0 or 1 for the current one; the label
   `0x438aac(B, [0x4d12a8+4k], byte [[0x4d12f0+4k]])` (k reread, EAX = B,
   EDX/ECX the last two); `0x44dd74(M, ESI, 0, f)` with f = 1 for the
   previous mode and, for the current one, 438aac's EAX with AL ← the flag
   byte. Then `M+0x81` ← `[M+0x5d]`.
10. Byte `M+0x144` ≠ 0 → the label redraw `0x44dd14(M)`.
11. Byte `0x49d6ae` ≠ 0 (the map redraw request): `[X+0x22c]` ← `EBP−0xc`
    (EDX), `[EBP−8]` ← s16 `[0x4d3ba2]`, `0x486704(M+0x71)` (EDX =
    `EBP−0xc`, ECX = X), the map draw `0x44e334(M)`, EDX ← 0, `M+0xc` ← 0,
    byte `0x49d6ae` ← 0.

Return EDX:EAX as left; ECX is popped by the caller and not reproduced.
25 conditional branches, all reachable. Mode values in the game are −1
and 0xa..0x11 (the eight buttons); the contract holds for any index (the
reads wrap); the in-vitro cases also use 0..9 except 7 (whose button
slot `M+0x31` is the wingman-button array).

**Guest callees (verdicts).** The tick `0x44d370` (sets the blink and
map-redraw bytes `0x49d6ac`/`0x49d6ae`, toggles `0x49d6ad`, requests the
gadget redraw, counts the label timer `[[0x49d6b0]+0x13e]` down and sets
`+0x144` at 0), the wingman-button blink `0x44f644` and the button bitmap
`0x44dd74` are H3 rows in
[function_verdicts.md](../reference-game/function_verdicts.md), as are
`0x44de14`, `0x43122c`, `0x438aac`, `0x44dd14`, `0x486704`, `0x44e334`;
`0x467828` is the platform clock, `0x44d82c` is the hook `wingman_pick`.
None of the three first ones reads the clock, draws stream B or calls the
sound API. `0x44c988` is H4.

**Replacement.** The core keeps the locals at the original's offsets
below a frame pointer f and stores f − 0xc in the context. The entry shim
`hdd_map_entry` does PUSH EBP, stores EBX/ESI at
`EBP−0x24`/`−0x28`, probes 28 pages, continues at the floor (`es2_machine_floor`) and leaves
{EBP, `EBP−0x28`, EBX, ESI, EDI} for the adapter; f is then the original
EBP and every game call runs through `ai_behaviour_stack_call` at
`EBP−0x28` with the EBX/ESI/EDI and return address the core names, so the
record, its residue word and the callees' frames are the original's. The
shim is reached only below `panel_frame`'s 48-page gap (about 0.6 KiB of
game frames for `0x449bf8` and `0x449f64`), so the 28-page probe ends
inside it. Without the shim the plain bridge is used and the locals live
in a zeroed scratch. Callee counters `hk_map_frame_call_<va>` for the 13
callees. Compare refused (game calls, draws).

**In vitro** (`//tools/reference-game/invitro:differential_hdd_map_test`,
`invitro/hdd_map_cases.h`; README "Harness notes per group"): quick tier 100,000
direct cases, 1,024 installed cases through the entry shim (31/a7 stack,
residue below each callee and the residue word `[EBP−8]`), 2,048
real-callee cases; all 25 branch sites both ways, none proved. Witness: a
core that also writes `[f−8]` ← 0 passes all direct cases and fails an
installed case (the residue word read by `0x438aac`). Mutation witness in
[MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md).

## HDD map key path

The keys of the HDD map page and the simulation-side functions they reach
are nine hooks of the adapter group `hdd_map_keys`
(`tools/reference-game/proxy/hdd_map_keys_hooks.h`; core
`core/src/hdd_map_keys.c`, `es2/core/hdd_map_keys.h`): `map_key`
`0x44cc68`, `map_press` `0x44d5c0`, `wingman_pick` `0x44d82c`,
`wingman_index` `0x433134`, `mode_next` `0x44ee48`, `mode_prev` `0x44ee88`,
`target_cycle1` `0x44ef08`, `target_cycle0` `0x44ef1c`, `target_cycle`
`0x44ef30`. Contracts read instruction by instruction from the listing;
the two byte tables were read as bytes. Fields of M as in
[Objects](#objects) and [HDD map order controls](#hdd-map-order-controls).

**Callers (from the listing).** `0x44cc68` is slot 4 (`+0x10`) of the
`HDDMap` table `0x49d9d8` and is called only through `hdd_key`'s page slot
call (`call [edx+0x10]` at `0x44a165`, then `add esp,8` and return; [HDD
key method 449ff4](#hdd-key-method-449ff4)). `0x44d5c0` has ten call
sites, all in `0x44cc68`. `0x44d82c` is called only by `map_frame`
(`0x44ca31`). `0x433134` has nine call sites (`0x435c72`, `0x435d2c`,
`0x435fe8`, `0x43a7e1`, `0x44b520`, `0x44b5f2`, `0x44d857`, `0x44e102`,
`0x45e305`), each followed by `POP ECX`. `0x44ee48`, `0x44ee88`,
`0x44ef08`, `0x44ef1c` are each called once by `0x44cc68`, `0x44ef30` only
by `0x44ef08`/`0x44ef1c`.

**`map_key` `0x44cc68(M, code)`.** k = the low word of code; EAX = k − 0xe.
k − 0xe > 0x3e (unsigned, `ja` at `0x44cc79`) → EAX = 0, return (EDX = the
entry EDX). Otherwise the byte table `0x44cc8c` (63 bytes) gives a jump
index j into the table `0x44cccb` (16 dwords); AL ← j over EAX = k − 0xe
(< 0x3f), so EAX = j at every case. j = 0 (every code not listed) → EAX = 0,
return. The handled keys (scan codes) and their cases:

| Code | Key | j | Action (registers at the call) |
| --- | --- | ---: | --- |
| `0x20` `0x1e` `0x21` `0x14` `0x22` `0x18` `0x2e` `0x12` | D A F T G O C E | 8 9 7 12 6 11 4 13 | `map_press(M, [M+0x35], 0xa … 0x11)` in that order; `[M+0x35]` is loaded into EDX (0xa, 0xd, 0x10), ECX (0xb, 0xe, 0x11) or EAX (0xc, 0xf) |
| `0x2d` / `0x0e` | X / backspace | 5 / 15 | `button_press([M+8], 0xd / 0xe, 1)` (the HDD in EAX / EDX) |
| `0x34` / `0x33` | `.` / `,` | 2 / 3 | `mode_next(M)` / `mode_prev(M)`, ECX ← the slot |
| `0x1c` | Enter | 10 | byte `M+0x103` = 0 → nothing. Mode m = `[M+0x5d]`: m − 0xb < 2 (unsigned): target `[M+0x15c]` ≠ 0 → `map_press(M, [M+0x39], 1)` (EAX = the gadget, ECX = m − 0xd); none: `[M+0x5d]` reread = 0xb → nothing, else `map_press(M, [M+0x39], 0)` (EDX = the gadget). m − 0xd < 2 → `map_press(M, [M+0x39], 0)` (ECX = m − 0xf). Other m → nothing |
| `0x0f` | Tab | 14 | byte `M+0x103` = 0 → nothing. m = 0xb → `[M+0x15c]` ← `target_cycle1(M)`, m = 0xc → `[M+0x15c]` ← `target_cycle0(M)` (ECX = 0 at the call). Then T = `[M+0x15c]` (reread): T = 0 → `sfx_play(0x1b)`; else, with e = `[M+0x105]` read three times: `[M+42e+0xa1]` ← T (record +0x1c), `[M+42e+0xab]` ← 1 (+0x26), byte `M+42e+0xa6` ← 1 (+0x21), byte `M+0x104` ← 1, `0x44dd14(M)` (EAX = EDX = e, ECX = 21e), `sfx_play(0x1a)` |
| `0x4c` | keypad 5 | 1 | EDX ← 0, `[M+0x118]` ← 0, `[M+0x114]` ← 0 |

Every handled key returns AL ← 1 over the EAX of the last step (`0x44cf0b`);
EDX is the last callee's or the entry's. The 32-bit record arithmetic is
`(8e − e)·3·2` (`shl 3; sub; lea [x+2x]`), wrapping like the original.

**`map_press` `0x44d5c0(M, gadget, mode)`** (the keys' synthetic click).
Byte `M+0x15a` ← 1 (the cursor-warp request `map_areas` reads). gadget =
`[M+0x35]` (the mode-button panel): R = `[M+0x3d + 4(mode − 0xa)]` + 4 (the
button's rectangle), `[EBP−8]` ← `[R]`, `[EBP−4]` ← `[R+4]`, both
incremented in memory, then `0x44d3cc(M, gadget, EBP−8)` (EAX = `EBP−8`,
EDX/ECX the two corner words). Else gadget = `[M+0x39]` (the map area):
EAX = mode − 1; mode 0: B = `[[0x49b088]+4]` read before the call,
`0x486ece(EBP−8)` (copies the cursor point `0x4a3620`, which the mouse
warp `0x486e7c` writes), the point less `B+0x210/+0x214` plus
`B+0x220/+0x224` (read-modify-write in that order), `0x44d3cc(M, gadget,
EBP−8)`; mode 1: the view V = `[M+0x14]`: `V+0xc` ← `[M+0x20]`, `V+4` ←
`[M+0x18]`, `V+8` ← `[M+0x1c]`, V reread, `0x48c2e4(V)`, `0x48c444(0)`,
`0x48c57c([M+0x15c]+0x26, EBP−0x24)` (the target's position into the view),
`0x48c5fc(EBP−0x24, EBP−0x10)` (projected to 2-D), `[EBP−0xc]` negated,
`0x44d24c(M, EBP−0x10, EBP−0x18)` (onto the map area), `0x44d3cc(M, gadget,
EBP−0x18)`. Other modes, and a gadget that is neither code, return without
a call (EAX = mode − 1 or mode, EDX/ECX the entry's). Every local is
written before it is read: by the core, by `0x486ece` (two dwords), by
`0x48c57c` (three dwords through `0x48d9e4` and `transform_point`
`0x48043c`; read for its path at `0x48045c`, the other two mode-byte paths
not walked), by `0x48c5fc` (both dwords on both paths,
`0x48c61c`/`0x48c62b`, `0x48c63e`/`0x48c64f`) and by `0x44d24c` (two
dwords, `0x44d25b`/`0x44d260`). In the game the two gadget codes differ;
with equal codes the panel test wins and a map-area mode 0/1 would read a
"button" below `M+0x3d` (not reproduced in vitro).

**`wingman_pick` `0x44d82c(M)`.** A = `map_pick(M, M+0x11d)` (EAX =
`M+0x11d`). A ≠ 0 and byte `A+0x99` = 0 (EAX = `A+0x92`) → i =
`wingman_index(A)`; i ≠ −1 → `0x44a748([[0x49d6b0]+8], i)` (EDX =
`[0x49d6b0]`, ECX = the gadget). Then byte `M+0x11c` ← 0, byte `0x49d6ae` ←
1. Returns the last callee's EDX:EAX (map_pick's on a miss or a dead
actor).

**`wingman_index` `0x433134(A)`.** A = 0 → EAX = −1, EDX the entry's.
Else the first i < 3 with `[0x4d03c8 + 4i]` = A → EAX = i, EDX =
`0x4d03c8 + 4i`; none → EAX = −1, EDX = `0x4d03d4`. ECX = A. A pure reader.

**`mode_next` `0x44ee48(M)` / `mode_prev` `0x44ee88(M)`.** EAX = M, EDX =
m = `[M+0x5d]`; m = −1 → return. Next: m − 9 < 8 (signed) → m + 1, else
0xa. Previous: m − 0xb ≥ 0 (signed) → m − 1, else 0x11. Then
`map_mode(M, new)` (ECX the entry's) and the map gadget's redraw request
(`G = [[0x49d6b0]+8]`, bytes `G+0x10f` ← 1, `G+0xb0` ← 2); EAX = G, EDX =
map_mode's.

**`target_cycle1` `0x44ef08(M)` / `target_cycle0` `0x44ef1c(M)`.**
`target_cycle(M, 1)` / `(M, 0)` with EAX = M; its EDX:EAX returned.

**`target_cycle` `0x44ef30(M, side)`.** The list L = `[0x4a9af8]` and the
signed count word `[0x4a9afe]` (reread at every step); an iterator of a
counter word `[EBP−4]` (from 0) and a slot pointer `[EBP−8]` (from L): a
step takes the counter, increments it and, when the old value is below the
count (signed 16-bit), reads the next slot (else 0). The current target T =
`[M+0x15c]`: T ≠ 0 and the low word of side equals the low word of the
sign-extended byte `[[T+0x45]+0x12]` → step until the entry equals
`[M+0x15c]` (reread each step), the start S = T. Otherwise one step gives
S, and `0x44f03c(M, S, side)` with AL ≠ 0 returns EAX = S. Then: step;
an entry 0 restarts the iterator once at the list head (a second 0 → EAX =
0); `0x44f03c(M, entry, side)` AL ≠ 0 → EAX = entry; entry = S → EAX = 0;
otherwise again. The registers at each `0x44f03c` call follow the words the
steps load (AX or DX of the counter, EDX or ECX of the slot address).
**Trap:** the original has no other end: when S lies behind the count
(reachable through the 16-bit counter's wrap) or behind a 0 entry, the
loop ends only when `0x44f03c` accepts an entry; the replacement loops the
same way.

**Register flow at the callers.** ECX is dead after every call site of
the nine (listed above: `POP ECX`, or a return that `hdd_key`'s callers
follow by testing AL); EDX:EAX are reproduced. No entry register is read
except as passed on; `map_press`'s locals are the only frame state and
are written before they are read, so there is no entry or exit shim and
the adapter keeps them in a zeroed 0x30-byte scratch (frame pointer at
+0x28). All nine refuse compare mode (game calls; `wingman_index` is a
pure leaf not offered compare here). Callee counters
`hk_<hook>_call_<va>` for every direct callee.

**Guest callees.** `0x44a748` (the selection: lamp virtuals, the
DialogPort message `0x22` with a partly uninitialised message record at
`EBP−0x18`, `wingman_select`), `0x44d3cc` (event record), `0x44d24c` (map
placement), `0x44dd14` (label, H3), `0x486ece` (cursor point, platform),
the projection `0x48c2e4`/`0x48c444`/`0x48c57c`/`0x48c5fc` (H3 renderer
state) and `0x44f03c` (the visibility test through `0x44f0c4`, the
projection and `0x411c58`) are guest calls, as are the HDD button
executor `0x44a1a0` (hook `hdd_execute`) and the button owner `0x44a174`.
In the sim `0x44a748`, `0x44a1a0`, `0x44d24c` and `0x44d3cc` (their exit
ECX reproduced) are rows in `sim/src/mfd.c`, compared in vitro by
`differential_mfd_rest_test`; `0x44a748` writes the uninitialised message
bytes `+6..+0x15` as 0 ([hud.md](hud.md#the-hdd-button-executor-and-its-leaves)).
`0x44a174` has its own row.

**In vitro** (`//tools/reference-game/invitro:differential_hdd_map_keys_test`,
`invitro/hdd_map_keys_cases.h`; README "Harness notes per group"): quick tier
100,000 direct cases per hook, 256 installed 31/a7 cases per hook and
real-callee passes against the all-original run; all 36 branch sites both
ways, none proved. Mutation
witness one per hook
([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)).

As hooks, `0x44a748` would need an entry shim for its uninitialised
message bytes (as `trigger_fire`), `0x44a174` an exit-ECX shim, and
`0x44d3cc`/`0x44d24c` reach their callers with a computed ECX through a
gadget slot.

## Drop sequence 45d85c and panel constructor 431bf8

The mission-start drop sequence `0x45d85c` is own code in
`sim/src/cockpit.c`, row `drop_sequence`. The cockpit panel constructor
`0x431bf8` is own code in `sim/src/hud_cockpit.c` (`panel_ctor`, called
from `0x429834`) and has its own row `panel_ctor`, so it can be compared in
vitro. The role and callers of the drop sequence are in
[display_modes.md](display_modes.md#0x45d85c-mission-start-drop-sequence).

**Drop sequence `0x45d85c()`** (listing `0x45d85c..0x45dc4e`), in four
pieces:

- **Begin (`0x45d85c..0x45d91b`).** Loads the record
  `0x45cdf4(0x492bec(0x49f225, [0x4a0a10]))` and the bitmap B =
  `[[record+8]]`; only then reads GC = `[0x6c5f70]`. Saves GC+0x220,
  GC+0x224, GC+0x21c and the player's position words +0x26, +0x2a, +0x2e
  (P = `[0x4d24e6]`); sets the local 3D point (0, 1000, 0);
  `sfx_play(0x21)`. A NULL record or bitmap list is refused at
  `0x45d893`/`0x45d898` (the original reads through it there).
- **Descent (`0x45d92a..0x45db07`).** z runs from P.z − 4000 while z < P.z
  (signed) in steps of 35 (`0x23`), wrapping 32-bit additions. Each pass:
  1. Writes the position, +0x2e first.
  2. `camera_update`, `0x48c2e4`, `0x48c444(0)`, rereading the camera
     `[0x4d24ea]` for each call.
  3. The point's third dword ← −[C+0xc]; projected with
     `0x48c5fc`/`0x48c6d0`.
  4. GC+0x21c ← the screen y v if 0 < v < the saved value, the saved
     value if v is at least that, [GC+0x214] if v ≤ 0.
  5. `render_root` and the pump `0x464c78`.
  6. Clears GC+0x220/+0x224, restores GC+0x21c.
  7. h = the bitmap's word +4 shifted left by word `[0x4d253c]` (16-bit,
     sign-extended, reread at every use): h + v < [GC+0x214] (signed) →
     the fill `0x486704` of GC+0x210; else h + v < [GC+0x21c] → the blit
     `0x45c8bc(0x4d24bc, B, &point)`, then the fill of (point.x, h + v,
     [GC+0x218], [GC+0x21c]); else the blit alone. Before each fill
     GC+0x22c points at the local colour record {dword 0, sign-extended
     word `[0x4d3ba2]`}.
  8. The HUD's slot +8 of `[[0x49b088]+0x1f5]`.
  9. Restores GC+0x220/+0x224.
  10. While `[0x49f138]` ≠ 0 and the point's y > −200 (signed),
      `[0x49f138]` ← the fade step `0x430b34()`.
  11. The present `0x45fba4`.
- **Landing (`0x45db0d..0x45db7b`).** Restores the position,
  `sfx_stop(0x21)`; end = clock + 0x1e; the jolt `0x42d2f8(5)`,
  `sfx_play(0x29)`; do { jolt step `0x42d4a8((stream B & 0xffff) mod 5)`,
  `render_root` } while end > clock (unsigned, `cmp ebx,eax; ja` at
  `0x45db72`); the jolt end `0x42d624`. **Trap (kept):** if the clock
  passes 2^32 within those 30 ticks, the loop runs until the clock reaches
  the end value again.
- **Release (`0x45db7b..0x45dc37`).** `delete[]` of `[0x4d274c]` and
  `[0x4d2750]`, each only when set. For each set palette object
  `[0x4d2744]`, `[0x4d2748]`: words +4 and +0xc ← 0, the RTTI count
  `0x496406(o, 0)` added to the counter, slot +0x18 with (o, 3). Byte
  `[0x49aef4]` ← 1. EAX = the second palette object's slot result, or 0
  when it is not set.

The frame record `0x49630c` is not linked, as in `0x45d548`. The
original's locals `ebp−0x88..ebp−0x2c` are a block in the own call area,
because their addresses reach callees (the point, the projection, the
fill rectangle) and GC+0x22c keeps pointing at the colour record after
the return. The clock and stream-B reads go through the traced coupling
calls (sites `0x45db31`, `0x45db6d`, `0x45db50`), and so do the sounds
(sites `0x45d91b`, `0x45db2b`, `0x45db45`).

**Panel constructor `0x431bf8`** (listing `0x431bf8..0x432017`). Two
details that the own code reproduces:

- **The slot is read again.** The power-on sweep reads each element slot
  again after `0x438ddc` before it sets byte +0x8d (`0x431fa7`, `0x431fc6`,
  ...).
- **The EAX result.** EAX is the last `0x438ddc` result. If the power word
  `[rec+0x50]` is 0, EAX is instead the record pointer `rec` =
  `[[[0x49b088]+0x203]+0x1f2]`, loaded at `0x431f81`. `0x429834` ignores
  EAX.

**In vitro** (`//tools/reference-game/invitro:differential_cockpit_rest_test`,
`--cockpit-rest`, `invitro/cockpit_rest_cases.h`): 600 drop cases and
3000 panel cases identical against the original's machine code (stand-in
log hash and count, data, world and EAX). Mutation
witness: the landing test made signed fails at drop case 1. In
vitro; not checked live.

**Not reached.** In vitro, the paths where the original reads through
NULL or exits: the refusals of `0x45d85c` (NULL record or list), operator
new returning NULL in `0x431bf8`, the VOL stream error with its fatal
report. No suite reaches the drop sequence live; it runs before the proxy
arms. The modal callbacks `0x461250`/`0x461274` have no own code.

## Bar and gauge family, power-on sweeps, balance numbers

Own code: `sim/src/panel.c` (the bar and gauge section at its end), 10
service rows in `sim/dispatch/services/panel_leaves.h`, all CDECL with EAX. None
of the rows is a hook. Every function names its listing range of the
reference `DBSIM.EXE` in its comment; each was written from the listing
and checked in vitro (below). Jump and byte tables were read as bytes.

### Rows

| VA | Row | What |
| --- | --- | --- |
| `0x434910` | `system_button_action` | SystemGadget slot +8; `0x4345a0([0x49b088], B)` inline |
| `0x434dd2` | `system_base_434dd2` | event-base entry: this − 0x20, then `0x434910` |
| `0x434ddc` | `system_base_434ddc` | the same, then `0x434dcd` (empty) |
| `0x434de6` | `system_base_434de6` | the same, then `0x434748` (the draw) |
| `0x434df0` | `scroll_trigger_event` | ScrollTrigger slot +8; `0x433a88([Z+0x20], Z)` inline |
| `0x439758` | `led_segments` | the LED segments of a bar |
| `0x443770` | `shields_frame` | ShieldsGauge slot +8; the power-on sweep `0x4437cc` inline |
| `0x444a90` | `shield_numbers` | the two balance numbers |
| `0x445008` | `pool_frame` | EnergyPool slot +8 |
| `0x446c38` | `mfd_alt_order` | the MFD's Alt order keys; `0x43f9e0` inline |

**The HUD line's bar rows delegate.** The rows `bar_draw_pos`
(`0x439118`), `bar_draw_neg` (`0x439398`), `bar_draw_led` (`0x4395e8`) and
`shields_draw` (`0x443758`) in `sim/dispatch/services/hud_load.h` have their
handlers in `sim/src/hud_draw.c`, which delegate to
`es2_sim_panel_bar_draw` and `es2_sim_panel_shields_draw` in `panel.c`
(declared in `gadgets_internal.h`). Those make the original's calls:
`0x486704` with the pen colour set and restored, `0x439758` for kind 2,
`0x443918` and then `0x444a90`. **Trap:** a test that reaches the bar
draws through the dispatch needs a row or stub for `0x486704` and
`0x483a04`, or it refuses.

### Verdict per VA

| VA | Verdict |
| --- | --- |
| `0x43907c`, `0x4390c4`, `0x439344` | inlined in an own row: the bar constructors (`hud_cockpit.c`) |
| `0x4390b8`, `0x439014`, `0x43901c`, `0x439c40`, `0x439f68`, `0x4395c4` | rows elsewhere in the sim |
| `0x439118`, `0x439398`, `0x4395e8` | rows of the HUD line (`hud_load.h`), delegating to `panel.c` (above) |
| `0x439758` | row (`panel_leaves.h`) |
| `0x438a70`, `0x438b20` | inlined in an own row: the gauge constructors (`hud_cockpit.c`) |
| `0x438df8`, `0x445138`, `0x444bc4` | inlined in an own row: `scale_layout` (`hud_cockpit.c`) |
| `0x434240`, `0x434260` | inlined in an own row: `radio_record` (`hud_gauges.c`) |
| `0x434298`, `0x4342b8` | inlined in an own row: the dialog text (`hud_cockpit.c`) |
| `0x434664` | inlined in an own row: `system_buttons` (`hud_cockpit.c`) |
| `0x434e50`, `0x435490` | inlined in an own row: `dialog_port` / `event_port` (`hud_cockpit.c`) |
| `0x434280`, `0x4342e0`, `0x434310`, `0x434368`, `0x434520`, `0x434604`, `0x434634`, `0x434748` | rows elsewhere in the sim |
| `0x434928`, `0x434944`, `0x43495c`, `0x4349ac`, `0x434a24`, `0x434ab9`, `0x434dcd` | rows elsewhere in the sim |
| `0x434974` (`nav_marker_set`), `0x43904c` (`slider_base`) | hooks |
| `0x434910`, `0x434dd2`, `0x434ddc`, `0x434de6`, `0x434df0` | rows (`panel_leaves.h`) |
| `0x4345a0` | inlined in an own row: `system_button_action` (only caller `0x43491d`) |
| `0x433a88` (outside the catalogue) | inlined in an own row: `scroll_trigger_event` (only caller `0x434df0`) |
| `0x434e05` | dead: an exception-unwind destructor, referenced only from the CODE descriptor at `0x434d15` |
| `0x4437cc` | inlined in an own row: `shields_frame` (only caller `0x443770`) |
| `0x440eac`, `0x441db0`, `0x443444` | rows of the weapon-gauge frames (`energy_sweep`, `pod_sweep`, `pod_sweep_plain`, see below) |
| `0x443758` | row of the HUD line, delegating (above) |
| `0x443770`, `0x444a90`, `0x445008`, `0x446c38` | rows (`panel_leaves.h`) |
| `0x443908` | computed in core (`core/src/shield.c`, EAX = G + `0xb5`); no game call |
| `0x444b14` | inlined in an own row: `shields_gauge` (`hud_cockpit.c`) |
| `0x445218` | inlined in an own row: the page base (`hud_cockpit.c`) |
| `0x43f9e0` | inlined in an own row: `mfd_alt_order` (four bytes written); also the hook `menu_select` |
| `0x439834` | dead: `pool_gauge`'s case 1 is never reached (`0x444e69..0x444e73` stores 0 in `[esi+0x20]` and branches on that 0) |
| `0x439858`, `0x439918` (table `0x49bd18`) and their callee `0x439a0c` | dead: only `0x439834` installs `0x49bd18` (`0x43984d`) |
| `0x439ae8` with table `0x49bd00` (`0x439b8c`, `0x439c40`, callee `0x439c48`); `0x439e10` with table `0x49bce8` (`0x439eb4`, `0x439f68`, callee `0x439f70`) | dead: neither constructor has a caller and neither address occurs as a dword in the image; each alone installs its table (`0x439b08`, `0x439e30`) |
| slots +8 and +0xc of the tables `0x49bd00..0x49bd60` (`0x43a190`, `0x43a211`, `0x4451b9`, `0x455619`, `0x4489c2`, `0x45565d`, `0x45569d`) | not code: no instruction starts there in the listing; probably RTTI-like fields |
| `0x4391e0`, `0x439460`, `0x4396a8` | dead: the bar tables' slot +4; no `call [reg+4]` with a bar and three arguments exists in the listing |

**The sweeps.** The weapon-gauge frames
([hud.md](hud.md#the-panel-weapon-gauges-frame-draws)) reach all three
sweeps, so their rows live with those frames (`hud_gauges.c`):

- `0x440cb8` (`energy_frame`) calls `0x440eac` directly at `0x440cf4`.
- `0x441db0` is slot +0x1c of table `0x49c878`, whose slot +8 is
  `0x441da0`.
- `0x443444` is slot +0x1c of `0x49c8a4` and `0x49c8d0`, the tables the own
  pod constructors install (`hud_gauges.c`).

### Contracts (the comments in `panel.c` are the full ones)

- **Bar draws** (slot 0 of a bar, `(B, value)`): P = `[[[0x6c5ed0]+0x44]+0x228]`
  and the old colour `[P+4]` are read on entry; k = `[B+0x14]`.
  - Kind 0 and 2 run forward when k > 0; kind 1 when k < 0.
  - The value is a signed word; the product with k wraps at 32 bits and is
    shifted right arithmetically by 16.
  - Kind 2 draws the first part as LED segments without setting a colour.
  - The backward path restores the colour twice.
  - `B+0x20` ← the edge e; EAX the last fill's.
- **LED segments `0x439758(B, rect)`:** the context G' = `[V+0x44]` − 4
  (V = `[0x6c5ed0]`), its pen `[G'+0x228]` pointed at a record
  `{0, colour}` in the frame and restored at the end.
  - Two passes of vertical lines `0x483a04(&p1, &p2)`, stepping by 2 while
    x1 ≥ x (signed). The first pass starts at the odd x at or after x0 in
    colour `[B+0x2c]`, the second at the even one in `[B+0x30]`.
  - EAX G'. With `[V+0x44]` = 0 the original reads `[0x228]` and faults;
    the in-vitro world never has it.
- **Shields frame `0x443770(G)`:** `0x431410(G+4)` and byte `G+0x8c`; the
  sweep while byte `G+0x8d` is clear; then by byte `G+0xb0`: 1 counts
  down; 2 calls `0x443918(G)`, `0x444a90(G)` and counts down.
  - **Sweep (`0x4437cc`):** t = clock − `[G+0x90]`; `[G+0xb5]` ← min(`[G+0xcc]`, t),
    `[G+0xb9]` ← min(`[G+0xd0]`, t) (signed); both at their targets sets
    byte `G+0x8d` ← 1; byte `G+0xb0` ← 2 always.
- **Balance numbers `0x444a90(G)`:** a = (`[G+0xbd]` × 200) sar 10 (the
  product wraps at 32 bits); `_itoa(a)` and `_itoa(200 − a)` into two
  8-byte locals; labels `[G+0xc4]` and `[G+0xc8]`. **Trap:** seven
  characters fill a local; a longer number overruns it in the original too.
- **Pool frame `0x445008(G)`:** `0x431410(0x4390b8([G+0xb5]))` and byte
  `G+0xb0` = 2.
  - The GC S = `[0x6c5f70]` gets `+0x22c` ← the address of a zeroed local.
    The original leaves that pointer behind after it returns.
  - With an own context `[G+0xbf]`, S's `+0x210..+0x227` are copied into
    it and it is selected (`0x480d44`).
  - Then the bar's slot 0 with (bar, the word `G+0xb1` over the upper half
    of EAX). That EAX is `0x480d44`'s, or 0 without a context.
  - Then `0x43122c(1, G+4, 0)`; S is selected again when `[G+0xbf]` is set;
    byte `G+0xb0` ← 0.
- **MFD Alt order keys `0x446c38`:** the hook `mfd_alt_key`; contract
  under [MFD keys](#mfd-keys).
- **System action `0x434910(B)`:**
  - With the word `[0x4d24d6]` set, nothing happens; EAX is H.
  - Otherwise it takes the first i < `[H+0x25a]` (signed) with
    `[H+0x246+4i]` = B:
    - i 0: `0x4669c4(0x45f070())`;
    - i 1: `0x4667c8()`, then slot +4 of `[H+0x1e9]`;
    - later i: EAX i − 1.
  - No match: EAX is the count reached.
- **Event-base entries** `0x434dd2`/`0x434ddc`/`0x434de6`: the gadget's
  method for this − 0x20, with the registers passed through. The original
  writes the adjusted value into the caller's argument slot. The own row
  does not, because the cdecl callers pop it unread.
- **Scroll event `0x434df0(Z)`:** D = `0x429820()`; n = `[D+0x14]` is read
  once. The view set is `0x42a3f4(D, v)`:

  | Zone | n = 0 | Second case |
  | --- | --- | --- |
  | bottom `[H+0x21a]` | v 0 | n = 1: v 1 |
  | left `[H+0x21e]` | v 5 | n = 3: v 6 |
  | right `[H+0x222]` | v 4 | n = 2: v 6 |

  EAX is the last view set's, else Z.

**Registers.** Every row is CDECL and pops nothing. The rows set EAX as
above and leave EDX and ECX as they came in. The entry rows pass through
the target's EAX, EDX and ECX. Nothing compares EDX or ECX.

**Coupling.** One clock call, at site `0x4437d3` (the sweep), traced as
`ES2_SIM_HUD_CLOCK`. No stream-B draw, no sound.

**Reach.** `0x445008` and `0x443770` are reached through the panel
per-frame caller's slots `[H+0x1e5]` and `[H+0x1e9]`, which is own code
(hud.md). `0x434910` and the three entries are reached through the
SystemGadget tables `0x49b774`/`0x49b798`, which own code installs
(`0x4346bc`). `0x434df0` is reached through the ScrollTrigger table
`0x49b7b4`, which the own scroll zones install.

### Verification

`//tools/reference-game/invitro:differential_panel_bars_test`
(`panel_bars_cases.h`) runs 4000 cases identical: 1..6 calls each of the
14 entry points (the ten rows plus the four delegating HUD-line rows),
original machine code against the dispatch, comparing DATA, the heap, the
stub log (label texts, whether GC+0x22c was written), EAX (0 for
`0x434ddc`) and the coupling trace (one clock event at `0x4437d3` per
clock call); all 52 path counters non-zero (both directions of every
branch above). Mutation
witness: the LED pass colours swapped (the stub log differs in case 0).
In vitro; not checked live.

`0x446c38` is not reached by own code: its only caller is the hook
`hud_key` (`0x432bc8`, at `0x432f9a`), which has no row in the sim.

## MFD button draws and the panel copy

The MFD buttons' draw slots and the panel copy in `sim/src/hud_state.c`;
the shields gauge in `sim/src/hud_cockpit.c`; `sim/src/panel.c` and
`sim/src/gadgets.c` call through the graphics rows. The rows are the HUD
line's (`mfd_toggle_draw` 0x44750c, `mfd_push_draw` 0x447380,
`panel_copy` 0x43122c in `sim/dispatch/services/hud_load.h`). In-vitro suite
`cockpit_pixels` (`tools/reference-game/invitro/cockpit_pixels_cases.h`,
`differential_cockpit_pixels_test`). The display half (the view border 0x42da08,
the layout set 0x42a1f0, the frame measurement) is in
[display_modes.md](display_modes.md#the-view-border-and-the-layout-set-draw).

| Place | Own code |
| --- | --- |
| `0x44750c`, `0x447380` (the MFD button draws) | the bitmap through 0x45c8bc, the label through 0x438920, the canvas push/pop 0x4311e0/0x431210, the orders mark 0x43fa5c, all through the dispatch |
| `0x43122c` (the panel copy) | the region copy through the row 0x487e60, with the driver's lock/unlock and page slots |
| shields gauge `0x443524` | the shield palette 0x443918 called through its row (`display.c`) |
| `panel.c` `pn_through` | 0x483a04 / 0x45caa4 always through the dispatch |
| `gadgets.c` `polygon` | 0x483eb8 / 0x4842bb always through the dispatch |

`mfd.c` has no counted pixel work. The only `headless` count in these
files is `cursor_place` (0x4808dc) in `hud_cockpit.c`:
ClientToScreen/SetCursorPos have no window headless (a platform item, not
a pixel draw).

### `0x44750c(B)`, the toggle button (listing 0x44750c..0x4475c6)

- The canvas push 0x4311e0 returns the canvas context K in EAX. K+0x208
  is set to 0, the state's bitmap `[B+0x30 + 4 * byte B+0x40]` is drawn
  through 0x45c8bc(0x4d24bc, bitmap, B), K+0x208 is set to 1, and the pop
  0x431210 runs.
- For ids (B+0x28) below 6, and for 11 and 12 (unsigned), the label's
  font `[[B+0x2c]]` is set to `[0x49b0ac + 4 * (byte B+0x40 ? 0xc : 0xa)]`.
- The label text goes through 0x438920([B+0x2c], s). For ids below 6
  (signed), s is a three-byte local at ebp-3: the word `[0x49cea6]` is
  written first, then the digit '1' + id over its high byte, then byte
  `[0x49cea8]`. For other ids, s = `[0x4d135c + 4 id]`.
- **Registers.** EAX and EDX are the label call's. ECX is the dword that
  `push ecx` / `pop ecx` frames at ebp-4: its low byte is the entry ECX,
  and for ids below 6 the upper 24 bits are the three text bytes.

### `0x447380(B)`, the push button (listing 0x447380..0x44740f)

- The bitmap `[B+0x30 + 4 * word B+0x1b (signed)]` is drawn through
  0x45c8bc(0x4d24bc, bitmap, B). There is no canvas push.
- The label is drawn as in 0x44750c, with the bytes of 0x49cea3 and
  0x49cea5.
- Id 10 (the orders button): P = `[[B+0x24]+0xd1]`, then the page-tab
  mark 0x43fa5c(P, menu_code(P, 0), 0). 0x43f9c0 is a hook.
- **Registers.** EAX is the last call's, EDX is the label's (for id 10
  the mark's), and ECX is the popped local as in 0x44750c.

### `0x43122c(mode, rect, point)`, the panel copy (listing 0x43122c..0x43140f)

- **Gates** (listing 0x431242..0x4312ab): any of these returns EAX 0:
  - word `[0x4d2566]` set;
  - panel_visible(rect) (a hook) with AL clear;
  - the transition byte `[[0x4cf99c]+0x1c]` set;
  - mode 1 with word `[0x4d252a]` set.
  On a gate, EDX is the saved head (FS:0 at entry) and ECX is the last
  value loaded: the entry ECX (first gate), rect (after the visible
  call), `[0x4cf99c]` (transition), or the old show page (mode-1 lock).
- **Copy.** V = `[0x6c5ed0]`, its pages A = V+0x88 and B = V+0x8c (in
  mode 1 the two are swapped), and the old show page S = V+0x3c.
  1. The canvas push 0x4311e0.
  2. The draw page V+0x38 is set to A and the show page V+0x3c to B. When
     V is current, each also sets its copy (0x4a3686 / 0x4a368a) and calls
     its driver slot (`[0x4a57ec]` / `[0x4a57f0]`, the page as argument).
  3. The construction counter `[[0x6cbee4]]` goes up by 3, for a local
     region object (vtables only on the stack).
  4. The region {kind 0, rect} is built at ebp-0x14. The right and bottom
     edges grow by 1 where the shift word `[0x4d253a]` / `[0x4d253c]` has
     low five bits 1..30. The whole rectangle is then offset by
     (-dword 0x4d2556, -dword 0x4d255a).
  5. The driver's lock `[0x4a5828]`, the copy 0x487e60(region, point, or
     0x49b07e when point is 0), and the unlock `[0x4a5824]`.
  6. The show page goes back to S, with its copy and slot.
  7. The pop 0x431210. AL is set to 1 over the pop's EAX, and the counter
     goes down by 3.
- **Registers on the copy.** EAX is (pop's EAX & ~0xff) | 1. EDX is the
  counter pointer `[0x6cbee4]`. ECX is the saved head (FS:0 at entry).
  The original links an SEH frame record (0x49630c) and restores FS:0 at
  the end. The own code does not link it and reads FS:0 for the register
  values, so a fault inside the copy would unwind differently.
- **Trap: shift 31.** The listing computes the edge growth as
  `mov eax, 1; shl eax, cl; dec eax; jle` (0x43135c..0x431384). For a
  shift of 31, `1 << 31` minus 1 overflows (OF set, SF clear), so `jle`
  is taken and the edge does not grow. `edge_shift` grows the edge only
  for 1..30.

### Evidence (in vitro)

`differential_cockpit_pixels_test`, 6000 cases, all identical: DATA, the
heap, the stand-in log hash and EAX, EDX and ECX for the three cockpit
rows (the dangling GC+0x22c logged and normalised after each call); all 34
coverage counters non-zero. Mutation
witness: in `mfd_label`, the word `[text_va]` written after the digit (the
popped local carries the text bytes, so ECX differs). Frame results:
[display_modes.md](display_modes.md#the-view-border-and-the-layout-set-draw).
None of this is validated live.

## MFD keys

Six hooks of the adapter group `mfd_keys`
(`tools/reference-game/proxy/mfd_keys_hooks.h`; core `core/src/mfd_keys.c`,
`es2/core/mfd_keys.h`, whose comments hold the full contracts): `mfd_key`
`0x4469e8`, `mfd_alt_key` `0x446c38`, `mfd_key_select` `0x447158`,
`menu_select` `0x43f9e0`, `mfd_entry_next` `0x44703c`, `mfd_entry_prev`
`0x447070`. Contracts read instruction by instruction from the listing. G
is the MFDGauge (the cockpit's `[H+0x1ed]`), its menu `[G+0xd1]`; fields
as in [MFD order path](#mfd-order-path).

**`mfd_key` `0x4469e8(G, key)`** (slot `+0xc` of the MFDGauge table
`0x49cfa0`). AL = 0 when `panel_visible` `0x431410(G+0xeb)` gives AL = 0.
Key words 20h, 213h, 214h, 2Dh: when the enable byte
`0x49cbdf + 13*[G+0xc1]` + (0, 1, 2, 3) is set, `button_press`
`0x438d9c(G, 7/8/9/10, 1)`; 213h with the byte clear `mfd_range`
`0x446ff0(G)`, the other three with the byte clear nothing. In MFD mode 1:
34h `0x44703c(G)`, 33h `0x447070(G)`, each followed by byte
`G+0xe5+[G+0xc1]` = 1; the key words 12h..2Eh through the byte table
`0x446b78` and the jump table `0x446b95` (12h and 2Eh entry 4, 18h 3, 1Eh
0, 21h 5, 22h 1, 23h 2) to `0x447158(G, [G+0xfb], n)`. A handled key gives
AL = 1 over the upper bytes of the current EAX, any other AL = 0.

**`mfd_alt_key` `0x446c38(G, code)`** (`hud_key`'s Alt order keys). ESI =
the menu `[G+0xd1]`, read on every path. The word code − 212h indexes the
byte table `0x446c65` (1Dh entries) and the jump table `0x446c82` (8
entries): 21Eh entry 0, 222h 1, 223h
2, 218h 3, 221h 5: `menu_select(menu, n)`, `mfd_order` `0x447248(G)`,
`0x447158(G, [G+0xfb], n)`; 22Eh and 212h: `menu_select(menu, 4)`, then
`menu_code` `0x43f9c0(menu)`, and only if it gives 4 (22Eh, `0x446d58`) or 7
(212h, `0x446d8a`) `mfd_order(G)` and `0x447158(G, [G+0xfb], 4)`. Handled
codes give AL = 1 over the current EAX; every other EAX = 0 with the entry
EDX.

**`mfd_key_select` `0x447158(G, o, n)`.** When o = `[G+0xfb]` and
`[G+0xc1]` = 1: `mfd_entry` `0x4470a4(G, n)` (EDX = n at the call) and
byte `G+0xe6` = 1 (`0x447180`). EDX:EAX as left on the other paths.

**`menu_select` `0x43f9e0(menu, k)`** (a leaf). Word `[menu+0x37]` = word
`[menu+0x32]` (`0x43f9ea`), dword `[menu+0x32]` = k. EAX = menu, DX = the
old word over the entry EDX's upper half, ECX = k.

**`mfd_entry_next` `0x44703c(G)` / `mfd_entry_prev` `0x447070(G)`.** From
i = `[G+0xb1]` the next (i+1, exactly 6 wraps to 0, `0x447050`) or previous
(i−1, negative wraps to 5, `0x447088`) index whose flag byte
`[[G+0xd1]+0x2c+i]` has bit 0 clear (the menu pointer read once, the flag
at every step; **trap:** with all six bits set the original loops for
ever, and so does the core), then `0x4470a4(G, i)` with EDX = the menu.
EAX = i, EDX = `0x4470a4`'s.

**Registers.** The framework returns EDX:EAX only, so after the five
non-leaf hooks ECX is the framework's, not the original's. No caller reads
ECX before writing it, so it is dead after all five (listing): the
MFDGauge table `0x49cfa0` slot `+0xc` (`0x4469e8`) is dispatched only by
`hud_key` at `0x4330aa`; `0x446c38`'s only caller is `hud_key`
(`0x432f9a`); `0x447158`, `0x44703c` and `0x447070` are called only from
`0x4469e8` and `0x446c38`, which do not read ECX before writing it or
returning. `hud_key` returns ECX unchanged on these paths, and every
caller up the chain (including the indirect slot use at `0x432fcc`)
writes ECX before reading it.
**Trap:** `menu_select` is the exception: `0x446c38` calls `0x447248` right after it without touching
ECX, so the original's ECX = k reaches `mfd_order`. Its gap shim reloads
ECX from the second argument slot after the bridge (`movl 12(%ebp),%ecx`)
in every mode; the in-vitro real-chain pass shows the difference without
the reload (ECX `a5a5a5a5` instead of 1 at `mfd_order`'s entry).

**Shims.** Frame shims (`MFD_KEYS_FRAME_SHIM`, the context and stack call
of `mfd_orders_hooks.h`): `0x4469e8` and `0x446c38` 31 probed pages, EBX
and ESI in EBP−4/−8, outgoing ESP EBP−8; `0x447158` (three slots),
`0x44703c` and `0x447070` (one slot) 29 pages, EBX in EBP−4, outgoing ESP
EBP−4. `menu_select` has a gap shim (4 pages, two slots, the ECX reload).

**Compare.** `menu_select` accepts compare (span menu+0x32, 7 bytes; EAX
relative to the span, EDX exact). The other five refuse it (game calls).

**Simulation rows.** Five of the VAs have service rows
(`sim/dispatch/services/hdd_buttons.h`, `mfd_status_draws.h`, `panel_leaves.h`;
implemented in `sim/src/mfd.c` and `panel.c`, see [Bar and gauge
family](#bar-and-gauge-family-power-on-sweeps-balance-numbers)). The sim
adapter registry skips those five (`served_rvas` in `sim/src/adapters.c`),
so the rows serve them and only `menu_select` is registered as an
adapter.

**Verification (in vitro).** `//core:mfd_keys_test`;
`differential_mfd_keys_test`: 100,000 direct cases per hook, 1,024
installed cases per hook, 4,096 real-chain cases for each non-leaf hook,
512 compare cases for `menu_select`, 25 of 25 branch sites with both
outcomes; mutation witness 6/6
(`tools/reference-game/invitro/check_mutations.ps1`).

## HDD button executor

`0x44a1a0(H)` is the hook `hdd_execute` (group `hdd_exec`, core
`core/src/hdd_exec.c`, adapter `tools/reference-game/proxy/hdd_exec_hooks.h`).

**Contract `0x44a1a0(H)`** (H the HDD gauge; only caller `0x449c64` in
the HDD draw `0x449bf8`, slot 2 of the table `0x49d64c`; the full case list
is in `core/include/es2/core/hdd_exec.h`). The pending command
c = s8 `[H+0x528]`: c = −1 returns EAX = 0. With byte `[H+0x76]` set and
c > 1 it returns at once (EAX = c+1, EDX = c−1). Otherwise `[EBP−8]` = 0,
`[EBP−0x10]` = 4 and the jump table `0x44a1f7` (15 entries; 10, 11 and 12
share `0x44a4d4`) dispatches on c with the page v = `[H+0x109]`:
the page button `0x44a60c`; the map and status-page commands
`0x44d0c0`/`0x44d124`/`0x44cff8`/`0x44d05c`/`0x44cf90`/`0x44cfc4`
(HDDMap M = `[H+0x101]`) and `0x450bf4`/`0x450c18` (W = `[H+0x105]`) with
the dirty bytes `H+0x10f`/`H+0x10e` and `H+0xb0` = 2; on page 1 commands
4/5 the wingman steps `0x44ba08`/`0x44b9b0` into `[H+0x524]` and the lamp
block; 10..12 the wingman selection `0x44a748(H, i)` for wingman i = c−10
of the table `0x4d03c8` (with `0x433e3c(2)` when byte `[M+0x103]` is set);
13 the send path (`0x44dc6c(M, 0, 1)`, or `0x44db9c(M)`, the order word
`[M+0x5d]` into `0x4d03d4`, `0x44db4c`, `wingman_order 0x431610`,
`0x433e3c(0)`, `order_cancel 0x44dc10`, `0x44a748(H, −1)`); 14 the cancel
path (`0x44dc10`, `0x433e3c(0)`, `0x44a748(H, −1)`). Every dispatched path
ends with byte `[H+0x528]` = −1. EDX:EAX are reproduced exactly; ECX is
popped by the caller (`0x449c69`), so the framework's ECX is returned. All
game calls are guest calls through the callee observer with the
original's EAX/EDX/ECX (each `POP ECX` after a one-argument call rereads
the argument slot, which the callee may have changed), so `order_cancel`
and `wingman_order` keep their own hook modes.

**Trap: the escaping frame pointer.** On the lamp path (page 1, commands
4/5, when the toggle yields a nonzero sound word in `[EBP−4]`) the original
stores EBP−8 in `[[0x6c5f70]+0x22c]` (`0x44a3e6` `lea edx,[ebp-8]`,
`0x44a3e9`), clips the rect `H+0xf1` (`0x486704`) and draws the label copy
at EBP−0x20 (`0x43122c`); the context keeps the pointer after the return,
and the record {`[EBP−8]` = 0, `[EBP−4]` = the sound word} is read through
it. The hook reproduces this exactly with an **entry shim**
(`hdd_exec_entry`): PUSH EBP gives the original's EBP; the
caller's EBX/ESI/EDI go to the original's save slots EBP−0x24/−0x28/
−0x2c; 28 guard-probed pages (EAX restored) and the floor (`es2_machine_floor`); the
context {EBP, outgoing ESP = EBP−0x2c, EBX, ESI, EDI} below the gap. The
adapter then runs the core with f = the original's EBP, so the locals
EBP−0x20..EBP−1, the stored pointer and the record behind it are the
original's, and every game call runs through `mfd_orders_stack_call` at
the original's outgoing ESP with the EBX/ESI/EDI the core names (the
original's return address is written into the slot after the call
returns; during the call `[ESP]` is the bridge's). The 28 pages: the shim
is reached only through `0x449bf8` (frame, return address and argument
slot 0x24 bytes) under `panel_frame`'s 48-page gap, as its sibling
`map_frame`; no shimmed hook is reachable from `0x44a1a0`.

**Deviation (plain path only).** Without the shim (a direct call of the
adapter, legacy dispatch) the locals live in a static scratch frame
`hdd_exec_plain` (0x30 bytes, f at +0x2c, zeroed per call; address in
`hdd_exec_plain_frame`), so the stored pointer names that scratch. The
original reads no uninitialised local, so only the address differs. In the
game the shim is always used.

**Simulation.** `sim/src/mfd.c` serves `0x44a1a0` (`VA_HDD_EXECUTE`;
[hud.md](hud.md#the-hdd-button-executor-and-its-leaves)); `0x4a1a0` is in
`served_rvas` in `sim/src/adapters.c`, so the sim skips the adapter.

**Compare.** Refused (game calls, draws, sound; the stack contract).

**Verification (in vitro).** `//core:hdd_exec_test` (1,005 checks: the
exits, the page commands, the lamp with the frame record, the wingmen, the
send and cancel paths, the frame coordinate);
`differential_hdd_exec_test`: 100,000 direct cases (original, plain
adapter and core; all 15 jump-table entries hit), 1,024 installed cases
through the entry shim, 4,096 real-chain cases with the four hooks
original/replaced at random; 31 of 31 branch sites with both outcomes;
mutation witness 1/1 (`tools/reference-game/invitro/check_mutations.ps1`).
The ECX of callees below a replaced `hdd_orders` hook is not compared:
those hooks do not reproduce ECX, which is dead ([HDD map order
controls](#hdd-map-order-controls)).

**Live.** With the hook replaced (and all hooks replaced) all oracle
suites are identical, every skipped/bypassed/mismatch counter 0, and
`hk_hdd_execute_calls` is nonzero in all of them.

**Trap: the ninth pending press of `0x4530bc`.** `0x4530bc` (reached from
`button_press` `0x438d9c`; hook, `core/src/hud_keys.c`) compares the pending-press count
word `+0x396` with 7 but has no conditional jump after the compare, so a
ninth pending press writes past the eight-entry array. Play can reach it
(static reading, not checked live): in the outside view (display mode
4) `0x431500` sets the panel-off byte `H+0x20f`, so the panel tick
`0x4327ac` skips the release scan `0x4530fc` (`je` at `0x4327fe`), while
the keys `[`/`]` (`0x415ace..0x415ae7`) still press the `ShieldsGauge`
buttons through `0x438d9c` without a gate. Nine presses there write the
ninth entry over the count word (it becomes the low word of the button
pointer), the heap footer `P+0x398..0x39b` and the low half of the next
block's header `P+0x39c`; the original faults when the scan resumes
(`0x453127`). The hook writes as the original does; the scan refuses a
count above 8 ([hud.md](hud.md#headless--d-runs)), a documented crash
guard.

## Open

- Whether any suite clicks a `ShieldsSelectGadget` button with the mouse
  (the counter `call_413af8` of `shield_balance` counts applied steps, from
  the keys `[`/`]` and from clicks alike); whether `S+8` can be 0 for the
  player (division fault in `0x413bc8`).
- `0x443758`'s callers `0x4316c0`, `0x4345a0`, `0x45f13c` were identified
  by their call sites only; their triggers (display mode change, a
  `SystemGadget` action, the graphics toggle `0x4667c8`) are hypotheses.
- The live effect of the queue slips (leaked slots) needs a run that
  queues more than one message of different priority at once; a watch on
  `Q+0x212`/`Q+0x1ea`/`Q+0x21c` would show it.
- Not read beyond their use here: the shields record byte `G+0xc1`
  (passed through unchanged); the bytes `+0x20`/`+0x21` of the HDD map's
  wingman records; which side value (0 or 1) is the enemy in the map
  modes `0xb`/`0xc`; `M+0xc` (1 and 2 enable the button and map part; the
  map redraw clears it) and the clip word `0x4d3ba2`; `M+0x114`/`M+0x118`
  (cleared by keypad 5; `0x44cff8` and `0x44d05c` step `M+0x114`);
  `0x44f03c`'s test; the HDD's other pages beyond their coupling.
- The meaning of part (0, 2) behind cond and why the map enters
  `0x438bc0` so rarely; who sets the talking-head overlay word `0x49cbc6`
  to −1.
- Whether a later reader of `[X+0x22c]` follows the dangling pointer after
  `0x44c988` returns; under the shim the address is the original's, and
  what the stack holds there later is the same as long as the later frames
  are.
- Whether the panel and MFD regions equal the original's inside the
  cockpit frames (the HUD pixels and the region report;
  [display_modes.md](display_modes.md#the-view-border-and-the-layout-set-draw)).
- The full in-vitro tier has not been run for `hdd_map` (1,001,024 direct
  cases), `hdd_map_keys` (1,001,024 each) and `hdd_exec` (1,000,448).
