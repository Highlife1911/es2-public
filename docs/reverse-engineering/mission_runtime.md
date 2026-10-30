# Mission runtime: triggers, objectives and routes

Runtime mission logic of the reference DBSIM as own code. The data side
(serialized records, readers, corpus statistics) is in
[mission.md](../formats/mission.md#runtime-mission-logic-and-operation-selectors);
this document holds the exact function contracts that the hooks implement.
Addresses are VAs at image base `0x400000`
([reference_version.md](../reference_version.md)); offsets are hexadecimal
unless written in decimal. Every contract was read from the LLVM
disassembly (`llvm-objdump -d -M intel`) of `VER95/DBSIM.EXE`. Names are mechanical; roles marked as hypotheses are
hypotheses.

| Section | Adapter group | Core |
|---|---|---|
| [Trigger dispatch and region predicates](#trigger-dispatch-and-region-predicates) | `mission` | `core/src/mission.c` |
| [Objectives, route completion and outcome policy](#objectives-route-completion-and-outcome-policy) | `mission_objectives` | `core/src/mission_objectives.c` |
| [Objective predicates](#objective-predicates) | `objective_preds` | `core/src/mission_objectives.c` |
| [Player route callback, mission timers and status check](#player-route-callback-mission-timers-and-status-check) | `route` | `core/src/mission.c` |
| [Group activation and current-order update](#group-activation-and-current-order-update) | `groups` | `core/src/groups.c` |
| [Member assessment 41b804](#member-assessment-41b804) | `cold_paths` | `core/src/groups.c` |
| [Route and behaviour helpers](#route-and-behaviour-helpers) | `ai_helpers` | `core/src/ai_helpers.c` |
| [Static-obstacle list 404ae4](#static-obstacle-list-404ae4) | `obstacle` | `core/src/obstacle.c` |
| [Collision walk 427c68 and shape cell 427360](#collision-walk-427c68-and-shape-cell-427360) | `obstacle_walk` | `core/src/obstacle.c` |
| [Results writer 42412c and its helpers](#results-writer-42412c-and-its-helpers) | `results_writer` | `core/src/results_writer.c` |
| [Team result records 423d68](#team-result-records-423d68) | `results` | `core/src/results.c` |

Adapters are `tools/reference-game/proxy/<group>_hooks.h`, core headers
`core/include/es2/core/<core>.h`. Unless stated otherwise every hook is
cdecl with caller cleanup and the EDX:EAX pair as result, and every game
call of a replacement goes through the weapons guest bridge with the
registers given in its contract, so nested hooks keep their own modes and
counters. Callee counters are `hk_<hook>_<suffix>` while the hook is
non-original.

## Trigger dispatch and region predicates

Five functions, five hooks:

| Hook | VA | Role |
|---|---|---|
| `trigger_scan` | 426b70 | the per-frame trigger pass (logic frame, call at 45f86c) |
| `trigger_regions` | 4234b8 | trigger true when one actor position lies in one of its regions |
| `region_test` | 4233a4 | one region (rectangle or distance) against a position |
| `trigger_group` | 423508 | `trigger_regions` for every actor of a group |
| `trigger_fire` | 423430 | latch the trigger, run its variable actions and messages |

### Trigger runtime records

Trigger (stride 3Ah = 58, table pointer `[4a9e28]`, count word `[4a9e24]`):

| Offset | Width | Meaning |
|---|---|---|
| +0 | word | predicate type (switch 0..10, anything else does nothing) |
| +2 | word | group activation mode (not read here) |
| +4 | word | region count (signed compare) |
| +6 | dword | pointer to an array of region pointers |
| +0A | word | latch: nonzero once fired |
| +0C | 10 words | variable IDs (negative = unused slot) |
| +20 | 10 words | action codes, paired with the IDs |
| +34 | word | message selector (negative = none) |
| +36 | dword | subject: actor (types 7..9) or group (type 10) |

Region (built by 423358 from a 6-byte record): +0 word type, +2 dword
pointer to point 1, +6 dword: pointer to point 2 for type 0, otherwise the
threshold (serialized word times ten). Points are 12-byte XYZ dword records;
only X (+0) and Y (+4) are read.

Group (stride 7Ah = 122, array pointer `[4a9e00]`, count word `[4a9e04]`):
+0 word class selector (0/1/2 = the three object pools), +0C pointer to the
actor pointer array, +10 actor count word, +12 category byte, +14 pending
activation pointer (nonzero = not yet active). Actor: position XYZ at +26,
group pointer at +45 (the player's).

Mission variables: signed words at `4a9e70 + 2*ID`.

### 4233a4 `region_test(region, position)`

cdecl, two slots. Result AX = 1 (inside) or EAX = 0.

- Type word +0 nonzero: `d = 4928d0(position, point1)` (the octagonal 2-D
  distance, hook `dist2d_point`, called with the entry EAX/EDX and
  ECX = point1 pointer). Inside when `d < threshold` (signed dword compare
  with +6). EAX keeps the distance's high word under AX = 1; EDX and ECX are
  what 4928d0 returned.
- Type 0: strict rectangle. With x1, x2, y1, y2 the point coordinates and
  (px, py) the position: inside when `min(x1,x2) < px < max(x1,x2)` and
  `min(y1,y2) < py < max(y1,y2)`, all signed, edges excluded. The four tests
  run in this order and stop at the first failure (each `min`/`max` is
  recomputed from the two pointers, so EDX holds the last compared bound).
  Inside: EAX = high word of y2 with AX = 1, ECX = point1 pointer. A failure
  at the first test leaves ECX unchanged (entry ECX).

Ten branch sites (4233b3 … 423421), all both ways in vitro.

### 4234b8 `trigger_regions(trigger, position)`

cdecl, two slots.

1. Latch +0A nonzero: return AX = 1 (EAX high word and EDX, ECX from entry).
2. For i = 0 while `(int16)i < (int16)[trigger+4]`: call
   `4233a4(region[i], position)` with EAX = position, EDX = region[i],
   ECX = what the previous call left (entry ECX first). On AX ≠ 0 call
   `423430(trigger)` with the registers 4233a4 returned, then return AX = 1
   over 423430's EAX (EDX from 423430; the caller cleanup POP leaves
   ECX = trigger).
3. No region inside: EAX = 0, EDX/ECX from the last 4233a4 (entry values
   when the count is not positive).

Three branch sites (4234c6, 4234e7, 4234fe).

### 423508 `trigger_group(group, trigger)`

cdecl, two slots. For each actor index `i < (int16)[group+10]` (signed word
compare; the index register is 32-bit, compared as BX): call
`4234b8(trigger, [[group+0C]+4*i] + 26h)` with EAX = trigger, EDX = the
actor array pointer, ECX = what the previous call left; store AX in the
local word EBP-2 and stop at the first nonzero AX. Return AX = that word;
EAX high word, EDX and ECX from the last call.

**Uninitialized result for an empty group.** The prologue's `PUSH ECX`
reserves the local; EBP-2 is the high word of the entry ECX. With a count
≤ 0 the function returns `AX = entry ECX >> 16` (EAX high word and EDX from
entry). The adapter receives the entry ECX from the framework, so no entry
shim is needed. At the six 426b70 call sites for types 2..6, ECX is the
group pointer, so an empty eligible group would fire its trigger at once (a
heap pointer has a nonzero high word); for types 1 and 10 ECX is whatever
the previous trigger left. Whether a group can be empty at run time is open:
actors are not removed from the array when destroyed (flags +99/+A4), the
player's group (type 1) contains the player, and type 10 does not occur in
the 62-mission corpus ([mission.md](../formats/mission.md#trigger-serialization-is-different-from-runtime-memory)).

Two branch sites (423536, 42353d).

### 423430 `trigger_fire(trigger)`

cdecl, one slot; no result contract beyond the register pair.

1. Latch +0A nonzero: return (EAX, EDX unchanged).
2. Set the latch to 1. For each of the ten slots i (ID word at +0C+2i,
   action at +20+2i, both re-read from the trigger every slot):
   - AX = ID; skip the slot when `(int16)ID ≤ -1`.
   - DX = action. 6: increment the word at `4a9e70 + 2*ID` (16-bit wrap;
     ECX = ID sign-extended). 5: clear it (EAX = ID sign-extended). Other
     codes: no change.
   - AX = selector word +34; when `(int16)selector ≥ 0`: write the event
     record {word selector, dword 0} at EBP-18h (EDX = 0), call 433158 (it
     returns `[[49b088]+207h]`, or 0 when `[49b088]` is 0), then call
     virtual slot 0 of the returned object with (object, &record) and
     EAX = the vtable pointer, ECX = &record, EDX = 0. The registers after
     the call carry into the next slot.

So the message is sent once per **used variable slot**, inside the loop;
with a selector but no used slot no message is sent. The selector and IDs
are re-read after every message, so a handler that changes them affects
the remaining slots. The original faults when 433158 returns 0; the core
reads the same null pointer.

**Stack residue in the event record.** The record is 22 bytes as the
handler reads it: +0 selector word and +2 actor dword are written, but +6
… +15h (EBP-12h … EBP-3, entry ESP-16h … ESP-7) are never initialized.
Both slot-0 handlers seen (`4362e4` in vtable `49baa8` and `435c48` in
`49bad4`, the two cockpit classes 436244/435bf0 constructed into
`[49b088]+207h` at 431d53/431da3) pass the record to 434e8c, which copies
+2 … +15h into the cockpit message queue (434eeb … 434f1c). So the 16
residue bytes are observable cockpit state. The hook's entry shim copies
them before the framework uses the stack and passes their address in ECX
(the entry ECX of 423430 is never read; its only use is the unchanged ECX
of the latched return, and every one of the twelve callers pops ECX
directly after the call). The replacement writes them into its own record
before every dispatch, as the original's record keeps them across slots.

Six branch sites (423441, 423455, 42345f, 423472, 423489, 4234ac).

### 426b70 `trigger_scan(player)`

cdecl, one slot (the player actor `[4d24e6]`; the caller pops ECX
afterwards). For each trigger t in table order (`edi < (int32)(int16)[4a9e24]`,
stride 3Ah), predicate result ESI = 0, then by type `(int32)(int16)[t]`
(unsigned compare with 10, so negative types are "other"):

| Type | Call (EAX, EDX, ECX at the call) | ESI |
|---:|---|---|
| 0 | `4234b8(t, player+26)` (0, player+26, carried) | sign-extended AX |
| 1 | `423508([player+45], t)` (player, group, carried) | sign-extended AX |
| 2..6 | for each group g with the condition below and `[g+14] == 0`: `423508(g, t)` (type 2: g, 0, g; types 3..6: 0, g, g), stop at the first nonzero | sign-extended AX |
| 7, 8, 9 | `4234b8(t, [t+36]+26)` (type, carried, carried) | sign-extended AX |
| 10 | `423508([t+36], t)` (10, carried, carried) | sign-extended AX |
| other | nothing | 0 |

Group conditions: type 2 byte +12 == 0, type 3 byte +12 == 1 (signed byte
minus one), types 4/5/6 word +0 == 0/1/2. The group walk uses local copies
of `[4a9e04]` (a word counted down to zero, so a negative count walks 65,536
groups) and `[4a9e00]`; failed tests leave EAX/EDX at the `SETcc`/`AND`
values the original computes (for example type 4: EAX = group, EDX = 0 when
the class word differs). Then, when ESI ≠ 0, `423430(t)` with the
registers as left; the caller cleanup leaves ECX = t. Region triggers
therefore call 423430 twice on the frame they fire (inside 4234b8, then
here; the latch makes the second call return at once) and once per frame
for every latched region trigger afterwards (4234b8 returns true for a
latched trigger). Result: EDX = the sign-extended trigger count (the loop
test is the last write), EAX as left by the last trigger, or the entry EAX
when the count is not positive. ECX carries from trigger to trigger, which
matters only for the empty-group residue of 423508 above.

28 branch sites (426b8e … 426e9c), all both ways in vitro.

### Trigger hooks

All five are cdecl with `ES2_RESULT_PAIR`; nested hooks keep their own
modes (`dist2d_point` below `region_test`, the group's own hooks below each
other). 433158 and the cockpit handler stay original code (presentation).
Four refuse compare mode (they call game code or write mission state);
`region_test` accepts it with an EAX-only comparator
([compare_mode.md](../reference-game/compare_mode.md#flag-level-leaves)).
ECX is outside the result contract; the analysis above shows it is dead
after every caller except through the empty-group residue.

| Hook | Prologue | Relocate / lengths | Slots | Entry shim |
|---|---|---|---:|---|
| `trigger_scan` | `55 8b ec 83 c4 c4` | 6 / 1,2,3 | 1 | — |
| `trigger_regions` | `55 8b ec 53 56 57` | 5 / 1,2,1,1 | 2 | — |
| `region_test` | `55 8b ec 53 56` | 5 / 1,2,1,1 | 2 | — |
| `trigger_group` | `55 8b ec 51 53` | 5 / 1,2,1,1 | 2 | — (entry ECX via the framework) |
| `trigger_fire` | `55 8b ec 83 c4 e8` | 6 / 1,2,3 | 1 | 16 bytes at entry ESP-16h |

Callee counters: `trigger_scan`: call_4234b8, call_423508, call_423430;
`trigger_regions`: call_4233a4, call_423430; `trigger_group`: call_4234b8;
`region_test`: call_4928d0; `trigger_fire`: call_433158 (one per message).

## Objectives, route completion and outcome policy

Twelve functions, twelve hooks in the adapter group `mission_objectives`.
The roles in the last column are hypotheses.

| Hook | VA | Arguments | Role (hypothesis) |
|---|---|---|---|
| `objective_eval` | 413280 | player | objective pass, status 4/5/6/9/10 |
| `actor_safe` | 4137b4 | actor | no hostile actor is engaging the actor |
| `target_known` | 411c58 | observer, target | the observer knows the target |
| `order_target` | 423918 | group, actor | the group's current order names the actor |
| `order_done` | 4239fc | group, order | completion predicate of an order |
| `subject_done` | 412e58 | group | status helper 412dc4 returns 4 |
| `rivals_done` | 412e74 | group | rival groups on the same subject are all down |
| `route_point` | 423b0c | route holder, index | point pointer of a route, or 0 |
| `outcome_poll` | 4131ac | player | periodic outcome policy (logic frame row 20) |
| `status_eval` | 413180 | player, force | status wrapper around 4135e8 |
| `bounds_check` | 41379c | actor | outside the map rectangle (margin 0) |
| `bounds_outside` | 41373c | position, margin | outside the rectangle widened by margin |

ECX is outside every result
contract (the callers either pop it or overwrite it), but the cores
reproduce it wherever it is passed into a game call.

### Objective runtime records

Objective (stride 4Ch, table `[4a9e40]`, count word `[4a9e3c]`): +0 word
marker (1 = required), +2 word predicate type, +4 word subject scope
(0 group, 1..3 actor pools), +6 subject pointer, +0E route pointer, +12h
four string pointers (the alert text, stored by address), +22h latch word,
+24h ten variable IDs, +38h ten action codes.

Group (stride 7Ah, array `[4a9e00]`, count word `[4a9e04]`, see
[Trigger runtime records](#trigger-runtime-records)): +4 route holder (word
current point index at +4, route pointer at +6), +0C actor array, +10 actor
count word, +12 category byte, +14 pending activation pointer, +44 ten order
pointers, +6C current order index dword, +70 ten completion bytes. Order
(22 bytes): +0 type word, +8 route pointer, +0C subject scope word (-1
none, 0 group, 1..3 actor), +0E subject pointer. Route: +0 point count
word, +2 pointer to the point-pointer array.

Actor: +26 XYZ position, +45 group pointer, +4B word index into an
observer's byte table at +0C2, +95 byte (target_known's distance gate), +99,
+0A4, +0A5 state bytes (the alive test below), +9E byte, +0A0 byte (read on
the player), +1A4 dword. Actor list: pointer `[4a9af8]`, count word
`[4a9afe]`; player `[4d24e6]`. Map rectangle: four dwords at `4aa640` (x0,
y0, x1, y1). Outcome globals: word status `4a9e4c`, word `4a9e54`, word
timers at `4a9e62`/`4a9e65` (flag byte, countdown word at +1, stepped by
`timer_word_step`), grace flag word `4a9e68`, and the per-status word table
`49935c` (statuses 2, 3, 6, 7, 8, 9 are 1, all others 0 in the image).

"Alive" below means the three bytes +0A5, +0A4 and +99 are all zero.

### 423b0c `route_point(holder, index)`

Leaf. With route = `[holder+2]`: null gives EAX = EDX = ECX = 0. When
`(int16)index >= (int16)[route]` (signed, only the low word of the slot is
read) EAX = EDX = 0, ECX = route. Otherwise EAX = EDX =
`[[route+2] + 4*(int16)index]`, ECX = route. A negative index is not
rejected: the original reads before the array (the core reads the same
address).

### 423918 `order_target(group, actor)`

Leaf. order = `[group + 4*[group+6C] + 44h]` (dword index, 32-bit address
arithmetic). Null order: EAX = 0, EDX = the index, ECX = entry ECX. Scope -1:
EAX = 0, ECX = 0. Scope 0: EAX = (`[actor+45]` == `[order+0E]`), EDX =
actor, ECX = `[actor+45]`. Scopes 1..3: EAX = (actor == `[order+0E]`), EDX =
actor, ECX = scope-4. Any other scope: EAX = 0, EDX = the index, ECX =
scope-4.

### 411c58 `target_known(observer, target)`

1. Byte `[target+95]` nonzero: `d = 49288c(observer+26, target+26)` (the
   3-D `dist_gate`, EAX = observer+26 at the call, EDX/ECX from entry). When
   `d < 1000000` (signed) return AX = 1 over d's high word, EDX/ECX from the
   call.
2. Otherwise (or when not closer): k = `(int16)[target+4B]`; when
   `[observer+0C2+k]` is nonzero, AX = 1 with the high word of EAX =
   0xFFFF for a negative k (MOVSX residue), else EAX = 0. EDX =
   observer+0C2; ECX from entry or from 49288c.

### 4137b4 `actor_safe(actor)`

Locals: same = (`[player+45]` == `[actor+45]`) (initial EAX), index word,
list cursor. Initial EDX = player, ECX = `[player+45]`. For index i = 0
while `(int16)i < (int16)[4a9afe]` and the list entry o = `[cursor]` is not
null (the cursor advances by 4 even for the terminating null entry):

1. Skip o when its group is pending (`[[o+45]+14]` ≠ 0; EAX = o's group,
   EDX = 1), when the category bytes `[[o+45]+12]` and `[[actor+45]+12]` are
   equal (EAX = actor's group, ECX/EDX = the two sign-extended bytes), or when
   o is not alive (EDX = 1; EAX = o+92h for +0A5, else 1).
2. k = `411c58(o, actor)` (EAX = EDX = 0, ECX = o's category).
3. Not the player's group (same = 0): when `423918([o+45], actor)` (EAX =
   actor, EDX = o's group) is true, look at the route end
   `423b0c(holder, ((EDX & FFFF0000h) | [holder]) + 1)` with holder =
   `[o+45]+4` (the slot is the 32-bit increment of the word loaded into DX
   over 423918's EDX); EAX = ECX = holder at that call. Return EAX = 0 when
   the route has a next point or when k is nonzero; otherwise skip o.
4. k nonzero: threshold = 100000 when `[o+1A4]` is the actor, else 80000;
   `d = 49288c(actor+26, o+26)` (EAX = o+26, EDX = actor+26). Return EAX = 0
   when `threshold > d` (signed).

After the loop AX = 1 over the high word EAX had at that moment (o's group,
a callee result, the distance, ... or the initial 0/1), EDX = the high word
of the last EDX with the last index in DX, ECX as left (the cursor when a
null entry ended the walk). Return-0 paths keep EDX/ECX of the last call.

### 412e58 `subject_done(group)`

`412dc4(group)` with the entry registers; EAX = (AX == 4), EDX from the
call, ECX = group (caller cleanup POP). 412dc4 is the hook
`subject_status` (the order subject's damage status through the actors'
virtual +40h; [Objective predicates](#412dc4-subject_statusg)).

### 412e74 `rivals_done(group)`

own = the group's current order (`[group + 4*[group+6C] + 44h]`, never
null-checked: the only caller, 4239fc type 2, passes it non-null). Local
copies of `[4a9e04]` (count word, counted down; zero ends the walk, so a
negative count walks 65,536 groups) and `[4a9e00]` (stride 7Ah; a null
array ends the walk). Initial EAX = entry high word over the count, EDX =
the array pointer, ECX = `[group+6C]`. For each group g:

- skip g == group; MOV DL, `[g+12]` (only the low byte of EDX changes),
  skip equal category bytes; ECX = `[g+6C]`, EDX = g's current order, skip
  null; ECX = `[EDX+0E]`, skip when it differs from `[own+0E]`;
- otherwise `412be4(g)` (EAX = g; every actor down, the hook `group_down`);
  ECX = g after the POP; AX = 0 returns EAX = 0.

End of the walk: EAX = 1 (the loop's own zero under AX = 1).

### 4239fc `order_done(group, order)`

Switch on `(int16)[order]` through SUB/DEC/JB tests (EAX = 0 first):

| Type | Result |
|---:|---|
| 0 | `412e58(group)` with EAX = 0, EDX = order, ECX = -1; its EAX/EDX, ECX = group |
| 2 | `[order+0E]` null: EAX = 0 (EDX = order, ECX = 0). Else `412e58(group)` (EAX = 0, EDX = order, ECX = 0): EAX ≠ 0 gives 1; else `412e74(group)` (EAX = 0, EDX/ECX from 412e58 and the POP): AX ≠ 0 gives 1, else 0 |
| 3, 5, 6 | `423b0c(group+4, (int32)[group+4] + 1)` (the word zero-extended, then the 32-bit increment; EAX = that value, EDX = order, ECX = type-3 or type-7); EAX = (result == 0), EDX/ECX from the call |
| other | EAX = 0, EDX = order, ECX = type-7 |

### 41373c `bounds_outside(position, margin)` and 41379c `bounds_check(actor)`

41373c: with x = `[position]`, y = `[position+4]`, m = margin and the
rectangle (x0, y0, x1, y1) at `4aa640`: outside when `x0-m >= x`, or
`x >= x1+m`, or `y0-m >= y`, or `y >= y1+m` (signed, tested in this order,
32-bit wrap). Outside: AX = 1 with the high word of the rectangle's address
(004Ah in the image), EDX = position, ECX = m, or m+y1 at the last test.
Inside: EAX = 0, ECX = m+y1. 41379c calls `41373c(actor+26, 0)` with EAX =
actor+26 and the entry EDX/ECX and returns its registers. The other caller
is 413780 (`bounds_margin`, margin 110000, status 8).

### 413180 `status_eval(player, force)`

Calls `4135e8(player, (EAX & FFFF0000h) | [4a9e54], force)` with EDX =
player and the entry ECX (4135e8 never reads its second slot, the core
still pushes the same value). When the low word of force is nonzero the
returned AX is stored in `4a9e4c`. Result: 4135e8's registers. 4135e8 is
the hook `status_check` (boundary statuses, status-change message to the
cockpit).

### 4131ac `outcome_poll(player)`

1. `467a48(4a9e65)` (grace timer), then `467a48(4a9e62)` (evaluation timer)
   with the registers carried (ECX = 4a9e65 at the second call).
2. When the second countdown is nonzero: `41379c(player)` (EAX = 0, ECX =
   4a9e62); AX = 0 returns EAX = -1 (EDX from 41379c).
3. `s = 413180(player, 0)` (EAX = 1 after an expired timer, else the
   41379c pair; ECX = 4a9e62 or player). EDX = (int16)s from here on.
4. When the table word `[49935c + 2*(int16)s]` is 0 (no bounds check on
   s) or s equals word `4a9e4c`: re-arm the evaluation countdown (word
   `4a9e63` = 10000) and return EAX = -1.
5. First reportable status: grace flag `4a9e68` = 1 and grace countdown
   (word `4a9e66`) = 10000.
6. While the grace countdown is nonzero only s = 2 passes; others re-arm as
   in 4 and return -1. A passing s is stored in `4a9e4c` and returned with
   413180's full EAX.

So a new status reaches the caller (45f992, alert dialog) at most once per
change, statuses other than 2 are held back for the grace period after the
first one, and without a bounds violation the objectives are evaluated only
when the evaluation countdown runs out.

### 413280 `objective_eval(player)`

Locals: fail = 0, ok = 1, i = 0 (the 10Ch-byte frame has no other used
slots). Initial EAX = EDX = 0, ECX = entry ECX. For each objective b while
`(int16)[4a9e3c] > i` (re-read every pass; EAX = that count at the top),
the predicate register ESI is set by scope and type:

- scope `(int16)[b+4]` 0 (ECX = -1), group g = `[b+6]`; 1..3 (ECX =
  scope-4), actor a = `[b+6]`; anything else: **ESI is not assigned**.
- type `(int16)[b+2]` above 10 (unsigned) or type 5: ESI not assigned.

| Type | Group subject (EAX = type at each call) | Actor subject |
|---:|---|---|
| 0 | `41324c(g, [b+0E])`, ESI = (int16)AX | `41324c([a+45], [b+0E])`, ESI = (int16)AX |
| 1 | `413920(g)`, ESI = the full EAX | ESI = EAX = EDX = (`[a+0A4]` or `[a+99]` nonzero) |
| 2 | `413950(g)`, ESI = (int16)AX | `4137b4(a)`, ESI = (int16)AX |
| 3, 4 | ESI = (int8)`[player+0A0]` (DL = that byte) | same |
| 6 | `412d90(g)`, ESI = (int16)AX | ESI = (int8)`[a+9E]` |
| 7 | `412c58(g)`, ESI = (int16)AX | ESI = (int8)`[a+0A5]` |
| 8 | `412d90(g)`, ESI = EAX = (AX == 0) | ESI = EDX = (`[a+9E]` == 0) |
| 9, 10 | ESI = EDX = (`[player+0A0]` == 0) | same |

A call's caller cleanup is POP ECX (ECX = the pushed subject) for one-slot
calls and ADD ESP for 41324c (ECX from the call; for actors ECX = (int16)AX).
Then: required (`[b]` == 1) and ESI = 0 and ok: ok = 0, `[4d1e98]` = EAX =
b+12h. Not required and ESI ≠ 0: fail = 1. ESI ≠ 0 and latch `[b+22]` = 0:
latch = 1 and the ten slots (ID ≥ 0 and action ≥ 0): action 4 sets the
variable word `4a9e70 + 2*ID` to 1, 5 clears it, 6 increments, 7 decrements
(16-bit wrap); afterwards EDX = b+38h, EAX = b+4Ch, ECX = 10.

After the loop `h = 4137b4(player)` (EAX = the count, EDX = player, ECX as
left; ECX = player after the POP): fail gives EAX = EDX = 6 (h ≠ 0) or 10;
otherwise ok gives EAX = ECX = 9 or 10 (EDX from 4137b4); otherwise EAX =
EDX = 5 or 4.

**Uninitialized ESI.** The unassigned cases reuse the previous objective's
ESI, and for the first objective the caller's ESI (only ESI ≠ 0 matters).
4135e8 enters with ESI = its force slot (0 on the periodic path, so the
caller's ESI is 0 there, nonzero for the forced Q-key evaluation); 42412c
enters with its own ESI. No such objective occurs in the 62-mission corpus
(scopes 0..3, types 0..4, 6, 7; [mission.md](../formats/mission.md#objective-serialization)),
but the hook reproduces it: an entry shim passes the entry ESI in ECX. The
entry ECX is then lost; it is only ever passed into 4137b4 when the table
is empty, which never reads it.

### Objective hooks

Seven refuse compare mode (they call game code or write mission state);
the readers `target_known`, `order_target`, `route_point`, `bounds_check`
and `bounds_outside` accept it (reads through runtime pointers need no
declaration,
[compare_mode.md](../reference-game/compare_mode.md#flag-level-leaves)).
Nested hooks: `dist_gate`, `timer_word_step`, the status helpers of
[Objective predicates](#objective-predicates) (41324c, 413920, 413950,
412d90, 412c58, 412dc4, 412be4), `status_check` 4135e8 and the group's own.

| Hook | Prologue | Relocate / lengths | Slots | Callee counters |
|---|---|---|---:|---|
| `objective_eval` | `55 8b ec 81 c4 f4 fe ff ff` | 9 / 1,2,6 | 1 | call_41324c, call_413920, call_413950, call_412d90, call_412c58, call_4137b4 |
| `actor_safe` | `55 8b ec 83 c4 e8` | 6 / 1,2,3 | 1 | call_411c58, call_423918, call_423b0c, call_49288c |
| `target_known` | `55 8b ec 51 53 56` | 5 / 1,2,1,1 | 2 | call_49288c |
| `order_target` | `55 8b ec 8b 45 08` | 6 / 1,2,3 | 2 | — |
| `order_done` | `55 8b ec 53 56 8b` | 5 / 1,2,1,1 | 2 | call_412e58, call_412e74, call_423b0c |
| `subject_done` | `55 8b ec 53 8b 5d 08` | 7 / 1,2,1,3 | 1 | call_412dc4 |
| `rivals_done` | `55 8b ec 83 c4 f8` | 6 / 1,2,3 | 1 | call_412be4 |
| `route_point` | `55 8b ec 53 8b 5d 0c` | 7 / 1,2,1,3 | 2 | — |
| `outcome_poll` | `55 8b ec 53 8b 5d 08` | 7 / 1,2,1,3 | 1 | call_467a48, call_41379c, call_413180 |
| `status_eval` | `55 8b ec 66 a1 54 9e 4a 00` | 9 / 1,2,6 | 2 | call_4135e8 |
| `bounds_check` | `55 8b ec 8b 45 08` | 6 / 1,2,3 | 1 | call_41373c |
| `bounds_outside` | `55 8b ec 53 56 b8` | 5 / 1,2,1,1 | 2 | — |

`status_eval`'s prologue holds `66 A1` (MOV AX, moffs32); the framework's
prologue whitelist accepts the 0x66 prefix before 89/8B and in 66 A1 (six
bytes, absolute address, valid at any location; fixture case in
`hook/tests/hook_fixture.c`). `objective_eval` has the ESI entry shim
(`objectives_eval_entry`).

## Objective predicates

Ten hooks in the adapter group `objective_preds`: the status helpers that
the objective hooks above and `status_check` call. The instruction counts
are those of the DBSIM map.

| Hook | VA | Args | Instr. | Direct call sites |
|---|---|---|---:|---|
| `subject_status` | 412dc4 | group | 59 | 412e60 (`subject_done`); 41308b, 4130dd, 413137, 41314e (412f90) |
| `subject_group` | 412bd0 | group | 8 | 412ddc (412dc4); 412d64 (412d4c); 4139b8 (4139a0); 413a1f, 413a37 (413a08) |
| `subject_actor` | 412bbc | group | 8 | 412dec, 412e01, 412e1c (412dc4); 412d74 (412d4c); 4139dc (4139a0); 413a55, 413a70 (413a08) |
| `group_down` | 412be4 | group | 44 | 412ebb (`rivals_done`) |
| `route_flag` | 41324c | group, route | 26 | 413307, 413412 (`objective_eval`, types 0) |
| `state_reached` | 413920 | group | 20 | 413318 (`objective_eval`, group type 1); 413959 (413950) |
| `group_safe` | 413950 | group | 35 | 413326 (`objective_eval`, group type 2); 413a3e (413a08) |
| `group_marked` | 412d90 | group | 15 | 413361, 41337f (`objective_eval`, group types 6 and 8); 412d6b (412d4c); 412ffb, 41315b (412f90) |
| `group_flagged` | 412c58 | group | 15 | 413370 (`objective_eval`, group type 7) |
| `bounds_margin` | 413780 | actor | 10 | 41364f (`status_check`, periodic path) |

412f90, 412d4c, 4139a0 and 413a08 stay original code; the listing has no
direct call to them (they are reached through pointer tables). Every call
site above is followed by `POP ECX` (or, for 41324c, `ADD ESP,8` and a
later write of ECX before any read: 4132a8 `MOVSX ECX` on the next pass,
or ECX reaches only 4137b4, which loads it before use), so ECX after each
function is dead and outside the contracts below.

Group, order and actor fields are those of
[Objective runtime records](#objective-runtime-records): group +0Ch actor
array, +10h signed actor count word, +12h category byte, +44h ten order
pointers, +6Ch current order index dword, +70h ten completion bytes; order
+8 route pointer, +0Ch subject scope word, +0Eh subject pointer; actor +0
vtable, +99h, +9Eh, +0A4h, +0A5h, +0B4h bytes.

### 412bd0 `subject_group(g)` and 412bbc `subject_actor(g)`

Byte-identical bodies (412bbc starts one byte after the padding `00` at
412bbb, which hides it from linear disassembly). EDX = `[g+6Ch]`, EAX =
`[[g + 4*EDX + 44h] + 0Eh]` (32-bit address arithmetic, the order never
null-checked); ECX untouched. The callers use 412bd0 where the subject
scope is 0 (a group) and 412bbc otherwise (an actor).

### 412be4 `group_down(g)`

Frame: `PUSH ECX` makes a local at EBP−4 that is written (actor+92h)
before its only read. ECX = 0 (counter, 32-bit INC, tested as CX), EDX =
`[g+0Ch]` (slot pointer, +4 per actor). While `(int16)CX < (int16)[g+10h]`
(re-read each pass through `[EBP+8]`): actor a = `[EDX]`; a is alive when
the bytes +0A5h, +0A4h and +99h are all zero (read in this order, each
read only when the previous one was zero; the three tests go through
redundant 0/1 materializations in EAX that end at 412c36). Alive: return
EAX = 0, EDX = its slot. After the walk: `MOV AX,1` over EAX = g (loaded
for the loop test), EDX = the slot past the last actor. A negative count
walks nothing.

### 412c58 `group_flagged(g)` and 412d90 `group_marked(g)`

Both: ESI = g, EDX = 0 (index, 32-bit INC, tested as DX against the
re-read `[g+10h]`), EAX = `[g+0Ch]` (slot pointer). 412c58 returns EAX = 0
with EDX = the index at the first actor whose byte +0A5h is zero, else
`MOV AX,1` over the final slot pointer (EDX = the count reached). 412d90
returns `MOV AX,1` over the current slot pointer with EDX = the index at
the first actor whose byte +9Eh is nonzero, else EAX = 0 (EDX = the count
reached). ECX = actor+92h of the last actor tested (dead).

### 41324c `route_flag(g, route)`

ESI = route, EDX = 0, EBX = g+70h, EAX = g+44h. For EDX = 0..9 (signed
32-bit compare with 10): ECX = `[EAX]`; when ECX ≠ 0 and route ==
`[ECX+8]`, return EAX = the sign-extended byte `[EBX]` (EDX = the slot
index). Otherwise EDX, EBX, EAX advance. No match: EAX = 0, EDX = 10.

### 412dc4 `subject_status(g)`

EBX = g, EAX = `[g+6Ch]`, EDX = the current order `[g + 4*EAX + 44h]`.

1. Scope word `[order+0Ch]` = 0: `412bd0(g)` (EAX = the index, EDX = the
   order, ECX = entry ECX), `POP ECX` (= g), then `412c8c(subject)`
   (`group_state`; EAX = the subject, EDX from 412bd0, ECX = g). Its
   EAX/EDX are the result.
2. Otherwise a = `412bbc(g)` (same registers as in 1). Byte `[a+99h]` ≠ 0:
   `MOV AX,4` over a, EDX from 412bbc.
3. Else a = `412bbc(g)` again (EAX = the previous a, EDX from the call,
   ECX = g); byte `[a+0B4h]` ≠ 0: `MOV AX,4` over this a.
4. Else a = `412bbc(g)` a third time and `[[a]+40h](a)` (the actor's
   virtual +40h; EAX = a, EDX = the vtable, ECX = g; `POP ECX` after it).
   With L = (int16)AX: L < 32h gives EAX = 0 (full `XOR`), L < 80h `MOV
   AX,1`, L < 0C0h `MOV AX,2`, else `MOV AX,3`, each over the virtual's
   EAX; EDX from the virtual. All three comparisons are signed.

`subject_done` tests AX = 4.

### 413920 `state_reached(g)`

ESI = 4 when byte `[g+12h]` ≠ 0, else 3. `412c8c(g)` with the entry
EAX/EDX/ECX (group_state writes both halves of its only local before
reading it and loads EAX/EDX itself, so they are dead). EAX = (ESI ≤
(int16)AX) as 0/1 (`SETLE`, `AND EAX,1`), EDX from 412c8c.

### 413950 `group_safe(g)`

ESI = g. `413920(g)` with the entry registers, `POP ECX` (= g). EAX ≠ 0
(full test): return EAX = 0, EDX from 413920. Else EBX = 0 (32-bit INC,
tested as BX against the re-read `[g+10h]`); per pass the actor a =
`[[g+0Ch] + 4*(int16)BX]` (both re-read), EDX = a+92h; byte `[a+99h]` ≠ 0
skips a (EAX = a). Otherwise `4137b4(a)` (`actor_safe`; EAX = a, EDX =
a+92h, ECX = g or the previous a after its `POP ECX`); AX = 0 returns
EAX = 0 with EDX from 4137b4. After the walk `MOV AX,1` over the EAX of
that moment: 0 (413920's, when no actor was walked), the last skipped a,
or 4137b4's EAX; EDX likewise (413920's, a+92h, or 4137b4's).

### 413780 `bounds_margin(a)`

`41373c(a+26h, 110000)` (`bounds_outside`; EAX = a+26h, EDX/ECX from
entry, which 41373c never reads); its EAX/EDX are the result (`ADD
ESP,8`). See 41373c above: outside the map rectangle widened by 110000.

### Predicate residue

None of the ten reads an uninitialized local or a caller register outside
the contracts: 412be4's local is written first, and the entry
EAX/EDX/ECX of 412dc4, 413920, 413950 and 413780 reach only callees that
load them before use (412bd0/412bbc ignore them, 412c8c, 4137b4 and
41373c load their own). The replacements leave other bytes below the
caller's ESP than the originals; no caller reads its own uninitialized
locals after these calls (the only such reader in this area, the
16-byte message record of 4135e8, lies in 4135e8's own frame above
413780's), so no entry shim is needed.

### Predicate branch sites

412dc4: 412dd9, 412dfe, 412e13, 412e2f, 412e39, 412e45; 412be4: 412c00,
412c0c, 412c1d, 412c2b, 412c39, 412c4a; 412c58: 412c72, 412c81; 412d90:
412daa, 412dbb; 41324c: 413263, 413268, 413277; 413920: 413931; 413950:
413961, 41397e, 41398a, 413995. All both ways in vitro; 412bd0, 412bbc
and 413780 have no conditional branch.

### Predicate replacement

One hook per function. 412bd0 and 412bbc are hooks of their own despite
their eight instructions: they are also called from three unhooked
original functions (412d4c, 4139a0, 413a08), and inside `subject_status`
they keep their own modes through the bridge. Cores `es2_<hook>_registers`
in `core/src/mission_objectives.c`. The six leaves read memory only.
`subject_status`, `state_reached`, `group_safe` and `bounds_margin` make
every game call through the bridge (the virtual +40h through the vtable
target), so the nested hooks (`subject_group`, `subject_actor`,
`group_state`, `state_reached`, `actor_safe`, `bounds_outside`) keep their
modes and counters: `hk_subject_status_call_412bd0`, `_call_412bbc`,
`_call_412c8c`, `_slot_40`; `hk_state_reached_call_412c8c`;
`hk_group_safe_call_413920`, `_call_4137b4`;
`hk_bounds_margin_call_41373c`. No entry shim.

**Compare mode.** The six leaves and `bounds_margin` accept compare
(pure readers; `bounds_margin`'s only callee `bounds_outside` is a pure
reader, as for `bounds_check`). `subject_status`, `state_reached` and
`group_safe` refuse it (refusal classes G and V of
[compare_mode.md](../reference-game/compare_mode.md#refusal-classes):
`group_state` calls the actors' virtual +40h, `actor_safe` calls game
code).

### Predicate evidence

- Core tests `//core:mission_objectives_test` (`preds_checks`: the
  getters, route matches with sign-extended bytes, all-down and negative
  counts, the +0A5h/+9Eh walks, both subject paths of 412dc4 with every
  level boundary, thresholds 3/4, an empty actor list for 413950, the
  110000 margin).
- In vitro (`differential_objective_preds_test`, full tier
  `differential_objective_preds_full_test`, `objective_preds_cases.h`,
  state from the `mission_objectives` builder): 1,001,024 direct cases per
  hook against logging stubs (412bd0, 412bbc, 412c8c and the virtual
  +40h under 412dc4; 412c8c under 413920; 413920 and 4137b4 under 413950;
  41373c under 413780: ordered calls with arguments and entry registers,
  return pair, arena and globals; the stubs change actor flags, counts and
  arrays between calls and return actors for 412bbc), 256 installed 31/a7
  poison probes, 2,304 caller cases per hook under its existing caller
  (`subject_done` for the three subject hooks, `rivals_done` for
  `group_down`, `objective_eval` for the five objective predicates,
  `status_check` for `bounds_margin`) with the caller and the real callees
  nested in rotating modes and the hook original, replaced or compare, and
  512 installed compare cases for each of the seven compare-capable hooks.
- Mutations: `check_mutations.ps1`.
- Live (`check-r6-set1`, all hooks and the CW3220 imports replaced): all
  suites identical, every skipped/bypassed/mismatch counter 0.
  `hk_subject_status_calls` = `hk_subject_group_calls` =
  `hk_subject_done_call_412dc4` in every suite. `subject_actor` runs in no
  suite (none meets an actor-subject order), `group_marked` and
  `group_flagged` neither (they need group objectives of types 6 to 8):
  these three are validated in vitro only. `group_down` runs only in the
  C3_10, C1_02 and C5_05 suites, `state_reached` and `group_safe` only in
  a few (c3_09, c3_10, c1_02, c5_05); `route_flag` and `bounds_margin`
  match the map's entry probes.

## Player route callback, mission timers and status check

Six functions, six hooks in the adapter group `route` (core in
`core/src/mission.c`). Roles are hypotheses where marked.

| Hook | VA | Arguments | Role |
|---|---|---|---|
| `player_route` | 41c194 | actor | the player's state callback: route progress and the mission-mode checks |
| `route_closing` | 423170 | route holder | the next point closes the route (loop) |
| `route_advance` | 42313c | route holder | advance the current point, wrap a closed route |
| `timer_scan` | 426b48 | — | the mission timer pass (logic frame row 13, call at 45f860) |
| `mission_timer` | 4230a4 | timer | one mission timer: step, fire its triggers, re-arm |
| `status_check` | 4135e8 | player, (unused), force | statuses 2/3/7/8 or the objective status, status-change message |

ECX is outside every result
contract, but the cores reproduce it wherever it is passed into a game call.

### Route and timer records

Route holder = group+4: +0 word current point index, +2 route pointer.
Route: +0 point count word, +2 pointer to the point-pointer array.
Mission timer (stride 31h, table `[4a9e38]`, count word `[4a9e34]`): +0
gate trigger pointer (0 = none), +4 ten trigger pointers, +2C timer record
(flag byte, countdown dword at +2D, stepped by `timer_dword_step` 467aa8
and armed by `timer_arm` 467ac4). Mission mode word `4a9e54` (hypothesis:
the mission type from the header; also read at 411f5c, 413183, 4241b7 and
461d34). Player route globals: dword `4a9cf8` (latch of the target-order
event), dword `4a9cec` (sequence step), timer record `4a9cf0` (countdown
dword `4a9cf1`), dword table `49a318` (image bytes `88 13 00 00 88 13 00
00 40 9c ff ff 00 00 00 00`: 5000, 5000, FFFF9C40h, 0). Status globals:
byte `499398` (set to 1 on the first call), dwords `49939c` (last status)
and `4993a0` (reported status, -1 = none), status timer record `4a9b68`
(countdown word `4a9b69`).

The event post 420a68(code) (hook `cockpit_notice`,
[vtables.md](vtables.md#event-notices-and-the-port)) builds a
24-byte record {word code, dword 0, bytes never written} in its own frame
and calls virtual slot 0 of the EventPort `[[49b088]+20Bh]` (436a3c,
[hud.md](hud.md)), which draws RNG stream B (`rng_next` on `4d260b`,
436a6c) and queues the record. The route hooks call 420a68 through the
guest bridge, so every post keeps its order. Its never-written record
bytes are stack residue below the caller in the original and zeros in the
replacement; the queue is presentation state.

### 423170 `route_closing(holder)`

Leaf. i = (int16)[holder], route = `[holder+2]` (not null-checked),
n = (int16)[route]. When `i+1 == n-1` (32-bit compare of the sign-extended
values) and `[[route+2] + 4*i + 4] == [[route+2]]` (the next point pointer
equals the first one): EAX = 1; otherwise EAX = 0. EDX = route when the
index test fails, else the next point pointer; ECX = i.

### 42313c `route_advance(holder)`

Leaf. Increments the word `[holder]` (16-bit wrap). With route =
`[holder+2]` and count = word `[route]`: when `(int16)count > 1`, the new
index equals `count-1` (sign-extended, 32-bit) and the pointer at that
index equals the first pointer, the index word becomes 0. EAX = holder;
EDX = the entry EDX high word over count (count ≤ 1), the sign-extended
count-1 (index differs) or the pointer array; ECX = route or the pointer at
the index.

### 41c194 `player_route(actor)`

The state callback installed for the player actor (mission.md, compiled
callback selection; called through 415b38 with the adjusted actor
pointer). Result EAX = 0 always; EDX as the last instruction left it.

1. G = `[actor+45]`. When the actor is not the first member (`[[G+0C]] !=
   actor`): return with EDX = `[G+0C]`.
2. mode = word `[4a9e54]`, read **once** here (the later tests use this
   copy in SI, even if a callee changes the global).
3. Route: H = `[actor+45]+4` (re-read). `p = 423b0c(H, ((G & FFFF0000h) |
   [H]) + 1)` with EAX = that index value (the 32-bit increment of the word
   loaded into AX over G), EDX = `[G+0C]`, ECX = entry ECX. When p ≠ 0:
   `d = 4928d0(actor+26, p)` (EAX = p, EDX = actor+26); when d < 10000
   (signed): `423170([actor+45]+4)` (EAX = d, ECX = holder); when its AX is
   0: post event 1Dh (`420a68(1Dh)`, ECX = holder) and
   `42313c([actor+45]+4)` (EAX = holder, ECX = 1Dh). Each holder is
   re-read from the actor.
4. mode 0: when dword `4a9cf8` is 0, T = `[actor+1A4]` is not null and
   `423918([actor+45], T)` (EAX = T, EDX = group) gives AX ≠ 0: `4a9cf8` =
   1, post event 19h, byte `[actor+9F]` = 1.
5. mode 5: `q = 41dbcc(actor)` (`actor_destination`, the order's
   destination point), then `d = 4928d0(actor+26, q)` (EAX = q, EDX =
   actor+26, ECX = actor); d < 40000 sets byte `[actor+9F]` = 1.
6. modes 3 and 7, unless dword `4a9cec` = 4: T = `4238a0([actor+45])`
   (`order_actor`, the current order's target actor; ECX = group after the
   call). The sequence continues only when byte `[T+99]` is 0, T's group
   is active (`[[T+45]+14]` = 0; ECX = 0/1 from SETNE), `49288c(actor+26,
   T+26)` < 10000 (EAX = actor+26, EDX = `[T+45]`, ECX = 0) and the
   bearing is in front: with h = `492934(T+26, actor+26)` (EAX = EDX =
   T+26, ECX = actor+26), `(uint16)(h - [actor+10] + [actor+298] + 2000h)
   < 4000h` (EAX = 0/1 from SETB). Then `467aa8(4a9cf0)` (EAX = 1); while
   the countdown runs (EAX ≠ 0) nothing else happens. When it expires:
   `[4a9cf1]` = `[49a318 + 4*[4a9cec]]` (dword index, no bound), post event
   `[4a9cec]+34h` (EAX = the table value, EDX = the event, ECX = the step),
   increment `4a9cec`, and at 4 set byte `[actor+0A0]` = 1. Any failed
   condition instead resets: when `4a9cec` ≠ 0 post event 38h and clear it
   (EDX = 0), then clear `[4a9cf1]`.
7. Other modes: nothing more.

The registers at each call carry from the previous call exactly as in the
evidence listing (`dis-41c194.asm`); the core reproduces them, including
the values the SETcc/AND pairs leave.

### 426b48 `timer_scan()` and 4230a4 `mission_timer(timer)`

426b48 (no frame, no slots): for i = 0 while `i < (int16)[4a9e34]`
(re-read every pass, signed 32-bit compare), `4230a4([4a9e38] + 31h*i)`
with EAX = the sign-extended count; the table pointer is read once. Result
EAX = the count, EDX from the last call (entry EDX for an empty table).

4230a4: gate g = `[timer]`. When g ≠ 0 and its latch word `[g+0A]` is 0,
return at once (EAX = g, EDX = entry). Otherwise `r = 467aa8(timer+2C)`
(EAX = g, EDX = timer+2C, ECX = entry ECX); when r ≠ 0 return (EAX = r,
EDX from the call). On expiry each non-null slot `[timer+4+4k]` (k = 0..9,
re-read every slot) is fired with `423430(slot)` (EAX = slot; ECX = 1 for
the first call, then the previous trigger), and `467ac4(timer+2C, 30000)`
re-arms the timer with EAX = the last slot value read or 423430's EAX.
Result: 467ac4's EAX/EDX. The triggers' messages and residue are
`trigger_fire`'s ([423430](#423430-trigger_firetrigger)); called from own
code here, the residue is the bridge stack's, as for `trigger_scan`.

### 4135e8 `status_check(player, unused, force)`

Three cdecl slots; the second is never read. ESI = force (only SI is
tested). Byte `499398` is incremented when zero (first-call flag).

1. Periodic (SI = 0): `467a48(4a9b68)` (entry registers); while the word
   countdown runs (AX ≠ 0) step it **again** (`467a48(4a9b68)` with EAX =
   0, ECX = 4a9b68) and return AX = word `4993a0` over the second call's
   EAX (EDX from it).
2. Status s (AX over EAX = player+92h, EDX = player+92h): byte
   `[player+99]` ≠ 0 gives 2; else byte `[player+0A4]` ≠ 0 gives 3; else,
   periodic only, `413780(player)` (outside the rectangle widened by
   110000; ECX = 4a9b68) gives 8 over its EAX, then `41379c(player)`
   (ECX = player) gives 7; else `s = 413280(player)` (the objective pass,
   with the caller's ESI = force, see
   [Uninitialized ESI](#413280-objective_evalplayer)).
3. Forced (SI ≠ 0): return s with the registers as left.
4. Periodic: when `4993a0` ≠ -1, `49939c` = (int16)s and `4993a0` = -1.
   The message code is 16h/1Eh/20h/17h for s = 6/7/8/9 when s differs from
   `49939c`, else none (EDX = -1). No message: `49939c` = (int16)s,
   return EAX = s, EDX = -1. Message: virtual slot 0 of `[[49b088]+20Bh]`
   with (object, &record), EAX = the vtable, EDX = &record, ECX = 0;
   record {word code, dword 0, 16 bytes never written} at EBP-18h, so
   entry ESP-16h..-7 are uninitialized record bytes (the same layout as
   423430's record). Then word `4a9b69` = 500 (re-arm), `4993a0` =
   `[49939c]`, and EAX = EDX = that value.

So a new status 6..9 (different from `49939c`) posts its message and the
call returns the **previous** status, which also goes to `4993a0`; the next
periodic evaluation then stores the new status in `49939c` and clears
`4993a0` (no second message). Other statuses only update `49939c`. The
periodic path evaluates only when the status countdown has run out (its
caller 413180 is `status_eval`); `core/tests/test_route.c` walks this
sequence.

### Route hooks

All six refuse compare mode (they call game code or write mission state).
Nested hooks: `route_point`, `order_target`, `actor_destination`,
`order_actor`, `cockpit_notice`, `dist2d_point`, `dist_gate`,
`heading_point`, `timer_dword_step`, `timer_word_step`, `timer_arm`,
`trigger_fire`, `bounds_check`, `bounds_margin`, `objective_eval` and this
group's own. The EventPort slot stays original. `status_check` calls
413280 with ESI = force through an own bridge variant (413280 reads the
caller's ESI).

| Hook | Prologue | Relocate / lengths | Slots | Entry shim |
|---|---|---|---:|---|
| `player_route` | `55 8b ec 83 c4 f4` | 6 / 1,2,3 | 1 | — |
| `route_closing` | `55 8b ec 53 56 57` | 5 / 1,2,1,1 | 1 | — |
| `route_advance` | `55 8b ec 53 8b 45 08` | 7 / 1,2,1,3 | 1 | — |
| `timer_scan` | `53 56 8b 1d 38 9e 4a 00` | 8 / 1,1,6 | 0 | — |
| `mission_timer` | `55 8b ec 53 56 57` | 5 / 1,2,1,1 | 1 | — |
| `status_check` | `55 8b ec 83 c4 e8` | 6 / 1,2,3 | 3 | 16 bytes at entry ESP-16h |

Callee counters: `player_route`: call_423b0c, call_4928d0, call_423170,
call_420a68, call_42313c, call_423918, call_41dbcc, call_4238a0,
call_49288c, call_492934, call_467aa8; `timer_scan`: call_4230a4;
`mission_timer`: call_467aa8, call_423430, call_467ac4; `status_check`:
call_467a48, call_413780, call_41379c, call_413280, event_post (the
EventPort slot-0 call at 4136fb, one per message). `route_closing` and
`route_advance` are leaves.

## Group activation and current-order update

The per-frame group pass of the logic frame (row 11 of
[main_loop.md](main_loop.md)) calls, for every group in index order,
`4236c4(group)` when the pending activation pointer `+14h` is nonzero and
`423a74(group)` otherwise; row 21 calls `423f08([player+45h])` on status 3.
Seven hooks in the adapter group `groups`: those three functions, the
placement helper `42354c` and the three order leaves `4238a0`, `4238d4`,
`41dbcc` (called below `player_route` and `target_select`).

**Callers and result registers.** Each of `4236c4`, `423a74` and `423f08`
has exactly one direct call site (`45f7ec`, `45f7f5`, `45f9bd`) and no
absolute reference. After the first two the caller pops ECX, loads AX from
its own counter and then writes EAX (`45f809`/`45f812`) and EDX (`45f7e0`
or `45f834`) before reading them: the whole result triple is dead. After
`45f9bd` the caller pops ECX and then loads ECX and EAX. Every caller of
`4238a0` (`411f67`, `41c2bc`, `41dabd`, `42107c`), `4238d4` (`41dc1a`) and
`41dbcc` (`41c26c`, `41df22`, `41e231`, `41f91f`) pops ECX at once. So ECX
is outside every contract below; EDX:EAX is reproduced exactly, and so is
every register that reaches a game call (the in-vitro stubs record them).

**Records.** Group (stride 7Ah): +6 route pointer (of the route holder
+4), +0C member array, +10 member count word, +13 activation-started
byte, +14 pending activation pointer (the activation trigger), +44 ten
order pointers, +6C current order index dword, +70 ten completion bytes.
Trigger: +2 placement mode word, +0A latch word. Order (16h bytes): +0
type word (read by `order_done`), +0C scope word, +0E subject pointer,
+12 trigger pointer. Actor: +0 vtable, +10 heading word, +26 position
(three dwords), +32 word, +45 group, +52 state countdown dword, +23E state
word, +240 a point, +248 and +24C actor pointers. Route: +2 pointer to the
point-pointer array. Globals: player `[4d24e6]`, actor list `[4a9af8]`
with count word `[4a9afe]`, terrain map `[4a0be0]`, meteor pool
`[4a96aa]`, RNG stream A state `4d2599`, runtime construction count
`[[6cbee4]]`.

### 4238a0 `order_actor(group)` and 4238d4 `order_point(group)`

Leaves. i = `[group+6C]` (EDX), order = `[group + 4*i + 44h]` (32-bit
address arithmetic, so the index wraps). The scope test is `sub ecx,1; jb`
then `sub ecx,3; jb` on the sign-extended scope word s: s = 0 is the group
scope, s = 1..3 an actor scope, everything else (negative scopes included)
the default.

`order_actor`: null order → EAX = 0. s = 0 → EAX = the first member
`[[[order+0E]+0C]]`; s = 1..3 → EAX = `[order+0E]`; default → EAX = 0.
EDX = i on every path.

`order_point`: s = 0 → EAX = EDX = first member + 26h (no further
dereference); s = 1..3 → EAX = `[order+0E]` + 26h, EDX = order; null order
or default scope → the route's first point: EDX = `[group+6]`, EAX =
`[[EDX+2]]`.

### 41dbcc `actor_destination(actor)`

Switch on the sign-extended state word `[actor+23E]` (sub 3 / sub 2 / dec):
3 → EAX = `[actor+24C]` + 26h; 5 → EAX = `[actor+248]` + 26h; 6 → when
`[actor+24C]` is nonzero EAX = that + 26h, else EAX = actor + 240h. EDX is
the zero the switch leaves, except state 6 with a target, where EDX = EAX.
Any other state calls `order_point([actor+45])` with EAX = actor, EDX = the
group, ECX = entry ECX and returns its EDX:EAX unchanged.

### 423f08 `group_assess(group)`

For i = 0 while `i < (int16)[group+10]` (re-read every pass, signed 32-bit
compare): `41b804([[group+0C] + 4i])` with EAX = the member, EDX = the
sign-extended count, ECX = entry ECX for the first call and the previous
member (POP) afterwards. Result EAX = entry EAX (empty group) or the last
call's EAX, EDX = the last count read. `41b804` is the hook
`member_assess` ([Member assessment 41b804](#member-assessment-41b804)).

### 423a74 `order_update(group)`

1. i = `[group+6C]` (EAX), order = `[group + 4i + 44h]` (kept in EDI),
   flag = 0.
2. order nonzero: `order_done(group, order)` (`4239fc`) with EAX = i and
   the entry EDX/ECX. A nonzero EAX (all 32 bits tested) sets flag = 1,
   EAX = 1 and the completion byte `[group + [group+6C] + 70h]` = 1 (the
   index re-read into EDX). Otherwise EAX = `[order+12]`; flag = 1 when
   that trigger is nonzero and its latch word `+0A` is nonzero.
3. flag set, `[group+6C] < 9` (signed) and the next order slot `[group +
   4*[group+6C] + 48h]` nonzero (EAX = the index): increment `[group+6C]`,
   then for every member (count re-read as in `423f08`, ECX = the
   sign-extended count, EDX = 0) clear the dword `+52` (the state
   countdown, mission.md).
4. Always: `411cec(member)` for every member, registers as in `423f08`
   (EAX = member, EDX = count, ECX = the value left before: entry ECX,
   `4239fc`'s ECX or the count of step 3, then the previous member).
   `411cec` is the hook `actor_step` (the per-actor state update through
   the virtuals +1C/+14/+18, mission.md).

Result: EAX/EDX from the last `411cec` call; for an empty group EAX as
left by steps 1..3 and EDX = the count.

### 42354c `spawn_point(out, distance, angle, check)`

cdecl, four slots; only the low word of `angle` takes part in arithmetic
(its high word goes on into `47eb30`'s slot, which reads only the word).
Frame: v = EBP-2Ch (three dwords {0, distance, 0}), the 2x2 rotation
m = EBP-8 (four words, all written by `47eb30`), w = EBP-18h (three zero
words), grid `[4a0be0]` copied once to EBP-1Ch, an index word and a list
cursor. Every local is written before it is read: no entry residue.

1. P = `[4d24e6]` (re-read at each use). out = P+26h in the order +8, +0,
   +4, then `[out+8]` = 0.
2. `47eb30(angle + [P+10], m)` (`sincos_pair`): the slot is the angle
   dword with its low word increased by the heading word (16-bit add);
   EAX = that value, EDX = m, ECX = P.
3. Loop: `47ffe8(v, m, out)` (`rotate_2d`; EAX = out, EDX = m, ECX = v)
   rotates v's X/Y by m into out+0 and out+4 and does **not** write out+8.
   Then out += P+26h component-wise (order +8, +0, +4; EDX/ECX/EAX keep
   P's Z/X/Y) and the dword v+4 (the distance) grows by 2000. Because
   out+8 is never reset, the Z of the n-th candidate is n times the
   player's Z (original behaviour, reproduced).
4. check ≠ 0: walk the actor list (AX = index word < count word, signed
   16-bit, over EAX's high word; the cursor advances by 4 per taken entry;
   a null entry or the end stops) until one actor rejects: skip it when its
   group is pending (`[[A+45]+14]` ≠ 0; ECX = 0/1 from SETNE); else r =
   virtual `+7C`(A) (EAX = the vtable, EDX = A's group, ECX = 0; ECX = A
   after the POP), skip it when the word r is 0; else `d = 49288c(A+26,
   out)` (`dist_gate`; EAX/EDX as the virtual returned, ECX = A) rejects
   when d < (int16)r + 5000 (signed; EDX = that bound afterwards). After the
   walk the index word is reset and EAX = the cursor = `[4a9af8]`.
5. Not rejected: `404ae4(out, 5000, 0)` (`obstacle_test`, registers as
   left); a nonzero AX rejects.
6. Not rejected: `46ff90(grid, w, out)` (`face_test`; EAX = w, EDX = grid);
   a nonzero AL rejects.
7. Rejected: next iteration from step 3. Otherwise return `46ff90`'s
   EDX:EAX. Unless the normal word equals 60Eh that EDX is the one `46ff4c`
   (`slope_test`) leaves, its entry EDX = w, a frame address; it reaches
   only `471b30`'s entry EDX in `4236c4` (returned unchanged for an empty
   pool, then dead).

There is no iteration bound: a candidate that leaves the terrain grid is
rejected by `46ff90`, and so is every later one (they lie farther out).

### 4236c4 `group_activate(group)`

Borland frame: EAX = `49a6bc` (frame descriptor, record offset -24h) and a
call to `49630c` link the record EBP-24h..-5 into FS:0 (entry EDX/ECX pass
through); the epilogue restores FS:0 from the record's saved head and
returns it in EAX. The context word EBP-14h is 14h around the constructor
call and 8 after it. T = `[group+14]` (re-read at each use).

1. Latch word `[T+0A]` = 0: return (EDX = T).
2. mode = `[T+2]` (AX over the high word `49630c` returned; ECX = T).
   Modes 2 and 3 (the meteor arrival): return when byte `[group+13]` is
   set; set it to 1; `r = rng_next(4d2599)` (stream A; EAX = mode word, EDX
   = ECX = T); angle = 4000h − (r & 7FFFh) when `[T+2]` (re-read) is 2,
   else 1000h − (r & 1FFFh) (16-bit arithmetic over r's high word; EDX = r
   with the masked low word, ECX = 4d2599 after the POP).
   `spawn_point(EBP-30h, 150000, angle, 0)` (EAX = angle, ECX = EBP-30h),
   then `o = pool_alloc([4a96aa])` (`471b30`; EDX/ECX from spawn_point; ECX
   = the pool after the POP). o = 0: return with `[group+13]` set and T
   still pending, so this group never activates. Otherwise `409b44(o,
   EBP-30h, group)` (`meteor_create`; EAX = EBP-30h, EDX = o, ECX = the
   pool) and the runtime construction count `[[6cbee4]]` decreases by 4. T
   stays pending: the meteor's update `409d2c` places the first member
   and clears `+14` later (mission.md).
3. Mode 4 (90000, angle = 1000h − (r & 1FFFh) + 8000h, behind the player)
   and mode 5 (150000, angle = 2000h − (r & 3FFFh)), one `rng_next` each
   (EAX = mode word over T's high word, EDX = ECX = T): `spawn_point
   (EBP-3Ch, distance, angle, 1)` (EAX = EBP-3Ch, EDX = r's EDX, ECX =
   4d2599). The first member gets the point (+26h, order +8, +0, +4), the
   heading word +10 = angle − 8000h and the word +32 = 0. Every further
   member k = 1.. (count re-read, EAX = it): a copy c = EBP-48h of the
   point (order +8, +0, +4), virtual `+78`(member, c) (EAX = the vtable,
   EDX = the member array, ECX = the member), then member+26h = c (order
   +8, +0, +4). Then `[group+14]` = 0 (ECX = 0).
4. Any other mode: `[group+14]` = 0 (ECX = 0).

The angle of step 3 is built in SI over ESI's high word, which is the
caller's (the logic frame's) ESI. That high word reaches only `42354c`'s
angle slot (and from there `47eb30`'s slot), whose high word nobody reads,
and EDX at the exit of mode 4/5 with fewer than two members (dead at the
caller). The core uses 0; the in-vitro harness calls the original with
ESI = 0.

**Modes in the data.** The corpus has only modes -1, 0, 1, 2, 3
(mission.md, trigger activation mode counts), so modes 4 and 5 are dead in
the retail missions. The map's probes see `42354c` and `409b44` only in
the C1_01/acceptance suites (1/4/2 calls) and `4236c4` 1000 times in every
suite (groups waiting for their latch).

**Callees.** `49630c` (runtime frame setup) and the virtuals +78 and +7C
are original code behind the guest bridge; `409b44` (`meteor_create`:
allocator `47749c`, `402188`, `490b64`, `49630c`), `411cec`
(`actor_step`), `41b804` (`member_assess`), `47eb30` (`sincos_pair`) and
`47ffe8` (`rotate_2d`) are hooks reached through it.

### Group hooks

All seven refuse compare mode (they write mission state or call game
code; the read-only leaves for uniformity). Nested hooks: `rng_next`,
`pool_alloc`, `dist_gate`, `obstacle_test`, `face_test`, `meteor_create`,
`actor_step`, `member_assess`, `sincos_pair`, `rotate_2d`, `order_done`
and its helpers, and this group's own. `group_activate` calls `49630c`
through the EBP-aware bridge (`weapons_impact_guest_call`, synthetic frame
in the adapter's scratch), and its adapter restores FS:0 from the returned
EAX, as `impact` does. No entry shim.

| Hook | Prologue | Relocate / lengths | Slots | Callee counters |
|---|---|---|---:|---|
| `group_activate` | `55 8b ec 83 c4 b8 b8 bc a6 49 00` | 6 / 1,2,3 | 1 | call_49630c, call_492ee0, call_42354c, call_471b30, call_409b44, slot_78 |
| `spawn_point` | `55 8b ec 83 c4 d4 33 c9` | 6 / 1,2,3 | 4 | call_47eb30, call_47ffe8, slot_7c, call_49288c, call_404ae4, call_46ff90 |
| `order_update` | `55 8b ec 53 56 57 8b 5d 08` | 5 / 1,2,1,1 | 1 | call_4239fc, call_411cec |
| `group_assess` | `55 8b ec 53 56 8b 75 08` | 5 / 1,2,1,1 | 1 | call_41b804 |
| `order_actor` | `55 8b ec 8b 45 08 8b 50 6c 8b 44 90 44` | 6 / 1,2,3 | 1 | — |
| `order_point` | `55 8b ec 8b 45 08 8b 50 6c 8b 54 90 44` | 6 / 1,2,3 | 1 | — |
| `actor_destination` | `55 8b ec 8b 45 08 0f bf 90 3e 02 00 00` | 6 / 1,2,3 | 1 | call_4238d4 |

**Branches.** 40 sites (10 in `group_activate`, 10 in `spawn_point`, 9 in
`order_update`, 1 in `group_assess`, 3 + 3 in the order leaves, 4 in
`actor_destination`); every one is reachable both ways with suitable
records, so no edge is proved one-way.

## Member assessment 41b804

Hook `member_assess`, group `cold_paths`, core `core/src/groups.c`.
`41b804(M)` is called by `group_assess` for every member of the player's
group (status 3). `actor_safe(M)` nonzero: return its pair. Otherwise it
sums the ratings of the live objects near `M` by side across three pools,
each reached by `pool_prev([pool], cursor)` (`0x471c70`): the MECH pool
`[4a9b7a]` within 45,000 by the chassis word `[[A+1F2h]+54h]`, the FLYER
pool `[4a9db9]` within 50,000 at 500 each, and the building pool `[4a95a0]`
within 35,000 at 500 each (only objects whose chassis word `+2Eh` is set).
A live object passes when bytes `A+0A5h`, `A+0A4h`, `A+99h` are 0 and its
group `[A+45h]` is not pending (`[[A+45h]+14h]` = 0); the side is
`(int8)[[A+45h]+12h]` against `M`'s. Distances come from `dist_gate`
(`0x49288c`). Friends (same side as `M`) minus 600, clamped at 0, against
hostiles: while friends < hostiles, `random_damage(M, 40h, 40h)`
(`0x417a04`) and hostiles −= 400. EAX/EDX = the last instruction's (a
callee's pair or the entry pair). Thirty-two conditional sites, all both
ways in vitro; ECX is the entry ECX, dead at `group_assess`.

Live (`check-r6-set3` with the four `timing-r111-p5` suites from
`check-r6-set3-rerun`, all hooks and the CW3220 imports replaced): all
suites identical, every skipped/bypassed/mismatch counter 0;
`member_assess` runs only in `c1_01-cheat-disable-leg` (the player
disabled with the wingman alive, `group_assess` status 3).

## Route and behaviour helpers

Seven functions below the AI behaviours that call
`player_route`/`actor_destination` and the torso helpers; the behaviours
themselves (`0x41c594`, `0x41c86c`, `0x41c9cc`, `0x41cf18`, `0x41d41c`,
`0x41d4e4`, `0x41e224`, ...) are in [ai.md](ai.md#41c594-attack_stepa).
Adapter group `ai_helpers`. Roles are hypotheses.

| Hook | VA | Slots | Role (hypothesis) | Direct callers |
|---|---|---|---|---|
| `point_offset` | 4929fc | P, angle, distance | moves the 2D point P by a distance along an angle (sine table) | 404355, 41c639, 41c806, 41de52, 41df91, 41f68d, 4225ce, 42277d, 46a994, 46ab99 |
| `threat_sum` | 41cb44 | X | sum of word +29Eh over every MECH whose target is X | 41cd7c, 41cdbc |
| `engage_done` | 41c4a8 | A | 1 when A should stop engaging its target T | 41c6d1, 41d489, 41d53d, 41e07f |
| `flank_step` | 41c72c | A, S | one step of a standoff/flank manoeuvre into the steering record S | 41c8be, 41d517 |
| `retreat_check` | 41cb94 | A | damage and threat score; starts the script at 499800 (retreat?) | 41d1ba, 41d1fe |
| `part_pick` | 41ce08 | A | picks the part index A+2A2h by a random weighted ratio | 41d2a5, 41e3af, 41f9ae |
| `drive_apply` | 41e828 | A, S | turns the steering record S into the `vehicle_control` call | 41c6b8, 41c8d9, 41ca34, 41d3af, 41d470, 41d524 |

All seven read no frame local before writing it (no entry shim). Every
direct call site pops ECX or adds to ESP and then reads at most EAX
(`test eax`, `test ax` or a compare of EAX), so the result contract is
EAX; the cores reproduce EDX exactly as well, and ECX wherever it reaches
a game call (ECX after return is outside every contract). Callees:
`target_class` 411cb4, `order_target` 423918, `timer_word_step` 467a48,
`heading_point` 492934, `rng_next` 492ee0 on stream A `4d2599`,
`part_ratio` 40dbc0, `vehicle_control` 416a04, `script_start` 413e50,
`order_resume` 41eb34, `pool_prev` 471c70
([main_loop.md](main_loop.md#pool-iterator-471c70--471c18)), this group's
`point_offset` and `threat_sum` (all hooks), and the actor virtuals +3Ch
and +40h (original). Nothing draws, plays a sound or calls the runtime.

**Fields used here** (A = the acting MECH, T = `[A+1A4h]` its target):
+0 vtable, +10h yaw word, +26h position (x, y, z dwords), +45h group,
+4Dh script record, +5Ah behaviour block (word +5Ah drive word, word +5Dh,
timer records +5Fh and +62h, word +60h, word +63h, byte +66h), +99h,
+0A4h, +0A5h out flags (+99h the destroyed byte), +0B4h flag, +1A2h
attacker count word, +1A4h target, +1A8h type word, +1F2h configuration
(word +2Eh), +202h (word +8), +206h part block (+0 current words, +0Ch →
+4 maximum words), +20Eh word array, +23Eh order state word, +248h order
target, +250h class word, +288h dword, +29Eh strength word, +2A2h picked
part word, +2AAh word, +30Bh pointer (word +7Fh). Group: byte +12h.
Globals: MECH list `[4a9b7a]` (a dword at an odd address), RNG stream A
state `4d2599`. Image tables (read, never written): `49a328` six part
indices 0, 1, 4, 5, 6, 7; `49a334` three weights 5, 25, 50; `49a33a` three
thresholds 60, 120, 180.

### 4929fc `point_offset(P, angle, distance)`

Leaf. The helpers 492b10/492b31 (with 492add, 492b52, 492aed and the Q14
multiply 47e0c0) are inlined into the core; their table lookup is
`heading_cos` 47dff0 on the quarter sine table `4a25c4`, computed in the
core as in `sincos_pair` (so `heading_cos`'s counter does not move). With
a = (angle + 4000h) mod 10000h (only the slot's low word is used) and d =
(int16)distance:

1. `[P]` += (int16)((d × (int16)cos(a)) >> 14) (arithmetic shift of the
   32-bit product, then its low word sign-extended);
2. `[P+4]` += (int16)((d × (int16)cos(a + C000h)) >> 14), where a + C000h
   = angle (mod 10000h). `[P]` is written before `[P+4]` is read.

Result EAX = P, EDX = the high dword of the second product (FFFFFFFFh
when it is negative, else 0); ECX = the second cosine word sign-extended.

### 41cb44 `threat_sum(X)`

sum = 0; E = `471c70([4a9b7a], 0)`; while E ≠ 0: when `[E+1A4h]` = X, sum
+= (int16)`[E+29Eh]` (32-bit); E = `471c70([4a9b7a], E)`. The list head is
reread before every call. Registers at the calls: EAX = the list head;
EDX = 0 at the first call, then the previous call's EDX; ECX = the entry
ECX at the first call, then the previous call's ECX or the word just added
(sign-extended). Result EAX = sum, EDX = the last call's EDX (ECX = 0: the
epilogue pops the cursor local).

### 41c4a8 `engage_done(A)`

1. When word `[A+23Eh]` = 4 and `[A+248h]` ≠ 0: `target_class(T)` (EAX =
   T, entry EDX/ECX); EAX = 1 when its AX > (int16)`[A+250h]` (signed),
   else 0; EDX = target_class's EDX.
2. Otherwise `order_target([A+45h], T)` (EAX = entry EAX, EDX = T, ECX =
   the group). When its AX ≠ 0 the group `[A+45h]` and T are read again:
   - group byte +12h = 0: EAX = EDX = 1 when byte `[T+0A4h]` or `[T+99h]`
     is set, else 0;
   - otherwise EAX = (int8)`[T+99h]` sign-extended, EDX = order_target's
     EDX with its low byte replaced by that byte.
3. When its AX = 0 (T read again): EAX = EDX = 1 when byte `[T+0A5h]`,
   `[T+0A4h]` or `[T+99h]` is set, else 0.

T is never checked; T = 0 faults in the original (the callers run it only
with a target).

### 41c72c `flank_step(A, S)`

S is the caller's steering record (frame local at EBP−18h in both
callers, passed on to `drive_apply`): +0 bearing word, +2 heading delta
word, +4 magnitude word, +8 distance word, +0Ah distance dword, +0Eh drive
word (1 forward, 0 stop, FFFFh reverse), +10h and +14h dword bounds.

1. `timer_word_step(A+5Fh)` (EAX = A+5Fh, entry EDX/ECX). AX ≠ 0 (still
   running): `[S+10h]` ← 0, word `[S+0Eh]` ← FFFFh, word `[S+2]` ← 0, byte
   `[A+66h]` ← 0, word `[A+63h]` ← 0FA0h; return EAX = A+65h, EDX = 0.
2. Expired: `timer_word_step(A+62h)` (EAX = the first call's EAX, EDX =
   A+62h, ECX = A+5Fh). When its AX = 0 and (int32)`[A+288h]` > 64h, word
   `[A+60h]` ← 3E8h.
3. limit = 4000h when byte `[A+66h]` ≠ 0, else 6000h. When limit ≤
   (int16)`[S+8]` (close): virtual +3Ch(A) (EAX = A+65h, EDX = the vtable,
   ECX = (int16)`[S+8]`); byte `[A+66h]` ← 1; when (AX + 7D0h) mod 10000h
   < 0FA0h (unsigned), word `[S+0Eh]` ← 0 and word `[A+5Dh]` ← 0, else
   word `[S+0Eh]` ← FFFFh. Return EAX = 0, EDX = A+65h.
4. Otherwise (far): L (EBP−0Ch) ← T's position (x, y, z; T read once),
   `[S+10h]` ← 0, word `[S+0Eh]` ← 1. c = y with its low word replaced by
   E890h, plus 2EE0h (32-bit) when (int16)`[S+2]` ≥ 0 (read after those
   writes); angle = z's high word over (word `[S]` + 8000h + c) mod
   10000h. `point_offset(&L, angle, 3A98h)` (EAX = &L, EDX = 0, ECX = c);
   h = `heading_point(&L, A+26h)` (EAX = point_offset's EAX, EDX = A+26h,
   ECX = &L); word `[S+2]` ← h − word `[A+10h]`; byte `[A+66h]` ← 0.
   Return EAX = A+65h, EDX = heading_point's EDX.

### 41cb94 `retreat_check(A)`

1. **A out** (byte `[A+0A5h]`, `[A+0A4h]` or `[A+99h]` set): when T's
   type word `[T+1A8h]` is 1 or 3 and word `[[T+1F2h]+2Eh]` = 0 (T and the
   configuration read again), `order_resume(A)` (EAX = T, EDX = T's
   configuration, ECX = A); else `script_start(A+4Dh, 499800)` (EAX =
   A+4Dh, EDX = 0 for another type or the configuration, ECX = A). Return
   AX = 1 over the call's EAX, EDX = the call's EDX.
2. Otherwise score = AX of virtual +40h(A) (EAX = 0, EDX = A, ECX = the
   vtable). For the six parts k of `49a328`: c = (int16)`[[A+206h] + 2k]`;
   ratio_k = 100h when c < 0, else (c << 8) / (int16)`[[[A+212h]+4] + 2k]`
   (IDIV, truncated; the pointers are reread for every part; a zero
   maximum faults). For threshold j = 0, 1, 2 (`49a33a`): every part with
   threshold_j < ratio_k (signed words) adds weight_j (`49a334`) to the
   score word; the loop over j stops after the first threshold that no
   part exceeds.
3. Decision on the signed score word, flag = 0:
   - > 46h: word `[A+2AAh]` ← 3E8h; flag = (word `[A+1A2h]` ≠ 0).
   - > 32h: `[A+2AAh]` ← 258h; n = (int16)`[A+1A2h]`; n > 2 → flag = 1;
     else n ≠ 0 → flag = (`threat_sum(A)` > (int16)`[A+29Eh]`) (EAX = A
     with n in AX, EDX = A, ECX = the frame address EBP−3Ch).
   - > 1Eh: `[A+2AAh]` ← 12Ch; when (int16)`[A+1A2h]` > 2: q =
     `threat_sum(A)` / (int16)`[A+1A2h]` (EAX = A, EDX from step 2, ECX =
     A; the count is reread after the call), flag = (q >
     (int16)`[A+29Eh]`).
4. flag ≠ 0: `script_start(A+4Dh, 499800)` with the registers of the
   path. Return AX = flag over the last EAX, EDX = the last EDX. Without a
   call after step 2, EAX's high word is A's (score > 1Eh) or 0 (EAX = 6
   after the part loop), and EDX = 0 or 00490001h (the weight pointer's
   high word over 1 when the last threshold pass counted a part).

### 41ce08 `part_pick(A)`

T = `[A+1A4h]`, W = `[T+20Eh]` (both read once, before the first call),
best = FFFFh, top = 0. r = `rng_next(4d2599)` & 7Fh (entry registers; AX
only, EAX's high word stays). near = `[A+30Bh]` ≠ 0 and
(int16)`[[A+30Bh]+7Fh]` < 0AAh. Range [first, end):

- byte `[T+0B4h]` set: [0, 1);
- r < 28h or near: [7, 13h) (first over r's high word);
- r ≥ 50h: [0, 7);
- otherwise [13h, 13h + word `[[A+202h]+8]`) (16-bit end, first over r's
  high word).

For i = first while (int16)i < (int16)end (a 32-bit counter whose low word
is compared): when word W[i] ≠ 0, p = `part_ratio(A+206h, i)` (EAX and EDX
as left by the previous step, ECX = A+206h); v = (`rng_next(4d2599)` &
3Fh) + p (16 bit; EAX = p, part_ratio's EDX/ECX at that call); when
(int16)v > (int16)top, top = v and best = i. Word `[A+2A2h]` ← best.
Result: EAX = (int16)first sign-extended and EDX = W + 2·(int16)first
when no part was scored, else EAX = the last v over its rng_next's high
word and EDX = the last p.

### 41e828 `drive_apply(A, S)`

1. When (int32)`[S+0Ah]` < (int32)`[S+10h]`: word `[S+0Eh]` ← 1 when
   (int16)`[S+4]` ≥ 4000h, else FFFFh. Otherwise when (int32)`[S+0Ah]` >
   (int32)`[S+14h]`: word `[S+0Eh]` ← FFFFh when (int16)`[S+4]` > 4000h,
   else 1. (`[S+4]` is still the previous magnitude.)
2. Word `[S+4]` ← |(int16)`[S+2]`|, with 8000h → 7FFFh.
3. steer = (int16)`[S+2]` >> 6 (arithmetic). When word w = `[S+0Eh]` ≠ 0:
   word `[A+5Ah]` ← w, then w' = word `[S+0Eh]` (reread): speed = (A+5Ah)
   with its low word w' when w' = 0 (only when A+5Ah overlaps S+0Eh by one
   byte), else (A+5Ah)'s high word over FF00h when w' < 0, or that plus
   200h (32-bit: the high word + 1 over 0100h) when w' > 0. w = 0 → speed
   = 0.
4. `vehicle_control(A, steer, speed, 0)` (EAX = S, EDX = steer, ECX = A);
   return its EDX:EAX.

### Helper hooks

Six refuse compare mode (they call game code). `point_offset` accepts it:
it writes P[0..7] in place, so it declares an 8-byte span on P and a
comparator remapping EAX = P
([compare_mode.md](../reference-game/compare_mode.md#flag-level-leaves)).
No entry shim, no game instruction embedded.

| Hook | Prologue | Relocate / lengths | Slots | Callee counters |
|---|---|---|---:|---|
| `point_offset` | `55 8b ec 53 56 57 8b 75 0c` | 5 / 1,2,1,1 | 3 | — |
| `threat_sum` | `55 8b ec 83 c4 f8 33 c0` | 6 / 1,2,3 | 1 | call_471c70 |
| `engage_done` | `55 8b ec 53 56 57 8b 5d 08` | 5 / 1,2,1,1 | 1 | call_411cb4, call_423918 |
| `flank_step` | `55 8b ec 83 c4 f4 53 56 57` | 6 / 1,2,3 | 2 | call_467a48, slot_3c, call_4929fc, call_492934 |
| `retreat_check` | `55 8b ec 83 c4 b8 53 56` | 6 / 1,2,3 | 1 | call_41eb34, call_413e50, slot_40, call_41cb44 |
| `part_pick` | `55 8b ec 83 c4 f4 53 56 57` | 6 / 1,2,3 | 1 | call_492ee0, call_40dbc0 |
| `drive_apply` | `55 8b ec 53 56 57 33 f6` | 5 / 1,2,1,1 | 2 | call_416a04 |

**Branches.** 63 sites (2 in `threat_sum`, 10 in `engage_done`, 7 in
`flank_step`, 25 in `retreat_check`, 10 in `part_pick`, 9 in
`drive_apply`; `point_offset` has none). Every one is reachable both ways;
the drive-word reread at 41e8b0 is taken only when A+5Ah overlaps S+0Dh
(never in the game, where S is the caller's frame; the in-vitro cases
arrange the overlap), so no edge is proved one-way.

## Static-obstacle list 404ae4

Hook `obstacle_test`, group `obstacle`. Callers (direct calls only; the
address occurs nowhere else in the image): `ground_collide` 418f74 at
4193ca (`404ae4(&P, radius, 0)`), `spawn_point` 42354c at 42368b
(`404ae4(out, 5000, 0)`) and the convoy collision test `convoy_collide`
46a614 at 46a6bb (`404ae4(B+26h, radius, B)`, [convoy.md](convoy.md)).
Every caller tests only AX afterwards; ECX is dead at all three (popped or
overwritten first, see [player_movement.md](player_movement.md) and the
`cell` liveness walk in
[weapons.md](weapons.md#leaf-hooks-side_scale-426b04-and-cell-427238)).

### 404ae4 `obstacle_test(P, radius, exclude)`

cdecl, three slots: P (point, only passed on), radius (only its low word
is used, and only as the second slot of 427c68, which never reads it),
exclude (an object pointer compared as a dword). Entry EAX/EDX/ECX are
never read. Frame (`ADD ESP,-23Ch` after the saved EBP): the list
L = EBP−23Ch (dwords), the cursor dword EBP−0Ch, the count word EBP−6,
the iterator dword EBP−4. The word EBP−8 is never written. EBX/ESI/EDI
are saved and restored.

1. iterator ← 0, count ← 0, cursor ← L; EAX = 0, EDX = L.
2. Loop: o = `471c70(pool, iterator)` with the pool dword `[4a95a0]`
   reread for every call (ECX = pool; EAX and EDX as left by the previous
   step). 471c70 walks the pool's used list backwards (the first call
   returns the tail, then each node's predecessor; it reads no entry
   register). The iterator slot ← o; o = 0 ends the loop.
3. o = exclude: skip (EAX = o, EDX = 471c70's).
4. C = `[o+1F2h]` (ECX). When word `[C+6]` ≠ 0, o must pass a first test:
   EDX = C reread; word `[C+4]` = FFFFh → EAX = 0; else ECX = `[o+205h]`
   and (int16)`[ECX+5]` ≥ 2 → EAX = 0; else EAX = o+92h and EAX = 1 when
   byte `[o+99h]` ≠ 0, else 0. AX = 0 skips o.
5. EDX = `[o+1F2h]` reread. When word `[EDX+4]` = FFFFh: EAX = o+92h, and
   o is skipped when byte `[o+99h]` ≠ 0 and (int16)`[EDX+12h]` ≤ 1.
6. Append: EDX = the cursor slot's value, dword `[EDX]` ← o, the count
   word += 1, the cursor slot += 4 (each read from its frame slot).
7. After the loop (EAX = 0): `427c68(P, radius word, L, count)` with EAX =
   the radius word (zero-extended), EDX = P, ECX = L; the count slot's high
   word is the high word of the EDX the last 471c70 call left (427c68 reads
   the slot as a word at 427c80/427d6d). Return 427c68's EDX:EAX; ECX is
   427c68's (L when the count is 0), dead at every caller.

427c68 is the collision walk (hook `collision_walk`, contract in
[Collision walk 427c68 and shape cell 427360](#collision-walk-427c68-and-shape-cell-427360)):
for each list entry below the count it tests the 2-D distance against the
entry's shape radius and returns the first nonzero `shape_cell` result.

### The overflow: which locals, in which order

The list holds 140 entries: L + 4 × 140 = EBP−0Ch is the cursor slot; only
the cursor is overwritten, not the count or the iterator. With n
qualifying objects o_0 … o_(n−1):

- Entries 0 … 139 are o_0 … o_139.
- The 141st append (o_140) reads the cursor (= EBP−0Ch) and writes o_140
  into the cursor slot itself; the count becomes 141 and the cursor slot
  += 4 makes the cursor o_140 + 4.
- Every later append k ≥ 141 writes o_k to o_140 + 4(k − 140), i.e. into
  the 141st object's own fields from +4 on; from k = 140 + 9Bh the dword
  reaches past its 26Dh bytes into whatever follows it in the pool's node
  array (the next node's links and fields). Nothing else in the frame is
  written: the count word, the iterator, EBP−8, the saved registers and
  the return address keep their values. The count word keeps counting
  (16-bit wrap).
- 471c70 is called after every append, so the original iterates over
  whatever those writes changed (a later object's node links or filter
  fields).
- 427c68 then sees: entry 140 = the final cursor o_140 + 4(n − 140), entry
  141 = (n << 16) | the uninitialized word EBP−8, entry 142 = 0 (the
  iterator after the last 471c70 call), entries 143 … = the saved EBP, the
  return address, the three argument slots and the caller's frame. Unless
  an earlier entry hits, entry 142 makes the original read `[34h]` and
  fault; for n ≥ 32768 the signed count is negative and nothing is read.

The sibling 404bc0 (`pool_query`, [ai.md](ai.md#404bc0-pool_queryout-record-filter))
has the same list with its cursor at EBP−10h.

### The overflow is unreachable in valid game states (proof)

1. The list only holds objects of the pool `[4a95a0]`. That pool is
   created once per mission by 405df4 (called only at 4252a0) as
   `471ad8(pool, count, 26Dh)`; the count is the word EBP−60h of the
   mission loader, incremented only at 42501d, once per base-object index
   i < (base-object count EBP−5Ah) whose mark byte `[4aa8ba+i]` is set
   (424e8a marks the members of category-2 actor groups). The pool never
   grows: the only allocations from it are the four 471b30 calls in the
   object factory 405314 (4054f1, 405613, 405734, 40587d; 405314 has the
   single caller 425c90, the mission loader's base-object loop), and
   471b30 returns 0 when the free count is 0. So the list length is at
   most the number of base objects in the prepared script.
2. The mission loader itself is built for at most 140 base objects: the
   mark tables `4aa8ba`, `4aa946` and `4aa9d2` are 8Ch = 140 bytes each
   (cleared by `memset(…, 0, 8Ch)` at 4243a6/4243ba/4243ce), and the
   per-object word array EBP−0B60h … EBP−0A48h holds 140 words. 141 base
   objects already overflow those, so the list's capacity equals the
   loader's own limit.
3. The retail data stays far below: VSHELL keeps at most as many list-14
   (actor table C, base object) records as the MSN declares (duplicates
   replace earlier slots, ID −1 records are dropped; vshell_preparation.md),
   and an own parse of all 62 `ZONES.VOL` `MSN\*.MSN` members (17-list
   framing of [mission.md](../formats/mission.md), 61 exact, DEMO2 short
   as documented) gives at most **89** list-14 records (C1_09; then
   C1_06/C1_10 84, C1_01/C1_02/C1_05/DEMO_02 80; the scratch listing
   `msn_list14.txt` is not kept). The
   prepared C1_01 script holds 50.

So no retail mission, and no mission the loader can hold, gets near the
141st entry. The replacement nevertheless reproduces the overflow (next
section); the in-vitro harness checks it on synthetic pools.

### Obstacle-list replacement

The core works on a frame image of 23Ch bytes in the adapter's scratch:
the list, the cursor slot at +230h, EBP−8 at +234h, the count word at
+236h and the iterator at +238h, and it appends through the cursor slot
exactly as the original does. Hence the cursor overwrite, the writes into
the 141st object and beyond, the count, entry 140 and entry 142 are
identical for any n. Two things are not modelled: the low word of entry
141 (the original's uninitialized EBP−8; the adapter's scratch holds 0),
which 427c68 dereferences only when n ≥ 142 and none of entries 0 … 140
hits, and entries ≥ 143 (the original's saved registers and caller frame),
which 427c68 reaches only after dereferencing entry 142 = 0, where the
original faults. Both need at least 142 base objects (unreachable, above);
no entry shim captures EBP−8.

## Collision walk 427c68 and shape cell 427360

Hooks `collision_walk` and `shape_cell`, group `obstacle_walk`; the third
hook of the group, `convoy_leader` 46a5bc, is in
[convoy.md](convoy.md#46a5bc-convoy_leaderb).

**Callers.** 427c68 has one call site, 404bb0 in `obstacle_test` 404ae4;
427360 has one, 427d50 in 427c68 (direct calls only; neither address
occurs elsewhere in the listing).

### 427c68 `collision_walk(P, radius, L, count)`

cdecl, four slots. Frame `ADD ESP,-24h` after the saved EBP, then EBX,
ESI, EDI pushed (restored at the end): D = EBP−24h (two dwords), R =
EBP−1Ch (two dwords), the slots EBP−14h and EBP−10h (written; only
EBP−10h = E+0Ch is read back), M = EBP−0Ch (four words), the index word
EBP−2. EBP−4 is never touched. The radius slot (EBP+0Ch) is never read.
The count slot is read only as a signed word (`CMP AX/DX, word
[EBP+14h]` at 427c80 and 427d6d). Entry EAX is never read; entry EDX
survives only to the return of an empty walk; entry ECX reaches the first
4928d0 call.

1. i ← 0 (word), EDI ← L, AX ← i (EAX's high word is L's). When
   (int16)i ≥ (int16)count (427c84): EAX = 0, return (EDX and ECX as at
   entry).
2. Loop body, E = `[EDI]` (EBX): when `[E+34h]` ≠ 0, EAX = `[E+34h]` and
   S = `[EAX+4]` (ESI); else S = 0, which faults at 427cac (`[8]`), so the
   JE at 427c90 is taken only by an invalid entry (proved one-way below).
3. `dist2d_point 4928d0(P, E+26h)` with EAX = E+26h, EDX = P, ECX as left
   (entry ECX, (int16)`[S+8]` of the previous entry, or 427360's ECX).
4. ECX = (int16)`[S+8]`, read after the call. d ≥ ECX (signed dword,
   427cb2): next entry (EDX = 4928d0's).
5. Inside: EDX = c = E+0Ch (also stored at EBP−10h). When word `[c+26h]`
   (E+32h) = 0: `euler_matrix 47ebb8(c, c+6)` with EAX = c+6, EDX = c,
   ECX = c (stored at EBP−14h), then ECX = c and word `[c+26h]` ← 1.
6. The four words at c+6 (E+12h … E+19h, read after the matrix call) are
   copied to M through DX, CX, DX, AX (16-bit loads that keep the
   registers' high words: EDX's from step 5 or from 47ebb8, ECX's from
   step 4 or c). `47df04(M)` (the unhooked 2 × 2 swap of words +2/+4, an
   `enter 0,0` leaf that returns EAX = M) with EAX = (c+6)'s high word
   over word 3, EDX = the old EDX's high word over word 2, ECX = M.
7. EAX = P, ECX = M (`POP ECX`); D.x = `[P]` − `[E+26h]`, D.y = `[P+4]` −
   `[E+2Ah]` (P and E reread here, in the order P.x, P.y, E.x, E.y).
   `rotate_2d 47ffe8(D, M, R)` with EAX = R, EDX = M, ECX = D.
8. `427360(S, R, 0)` with EAX = R, EDX/ECX = 47ffe8's. EAX (whole dword,
   427d5c) ≠ 0: return it with 427360's EDX (and ECX).
9. Next entry (427d62): i += 1, EDI += 4, DX ← i (EDX's high word is the
   last callee's: 4928d0's after a distance skip, 427360's after a miss);
   loop while (int16)i < (int16)count (427d71). Then EAX = 0 and return
   with that EDX.

So EDX:EAX is (hit, 427360's EDX), (0, the last callee's EDX high word
over i) or (0, entry EDX). ECX at return is dead: `obstacle_test` passes
it on and every caller of 404ae4 (418f74, 42354c, 46a614) pops or
overwrites ECX first ([Static-obstacle list 404ae4](#static-obstacle-list-404ae4);
the liveness walk of the cell's ECX in [weapons.md](weapons.md)). The
callers of 404ae4 test only AX, so the EDX high word of the no-hit exit is
dead too; the replacement keeps it anyway.

**The count slot and `pool_prev`.** `obstacle_test` pushes the count slot
as EDX = the last `pool_prev` 471c70 call's EDX high word over the count
word, and the radius slot as EAX = 471c70's EAX (0) high word over the
radius word. 427c68 reads the count slot only as a word and never reads
the radius slot, so neither high word reaches it. The bytes are identical
in replaced runs anyway: the replaced `pool_prev` returns the original EDX
on every path ([main_loop.md](main_loop.md#pool_prev-0x471c70-cdecl-p-o))
and the replaced `obstacle_test` builds both slots from the registers it
got.

### 427360 `shape_cell(S, R, word)`

cdecl, three slots; EBX saved and restored. Frame `ADD ESP,-0Ch`: Q =
EBP−0Ch (two dwords), the shifted origin words at EBP−4 and EBP−2.

1. EAX = S. Word `[S+2Ah]` = 0 (42736f): EAX = 0, return (EDX and ECX
   as at entry).
2. Q.x = `[R]` (via ECX), Q.y = `[R+4]` (via EDX); DX ← word `[S+32h]`
   (EDX keeps R.y's high word); the shift count is CL, the low byte of
   that word, masked to five bits by `SHL r16,CL`. Word `[S+2Eh]` << count
   → EBP−4, word `[S+30h]` << count → EBP−2 (16-bit shifts: a count of 16
   to 31 gives 0).
3. EDX = (int16) word EBP−4, ECX = (int16) the second word; Q.x += EDX,
   Q.y += ECX (dword wrap).
4. ECX = Q, DX ← the third slot's low word (EDX keeps the sign-extended
   x word's high word, 0 or FFFFh). `cell 427238(S, Q, EDX)` with EAX =
   S, that EDX pushed as the whole dword, ECX = Q. Return 427238's
   EDX:EAX (its EDX is S, [weapons.md](weapons.md)) and ECX.

ECX at return is dead (the cell liveness walk in [weapons.md](weapons.md)
covers 427360's caller); EDX:EAX is exact.

### Collision-walk branch sites

427c68: 427c84, 427c90, 427cb2, 427cc6, 427d5c, 427d71; 427360: 42736f.
All both ways except 427c90, proved one-way (`PROVED`, edge 0): its taken
edge leaves ESI = 0 and 427cac reads `[8]`, which faults in every Win32
process.

### Collision-walk replacement

Cores `es2_collision_walk_registers` and `es2_shape_cell_registers` in
`core/src/obstacle.c`. Every callee (4928d0, 47ebb8, 47df04, 47ffe8,
427360; 427238) goes through the bridge, so the nested hooks
(`dist2d_point`, `euler_matrix`, `rotate_2d`, `shape_cell`, `cell`) keep
their modes; 47df04 stays original code behind the bridge (unhooked; no
map probe because of its `enter`). D, R, M and Q live in a zeroed frame
image in the adapter's stack scratch at the original offsets; no callee
reads a byte of them that the function did not write (rotate_2d reads D
and M, 427360 reads R, cell reads Q.x/Q.y), so no uninitialized local is
read and no entry shim is needed. The index word stays in the core (no
callee gets its address).

**Compare mode.** Both accept compare. `shape_cell` writes nothing but its
frame, and its callee `cell` is a read-only compare-capable hook.
`collision_walk` writes object state only through the lazy matrix cache
(euler_matrix's matrix at E+12h and the flag word E+32h), which the
compare's original phase fills first, so the replacement phase finds the
flags set and writes nothing (the lazy-matrix-cache argument of
[compare_mode.md](../reference-game/compare_mode.md#compare-contract)). Its
comparator checks EAX and DX: after a walk without a hit the high word of
EDX is the last callee's, and in the compare's original phase the nested
hooks run their originals, whose EDX can differ from a nested hook's in
another mode (dead at every caller, above).

### Collision-walk evidence

- Core tests `//core:obstacle_test` (hit, no hit, empty and negative
  count; register images at every call, the D/M/R contents, the flag;
  shape_cell's early return and shift counts 4, 16 and 32).
- In vitro (`differential_obstacle_walk_test`, full tier
  `differential_obstacle_walk_full_test`, `obstacle_walk_cases.h`):
  1,001,024 direct cases per hook against logging stubs (ordered calls,
  arguments, entry registers, frame payloads by content and frame
  pointers as offsets, return pair, arena; the stubs change the radius
  word, the flag, the matrix, P and later list entries between the
  reads), 256 installed 31/a7 poison probes, 2,304 real-callee cases
  under `obstacle_test` over a real pool with eight nested hooks in
  rotating modes (`obstacle_test`, the other new hook, `pool_prev`,
  `pool_next`, `dist2d_point`, `euler_matrix`, `rotate_2d`, `cell`) and
  the hook itself original, replaced or compare, and 512 installed compare
  cases each (exact replacement 0 mismatches, skewed replacement one
  mismatch per call).
- Mutations: `check_mutations.ps1`.
- Live (all hooks and imports
  replaced, heap shadow on): all suites identical, no skip or bypass.
  `hk_collision_walk_calls` equals `hk_obstacle_test_call_427c68` and
  `hk_shape_cell_calls` equals `hk_collision_walk_call_427360` in every
  suite; `shape_cell` is 0 in c4_08 and c3_10.

## Results writer 42412c and its helpers

Four hooks, group `results_writer`, core `core/src/results_writer.c`:
`results_writer` 42412c, `score_sum` 423e88, `vehicle_value` 418e60,
`list_append` 426ac8. The byte layout of the two files is in
[mission.md](../formats/mission.md#return-resultsdat); this section is the
exact runtime contract. Notation: f = the function's EBP; `lo(r, w)` =
register r with its low word replaced by w; `sx8`/`sx16` = sign extension;
hexadecimal constants are image VAs or values as in the listing.

**Call chain.** 42412c is called once from 461ff0 (`462029`, the DBSIM
return path) with the player vehicle `[4d24bc+2Ah]`. It calls 423e88
(`4241c3`), which walks the MECH list and calls 418e60 (`423ed7`) for
every opposing vehicle that counts; 418e60 calls 426ac8 (`418ede`) per
salvageable weapon. 426ac8 is also called by `mech_part_hit` (`418ad6`).
No other call to the four entry points exists in the listing.

### 426ac8 `list_append(item)`

cdecl, one slot, `PUSH EBP` frame without locals. EAX = `sx16([4a9e6a])`;
EDX = `[4a9e6c]`; ECX = `[item]` (the dword at the argument); it is
stored at `EDX+4·EAX` (no bound check; a negative count writes below the
list); then `INC WORD [4a9e6a]` rereads the count (a store that hit the
count word is seen). Result EAX, EDX, ECX as just stated; entry registers
are not read. Both callers pop ECX (`POP ECX`) right after the call. At
418ee3 EAX/EDX are dead (the loop test reloads EDX, the body EAX); at
`418adb` they reach `40da38` only as entry registers, which it overwrites
before reading them (`40da4a`, `40da4f`).

### 418e60 `vehicle_value(V)`

cdecl, one slot; frame `ADD ESP,-8` (dwords f−8 and f−4), then EBX = V,
ESI, EDI pushed.

1. `byte [V+0B3h]` (V+92h+21h) ≠ 0 (`418e76` falls through): EAX = 0,
   EDX = the entry EDX, ECX = the dword f−4, which this path never writes
   (stack residue); return.
2. Weapon loop (`418e83`…`418ef1`), i = ESI (32 bit) from 0 while
   `i < sx16([W+8])` with W = `[V+202h]` (both reread at every test,
   signed 32-bit compare). Per i: EAX = `sx16(lo16(i))`, EDX = `[W+4]`,
   item I = `[EDX+4·EAX]`; I = 0 skips (`418e90`).
   - EAX = `sx8(byte [[I+18h]+17h])`, then `ADD AX,13h` (the high word
     keeps the byte's sign); `part_ratio(V+206h, EAX)` 40dbc0 with that
     EAX, EDX = V+206h, ECX = what the previous step left (the entry ECX
     at the first call).
   - r = the returned AX; `s16(r) ≥ 80h` skips (`418eb1`).
   - item id `word [[I+1Ch]+56h]` → word f−4 (through DX: EDX becomes
     `lo(part_ratio's EDX, id)`), ECX = `100h − sx16(r)`, the percentage
     `((100h − sx16(r))·100) SAR 8` (32 bit) → word f−2;
     `list_append(f−4)` 426ac8 with EAX = that 32-bit percentage, EDX =
     f−4, ECX = `100h − sx16(r)`; afterwards ECX = f−4 (`POP ECX`).
3. After the loop (EDX = W, EAX = the count): ECX = `[V+1F2h]`, SI =
   `word [ECX+54h]`; `part_ratio(V+206h, 0)` with EAX = V+206h, EDX = W,
   ECX = `[V+1F2h]`. When its AX = 100h exactly (`418f12` falls through)
   SI = `SAR SI,1` (16-bit arithmetic shift).
4. `damage_score(V+206h)` 41537c with the registers part_ratio returned;
   ECX = V+206h afterwards. Dword f−8 = `sx16(SI)`; `multiply_q10(score,
   sx16(SI))` 47e0b0 with EAX = EDX = `sx16(SI)`, ECX = V+206h.
5. Result EAX/EDX = multiply_q10's; ECX = the dword f−4 (`POP ECX`
   twice): the last pair (id, percentage) this call appended, else
   residue.

ECX is dead at the only caller (423e88 pops it at `423edc`). The
replacement returns the dword at its own scratch f−4, which is exact
whenever step 2 appended; the residue of the other paths is not
reproduced.

### 423e88 `score_sum(team, unused)`

cdecl; the caller pushes two slots (team, and `lo(EDX, [4a9e54])`); 423e88
reads only the first (EDI = team). Frame `ADD ESP,-8`: f−8 the sum, f−4
the cursor; EAX = EDX = 0 before the first call (`XOR`).

1. Cursor c = 0. Loop (`423ee0`): `pool_prev([4a9b7a], c)` 471c70 (the
   MECH list dword is reread before every call) with EDX = `[4a9b7a]` and
   EAX/ECX as the previous step left them (EAX = 0 and the entry ECX at the
   first call). c = the returned EAX; c = 0 ends the loop (`423efa`).
2. Per vehicle c: EAX = EDX = `sx8(byte [[c+45h]+12h])` against ECX =
   `sx8(byte [team+12h])` (reread for every vehicle): equal skips
   (`423eb0`, same side).
3. Counted when `byte [c+0A4h]` (c+92h+12h) ≠ 0 (`423ebe`) or, if it is
   0, `byte [c+99h]` (c+92h+7, EDX = c+92h for this test) ≠ 0 (`423ec6`);
   EAX = 1 resp. 0 and `TEST AX,AX` (`423ed4`) decides.
4. Counted: `vehicle_value(c)` 418e60 with EAX = 1, ECX = the team's side
   byte, EDX = the vehicle's side byte (first test true) or c+92h; sum +=
   EAX (32 bit); ECX = c afterwards (`POP ECX`).
5. Result EAX = sum, ECX = the final cursor (always 0), EDX = the last
   pool_prev's EDX.

At the caller (`4241c8`) EAX is the sum; EDX and ECX reach `multiply_q10`
47e0b0 only as entry registers, which it does not read (`ENTER`, two
stack slots).

### 42412c `results_writer(P)`

cdecl, one slot (P = the player vehicle, kept in EBX until step 3);
`ADD ESP,-4Ch`; EDI = R = f−0Ch (the `results.dat` stream object, 12
bytes). Exception frame through the runtime setup 49630c with EAX =
49a76ch (descriptor: record at f + `[49a770h]` = f−38h, so FS:0's saved
head is the dword f−38h and the context word is f−28h). Locals: f−18h
the `mission.var` stream V, f−3Ch the team T, word f−3Eh, dword f−44h,
words f−46h, f−48h, f−4Ah; no local is read before it is written.

| Step | Memory and call (arguments) | EAX / EDX / ECX at the call |
| --- | --- | --- |
| 1 | 49630c (register ABI, EBP = f) | 49a76ch / entry EDX / entry ECX |
| 2 | T = `[P+45h]` → f−3Ch; `objective_eval(P)` 413280 | 49630c's EAX (the old FS:0 head) / T / entry ECX |
| 3 | flag = (its `AX = 9`); context word ← 8; path `45ee00(49a97dh)` ("data\results.dat") | objective_eval's EAX / its EDX / flag (0 or 1) |
| 4 | open `47b064(R, path)` | path / 45ee00's EDX / 49a97dh |
| 5 | `[R+4]` ≠ 0 (`42417e` falls through): `479c78(6c56c8h, 5, 0, 49a98eh, 6Fh, 49a998h)` (fatal report; in the game it does not return) | `[R+4]` / 0 / 47b064's ECX |
| 6 | word f−3Eh ← flag; `V14(R, 2, f−3Eh)`, V14 = `[[R]+14h]` | `[R]` / previous EDX / f−3Eh |
| 7 | `score_sum(T, lo(EDX, [4a9e54]))` 423e88, T from f−3Ch | the write's EAX / `lo(the write's EDX, [4a9e54])` / T |
| 8 | `multiply_q10(9C4h, sum)` 47e0b0 | sum / score_sum's EDX / its ECX |
| 9 | tally = product + `sx16([4a9e98])`·25000 (32 bit) → f−44h; `V14(R, 4, f−44h)` | `[R]` / `sx16([4a9e98])`·25000 / f−44h |
| 10 | count `[4a9e6a]` → word f−46h; `V14(R, 2, f−46h)` | `[R]` / `lo(the write's EDX, count)` / f−46h |
| 11 | list L = `[4a9e6c]` (read once); for i = 0 while `s16(i) < s16([4a9e6a])` (count reread at every test, `42426a`): word `[L+4i]` → f−48h, `V14(R, 2, f−48h)`; word `[L+4i+2]` → f−4Ah, `V14(R, 2, f−4Ah)` | `lo(previous EAX, word)` / f−48h resp. f−4Ah / `[R]` |
| 12 | `result_conditions(T, R)` 423d68, T reread from f−3Ch | T / previous EDX / previous ECX |
| 13 | close `47b09c(R, 2)` | result_conditions' registers |
| 14 | context word ← 14h; path `45ee00(49a9a4h)` ("data\mission.var") | 47b09c's registers |
| 15 | open `47b064(V, path)` | path / V / 49a9a4h |
| 16 | `[V+4]` ≠ 0 (`4242a7` falls through): `479c78(6c56c8h, 5, 0, 49a9b5h, 84h, 49a9bfh)` | `[V+4]` / 0 / 47b064's ECX |
| 17 | `47b2b8(V, 7D0h, 4a9e70h)`: the 2,000-byte variable bank, a direct call of the class's write | `[V+4]` (0) or the report's EAX / previous EDX / V |
| 18 | close `47b09c(V, 2)` | V / 47b2b8's EDX / its ECX |
| 19 | EDX = the dword f−38h → FS:0; return | |

`[4a9e98]` is word 14h (byte offset 28h) of the mission variable bank at
4a9e70h, one of the words DBSIM clears after reading `mission.var`
([mission.md](../formats/mission.md#missionvar)); `[4a9e54]` is the
mission mode word.

"Previous" means the registers the preceding call returned. The stream's
table is read from `[R]` before every V14 call (into EAX in steps 6, 9,
10, into ECX in step 11); `[R+4]` and `[V+4]` are read once, right after
the open. Result: EAX = the last close's EAX, EDX = the saved FS:0 head,
ECX = the last close's ECX.

`results.dat` thus receives the word flag (objective status 9), the dword
tally, the word count P, P word pairs from the salvage list and then
result_conditions' 72-byte records; `mission.var` receives the 2,000 bytes
at 4a9e70h. This is the grammar our shell model reads (`shell/src/
campaign.c`, `run()`: outcome word, u32 tally, i16 pair count, four bytes
per pair, then the actor records); the unit test reassembles the bytes
the core writes and reads them back with that grammar.

**Caller.** 461ff0 pops the slot (`46202e POP ECX`), then reads only
`[4d24bc+74h]`, `+42h`, `[[4d24bc+2Ah]+99h]` and `[4a9e52]` and writes
`[4d27b8]`; EAX and EDX can reach its caller 45f54b unchanged (426514 and
425344 are bare `RET`s), where 45f550..45f556 overwrites EDX and pushes
the slots of 453848, which reads only its slots. EAX/EDX are exact in the
replacement; ECX is dead. The frame residue below 461ff0's ESP is not
read afterwards: 426514, 425344 and 453848 have no locals.

**Branch sites (13).** 418e76, 418e90, 418eb1, 418ef1, 418f12 (418e60);
423eb0, 423ebe, 423ec6, 423ed4, 423efa (423e88); 42417e, 42426a, 4242a7
(42412c). 426ac8 has none.

### Results-writer replacement

Adapters in `proxy/results_writer_hooks.h`. Nested hooks keep their modes
(`objective_eval`, `score_sum`, `multiply_q10`, `result_conditions`,
`pool_prev`, `vehicle_value`, `part_ratio`, `list_append`,
`damage_score`). The writer's frame lives in adapter scratch at the
original's EBP offsets (synthetic EBP): 49630c links the record at
scratch f−38h into FS:0; the stream objects R and V are built in place by
the original 47b064; the V14 writes, the closes 47b09c, the path builder
45ee00, the block write 47b2b8 and the fatal report 479c78 stay original
guest calls (file I/O through CW3220); the adapter writes the returned EDX
back to FS:0 as the epilogue does. `vehicle_value` keeps f−8/f−4 in 8
scratch bytes (the pair handed to `list_append` is f−4). EDX:EAX are exact
for all four; ECX is modelled in the core (exact except `vehicle_value`'s
residue path) and dead at every caller. No entry shim: no entry register
or unwritten frame byte is read (apart from that dead ECX residue). All
four refuse compare mode (file writes, game calls, runtime-indexed
stores).

Callee counters: `hk_results_writer_call_<va>` for 49630c, 413280, 45ee00
(2), 47b064 (2), 479c78 (0 in a normal run), 423e88, 47e0b0, 423d68,
47b09c (2), 47b2b8, and `hk_results_writer_slot_14` (3 + 2 per salvage
pair); `hk_score_sum_call_471c70` (MECH list length + 1) and
`_call_418e60`; `hk_vehicle_value_call_40dbc0`, `_426ac8`, `_41537c`,
`_47e0b0`. `list_append` has no game call.

### Results-writer evidence

- Unit test `//core:results_writer_test` (144 checks): `list_append`
  with positive, negative and self-overwriting counts; `vehicle_value`
  excluded, appending, skipping null items and ratios ≥ 80h, halving on
  100h only, a count lowered by part_ratio (reread), negative ratios;
  `score_sum` over a planned walk with every branch, the MECH list dword
  reread and the registers at every call; the writer's 19 calls with
  their registers and arguments, both stream-failure reports, a pair
  write that lowers the count and moves the list; the bytes the writes
  produce are reassembled and read back with the shell's `results.dat`
  grammar (`shell/src/campaign.c`, `run()`), plus the 2,000-byte bank.
- In vitro (`differential_results_writer_test`, full tier
  `differential_results_writer_full_test`, `results_writer_cases.h`):
  1,001,024 direct cases per hook (original through an own caller,
  adapter, core for ECX; ordered stub calls with slot counts, arguments,
  entry registers, the record's context word, the written bytes; arena,
  globals including the whole variable bank, FS:0, callee counter
  deltas), 256 installed 31/a7 poison probes per hook, 3,072 real-callee
  cases each for `results_writer` (nine nested hooks in rotating modes:
  score_sum, pool_prev, pool_next, vehicle_value, part_ratio,
  list_append, damage_score, multiply_q10, result_conditions; real
  49630c), `score_sum` (seven) and `vehicle_value` (four). All 13 branch
  sites both ways.
- Four caught mutations (`check_mutations.ps1`).
- Live (all hooks
  `replaced`): all six suites identical, including `results.dat` and
  `mission.var` of the four outcome suites. The calls come at process exit
  after the last state record: `results_writer` and `score_sum` once in
  c1s/c3_09/c4_08/c3_10, `list_append` (as a hook) in c1s only; the map
  probe's other `426ac8` entries come from `mech_part_hit`, whose core
  computes the append itself. A run of `es2_shell_results` itself on the
  produced bytes is not part of the evidence (it needs a full SAV and VFS
  fixture).

## Team result records 423d68

Hook `result_conditions`, group `results`. 423d68 receives the player's
group object (the layout `group_assess` walks: member array dword +0Ch,
count word +10h) and is the last block of the results writer: for every
member of that group it writes the member's 72-byte result record into
`results.dat`. Record layout and the shell side are in
[mission.md](../formats/mission.md#return-resultsdat); this section is the
exact runtime contract.

**Caller.** Only 42412c (the results writer, called once from the DBSIM
return function 461ff0 at 462029), at 424271: `423d68([player+45h],
stream)` with `stream` the write stream at the caller's EBP−0Ch (class
table 4a1760, opened on `results.dat` by 47b064 at 424171). After the
return the caller pops the two slots and calls `47b09c(stream, 2)` (close);
neither reads EAX, EDX or ECX first, so all three are dead.

### 423d68 `result_conditions(team, stream)`

cdecl, two slots; entry EAX/EDX/ECX are not read except that EAX's high
word and EDX survive to the return when the group has no member. Frame
(`ADD ESP,-64h` after the saved EBP, then EBX/ESI/EDI pushed): the record
`out` = EBP−64h (84 bytes, filled by `condition_record`), three word
temporaries EBP−0Eh/−0Ch/−0Ah, the member pointer EBP−8 and the member
index word EBP−2. EDI = stream for the whole call.

1. `i = 0` (word). Loop test at 423e6e: ECX = team (reloaded from the
   argument slot), `AX = R16(team+10h)` (reread every time, high word of
   EAX kept), continue while `s16(AX) > s16(i)` (JG 423e79).
2. Member: `A = R32(R32(team+0Ch) + 4·sx(i))` (array pointer reread each
   member); `condition_record(A, out)` 41b4ec with EAX = A, EDX = out,
   ECX = A. Its returned registers are overwritten before they are read.
3. Three spans of the record, each a do-while of fixed length:
   `out+2` × 13 words (condition groups 0..12; temporary EBP−0Ah, loop
   423da4..423dda), `out+28h` × 10 (parts 0..9; EBP−0Ch, 423de1..423e17),
   `out+40h` × 10 (the ten combined group/part words; EBP−0Eh,
   423e1e..423e54). Per word c (read signed):
   `ECX = ((100h − sx(c)) · 100) SAR 8` (32-bit: SHL 2 and two LEA ×5),
   CX is stored back into the record word **and** into the span's
   temporary, then `stream.V14(stream, 2, &temporary)` with EAX =
   &temporary, EDX = `R32(stream)` (the table is reread before every
   write) and ECX = the full 32-bit value. Not written: record word +0,
   groups 13..18 (+1Ch..+27h) and parts 10..11 (+3Ch..+3Fh).
4. `stream.V14(stream, 6, A+2A4h)` with EAX = A+2A4h, EDX = `R32(stream)`
   and ECX = **the ECX the previous write returned** (nothing between
   423e46 and 423e64 writes ECX).
5. `i += 1` (INCW), back to the test.

Result: EAX = `lo(EAX, R16(team+10h))` from the final test, i.e. the high
word of the last write's EAX (or of the entry EAX when no member ran);
EDX = the last write's EDX (or the entry EDX); ECX = team. All dead at the
caller.

V14 of the stream class is 47b2b8 `write(stream, size, pointer)`: with
the error dword +4 nonzero it returns AL = 0 without writing, otherwise it
writes through the RTL (49677e) and returns 1 or the error path 47b294. It
reads no stack bytes of its caller beyond its three slots.

Values. The written word is `((100h − c) · 100) SAR 8` of the Q8 word c:
100h → 0, 0 → 100, −1 → 100; extremes stay in range (−8000h → 12900,
7FFFh → −12700), so ECX always equals the sign-extended stored word.
Which end means intact is a **hypothesis** from the written files: every
untouched team member reports 100 and every killed pilot 0 in part 9, so
the written word reads as remaining condition and c is 0 for an intact
part ([vshell_preparation.md](vshell_preparation.md#fields-seen-live-versus-the-static-contract)).

**Precondition.** Every word the spans read must have been written by
`condition_record` in the same member iteration, otherwise the original
reads its own frame residue (uninitialized at the first member, the
transformed words of the previous member afterwards). The real
`condition_record` stores word +0 and `condition_table` all 41 words
+2..+53h, so this holds in the game; the in-place stores are reproduced
anyway (below). The member array must hold `count` valid pointers (no
check).

**Branch sites (4):** 423dda, 423e17, 423e54 (span loops, both ways in
every member) and 423e79 (member loop: taken for every member, falls
through at the end or for a count ≤ 0). All four both ways in vitro.

### Team-result replacement

`core/src/results.c` (`es2_result_conditions_registers`), adapter
`proxy/results_hooks.h`. The record and the three temporaries live in the
adapter's 128 scratch bytes at the original's EBP-relative offsets
(EBP = scratch+80h), so the pointers handed to `condition_record` and to
the writes point at the same relative bytes, and the transformed words
are written back into the record as in the original (a mutation that
drops this store is caught by the stub that sometimes leaves the record
unchanged). `condition_record` (group `vehicle_update`) keeps its mode,
the stream write stays original code. Compare mode is refused (the
function writes the file). No entry shim: no frame byte is read before it
is written under the precondition. The stack residue below the caller
differs after the call (host frames instead of the 64h-byte frame); the
caller only closes the stream next: 47b09c and its callees 47b158 and
47a8a4, and the path builder 45ee00 that follows, read only their slots,
object fields and globals (checked in the listing); the RTL routines below
them (the close 496874 and the CW3220 imports) were not walked.

Callee counters: `hk_result_conditions_call_41b4ec` (per member) and
`hk_result_conditions_slot_14` (34 per member).

### Team-result evidence

- Unit test `//core:results_test`: no member (count 0 and negative), two
  members with fixed record values (call order, arguments, entry
  registers, written bytes, write-back, extreme values, ECX of the 6-byte
  write, EDX:EAX), and a write that lowers the count and switches the
  stream's table in the middle of a member.
- In vitro (`differential_results_test`, `results_cases.h`): direct
  cases against a patched `condition_record` stub and a write stub reached
  through two arena tables (ordered calls with arguments, entry
  EAX/EDX/ECX, stack addresses masked, and the bytes each write receives;
  EDX:EAX in full; the arena). The stubs change the count (0..6), the
  member array, the stream's table and the bytes at actor+2A4h, and the
  record stub fills only word +0 on a quarter of its later calls. Then
  256 installed 31/a7 poison probes and 3,072 real-callee cases with the
  real `condition_record` 41b4ec and `condition_table` 4151a4, both nested
  hooks in rotating modes, the hook original or replaced. All four branch
  sites both ways in the instrumented prefix.
- Two caught mutations (`check_mutations.ps1`).
- Live: the map's entry probes are 0/1/0/1/1/1 (acceptance-60s,
  c1_01-success, c1_01-collision, c3_09, c4_08, c3_10), one call per
  normal mission end. The writer runs after the last state record, so the
  tick counters stay 0, as for `damage_score` 418e60 on the same caller
  chain; the evidence of a replaced run is the identical `results.dat` of
  the four suites with a normal end.
