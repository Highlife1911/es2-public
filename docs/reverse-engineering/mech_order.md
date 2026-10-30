# MECH order handler, wingman order and AI order helpers

Contracts of `0x420ad4` (the MECH vehicle virtual `+0x28`, "order
handler"), `0x431610` (the cockpit's order to a wingman) and the four AI
helpers they call: `0x411cb4`, `0x413e50`, `0x41c0f4`, `0x41eb34`. Read
from the bytes of the reference `DBSIM.EXE`
([reference_version.md](../reference_version.md), image base `0x400000`)
with `llvm-objdump -d --x86-asm-syntax=intel`; the jump tables are decoded
from the file bytes. Names of fields, order codes and answer codes are
descriptive **hypotheses**; the register and memory effects below are what
the bytes do and what the in-vitro differential test compares.

## Own code

| Original | Hook (group) | Core function | Compare mode |
| --- | --- | --- | --- |
| `0x420ad4` | `mech_order` (`mech_order`) | `es2_mech_order_registers`, `core/src/mech_order.c` | refused |
| `0x431610` | `wingman_order` (`mech_order`) | `es2_wingman_order_registers`, `core/src/mech_order.c` | refused |
| `0x411cb4` | `target_class` (`ai_order`) | `es2_target_class_registers`, `core/src/ai_order.c` | accepted, default comparison |
| `0x413e50` | `script_start` (`ai_order`) | `es2_script_start_registers`, `core/src/ai_order.c` | refused |
| `0x41c0f4` | `attack_target` (`ai_order`) | `es2_attack_target_registers`, `core/src/ai_order.c` | refused |
| `0x41eb34` | `order_resume` (`ai_order`) | `es2_order_resume_registers`, `core/src/ai_order.c` | refused |

- Adapters: `tools/reference-game/proxy/mech_order_hooks.h`,
  `ai_order_hooks.h`; registry and compare capability in
  [hooks.md](../reference-game/hooks.md#registry) and
  [compare_mode.md](../reference-game/compare_mode.md). The hooks that call
  game code refuse compare mode because every callee goes through the guest
  bridge and the callee observer of `weapons_graph`.
- Every game callee is called through that bridge in the original's order
  with the original's EAX/EDX/ECX at the call; memory is re-read wherever the
  original re-reads it, because a bridged callee may change it.
- No hook needs an entry shim: no local is read before it is written and no
  entry register beyond the framework's EAX/EDX/ECX is read
  (`order_resume` reads the entry EDX, which the framework passes).
- In vitro: `tools/reference-game/invitro/mech_order_cases.h`,
  `ai_order_cases.h`, branch sites in `branch_sites.h`
  ([README](../../tools/reference-game/invitro/README.md)). Synthetic tests:
  `core/tests/test_mech_order.c`, `core/tests/test_ai_order.c`.
- `sim/` does not implement these functions. Its HDD button executor
  (`hdd_execute`, `sim/src/mfd.c`, original `0x44a1a0`) calls `0x431610`
  through its guest call; the cockpit-side port is `core/src/hdd_exec.c`.

## Classification

`0x420ad4` is simulation: it changes the vehicle's AI order state (state
word `A+0x23e`, order target `A+0x248`, order point `A+0x240`, guarded
object `A+0x24c`, behaviour script through `0x413e50`, attack target
through `0x41c0f4`) and the flag bytes of its status block. Its only
presentation effect is the radio answer through `0x420a98`, which builds a
6-byte message (code word, vehicle) and passes it to the cockpit's
DialogPort `[[0x49b088]+0x207]` virtual 0; that call stays original game
code. It draws no RNG: the four calls that
[rng.md](rng.md#other-generators-and-the-phase-counter) lists for this
function (`0x4212d6`, `0x421369`, `0x421491`, `0x42153c`) are calls of
`0x413e50`, the deterministic 13-step stagger counter `0x4a9b70`. It reads
no clock and draws no stream B.

`0x431610` is the cockpit side: it picks a wingman, writes the player into
the order record and calls the wingman's virtual `+0x28` (for MECHs
`0x420ad4`), then cancels HUD message `0x22` through `0x435ac8` (HUD
message queue, original). It holds no state of its own, but it decides
whether and to whom the order goes, so it is replaced together with the
order handler.

DBSIM map ([DBSIM map](../reference-game/dbsim_map.md)): both are entered 4
times in `c3_09-loadout-b` only (0 in the other five suites). The map lists
`0x420ad4` with 32 instructions because its jump table cuts the linear
walk; the function runs from `0x420ad4` to `0x421580`, 728 instructions,
with the 72-byte table at `0x420afe..0x420b45`.

## `0x420ad4` mech_order(A, R, mode)

cdecl, three slots: A = vehicle (`[ebp+8]`, kept in EBX), R = order record
(`[ebp+0xc]`), mode = word `[ebp+0x10]`. Frame `0xd8` bytes of compiler
temporaries, each written before it is read; saves EBX/ESI/EDI. Entry
EAX/EDX are not read; entry ECX is not read but reaches the callees on paths
that do not set it (the default path passes it to `0x420a98`). EAX at
return = ESI (0, or 1 when the order was taken), EDX = the last value the
path leaves (often `0x420a98`'s), ECX outside the contract (the framework's
dispatch returns the entry ECX). The instructions after the nine
`call [reg+0x28]` sites were read up to the next ECX write or call and none
uses ECX (`0x410ee4` passes it into the next virtual `+0x2c`, not followed
further); `0x431610` passes it to `0x435ac8`, which loads ECX from its
argument first.

**Record R:** word `+0` order code, dword `+2` sender (written by
`0x431610`), 8 bytes `+6` point (copied to `A+0x240`), dword `+0x12`
object.

**Vehicle A** (B = `A+0x92`, the status bytes): `+0x26` position, `+0x45`
group (`+0xc` leader slot, `+0x6c` current order index, `+0x44 + 4i` order
records), `+0x4d` behaviour script (record at `[A+0x4d]+8`, flag bytes `+1`
engage at will, `+2`, `+4`), `+0x52` dword, `+0x96` (B+4) hold, `+0x99`
(B+7), `+0x9a` (B+8) the cockpit-target gate of `0x411e80`, `+0xa4`
(B+0x12), `+0xa5` (B+0x13), `+0xb2` (B+0x20), `+0xb6` (B+0x24), `+0x1a4`
attack target, `+0x23e` order state word, `+0x240..+0x247` order point,
`+0x248` order target, `+0x24c` guarded object, `+0x250` target class word
(`0x411cb4`), `+0x281` dword cleared by several orders.

**Disabled test.** Nine places inline "X is out": the first set byte of
`X+0xa5`, `X+0xa4`, `X+0x99`. Each copy leaves different EAX/ECX/EDX values;
all are compared, and the core sets them per copy.

**Answer code EDI.** EDI starts at `0xffffffff`; most paths set only DI, so
the high word stays `0xffff`, except: code 0 loads the cockpit target into
EDI first (high word = the target's), code 2 leaves EDI = 0 after its list
walk (so the answers `0x1d`, `0x1a`, `0x0b` are exact). The full dword is
pushed to `0x420a98`, which reads its word.

**Tail `0x421559`.** If mode word = 0, or mode word = 1 and SI ≠ 0:
`0x420a98(A, EDI, 0)` (posts the answer unless `A+0x99` is set). Return
EDX:EAX = (that call's or the path's EDX) : ESI.

**Jump table** `[4*code + 0x420afe]`, code = `movzx word [R]`; `ja
0x421559` for code > `0x11`:

| Code | Target | Effect (hypothetical name) |
| --- | --- | --- |
| 0 | `0x420b46` | attack the cockpit target `T = [[0x49b088]+0x210]`: B+8 ← 0; out → `0x1b`; script flag +4 → `0x0d`; T = 0 → `0x12`; T out (`+0xa4`/`+0x99`) → `0x12`; state 4 and `A+0x248` = T → `0x0f`; else state 4, `A+0x248` ← T, `A+0x250` ← `0x411cb4(T)`, `0x41c0f4(A, T)`, `A+0x281` ← 0, `0x11`, taken |
| 1 | `0x420c5c` | break off from the cockpit target T: out → `0x1e`; T = 0 → `0x12`; state 4 with `A+0x248` = T → state 0; B+8 set → `0x0f`; else B+8 ← 1, `0x16`, taken; then only if script flag +1 and T = `A+0x1a4`: script flag +2 → `A+0x52` ← 0, else `t = target_select(A, 1, 0)`: `0x41c0f4(A, t)` or, for t = 0, `0x41eb34(A)` |
| 2 | `0x420d8d` | engage the nearest attacker of the player `P = [0x4a9b84]`: out → `0x1b`; walk the list `[0x4a9b7a]` with `0x471c70(list, cursor)` until 0; candidates not out with `+0x1a4` = P (P re-read per candidate); `dist_gate(P+0x26, o+0x26)` < nearest (start 60000, strict) wins; none → `0x1d`; winner = `A+0x1a4` and script flag +1 → `0x1a`; else as code 0 with the winner, answer `0x0b` |
| 3, `0xa`, `0xf` | `0x4214e0` | rejoin the group leader `L = [[A+0x45]+0xc]`: B+0x12 → `0x1b`; `dist_gate(A+0x26, L+0x26)` < 25000 → `0x1e`, else `0x14`; `0x413e50(A+0x4d, 0x499594)`, state 0, `A+0x281` ← 0, taken |
| 4, `0x10` | `0x420f16` | hold: B+4 set → `0x0f`, else B+4 ← 1 and `0x26`; B+0x20 ← 1; taken |
| 5 | `0x420f8e` | engage at will: B+8 ← 0; out → `0x1b`; script flag +1 → B+0x24 ← 1, `0x20`, taken; else `t = target_select(A, 1, 0)`; if t: when the group's current order record (`[G+0x44+4*[G+0x6c]]`, word type, 0x0b if none) has type 2, `X = 0x4238a0(G)` and `dist_gate(A+0x26, X+0x26)` < 100000 → state 0 and `0x41eb34(A)`; otherwise state 0 and `0x41c0f4(A, t)`; B+0x24 ← 1, `0x1c`, taken |
| 6, 9 | `0x421559` | nothing (answer `0xffffffff` in mode 0) |
| 7, `0x11` | `0x420f5b` | release the hold: B+4 ← 0 if set, B+0x20 ← 0, `0x28`, taken |
| 8 | `0x4210f0` | stop engaging at will: script flag +1 → `0x41eb34(A)` and state 0; B+0x24 ← 0, `A+0x281` ← 0, `0x0c`, taken |
| `0xb` | `0x421143` | attack R's object `[R+0x12]` (re-read at every use): B+8 ← 0; out → `0x1b`; state 4 and `A+0x248` = object → `0x0f`; object out (`+0xa4`/`+0x99`) → `0x1d`; else as code 0, answer `0x11`, taken |
| `0xc` | `0x4213af` | guard: `same` = (`A+0x24c` ≠ 0 and `R+0x12` ≠ 0: `R+0x12` = `A+0x24c`) or (both 0: `dist2d_point(A+0x240, R+6)` < 1000) else 0; out → `0x1b`; state 6 and same → `0x20`; else `0x413e50(A+0x4d, 0x499746)`, state 6, `0x17`, taken; then always `A+0x240` ← point, `A+0x24c` ← `R+0x12` |
| `0xd` | `0x421318` | move to the point (state 2): B+0x12 → `0x1b`; state 2 and `dist_gate(R+6, A+0x26)` < 2000 → `0x20`; else `0x413e50(A+0x4d, 0x499594)`, state 2, `0x2a`, taken; always `A+0x240` ← point |
| `0xe` | `0x421281` | the same with state 1 |
| > `0x11` | `0x421559` | nothing |

"out" = the disabled test of A; "taken" = ESI ← 1. Every comparison of a
distance is signed (`jge`).

**Callees** (all through the guest bridge, in the original order):

| Callee | Args | Mode in the replacement |
| --- | --- | --- |
| `0x411cb4` target class (27 instr.) | T | hook `target_class` (pure; the following `pop ecx` reloads T into ECX) |
| `0x41c0f4` set attack target (50 instr., subtree 17 functions) | A, T | hook `attack_target` |
| `0x411fa0` `target_select` | A, 1, 0 | hook (its own mode) |
| `0x41eb34` resume the group behaviour (84 instr.) | A | hook `order_resume` |
| `0x471c70` list step (backwards) | list, cursor | original |
| `0x49288c` `dist_gate` | two points | hook |
| `0x4238a0` order subject of a group | G | hook `order_actor` |
| `0x4928d0` `dist2d_point` | two points | hook |
| `0x413e50` script start (stagger counter `0x4a9b70`) | `A+0x4d`, script table | hook `script_start` |
| `0x420a98` radio answer → DialogPort virtual 0 | A, code, 0 | original (HUD message queue) |

None of `0x411cb4`, `0x41eb34`, `0x4238a0` writes its argument slot, so the
`pop ecx` after each reloads the pushed value (checked in the listing).

**Preconditions** (the original faults otherwise; the in-vitro generator
keeps them): `[0x49b088]` non-null for codes 0 and 1; `[A+0x4d]` non-null
where its flags are read (codes 0, 1, 2, 5, 8); `[R+0x12]` non-null for
code `0xb` (dereferenced at `0x4211ff` without a test); the group's leader
slot valid for 3/`0xa`/`0xf`; `0x4238a0`'s result non-null after a type-2
group order (code 5). The DialogPort must exist for `0x420a98`.

**Branch sites:** 87 conditional jumps, all feasible both ways (in vitro:
every site both ways in the first 16,384 cases); the 18 table entries and
the default edge are run per code.

## `0x431610` wingman_order(H, R, slot)

cdecl, three slots: H = cockpit `[0x49b088]`, R = order record, slot word
(`movsx`). `w = [0x4d03c8 + 4*slot]` (wingman slots, [hud.md](hud.md)). If w
≠ 0, w ≠ `[H+0x203]` (the player) and byte `w+0x99` = 0: `R+2` ←
`[H+0x203]`, then w's virtual `+0x28(w, R, 0)` (EAX = w+0x92, EDX = `[w]`,
ECX = player at the call). Always then `0x435ac8([H+0x207], w, 0x22)` with
EAX = `[H+0x207]` (re-read after the virtual), EDX/ECX as left (R and the
entry ECX on the skip paths). Returns `0x435ac8`'s EDX:EAX; ECX is dead for
the only caller `0x44a1a0` and `0x435ac8` writes ECX/EDX before reading
them. Three branch sites (`0x431628`, `0x431630`, `0x43163c`), all both
ways.

## AI order helpers

### Script record

Every caller of `0x413e50` passes `A+0x4d` of an actor as the record S, so
the "script record" is embedded in the actor: `S+0` (= `A+0x4d`) the script
table pointer, `S+5` (`A+0x52`) a dword timer, `S+9` (`A+0x56`) a dword
counter, `S+0xd..S+0x34` (`A+0x5a..0x81`) and `S+0x35..S+0x44`
(`A+0x82..0x91`) cleared state, `S+0x36` (`A+0x83`) set to 1. A script
table (`0x4993a4`, `0x4993e2`, `0x499420`, `0x499518`, `0x499556`,
`0x499594`, ... in DATA, filled at startup by static initializers such as
`0x413ed4`) holds the initial timer at `+4`, flag bytes from `+8` (the flags
`mech_order` reads at `[A+0x4d]+8`) and three Borland member pointers
(function, this-adjust, vtable slot) at `+0x18`, `+0x24` and `+0x30`;
`0x415b38`/`0x415afc`/`0x415b74` call the one at `+0x18`/`+0x24`/`+0x30` as
`fn(A + adjust)` and return its EDX:EAX (ECX popped).

### `0x411cb4` target_class(T)

cdecl, one slot T (an actor). Pure: no write, no call, no frame local.
`P = [T+0x4d]`; P = 0 → EAX = 0, EDX = entry EDX. Otherwise EDX = P+8 on
every path and: byte `P+0xd` (table flag +5) set → AX = 2; else byte `P+0xc`
(flag +4) set → AX = 1; else EAX = 0. AX = 1/2 keep T's high word (`mov ax,
imm`). ECX (P or 0) is outside the contract: all four call sites (`0x41c4cb`
in `0x41c4a8`, `0x420c30`, `0x420eef`, `0x42124f` in `0x420ad4`) pop ECX
right after the call; `0x41c4a8` compares AX with `A+0x250` and returns this
function's EDX. T = 0 faults (`0x41c4a8` calls it only in state 4 with an
order target; `0x420ad4` after its null tests). Three branch sites
(`0x411cc0`, `0x411ccd`, `0x411cdc`), all feasible both ways. The DBSIM map
saw it only in `c3_09-loadout-b` (1000+ entries, its only probed caller
`0x41c4a8`).

### `0x413e50` script_start(S, table)

cdecl, two slots. In order: `[S] ← table`, `[S+9] ← 0`, then `t =
[[S]+4]` (re-read through S after both writes), the stagger word
`[0x4a9b70] += 13` (16-bit read-modify-write), `c = [0x4a9b70] & 15`
(re-read), `[S+5] ← t + c`; `memset(S+0x35, 0, 0x10)` with EAX = S+0x35, EDX
= ECX = c at the call; byte `[S+0x36] ← 1`; `memset(S+0xd, 0, 0x28)` with
EAX = S+0x35 and EDX/ECX as the first memset returned them. Returns the
second memset's EDX:EAX (the RTL import thunk `0x49681a` →
`CW3220!_memset`, called through the guest bridge). No branch, no other
global, no RNG ([rng.md](rng.md) lists it as the deterministic stagger). No
uninitialized read: entry EAX/EDX/ECX are overwritten before use.

Callers: 30 direct calls (the table in [rng.md](rng.md)): `0x415f33`,
`0x415f46`, `0x415f59`, `0x41829b`, `0x4185ff`, `0x418674`, `0x41c15f`,
`0x41c185`, `0x41cc38`, `0x41cdf6`, `0x41d2b3`, `0x41d74a`, `0x41d920`,
`0x41de09`, `0x41debd`, `0x41e387`, `0x41e3a6`, `0x41eb67`, `0x41ec14`,
`0x41ec6c`, `0x41ed89`, `0x41f9a5`, `0x4212d6`, `0x421369`, `0x421491`,
`0x42153c`, `0x42172d`, `0x421c61`, `0x422c02`, `0x422d80`; no data
reference. The window after each call was read: none reads ECX before
writing it; `0x41c0f4`, `0x41cb94` (`0x41cc38`, `0x41cdf6`), `0x41cf18`
(`0x41d2b3`) and `0x422d00` return soon after it. The map could not probe
it (`not_probeable.json`), so its live entry count is unknown.

### `0x41c0f4` attack_target(A, T)

cdecl, two slots; EAX = A, ECX = T, EDX = T from the start. If T ≠
`[A+0x1a4]`: if `[A+0x1a4]` ≠ 0, the word `[[A+0x1a4]+0x1a2]` (the old
target's attacker count; the pointer is read twice) is decremented;
`[A+0x1a4] ← T`; if T ≠ 0, EDX = `[A+0x1a4]` (re-read) and the word
`[EDX+0x1a2]` is incremented; byte `[A+0x9d]` (B+0xb) ← 1. Then `v = movsx
word [T+0x1a8]` (T is dereferenced without a test, so T = 0 faults: the
branch at `0x41c127` is never taken, PROVED):

| v | Effect, registers at the call | Result |
| --- | --- | --- |
| 0 | byte `[A+0xac]` (B+0x1a) ← 1; `0x41d5ec(A)` with EAX = A, EDX = A+0x92, ECX = T | `0x41d5ec`'s EDX:EAX |
| 1, 3 | `0x413e50(A+0x4d, 0x499518)` with EAX = A+0x4d, EDX = 0, ECX = T | its EDX:EAX |
| 2 | `0x413e50(A+0x4d, 0x499556)`, same registers | its EDX:EAX |
| other (also negative) | nothing | EAX = A, EDX = v − 3 |

`0x41d5ec` is an 8-instruction wrapper of `0x41cf18` (268 instructions, its
own AI subtree, which calls `0x41eb34` and `0x41c0f4` again); it stays
original game code behind the bridge, which passes the original registers.
Seven branch sites, six feasible both ways, the `0x41c127` taken edge
unreachable. Callers: `0x41ece9` (`0x41eb34`), `0x41f9bb` (`0x41f7b8`, a
vtable function), `0x420c3f`, `0x420d74`, `0x420f01`, `0x4210c5`,
`0x421264` (`0x420ad4`). The map saw it only in `c3_09-loadout-b` (3
entries).

### `0x41eb34` order_resume(A)

cdecl, one slot; EBX = A, B = A+0x92. "CLEAR" is the tail that four paths
share (inlined four times): `t = [A+0x1a4]`; t = 0 → done (EDX = 0); else
the word `[t'+0x1a2]` with t' = `[A+0x1a4]` re-read is decremented,
`[A+0x1a4] ← 0`, byte `[A+0x9d]` ← 1 and EAX = B. Each copy contains a
`test edx, edx; je` after the null test (never taken) and one after `xor
edx, edx` (always taken): eight PROVED edges.

1. Byte `[A+0xa3]` (B+0x11) set: table = `0x499420` if the word
   `[[A+0x1f2]+0x50]` ≠ 0, else `0x4993e2`; `0x413e50(A+0x4d, table)` with
   EAX = EDX = table, ECX = A+0x4d; CLEAR.
2. Else, if the group leader `[[[A+0x45]+0xc]]` is the player `[0x4a9b84]`
   and the order state word `[A+0x23e]` ≠ 0: `s = movsx [A+0x23e]`
   (re-read); s > 6 unsigned (also negative) → done. Jump table `[4*s +
   0x41ebef]` (7 entries; entry 0 is unreachable because s = 0 took path 3):
   - 1, 2: `0x413e50(A+0x4d, 0x499594)` with EAX = leader, EDX = s, ECX =
     A+0x4d; CLEAR.
   - 3, 5: done.
   - 4: T = `[A+0x248]`; byte `[T+0x99]` set → `[A+0x23e] ← 0` and
     `0x41eb34(A)` (itself; with state 0 it takes path 3) with EAX = T+0x92,
     EDX = 4, ECX = `[[A+0x45]+0xc]`, then ECX = A (POP); otherwise
     `0x41c0f4(A, [A+0x248])` (re-read) with the same EAX/ECX and EDX = the
     re-read target. Done.
   - 6: `0x413e50(A+0x4d, 0x499746)` with EAX = A+0x4d, EDX = 6, ECX =
     `[[A+0x45]+0xc]`; CLEAR.
3. Otherwise the group's current order type: G = `[A+0x45]`, record
   `[G+0x44+4*[G+0x6c]]` (index re-read) → type word, or `0xb` if the slot
   is 0 (AX only: EAX keeps G's or the record's high word). Jump table
   `[4*type + 0x41ed22]`: 0 → `0x49968c`, 1 → `0x4997c2`, 2 → `0x499746`, 3
   → `0x499594`, 4 → `0x4996ca`, 5 → `0x49964e` if the signed word
   `[[A+0x1f2]+0x22]` > `0x7d00`, else `0x4995d2`, 6 → `0x499610`; type > 6
   unsigned (the empty slot's `0xb` included): **the table is the entry
   EDX** (no instruction on the way to `0x41ecf6` writes EDX).
   `0x413e50(A+0x4d, table)` with EAX = EDX = table, ECX = A+0x4d; CLEAR.

Result: AX = 1 always, EAX's high word from the path (the script start's
EAX or B after CLEAR, the leader, the recursion's or `0x41c0f4`'s EAX); EDX
= 0 after CLEAR, s after "done" in path 2, else the callee's EDX. ECX is
outside the contract (all eight direct call sites and the member pointer
caller `0x415b74` pop it). Entry EDX is the only entry register read.
Preconditions (the original faults otherwise): `[A+0x1f2]` valid on path 1
and for type 5, `[A+0x45]` and the leader slot valid, `[A+0x248]` non-null
in state 4 under the player, a readable table at entry EDX for types above
6. The call of itself in state 4 enters with EDX = 4, so there the group's
order must be of type 0..6 (an empty slot would make `0x413e50` read
`[8]`); the in-vitro real-callee pass keeps that domain.

Callers: `0x41cc24` (`0x41cb94`), `0x41d068` and `0x41d134` (`0x41cf18`),
`0x41ecd6` (itself), `0x41fab5` (`0x41f7b8`), `0x420d82`, `0x4210af`,
`0x421109` (`0x420ad4`), and indirectly as the `+0x30` member pointer of
eleven script tables (templates at `0x499910`, `0x499a30`, `0x499a54`,
`0x499a78`, `0x499a9c`, `0x499ac0`, `0x499ae4`, `0x499b08`, `0x499b2c`,
`0x499b50`, `0x499b74`) through `0x415b74`. 21 branch sites: 13 feasible
both ways, 8 with one PROVED edge.

### Callee modes in the replacement

| Hook | Callees (through the guest bridge, with the original registers) |
| --- | --- |
| target_class | none |
| script_start | `0x49681a` memset (RTL, original) twice |
| attack_target | `0x413e50` (`script_start`), `0x41d5ec` (original: wrapper of the large AI function `0x41cf18`) |
| order_resume | `0x413e50` (`script_start`), `0x41c0f4` (`attack_target`), `0x41eb34` (itself) |

## Open

- The meaning of the answer codes (`0x0b`..`0x2a`, message IDs of the
  DialogPort) and of the script tables `0x499594`/`0x499746`; what the other
  script tables and their member functions do (the `+0x30` slot is
  `0x41eb34` in all eleven templates).
- Which codes the suites send: only `c3_09-loadout-b` reaches the handler
  (4 entries, through the MFD order buttons `0x44a1a0` → `0x431610`); the
  per-code split needs the live run. The other callers of a vehicle virtual
  `+0x28` (`0x41f74c` in `0x41f7b8`, `0x4232f9`, `0x410922`, `0x410e8e`,
  `0x410ee1`, `0x42fba8`, `0x4794bd`, `0x479605`) are not classified; only
  MECH objects reach `0x420ad4`.
- Whether the empty-slot path of `0x41eb34` (type `0xb`, table = entry EDX)
  occurs live and which caller's EDX it then uses.
