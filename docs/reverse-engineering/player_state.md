# Player state

The player actor's layout, heading and speed conventions, the HUD speed
conversion and the world-distance scale, as calibrated on a live drive of the
original. [player_movement.md](player_movement.md) covers the
animation-driven integration, write order, integer heading tables and both
HUD branches; the hooks that replace those functions are in
[hooks.md](../reference-game/hooks.md#movement-leaves).

Evidence directory: `<es2-data>\logs\calibration-20261005-01`
(`calibration-summary.json`, `movement-validation.json`,
`movement-analysis-filtered.json`, `hud-speed-configuration.json`,
`user-observation.json`, the disassembly listings
`speed-conversion-disassembly.txt`, `speed-hud-disassembly.txt`,
`distance-*-disassembly.txt`, `approximate-distance-disassembly.txt`).

## Calibration run

A visible clone (`calibration-r111`) was driven by the user: waiting, driving
at a reported maximum of 95 km/h, turns, combat, further movement, a pause.
The boundary observer ran for its five-minute limit, restored its debug
registers and detached. The primary installation (72 files) stayed unchanged
(`primary-preservation.json`).

The trace has 2,759 consecutive boundaries (counters 78 through 2836) with one
actor pointer. 2,040 adjacent pairs change position, 718 do not, 183 change
the planar heading. The analyzer keeps every raw phase but excludes pairs with
a pause flag or a completed-clock/QPC gap above 250 ms from ratios,
correlation and stable-run selection (5 pairs); use
`movement-analysis-filtered.json`. Command/speed/heading stability gives 24
candidate runs; the selection does not prove the absence of combat, terrain
effects or walking animation.

## Layout and direction

| Item | Address | Notes |
|---|---|---|
| Active actor pointer | `0x004d24e6` | input processing selects it from the entity list by its player flag |
| Active camera pointer | `0x004d24ea` | the neighbour; not the player |
| Boundary site | `0x0045f4ad` | before the frame counter's increment; the observer read a `0x292`-byte actor block |
| Position | `actor+0x26,+0x2a,+0x2e` | signed 32-bit integers |
| Angles | `actor+0x0c,+0x0e,+0x10` | three unsigned 16-bit words; word 2 is the planar heading, one turn is 65536 units |
| Signed speed | `actor+0x28e` | signed 16-bit, observed −35 to 276 |
| Command | `actor+0x290` | signed Q10: normalized command is `raw/1024`, full forward 1024, neutral 0; observed −219 to 1024 |
| HUD configuration | `uint32(actor+0x1f2)` | identical pointer in every captured actor block |

In raw coordinate components 0/1 forward motion follows

```text
theta = heading_u16 * 2*pi/65536
forward direction = (-sin(theta), cos(theta))
```

and negative signed speed reverses it. No north/east labels are assigned.

| Segment | Duration | Raw displacement | Heading | Direction error |
|---|---:|---|---:|---:|
| Sequences 168–603 | 20.391 s | `[16412,83853,-737]` | 63524 | −0.0219° |
| Sequences 1392–1557 | 7.734 s | `[32585,3107,-71]` | 50145 | −0.0080° |

Both runs have constant heading and forward command 1024; the second has
constant speed raw value 276. Reverse runs show the opposite direction. The
runs support the player identity, the position-vector layout and the heading
convention; they do not calibrate physical units. The last 221 intervals have
unchanged position and both words zero. Key bindings were not recorded.

Own code: `es2_tick_record_actor` (`sim/oracle/tick_record.c`) writes these
fields (`position_raw_i32`, `angles_raw_u16`, `speed_raw_i16`,
`throttle_raw_q10`) into the tick log
([tick_log.md](../reference-game/tick_log.md)); `sim/src/hud_draw.c` and
`sim/src/cockpit.c` read the player through `0x004d24e6`.

## HUD speed conversion

The renderer `0x0043dca0` loads the active actor, calls `0x0041bb3c`, converts
the result to decimal and appends the original literal `" K/H"` (`0x0049c112`).
For the ground-vehicle branch, `int16(config+0x50)==0` with
`config=uint32(actor+0x1f2)`:

```text
display_KH = trunc_toward_zero(
    int16(actor+0x28e) * int16(config+0xc2)
    / int16(config+0x06)
)
```

The signed loads, multiplication and division are at `0x0041bb7f–0x0041bb94`;
this branch has no weight/damage multiplier. The other branch
(`config+0x50 != 0`, `actor+0x2bd`, `config+0x206`) is in
[player_movement.md](player_movement.md).

A read-only inspection of the calibration process after the trace found
`config+0x50 = 0`, `config+0x06 = 276`, `config+0xc2 = 99` and the HUD string
`0 K/H` at raw speed 0. With these values raw 265–267 shows 95 K/H and raw 276
shows 99 K/H. The user's 95 km/h is an independent observation; HUD frames and
configuration were not captured continuously, so it cannot be matched to a
raw value. No universal `95/276` factor applies.

Own code: `es2_hud_speed` (`core/src/movement.c`, both branches) replaces
that function; `speed_text` (`sim/src/hud_draw.c`, service row at `0x0043dca0`)
calls it on `[0x4d24e6]`.

## World-distance scale

**1000 raw position units are 6 game metres**, so one unit is 6 game
millimetres.

The waypoint/target renderer reads the player position at `+0x26/+0x2a`
unscaled (`0x0043c51c–0x0043c52d`); target coordinates come directly from
another actor or waypoint. The distance path `0x0043c532 → 0x0047d694 →
0x00492a30` subtracts them directly, and the result reaches the conversion
`0x0043c749 → 0x00434228`:

```text
displayed_metres = 6 * trunc_toward_zero(raw_distance / 1000)
```

The renderer appends the literal `" M."` (`0x0049c0fc`). Division comes
first, so the display moves in six-metre steps. The 2D distance is
approximate, `max(abs(dx),abs(dy)) + floor(min(abs(dx),abs(dy))/2)`, not the
Euclidean norm; a separate 3D path also consumes unscaled components before
its own approximate norm. Store exact raw integers, or multiply by 6 for
integer game millimetres.

Own code: `radar_units` (`sim/src/hud_draw.c`, `0x00434228`) is
`(v idiv 1000) * 6`; the waypoint line is in `sim/src/gadgets.c`
(`VA_RADAR_UNITS`).

## Derived state log

`derive-calibrated-state.py` (evidence directory) derives from the immutable
trace and the captured configuration one state per boundary (2,759): raw and
millimetre positions, wrapping angles, heading degrees, Q10 and normalized
command, reconstructed HUD speed, and 2,753 finite-difference velocities in
m/s (the first state and the 5 pause/gap intervals have none; the exact
millimetre displacement and elapsed clock milliseconds are kept beside them).
It refuses an existing output; the output file `player-state-calibrated.jsonl`
is not kept in the evidence directory. The HUD speed uses the configuration
read after the trace; the velocity is a finite difference of world positions,
not an internal physics field. Both limits are in the artifact header.

## Limits

Constant scalar speed does not give constant per-boundary displacement:
ground motion passes through animation progress and transformed position
writes (`0x00418f40 → 0x004796d8`, then `0x00402604 → 0x0040250c`, writes at
`0x00402590–0x004025a1`; see [player_movement.md](player_movement.md)).
Terrain and combat also influence a run. Keep the HUD speed separate from the
position-derived velocity. Not established: remaining angular-axis names, and
deterministic replay from this manual drive.
