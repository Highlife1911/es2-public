# Ground movement: the DBSIM ground-vehicle update

The ground-vehicle movement callback `0x41a360` and everything below it:
the movement leaves, the
[matrix/pose/inverse-trig layer](#matrix-pose-and-inverse-trig-layer), the
[animation step 40250c](#animation-step-40250c-and-manager-reset-478b7c) with
the [track advance 478d38](#track-advance-478d38), the
[ground animation](#ground-animation-418f40-and-track-target-47967c),
[terrain height](#terrain-height-46e180),
[snapshot/rollback](#snapshot-402628-and-rollback-4027fc), the
[callback itself](#ground-vehicle-update-41a360), its
[collision](#ground-collision-418f74) and [effects](#ground-effects-4195c8),
the [damage path](#damage-path) of the collision's virtual targets, and the
helpers that share these leaves. Every function named with a hook name here
runs in `core/` behind that hook; the registry, the per-suite live call counts
and the all-hooks live regression are in
[hooks.md](../reference-game/hooks.md). Sound 462830 and 4628e0 are
`sfx_play`/`sfx_play_at`, the V58 point methods 41b60c/406808 are
`part_position`/`base_part_position`. Still original: the effects' callee
434144 and clip methods of classes other than `ANSequence`/`ANCyclicSequence`,
which run as game code behind their callers' hooks.

Analysis used the hash-pinned reference binaries
([reference_version.md](../reference_version.md); DBSIM SHA-256
`8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad`), PE
metadata and LLVM disassembly. CODE file offsets are VA − 401000h + C00h. The binary comparisons are
reproduced by the [in-vitro harness](../../tools/reference-game/invitro/README.md);
the mutation witnesses are listed in
[MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md).

Conventions. Addresses are DBSIM VAs; offsets are hex unless called decimal.
**C** means confirmed from the bytes (entry, stack use, direct edges, field
accesses); **H** marks a semantic or runtime interpretation that is not
established. "C + 1M" means C plus 1,000,000 or more in-vitro cases against
the original code with exact EDX:EAX and memory. `cdecl` means right-to-left
four-byte stack slots, plain `ret` and caller cleanup, including explicit
object pointers; `s16` arguments consume the low word of a slot. `EAX*` is a
scratch, path-dependent EAX with no value contract established; it is not
permission to ignore EAX in a hook. `A` = actor; `M` = animation manager
`[A+34]`; `T` = animation track; `R` = the track's resource `[T+0]` (an
`ANAnimList`) or, in the contract tables, a 32-byte transform; `C` = vehicle
config `[A+1f2]`. Globals list **direct** accesses; a parent's transitive
effects include all its callees. "Compare refused" means the hook is
`compare_unsupported`: its replacement calls unreplaced game code, so a
second compare phase would change game state twice.

## Entry and call graph

`0x415afc(actor)` dispatches the movement callback from the definition at
`actor+0x4d`: callback triple at definition+0x24/+0x28/+0x2c; the middle dword
adjusts the actor pointer before the stack call. For example, initialization
at `0x413fa2` copies the pointer stored at `0x499928` (`0x41a360`) into that
callback field. This is a stack-object callback, not an EAX/EDX/ECX call.
`0x41a360` is the ground-motion actor update; its frequency relative to the
`0x45f4ad` completed-frame boundary is an
[open question](#open-live-questions).

The graph expands the complete direct-call closure of the two integration
entries. Repeated nodes are shared. `Vxx` is a dynamic clip method at vtable
byte offset xx (targets in [Track advance 478d38](#track-advance-478d38)).
Imports terminate at the named import boundary. The ground update's
collision, sound and post-update side branches are listed in the
[side-path inventory](#callback-side-path-closure-inventory); this is not a
claim to have reverse-engineered those whole subsystems or all dynamic
callback targets.

```text
415afc -> definition callback -> 41a360
  41a360 -> 402628 (snapshot), 418f40, 46e180 (terrain), 418f74 (collision),
            4027fc (rollback), 462830 (sound/RNG), 4792ac, 4195c8
  418f40 -> 4796d8 -> 47967c -> 4793d4 -> V20/V24/V28/V2c
          -> 467924 -> 47e0a0
          -> 402604 -> 40250c
  40250c -> 49681a (CW3220!_memset)
          -> 478b7c -> 479194 -> 47ebb8, 47df19, 480304, 47fa20
          -> 478d38
               -> 478aac -> 4789b0, 479720, 4789d0, 4790b4
                    479720 -> V20/V24, 478ef4, 478f6c, 478ff4
                    4789d0 -> 4789b0, 478e8c
                    478f6c -> 47ebb8, 47fa20
                    478ff4 -> 47ebb8, 47df19, 480304, 47fa20
                    4790b4 -> 47ebb8, 47fa20
               -> 478b00 -> 4789b0, 479ab0 -> 49270c
               -> 478c64 -> 478bc4 -> 47ebb8, 47fa20
                          -> 478c1c -> 47ebb8
                          -> 49681a
          -> 47ebb8 (inline integer lookup trigonometry)
          -> 48043c (world-position transformation)
          -> 47f9a0 -> 47da4c, 47d32c (inverse-angle tables)
```

Analysis trap: linear disassembly past a return includes adjacent functions
and sometimes starts inside `ENTER` following padding/data. Each entry above
was decoded at its known address, following branches until returns. For
example, `47ebb8` ends at `47f054`; it does **not** call the nearby
inverse-angle function. `478e8c` ends at `478ea3`; the virtual call in the
next function at `478ed4` is not one of its callees.

## Function contracts and classification

The prologues identify entries; exact relocated signatures of the hooks are
in the adapters and checked by `check_signatures.py`. No row executes x87
instructions. `none` excludes ordinary code/stack reads.

| VA / prologue | ABI; arguments -> result | Globals R/W | Object R/W | Callees | Hook and evidence |
| --- | --- | --- | --- | --- | --- |
| 415afc / `55 8b ec 83 c4 f4` | cdecl A -> EAX callback result or 0 | none | R A+4d, definition+24..2f | dynamic definition callback | Not hooked: target/adjustment domain; C, active target H |
| 41a360 / `55 8b ec 83 c4 e8` | cdecl A -> AX=1, high EAX* | R 4a0be0,4a9cd0/d4/e4; W 4a9cd8/dc/e0; RNG via callees | R A+a3,1f2,22c,28e,2a0; C+6,12,16,6e; W A+26/2a/2e,93,ae,254,26e,28e,290,2a0 | root list above | `ground_update`, callees as game calls ([41a360](#ground-vehicle-update-41a360)); C + 1M; per-frame scheduling H |
| 418f40 / `55 8b ec 53` | cdecl A -> EAX* | none | R A+22c,2a0; descendant writes | 4796d8,467924,402604 | `ground_animate` ([418f40](#ground-animation-418f40-and-track-target-47967c)); C + 1M |
| 4796d8 / `55 8b ec 8b 45 08` | cdecl T,s16 rate -> EAX* | none | R/W T+4e; R +16/18/1a; descendant writes | 47967c on sign change | `track_rate`; C + 1M |
| 47967c / `55 8b ec 8b 45 08` | cdecl T,s16,s16,s16 -> EAX* | none | W T+16/18/1a,50,0c; R +4/6/1c | 4793d4 | `track_target` (4793d4 in core); C + 1M |
| 4793d4 / `55 8b ec 83 c4 e0` | cdecl T -> EAX* | none | R T+0 resource graph; R/W clip/transition words +4..1e and flags+50 | V20,V24,V28,V2c | `track_route` ([contract](#4793d4-track_route)); C + 1M; resource meanings H |
| 467924 / `55 8b ec 53 56` | cdecl s16 value -> signed EAX | R s16 4d3b64 | none | 47e0a0 | `movement_step`; C + 1M |
| 47e0a0 / `c8 00 00 00 8b 45 08` | cdecl s32,s32 -> low shifted product EAX; EDX scratch | none | none | leaf | `multiply_q8`; C + 1M |
| 402604 / `55 8b ec 8b 45 08` | cdecl A,s16 step -> EAX* | none | R A+39,+34 | 40250c if A+39 != 0 | `actor_animate` (calls the game's 40250c); C + 1M |
| 40250c / `55 8b ec 83 c4 c8` | cdecl A,M,s16 step -> EAX* | none | R A+c/e/10,32; M+16; W A+12..24,26/2a/2e,c/e/10,32; W `[M+16]` 32 bytes | 49681a,478b7c,478d38,47ebb8,48043c,47f9a0 | `animation_step` ([40250c](#animation-step-40250c-and-manager-reset-478b7c)); C + 1M |
| 478b7c / `55 8b ec 53` | cdecl M,R* -> EAX* | none | R M+1a,+16; W `[M+16]` 32; track changes via call | 479194 | `manager_reset`; C + 1M |
| 478d38 / `55 8b ec 53` | cdecl M,s16 step -> EAX* | none | M graph via callees | 478aac,478b00,478c64 | `track_advance` ([478d38](#track-advance-478d38)); C + 1M |
| 478aac / `55 8b ec 53` | cdecl M,s16 step -> EDX:EAX of 4790b4, or 0 with EDX=M+1a | none | R M+1a track list,+16; W descendants/list order | 4789b0,479720,4789d0,4790b4 | `track_list` ([contract](#track-hooks-479720-4789d0-478aac-and-478c64)); C + 1M |
| 4789b0 / `55 8b ec 8b 55 08` | cdecl M -> EAX count 0..10 | none | R M+1a..41 until first null | leaf | `track_count` ([contract](#p-interpolation-and-track-contracts)); C + 1M |
| 479720 / `55 8b ec 53` | cdecl T,s16 step -> EDX:EAX path-dependent | none | R/W T+4..1e,4e,50; resource clip graph; transform+2e | V20,V24,478ef4,478f6c,478ff4 | `track_step` ([contract](#track-hooks-479720-4789d0-478aac-and-478c64)); C + 1M |
| 4789d0 / `55 8b ec 51` | cdecl M -> EDX:EAX of 4789b0 (n<2) or of 478e8c for the last track | none | R/W M+1a track pointer order | 4789b0,478e8c | `track_sort` (stable bubble sort by clip priority, 478e8c in core); C + 1M |
| 478e8c / `55 8b ec 8b 45 08` | cdecl T -> AX clip word; high EAX from clip pointer | none | R T+0,+4; resource+6 list; selected clip+6 | leaf | Computed inside `track_sort`/`track_advance`; not hooked alone (only caller 4789d0); C |
| 478ef4 / `55 8b ec 53` | cdecl T,s32 clip,s32 frame -> EAX=T | none | R resource pointers+6/+12, clip+8/+10/+16; W T+20,22..2d | leaf | `clip_pose` ([contract](#p-interpolation-and-track-contracts)); C + 1M |
| 478f6c / `55 8b ec 83 c4 b4` | cdecl T -> EAX* | none | R T+20,22..2d,2e..4d; W T+2e..4d | 47ebb8,47fa20 | `track_forward`, [padding contract](#padding-contract-for-the-five-wrappers); 1M under 31/a7 stack poison |
| 478ff4 / `55 8b ec 83 c4 a8` | cdecl T -> EAX* | none | R T+20,22..4d; W +2e..4d | 47ebb8,47df19,480304,47fa20 | `track_inverse`, padding contract; 1M under 31/a7 stack poison |
| 4790b4 / `55 8b ec 83 c4 cc` | cdecl T,R output -> EAX* | none | R T+1c/1e/20,22..4d; W output 32 | 47ebb8,47fa20 | `track_scaled`, padding contract; 1M under 31/a7 stack poison |
| 479194 / `55 8b ec 83 c4 c0` | cdecl T,R input -> EAX* | none | R T+1c/1e/20,22..2d,input 32; W T+2e..4d | 47ebb8,47df19,480304,47fa20 | `track_scaled_inverse`, padding contract; 1M under 31/a7 stack poison |
| 478b00 / `55 8b ec 53` | cdecl M -> EAX* | none | R M+1a list,+12 pose array,+0e flags; W arrays via callee | 4789b0,479ab0 | `pose_list` ([contract](#478b00-pose_list)); C + 1M |
| 479ab0 / `55 8b ec 83 c4 e8` | cdecl T,pose-array,flags -> EAX* | none | R T+0,4/6/8/a/1c/1e; resource clip/pose lists; W selected poses12 and flags bytes | 49270c | `track_poses` ([contract](#479ab0-track_poses)); C + 1M |
| 49270c / `c8 00 00 00 53` | cdecl pose0,pose1,u32 fraction,pose-out -> EAX=out | none | R inputs 12; W output 12 sequentially | leaf | `pose_interpolate`; C + 1M |
| 478c64 / `55 8b ec 51` | cdecl M -> EAX=flags, EDX=s16 M+c (memset) | none | R M+4->+2a hierarchy,+c count,+e flags,+12 poses,+16 matrices; W matrices/flags | 478bc4,478c1c,49681a | `node_matrices` (memset result reproduced; callers 478838, 478d38); C + 1M |
| 478bc4 / `55 8b ec 83 c4 d4` | cdecl pose12,parent-R,out-R -> EAX* | none | R pose angles+0..5 and translation+6..b,parent32; W output 32 | 47ebb8,47fa20 | `pose_parent`, padding contract; 1M under 31/a7 stack poison |
| 478c1c / `55 8b ec 83 c4 f4` | cdecl pose12,out-R -> EAX=out | none | R pose12; W out+0..12 and +14/+18/+1c | 47ebb8 | `pose_matrix`; C + 1M |
| 47ebb8 / `c8 00 00 00 57` | cdecl angles3*u16,R-out -> EAX=out | R u16 table 4a25c4 | R angles6; W R coefficients+0..11 and mode+12; translation unchanged | leaf, inline lookup | `euler_matrix`; C + 1M |
| 47df19 / `c8 00 00 00 8b 45 08` | cdecl R -> EAX=R | none | swaps words R+2/4,+8/c,+a/e | leaf | `matrix_transpose`; C + 1M |
| 480304 / `c8 00 00 00 56` | cdecl XYZ,R,out-XYZ -> EAX=out or out+12 by mode | none | R XYZ12,R coefficients/mode; W output 12 | leaf | `rotate_point`; C + 1M |
| 47fa20 / `c8 00 00 00 56` | cdecl R0,R1,out-R -> EAX=out | none | R matrices/modes; W composed output 32 | leaf | `matrix_compose`; C + 1M |
| 48043c / `c8 00 00 00 56` | cdecl XYZ,R,out -> EAX=out if signed mode>=1, else output Z | none | R XYZ12,R32; W output 12 X,Y,Z | leaf | `transform_point`; C + 1M |
| 47f9a0 / `c8 00 00 00 57` | cdecl R,angles-out -> EAX=out | none | R coefficients; W three angle words | 47da4c,47d32c | `matrix_angles`; C + 1M |
| 47da4c / `c8 00 00 00 0f bf 45 08` | cdecl s16 -> EAX angle bits (branch-dependent upper bits) | R tables 4a05bc,4a1e3c | none | leaf | `inverse_sine`; C + all 65,536 inputs |
| 47d32c / `c8 00 00 00 56` | cdecl s32,s32 -> EAX angle &ffff | R correction bytes 4a1c34 | none | leaf | `angle_ratio`; C + 1M |
| 49681a / `ff 25 58 c3 6c 00` | cdecl dst,int,size -> EAX dst | R IAT 6cc358 | W dst[size] | CW3220!_memset | Not hooked here: external runtime boundary (the own `cw3220.dll` replaces the DLL); C |

The contracts of the matrix and pose rows are in
[Matrix, pose and inverse-trig layer](#matrix-pose-and-inverse-trig-layer).

### Callback side branches

| VA / prologue | ABI / result | Direct state and callees | Hook and evidence |
| --- | --- | --- | --- |
| 402628 / `55 8b ec 83 c4 f0` | cdecl A / EAX* | R A+c..34 and M's ten tracks/arrays; W A rotation cache and globals 4a8cd0(32),4a8cf0(10*82),4a9024 flags,4a9042 poses,4a91aa matrices; calls 47ebb8,49680e memcpy | `actor_snapshot` ([402628](#snapshot-402628-and-rollback-4027fc)); C + 1M |
| 4027fc / `55 8b ec 83 c4 f0` | cdecl A / EAX* | reverses snapshot into A/M; reads same snapshot globals; calls 47ebb8,49680e | `actor_rollback`; C + 1M |
| 46e180 / `55 8b ec 83 c4 b0` | cdecl terrain*,XYZ* / signed EAX height | no direct globals/callees; terrain tiles/grid pointers through first argument | `terrain_height` (`core/terrain`, [46e180](#terrain-height-46e180)); C + 1M |
| 418f74 / `55 8b ec 81 c4 38 ff ff ff` | cdecl A / AX collision flag | R scene 4a0be0,4a9af8/afe,4a9e5c,6c56a8,49a058; R/W 4a9cd0/d4/e0/e4; RNG 4d2599; actor and scene graph; calls 49288c,47ebb8,47df19,480304,47e0b0,404ae4,46e498,47de4c,492f24,434010,462830 and virtuals+20/5c/68/70/74/7c | `ground_collide` ([418f74](#ground-collision-418f74)); virtuals and game callees called at their addresses |
| 4792ac / `55 8b ec 53` | cdecl T,clip,frame,phase (low words); entry EAX high word / EDX:EAX from 478ef4 | T words+4/6/8/a/c/16/18/1a/1c/1e/50 and pose+20..2d; calls clip V20,478ef4 | `track_clip` ([4792ac](#track-clip-selection-4792ac)); compare refused |
| 4195c8 / `55 8b ec 83 c4 a0` | cdecl A / full EDX:EAX, including entry bits | R 4d24ea,6c56a8 and actor/scene graph; W actor cache/latches and attached transforms; calls 47ebb8,48043c,47de72,4628e0,434144,virtual+20 | `ground_effects` ([4195c8](#ground-effects-4195c8)); compare refused |
| 462830 / `55 8b ec 53` | cdecl s32 sound-id / EAX* | R 49f8ec,4d2b78/4d2c78 indexed tables; RNG 4d260b via 492f24; calls 462784,47e08d,464618,464038 | `sfx_play` ([462830](#sound-selection-462830-and-the-isolation-boundary)); no clock read in the call closure |
| 492ee0 / `55 8b ec 8b 45 08` | cdecl RNG* / EAX draw | R/W 114-byte passed RNG, no direct globals; leaf | `rng_next` ([rng.md](rng.md)); 1M in vitro |

## Movement step

The position is **not** a constant speed-times-elapsed displacement. The
ground callback first snapshots actor transform and animation state. It calls
`418f40`, which sets track `[A+22c]`'s signed rate from `A+2a0`, computes
`clamp(floor(s16(scale)*100/256),-32767,32767)` using scale at `4d3b64`, and
passes its low word to `402604`. A zero byte at A+39 suppresses integration.
The command at A+290 and HUD scalar at A+28e are not directly multiplied into
XYZ here; their relation to animation rate belongs to the surrounding actor
controller. Calling A+2a0 a physical velocity is H.

`40250c` creates a zeroed 32-byte transform with Q14 diagonal 16384. It
initializes the manager transform, then advances all tracks. `479720` starts
with signed low-word step times signed track+4e, arithmetic shift by eight,
and tests the resulting **AX**. Track phase/frame transitions, track ordering,
pose interpolation and hierarchy composition produce `[M+16]`'s transform.
They can cross frames and call clip virtual methods, so no single linear
displacement formula describes the result; `core/animation` reproduces the
manager step by step ([Track advance 478d38](#track-advance-478d38)).

If A+32 is zero, `47ebb8(A+c,A+12)` builds the actor's cached rotation and
sets A+32=1. Its translation already occupies A+26/2a/2e. The animation
translation `[M+16]+14/+18/+1c` is transformed by this actor matrix.
`48043c` writes its local result **X,Y,Z**. `40250c` then writes the actor
position **Z at 402596, X at 40259c, Y at 4025a1**. It converts the animation
matrix back to three angles, adds the existing actor angles modulo 65536,
writes the first two words as one dword then heading, and clears A+32.
Finally it zeroes the animation transform and restores the three Q14 diagonal
words; this consumes the accumulated animation delta.

`48043c` (`transform_point`): for signed matrix mode <1, transformation is
three wrapping additions. For mode=1, X/Y use two signed products and Z is an
addition. Higher signed modes use three products. Each dot product has
signed32 coordinates and signed16 Q14 coefficients, accumulates in 64 bits,
adds 8192, shifts right 14, retains low32, then adds translation modulo 2^32.
Coefficient offsets for X/Y/Z are `[0,4,c]`, `[2,6,e]`, `[8,a,10]`. Inputs
are reread between output writes, so aliasing can change later components.
Pure translation returns the Z bits; the other branches return the output
address.

The ground callback subsequently adds optional XY collision offsets from
4a9cd0/d4 when A+a3 and 4a9ce4 are nonzero. It queries terrain height at the
new XY, adds signed config+16, and again writes actor **Z,X,Y**. Collision
may restore the snapshot, clear command, reverse animation rate and bounded
scalar speed, retry movement, and roll back a second time. Terrain and
collision are therefore part of the world-position result
([41a360](#ground-vehicle-update-41a360)).

## Heading and HUD

There is no `fsin`/`fcos` or floating polynomial in the movement rotation.
`47ebb8` performs table lookup inline; `47dff0` and `47e027` expose scalar
cos/sin using the same quarter table at 4a25c4 (1025 little-endian words).
Decode heading as signed16; round its /16 index using arithmetic `SAR 4`
followed by carry-add (bit 3), take its absolute value, and reflect at index
1024. The reflected branch negates the zero-extended table value. Sin first
subtracts 16384 and sign-extends the wrapped low word. The planar Euler branch
puts cos at +0/+6, sin at +2 and negative sin at +4. Thus a positive local
Y step points along `(-sin(heading),cos(heading))`, consistent with the
separately observed calibration.

`41bb3c(A)` has two config branches. With config+50 zero it returns
`trunc(s16(A+28e)*s16(C+c2)/s16(C+6))` (C+6 is the configuration file's
forward speed +4, nonzero in every shipped herc; a zero one faults first in
the cockpit sync 41b130 at 41b26b, scenario `forward-speed-zero`).
Otherwise it calls `47df48` with
`(s32(A+2bd),0,s32(C+206),0,s16(C+c2))`. Let `v`, `d`, `m` be those three
nonzero-branch values. Let `b` be signed32 interpretation of
`wrap32(abs_wrapped32(d)-1)`, negated in 64 bits when m<0. The result is
`trunc((v*m+b)/d)`, with the original signed-IDIV valid-quotient domain.
This is not the ground speed word formula. Zero divisor and quotient overflow
are original faults, outside the portable and detour contracts. The million
HUD cases cover both branches, both signs, full-width v, and denominators
whose magnitude avoids quotient overflow; they do not establish which
non-ground vehicle types use the second branch in a live mission.

| Leaf | Prologue; ABI; result | State / callees / hook |
| --- | --- | --- |
| 47dff0 heading_cos | `c8 00 00 00 0f bf 45 08`; cdecl s16 -> EAX | table 4a25c4 RO; no writes/calls; hook, exhaustive |
| 47e027 heading_sin | same prefix; cdecl s16 -> EAX | same; hook, exhaustive |
| 41bb3c hud_speed | `55 8b ec 53 8b 5d 08`; cdecl A -> EAX | A+1f2/28e/2bd, config+6/50/c2/206 RO; call 47df48; hook, 1M |
| 47df48 interpolation | `c8 00 00 00 53`; cdecl five s32 -> EAX quotient+y0, EDX remainder | `scalar_interpolate` ([IDIV preconditions](#p-interpolation-and-track-contracts)); C + 1M |
| 47d694 distance_display | `55 8b ec 83 c4 f8`; cdecl XY*,XY* -> EAX | inputs 8 RO; calls 492a30,492a45; no globals; hook, 1M |
| 492a30 vector_subtract | `55 8b ec 8b 55 0c`; cdecl dstXY,srcXY -> EAX=dst | writes X then Y modulo 2^32; sequential reads preserve aliases; no globals/calls; hook, 1M |
| 492a45 distance vector | `55 8b ec 53 8b 5d 08`; cdecl XY* -> EAX | inputs 8 RO; calls 492a9a; no globals; hook, 1M |
| 492a9a distance delta | `55 8b ec 8b 45 0c`; cdecl s32,s32 -> EAX | calls 47de4c; no globals/writes; hook, 1M |
| 47de4c octagonal | `c8 00 00 00 8b 45 08`; cdecl s32,s32 -> EAX | wrapped absolute, signed ordering/SAR; no globals/writes/calls; hook, 1M |

## FPU startup and calling conventions

DBSIM entry 40105d calls thunk 496898 -> IAT 6cc32c -> CW3220 `__startup`.
In the hash-verified DLL, preferred-base VA 41a6f7 calls `__fpreset` at 413aac;
it executes FNINIT and `_control87(default,0x1fff)`. The default dword at
DLL VA 421dc8 is **0x1332**. Our fixtures load that value; hardware saves it
as **0x1372**, with reserved bit 6 set. The initialized value is confirmed;
its persistence through other DLL calls and a live simulation tick is H
([open question](#open-live-questions)). The apparent FLDCW instructions in
linear DBSIM disassembly at 43a99d/446b9d are jump-table data, not evidence
that movement changes precision.

Every hook and every resolved integration function in this document uses
explicit stack arguments and caller cleanup. Borland's compiler identity
alone does not make them register-call functions. The framework's opt-in
bridge implements EAX/EDX/ECX with an optional fourth stack argument and
callee cleanup of that slot; stdcall cleans all argument slots. Those two
bridge contracts are synthetically verified, not claims that an unresolved
game function uses them. No hidden `this`, aggregate or floating arguments
are supported.

## Open live questions

- Ground callback count and phase relative to frame completion (`0x45f4ad`).
- The x87 control word at mission ticks: record FNSTCW at the tick boundary.
- A live nonzero config+50 HUD branch, and the HUD quotient bound of
  [47df48](#p-interpolation-and-track-contracts) (the divisor FM+2a is
  nonzero in the shipped flight models).
- Enabled track duration != 0 on every live route to the scaled wrappers
  with a modified DTS (H, [P contracts](#p-interpolation-and-track-contracts));
  the shipped files hold no zero duration.
- Full-width `angle_ratio` callers excluding INT_MIN with y != 0
  ([T contracts](#t-inverse-lookup-contracts)).
- The physical meaning of the energy fields
  ([41aa5c](#energy-accumulator-and-countdowns-41aa5c)).
- Stack headroom in the mission thread
  ([Leaf chain test and stack headroom](#leaf-chain-test-and-stack-headroom)).
- Section-specific open items stay in their sections (effects semantics,
  collision receiver classes, sound isolation, damage-path hypotheses).

## Energy accumulator and countdowns 41aa5c

Hooks `energy_accumulate` (413b38), `multiply_q10` (47e0b0) and
`approach_word` (467adc). The actor update at `41aa5c` is called at `45f8ac`
in a list iteration over global 4a9b7a (not only the active player). It
starts by adding `movement_step(s16(A+256))` to word A+292, passes wrapped
`A+292-500`, A and `[A+202]` to that object's virtual slot0, then calls
`413b38(A+222, lowword(virtual result))`. It adds 500 to the returned AX and
clamps the signed word into 0..10000. The HUD path at 41b130 normalizes
A+292 to Q10 by signed multiply 1024/divide 10000 and writes global 4a9ce8.
This establishes an accumulator/controller/display relationship; identifying
it as the energy reserve and the substructure as heat allocation is H.

```text
45f8ac -> 41aa5c(A)
             -> 467924 -> 47e0a0                (movement_step)
             -> [A+202].vtable[0]              (unresolved consumer)
             -> 413b38(A+222, s16 request)
                    -> 47e0b0                 (Q10 multiply)
                    -> 467adc                 (signed-word approach)
             -> 467aa8(A+272/277/27c), 467a48(A+26a/285/...)
```

`413b38` wraps the sum of words S+0/S+2, computes wrapped capacity gap
S+6-sum, and chooses signed min(request,5,gap). It adds this delta to the
sum, multiplies by signed word S+4 in Q10, then calls 467adc on S+0 with step
10000 when the gap was negative, otherwise 65. It writes wrapped(new-sum
minus updated S+0) to S+2 and returns AX=request-delta; upper EAX is
path-dependent ([E contracts](#e-energy-register-and-write-contracts)). The
caller uses only AX. `467adc` moves the pointed signed word towards a target
by a signed low-word step, with wrap before its signed comparisons. Its
nonzero return has pointer/high-word quirks and must not be assumed to be a
clean signed16 result. Neither helper reads a clock itself.

| VA / prologue | ABI / state | Callees and hook |
| --- | --- | --- |
| 41aa5c / `55 8b ec 81 c4 74 ff ff ff` | cdecl A -> EAX*; R A+256,292,202,222..229; W 292 and descendants | 467924, virtual0, 413b38, 467aa8, 467a48; the rest of the controller includes target/AI/UI; C slice, subsystem identity H; this slice is not hooked (virtual consumer and broad actor state) |
| 413b38 / `55 8b ec 83 c4 f4` | cdecl S*,s16 request -> AX remainder/high EAX*; R S+0,2,4,6; W S+0,2; no globals | `energy_accumulate`; C + 1M |
| 47e0b0 / `c8 00 00 00 8b 45 08` | cdecl s32,s32 -> low32 of signed64 product shifted 10; no state | `multiply_q10`; C + 1M |
| 467adc / `55 8b ec 8b 45 08` | cdecl s16*,s16 target,s16 step -> EAX mixed high bits; R/W two-byte state, no globals | `approach_word`; C + 1M |
| 467aa8 / `55 8b ec 8b 45 08` | cdecl timer* -> EAX dword; R scale 4d3b64; W timer+1..4 | `timer_dword_step` |
| 467a48 / `55 8b ec 8b 45 08` | cdecl timer* -> AX word, pointer-derived high EAX; R scale 4d3b64; W timer+1..2 | `timer_word_step` |

At 41ab29..41ab9e the actor's three dword countdowns and two word countdowns
advance. Expiry at A+27c clears dword A+281; expiry at A+285 resets its word
payload to 4000 and clears dword A+288. These are confirmed countdown
effects, not established weapon cooldown names. Distinguishing energy/heat
semantics needs A+222..229, A+256, A+292, the virtual consumer target/return
and the HUD bar recorded while idle, firing, changing power allocation and
recovering.

## Matrix, pose and inverse-trig layer

Twenty entries, each a hook: the fourteen leaves `euler_matrix`,
`matrix_compose`, `rotate_point`, `matrix_transpose`, `pose_matrix`,
`pose_interpolate`, `scalar_interpolate`, `track_count`, `inverse_sine`,
`angle_ratio`, `matrix_angles`, `multiply_q10`, `approach_word`,
`energy_accumulate`; the five padding wrappers `pose_parent`,
`track_forward`, `track_inverse`, `track_scaled`, `track_scaled_inverse`
([padding contract](#padding-contract-for-the-five-wrappers)); and
`clip_pose`, whose resource extents are unproved and which therefore replays
the original's memory accesses. **C** below means independently decoded from
the hash-pinned DBSIM bytes, with execution evidence where stated; **H** a
game-domain assumption, not an established precondition of real missions.
All twenty are cdecl, with four-byte argument slots, caller cleanup, no x87
and no SSE. None requires six arguments: scalar interpolation needs five,
pose interpolation four, all others at most three. The bridge nevertheless
tests both five and six in each supported convention.

The fourteen leaves have 1,000,000 direct cases each, including exact full
EDX:EAX and whole synthetic arenas; inverse sine additionally covers every
16-bit input. The private-image harness counts both outcomes at every Jcc
site of the hooked bodies and fails on an uncovered outcome. `branch_sites.h`
records instruction VA, length, condition and destination, not instruction
bytes. Tables are borrowed from the image; none was exported. The script
outputs of the original derivation (contract probes, the per-register caller
audit, the second derivation) were not kept; the in-vitro cases reproduce the
arithmetic claims.

### Shared representation and arithmetic

A matrix is 32 bytes: signed Q14 words at 0,2,4,6,8,a,c,e,10; signed mode
byte at 12; padding byte 13; signed32 translation at 14,18,1c. In mathematical
column order, the coefficient offsets are `(0,2,8)`, `(4,6,a)`, `(c,e,10)`.
Mode byte values 80..ff and 00 form the nonrotating class; 01 is planar;
02..7f is full rotation. Euler emits 0,1,2,3, while composition emits 0/1/2
or copies a source's mode. A pose is six LE words: three angles, then XYZ
signed translations. Q14 rounding adds 8192 before arithmetic shift 14;
negative ties therefore round toward positive infinity. Every narrowed store
wraps, and every low32 product/add explicitly wraps before a later SAR.
These functions permit sequential alias effects: they do not first snapshot
all inputs. The portable implementations use explicit LE reads and writes.
Direct tests include identical and partially overlapping arguments. Installed
compare mode conservatively skips overlapping declared spans.

### M: rotation and composition contracts

**47ebb8, `euler_matrix(angles,out)` (C).** Let S/C be the heading
sine/cosine lookup, including its signed-word angle wrap, bit 3 rounding,
folding and image table; the core reuses those APIs. Inspect
47ebcb/47ebd8/47ebe5: mode is 3 if angle0!=0, else 2 if angle1!=0, else 1 if
angle2!=0, else 0. Mode is written first. Mode0 writes diagonal words 0,6,10
as 16384, then zeros 2,8,4,a,c,e. Mode1 computes C(angle2), writes 0 then 6,
rereads angle2 for S, writes 2 and its negative at 4, zeros 8,a,c,e, then
writes diagonal 10. Angle reads can therefore observe earlier aliased writes.
Mode2/3 share the full path. With half()=SAR32(value,1) and
Q(u,v)=SAR32(low32(u*v)+8192,14), the ordered stores are:

1. a=S(angle0).
2. 0=half(C(angle1-angle2)+C(angle1+angle2)) minus
   Q(S(angle0),half(C(angle1-angle2)-C(angle1+angle2))).
3. e=the same half-difference minus Q(S(angle0),the same half-sum).
4. 2=Q(S(angle0),half(S(angle1+angle2)-S(angle2-angle1))) plus
   half(S(angle1+angle2)+S(angle2-angle1)).
5. c=Q(S(angle0),that half-sum) plus that half-difference.
6. 6=half(C(angle0-angle2)+C(angle0+angle2));
   10=half(C(angle0-angle1)+C(angle0+angle1)).
7. 8=half(negate32(S(angle1-angle0)+S(angle1+angle0))).
8. 4=negate32(half(S(angle2-angle0)+S(angle2+angle0))).

S(angle0) and the paired half-sums/differences are retained within each
stage; angle words are reread for later pairs. Negating before versus after
the last two shifts matters. Padding and translations remain untouched.
EAX=out on every path. EDX is incoming EDX for mode0; otherwise it is the
sign mask of the last sine's rounded signed angle before absolute value:
mode1 uses s16(angle2-16384)<-8; full uses
s16(angle2+angle0-16384)<-8, evaluated at that final lookup. Each is 0 or
ffffffff. The fifteen lookup-fold branches are measured as well as modes.

**47fa20, `matrix_compose(A,B,out)` (C).** Both initial mode bytes are
sampled before output stores. At 47fa34, A.mode<1 copies B's first 20
decimal bytes in five forward DWORD operations; it still computes the full
three-term translation described below. Otherwise B.mode<1 at 47fa3e writes
translations first as wrapped A+B, in XYZ order, then copies A's first 20
bytes; EDX is unchanged on this path. For positive modes, each coefficient
dot product is signed16*signed16, accumulated modulo 32 with 8192, SAR14 then
narrowed to a word. Both planar modes write 0,2,4,6 with two terms, zeros
8,a,c,e, diagonal 10=16384, mode1. A planar/B full writes 0,2,8,4,6,a using
two terms, copies B's c,e,10 sequentially, then mode2. A full/B planar writes
0,2,4,6,c,e using two terms, copies A's 8,a,10, then mode2. Both full writes
0,2,8,4,6,a,c,e,10 with three terms, then mode2. Except copy paths, padding
remains the previous output byte.

All but the B.mode<1 path then write XYZ translations sequentially:
low32((sum of three signed32 A coordinates * signed16 B coefficients
+8192) arithmetic-shifted 14) + the corresponding B coordinate, wrapping 32.
The accumulation fits signed64. This full dot product happens even for
nonrotating matrices; it cannot be replaced with mode-dispatched rotate.
EAX=out; EDX is the upper32 bits of the last biased Z sum, or incoming EDX
on the additive path. In-place output can alter subsequent coefficient or
translation reads; forward copies are not memmove. All nine mode-class pairs
and aliases are generated.

**480304, `rotate_point(point,matrix,out)` (C).** Mode<1 copies three
DWORDs forward and returns EAX=out+12, EDX unchanged. Mode1 computes X then Y
from two signed32*signed16 terms, adds 8192 in signed64, shifts 14, stores
low32, then copies Z. Full mode does the same with three terms for X,Y,Z. No
translation is added. EAX=out for both rotating modes; EDX is the upper half
of the final biased Y or Z product sum. Point and matrix are reread between
sequential stores, including partial aliases.

**47df19, `matrix_transpose(matrix)` (C).** Swaps word pairs (2,4), (8,c),
(a,e), in that order, storing the second position before the first in each
pair. All other bytes remain unchanged. EAX=matrix and EDX is incoming EDX.
There is no conditional branch or division.

**478c1c, `pose_matrix(pose,out)` (C).** At 478c2c..478c3d, read and
sign-extend pose translations Z,X,Y completely before any store. Write
out+1c,+14,+18 in Z,X,Y order, then run Euler on the live pose angle address
(478c56). Thus translation/angle overlap may change Euler inputs. EAX=out;
EDX=Euler's return, with incoming EDX=pose address for mode0. Padding remains
unchanged. The wrapper itself has no conditional branch.

**478bc4, `pose_parent(pose,parent,out)` (C; padding contract).** Pre-read
the same Z,X,Y translations into a local matrix, call Euler 478bfa, then
compose local,parent,out at 478c0e. EAX=out; EDX follows composition, whose
entry EDX is parent on its additive path. Inputs aliasing output are read
according to that two-stage flow. Local matrix byte 13 is never initialized.
A positive local mode with nonrotating parent copies that stack byte into
out+13; other positive/positive paths retain the previous out+13. Bridge
stack patterns 31/a7 change **only output byte 19 decimal** with identical
arguments. No division occurs.

### P: interpolation and track contracts

**49270c, `pose_interpolate(a,b,fraction,out)` (C).** Six word stores occur
in increasing offset order, with live rereads of a/b. For each of the first
three angles, if target<=source add 65536 to target; then subtract source;
if this nonnegative difference>=32768 subtract 65536. The exact half-turn tie
is therefore -32768. Multiply by the unsigned32 fraction, retain low32,
SAR10, add source and store low16. For translations, subtract in 16 bits,
sign-extend that wrapped difference, multiply by the signed32 interpretation
of fraction, keep low32, SAR10, add source/store 16. For low32 results both
paths equal signed16(target-source)*fraction. EAX=out; EDX=upper32 of the
last translation's signed64 product. The angular MUL high halves are
overwritten before return. Fractions outside 0..1024 are legal machine
inputs and explicitly tested, without saturation.

**47df48, `scalar_interpolate(x,x0,x1,y0,y1)` (C).** D is
signed32(wrap32(x1-x0)); dY=signed32(wrap32(y1-y0)). The bias magnitude is
signed32(abs_wrapped32(D)-1). Negate the signed64 bias when signed y1<y0:
the JGE at 47df67 follows SUB and includes OF, so testing dY's wrapped sign
would be wrong. N=signed32(wrap32(x-x0))*dY+bias, computed signed64. IDIV at
47df7a truncates N/D toward zero. EAX=wrap32(quotient+y0), EDX=the signed
remainder's 32-bit representation. No state writes. Preconditions: D!=0 and
quotient in [-2147483648,2147483647]. These are exactly its #DE cases, not
replacement emulation. Probes cover each legal endpoint, zero D, and
positive/negative quotient overflow. The 41f37d caller supplies signed16 x,
x0=0,x1=1024,y0=150,y1=-100: always legal. At 41bb71 HUD supplies signed16
endpoint y1, y0=x0=0, but full32 config+206 and actor+2bd; it does not check
the two preconditions. config+206 is the flight model's word FM+2a (the
configuration holds the FM at +1dc): 1500 in RAZOR.FM, 1000 in SKIMMER.FM, so
D!=0 with the shipped files. A flight model with FM+2a=0 reaches the #DE at
47df7a when the panel draws (own_process.md, scenario `hud-speed-zero`). The
quotient bound for live state is H.

**4789b0, `track_count(manager)` (C).** Read DWORDs at manager+1a+4*i in
increasing order, stop on first zero or i=10. No write. EAX=i,
EDX=manager+1a+4*i, including the one-past address at count 10. All eleven
termination positions are tested. The descriptor conservatively covers the
66-byte prefix, though execution stops reading at the first null.

**478ef4, `clip_pose(T,clip,frame)` (C; bytes 478ef4-478f6b).** Load
resource=[T], list=[resource+6], selected=[list+4*clip]. If selected's word
8==0, store T+20=0 and return EAX=T, EDX=the original full32 clip argument.
Otherwise stride=s16(selected+10), index=wrap32(frame*stride),
poseIndex=s16 word at [selected+16]+2*index. Address arithmetic wraps 32.
Read pose base from [resource+12], copy source(12*poseIndex)+6 as DWORD to
T+22, then source+a as WORD to T+26. Reread [T] and its pose base, copy the
angle DWORD to T+28 then angle WORD to T+2c. Store T+20=1 last. EAX=T,
EDX=wrap32(3*poseIndex). The redundant resource+6 reads at 478f16 and 478f26
also occur and require readable memory. Aliases may alter later
pose-base/angle reads. No divide. Pose records are 12 bytes: angles +0..5,
translation +6..b.

All four direct callers (47931a, 4797f7, 479920, 479a3e) pass
`clip = s16 T+4` and `frame = s16 T+6` sign-extended, and each has already
dereferenced `list[clip]` itself before the call. The bounds of
clip/frame/pose counts are unproved, but exact replacement does not need
them: `core/pose` `es2_clip_pose_registers` performs the same reads and
writes in the same order with the same 32-bit wrapping address arithmetic
through the guest-memory interface (`es2_guest_memory`,
`core/include/es2/core/guest.h`), so it touches exactly the addresses the
original touches and faults where the original faults. The two discarded
resource+6 reloads are not repeated (no effect on readable memory). Compare
mode only snapshots T+0..2d (span `{0,0,0x2e}`): the graph behind the
unchanged pointer at T+0 is read-only for this function, so both phases read
the same graph. Limit: if the graph pointed back into T (pose record or
resource+12 overlapping T+22..2d), the shallow snapshot cannot isolate it;
replaced mode reproduces even that aliasing exactly (in-vitro modes 3/4). In
vitro 1,000,000 direct cases in five modes (disabled clip, full-range 32-bit
clip/frame/stride/pose with wrapped base pointers, small realistic values,
pose record over T, resource+12 at T+22); branch 478f0c both outcomes. Live:
identical regression with the hook replaced
(`<es2-data>\logs\oracle-check-20261006T151007Z`).

**478f6c, `track_forward(T)` (C; padding contract).** T+20==0 leaves both
incoming EAX/EDX unchanged and writes nothing. Otherwise build a local
matrix from signed translations T+22/24/26 and angles T+28/2a/2c, Euler
478fad, compose local,T+2e into a second local matrix at 478fc3. Copy its
first 20 bytes forward to T+2e, then copy translation Z,X,Y to T+4a,+42,+46.
EAX=T+42, EDX=T+2e. No divide. The copied byte T+41 is unwritten local
padding on reachable paths; both stack patterns reproduce the defect
independently.

**478ff4, `track_inverse(T)` (C; padding contract).** Same disabled branch
and copy/return order. Enabled: construct local pose matrix, transpose
rotation, negate the translations modulo 32 in Z,X,Y order; rotate those
negatives by the transposed matrix into its translation; compose with T+2e,
copy back. The temporary vector ensures the intended input to rotate; its
output matrix translations are separate from its coefficients. EAX=T+42,
EDX=T+2e. Padding is likewise stack-dependent; no divide.

**4790b4, `track_scaled(T,out)` (C; padding contract).** Disabled T+20==0
copies T+2e's first 20 bytes forward to out, then reads/stores translation
Z,X,Y. EAX=out+14, EDX=the last source Y DWORD. Enabled: compute each of six
pose words*s16(T+1c)/s16(T+1e) using signed32 product and IDIV, truncation
to zero; angles narrow 16, translations retain 32. Translation calculations
precede Euler on the three scaled angle words, then compose local,T+2e,out.
EAX=out; EDX=composition result (entry EDX=T+2e on its additive path). The
local padding byte is not initialized. Probes confirm only byte 13 changes
under stack poison. Aliases follow the disabled copy order or enabled
local-build then composition order.

**479194, `track_scaled_inverse(T,input)` (C; padding contract).**
Disabled: copy input's first 20 bytes forward to T+2e, then translation
Z,X,Y; EAX=T+42, EDX=T+2e. Enabled: scale the six words exactly as 4790b4,
Euler, transpose, negate translations, rotate them by the transposed matrix,
then compose local,input,T+2e. EAX=T+2e; EDX=composition (incoming value at
that call is input). Local padding propagates for a nonrotating input; stack
poison changes only T+41 in the test.

For both scaled entries, IDIV faults exactly when enabled and s16(T+1e)==0
(first instructions 4790da/4791bd). The product of two signed16s is in
[-1073709056,1073741824], so any nonzero signed16 divisor has a fitting
signed32 quotient, even -1 with both operands -32768. Probes confirm disabled
zero-denominator bypass, enabled #DE and that worst legal product. 479720
assigns duration from frame-record words at 4797df/e3, 4798fb/ff, 479a26/2a
or transition word T+12 at 479873/4799a7. Its zero-duration branch at 479903
does not prove that enabled T+20 is cleared on every route to the scaled
functions. The shipped resources hold no zero duration: the 152 ANAnimLists
in SIMVOL0.VOL have 2,446 frame durations of 50 to 100 and 989 transition
durations of 100. **H:** no route builds a zero duration at run time. That
is not used as a reason to manufacture a nonfaulting result.

### T: inverse lookup contracts

**47da4c, `inverse_sine(s16 value)` (C).** EDX=sign mask of the signed16
input. Let i=floor((abs(value)+2)/4). If i<=3583 (47da64), j=floor((i+4)/8);
read u16 at 4a1e3c+2*j and apply sign in 32 bits. Otherwise read u16 at
4a05bc+2*i and apply sign in **16 bits only**, leaving EAX's upper16 zero.
This is significant for negative high-magnitude inputs. Every 16-bit input is
readable in this pinned image: small j=0..448, large i=3584..8192. Values
beyond geometric Q14 range can read adjacent image data; the machine contract
preserves that behavior without naming it a valid mathematical asin. No
divide or writes. RO region 4a1c34..4a45bd covers every actual lookup plus
the ratio corrections used by matrix_angles.

**47d32c, `angle_ratio(x,y)` (C).** Wrapped abs32 is used, so INT_MIN
remains 80000000. If absY==0, skip division, initial angle=0, EDX=0.
Otherwise signed-compare absY>absX: larger becomes divisor and smaller the
numerator source. Build signed64(smaller)<<9 as a bit pattern, then add
SAR32(divisor,1) to the low32 **without carrying into the high32**. Unsigned
DIV at 47d35d/47d381 yields i. For i!=0, angle=4*i+u8[4a1c34+i], EDX=that
correction byte. For i==0 angle=0 and EDX retains DIV remainder. If
exchanged, angle=4096-angle; if original x<0, angle=8192-angle; if original
y<0, angle=16384-angle. EAX=(angle<<2)&ffff. No writes. Outside INT_MIN the
index is 0..512. Exactly the cases y!=0 and either argument INT_MIN fault
(#DE, divisor 0 or unsigned quotient overflow); x=INT_MIN,y=0 is legal and
returns 32768. Core exposes a domain predicate.

Caller widths: matrix_angles' three sites 47f9d9/47f9f3/47fa0b sign-extend
words (one may be negated), so INT_MIN is impossible; 409eb4 uses constant
2000 and a signed word, also safe. 402ad9/402b79 use full32
sums/differences; 4335be combines a signed-word virtual result with full32
state; 43bf8b uses prior full32 arithmetic; 462a84 uses full32 coordinates;
4762d7 uses 1<<(global 6c6028&31) and another value; 47660d uses a negated
full32 value; 492ad3 forwards both full32 arguments. None of those local call
sequences proves INT_MIN impossible. Their actual value bounds are H; a live
audit must record them before claiming every game call satisfies the
precondition.

**47f9a0, `matrix_angles(matrix,out6)` (C).** Store lowword
inverse_sine(matrix+a) to out first. If its signed-word magnitude is 16384
(47f9c8), store out+2=0 then lowword angle_ratio(matrix0,matrix2) to out+4.
Otherwise store angle_ratio(s16(matrix10),-s16(matrix8)) to out+2, then
angle_ratio(s16(matrix6),-s16(matrix4)) to out+4. Inputs are reread after
each store, so overlap matters. EAX=out; EDX=the last ratio's EDX.
Word-derived ratio arguments are always legal; inverse lookup accepts all
input words.

### E: energy register and write contracts

**47e0b0, `multiply_q10(s32 a,s32 b)` (C).** Signed64 product P;
EAX=low32(P arithmetic-shifted 10), EDX=upper32 of **unshifted** P. SHRD at
47e0ba changes only EAX. No branch, state, table or division.

**467adc, `approach_word(state,target,step)` (C).** Comparisons use signed
low words, while target's upper16 bits survive some returns. If target<old,
subtract lowword step modulo 16 and store; if signed new<=target, store
target and return EAX=0, EDX=full target. Otherwise EAX combines the state
pointer's upper16 with lowword(new-target), EDX=full target. If target>old,
add step modulo 16 and store; if signed new>=target, clamp/store target and
return EAX=0, EDX=full target. Otherwise EDX=target's upper16 combined with
lowword(target-new), and EAX=EDX. Equal values cause no write, EAX=0,
EDX=full target. Both comparisons after add/sub use the wrapped word;
negative steps and wrap can move away from the target. Four Jcc sites cover
the two directions, equality and clamp/nonclamp paths. No divide.

**413b38, `energy_accumulate(S,request)` (C).** Read word sum S0+S2 modulo
16, gap=s16(wrap16(S6-sum)), request=s16(lowword argument).
delta=min(request,5,gap), signed; updated sum=wrap16(sum+delta). Q10-multiply
s16(S4) by s16(updated sum). Call approach_word on S0 with that full32
product result and step 10000 if gap<0, otherwise 65. Store S2=wrap16(updated
sum-new S0) after the approach writes; S4/S6 unchanged. EDX=approach's EDX.
EAX=approach's upper16 combined with lowword(request-delta). The sole direct
caller 41aaa6 consumes AX, but the replacement still preserves the entire
result. Meaning of these fields as energy/heat is H; the arithmetic, branch
conditions and write order are C + 1M.

### Padding contract for the five wrappers

Hooks `pose_parent` 478bc4, `track_forward` 478f6c, `track_inverse` 478ff4,
`track_scaled` 4790b4 and `track_scaled_inverse` 479194 (`core/track`).
Every wrapper builds one local matrix (478f6c and 478ff4 a second one for the
composition result) and writes all of its bytes except byte 19, the Borland
padding between the mode byte 18 and the translation at 20. Byte 19 of the
output matrix is therefore stack residue on two kinds of path: composition
takes its copy path from the local (local mode >= 1, other mode < 1), or, for
478f6c/478ff4, composition takes a computing path that never writes byte 19
of the second local, which `rep movsl` then copies to T+41. All other output
bytes and EDX:EAX are deterministic.

Why replacing the residue by a fixed value is behaviour-preserving:
(a) the residue is not a function of the arguments but of earlier stack use,
and every replaced hook already changes that stack history, so no replacement
could reproduce it in general; (b) compiled code touches struct padding only
through block copies; all matrix consumers implemented in core (compose,
rotate, transform, transpose, matrix_angles, Euler, pose_matrix) read byte 19
only inside 20-byte rotation copies; (c) DBSIM's three `memcmp` calls
(401dfd, 4510d0, 45c4f2) compare a file header, a 24-byte HUD damage snapshot
built by 41b4ec/4151a4 and two 29-byte globals, never matrices; (d) the live
regression with the fixed value is identical
(`<es2-data>\logs\oracle-check-20261006T153319Z`). Open hypothesis:
the generic stream writers at 47abff/47b2db (and the `fwrite` sites) might
serialize objects containing matrices; that would change one padding byte in
such a file, which a reload again only copies.

Contract: `core/track` takes the value of an uninitialized local byte as
`padding`; the proxy passes 0 (`MATH_PADDING`). In vitro, the original runs
with the stack below the call poisoned with 0x31 or 0xa7 (and incoming
EAX/EDX set to the same pattern) and must equal core called with that padding
in every byte and in EDX:EAX: 1,000,000 cases per wrapper, the residue path
taken in 30-60 % of them, all four Jcc sites both ways. Compare mode masks
only the output byte 19 (T+41 for the T+2e outputs; comparator
`math_padding_equal`). Disabled tracks (T+20 == 0) in 478f6c/478ff4 return
the incoming EAX/EDX. The scaled variants fault (#DE) like the original when
enabled with s16(T+1e) == 0.

### Leaf chain test and stack headroom

The chain test executes the exact leaf order for zero-track managers with a
dirty root, optionally a full-rotation parent and dirty child, then both
actor cache states: track_count three times (advance, sort, pose traversal),
pose_matrix, optional child Euler and composition, optional actor Euler,
transform_point, matrix_angles, angle addition and root-matrix reset. It
compares 4096 complete synthetic actor/matrix/flag arenas. It does not
execute or hook 40250c, does not resolve imports and does not prove
active-track or virtual-resource paths.

The pinned executable reserves 1 MiB stack (commits 8 KiB initially). The
x86 extended dispatcher reserves 0x102b0 bytes per call in every mode,
including its comparison buffers; the replaced ground callback nests about
five such frames. **Open:** measure the real stack headroom in the mission
thread (synthetic 16 MiB harness stacks do not establish it) or move the
compare buffers out of the common dispatch path.

### Direct caller register audit

Every direct CALL to the twenty entries was rescanned from the hashed CODE
section. Lists below are call-site VAs, not function-entry VAs. `read` means a
register byte is consumed before being overwritten; `dead` means all paths
overwrite all bits first; `returned` means bits survive a RET; `next-call`
means they survive to another call, so liveness is deliberately conservative.
Mixed outcomes retain every possibility. No indirect-target completeness or
dead-return exemption is claimed: the hooks reproduce both registers
regardless.

**47ebb8** (90 direct sites):
- EAX dead: 401338, 401516, 401583, 401676, 4016ef, 401833, 40195f, 40256e, 402650, 40281f, 402a03, 40371a, 4037bc, 403bf1, 403f31, 4044e5, 404a44, 404cdb, 40514a, 40682a, 40862b, 4093de, 40a32d, 40a68c, 40a6c3, 40b050, 40b20a, 40b241, 40d5b3, 40d669, 40d8c9, 40e5ab, 40e860, 40e8aa, 40f672, 411d99, 4158d7, 41594e, 4159c2, 415a36, 41602c, 416359, 4163ae, 4164b5, 41650a, 416979, 418c82, 419079, 4190f8, 41969f, 4199f5, 41b6cb, 41b701, 41b7b3, 41ea38, 41ef6b, 41fceb, 421762, 4218ea, 421d2e, 422948, 4229d2, 427b75, 427bd1, 427c0e, 427cd9, 427eab, 428fb4, 42fbe4, 461406, 466ccc, 4674f3, 467522, 467653, 4677d5, 46a85c, 46aafa, 46b783, 478bfa, 478fad, 479142.
- EAX next-call: 401f8e, 401fcd, 401ffe, 409cf0, 479039, 479225.
- EAX read: 40f5d1.
- EAX returned: 478c56, 4951f7.
- EDX dead: 401338, 401516, 401583, 401676, 4016ef, 401833, 40195f, 401f8e, 401fcd, 40256e, 402650, 40281f, 402a03, 40371a, 4037bc, 403bf1, 403f31, 4044e5, 40514a, 40682a, 40862b, 4093de, 40a32d, 40a68c, 40b050, 40b20a, 40b241, 40d5b3, 40d669, 40d8c9, 40e5ab, 40e860, 40e8aa, 40f5d1, 411d99, 41602c, 416359, 4163ae, 4164b5, 41650a, 418c82, 419079, 4190f8, 41969f, 4199f5, 41b6cb, 41b701, 41b7b3, 41ef6b, 41fceb, 421762, 4218ea, 421d2e, 422948, 4229d2, 427b75, 427bd1, 427c0e, 427eab, 461406, 466ccc, 4674f3, 467522, 467653, 4677d5, 46a85c, 46aafa, 46b783, 478bfa, 478fad, 479142, 479225.
- EDX next-call: 401ffe, 404cdb, 409cf0, 40a6c3, 40f672, 4158d7, 41594e, 4159c2, 415a36, 416979, 41ea38, 427cd9, 428fb4, 42fbe4, 479039.
- EDX read: 404a44.
- EDX returned: 478c56, 4951f7.

**47fa20** (17 direct sites):
- EAX dead: 401874, 4044ff, 405164, 40c972, 40d68c, 40e8c4, 418ca2, 421d48, 478fc3, 479081.
- EAX next-call: 40f622, 4761ca, 478d79.
- EAX returned: 478c0e, 479156, 479273, 48db66.
- EDX dead: 401874, 4044ff, 405164, 40c972, 40d68c, 418ca2, 421d48, 478fc3, 479081.
- EDX next-call: 40e8c4, 40f622, 4761ca, 478d79.
- EDX returned: 478c0e, 479156, 479273, 48db66.

**480304** (30 direct sites):
- EAX dead: 401563, 4016c3, 4040a4, 40c195, 413de2, 416097, 4191bc, 4191d9, 41f111, 4217cf, 42663a, 427f5c, 428fd1, 466d22, 466dd2, 46771a, 46cdbc, 46ce5a, 46d0a7, 46d172, 47906e, 479260, 48db47.
- EAX next-call: 40406f, 41f0cd, 466d92.
- EAX read: 404d4f.
- EAX dead/returned: 46775a.
- EAX returned: 467813, 4954f3.
- EDX dead: 401563, 4016c3, 4040a4, 404d4f, 413de2, 416097, 4191bc, 4191d9, 41f111, 427f5c, 428fd1, 466d22, 466d92, 466dd2, 46771a, 46775a, 46cdbc, 46ce5a, 46d0a7, 46d172, 47906e, 479260.
- EDX next-call: 40406f, 41f0cd, 42663a.
- EDX dead/next-call: 40c195.
- EDX returned: 4217cf, 467813, 48db47, 4954f3.

**47df19** (16 direct sites):
- EAX dead: 404043, 404078, 404d20, 413db3, 41606b, 4191a1, 41f092, 41f0d6, 4217a1, 426602, 427f2d, 428fc0, 4676c8, 479042, 479231.
- EAX returned: 48dc4a.
- EDX dead: 404043, 404078, 404d20, 413db3, 41606b, 4191a1, 41f092, 41f0d6, 4217a1, 426602, 427f2d, 428fc0, 4676c8, 479042, 479231.
- EDX returned: 48dc4a.

**478c1c** (1 direct site):
- EAX dead: 478cf6.
- EDX dead: 478cf6.

**478bc4** (1 direct site):
- EAX dead: 478cc1.
- EDX dead: 478cc1.

**49270c** (1 direct site):
- EAX dead: 479bcb.
- EDX dead: 479bcb.

**47df48** (2 direct sites):
- EAX returned: 41bb71.
- EAX read: 41f37d.
- EDX returned: 41bb71.
- EDX read: 41f37d.

**4789b0** (3 direct sites):
- EAX read: 4789db, 478ab6, 478b0a.
- EDX next-call/returned: 4789db.
- EDX dead/next-call: 478ab6.
- EDX dead/returned: 478b0a.

**478ef4** (4 direct sites):
- EAX returned: 47931a, 4797f7.
- EAX next-call/returned: 479920.
- EAX next-call: 479a3e.
- EDX returned: 47931a, 4797f7.
- EDX next-call/returned: 479920.
- EDX next-call: 479a3e.

**478f6c** (1 direct site):
- EAX dead/read: 47983b.
- EDX dead: 47983b.

**478ff4** (1 direct site):
- EAX dead/read/returned: 479a47.
- EDX dead: 479a47.

**4790b4** (2 direct sites):
- EAX returned: 478af2, 478b52.
- EDX returned: 478af2, 478b52.

**479194** (1 direct site):
- EAX dead: 478b91.
- EDX dead: 478b91.

**47f9a0** (8 direct sites):
- EAX dead: 4018b0, 4025a9, 40450f, 40864e, 40d914, 40e927, 40f751, 467587.
- EDX dead: 4018b0, 4025a9, 40d914, 40f751, 467587.
- EDX dead/returned: 40450f.
- EDX next-call/read: 40864e.
- EDX next-call: 40e927.

**47da4c** (1 direct site):
- EAX read: 47f9b2.
- EDX read: 47f9b2.

**47d32c** (12 direct sites):
- EAX read: 402ad9, 402b79, 409eb4, 4335be, 43bf8b, 462a84, 4762d7, 47660d, 47f9d9, 47f9f3, 47fa0b.
- EAX returned: 492ad3.
- EDX dead: 402ad9, 402b79, 409eb4, 4335be, 462a84, 47660d.
- EDX next-call: 43bf8b, 47f9f3.
- EDX read: 4762d7.
- EDX returned: 47f9d9, 47fa0b, 492ad3.

**413b38** (1 direct site):
- EAX read: 41aaa6.
- EDX dead: 41aaa6.

**47e0b0** (163 direct sites):
- EAX read: 4013c1, 40347e, 408b5a, 408c74, 408c8e, 408cec, 408d1b, 40b291, 40b2ac, 40b2d3, 40b2ee, 40bfe6, 40c00a, 40e0a5, 40e93c, 40ee80, 4121d7, 412254, 4122a5, 4122da, 41231d, 413adb, 413b8b, 413c8b, 416259, 416643, 416745, 416b16, 416b58, 416bf4, 416c1e, 416c41, 41727e, 41742c, 417c70, 417cdf, 417d31, 417d4d, 417dc9, 4188ff, 418aa0, 4191fd, 419222, 419238, 419457, 41946c, 4194da, 419c9a, 419cb5, 419dac, 419dc7, 41a008, 41a023, 41a1a0, 41a229, 41e8eb, 41e92d, 41f16a, 41f181, 41f1f9, 41f22e, 41f4db, 41f4f7, 42030d, 42196b, 4219cc, 421a48, 421ac2, 4241d5, 426b36, 428be9, 4335d2, 443991, 4439b9, 4439e0, 443a08, 443a2e, 443a55, 443ac4, 443aec, 443b13, 443b3b, 443b61, 443b88, 443c04, 443c38, 443c6b, 443c9f, 443cd4, 443d07, 443d72, 443d9f, 443dd3, 443e01, 443e35, 443e63, 443ee7, 443f14, 443f48, 443f76, 443faa, 443fd8, 44405d, 44408a, 4440be, 4440ec, 444120, 44414e, 4441c9, 4441fd, 444233, 444267, 44429c, 4442cf, 444356, 44438a, 4443c0, 4443f4, 444429, 44445c, 4444e4, 444518, 44454e, 444582, 4445b7, 4445ea, 444658, 444685, 4446b9, 4446e7, 44471b, 444749, 4447cd, 4447fa, 44482e, 44485c, 444890, 4448be, 444943, 444970, 4449a4, 4449d2, 444a06, 444a34, 460b6e, 460b86, 466c3f, 466c62, 466c7b, 466d41, 466d66, 466ece, 466f47, 467034, 4671f8, 467237, 467385, 46778e, 47630f, 476a40.
- EAX returned: 4154c2, 418f31, 421599.
- EDX dead: 4013c1, 40347e, 408d1b, 40b291, 40b2ac, 40b2d3, 40bfe6, 40e0a5, 412254, 4122a5, 4122da, 413b8b, 416745, 416bf4, 418aa0, 4191fd, 419dac, 41a008, 41a1a0, 41e8eb, 41f181, 41f1f9, 41f22e, 41f4db, 41f4f7, 42030d, 42196b, 4219cc, 421a48, 421ac2, 4241d5, 4335d2, 443991, 4439b9, 4439e0, 443a08, 443a2e, 443a55, 443ac4, 443aec, 443b13, 443b3b, 443b61, 443b88, 443c04, 443c38, 443c6b, 443c9f, 443cd4, 443d07, 443d72, 443d9f, 443dd3, 443e01, 443e35, 443e63, 443ee7, 443f14, 443f48, 443f76, 443faa, 443fd8, 44405d, 44408a, 4440be, 4440ec, 444120, 44414e, 4441c9, 4441fd, 444233, 444267, 44429c, 4442cf, 444356, 44438a, 4443c0, 4443f4, 444429, 44445c, 4444e4, 444518, 44454e, 444582, 4445b7, 4445ea, 444658, 444685, 4446b9, 4446e7, 44471b, 444749, 4447cd, 4447fa, 44482e, 44485c, 444890, 4448be, 444943, 444970, 4449a4, 4449d2, 444a06, 444a34, 460b6e, 460b86, 466d41, 466d66, 466ece, 467034, 4671f8, 467237, 46778e, 47630f, 476a40.
- EDX returned: 408b5a, 40e93c, 40ee80, 413adb, 4154c2, 417dc9, 418f31, 41946c, 421599, 426b36.
- EDX next-call: 408c74, 408c8e, 408cec, 40b2ee, 40c00a, 416259, 416c1e, 416c41, 417cdf, 4188ff, 419222, 419457, 4194da, 419c9a, 419cb5, 419dc7, 41a023, 41a229, 41e92d, 41f16a, 466f47, 467385.
- EDX dead/next-call: 4121d7, 416b16, 416b58, 417c70, 466c3f.
- EDX read: 41231d, 41742c, 466c62, 466c7b.
- EDX dead/returned: 413c8b, 417d31, 417d4d, 419238, 428be9.
- EDX dead/read: 416643, 41727e.

**467adc** (13 direct sites):
- EAX returned: 401c40, 401c69, 413ba9.
- EAX dead/read: 403e66.
- EAX next-call: 40a42c, 40b0db.
- EAX read: 40a442, 40b0c5.
- EAX dead: 416c82, 41746c, 41a5f7, 41a8af, 46a904.
- EDX returned: 401c40, 401c69, 413ba9.
- EDX dead: 403e66, 40b0c5, 41746c, 41a5f7, 41a8af.
- EDX read: 40a42c.
- EDX next-call: 40a442, 40b0db, 46a904.
- EDX dead/next-call/read: 416c82.

## Animation step 40250c and manager reset 478b7c

Bytes 40250c-402603, 478b7c-478bc3; CW3220.DLL `_memset` at RVA 0x2a58.

**478b7c `manager_reset(M,R)` (C, implemented).** If the first track
pointer [M+0x1a] is zero: no write, EAX=0, EDX unchanged. Otherwise call
`track_scaled_inverse([M+0x1a], R)`, then read [M+0x16] and copy R into it:
five DWORDs (bytes 0..19), then translation +0x1c, +0x14, +0x18, sequential
reads. EAX=[M+0x16]+0x14, EDX=[M+0x16]. The padding byte T+0x41 of the
nested call follows the [padding contract](#padding-contract-for-the-five-wrappers).

**40250c `animation_step(A,M,step)` (C, implemented).**
1. Local identity I: memset(I,0,32) through CW3220, then words I+0x10, I+6,
   I+0 = 0x4000.
2. `manager_reset(M,I)`.
3. `478d38(M,step)`; the step is pushed as ECX with only CX defined, and
   478d38 reads only the word.
4. [M+0x16] is read after the advance.
5. If word A+0x32 is zero, Euler from A+0xc into the actor matrix A+0x12
   and A+0x32=1; its translation A+0x26..0x31 is the world position.
6. `transform_point(R+0x14, A+0x12, local)`, then A+0x2e=Z, A+0x26=X,
   A+0x2a=Y.
7. `matrix_angles(R, local)`; angle words += A+0x10, A+0xc, A+0xe (16-bit
   wrap), written as one DWORD at A+0xc and a word at A+0x10; A+0x32=0.
8. memset(R,0,32), then R+0x10, R+6, R+0 = 0x4000. CW3220's memset returns
   EAX=destination and leaves EDX=count, so the result is EAX=[M+0x16],
   EDX=0x20. Callers 403edd and 418f68 pass EAX/EDX on to their callers, so
   both are reproduced.

`core/animation` implements the whole body. The track advance 478d38 is
called through its address (host callback), so its own hook
[`track_advance`](#track-advance-478d38) applies or the game's function
runs. Compare mode is impossible for 40250c because both phases would
advance the tracks: the hook is `compare_unsupported`. In vitro: 478d38
replaced by a stub in the private mapping (the same stub as core's
callback), CW3220 memset emulated with the same register results. Live:
identical regression with the hook replaced
(`<es2-data>\logs\oracle-check-20261006T162015Z`).

## Track advance 478d38

Bytes 478d38-478d5d, 478aac-478afe, 4789d0-478a38, 478e8c-478ea3,
478b00-478b3a, 478c64-478d34, 479720-479aae, 479ab0-479bf6 and the clip
classes below. `es2_track_advance_registers` in `core/animation`, hook
`track_advance` (`compare_unsupported`: a second phase would advance the
tracks again). 478d38 has one direct caller, 40254e in 40250c; the replaced
40250c calls 478d38 through its address, so both hooks are independently
selectable. `es2_track_advance_registers` calls the core functions of the
four [track hooks](#track-hooks-479720-4789d0-478aac-and-478c64) directly.

**Clip classes (C).** The clip objects in `[R+6]` (R = `[T+0]`, an
`ANAnimList`) are created only by the stream factories of the class registry
at 4a687c (`ANSequence` 4913f0 → vtable 4a684c, `ANCyclicSequence` 4919a0 →
vtable 4a6810, derived from 4a684c; the only writes of these vtable
immediates are at 49143b, 4919eb, 4919fc). Their slots +20/+24 are the only
virtual calls in the subtree:

| Class | +20 next(frame) | +24 previous(frame) |
| --- | --- | --- |
| ANSequence 478760 / 47877c | frame+1 if s16 frame < s16 count-1, else frame | frame-1 if the word is nonzero, else frame |
| ANCyclicSequence 4787e4 / 478804 | frame+1 if s16 frame < s16 count-1, else 0 | frame-1 if the word is nonzero, else count-1 |

`count` is the word at clip+0a. The methods compute in EAX from the full
argument slot, but the callers push a register whose high word is not
defined and consume only AX (`movw`, `testw`, `movswl %di`), so only the low
words matter for memory. Core reproduces both classes; any other vtable is
forwarded to its original method (`es2_clip_classes.other`).

**Structure.** 478d38(M, step): 478aac(M, step); 478b00(M); 478c64(M).
EAX/EDX come from the final CW3220 memset: EAX = `[M+0e]`, EDX = s16 `M+0c`.

- 478aac: for i = count-1 … 0 run 479720(track i, step); sort (4789d0);
  if `[M+1a]` != 0, `track_scaled(first track, [M+16])` (4790b4).
- 4789d0: bubble sort of the track pointers, repeated until a pass makes no
  swap, ascending by the signed priority word clip+06 of each track's current
  clip (478e8c; ties keep their order).
- 479720(T, step): if state bit 0 and stop flag byte T+1a bit 0, nothing.
  delta = low word of (s16 step × s16 rate T+4e) >> 8 (arithmetic); zero →
  nothing. time = T+1c + delta (16 bit). Backward at frame 0 of a clamped
  clip (previous returns 0, trigger frame T+0c ≠ 0) clamps time to 0.
  time 0: T+1c = 0, and if state bit 1 (in transition) the next frame is
  re-derived (T+8 = T+4, T+0a = next(T+6), T+1e = duration of (T+4, T+6),
  bit 1 cleared, clip_pose). Forward while time ≠ 0 and time ≥ T+1e:
  track_forward (478f6c), current = next; if the new frame equals the
  trigger T+0c, next = transition target T+0e/T+10, duration T+12, trigger
  cleared to ffff, bit 1 set, pose T+20..2d from resource pose T+14; else
  next frame = next(T+0a), duration from the frame record, bit 1 cleared,
  clip_pose; time -= old duration (a zero new duration ends the loop).
  Backward while time < 0: next = current; trigger → transition target as
  the current frame; else current frame = previous(T+6); track_inverse
  (478ff4); time += duration, the rest stays in T+1c. After every step,
  reaching stop clip T+16 at stop frame T+18 (ffff = any) sets bit 0; with
  stop flag bit 0 the remaining time is dropped.
- 478b00 → 479ab0(T, node poses `[M+12]`, flags `[M+0e]`) for every track
  from last to first: fraction = (T+1c × 1024 + T+1e/2) / T+1e (C
  truncation, IDIV; zero T+1e faults); for each bone j of the current clip's
  map (clip+10 count, clip+12 node words): node 0 is skipped; pose indices
  of (current clip, frame) and (next clip, next frame) — both tables strided
  by the *current* clip's bone count — are copied when equal, else blended by
  `pose_interpolate`; flag[node] = 1.
- 478c64: walks the shape hierarchy `[[M+4]+2a]` (count word +0a, 4-byte
  (parent, node) entries at `[+0c]`); a root (parent ffff) with flag bit 0
  gets `pose_matrix`, a child with flag bit 0 or a parent flag bit 1 gets
  `pose_parent(pose, parent matrix, own matrix)`; rebuilt nodes get flag
  bit 1. Then memset(flags, 0, s16 M+0c).

The matrix wrappers inside (478f6c, 478ff4, 4790b4, 478bc4) keep the
padding contract: byte 19 of every written matrix may hold stack residue in
the original. With the deeper nesting here the residue is not just the
caller's stack pattern but leftovers of earlier calls in the same advance,
so the in-vitro comparison masks those bytes (T+41 of each track, byte 19 of
each node matrix) and reports how often they differ.

**Validation (in vitro).** 1,000,000 synthetic managers (0-10 tracks, 1-12
nodes, random hierarchies, one or two resources with up to four clips of
both classes plus an own vtable for the fallback, steps mostly ±64 and ±512,
one in 256 over the full word; stack poisoned 31/a7): EDX:EAX and all 12,288
arena bytes identical except the masked padding bytes. All conditional
branches of the subtree and the four frame-step methods are observed both
ways; the sites of 4789d0, 479720, 478aac and 478c64 belong to their own
hooks in `branch_sites.h`. A deliberate core mutation (descending sort) and
the stop-sentinel mutation on the time-0 path (stop frame without the ffff
wildcard) are caught. Live: identical regression with the hook replaced
(`<es2-data>\logs\oracle-check-20261006T184926Z`).

## Track hooks 479720, 4789d0, 478aac and 478c64

Bytes 4789b0-4789cc, 4789d0-478a38, 478a3c-478a6e, 478aac-478afe,
478c64-478d34, 478e8c-478ea3, 479720-479aae and the four clip frame-step
methods; a call scan of the whole image finds only direct `call rel32`
references and no stored addresses of these functions.

| Hook | VA | Callers | Live entries with `track_advance` replaced |
| --- | --- | --- | --- |
| `track_sort` | 4789d0 | 478a3c (append a track to the first free slot of M+1a and sort; from 4023e4, which creates a track, sets its clip and rate 0 and appends it), 478aac | 478a3c only |
| `track_step` | 479720 | 478aac | none |
| `track_list` | 478aac | 478d38 | none |
| `node_matrices` | 478c64 | 478838 (manager constructor, after the node poses and flags are set), 478d38 | 478838 only |

**478e8c.** A live, unreplaced callee of 4789d0 (`callees_live` in the
DBSIM map). It is a ten-instruction read-only leaf: EDX = s16 T+4, EAX =
high word of the selected clip pointer : priority word clip+6. Its only
caller is 4789d0, which compares the two results' low words (`cmp cx, ax`).
Core computes it inside `es2_track_sort_registers` (and keeps its full
EDX:EAX, which is the sort's result); it is not hooked alone. 4789b0
(`track_count`) is likewise computed in core.

**Register contract** (cdecl; values the callers can observe):

- 4789d0(M): n = 4789b0(M). n < 2: EAX = n, EDX = M+1a+4n. Otherwise the
  last pass makes no swap, so EDX:EAX is 478e8c's result for the last
  track. A swapping iteration leaves EAX/EDX = the two old pointers, which
  only matters for later iterations.
- 479720(T, step slot), entry EDX = E. Stopped (T+50 bit 0 and T+1a bit 0):
  EAX = full step slot, EDX = E. Otherwise IMUL of s16 step and s16 T+4e:
  EDX = sign of the product (it always fits 31 bits), EAX = product sar 8;
  AX = 0 returns there. Then every 16-bit move into AX/DX keeps the other
  register half, and the virtual frame steps receive full slots built that
  way: 47976e previous(T+6) with EDX's high word (the product sign), 4797b0
  next(T+6) with EAX's high word, 4798c7 next(T+0a) on clip T+8 and 4799f1
  previous(T+6) with EAX's high word. 478ef4 leaves EAX = T, EDX = clip slot
  or 3 * pose index; 478f6c/478ff4 leave EAX = T+42, EDX = T+2e when T+20
  is set and keep EAX/EDX otherwise; a transition pose leaves EAX =
  3 * s16 T+14 and EDX = high word of the angle dword : angle word +4;
  the backward loop sets EDX = -s16 time before adding the duration. The
  exits therefore return: the time-0 path EDX = (EDX high : T+4) and, at
  the stop clip, EAX = (EAX high : T+18); the forward loop and the
  in-frame path whatever the last of these left; the backward loop EAX =
  (high : T+4 or T+18), EDX = -time. ESI's entry high word never reaches a
  result.
- 478aac(M, step): each 479720 call gets EDX = T (`mov edx,[esi]`) and a
  slot whose high word is the previous call's EAX high word (first: 0 from
  4789b0). Result: 4790b4's EDX:EAX for the first track after the sort,
  or EAX = 0 with the sort's EDX = M+1a for an empty list. The slot high
  words never reach 478aac's own result.
- 478c64(M): contract as in [Track advance 478d38](#track-advance-478d38);
  EAX = flags `[M+0e]`, EDX = s16 M+0c (the runtime memset, emulated in the
  harness). EDX is dead at 478838 (another memset follows); at 478d38 it is
  `track_advance`'s result. The hierarchy count word and, after each matrix
  call, the node word and the flag pointer are reread, as in the original.

For the game's two clip classes only the low words of these slots reach
memory; the full slots matter for their register results (ANSequence next
returns slot + 1) and are passed unchanged to any other class. The game
creates no other class (ANSequence/ANCyclicSequence are the only classes the
factories create).

**Preconditions.** As for 478d38: zero divisors in 4790b4 fault. Standalone
479720 accepts zero frame durations going forward (479906 falls through and
ends the step). Going backward through a zero duration the original loops
forever (479a58: -time > 0 holds, time += 0); that input is outside the
contract and not generated. A negative node count makes 478c64's memset
clear about 4 GiB.

**Proved one-way edge.** 479973 repeats `test si,si` right after the
identical test at 47996a fell through (time < 0, no instruction in
between): the jge is never taken (coverage.h, `check_shards.py`).

**Validation (in vitro).** `track_callee_cases.h`, per hook 1,001,024
direct cases on managers from the 478d38 generator (0-10 tracks; one track
of a 1-10 track manager for 479720, with random entry registers and step
slots, and in a quarter of its cases zeroed frame durations on the forward
path): EDX:EAX and all 12,288 arena bytes exact except byte 19 of the
matrices the padding wrappers write, and the ordered calls of the own clip
class (whose results depend on the full slot) identical. 256 installed
poison cases per hook and 3,072 real-callee cases with 4789b0, 478ef4,
478f6c, 478ff4, 4790b4, 478bc4, 478c1c (and 479720/4789d0 below 478aac)
hooked in rotating modes. All 42 sites both ways except the proved 479973;
the fall-through at 479906 is covered by the standalone 479720 cases.

**Live.** With every hook replaced, `track_step` and `track_list` have 0
calls (their only callers are replaced), `track_sort` is entered only from
478a3c and `node_matrices` only from 478838 (hooks.md table). A run with
`track_advance=original` and the four hooks replaced would exercise
`track_list` once per advance, `track_step` once per occupied track and
advance, `track_sort` once per advance plus 478a3c, and `node_matrices`
once per advance plus 478838; no such run is recorded.

## Ground animation 418f40 and track target 47967c

Bytes 418f40-418f72, 402604-402626, 4796d8-47971e, 47967c-4796d6,
4793d4-47967a and the clip methods 47878c/4787b8 (ANSequence) and
478820/47882c (ANCyclicSequence). Implemented in `core/animation`; hooks
`ground_animate`, `actor_animate`, `track_rate`, `track_target`
(4793d4 is [`track_route`](#4793d4-track_route)).

- **402604 `actor_animate(A, step)`**: if byte A+39 is set,
  `animation_step(A, [A+34], step)` and its EDX:EAX; otherwise EAX = A,
  EDX = A+38. Callers 403d4b, 403edd, 4176ac, 418f68.
- **418f40 `ground_animate(A)`**: `track_rate([A+22c], word A+2a0)` (the
  pushed slot's high word is EAX at entry), `movement_step(100)` with the
  time scale 4d3b64, then `actor_animate(A, that step)`; returns
  actor_animate's EDX:EAX. Callers 41a3a5, 41a51b (41a360) and 41e495.
- **4796d8 `track_rate(T, rate)`**: stores T+4e; only when the sign of the
  word changes (negative ↔ non-negative) it re-runs 47967c with the current
  target (T+16, T+18, T+1a). Otherwise EAX = T, EDX = the rate slot.
- **47967c `track_target(T, clip, frame, flags)`**: stores the words to
  T+16/18/1a and clears state bit 0. Target clip ffff, or already at the
  target (clip and frame equal, time T+1c = 0), sets bit 0 and the trigger
  T+0c = ffff (EAX = T, EDX = entry EDX high word with flags or T+6).
  Otherwise the route search 4793d4.
- **4793d4 route search**: start at the next frame (rate ≥ 0) or the
  current frame (rate < 0) of the current clip, stored as T+0c. For that
  frame, the frame record ([clip+0c] + 6×frame: duration, transition count,
  first index) selects transitions in [R+1e] (8 bytes: duration, target clip,
  target frame, pose). The first transition to the target clip T+16 is
  stored (T+12 duration, T+0e = target clip, T+10 frame, T+14 pose); with
  target frame ffff the search ends, otherwise the target clip's `reached`
  method (+28 forward, +2c backward) must accept (transition frame, target
  frame, time). Without an accepted transition the frame steps by the clip's
  next/previous method; the search ends with T+0c = ffff when the step makes
  no progress or returns to the start frame.

Clip methods +28/+2c ("reached", three word arguments a = transition frame,
b = target frame, c = time): ANSequence returns AX = 1 if b is beyond a in
the play direction (+28: b > a, +2c: b < a) or b = a with time 0, else 0;
ANCyclicSequence always returns AX = 1. The methods leave EAX/EDX partly
as they were at the call; core reproduces those values (`es2_clip_call`),
because 4793d4 returns them.

Result registers: 4793d4 returns EDX:EAX of its last action (the matched
transition's pose word with R's high word, the accepting method's result,
or the sign-extended last step with the method's EDX). The first argument
of each virtual call carries register high words that core tracks; only the
caller's EDI (high word of the time argument) is not known to a hook, which
matters for no known class.

Branches never taken at their sites and therefore not instrumented: 4794aa
and 4795f2 (rate sign tests inside the branch already chosen by that sign at
4793e5) and 479703 (reached only when the sign bits differ).

**Validation (in vitro).** 1,000,000 cases each with random entry
EAX/EDX/ECX: `track_target` and `track_rate` on synthetic tracks with up to
four clips (both classes and an own vtable), frame records and up to twelve
transitions; `actor_animate` with 40250c replaced by a stub; `ground_animate`
with 402604 replaced by a stub and the real 4796d8/467924. EDX:EAX, all
12,288 arena bytes and the stub calls identical; all 28 instrumented branch
sites both ways (418f40 has none). Mutations caught: `reached` with `>=`,
rate sign test with `<= 0` (only rate 0 differs). Live: identical regression
with the four hooks replaced; `track_target` is called directly from the
AI/animation control code, and 4796d8 rarely changes the rate sign
(`<es2-data>\logs\oracle-check-20261006T191501Z`).

## Terrain height 46e180

Bytes 46e180-46e494; `es2_terrain_height_registers` in `core/terrain`, hook
`terrain_height` (read-only, compare mode supported). 33 direct callers,
among them the ground callback 41a360 (41a400) and its collision/rollback
neighbours.

Grid G: cell array `[G+ec]` of 16-byte cells (height byte +0, flags byte
+0f), log2 columns `[G+100]`, log2 rows `[G+104]`, log2 cell size s
`[G+108]`, height offset `[G+110]`, height scale `[G+118]`; all dwords,
shift counts use their low five bits. Point P: X at +0, Y at +4.

1. Cell column/row x = X >> s, y = Y >> s (arithmetic). Outside
   0 ≤ x < 2^columns, 0 ≤ y < 2^rows: EAX = 0, EDX = x.
2. Corner cells (x,y), (x,y+1), (x+1,y+1), (x+1,y) at index
   (row << columns) + column; an index outside 0 ≤ i < 2^(columns+rows)
   (signed) gives no cell: EAX = 0, EDX = the (x+1,y) pointer or 0. A column
   x+1 past the row end wraps into the next row's first cell, as in the
   original.
3. Heights h = byte × scale + offset; fx = X − (x << s), fy = Y − (y << s),
   size = 1 << s; every product is 32-bit and shifted right by s.
   Flags (& 3) = 2: if fx ≤ fy, h01 + (h11−h01)·fx + (h00−h01)·(size−fy)
   (EDX = the last term), else h10 + (h00−h10)·(size−fx) + (h11−h10)·fy
   (EDX = EAX). Flags 0 with size−fx < fy: h11 + (h01−h11)·(size−fx) +
   (h10−h11)·(size−fy) (EDX = EAX). Otherwise (flags 0 in the lower
   triangle, flags 1 and 3): h00 + (h10−h00)·fx + (h01−h00)·fy, where the
   last product is a one-operand IMUL, so EDX = its high 32 bits.

**Validation (in vitro).** 1,000,000 random grids (up to 16 × 16 cells,
cell sizes 2^0 … 2^31, shift words with high bits, points inside, at the
edges and outside, one case in 64 with 30 column bits so that the row shift
reaches the sign bit): EDX:EAX identical for random entry registers, arena
unchanged; 19 branch sites both ways. Not instrumented: 46e344 (the
(x+1,y) index is the (x,y) index + 1, between the indices of (x,y) and
(x,y+1), so it cannot leave the array while those two are inside). A
mutation treating flags 3 like flags 2 is caught. Live: identical regression
with the hook replaced (`<es2-data>\logs\oracle-check-20261006T205337Z`).

## Snapshot 402628 and rollback 4027fc

Bytes 402628-4027fa, 4027fc-4029d5 and CW3220.DLL `_memcpy` (RVA 0x29e8:
dwords forward, then the remaining bytes; EAX = destination, EDX = count,
ECX = 0). `es2_actor_snapshot_registers` / `es2_actor_rollback_registers`
in `core/animation`; hooks `actor_snapshot` (caller 41a39e) and
`actor_rollback` (callers 41a44f, 41a52e), both `compare_unsupported`
(variable-size copies into a global block).

The block at 0x4a8cd0 holds the actor matrix (+0, 32 bytes), ten 82-byte
track states (+0x20), node flags (+0x354 = 0x4a9024), node poses (+0x372 =
0x4a9042, 12 bytes each) and node matrices (+0x4da = 0x4a91aa, 32 bytes
each). Room for 30 nodes; the node count s16 M+0c is not checked, so more
nodes overwrite the following globals. Both functions first build the cached
actor matrix (Euler A+0c → A+12, A+32 = 1) if A+32 is zero — in the rollback
the copy then overwrites it, only the flag remains — then copy the actor
matrix (20 rotation bytes, then translation Z, X, Y), every non-empty track
slot of M = [A+34] (all ten slots are visited; an empty slot does not end
the loop; the unrolled field order covers all 82 bytes) and the three node
arrays with memcpy. Result: the last memcpy, EAX = node matrices destination,
EDX = 32 × n.

**Validation (in vitro).** The harness emulates CW3220 memcpy. 1,000,000
cases each with 0-40 nodes, random empty slots and random entry registers;
arena and a 4 KiB window from 0x4a8cd0 identical, all three branch sites of
each function both ways. A mutation that drops the T+50 copy is caught.
Live: identical regression with both hooks replaced
(`<es2-data>\logs\oracle-check-20261006T211210Z`); `actor_rollback`
is reached through the collision branch of `ground_update` in the suite
`c1_01-collision`, which drives into a building twice
(`<es2-data>\logs\oracle-check-20261006T220431Z`, golden
`<es2-data>\oracle\golden\c1_01-collision`), and in 10 suites of the
hooks.md table.

## Ground vehicle update 41a360

Bytes 41a360-41a54e. `es2_ground_update_registers` in `core/animation`, hook
`ground_update` (`compare_unsupported`). All callees are called through
their addresses, so their own hooks apply: snapshot 402628, ground
animation 418f40, terrain height 46e180, collision 418f74, rollback 4027fc,
RNG 492ee0 on 4d2599, sound 462830, clip 4792ac, effects 4195c8. Effects
receives the last collision/clip/rollback EAX/EDX explicitly, preserving its
no-track return in either mode ([Ground effects 4195c8](#ground-effects-4195c8)).

1. If byte A+a3 (in the motion block A+92; meaning H, not established) is set,
   the position A+26/2a/2e goes to the globals 4a9cd8/4a9cdc/4a9ce0.
2. Snapshot, ground animation.
3. Probe point = the new position; with A+a3 still set and dword 4a9ce4
   nonzero, X/Y get 4a9cd0/4a9cd4 added. Z = height(grid [4a0be0], point) +
   s16 [[A+1f2]+16]; the actor gets Z, X, Y (written in that order).
4. If the collision call returns AX ≠ 0: rollback; with A+a3 clear dword
   A+26e = 10000 and word A+254 = −5000 or +5000 (RNG bit 0 set → −5000),
   byte A+ae = (s16 speed A+28e > 0); with A+a3 set sound 0x29. Then word
   A+290 = 0, byte A+93 = 1. If the vehicle track [A+22c] is in transition
   (byte T+50 bit 1): clip call (T, word [[A+1f2]+12], 0, 0). Otherwise the
   speed becomes the vehicle limit word C+6 if that is ≤ −speed, else
   max(word C+6e, −speed) (C = [A+1f2] read at entry); the rate A+2a0 is
   negated, the ground animation runs again and a second collision rolls
   back and sets the speed to 0.
5. Effects call; result AX = 1 with the effects' high EAX word and EDX.

**Validation (in vitro).** All nine callees replaced by logging stubs that
change the actor from a per-case script (including the A+a3 flag, which the
function re-reads) and return script values. 1,000,000 cases: identical
call log with arguments, arena, globals and EDX:EAX; all 11 branch sites
both ways, with rollbacks and clip switches in a large share of the cases.
A mutation swapping the recoil sign is caught. Live: identical regression
with the hook replaced (`<es2-data>\logs\oracle-check-20261006T212904Z`);
the collision branch (step 4) is reached live in `c1_01-collision`
(`<es2-data>\logs\oracle-check-20261006T220431Z`). The clip call in
step 4 is reached in no suite (hooks.md, ground_collide 418f74).

## Ground collision 418f74

Bytes 418f74-4195c5. `es2_ground_collide_registers` in `core/collision`,
hook `ground_collide` (`compare_unsupported`). The arithmetic callees run in
core (distance 49288c, Euler matrix 47ebb8, transpose 47df19, rotation
480304, Q10 product 47e0b0, octagonal distance 47de4c). Virtual methods and
the game functions 404ae4, 46e498, 492f24, 434010 and 462830 are called at
their addresses through one cdecl caller (`es2_guest_call`), so their own
hooks apply. Names below are hypotheses; the sequence and arithmetic are
exact.

1. Read the object list pointer [4a9af8]; radius = AX of A's virtual +5c;
   copy the position P = A+26/2a/2e; dword A+2b0 = 0.
2. For each list entry B while the signed word counter is below word 4a9afe
   and B is not null: skip B = A and objects whose dword [[B+45]+14] is
   nonzero. reach = AX of B's virtual +7c; d = 49288c(A+26, B+26). If word
   B+1a8 = 1 and s16 of B's virtual +5c > d, A+2b0 = B. If reach ≠ 0 and
   s16 radius + s16 reach > d: contact (step 3).
3. Contact: B's virtual +68; if word B+1a8 ≠ 0 return AX = 1. Otherwise both
   Euler matrices (rebuilt from +0c into +12 when word +32 is 0, then +32 = 1)
   are copied; B's speed (0, s16 B+28e, 0) is rotated by B's matrix and then
   by A's transposed matrix. closing = Q10(s16 [[A+1f2]+4e], s16 A+28e) −
   Q10(s16 [[B+1f2]+4e], Y of that vector); impact = Q10(300, closing). If
   impact ≤ 200 return AX = 1. Otherwise each side's part strength: the list
   [[O+34]+4] maps word [[O+1f2]+0c] through its virtual +20 to an entry whose
   s16 index +4 selects a 32-byte record at [[O+34]+16] (record 6c56a8 when the
   entry is null or the index negative); strength = dword record+1c. Hit point
   = (B position + P) >> 1 per axis (arithmetic), then Z = the smaller
   strength (signed). A's virtual +70(impact, point, 1200, B), then B's
   virtual +70(impact, point, 1200, A); return AX = 1.
4. After the list: 404ae4(P, radius, 0) (static obstacles; the collision
   suite's two building hits end here, confirmed live); AX ≠ 0 returns
   AX = 1. Both 404ae4 and 46e498 receive P and may change it.
5. face = 46e498([4a0be0], P); steep = no face, or |s16 face+4| < 0x5aa.
   If byte A+a3 is clear, return EAX = steep.
6. Steep: if dword 4a9ce4 is 0 and Z of P > dword 4a9ce0 (last grounded Z),
   return 1. Otherwise 4a9ce4 = 1 and the slope drift 4a9cd0/4a9cd4 grows by
   Q10(10, s16 face+0) and Q10(10, s16 face+2); return 0.
7. Not steep with drift active: 4a9ce4 = 0; slide = 47de4c(drift X, Y); if
   slide > 250: damage = Q10(s16 table word 49a058[s16 4a9e5c], slide); six
   calls of A's virtual +74(part 7..12, RNG 492f24(4d2599, 3·damage) + damage,
   0), then 434010(0) and sound 0x29. The drift is cleared; return 0.

Registers: AX is the flag. Where the original sets only AX (`mov ax, 1`) the
high EAX word and EDX are those of the last call (the touch, Q10, second hit
or obstacle call); the steep and early grounded exits leave EDX = A+92,
the drift exit the high half of the last Q10 product, the slide exits the sign
of drift Y or the sound call's EDX. Pushed word arguments carry the upper half
of the register they were loaded into (the radius word for 404ae4 the EAX of
the last loop step, the part words of step 3 the config pointer); core
reproduces both.

**Validation.** In vitro (`ground_collide`, `collide_cases.h`): scenes of up
to six objects with stub vtables, part lists and records; 0x404ae4, 0x46e498,
0x492f24, 0x434010, 0x462830 replaced by logging stubs that also change the
point buffers; arithmetic helpers run as original code. 1,000,000 cases exact
(EDX:EAX, arena, globals, call log), including damaging contacts, slide
damage sequences and obstacle tests; all 25 branch sites both ways. Two
mutations caught (steep threshold `<` → `<=`; dropping the pushed high word
of the part damage value). Live: identical regression with the hook replaced
(`<es2-data>\logs\oracle-check-20261006T224803Z`); the two building
hits of `c1_01-collision` return through step 4. The live path counters
(`<es2-data>\logs\oracle-check-20261007T071143Z`; hooks.md,
ground_collide 418f74) show no object contact, slope drift or slide damage
in any suite: steps 3, 6 and 7 are confirmed in vitro; not checked live.

## Ground effects 4195c8

Hook `ground_effects` (`core/effects`, `compare_unsupported`). Derived with
llvm-objdump from the reference `VER95/DBSIM.EXE`; body VA 4195c8..4198f0,
file offsets 18bc8..18ef0. Addresses below are evidence for the operations,
not claims about the original source. Semantic names (attached object,
part, latch, listener, footstep, cockpit feedback) are hypotheses.

cdecl, one dword actor pointer A; caller removes its argument. Prologue
`55 8b ec 83 c4 a0` reserves 96 local bytes; saved EBP/EBX/ESI/EDI add 16,
and the largest outgoing argument list adds 12 (124 bytes below entry ESP,
before the nested CALL's return address). EBX/ESI/EDI/EBP are preserved;
there are no x87 instructions. Both EAX and EDX are scratch results, detailed
below. No-track return preserves the incoming EAX/EDX completely.

The CODE scan finds two direct calls, checked at instruction boundaries:
41a53e (file 19b3e), inside `ground_update` 41a360, sets only AX=1 at 41a544
then returns the effects' high EAX word and full EDX; 41e4e8 (file 1dae8),
inside 41e488, passes both registers untouched to the immediately following
`ground_collide` call at 41e4ef. Neither is an argument of that cdecl call,
and 418f74 overwrites both before use. This does not establish completeness
of indirect callers.

All offsets below are hexadecimal. C=[A+1f2] is captured once; T=[A+22c],
M=[A+34], L=[M+4], S=[A+238]. Addresses and arithmetic wrap modulo 2^32.

| State | Width and use, confirmed by instructions |
| --- | --- |
| A+22c, +1f2, +34, +238 | u32 guest pointers (4195d4, 4195e1, 419632, 419618); S and its element are re-read after transform and before heading writes |
| A+23c | s16 loop bound, re-read each iteration (4198dd); counter is a wrapping word starting at 0 |
| T+16, T+4 | word animation IDs; +16 captured, +4 re-read only while captured ID is ffff (419602, 419790) |
| C+77+i | s8 part selector, sign-extended to low word of the pushed argument; upper word comes from the address C+77+i (419628..41963c) |
| C+72+i | byte category, compared with 0 or sign-extended and compared with 2 (4197a0, 4197c9) |
| L+0, vtable+20 | u32 vtable/function pointers; cdecl method(L, selector) returns an entry or null (419640..419646) |
| entry+4, M+16 | s16 record index and u32 record base; negative index or null uses record 6c56a8, else base+32*index (41964f..419678) |
| record+14/+18/+1c | three s32 point components, passed to transform; +18 also read as s32 threshold coordinate (419678, 41981e) |
| A+0c/+0e/+10, +12..+31, +32 | Euler words, cached 32-byte matrix, u16 valid word; when valid=0 call Euler(A+0c,A+12), then store valid=1 (419687..4196aa). Euler leaves its padding byte as in [M: rotation and composition contracts](#m-rotation-and-composition-contracts) |
| S+4*i, attached+26/+2a/+2e | u32 object pointer; transformed s32 position written **Z, X, Y** (4196d1..4196e5) |
| attached+10, +32 | word heading copied from A+10, then valid word cleared (4196e8..419708) |
| A+26/+2a/+2e; [4d24ea]+4/+8/+c | s32 actor and reference position; subtract modulo 2^32, feed XYZ deltas to 47de72 (41970e..41975d) |
| A+a3 | byte, nonzero bypasses distance gate and enables feedback; re-read after sound (419776, 4198bb) |
| C+46,+0e,+7c,+12,+14,+10 | word animation IDs, tested in this order (419797..41980c) |
| A+2a0 | s16 rate, positive selects forward threshold pair, zero/negative reverse (4197ea) |
| C+ac+2*k, C+98+2*k | s16 arm/fire thresholds, k=0 forward, 1 reverse, 2 other selected animation (419831/419848, 419877/41988e) |
| latch pointer F | starts A+2b4; u8 read/write. Special category 2 redirects F to A+2b8; arm writes 1, firing writes 0 (4197ac, 419858, 4198c9) |

No direct global writes. Sound/feedback have additional state outside this
table, which is why compare is refused. The written graph is variable size:
the actor cache, latch bytes starting at A+2b4 (or redirected to +2b8), and
every reached attached object's transform. No fixed maximum count is
proved; fixed spans cannot describe the complete graph.

Operation order:

1. No track: return unchanged registers. Otherwise F=A+2b4, ID=T+16,
   selector=C+77, category=C+72. Signed count <=0 exits without calls.
2. For each nonnull attached object: virtual +20, choose record; optional
   Euler and cache-valid write; transform(record+14,A+12,local point);
   attached Z/X/Y writes, heading copy, cache clear. Then distance(delta X,
   delta Y, delta Z). `near` is the **signed** full result <15000. A+a3=0
   and !near skips effects without advancing F.
3. If ID=ffff, load T+4 into ID, retaining it across later iterations.
   Start sound ID at ffff. ID=C+46 skips normal threshold logic; category=2
   additionally redirects F to A+2b8 and writes only unused local k=3 and
   world Z. Sound ID stays ffff, so this path never fires.
4. Otherwise category!=0 also skips normal logic. With category=0, the first
   match of C+0e/+7c/+12/+14 selects k=0 if rate>0, otherwise k=1 and reverse.
   Failing these, ID=C+10 selects k=2, forward. No match advances F and skips.
   Coordinate is record+18. Arm F=1 if forward and arm<coordinate, or reverse
   and arm>coordinate. Set only AX=001d, preserving arm threshold's high word.
5. If AX!=ffff and F!=0, fire if forward and fire>coordinate, or reverse
   and fire<coordinate. If near: sound 4628e0(29,A+26). Then re-read A+a3;
   if nonzero call 434144() with no arguments. Clear F **after both calls**.
   Normal/disabled-category paths advance F once. Null attached and distant
   unflagged paths do not advance F. Every iteration advances the selector,
   category and word counter, then re-reads the signed bound.

All 30 conditional sites (taken condition and target; fallthrough is the
negation):

| Site | Taken condition | Target |
| --- | --- | --- |
| 4195db | [A+22c]=0 | 4198ea |
| 419622 | S[i]=0 | 4198cf |
| 419653 | lookup result=0 | 419673 |
| 41965d | s16 entry index<=-1 | 419673 |
| 41968c | cache valid!=0 | 4196b0 |
| 41977a | A+a3!=0 | 419784 |
| 41977e | near=0 | 4198cf |
| 419788 | ID!=ffff | 419794 |
| 41979b | ID!=C+46 | 4197c6 |
| 4197a6 | s8 category!=2 | 41985f |
| 4197cc | category!=0 | 41985f |
| 4197d6 | ID=C+0e | 4197ea |
| 4197dc | ID=C+7c | 4197ea |
| 4197e2 | ID=C+12 | 4197ea |
| 4197e8 | ID!=C+14 | 419808 |
| 4197f2 | s16 rate<=0 | 4197fc |
| 41980c | ID!=C+10 | 419816 |
| 41982b | reverse | 41983e |
| 41983c | s16 arm<s32 coordinate | 419855 |
| 419842 | forward | 41985b |
| 419853 | s16 arm<=s32 coordinate | 41985b |
| 419863 | AX=ffff | 4198cc |
| 41986b | latch=0 | 4198cc |
| 419871 | reverse | 419884 |
| 419882 | s16 fire>s32 coordinate | 41989b |
| 419888 | forward | 4198cc |
| 419899 | s16 fire>=s32 coordinate | 4198cc |
| 41989d | near=0 | 4198af |
| 4198bf | A+a3=0 after sound | 4198c6 |
| 4198e4 | s16 counter<s16 [A+23c] | 419614 |

Return bits (4198d9..4198f0): every loop test replaces AX with the counter
and leaves upper EAX/EDX unchanged. Initially EAX=A+2b4, EDX=C+72. An empty
iteration sets EDX=sign-extended index and leaves EAX's upper word from the
previous test. Distant skip leaves EAX=A+92, EDX=0. Disabled category or
unmatched animation leaves EAX=ffffffff, EDX=near. Normal threshold path
leaves high EAX from the sign-extended arm threshold, EDX=near. Firing
leaves EAX=EDX=A+92 if feedback is skipped, otherwise both come from 434144.
Sound return registers are overwritten before feedback. Finally AX is the
exit counter, regardless of those paths.

Callees: 47ebb8 (`euler_matrix`), 48043c (`transform_point`) and 47de72
(`dist3d_delta`, the signed wrapped 3-D metric of `core/geometry`: sorted
magnitudes L/M/S give L+(M>>2)+(M>>4)+(M>>5)+(S>>2), separate arithmetic
shifts, 47de72..47deb9) are hooks. Virtual +20 maps the signed-byte selector
to a nullable entry with a signed index; class and semantic identity are
hypotheses. 4628e0 (`sfx_play_at`, [audio.md](audio.md)) checks 49f8ec,
uses tables 4d2b78/4d2c78, optionally calls RNG 492f24 on 4d260b, then
46299c(id,point), and on success 464038 (4628e8..462939); positional sound
is the working interpretation. 434144 is original: calls 429820, checks
returned object's +14 against 4, calls 42d854, clears 49b638, calls clock
467828, writes 49b634/49b638 and calls 42d820 with 10 shifted by the low
five bits of word 4d253c (434144..434191). Its state-4 early return leaves
EDX unchanged: 429820 only loads EAX from 4cf99c (429820..429825), so the
bridge enters feedback with EDX=A+92. On the other path 42d854 subtracts
dword 4cfcb8 from [[6c5f70]+224], then clears 4cfcb8; 42d820 clears it
again and returns EAX=0 (42d820..42d86d). Cockpit feedback is a hypothesis.

**Implementation.** `core/effects` owns the control flow and fixed-width
arithmetic; callbacks call the five direct callees and the virtual dispatch
at their addresses. Two bridge requirements: 434144 is entered with
EDX=A+92, and `ground_update` forwards the last collision/clip/rollback
EAX/EDX pair to effects, so the no-track return is the same whether update
and effects are original or replaced. Compare mode is refused: sound and
feedback cannot run twice, and `-Hooks "ground_effects=compare"` is
rejected, not run twice.

**Validation.** In vitro (`effects_cases.h`): 1,001,024 seeded/boundary
cases match full EDX:EAX, arena/guards, ordered callback arguments/results
and counter deltas; all 30 branch sites have both outcomes. A further 256
states exercise all four update/effects original/replaced combinations,
including the no-track return. A real Euler/transform/distance/feedback
probe checks the state-4 434144 early return. Three mutations are caught:
distance `<15000` changed to `<=`, dropping high EAX at the loop test, and
supplying zero entry EDX to feedback. Live: identical all-replaced
regression, calls in all suites (hooks.md table). Real attachment counts,
virtual targets and the frequency of the second caller 41e488 are not
measured.

## Track clip selection 4792ac

Hook `track_clip` (`core/animation`, `compare_unsupported`). Bytes
4792ac..479341 (file offsets 788ac..78941), branchless, cdecl with four
dword slots `(T, clip, frame, phase)`, using the low word of each numeric
argument. No direct global access or x87 instruction. It saves EBP/EBX, has
no locals, and uses at most 20 bytes below entry ESP before a callee's
return address (8 saved + 12 outgoing). Signature `55 8b ec 53 8b 5d 08`,
seven relocated bytes, lengths 1/2/1/3.

Order (instruction addresses are the evidence):

1. 4792b7/bf/c7 write words T+4=clip, T+6=frame, T+8=R16(T+4).
   4792cb..df calls `[R32(clip_object)+20]` with `(clip_object,
   (entry_EAX & ffff0000) | R16(T+6))`. The object is
   `R32(R32(R32(T)+6) + 4*s16(T+4))`. Index/address arithmetic wraps
   to 32 bits; the original does not check bounds. The upper frame slot
   comes from entry EAX, not from the frame argument's high word.
2. 4792e2 writes the returned AX to T+a; 4792ed writes phase to T+1c.
   4792f1..30b rereads T, resource+6, signed T+4, signed T+6 and
   clip_object+c (all pointers dwords), then writes duration word
   `[R32(clip_object+c) + 6*s16(T+6)]` to T+1e. These are fresh reads
   after the virtual call, including a possibly changed resource pointer.
3. 47930f..31a rereads T+6 and T+4 as signed words and calls 478ef4
   `(T, sign_extended_clip, sign_extended_frame)`. This is the
   `clip_pose` hook, independently selectable; its own T+20..2d writes
   remain at this point in the call sequence.
4. 479325/329/32d/333/339 write words T+18=ffff, T+16=ffff, T+1a=0,
   T+c=ffff, T+50=1, in that order. 479322..341 does not change EAX/EDX:
   the complete return is the pair returned by 478ef4, with no truncation.

The virtual slot is delegated unchanged through the guest-call callback.
For the ANSequence/ANCyclicSequence vtables it is the next-frame method
(478760/4787e4); other classes stay original. A fixed T span 0..51 covers
this body's writes and the known pose writes, but cannot isolate arbitrary
virtual side effects, so live compare is refused. There are no conditional
coverage sites.

**Validation.** In vitro (`clip_cases.h`): 1,000,025 direct differential
cases plus 256 installed replacement cases, including `ground_update`'s
clip bridge, match full EDX:EAX, the guarded arena and both callees' ordered
full arguments, results and track-entry snapshots. The portable unit test
covers 25 signed boundary pairs on x86/x64. A mutant that drops entry EAX's
high word is caught. Live: identical all-replaced regression with calls in
all suites (hooks.md table); its callers include `track_seek` 479344
([4793d4 track_route](#4793d4-track_route) and following). The transition
branch of `ground_update` that calls it is reached in no suite.

## Collision virtual targets (+70 hit, +74 part damage)

Vtable dwords below were read at their actual VA+slot; constructor stores
and bodies were disassembled. These are descriptive class labels, not
recovered C++ names.

| Class / provenance | Vtable VA | +70 | +74 | Evidence and limit |
| --- | --- | --- | --- | --- |
| Vehicle actor, selected-player candidate | 49a282 | 4187d0 | 417de4 | Constructor 415bb0 stores it at 415c60 after base table 49a54c; its only direct caller 4256a4 stores the resulting object in 4a9b84 at 4256d5 and sets A+a3 at 4256e7. Actual live player vtable still needs a memory observation. |
| Building actor base | 497940 | 404f20 | 404d70 | Construction at 405486, type word A+4=5 at 40548f, A+1a8=1 at 405498; allocator pool 4a95a0 at 4053bc/4053c2. |
| Building variants (names unresolved) | 4979d4, 4978ac, 497818, 497784 | 404f20 | 404d70 | Final constructor stores 4054ab, 4055e1/405825, 405836, 405989; all five tables have the same two targets. |

The building designation is supported by the shared pool 4a95a0 used by
404ae4 (static-obstacle path). The exact building subtype selected by each
mission record, and the live player-to-49a282 identity, remain **hypotheses**.
Do not confuse 49a262 (attached objects written at 415eb6 into A+238's
array) with the vehicle's root vtable.

**Interfaces:** +70 is cdecl `(A, damage_word, point12, range_word,
source_actor)`; +74 is cdecl `(A, part_word, damage_word, source_actor)`.
Numeric words are signed where extended; all argument slots remain dwords.
Each body uses plain RET and preserves EBX/ESI/EDI/EBP. Their EAX/EDX are
scratch, not a normalized success flag. They have no x87 instructions.

| Target | Behaviour / fields (instruction evidence) | Hook |
| --- | --- | --- |
| 4187d0..4188c4 | Heading from 492934(point,A+26), subtract A+10 as a word, saturate abs(-32768) to 32767, front selector abs(angle)<4000h (4187e4..41881c). 413c68(A+222, selector, damage) returns remaining low-word damage. If positive, visit 29 word flags at [A+20e], consume RNG 4d2599 for each enabled part, keep `(AX & 0fff)<0802`, ask V58(A,part,point_out), and if distance<signed range call V74 with signed division `((range-distance)*s16(damage))/s16(range)` (418839..4188bc). | `vehicle_hit` ([Damage path](#damage-path)); all callees delegated. |
| 417de4..4187cf | A+a3 and 4240f4 guard; zero [A+20e][s16(part)] exits (417dff..417e22). Reads equipment graph A+202, damage graph A+206 and config A+1f2; writes global 4a964c=config+212 before 40da38 (417ed8..417f00). Later paths change actor flags, A+1a4, callback at A+4d, attachment list A+238/count A+23c and even A+2e (4182d8,41829b,4185ef,418653). Normal exit passes through 409904 at 4187c4; two initial guarded exits bypass it. | `vehicle_part` in [weapons.md, Vehicle part 417de4](weapons.md#vehicle-part-417de4); all lifecycle and virtual calls delegated, compare refused. Destruction, scoring and notification names remain hypotheses. |
| 404f20..405018 | Config+1e guard, additional config+4/38 and state [A+205]+5/A+99 guards (404f2c..404f79). Iterate s16(config+12) parts through [A+201]. Enabled parts consume RNG 4d2599; the masked 0..4095 result is compared to 4100, hence always accepted, but the RNG advance remains (404f94..404fb0). V58 produces a point, 49288c measures it, and V74 receives the same signed range falloff as the vehicle (404fba..404ff6). | `building_hit` ([Damage path](#damage-path)); all callees delegated, mandatory RNG advance retained. |
| 404d70..404f1f | Config+1e and enabled-part guards; part -1 becomes 0. Adds s16(damage) to a signed word in an 11-byte state record [A+205]+11*part, checks a threshold in a 30-byte config record [config+14]+30*part (404d8d..404dda). Between half and full threshold it makes RNG trials; crossing sets source at state+7, clamps the word, clears [A+201][part], invokes lifecycle methods, and may set state+5 from table 49741c and state+3=300 (404e65..404f10). Otherwise only the damage word accumulates. | `building_part` ([Building part damage 404d70](#building-part-damage-404d70)). |

Direct/virtual call graph (unique edges; call order is not implied by this
condensed graph):

```text
vehicle V70 4187d0 -> 492934, 413c68, 492ee0, V58, 49288c, V74
vehicle V74 417de4 -> 4240f4, 40dbc0, 40da38, 420a68, V68, 417bec,
                     478628, V60, 411bc8, 423430, 413e50, 40d9f8,
                     recursive V74, 409904
building V70 404f20 -> 492ee0, V58, 49288c, V74
building V74 404d70 -> 47e068, 47e08d, 492ee0, V40, 420a68, V60,
                      411bc8, 423430
```

492ee0 is `rng_next` and 49288c is `dist_gate` (three-axis contract in
hooks.md). The ground collision reaches +70 only through an object contact
(step 3 of [Ground collision 418f74](#ground-collision-418f74)), which no
suite reaches; the building collisions of `c1_01-collision` end in 404ae4.
The live calls of these hooks in the hooks.md table come from other callers.

## Sound selection 462830 and the isolation boundary

Body 462830..4628dd, file 61e30..61edd; replaced as `sfx_play`
([audio.md](audio.md), sound/clock isolation). cdecl one full-width sound
index; EBP/EBX/ESI saved, no locals, maximum 24 bytes below entry ESP before
a callee return address. No direct x87 or memory writes. Three conditional
branches: 46283f (context zero), 462854 (variant count <=1), 462872 (gate AL
zero). Address/index operations wrap to 32 bits; there is no bounds check.

1. If dword 49f8ec is zero, return the incoming EDX:EAX unchanged
   (462838..46283f, 4628da..dd).
2. Read a definition pointer from `4d2b78 + 4*index`, then its unsigned
   byte +6. For count>1, call 492f24(4d260b,count), sign-extend its returned
   AX, and add it to the index (462845..462867). This is the separate
   sound RNG, not the collision RNG 4d2599; its state advances exactly once
   here.
3. Call 462784(index). If AL is nonzero, reread the definition pointer and
   unsigned byte +1, call 47e08d(byte,65000), and take the signed low word
   of its EAX. Otherwise use zero (462869..46289b). 47e08d is the signed
   64-bit product shifted right 16 (47e091..47e097), not Q10/Q8.
4. Reread the definition pointer, multiply that signed low word by unsigned
   byte +8, and signed-divide by 100, truncating toward zero
   (462894..4628aa). Call 464618(context from 49f8ec,
   handle from `4d2c78+4*index`, quotient). Then reread context and handle
   and call 464038(context,handle), even when the gate produced zero
   (4628ac..4628d2). Return its complete EDX:EAX.

Subordinate effects:

| Boundary | Evidence / state | Consequence for isolation |
| --- | --- | --- |
| 462784 gate | 462787..4627c4 checks context and signed index<10, then dwords 49f8f4 or 49f8f8. With a context it returns EAX=EDX=0/1; without one it only writes AL=1. | Capture gate state and results; zero amplitude is not a skipped play. |
| 464618 parameter update | 464623..464669 validates handle != -1 and the full handle at `[context+38]+40*u16(handle)`. The payload at record+4 receives the quotient at payload+10. Payload byte+5 bit0 may call 473aec. | The referenced slot graph and backend effects belong to the call, not only global tables. |
| 464038 play | 464045..464115 validates the 40-byte slot and the associated 540-byte resource record in [context+34]. May load via 463d4c, then calls 473898. Success increments context+3c or +40 and sets payload flags bit100. | Handle generation, loading, counters and flags happen exactly once. |
| 473898 / 473aec backend | 4738c5,4738d5,4738e9,473985,473b0f,473b47 reach wrappers which call runtime pointers 6cbe7c,6cbe68,6cbe80,6cbe28,6cbe98,6cbdec respectively (loads at 496034,495fd5,496047,495e8a,4960df,495d2f). 473913..47399d and 473b16..473b47 read master level 4a0e30 and mutate backend payloads. | External audio state is outside fixed compare spans; no backend call can be replayed twice by restoring game memory. |

The split between own code and the delegated backend, the result registers,
the absence of clock reads in the call closure and the stream-B RNG draws
are in [audio.md](audio.md) (sound/clock isolation). Compare mode stays
refused.

## Damage path

Contracts derived with llvm-objdump from the reference `VER95/DBSIM.EXE`.
CODE file offsets are VA minus 401000h plus C00h (for example 4187d0 ->
183d0). Statements below describe instructions, not measured live class
identities. All routines are cdecl, preserve EBX/ESI/EDI/EBP, use plain
RET, and contain no x87 instructions. Arithmetic wraps at its stated width;
`s16/s32` mean signed interpretation. Valid pointers are required; bounds
checks and divide-fault recovery are not present in the originals.

Hooks: `dist3d_delta`, `dist3d_forward`, `heading_point`, `shield_damage`
(compare supported), `vehicle_hit`, `building_hit`, `building_part`
(`core/damage`, compare refused). The vehicle +74 body 417de4 is
`vehicle_part` ([weapons.md](weapons.md#vehicle-part-417de4)); the V58
bodies are `part_position` 41b60c and `base_part_position` 406808
(hooks.md table). Live: identical all-replaced regression; calls per the
hooks.md table.

### Leaves and bounded state

| Body | ABI, memory and result | Branches, calls and stack below entry ESP |
| --- | --- | --- |
| 47de72..47deb9, `dist3d_delta` | Three dword XYZ deltas. No memory access except stack arguments; no writes. Absolute values wrap (INT_MIN remains negative); sort them by **signed** comparison into largest L, middle M, smallest S. EAX = L + SAR(S,2) + SAR(M,2) + SAR(M,4) + SAR(M,5), modulo 2^32. EDX is the original Z sign (0 or ffffffff), from CDQ at 47de8e, the last EDX write in the metric. | JGE at 47de95 (Xabs>=Yabs), 47de9b (current middle>=Zabs), 47dea0 (after exchanging Z, largest>=middle). Separate shifts/additions 47dea4..47deb5 must not be rounded as one product. No callee; 8 bytes (ENTER plus EBX). |
| 492aaf..492ac7, `dist3d_forward` | Same three arguments, unchanged EDX:EAX from 47de72. No non-stack reads/writes. | Branchless wrapper; sole call at 492abe goes **to** 47de72, not the reverse. 16 own bytes at call, plus callee frame/return address. |
| 492934..49295a, `heading_point` | Two point pointers P,Q; reads dwords +0/+4 only. Wrapped dx=P.x-Q.x, dy=P.y-Q.y. 49290c changes dx to 1 only when both deltas are zero, then 492ac8 calls 47d32c (`angle_ratio`). EAX has low word angle+ c000h; high word remains zero, EDX is angle_ratio's full scratch result. Reads the 513-byte ratio correction table 4a1c34; no writes. | No branch in this body; 492919/49291d are the zero-delta wrapper's two JNE sites. Calls 49290c at 49294b, then 492ac8 at 492926, then 47d32c at 492ad3. Own stack 20 bytes, wrapper 20, forwarding wrapper 12, plus return addresses and angle_ratio frame. Same valid division/table-index domain as angle_ratio; extreme wrapped inputs can fault. |
| 413c68..413cc0, `shield_damage` | `(S,front_word,damage_word)`, where caller supplies A+222. Select S+0 if front low word nonzero, else S+2; read/write **one word**. Q = signed product 1000*s16(damage) shifted arithmetically 10. Subtract low Q from selected word modulo 65536. If s16(result)>=0 return EAX=0 and EDX=high signed product. Otherwise set word=0; EAX=s16((-s16(result)*1024)/1000), EDX=nonnegative division remainder. Other shield word untouched. | JE 413c79 selects rear; JGE 413c9c selects fully absorbed path. Calls multiply_q10 at 413c8b before subtract. Own stack 24 bytes. **Stateful, isolatable**, not a pure leaf: four-byte S span contains every possible write. |

Direct call **sites** (whole CODE disassembly, no matching stored function
pointers in initialized data for these four entries):

- 47de72: 403443,40a7de,40c70c,40c78f,41975d,41a2d9,41f337,421b7a,
  4274d5,4289e2,429140,4291aa,42920a,42927f,4292f5,43be68,44edc5,
  4629f1,46c269,46c28b,476a05,492abe.
- 492aaf: 47d88d,47d998,492a8f. It is separately called and has its own hook.
- 492934: 41216a,412aea,416669,416777,4187e4,41ad65,41adc5,41c31b,
  41c692,41c816,41da80,41dba1,41dd8f,41ddab,41de62,41df84,41dfa1,
  41dfbd,41e05c,41e42f,41e60e,41e664,41e6af,41e77e,41e9b1,41fad8,
  41fc07,422564,4225fb,4226d4,42278d,42612d,4269ad,4269d2,433273,
  43329e,43354d,46a9a4,46aa20,46abcc.
- 413c68: 418822 only.

All four are bounded hooks with compare support. Their portable
implementations compose existing arithmetic in core; the two hit bodies
below delegate every game callee, including these leaves.

The `dist_gate` 49288c hook returns the full EDX: 4928c4 -> 492a7d ->
492aaf -> 47de72 propagates the sign of the wrapped Z delta (0/ffffffff);
this matters on `vehicle_hit`'s distance-skip path. Its million-case
generator checks EDX as well as EAX.

### Hit control flow

+70 ABI: `(A,damage_word,P,range_word,source)` in five dword slots. Both
reserve 12 scratch bytes for V58's output and save three registers plus
EBP: 28 bytes, maximum 44 with outgoing V74 arguments, excluding callee
return address and callee frames. Neither body writes non-stack memory
directly; delegated callees own all effects. Vtables and part flags are not
snapshotted across calls unless specifically noted.

**4187d0..4188c4 (`vehicle_hit`).** Read A+10 (heading u16), A+20e
(pointer to 29 word flags), and current A vtable on each virtual call.
Call order and branches:

1. 492934(P,A+26) at 4187e4. Subtract A+10 from AX modulo 65536.
   4187f4: if result=8000h, absolute magnitude=7fffh; otherwise
   4187ff tests signed nonnegative and negates the low word when negative.
   Front is signed magnitude<4000h (SETL 418819). High EAX from heading
   survives the subtraction and replacement of AX with incoming damage.
2. 413c68(A+222,front,(heading_EAX & ffff0000h)|u16(damage)) at 418822.
   Its AX replaces the low word of the local damage argument; 418833 exits
   if s16(AX)<=0, returning its full EDX:EAX unchanged.
3. Capture [A+20e] once, loop full dword part=0..28 (signed word loop J L
   at 4188bc). 418845 skips zero flag words. Nonzero flags call
   492ee0(4d2599) at 41884c, then EAX=AX&fffh, EDX=(EAX<802h).
   418868 skips when EDX=0. A skipped flag preserves the previous pair.
4. V58(A,part,scratch12) at 418872; then 49288c(scratch12,P) at 418880.
   418890 skips when s32(distance)>=s16(range); this path returns/retains
   EAX=s32(range), EDX=distance callee's EDX, not its EAX.
5. Otherwise compute wrapped u32 `(range-distance)*s16(remaining)`;
   **CDQ at 4188a2 replaces IMUL's high product**, then IDIV by s16(range),
   truncating toward zero. V74(A,part,full_quotient,source) at 4188ae.
   Retain its complete EDX:EAX. Subsequent disabled flags do not alter it.

Resulting returns: the positive-damage/all-flags-zero path retains the
shield pair; a failed RNG test returns `(masked,0)`; a failed range test
returns `(s16(range),distance_EDX)`; V74 results survive the loop. A second
independent disassembly of 47de72 and 4187d0 found no branch or
return-value difference.

**404f20..405018 (`building_hit`).** Read A+1f2 (config pointer C),
A+205 (state pointer S), A+99 byte, A+201 (word-flag pointer), and current
vtable. The guard reads C+1e u16, C+4 i16, S+5 i16, C+38 u32. The loop
rereads A+1f2 and signed C+12 every iteration, including the exit test;
its flag pointer is captured once and advances by two.

- 404f37 exits when C+1e!=0, EAX=C and **entry EDX** survives.
- 404f48 skips the intermediate guard when C+4=-1; 404f55 skips it when
  S+5>=2. Otherwise EAX=A+92, and 404f61 tests A+99!=0. 404f6f exits if
  C+4!=-1 && S+5<2 && A+99!=0. EDX=C throughout this guard; EAX is C or
  A+92 depending on whether the byte was tested.
- 404f79 exits if C+38=0 with that same pair. Otherwise loop while
  part<s16(current C+12), J L 40500c. Zero/negative counts do no work.
- 404f8d skips zero part flag. Every enabled flag calls 492ee0(4d2599)
  at 404f94. Masked EAX=AX&fffh is **always <1004h** (404fa2..404fb0),
  so 404fb0's taken edge is statically impossible for every RNG return.
  The RNG advance is still observable and required.
- V58 at 404fba, distance at 404fc8, signed distance>=range skip at
  404fd8, and V74 at 404ff6 follow the vehicle sequence, using incoming
  s16(damage). CDQ at 404fea again discards the high product. No positive
  damage guard exists. Zero range can divide-fault if distance is negative;
  INT_MIN/-1 can overflow. Defined-domain tests exclude or trap these.
- Every loop test overwrites EAX=current C, EDX=s16(current C+12).
  Thus every loop exit returns this pair, even after a V74 call.

No direct callers of either body were found. Initialized-data references:
vehicle table 49a282 +70=4187d0/+74=417de4/+58=41b60c; building tables
497784,497818,4978ac,497940,4979d4 share +70=404f20/+74=404d70/+58=406808.
All matching pointer occurrences are these table slots. Dynamic assignment
of additional vtables remains a hypothesis. Function-body detours cover
all these tables without changing any vtable.

Own arithmetic/control flow, original game callees through the guest
adapter; compare refused because virtual methods, RNG and lifecycle state
are not a fixed isolated span. Scratch registers are preserved on every
exit, and the full dword arguments, not only semantic low words.

**Stack argument slot.** 418827 writes AX into its incoming damage slot at
EBP+c; the replacement retains this value locally. Every decoded +70
caller discards all five outgoing slots: 41895d,419376,41938d,426a96 add
ESP,14h. None reads the overwritten slot before that cleanup (426a90 only
writes its own EBP-a flag). The guest API does not expose expired callee
argument storage or permit point buffers aliasing it. This is a
caller-domain restriction, not an actor-state write.

**Indirect candidate call sites**, grouped by slot (every decoded
`call [register+slot]` in CODE):
+58: 404fba,4068ac,40a2fd,40e530,4106cd,418872,41b569,41ea1b,41ead6.
+70: 41895a,419373,41938a,426a8d.
+74: 4038e6,404ff6,4051e9,411b82,417999,4179d0,417a91,4184fa,4188ae,
41893b,419500,41951f,41953e,41955d,41957c,41959b,41e537,421dba,
460138,460197.
A matching slot alone does **not** establish the receiver's runtime class;
binding these callers to particular table/body pairs requires a live vtable
observation. Computed calls using other encodings are not ruled out by this
inventory. The ground_collide sites are 419373/41938a (+70) and
419500/51f/53e/55d/57c/59b (+74).

### Original point and part-damage boundaries

**V58 ABI** `(A,part_word,out12)`; both tables above, no direct call sites
or other initialized-data references found. Vehicle 41b60c uses 28 local
bytes and saves EBP/EBX/ESI/EDI (44, max 56 with outgoing args). Building
406808 uses four local bytes (20, max 32). Neither is a pure leaf: both
update actor matrix caches and invoke transforms; vehicle V58 additionally
traverses a manager and V20. Both are hooks of other groups
(`part_position`, `base_part_position`; compare_mode.md lists their
compare requirements).

| Target / instruction sites | Boundary contract |
| --- | --- |
| 41b60c..41b726 | R pointer A+1fa, then its first pointer; region = base+18*s16(part). Region+e is center pointer; +c signed part word. If center null (41b636), copy A+2e, A+26, A+2a dwords to out+8,+0,+4, in that order; EAX=Y, EDX=Z. Else if region+c>0 (41b655), call V20([A+34]+4,zero-extended part), use default record 6c56a8 if returned pointer null (41b680) or returned s16(+4)<=-1 (41b68a); else record=[manager+16]+32*index. If record nonzero (41b6a7), call 4801d4(center,record,local12), ensure A+32 cache via 47ebb8(A+c,A+12) if zero (41b6c2), W A+32=1, then 48043c(local12,A+12,out). If no record, ensure same cache (41b6f4), then 4801d4(center,A+12,out). Return full last transform EDX:EAX. W out12 and cached matrix A+12..31/A+32 word, plus unknown V20 effects. |
| 406808..406865 | If A+32 word zero (40681d), call 47ebb8(A+c,A+12), W A+32=1. Then R C=[A+1f2], records=[C+14]; call 4801d4(records+30*s16(part)+10h,A+12,out). Return complete transform pair. Matrix cache and out12 are the writes; calls Euler and transform. |

4801d4 consumes a **six-byte signed-word point**, not a 12-byte input
(4801f4,4801fa,480204,480209); its output is three dwords. Thus V58's COL
centers and building record+10h are i16 XYZ; only its local/world output is
12 bytes. Cached rotation is nine signed words plus a mode byte;
translation is three dwords.

**+74 ABI** `(A,part_word,damage_word,source)`; no direct body calls,
vtable references above. Their transitive callee writes are not claimed
bounded.

**404d70..404f1f** (`building_part`, see also
[Building part damage 404d70](#building-part-damage-404d70)): locals 20
bytes, saved EBP/EBX/ESI/EDI (36), max 48 with outgoing args. R
C=[A+1f2], flags=[A+201], states=[A+205]. 404d87 exits for C+1e!=0 with
entry EAX, EDX=C. Part -1 becomes zero (404d92); 404da9 exits for zero
flags[s16(part)], EAX=s16(part), EDX=C. Set S=states+11*part,
D=[C+14]+30*part; read signed words S+0, D+0 and incoming damage, sum in
s32. Threshold<=sum (404de7) marks destruction; otherwise threshold>>1>=sum
(404df1) bypasses trials. Between half and full, call 47e068(10,threshold),
then 47e08d(old,scale), 47e08d(sum,scale). Loop with postincremented old
bucket<new bucket (404e63), call RNG each iteration; `(AX&fff)<19a`
(404e4f) marks successful trial and exits. 404e69 selects destruction vs
accumulation.

Destruction writes S+7 dword=source, S+0 word=D+0, flags[part]=0, then
V40(A). Only when AX=100h (404e96) does the following lifecycle block run.
If source==[4d24e6] (404ea0) and A==[source+1a4] (404ea8), call 420a68(2e).
If source nonnull (404eb6), call source.V60(source,A,0). Write A+99=1 and
A+96=0 bytes; call 411bc8(A), then 423430([A+1b6]) if pointer nonzero
(404ee8). Read D+4 word; if not -1 (404ef9), read signed-index word
49741c+2*s16(D+4), W S+5 to that word, S+3=300. Return EAX with AX=D+4
and high half from preceding path; EDX=s16(D+4) on lookup, otherwise
previous path EDX. Accumulation writes S+0 += incoming damage modulo
65536 (404f16), returns prior EAX with AX=damage and prior EDX (threshold
comparison or last trial/bucket). These scratch values depend on delegated
callees, not a boolean success result. Runtime-indexed graph writes and
lifecycle effects make compare unsafe.

**417de4..4187cf** (`vehicle_part`, implemented and validated in
[weapons.md](weapons.md#vehicle-part-417de4)): locals 124h, saved
EBP/EBX/ESI/EDI (308 bytes), max 324 with outgoing arguments. Initial
guards: if A+a3 byte nonzero (417e03), call 4240f4(); AX!=0 (417e0d) exits
with its pair. Otherwise R [A+20e][s16(part)]; zero (417e22) exits
EAX=[A+20e], EDX=s16(part). All other exits call 409904 at 4187c4 and
return its full EDX:EAX.

Ordered phases and conditional sites (all word ratios use signed state,
negative -> 100h, else `(state<<8)/limit`; ratios subsequently narrowed to
words):

1. Save attachment damage: iterate s16([A+202]+8), pointer list at
   [A+202]+4 (417e81); null entry skip (417e45); entry+18 points at GL,
   signed GL+17 byte +19 selects 40dbc0(A+206,region). Save s16(AX) in
   local dword array. If A+a3 (417e93), save ratio for state offset 8;
   negative-state branch 417eba. Set dword 4a964c=[A+1f2]+212; call
   40da38(A+206,part low word,damage low word), preserving upper slot bits.
2. If A+a3 (417f18), recompute ratio (417f31); previous<100 and
   new==100 (417f52/59) calls 420a68(12); otherwise previous==0 and
   new!=0 (417f69/6d) calls 420a68(3). Iterate attachments again
   (418027), skip null (417fa1), call 40dbc0 then attachment.V68(entry,A,
   saved,new). If A+a3, saved!=100, new==100 (417fef/ff7,418000),
   set local notification; 41800d calls 420a68(10) when set.
3. Call 417bec(A). Unless config+50 word nonzero (41803f), read signed
   config+4a count; ratio state offsets 0,2 (418069/09a), and if count>2
   (4180ba) offsets 14,16 (4180e5/128). For each count (418192), if
   ratio==100 (418150) and attachment pointer [A+238][i]!=0 (41815c),
   call 478628([4a9691],attachment), then clear that pointer dword.
4. Unless A+a4!=0 or count==0 (4181a4/1af), take half-count; if count=4
   (4181d8), average each pair of ratios arithmetically. Count ratios==100
   (4181fe/220); at least half (41820c) marks disable. 418227 selects it.
   If A+99==0 (41823d), optional source.V60(source,A,0) (418243),
   optional 420a68(2f) for source==player and player's +1a4==A
   (41825d/26b); 411bc8(A), optional 423430(A+1b6) (418283), then
   413e50(A+4d,4998ba). W A+a4=1. If A+1a4 nonzero (4182ba/2c7),
   decrement target+1a2 word, clear A+1a4 dword, set A+9d=1. The
   post-clear increment guard 4182e0 is always taken (ECX=0).
5. If not disabled, either signed pair>140 (418308/30f) sets A+a9=1,
   with 420a68(13) only for A+a3 && !A+a9 (418327/33f); else either
   >80 (418365/36b) sets A+a8=1, with 420a68(8) only for A+a3 &&
   !A+a8 (418383/39b).
6. If A+99==0 (4183d1), test 40d9f8(A+206,0), then (...,1)
   (4183eb/405); either AX!=0 triggers destruction. Otherwise ratio
   state+12 (418430/455) or state+10 (41848e/4b3) ==100 triggers it.
   W A+99=1; save signed A+a4 byte; recursive V74(A,0,30000,0).
   Optional source.V60(source,A,saved) (418504). If saved==0
   (418523), call 411bc8 then optional 423430(A+1b6) (418533).
   Release target as in phase 4 (418549/552/56b), W A+96=0.
   If source nonnull, source==player and player's target==A
   (4185aa/5b4/5c2), 420a68(2e). Config+4c!=0 (4185d7) writes
   A+38 byte=1, A+2e dword=-100000, calls 413e50(A+4d,49983e),
   removes nonnull attachments via 478628 (418615/651), then W A+23c
   word=0. Else config+50==0 (418669) calls 413e50(A+4d,49987c).
7. Unless A+aa or A+ab (418692/6ae), compute ratio state+a
   (4186eb). If >192 (418719), optionally notify 420a68(4) for
   A+a3 && !A+ab (418731/749), W A+ab=1. Else if >128 (418774),
   optionally notify for A+a3 && !A+aa (41878c/7a4), W A+aa=1.
   Finally 409904().

All state ratios read current pointer [A+206] and capacity pointer
[[A+212]+4] at the listed word offsets. Other reads are the pointers,
words and bytes named above; local arrays and temporaries occupy the stack.
All direct non-stack writes are listed (byte flags, dword 4a964c, target
reference count/slot, attachment slots/count and Z). Writes inside callees,
including 40da38 and callback 413e50, are outside this body's fixed contract.
Semantic names such as shield, disable, notification and destruction are
**hypotheses** for these instruction-confirmed transitions.

### Damage path hypotheses

The arithmetic, memory widths, control flow and static table mappings above
are confirmed by instruction addresses and the private-image tests.
Remaining hypotheses:

- Vehicle table 49a282 is the live selected-player root, and the five
  building tables identify the mission's building classes. The pointer-slot
  mappings themselves are confirmed; a live observation of the receiver
  vtables is missing.
- Every relevant dynamic receiver uses one of the known tables:
  initialized-data scanning does not exclude runtime-created tables or other
  call encodings.
- Labels such as shield, destruction, disable, notification, and the exact
  meaning of damage/range units are semantic interpretations; no field
  meaning is required by the replacement arithmetic.
- Supported recursion depth of the +74 bodies is not measured.

## Weapons, projectiles and flight

The firing/flight/impact graph, its contracts and hooks are in
[weapons.md](weapons.md).

## Building part damage 404d70

Hook `building_part` (`core/damage`); contract in
[Original point and part-damage boundaries](#original-point-and-part-damage-boundaries);
every game callee is delegated. The five building vtables all have
V40=4052b4 (slots 4977c4,497858,4978ec,497980,497a14). 4052b4 sums signed
state/definition words and returns `(state_sum<<8)/limit` with the IDIV
remainder (4052d8..4052fe); both result registers are independent of entry
scratch bits. A nonzero total divisor and valid graph are required.

Return details (1,001,024 generated cases):

- C+1e guard preserves entry EAX, EDX=C. Disabled part returns EAX=s16(part),
  EDX=C. Part=-1 becomes zero, including an original write to the low word of
  its incoming stack slot at 404d94; the replacement retains it locally under
  the same expired-callee-argument-storage restriction as the +70 bodies.
- Below the half-threshold, accumulation returns threshold's high EAX word
  with AX=incoming damage, EDX=0. In the trial band, 47e068 and both 47e08d
  results supply scale/buckets; the comparison postincrements the old bucket
  and places the tested value in EDX. A completed loop retains the last
  EAX (new bucket if no trial; zero after failed trials), replacing only AX
  with damage. A successful trial enters destruction with the RNG EDX.
- Destruction captures S/D pointers before calls, writes source/state and
  reloads the actor flag pointer for its clear. V40 runs after those writes.
  **All** notification/source/lifecycle/actor-flag work is gated by V40's
  low word being exactly 100h (404e96). Otherwise jump directly to selector.
- The final selector replaces only AX, preserving EAX's high half. Selector
  -1 leaves EDX unchanged. Other signed selectors set EDX=s16(selector),
  read a word from 49741c+2*selector, write S+5 and S+3=300. These are table
  reads from the mapped image, never copied original values.

**Caller-register boundary at 423430.** 423430's early exit at 423441
preserves incoming EDX:EAX, and its all-disabled normal loop can write AX
alone. At 404eeb the original supplies EAX=linked object, EDX=411bc8's
result and ECX=actor (404edf popped argument). The replacement supplies
exactly those registers through the machine-call bridge also used by
ground_effects. Two original-423430 probes cover its active guard and
inactive empty records; a mutation using an ordinary C call is caught.
411bc8 itself defines EAX/EDX as its final actor-array pointers at
411be2/411be8..411c49, so this contract does not depend on caller registers.

The fixture compares the complete guarded arena, ordered full argument
slots/results, and state observed at each callback. It changes flag/state
pointers, cached definition selectors, source vtables and linked-object
pointers. Fourteen branches are all covered both ways. The V40-gate
mutation is caught. The fixed actor byte span is +96..99; indexed
state/flag writes and arbitrary lifecycle callbacks prevent compare mode.
`hk_building_hit_slot_74` covers calls from replaced +70;
`hk_building_part_calls` also covers original callers entering the body.
Live: identical all-replaced regression with calls per the hooks.md table;
whether a live run reaches the V40=100h path is not recorded.

## Terrain pose 4029d8

Hook `terrain_pose` (bytes 4029d8..402bb6); core `es2_terrain_pose_registers`
in `core/src/node.c`, adapter `tools/reference-game/proxy/node_hooks.h`.
Callers: 40991c (a vtable entry at 49a262) at 409924 and the building V18
46a6d4 (vtable 497818) at 46a7a2. Interpretation (hypothesis): it tilts the
object to the terrain under four points around it.

**Argument** O (cdecl, one slot). Fields: vtable [O], angles O+0c/0e/10
(pitch, roll, yaw words), matrix O+12 (coefficients O+12..23, mode byte
O+24, translation = position O+26/2a/2e), matrix-valid word O+32.

1. s = s16(V10(O)) (`call [vtable+10h]`; for both caller classes this is
   46b910: word +8 of the record at [[O+34h]+4], or 0 when either pointer
   is null). Entry EDX/ECX reach this call unchanged; EAX = the
   vtable.
2. If word O+32 = 0 (JNE 4029f6): 47ebb8(O+0c, O+12) (`euler_matrix`), then
   O+32 = 1.
3. The first four coefficient words O+12..19 are copied once to a local
   2 × 2 matrix [m0 m1; m2 m3].
4. Four samples, each: (x, y) through 47ffe8 (x·m0 + y·m2 and x·m1 + y·m3,
   64-bit products, + 2000h, bits 14..45), plus the position dwords O+26/O+2a
   (reread for every sample), then h = 46e180([4a0be0], point) (`terrain_height`,
   map pointer reread every time; the point's Z is never written and
   46e180 does not read it). Order: (0, s), (0, −s), (−s, 0), (s, 0);
   below called front, back, left, right by axis only.
5. pitch = AX of 47d32c(2s, front − back), roll = AX of 47d32c(2s, left −
   right) (`angle_ratio`; 32-bit differences).
6. Yaw word O+10 is read, then O+0c = pitch | roll << 16 (one dword), O+10 =
   yaw, O+32 = 0, and O+2e = (front + back + left + right) >> 2
   (arithmetic, 32-bit wrapped sum).

Result: EAX = that average, EDX = roll << 16 | yaw. Both callers ignore
them (409929 and 46a7a7: `pop ecx`, then EAX/EDX are written or dead; walk
evidence `<es2-data>\logs\wp-h4-20261008\wp3\liveness.txt`); ECX is
dead at both. 47ffe8 ([`rotate_2d`](#47ffe8-rotate_2dv-m-out)) is computed
in core here; its result registers are overwritten before every use. No
local is read uninitialized.

**Callee registers reproduced:** Euler gets EAX = O+12, EDX = O+0c, ECX = O;
the four terrain calls get (EAX, ECX, EDX) = (Y, map, point), (Y, map,
point), (point, Y, map), (map, point, Y), where Y is the position word added
last; the first ratio call EAX = front − back, EDX = ECX = 2s; the second
EAX = 2s, EDX = right height, ECX = whatever the fourth terrain call left.
Every game call goes through the guest bridge, so the hook refuses compare
mode. Callee counters: `slot_10`, `call_47ebb8`, `call_46e180`,
`call_47d32c`.

**Validation (in vitro).** `node_cases.h`: 1,001,024 direct cases with
Euler, terrain height, angle ratio and V10 as logging stubs (entry
registers, the terrain point by content); stubs flip the matrix-valid word
and the matrix words after V10, move the object and swap the map pointer
between samples; radius words include 0, ±1, 7fffh, 8000h. 256 installed
31/a7 probes; 3,072 real-callee cases on generated terrain of up to 8 × 8
cells with real Euler, terrain height and angle ratio in rotating
original/replaced/compare modes (V10 stays a stub), full pair and memory.
Branch site 4029f6 both ways. Live: identical all-replaced regression,
calls per the hooks.md table.

## Track and pose remainder

Five hooks from the bytes (4793d4..47967a, 479ab0..479bf6, 478b00..478b3a,
479344..4793d2, 403d5c..403eeb) and a call scan of the whole image (only
direct `call rel32` references, no stored addresses of any of the five).
The first three are also computed inside `track_target` and
`track_advance`; as standalone hooks their callers run own code when the
outer hook stays original, as for the
[track hooks](#track-hooks-479720-4789d0-478aac-and-478c64).

| Hook | VA | Callers | Role (hypothesis where marked) |
| --- | --- | --- | --- |
| `track_route` | 4793d4 | 4796cf (47967c) | route search to the stop target (see ground animation 418f40) |
| `track_poses` | 479ab0 | 478b26 (478b00) | one track's interpolated node poses |
| `pose_list` | 478b00 | 478d4e (478d38) | `track_poses` for every track, last to first |
| `track_seek` | 479344 | 403ea6, 405a2a, 41a6a6, 41a95b | put a track at a fraction of a clip's total duration |
| `turret_aim` | 403d5c | 4040ec (403eec, from `turret` 404100), 46a747 (building V18 46a6d4) | turn a building's two turret tracks toward an angle pair (hypothesis: yaw/pitch) |

**Not track/pose code: 47cdf8, 47d198, 47d2c4.** They lie in the same
address area, but the bytes show the VOL member lookup (vol.md) and two
path helpers: 47d198 splits a path into drive, directory, name and
extension with CW3220 `strncpy`/`strrchr`/`strchr` (import thunks 496760,
4967cc, 4967a2) and inline `strcpy`; 47d2c4 concatenates up to four parts
with CW3220 `strcat` (4967ea) into a static buffer 6c5dab; 47cdf8 compares
names with CW3220 import 496868 and reads the archive through the stream
object's virtual +08/+18. That is file I/O on top of the runtime library.

### 4793d4 track_route

Route search as in [Ground animation 418f40 and track target
47967c](#ground-animation-418f40-and-track-target-47967c); this is the
standalone register contract. cdecl, one slot T; no entry register is read
except the high word of EDI: the time argument of the first `reached` call
(+28/+2c) is DI = word T+1c over the caller's EDI high word, later ones carry
the sign-extended frame of the previous step. No hook sees EDI; core uses
0. The game's two clip classes ignore that high word (ANSequence compares
words; ANCyclicSequence returns 1), so it matters only for an unknown class.
Result: the matched transition's pose word over R's high word with EDX = R
(target frame ffff), the accepting method's EDX:EAX, or EAX = the
sign-extended last frame step with the method's EDX after T+0c = ffff.
Compare is allowed as for `track_target` (span T+0..51, the result may
equal neither T nor a span address).

### 479ab0 track_poses

cdecl (T, P, F): P node poses (12 bytes each), F node flag bytes. With C =
the current clip [[R+6]+4·s16 T+4], n = s16 C+10, map = [C+12]:
fraction = (s16 T+1c · 1024 + d/2) / d with d = s16 T+1e (C truncation;
IDIV, d = 0 faults: precondition). Frame words of the current clip
`first = [C+16] + 2·(s16 T+6 · n)` and of the next clip
`second = [C'+16] + 2·(s16 T+0a · n)` with C' = clip s16 T+8, both strided
by the **current** clip's n. For j = 0..n-1 (signed; n ≤ 0 runs nothing):
a = word first+2j, b = word second+2j, node = word map+2j; node 0 is
skipped; equal a and b copy the 12-byte pose [R+12]+12·s16 a to P+12·s16
node (dwords 0, 4, 8), otherwise 49270c (`pose_interpolate`) blends
[R+12]+12·s16 a and +12·s16 b with the full fraction into it; then byte
F+s16 node = 1. [R+12] is reread per node. Result: n ≤ 0: EAX = 0, EDX =
map. Otherwise from the last node: a written node gives EAX = s16 node,
EDX = F; a skipped node gives EAX = high word of the address first+2j :
a and EDX = high word of second+2j : b. 49270c runs in core (as in
`track_advance`); its registers never reach the result. Compare refused
(runtime-indexed pose and flag arrays).

### 478b00 pose_list

cdecl (M). n = 4789b0(M) (`track_count`, computed in core); for i = n-1 down
to 0: 479ab0([M+1a+4i], [M+12], [M+0e]) (M+0e, M+12 and the track pointer
reread per track). Result: n = 0: EAX = 0, EDX = M+1a (4789b0's registers);
otherwise the last (track 0) 479ab0 result. `track_advance`'s core calls
the same function. Compare refused.

### 479344 track_seek

cdecl (T, clip, fraction); only the low words of clip and fraction are
read (sign-extended). S = address [R+6]+4·s16 clip (R = [T]); the clip
pointer [S] is reread on every iteration.

1. total = 16-bit sum of the frame durations (word +0 of the 6-byte records
   [[S]+0c]) for j = 0..s16([S]+0a)-1, signed loop.
2. q = 47e0c0(s16 fraction, total-1) = (s16 fraction · s16(total-1)) >> 14
   (arithmetic; 47e0c0 is the pure Q14 word product, computed in core).
3. Frame walk: k = 0, CX = low word of q; while s16 CX ≥ s16 duration(k):
   CX -= duration(k) (16 bit), k += 1. Unbounded in the original; the
   callers' fractions (0..3fffh: u16 >> 2 at 403e9a, 41a69c, 41a951; 1770h
   at 405a1b) and nonnegative durations keep it inside the clip. Inputs
   whose walk leaves the frame records are outside the contract.
4. 4792ac(T, high word of S : clip, k, high word of q : CX)
   (`track_clip`, through the guest bridge so its hook mode and counter
   stay), entry EAX = high word of S : clip (track_clip keeps that high word
   in its V20 frame slot), EDX = T, ECX = the phase slot.

Result: 4792ac's EDX:EAX. No entry register is read. Compare refused (the
callee runs game code). Callee counter `call_4792ac`.

### 403d5c turret_aim

cdecl (B, P); P points at signed words P+0 and P+4 (from 403eec a
local vector, from 46a6d4 the negated angle word B+20d and B+20f).
Words read from the image's data: k[2] at 4973e4, step[2] at 4973e8,
lo[2] at 4973ec, hi[2] at 4973f0 (each reread at its use).

1. t0 = clamp(s16 P+0 >> 3, -256, 256), t1 = -(clamp(s16 P+4 >> 3, -256,
   256)) (arithmetic shifts; 16-bit negate). |t| therefore never reaches
   8000h, so the JNE at 403dfe (special case 8000h → 7fffh) is never
   taken: omitted from the branch sites with this proof.
2. For i = 0, 1:
   r1 = 47e0a0(|ti|, s16 ti) (`multiply_q8`; entry EAX = ECX = s16 ti,
   EDX = |ti|); r2 = 47e0a0(r1, s16 k[i]) (entry EAX = ECX = s16 k[i],
   EDX = r1); 467adc(B+209+2i, r2, high word of r1 : step[i])
   (`approach_word`; entry EAX = r2, EDX = r2's EDX, ECX = B+209+2i);
   v = word B+20d+2i + word B+209+2i (16 bit); B+20d+2i = hi[i] if s16
   hi[i] ≤ s16 v, else v if s16 lo[i] < s16 v, else lo[i];
   479344([B+1f9+4i], 0049h : i, u16 B+20d+2i >> 2) (`track_seek`; entry
   EAX = B+1f9+4i, EDX = the track, ECX = 0049h : i; 0049h is the high
   word of the table address 4973ec+2i).
3. 402604(B, 100) (`actor_animate`; entry EAX/EDX = the second 479344's
   result, ECX = B).

Result: 402604's EDX:EAX (dead at 46a6d4; 403eec returns its EDX). No
entry register or uninitialized local is read. All four callees go through
the guest bridge (their hook modes and counters stay); compare refused.
Callee counters `call_47e0a0`, `call_467adc`, `call_479344`, `call_402604`.
B+1f9/1fd are the two turret track pointers (unaligned), B+209/20b the
angular rates, B+20d/20f the angles (hypothesis from the use).

### Validation

In vitro (`track_pose_cases.h`): per hook 1,001,024 direct cases against
the installed adapter (exact EDX:EAX, the whole arena, the ordered own-class
frame steps or the ordered stub calls with entry registers, the turret
tables, the callee counter deltas), 256 installed 31/a7 poison cases, and a
real pass (1,536 caller cases for `track_route` through 47967c, 3,072
real-callee cases for the others with the nested hooks in rotating modes).
All 30 sites of the five functions are observed both ways; 403dfe is
omitted with the proof above. `track_target` (its four 47967c sites and the
reached methods) and `track_advance` (the frame-step methods) pass with
their 4793d4, 478b00 and 479ab0 sites owned by the new hooks. Mutation
witnesses in `check_mutations.ps1`
([MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md)). Live:
identical all-replaced regression; `track_route`, `track_poses` and
`pose_list` have 0 calls there because their callers are replaced, while
`track_seek` and `turret_aim` are entered (hooks.md table).

## Trig leaves 47eb30 and 47ffe8

Two pure leaves, from the llvm-objdump listing of the reference DBSIM.EXE.
Neither calls game code, reads an entry register or an uninitialized local,
and neither has an x87 instruction.

### 47eb30 `sincos_pair(angle, m)`

cdecl, two slots, `enter 0,0` frame, EBX saved. Only the low word of the
`angle` slot is read (into CX, over the caller's ECX high word).

1. c = the heading cosine of the word angle, computed exactly as
   `heading_cos` (47dff0): x = s16 angle; i = SAR(x, 4) + the bit shifted
   out (ADC 0), that is (x + 8) >> 4 rounded down; |i| by CDQ/XOR/SUB;
   |i| < 400h (JL 47eb4f): the word of the quarter sine table
   `4a25c4 + 2|i|`, else the negated word at `4a25c4 + 2(800h − |i|)`.
   Words m+0 and m+6 ← c.
2. s = the same for the word angle − 4000h (`heading_sin`, 47e027; JL
   47eb8e). Word m+2 ← s, word m+4 ← −s.

Result: EAX = −s (the dword, after the second NEG), EDX = the CDQ of the
second index: FFFFFFFFh when s16(angle − 4000h) < −8, else 0. ECX = the
caller's ECX high word over the angle word; EBX restored. Memory: the
2,050-byte table (read only) and the eight bytes at m (written in the order
+0, +6, +2, +4, nothing read back). |i| ≤ 800h, so the table index stays
in 0..400h.

The arithmetic is the one of `heading_cos`/`heading_sin`; the core reuses
`es2_heading_cos` and `es2_heading_sin` (movement.c).

### 47ffe8 `rotate_2d(v, m, out)`

cdecl, three slots, `enter 0,0` frame, ESI/EDI/EBX saved, EBP pushed again
and used as a scratch register. v points to two dwords, m to four words,
out to two dwords.

1. EDX:EAX = s32 v0 · s16 m0 (IMUL), plus s32 v1 · s16 m2, plus 2000h
   (ADD/ADC on 64 bits); out+0 ← bits 14..45 of that sum (SHRD 14).
2. Then, after that store, v0 and v1 are read again (v0, m1 into the first
   product, v1, m3 into the second): out+4 ← bits 14..45 of
   s32 v0 · s16 m1 + s32 v1 · s16 m3 + 2000h.

Result: EAX = out, EDX = the high dword of the second sum (after the
2000h carry), ECX = the high dword of v0 · m1. No branch. Aliasing: out may
overlap v or m; the second row then sees the first row's store (the core
keeps the read/write order).

### Callers and register liveness

47eb30 has 13 call sites: 4048ef (4045c8, a vtable function), 409b89
(meteor constructor 409b44), 4156bf (`fire_line` 415608, computed in its
core), 4235a8 (`spawn_point` 42354c, called through the guest bridge),
43bfb4 (43b978, vtable), 43edc3 (43ec08), 43f3e4 and 43f427 (43f2d8),
450397 (45030c), 46a2a0 (46a10c), 470ab8 (470a1c), 4766b9 (4763f4,
vtable) and 488dd3 (the two-slot wrapper 488dc7, called once from
488b98). 47ffe8 has 25 call sites: 402a50, 402a92, 402afd, 402b3b
(`terrain_pose` 4029d8), 404909 (4045c8), 409bcb (409b44), 411db2
(411d64), 4156d3 (`fire_line`), 41fd46 (41fbb8), 4235d8 (`spawn_point`),
427d41 (the collision walk 427c68), 43c038, 43c052, 43c06c (43b978),
43ede3 (43ec08), 43f3f8, 43f43b (43f2d8), 4503c2 (45030c), 46a2ba,
46a2d4, 46a2ee, 46a305 (46a10c), 46ab55 (46aa60), 4766cd (4763f4) and
488dec (the three-slot wrapper 488ddd, called four times from 488b98). No
other reference: neither address nor the wrappers' appear as a dword
anywhere in the file. Several of these callers are presentation code
(43b978, 4763f4, 488b98); the leaves themselves draw nothing.

EAX and EDX are returned exactly. ECX differs in replaced mode (the hook
framework returns the entry ECX), so its liveness was walked at every
site: each caller overwrites ECX (MOV/LEA/MOVSX/POP) before any read,
except where it passes through a callee that does so first:
- 43bfb4: `mov cx, [4d253c]` writes the low word and only CL is read before
  `mov ecx, eax` at 43bfda.
- 409b89 → `rng_bounded` 492f24: 492ee0 writes ECX (MOVSX at 492f00) before
  reading it; 492f24 then pops ECX.
- 470ab8 → 480050 writes ECX (`mov ecx, edx` at 48006a) before reading.
- 43ede3: every path to 43ee84 (`mov ecx, [ebp-4]`) writes CX before
  MOVSX reads it.
- 427d41 → 427360 (`[obj+2Ah]` = 0 returns at once with ECX untouched) →
  427c68's loop (`dist2d_point` 4928d0: 49545b is an identity function and
  4928d0 pops ECX) or its return → 404ae4's return → the three callers of
  404ae4: 41a432/41a527/41e4f4 behind `ground_collide` pop ECX, 4236a3
  (`face_test` 46ff90) writes ECX at 46ffac before reading, 46a7b4 pops
  ECX.
- 488dd3/488dec return to 488b98, which writes ECX by LEA right after each
  wrapper call.

So ECX is dead after both leaves; it is outside the contract.

### Hooks

| Hook | Prologue | Relocate / lengths | Slots | Result |
|---|---|---|---:|---|
| `sincos_pair` | `c8 00 00 00 53 66 8b 4d 08` | 5 / 4,1 | 2 | EDX:EAX |
| `rotate_2d` | `c8 00 00 00 56 57 53 8b 5d 08` | 5 / 4,1 | 3 | EDX:EAX |

Adapter group `mount` (`proxy/mount_hooks.h`). Both accept compare mode:
an 8-byte span on m (`sincos_pair`, default comparison) and on out
(`rotate_2d`, a comparator remapping EAX = out); an out that overlaps v or
m is not modelled by the private copies, but none of the 25 call sites (nor
the wrapper 488ddd's four) overlaps them
([compare_mode.md](../reference-game/compare_mode.md#spans-and-comparators)).
No entry shim, no callee counter. Branch sites: 47eb4f and 47eb8e (JL),
both ways; 47ffe8 has none. Live: identical all-replaced regression, calls
per the hooks.md table.

## Graphics-library candidates and the texture edge setup 48aaac

From an llvm-objdump listing of the reference `DBSIM.EXE` (image base
`0x400000`) and a call graph built from that listing plus every dword in
the image that points into the code (vtables, driver tables). The six
candidates sit next to `transform_point`/`pose_interpolate` by address, but
all six lie in the graphics library `0x480000`-`0x48cfff` ([hud.md](hud.md)
legend "D").

### Classification

| VA | Instr. | Call sites (caller) | Root | Reads / writes | Verdict |
|---|---:|---|---|---|---|
| `0x486ee8` | 919 | 487ba5 (`0x487acc`); 44fef0, 44ff60 (`0x44f6f4`) | rendering `0x45fca8` (45fca8 → 42e700 → 42ebe8 → 42ec08 → 42f0b0 → polygon fill 483eb8 → 487acc) and many draw vtables; 44f6f4 once per mission start (461608 at 461830, called at 45f42b) | three 8-byte screen points and three shade words (arguments), its 6.5 KB frame of band polygons, the graphics context `[0x4a3614]`: colour dword `[[ctx+0x228]+4]` written before each band; calls the polygon fill `0x483eb8` five times (483eb8 adds the canvas origin and dispatches on the shading mode to the driver slots) | **draws** (flat bands of a Gouraud triangle): H3 |
| `0x487acc` | 94 | 483f94 (`0x483eb8`, shading mode 1) | rendering, as above | shading record `[ctx+0x228]`: mode set to 0, colour saved and restored, mode back to 1; maps each corner's shade word through the ramp table at `0x4a4fa5` (8-byte entries: a word-table pointer and a scale word); fans the polygon into triangles for 486ee8 | **draws** (Gouraud polygon): H3 |
| `0x48aaac` | 108 | 48a9c3, 48a9da (`0x48a924`); 48aa60 (`0x48aa50`) | rendering only: 48a924 is entry 33 of the driver table at `0x4a586c`, which `0x4893aa` copies to the runtime slots at `0x4a57ac` (`[0x4a583c]` = 24h dwords), so it is the slot `[0x4a5830]` that 483eb8 calls at 483f70 for shading modes 5 and 7 (textured polygons); 48aa50 is 48a924's per-row edge stepper | the caller's edge record, the four globals 48a924 sets (`0x6c5f80`..`0x6c5f8c`) and the arrays they point to; the pure Q16 divide `0x47e068` | **pure helper** (edge setup of the textured polygon fill), no drawing side effect: replaced (`tex_edge`, below) |
| `0x488974` | 107 | 487f3b (`0x487eac`); 4540d7 (`0x453f24`, screen capture, [file_io.md](file_io.md)) | presentation `0x45fba4` (45fba4 → 42a9c0 → 42d2f8 → 488012 → 487eac) | bitmap header bytes +4/+5 (rewritten from the screen depth `[0x4a3662]`), the RTL call `0x49681a(bits, 0, size)` on the bitmap, then the blit slot `[0x4a5818]` by the clip mode `[ctx+0x204]` | **draws**: H3 |
| `0x4887d8` | 84 | 4827ca (`0x4827bb`); 4880b2 (`0x488078`) | rendering (45fca8 → 4327ac → 43408c → 42d624 → 488143 → 488078) and the HUD/view code of the logic frame (45f570 → 45de30 → 45e208 → 482634 → 482534 → 4827bb; [main_loop.md](main_loop.md) step 15, presentation side) | canvas origin `[ctx+0x208..0x220]`; draws through the RLE path `0x488df8` (bitmap flag nibble 1) or the blit slots `[0x4a5814]`/`[0x4a581c]` | **draws**: H3 |
| `0x48c954` | 82 | 474f86, 4752e8, 4756b0, 475851, 475d6b, 475ec8, 476069 (the DTS polygon draw methods 474ec0, 474fa8, 475538, 4756d4, 475cd0, 475d8c, 475eec, reached only through their vtables; [dts.md](../formats/dts.md)) | rendering (shape drawing) | transforms the current polygon's corners (`0x48db93`, or the perspective path `0x4912a0` when `[0x6c6028]` is set), projects them with `[0x6c6030]`/`[0x6c6034]`/`[0x6c6038]` and keeps per-vertex state in the renderer's globals: done flags `0x6c68fa`, the screen cache `0x6c9bc2`, the corner list `0x6cbb02`, its count `0x6cbc02` | renderer vertex stage with global caches (not a caller buffer): H3, deferred |

None of the six is reached from the simulation: the only path from the
logic frame (4887d8 via 45de30) is the HUD/view code that main_loop.md
keeps original, and it draws. Only `0x48aaac` is a pure arithmetic helper
that runs in every suite. Its output only feeds the textured polygon fill,
so the live oracle (which compares simulation state) cannot see a
deviation: the in-vitro comparison against the original machine code is
the evidence, and the live run checks the counters and that nothing else
changes. The stepper `0x48aa50` (36 instructions, pure, its only live
callee is 48aaac) would qualify too; it stays original.

### Contract of 0x48aaac `tex_edge`

cdecl, two slots: `E` (the edge record) and `i` (the vertex the edge
starts from). Prologue `55 8b ec 51 53 56 57 8b 7d 0c 8b 5d 08`; the
`push ecx` makes the dword local `[ebp-4]`. Callee-saved EBX/ESI/EDI.

Globals, read only here (48a924 sets them before the first call):
`[0x6c5f80]` the end vertex (the one with the largest y), `[0x6c5f84]` the
vertex count `n`, `[0x6c5f88]` the screen points `P` (8 bytes each: x, y
dwords), `[0x6c5f8c]` the texture points `T` (8 bytes each: u, v dwords;
48a924 copies it from `[[ctx+0x228]+8]`). Element `k` of an array is at
`base + 8k` modulo 2^32.

Edge record (34h bytes, in the caller's frame): `+0` step direction (read
only; the callers store -1 and +1), `+4` rows left, `+8` the vertex the
edge runs to, `+0c`/`+10` u and v (Q16), `+14`/`+18` their steps per row
(Q16), `+1c` x, `+20` whole x step per row, `+24` x error direction, `+28`
error accumulator, `+2c` error increment, `+30` error modulus. 48aa50
consumes it: it decrements `+4`, calls 48aaac(E, `[E+8]`) when it reaches
zero, and otherwise adds the steps, adding `+24` to x and subtracting `+30`
from `+28` when the accumulator becomes positive.

Steps, in the original's order of reads and writes:

1. Loop: if `i` = `[0x6c5f80]`, return EAX = 0 (no edge left). Otherwise
   `j = [E] + i` (32-bit); if `j >= n` (signed) then `j = 0`, else if
   `j < 0` then `j = n - 1`. Read `P[i].y`, then `P[j].y`;
   `dy = P[j].y - P[i].y` is stored to `E+4`. If `dy` = 0, `i = j` and
   loop (the zero-height edge is skipped).
2. `E+8 = j`; `E+0c = T[i].u << 16`; `E+10 = T[i].v << 16`;
   `E+14 = q16((T[j].u << 16) - [E+0c], dy << 16)`,
   `E+18 = q16((T[j].v << 16) - [E+10], dy << 16)`, where
   `q16(a, b)` is `0x47e068`: the 64-bit value `a * 65536` (sign-extended)
   divided by `b` with IDIV, truncated toward zero. `E+0c`/`E+10` are read
   back from memory for the subtractions.
3. `E+1c = P[i].x`; `dx = P[j].x - P[i].x` (P[i].x read again).
   If `dx < 0`: `E+24 = -1`, `dx = -dx`, `E+28 = 1 - [E+4]`,
   `E+20 = -(dx / [E+4])`; else `E+24 = 1`, `E+28 = 0`,
   `E+20 = dx / [E+4]`. Then `E+2c = dx mod [E+4]` (IDIV remainder, sign
   of the dividend) and `E+30 = [E+4]`. Return EAX = 1.

Registers at return: EAX as above. EDX is returned exactly: the entry EDX
when step 1 returns at once, `P[i].y` of the last skipped edge when it
returns 0 after skipping, the remainder of step 3 when it returns 1. ECX
is the local `[ebp-4]` popped by the epilogue: the entry ECX on the
return-0 paths, `dy << 16` on the return-1 path. The hook framework returns
the entry ECX, so ECX differs on the return-1 path; it is dead at all three
call sites: after 48a9c3, 48a9d2 loads ECX before any read; after 48a9da,
48a9ee loads it; after 48aa60, 48aa50 returns without touching ECX to
48aa2e or 48aa3c, both `pop ecx`. No entry register and no uninitialized
local is read (`[ebp-4]` is written at 48ab04 before the reads at 48ab2f
and 48ab4e), so no entry shim.

Outside the contract (the original faults or never returns; the in-vitro
generator excludes these states): a divisor `dy << 16` of 0 (dy a multiple
of 65536) or a Q16 quotient outside 32 bits (#DE in 47e068); `dx` =
-2^31 with `[E+4]` = -1 (#DE at 48aba2/48abbb); a walk that never reaches
`[0x6c5f80]` while every edge it meets has `dy` = 0 (endless loop, e.g. a
direction other than ±1 that cycles past the end vertex). In the game the
coordinates are clipped screen positions and the direction is ±1.

Branch sites: 48aabf (JNE, end vertex), 48aad3 (JL, wrap to 0), 48aadb
(JGE, wrap to n - 1), 48aafb (JE, zero-height edge), 48ab89 (JGE, dx sign).
`0x47e068` has none.

### Hook

| Hook | Prologue | Relocate / lengths | Slots | Result |
|---|---|---|---:|---|
| `tex_edge` | `55 8b ec 51 53 56 57 8b 7d 0c 8b 5d 08` | 5 / 1,2,1,1 | 2 | EDX:EAX |

Adapter group `math_lib` (`proxy/math_lib_hooks.h`), core
`es2_tex_edge_registers` in `core/src/math_lib.c`. The Q16 divide 47e068
is computed in core (as in camera.c and weapons.c), so there is no game
call and no callee counter. Compare mode is refused (the record may
overlap the point arrays, which the private-copy compare does not model).
In vitro: `math_lib_cases.h`. Live: identical all-replaced regression,
calls per the hooks.md table.

## Enter-prologue helpers 47ff8c and 4801d4

Two pure leaves with an `enter 0,0` prologue, from the llvm-objdump listing
of the reference DBSIM.EXE. The hook framework relocates `enter imm16,imm8`
(`c8`, four bytes; `instruction_size` in `hook/hook.c`, fixture
`hook/tests/hook_fixture.c`). The DBSIM map has no probe row for them
(the entry is `enter`). Neither function calls game code, reads x87 state,
an uninitialized local or (except on one path, below) an entry register.

### 47ff8c `rotate_words(v, m, out)`

cdecl, three slots, `enter 0,0`, ESI/EDI/EBX saved; EBX = v (two words),
ESI = m (four Q14 words), EDI = out (two words). No branch.

1. EAX = s16 v0 · s16 m0 (one-operand IMUL, low dword), EDX = s16 v1 ·
   s16 m2 (two-operand IMUL); word out+0 ← (EAX + EDX + 2000h) SAR 14
   (32-bit sum, wraps).
2. After that store v0, m1, v1 and m3 are read again: EAX = v0 · m1, ECX =
   s16 m3, EDX = v1 · m3; word out+2 ← (EAX + EDX + 2000h) SAR 14.

Result: EAX = out, EDX = the low dword of v1 · m3, ECX = s16 m3; ESI, EDI,
EBX, EBP restored. It is the word sibling of `rotate_2d` 47ffe8 (dword
vector, 64-bit sums). Aliasing: out may overlap v or m; row 2 then reads
row 1's store.

Call sites (2; the address does not occur as a dword in the file):
409c10 in the meteor constructor 409b44 (`meteor_create`, through its
guest bridge) and 4169a0 in `slope_speed` 41693c (whose core computes the
rotation itself, so the hook is entered from there only while
`slope_speed` is original).

### 4801d4 `matrix_point(v, M, out)`

cdecl, three slots, `enter 0,0`, ESI/EDI/EBX saved; EBX = v (three
words), ESI = M (nine Q14 words at +0..+11h, mode byte +12h, translation
dwords T0..T2 at +14h/+18h/+1Ch), EDI = out (three dwords). The mode byte
is read once: CMP with 1, JL 4802e3 / JE 48028c (signed: 80h..FFh are
below 1).

- mode < 1 (4802e3): out0 ← s16 v0 + T0, out1 ← s16 v1 + T1, out2 ←
  s16 v2 + T2, each stored before the next word is read. EAX = the out2
  value; EDX and ECX are the entry values (not touched).
- mode = 1 (48028c): out0 ← ((v0 m0 + v1 m2 + 2000h) SAR 14) + T0, out1 ←
  ((v0 m1 + v1 m3 + 2000h) SAR 14) + T1, out2 ← s16 v2 + T2. EAX = out,
  EDX = the low dword of v1 · m3, ECX = s16 m3.
- mode > 1 (4801f4): row r ← ((v0 a + v1 b + v2 c + 2000h) SAR 14) + Tr
  with the word triples (a, b, c) = (m0, m2, m6), (m1, m3, m7), (m4, m5,
  m8). EAX = out, EDX = the low dword of v2 · m8, ECX = s16 m8.

Products are low dwords of signed 16 × 16 multiplies, sums wrap at 32
bits. Each row reads v, its matrix words and its translation after the
previous row's store (out may overlap v or M, also the mode byte, which is
not read again). 48043c `transform_point` is the dword-vector sibling.

Call sites (31; no dword reference): 4015ac, 40171e, 40189a, 401994
(401148, the camera body computed by `camera_update`'s core), 403734
(`base_part_blast`), 4037d6 (`base_part_debris`), 404529, 404575
(`turret`), 406858 (`base_part_position`), 4093ac (`smoke_update`),
40a6a6 (`projectile_flight`), 40b224 (`bullet_flight`), 40c4de
(`col_bound`), 40c584 (`sphere_hits`), 40d6ac (region damage 40d434,
original), 40e915 (`weapon_prepare`), 40eb6f, 40ebd8 (`emit`), 40f63a
(`weapon_eject`), 413cfc (`shield_geometry`), 41590b, 41597f, 4159f3,
415a6a (`vehicle_keys`), 41b6b2, 41b718 (`part_position`), 422968,
4229ec (`target_emit`), 46b751 (`fire_update`), 48dba2 (the wrapper
48db93, called by the renderer's vertex stage 48c954) and 491319 (the
perspective vertex path 4912a0). The last two are presentation callers;
the helper itself draws nothing.

### Registers, flags and stack

EAX and EDX are returned exactly by the cores (EDX:EAX pair; the mode < 1
path returns the entry EDX). ECX is live after both helpers: 48db93,
`part_position` (41b718) and `col_bound`'s JBE exit return 4801d4's ECX
unchanged to their callers, 4229ec passes it into the bullet constructor
40b43c, 40189a/40e915 into 47f9a0, 4037d6 into 4089bc; 47ff8c's ECX leaves
`slope_speed` on its early return (face 0, JE 4169ac) to 416a04. Instead
of walking every path, both hooks have an exit shim that returns the ECX
the core computed (the mode < 1 path gives back the entry ECX). Flags are
dead: the first instruction after each of the 33 calls that touches flags
writes them (`add esp,0Ch`, or `xor ecx,ecx` at 409c18, `add eax,[...]`
at 40c4e6) before any read. Stack: the original leaves its return
address, EBP, ESI, EDI and EBX in the five dwords below the caller's ESP;
the shim leaves the return address, EBP and the three copied slots there
and the dispatcher's frames below. No caller reads below its ESP; whether
a later callee reads an uninitialized local over those slots was not
audited per site (the same status as the other leaf hooks).

### Hooks

| Hook | Prologue (signature) | Relocate / lengths | Slots | Result |
|---|---|---|---:|---|
| `rotate_words` | `c8 00 00 00 56 57 53 8b 5d 08 8b 75 0c 8b 7d 10 0f bf 03` | 5 / 4,1 | 3 | EDX:EAX, ECX by exit shim |
| `matrix_point` | `c8 00 00 00 56 57 53 8b 5d 08 8b 75 0c 8b 7d 10 80 7e 12 01` | 5 / 4,1 | 3 | EDX:EAX, ECX by exit shim |

Adapter group `transform_helpers` (`proxy/transform_helpers_hooks.h`),
cores `es2_rotate_words_registers` and `es2_matrix_point_registers` in
`core/src/transform_helpers.c`. The exit shims follow the `panel_weapons`
pattern with three slots and are chained from
`panel_weapons_capture_entry`. Compare mode is refused (out may overlap
the inputs). No game call, no callee counter. Branch sites: 4801e8 (JL)
and 4801ee (JE), both ways; 47ff8c has none.

In vitro (`invitro/transform_helpers_cases.h`): 1,001,024 direct cases
per hook against the original body with chosen entry EAX/EDX/ECX (edge and
random words, every mode class, out over v, M, the mode byte and the
translation), comparing EAX, EDX, the shim's ECX and the arena; 256
installed 31/a7 poison cases through the exit shim (fast and preserving
dispatch); 4,096 real-caller cases each from a 31/a7 stack with the hook
original or replaced: `matrix_point` under 48db93, `rotate_words` under
the original `slope_speed` 41693c over the real 47ebb8 and 47dbca (stub
for the face lookup 46e498, one third of the cases take the early return).
Mutations in [MUTATIONS.md](../../tools/reference-game/invitro/MUTATIONS.md).

Live: with all hooks replaced and the exit shims active, all six suites of
`oracle-parallel-394hooks-20261010T235619Z` were identical; `rotate_words`
is entered only through `meteor_create`, `matrix_point` in every suite,
dominated by the renderer callers (hooks.md table).

## Callback side-path closure inventory

The [contract table](#function-contracts-and-classification) is the detailed
movement contract. This inventory adds every other statically resolved direct
callee reached from `41a360`, including collision/audio/UI/resource side
paths, until a return or unresolved indirect transfer. It is an **inventory of
hypotheses**, not replacement specifications; it was produced by fresh
entry-aligned decoding and a branch walk. Short prefixes and direct call sites
are confirmed bytes; slot widths are observed stack accesses (not a complete
prototype). Unused/forwarded arguments, signedness, return meaning (EAX and
scratch registers), indirect object sizes and actor/config attribution are H.
Reads/writes cover direct memory operands only, not immediate addresses
passed to callees or transitive effects. No x87 instruction was observed in
any row.

Frame: `F` stack frame (C), cdecl/prototype H; `I` import tail transfer, ABI
and forwarding H; `N` custom/no-frame entry (H); `S4`/`S8`/`S12` stdcall
`ret n` (C). Slots: `p` u32/pointer, `w` u16, `-` none proven. Status:
`open` = not hooked, side-path contract and state bounds unverified;
`boundary` = external or dynamic target; `hook` = registered hook today (its
contract is in the document that owns it).

Imports and polymorphic calls are explicit boundaries. Indirect jump tables,
shared tails outside an entry's bounded range and exception landing pads are
unresolved control flow, not proof of a leaf. The inventory does not claim
that all runtime targets can be recovered without live object identities;
those targets must be recorded before state-changing hooks extend beyond the
verified set.

| VA | Prefix | Frame; slots | Reads | Writes | Callees / frontier | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 49680e | `ff 25 50 c3 6c 00` | I; - | 6cc350 | none | indirect 49680e -> jmpl *0x6cc350 | boundary |
| 49288c | `55 8b ec 83 c4 f4` | F; p,p | none | none | 4953c4,492a5e,492a7d | hook `dist_gate` |
| 404ae4 | `55 8b ec 81 c4 c4 fd ff` | F; p,w,p | 4a95a0 | none | 471c70,427c68 | hook `obstacle_test` |
| 46e498 | `55 8b ec 83 c4 e8` | F; p,p | none | none | leaf | hook `terrain_face` |
| 492f24 | `55 8b ec 53` | F; p,s16 | none | none | 492ee0 | hook `rng_bounded` |
| 434010 | `55 8b ec 53` | F; - | 49b0fc,49b100,4d253c | 49b0fc,49b100 | 429820,42ea94,42d624,467828,492ee0,42ea44,42d2f8 | open |
| 462784 | `55 8b ec 83 3d ec f8 49` | F; p | 49f8ec,49f8f4,49f8f8 | none | leaf | open |
| 47e08d | `c8 00 00 00 8b 45 08 f7` | F; p,p | none | none | leaf | open |
| 464618 | `55 8b ec 53` | F; p,p,p | none | none | 473aec | open |
| 464038 | `55 8b ec 51` | F; p,p | none | none | 463d4c,473898 | open |
| 47de72 | `c8 00 00 00 53` | C cdecl XYZ dwords / full EDX:EAX | No non-stack memory; signed wrapped metric, EDX=Z sign | | leaf | hook `dist3d_delta`, compare supported |
| 4628e0 | `55 8b ec 53` | F; p,p | 49f8ec,4d2b78,4d2c78 | none | 492f24,46299c,464038 | hook `sfx_play_at` |
| 434144 | `53 e8 d6 56 ff ff 8b d8` | N; - | 49b638,4d253c | 49b634,49b638 | 429820,42d854,467828,42d820 | open |
| 4953c4 | `55 8b ec 53` | F; p | none | none | 49545b | open |
| 492a5e | `55 8b ec 53` | F; p,p | none | none | 492a30 | open |
| 492a7d | `55 8b ec 53` | F; p | none | none | 492aaf | open |
| 471c70 | `55 8b ec 53` | F; p,p | none | none | 471c18 | hook `pool_prev` |
| 427c68 | `55 8b ec 83 c4 dc` | F; p,p,p | none | none | 4928d0,47ebb8,47df04,47ffe8,427360 | hook `collision_walk` |
| 429820 | `a1 9c f9 4c 00 c3` | N; - | 4cf99c | none | leaf | open |
| 42ea94 | `83 3d f8 ae 49 00 00 74` | N; - | 49aef8,4cfd0c | 49aef3 | 4303b0 | open |
| 42d624 | `53 56 57` | N; - | 49ac44,4a57e8,4a57f0,4a5af8,4cfa58,4cfa5c,4cfa70,4cfa94,6c5ed0,6c5f70 | 49ac44,4a368a,4cfa70 | 429820,488143,43271c; indirect 42d678 -> calll *0x4a57e8; 42d78d -> calll *0x4a57f0; 42d7c7 -> calll *0x4a57f0 | boundary |
| 467828 | `e8 bf f0 02 00 2b 05 78` | N; - | 4d3b78 | none | 4968ec | open |
| 42ea44 | `53 56 8b 1d f8 ae 49 00` | N; - | 49aef8 | 49aef3,4cfd08,4cfd0c | 4303b0 | open |
| 42d2f8 | `55 8b ec 83 c4 f0` | F; w | 4a57f0,4cf9a0,4cf9a4,4cfa68,4cfa94,4d2566,4d3018,4d301c,4d3ba2,6c5ed0,6c5f70 | 49ac44,4a368a,4cfa58,4cfa5c,4cfa68,4cfa6c,4cfa70 | 429820,488012,486704; indirect 42d418 -> calll *0x4a57f0 | boundary |
| 473aec | `55 8b ec 53` | F; p | 4a0e30 | 4a0e30 | 4960d7,474350,495d1f | open |
| 463d4c | `55 8b ec 83 c4 a8` | F; p,p,p | none | none | 49630c,4968e6,473828,472f14,4649b8,472f64,473334,4968b0,47307c,464390 | open |
| 473898 | `55 8b ec 53` | F; p,p | 4a0e30,6b5588 | none | 49602b,495fcd,49603f,47473c,474350,495e7c,474324 | open |
| 46299c | `55 8b ec 83 c4 e8` | F; p,p | 49f8ec,4d2b78,4d2c78 | none | 48c444,48c57c,462784,47de72,47e08d,464618,47d32c,4646dc | hook `sfx_place` |
| 42d854 | `a1 70 5f 6c 00 8b 15 b8` | N; - | 4cfcb8,6c5f70 | 4cfcb8 | leaf | open |
| 42d820 | `55 33 c0 a3 b8 fc 4c 00` | N; - | none | 4cfcb8 | leaf | open |
| 49545b | `55 8b ec 8b 45 08` | F; p | none | none | leaf | open |
| 492aaf | `55 8b ec 8b 45 10` | C cdecl XYZ dwords / full EDX:EAX | No non-stack memory | | 47de72 | hook `dist3d_forward` (separately called), compare supported |
| 492934 | `55 8b ec 53 56` | C cdecl P,Q / full EDX:EAX | R XY dwords and ratio table, no writes | | 49290c -> 492ac8 -> 47d32c | hook `heading_point`, compare supported |
| 413c68 | `55 8b ec 53 56 57` | C cdecl S,front,damage / full EDX:EAX | R/W one selected word in S..S+3 | | 47e0b0 | hook `shield_damage` (stateful, fixed span) |
| 471c18 | `55 8b ec 83 c4 e0` | F; p,p | none | none | 496796 | hook `pool_next` |
| 4928d0 | `55 8b ec 83 c4 f8` | F; p,p | none | none | 49545b,492a30,492a45 | hook `dist2d_point` |
| 47df04 | `c8 00 00 00 8b 45 08 66` | F; p | none | none | leaf | open |
| 47ffe8 | `c8 00 00 00 56 57` | F; p,p,p | none | none | leaf | hook `rotate_2d` |
| 427360 | `55 8b ec 83 c4 f4` | F; p,p,w | none | none | 427238 | hook `shape_cell` |
| 4303b0 | `55 8b ec a1 20 b0 49 00` | F; p | 49b020 | 49b020 | leaf | open |
| 488143 | `c8 00 00 00 8b 4d 08 8b` | F; p,p,p | none | none | 488078,4880bc | open |
| 43271c | `55 8b ec 83 c4 f8` | F; p,p | none | none | 452bec,433968,452434 | open |
| 4968ec | `ff 25 e8 c3 6c 00` | I; - | 6cc3e8 | none | indirect 4968ec -> jmpl *0x6cc3e8 | boundary |
| 488012 | `c8 00 00 00 8b 4d 08 8b` | F; p,p | none | none | 487eac,487f6a | open |
| 486704 | `c8 34 00 00 56 57` | F; p | 4a3614,4a3691,4a3695,4a57ac,4a57b4,4a57b8 | none | 48699c,4869fc,486c00,483a04; indirect 486843 -> calll *-0x8(%ebp); 4868b7 -> calll *0x4a57ac; 4868ed -> calll *-0x8(%ebp); 4868ff -> calll *-0x8(%ebp) | boundary |
| 4960d7 | `55 8b ec 0f b7 45 08` | S4; w | 6cbe98 | none | indirect 4960e4 -> calll *%eax | boundary |
| 474350 | `66 a1 6e 56 6b 00 c3` | N; - | 6b566e | none | leaf | open |
| 495d1f | `55 8b ec ff 75 10` | S12; w,w,p | 6cbdec | none | indirect 495d34 -> calll *%eax | boundary |
| 49630c | `53 8b dd 03 58 04` | N; - | 6cbee4 | none | leaf | open |
| 4968e6 | `ff 25 e4 c3 6c 00` | I; - | 6cc3e4 | none | indirect 4968e6 -> jmpl *0x6cc3e4 | boundary |
| 473828 | `55 8b ec 53` | F; p,p,p | none | none | 47465c,474360,474708,474508 | open |
| 472f14 | `55 8b ec 53` | F; p,p | 6cbee4 | none | 47a888,4730e8 | open |
| 4649b8 | `55 8b ec 8d 45 0c` | F; p,p | none | none | 49683e | open |
| 472f64 | `55 8b ec 83 c4 d8` | F; p,p | 6cbee4 | none | 49630c,496406,47307c,47a8a4,477548; indirect 472fc2 -> calll *(%edx); 473001 -> calll *(%edx) | hook `stream_destroy` |
| 473334 | `55 8b ec 8b 45 08` | F; p | none | none | 496856 | hook `stream_size` |
| 4968b0 | `ff 25 c0 c3 6c 00` | I; - | 6cc3c0 | none | indirect 4968b0 -> jmpl *0x6cc3c0 | boundary |
| 47307c | `55 8b ec 53` | F; p | none | none | 496874,47b5cc,47b9cc | hook `stream_close` |
| 464390 | `55 8b ec 83 c4 e8` | F; p,p | none | none | 4774f0,464118,473828,477548 | open |
| 49602b | `55 8b ec ff 75 0c` | S8; p,p | 6cbe7c | none | indirect 496039 -> calll *%eax | boundary |
| 495fcd | `55 8b ec 0f b7 45 08` | S4; w | 6cbe68 | none | indirect 495fda -> calll *%eax | boundary |
| 49603f | `55 8b ec 0f b7 45 08` | S4; w | 6cbe80 | none | indirect 49604c -> calll *%eax | boundary |
| 47473c | `55 8b ec 51` | F; p,p,p | 6b566e,6b5a88 | none | 495c12,4968bc,4968c2,496892,4968da,496271,495c5b,495c91,495cac | open |
| 495e7c | `55 8b ec ff 75 10` | S12; w,p,p | 6cbe28 | none | indirect 495e8f -> calll *%eax | boundary |
| 474324 | `a1 50 0e 4a 00 c3` | N; - | 4a0e50 | none | leaf | open |
| 48c444 | `55 8b ec 83 c4 d4` | F; p | 6c5fc8,6c5fcc,6c5fd0,6c5ff4,6c5ff8,6c60a8,6cbc04 | 6c5ff8 | 48db57,48db12,48db70,48d9e4,48bcfc | open |
| 48c57c | `55 8b ec 8b 45 0c` | F; p,p | none | none | 48d9e4 | open |
| 4646dc | `55 8b ec 53` | F; p,p,p | none | none | 473b80 | open |
| 496796 | `ff 25 00 c3 6c 00` | I; - | 6cc300 | none | indirect 496796 -> jmpl *0x6cc300 | boundary |
| 427238 | `55 8b ec 83 c4 ec` | F; p,p,p | none | none | leaf | hook `cell` |
| 488078 | `c8 0e 00 00 8b 55 08 8b` | F; p,p,p | none | none | 4887d8 | open |
| 4880bc | `c8 0c 00 00 56 57` | F; p,p,p | 4a3614,4a5808 | none | indirect 48812c -> calll *0x4a5808 | boundary |
| 452bec | `55 8b ec 8d 45 0c` | F; p,p | none | none | 45275c | open |
| 433968 | `55 8b ec 83 c4 ec` | F; p,p | 6c5ed0 | none | 481350 | open |
| 452434 | `55 8b ec 8b 55 0c` | F; p,p | none | none | leaf | open |
| 487eac | `c8 22 00 00 56 57` | F; p,p | 4a3614 | none | 488974 | open |
| 487f6a | `c8 08 00 00 56 57` | F; p,p | 4a3614,4a3691,4a5800 | none | indirect 487ff7 -> calll *0x4a5800 | boundary |
| 48699c | `c8 00 00 00 57 56` | F; p,p,p | none | none | leaf | open |
| 4869fc | `c8 00 00 00 57 56` | F; p,p | none | none | leaf | open |
| 486c00 | `c8 14 00 00 53 56` | F; p,p,p | none | none | leaf | open |
| 483a04 | `c8 2c 00 00 56 57` | F; p,p | 4a3614,4a3691,4a3695,4a57ac,4a57b0,4a5834 | none | 483c6c,483d6c,486c00; indirect 483b6b -> calll *-0x8(%ebp); 483be9 -> calll *-0x8(%ebp); 483c46 -> calll *-0x8(%ebp); 483c5c -> calll *0x4a5834 | boundary |
| 47465c | `55 8b ec 51` | F; p,p | none | none | 4968bc,4968c8,4968c2,496892,4968da,4968ce,49681a,496271 | open |
| 474360 | `55 8b ec 83 c4 b0` | F; p,p | none | none | 49630c,472e6c,4730e8,473334,4968c2,472f64,496892,47307c,49681a,496778,496271; indirect 4743f8 -> calll *0x18(%eax) | boundary |
| 474708 | `55 8b ec 53` | F; p | none | none | 4962a2,49690a,4968b6 | open |
| 474508 | `55 8b ec 53` | F; p | none | none | 4962a2,49690a,4968b6 | open |
| 47a888 | `55 8b ec 33 d2` | F; p | 6cbee4 | none | leaf | open |
| 4730e8 | `55 8b ec 83 c4 d8` | F; p,p | 4a0d18,6cbee4 | none | 49630c,47307c,4967c6,468064,47749c,47b50c,47b594,47b840,47b974 | hook `stream_open` |
| 49683e | `ff 25 70 c3 6c 00` | I; - | 6cc370 | none | indirect 49683e -> jmpl *0x6cc370 | boundary |
| 496406 | `55 8b ec 8b 45 08` | F; p,p | none | none | leaf | open |
| 47a8a4 | `55 8b ec a1 e4 be 6c 00` | F; p,p | 6cbee4 | none | 477548; indirect 47a8bf -> calll *0x10(%edx) | boundary |
| 477548 | `55 8b ec 53` | F; p | 6bb30c | 6bb30c | 476e2c,47700c,4967a8 | open |
| 496856 | `ff 25 80 c3 6c 00` | I; - | 6cc380 | none | indirect 496856 -> jmpl *0x6cc380 | boundary |
| 496874 | `ff 25 94 c3 6c 00` | I; - | 6cc394 | none | indirect 496874 -> jmpl *0x6cc394 | boundary |
| 47b5cc | `55 8b ec 53` | F; p | none | none | indirect 47b5dc -> calll *0x10(%eax) | boundary |
| 47b9cc | `55 8b ec 53` | F; p | none | none | indirect 47b9dc -> calll *0x10(%eax) | boundary |
| 4774f0 | `55 8b ec 53` | F; p | 6bb304 | none | 476edc,476f0c,49681a,47747c | open |
| 464118 | `55 8b ec 53` | F; p,p | none | none | 4739ac,4968e6 | open |
| 495c12 | `55 8b ec ff 75 10` | S12; w,p,p | 6cbdc0 | none | indirect 495c25 -> calll *%eax | boundary |
| 4968bc | `ff 25 c8 c3 6c 00` | I; - | 6cc3c8 | none | indirect 4968bc -> jmpl *0x6cc3c8 | boundary |
| 4968c2 | `ff 25 cc c3 6c 00` | I; - | 6cc3cc | none | indirect 4968c2 -> jmpl *0x6cc3cc | boundary |
| 496892 | `ff 25 ac c3 6c 00` | I; - | 6cc3ac | none | indirect 496892 -> jmpl *0x6cc3ac | boundary |
| 4968da | `ff 25 dc c3 6c 00` | I; - | 6cc3dc | none | indirect 4968da -> jmpl *0x6cc3dc | boundary |
| 496271 | `55 8b ec 57` | S12; p,p,p | 6cbd5c,6cbe34 | none | indirect 496298 -> calll *%eax | boundary |
| 495c5b | `55 8b ec ff 75 10` | S12; w,w,p | 6cbdc8 | none | indirect 495c70 -> calll *%eax | boundary |
| 495c91 | `55 8b ec ff 75 10` | S12; w,w,p | 6cbdd0 | none | indirect 495ca6 -> calll *%eax | boundary |
| 495cac | `55 8b ec 0f b7 45 0c` | S8; w,w | 6cbdd4 | none | indirect 495cbe -> calll *%eax | boundary |
| 48db57 | `55 8b ec 8b 45 10` | F; p,p,p | none | none | 47fa20 | open |
| 48db12 | `55 8b ec 83 c4 f4` | F; p | none | none | 48dc43,4951d6,480304 | open |
| 48db70 | `55 8b ec 53` | F; p,p,p,p | none | none | 48d9a7 | open |
| 48d9e4 | `55 8b ec 8b 45 10` | F; p,p,p | none | none | 48043c | open |
| 48bcfc | `55 8b ec 83 c4 ec` | F; p,p | none | none | 4953c4,4922d3,495157,48d9e4,48d9fd,48da08,48da21,48da3f | open |
| 473b80 | `55 8b ec 53` | F; p | none | none | 474350,495d9f | open |
| 4887d8 | `55 8b ec 83 c4 f8` | F; p,p,p | 4a3614,4a5814,4a581c | none | 49545b,488df8; indirect 488865 -> calll *0x4a5814; 48888a -> calll *0x4a581c; 4888b4 -> calll *0x4a581c | boundary |
| 45275c | `55 8b ec 83 c4 f8` | F; p,p | 6c5ed0 | none | 481350,452434 | open |
| 481350 | `55 8b ec 8b 45 08` | F; p,p | none | none | indirect 481362 -> jmpl *0x481369(,%edx,4) | boundary |
| 488974 | `55 8b ec 83 c4 f8` | F; p,p,p | 4a3614,4a3662,4a5818 | none | 49545b,49681a; indirect 488a2e -> calll *0x4a5818; 488a56 -> calll *0x4a5818; 488a8e -> calll *0x4a5818 | boundary |
| 483c6c | `c8 00 00 00 57 56` | F; p,p,p | none | none | leaf | open |
| 483d6c | `c8 00 00 00 57 56` | F; p,p,p | none | none | leaf | open |
| 4968c8 | `ff 25 d0 c3 6c 00` | I; - | 6cc3d0 | none | indirect 4968c8 -> jmpl *0x6cc3d0 | boundary |
| 4968ce | `ff 25 d4 c3 6c 00` | I; - | 6cc3d4 | none | indirect 4968ce -> jmpl *0x6cc3d4 | boundary |
| 472e6c | `55 8b ec 53` | F; p | 6cbee4 | none | 47a888 | open |
| 496778 | `ff 25 ec c2 6c 00` | I; - | 6cc2ec | none | indirect 496778 -> jmpl *0x6cc2ec | boundary |
| 4962a2 | `55 8b ec 66 83 3d 5c bd` | S4; p | 6cbd5c,6cbe38 | none | indirect 4962bd -> calll *%eax | boundary |
| 49690a | `ff 25 fc c3 6c 00` | I; - | 6cc3fc | none | indirect 49690a -> jmpl *0x6cc3fc | boundary |
| 4968b6 | `ff 25 c4 c3 6c 00` | I; - | 6cc3c4 | none | indirect 4968b6 -> jmpl *0x6cc3c4 | boundary |
| 4967c6 | `ff 25 20 c3 6c 00` | I; - | 6cc320 | none | indirect 4967c6 -> jmpl *0x6cc320 | boundary |
| 468064 | `55 8b ec 53` | F; p,p,p | none | none | 47cdf8 | hook `vol_lookup` |
| 47749c | `55 8b ec 53` | F; p | 6bb304 | none | 476edc,476f0c,49681a,47747c | open |
| 47b50c | `55 8b ec 53` | F; p | 6cbee4 | none | 47a888,47b574 | open |
| 47b594 | `55 8b ec 53` | F; p,p | none | none | 47b5cc,4930ab | open |
| 47b840 | `55 8b ec 53` | F; p | 6cbee4 | none | 47a888,47b8a8 | open |
| 47b974 | `55 8b ec 53` | F; p,p | none | none | 47b9cc,47bcdc,4930ab | open |
| 476e2c | `55 8b ec 56` | F; p,p | none | none | leaf | open |
| 47700c | `55 8b ec 53` | F; p,p | none | none | leaf | open |
| 4967a8 | `ff 25 0c c3 6c 00` | I; - | 6cc30c | none | indirect 4967a8 -> jmpl *0x6cc30c | boundary |
| 476edc | `55 8b ec 8b 45 08` | F; p | none | none | leaf | open |
| 476f0c | `55 8b ec 51` | F; p,p,p | none | none | 49681a | open |
| 47747c | `55 8b ec 8b 45 08` | F; p | 6bb30c | 6bb30c | 49686e | open |
| 4739ac | `55 8b ec 53` | F; p | none | none | 495fe0,49603f,4749d0,474350,495e95 | open |
| 48dc43 | `55 8b ec 8b 45 08` | F; p | none | none | 47df19 | open |
| 4951d6 | `55 8b ec 53` | F; p | none | none | 495201 | open |
| 48d9a7 | `55 8b ec 8b 45 08` | F; p,p,p | none | none | leaf | open |
| 4922d3 | `55 8b ec 53` | F; p | none | none | 492428 | open |
| 495157 | `55 8b ec 8b 45 08` | F; p | none | none | leaf | open |
| 48d9fd | `55 8b ec 8b 45 08` | F; p | none | none | leaf | open |
| 48da08 | `55 8b ec 8b 45 10` | F; p,p,p | none | none | 4800c0 | open |
| 48da21 | `55 8b ec 8b 45 08` | F; p,p | none | none | leaf | open |
| 48da3f | `55 8b ec 8b 45 08` | F; p,p | none | none | leaf | open |
| 495d9f | `55 8b ec 0f b7 45 10` | S12; w,w,w | 6cbe00 | none | indirect 495db6 -> calll *%eax | boundary |
| 488df8 | `55 8b ec 81 c4 74 ff ff` | F; p,p,p | 4a3614 | none | 48929c,4892a7,4892b4,4892c3,4892e1,4892ff,48931d,48933b,489353,489362,48937b | open |
| 47cdf8 | `55 8b ec 83 c4 ec` | F; p,p,p | none | none | 47d198,47d2c4,496868; indirect 47cf05 -> calll *0x8(%ecx); 47cf17 -> calll *0x18(%ecx) | hook `member_lookup` |
| 47b574 | `55 8b ec 8b 45 08` | F; p | none | none | leaf | open |
| 4930ab | `55 8b ec 8b 45 08` | F; p | none | none | leaf | open |
| 47b8a8 | `55 8b ec 83 c4 dc` | F; p | 6cbee4 | none | 49630c,47749c,47bc5c,4774f0 | open |
| 47bcdc | `55 8b ec 53` | F; p,p | none | none | leaf | open |
| 49686e | `ff 25 90 c3 6c 00` | I; - | 6cc390 | none | indirect 49686e -> jmpl *0x6cc390 | boundary |
| 495fe0 | `55 8b ec 0f b7 45 08` | S4; w | 6cbe6c | none | indirect 495fed -> calll *%eax | boundary |
| 4749d0 | `55 8b ec 53` | F; w | 6b566e,6b5a88 | none | 495cc4,495c43,4968ce,495c76,4962a2,49690a,4968b6 | open |
| 495e95 | `55 8b ec 0f b7 45 0c` | S8; w,w | 6cbe2c | none | indirect 495ea7 -> calll *%eax | boundary |
| 495201 | `55 8b ec 8b 45 08` | F; p | none | none | leaf | open |
| 492428 | `55 8b ec 8b 45 08` | F; p | none | none | leaf | open |
| 4800c0 | `c8 00 00 00 56 57` | F; p,p,p | none | none | leaf | open |
| 48929c | `55 8b ec 8a 45 08` | F; u8 | none | none | leaf | open |
| 4892a7 | `55 8b ec 8a 45 08` | F; u8 | none | none | leaf | open |
| 4892b4 | `55 8b ec f6 45 08 40` | F; p | none | none | leaf | open |
| 4892c3 | `55 8b ec 8b 45 10` | F; p,p,p,p | 4a5810 | none | indirect 4892d6 -> calll *0x4a5810 | boundary |
| 4892e1 | `55 8b ec 8b 45 10` | F; p,p,p,p | 4a580c | none | indirect 4892f4 -> calll *0x4a580c | boundary |
| 4892ff | `55 8b ec 8b 45 10` | F; p,p,p,p | 4a5808 | none | indirect 489312 -> calll *0x4a5808 | boundary |
| 48931d | `55 8b ec 8b 45 10` | F; p,p,p,p | 4a5804 | none | indirect 489330 -> calll *0x4a5804 | boundary |
| 48933b | `55 8b ec 83 7d 08 00` | F; p,p | none | none | leaf | open |
| 489353 | `55 8b ec f6 45 08 c0` | F; p | none | none | leaf | open |
| 489362 | `55 8b ec 8b 45 08` | F; p | none | none | leaf | open |
| 48937b | `55 8b ec 80 7d 08 40` | F; p | none | none | leaf | open |
| 47d198 | `55 8b ec 53` | F; p,p,p,p,p | none | none | 496760,4967cc,4967a2 | open |
| 47d2c4 | `55 8b ec 53` | F; p,p,p,p,p | none | none | 4967ea | open |
| 496868 | `ff 25 8c c3 6c 00` | I; - | 6cc38c | none | indirect 496868 -> jmpl *0x6cc38c | boundary |
| 47bc5c | `55 8b ec 53` | F; p | 6cbee4 | none | 4774f0 | open |
| 495cc4 | `55 8b ec 0f b7 45 0c` | S8; w,w | 6cbddc | none | indirect 495cd6 -> calll *%eax | boundary |
| 495c43 | `55 8b ec 0f b7 45 0c` | S8; w,w | 6cbdc4 | none | indirect 495c55 -> calll *%eax | boundary |
| 495c76 | `55 8b ec ff 75 10` | S12; w,w,p | 6cbdcc | none | indirect 495c8b -> calll *%eax | boundary |
| 496760 | `ff 25 dc c2 6c 00` | I; - | 6cc2dc | none | indirect 496760 -> jmpl *0x6cc2dc | boundary |
| 4967cc | `ff 25 24 c3 6c 00` | I; - | 6cc324 | none | indirect 4967cc -> jmpl *0x6cc324 | boundary |
| 4967a2 | `ff 25 08 c3 6c 00` | I; - | 6cc308 | none | indirect 4967a2 -> jmpl *0x6cc308 | boundary |
| 4967ea | `ff 25 38 c3 6c 00` | I; - | 6cc338 | none | indirect 4967ea -> jmpl *0x6cc338 | boundary |
