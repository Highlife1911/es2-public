# In-process tick log (schema 2)

The per-frame log the proxy writes inside the original DBSIM. Own code: the proxy side is [tick_log.h](../../tools/reference-game/proxy/tick_log.h) (site patch, guest reads, I/O, proxy-only fields); the line formatting is the shared library `//sim/oracle:tick_record` ([tick_record.h](../../sim/oracle/tick_record.h), schema 2 and 3), which the own process links as well, byte for byte the proxy's text. Watches are `//sim/oracle:mem_watch` ([mem_watch.h](../../tools/reference-game/proxy/mem_watch.h)).

## Mechanism

The proxy `ddraw.dll` installs the hook when it initializes in `DBSIM.EXE` (first `DirectDrawCreate` or MCI call, before the main loop). It checks:

- image base `0x00400000` and size `0x2d8000`;
- the boundary bytes `FF 83 BF 00 00 00 EB A3` at `0x0045f4ad`;
- `mov ebx, 0x004d24bc` at `0x0045f176`;
- the `rng_next` prologue at `0x00492ee0`.

Then it replaces the 6-byte `inc dword [ebx+0xbf]` in memory with `call thunk; nop`. The thunk saves all registers and EFLAGS, records one line, restores them and executes the original increment, so the flags and the counter behave exactly as before. The file on disk is unchanged. The output file must not exist (`CREATE_NEW`); otherwise nothing is patched.

Configuration in the clone's `es2-oracle.ini` (written by [configure-tick-log.ps1](../../tools/reference-game/setup/configure-tick-log.ps1), which refuses the primary installation):

```ini
[trace]
tick_log=<run-dir>\<run>.jsonl
tick_log_max=0          ; 0 = 1,000,000 records
tick_log_actor_raw=0    ; 1 adds the 0x292-byte actor block per tick
```

Runs are driven by [run-tick-log-mission.ps1](../../tools/reference-game/setup/run-tick-log-mission.ps1) (deploy, launch, wait for N ticks, stop after an identity check of path and start time). Two logs are compared with [compare_tick_logs.py](../../tools/reference-game/scripts/compare_tick_logs.py). With virtual time, identical runs are achieved ([timing.md](../reverse-engineering/timing.md#virtual-time)); each tick then also carries `vt_limiter_polls`, `vt_stall_advances` and `vt_late_polls`.

### Writing and buffering

Every record is written immediately by default. Optional batching writes the same bytes in the same order, with a worker that drains even when the game loop stalls:

```ini
[trace]
tick_log_buffered=1
tick_log_target=1600
tick_log_flush_ms=100
```

`tick_log_target` is a publication target, not a record limit: its tick and watches are flushed together; subsequent ticks are immediate. Other flush triggers: capacity, the bounded interval (1-1000 ms), the record limit, verified runtime exit and a runner request. Short writes are retried; failed writes retain the pending bytes and prevent acknowledgement. `WriteFile` completion makes the bytes visible to the runner; durability across power loss is not claimed.

`run-tick-log-mission.ps1 -BufferedTickLog` supplies its `-Ticks` as the target. Before a stall snapshot or a forced stop it signals the PID-specific flush event (`Request-TickFlush` in `OracleSpeed.psm1`) and waits for the acknowledgement; without it the stop is refused. Path and start time are checked again afterwards and immediately before stopping. DLL detach does not wait on the worker: the normal and immediate runtime exit callbacks drain it, and forced stops use the handshake. A crash or an unrelated external kill can still lose buffered bytes.

The runner reads complete appended lines through an incremental cursor, including markers split across reads. The polling interval is 500 ms; a stall is a positive tick count without progress; zero ticks use the overall timeout. `-PreparedProxy` reuses a hash-checked build whose fixture run and source stamp are recorded.

### Hold at a record

```ini
[trace]
hold_frame=650          ; 0 (default) = never
```

After the `hold_frame`-th tick record (with its watches) the proxy appends `{"event":"hold","sequence":N}`, flushes (buffered too), leaves the tick I/O lock and parks the game thread in a `Sleep` loop inside the thunk. The thread never returns: the increment `inc dword [ebx+0xbf]` at `0x0045f4ad` and the `jmp` to the loop top `0x0045f458` of that iteration do not run, so the frame of the last record ran up to the boundary and nothing of the next frame runs (no limiter, no message pump, no logic, no render). The proxy-only tick work after the record (`live_coverage_tick`, `vol_probes_tick`, `perf_tick`) still runs; no exit path is called and no earlier record changes. The flush worker keeps answering the runner's flush request; other game threads are not stopped. The count is of records (`sequence`, as the runner's `ticks_seen`), not of counter values: the last record carries the first record's `frame_counter_before_increment` plus `N-1`.

`run-tick-log-mission.ps1 -HoldAtTicks` writes `hold_frame=<Ticks>` (`configure-tick-log.ps1 -HoldFrame`) and adds `hold_frame` to its JSON line; the runner then sees exactly `Ticks` records and stops the process as usual. It exists for runs without the limiter (tape playback, [oracle_regression.md](oracle_regression.md#tape-suites)), where the 500 ms poll overshoots by hundreds of frames. Without the switch the INI and the report keep their bytes. Fixture modes `tick_log_hold_test` and `tick_log_hold_buffered_test` (a synthetic loop thread: exactly 5 records, the thread parked, the 5th increment not executed, everything flushed without a shutdown); `tick_log_valid_test` checks the default is off.

The opt-in `[perf]` section writes a separate JSON report and adds no tick fields. The fixed-QPC fixture compares all-hook ticks and watch changes byte for byte with profiling off/on and buffering off/on. Actual QPC, PID and heap-address diagnostics differ across live runs; the normalization rules and goldens do not depend on them.

## Format

JSON Lines. First line `start` (schema, pid, tid, image base, boundary, QPC frequency, limit, `watches`). Then one `tick` per completed active loop iteration:

| Field | Source |
|---|---|
| `sequence` | 1-based record number |
| `qpc` | `QueryPerformanceCounter` at the hook |
| `ebx` | loop context (always `0x004d24bc`) |
| `frame_counter_before_increment` | u32 `[0x4d257b]` |
| `completed_clock_ms` | u32 `[0x4d3b74]` |
| `limiter_bypassed`, `loop_pause_flag` | `[ebx+0x1a]`, `[ebx+0xb6]` |
| `clock_raw_hex` | 24 bytes at `0x4d3b64` (scales, references, limiter clocks) |
| `rng_a_hex`, `rng_b_hex` | full 0x72-byte state of stream A `0x4d2599` and B `0x4d260b` ([rng.md](../reverse-engineering/rng.md)) |
| `player_actor` | pointer `[0x4d24e6]` (diagnostic only; never part of a cross-run comparison) |
| `position_raw_i32`, `angles_raw_u16`, `speed_raw_i16`, `throttle_raw_q10` | actor `+0x26`, `+0x0c`, `+0x28e`, `+0x290` ([player_state.md](../reverse-engineering/player_state.md)) |
| `actor_raw_hex` | optional (`tick_log_actor_raw=1`) |
| `in_mode`, `in_recorded`, `in_injected`, `in_dropped`, `in_fallbacks`, `in_focus_dropped` | input journal counters ([input_journal.md](input_journal.md)) |
| `hk_*` | cumulative hook counters, present only with a `[hooks]` section ([hooks.md](hooks.md)) |
| `rtl_*` | CW3220 import shim fields ([rtl_imports.md](rtl_imports.md#tick-fields-and-the-exit-record)) |

The `hk_*` counters are `uint64` and count replacement work only, across all actors; subtract successive ticks for the activity of one iteration. No `[hooks]` section leaves the tick byte-identical. The ground hooks add diagnostics after their parent's four counters, only when the parent hook is non-original:

| Field | Content, in this order |
|---|---|
| `hk_ground_update_call_<address>` | outgoing calls: `402628`, `418f40`, `46e180`, `418f74`, `4027fc`, `492ee0`, `462830`, `4792ac`, `4195c8` |
| `hk_ground_collide_call_<address>` | direct calls: `404ae4`, `46e498`, `492f24`, `434010`, `462830` |
| `hk_ground_collide_slot_<offset>` | virtual calls, keyed by slot rather than resolved method: `20`, `5c`, `68`, `70`, `74`, `7c` |
| `hk_ground_collide_path_contact`, `hk_ground_collide_path_slope_drift`, `hk_ground_collide_path_slide_damage` | step-3 entry, step-6 drift accumulation (excluding the uphill return), step-7 damage above the slide threshold |

`oracle_regression.py:TICK_FIELDS` and the `fields` list in `compare_tick_logs.py` select the simulation fields and exclude these diagnostics like every other `hk_*` counter; a synthetic normalization fixture checks both tools, including a negative control with a changed clock.

The static line buffer is `TICK_LOG_LINE_MAX` (262144 bytes, `tick_log.h`); the `bound` fixtures of `path_counters` and `exit_counters` check the maximum with every hook replaced and every counter at `UINT64_MAX`. A final `limit_reached` line appears when the record limit is hit; the hook then passes through without logging.

## Memory watches

`[trace] watch_count` and `watchN = NAME ADDRESS LENGTH` add `watch` events after a tick when a named range changed:

- `{"event":"watch","sequence","frame","name","address","length","hex"}` for the first record and for ranges up to 256 bytes;
- `"diff":[[offset,"hex"],...]` for later changes of larger ranges;
- `"unreadable":true` once when the pointer chain or range becomes unreadable.

Address syntax, length syntax and the mission watch set: [mem_watch.h](../../tools/reference-game/proxy/mem_watch.h) and the sets in `tools/reference-game/watch-sets/`. A malformed watch refuses the whole log.

Each hop, count and final range is read by a fresh current-process `ReadProcessMemory` copy; the final copy only probes accessibility. Comparison, formatting and saving of previous bytes use the original reads, including reading previous bytes after emission. No address or protection is cached between watches or frames. A failed or short copy falls back to `VirtualQuery` plus copy, preserving the protection and `LastError` rules. A sample taken during a concurrent modification has no atomic snapshot contract.

## Compact RNG format (schema 3)

Schema 2 is the default and carries complete RNG fields. `-CompactRng` on configure/run-tick-log-mission writes `[trace] tick_log_compact_rng=1`: the log then uses `tick_compact` events and replaces unchanged RNG states after the first tick with `rng_a_same:true` or `rng_b_same:true`. Changed states still carry all 114 bytes as hex. All other fields and watch events are retained. The bounded runner selects the compact event explicitly; ordinary polling never counts compact ticks.

[expand_compact_ticks.py](../../tools/reference-game/scripts/expand_compact_ticks.py) `INPUT OUTPUT` expands losslessly; it refuses missing state and broken sequence order. The oracle runner has no automatic compact mode, and goldens use schema 2. Synthetic writer/decoder tests and byte-exact round trips of saved logs pass; live compact-writer validation is open, and the measured size saving is about 3% with all hook fields enabled.

## Exit record

Hooks that run after the last tick never show their calls in a tick: the result writer `0x42412c` runs `damage_score` and `result_conditions` once per mission after the loop has ended. The proxy therefore writes one final record with every `hk_*` counter:

```json
{"event":"exit_counters","reason":"shutdown","last_sequence":...,"frame_counter":...,"hk_rng_next_calls":...,...}
```

- `reason`: `shutdown` (clean exit: `tick_log_shutdown` from DLL detach for immediate logs, or the runtime exit hooks `atexit`/`_exit` for buffered logs) or `runner_flush` (the flush request of `Request-TickFlush`, sent by `run-tick-log-mission.ps1 -BufferedTickLog` before a forced stop).
- `last_sequence`: the number of tick records written; `frame_counter`: u32 `[ebx+0xbf]` at that moment (diagnostic).
- The `hk_*` fields are exactly the ones a tick would carry then (`rng_hooks_tick_fields`: same names, order and omission rules). The record shares the tick's static line buffer; one that did not fit would carry `"counters_overflow":true` instead.

Rules:

- At most one record per log; only with a `[hooks]` section or the CW3220 import shim (`rtl_*` fields, [rtl_imports.md](rtl_imports.md#tick-fields-and-the-exit-record)); without either the log stays byte-identical; only while the log file is open (none after `limit_reached`).
- No tick record changes, no allocation and no thread is added; the record is formatted under the existing tick I/O lock.
- A process terminated without either path (an immediate log stopped by the runner's `Stop-Process`, a crash) has no exit record. With an immediate log only the suites that end by themselves (the outcome suites) get one.
- With `runner_flush` the record is a snapshot taken by the flush worker while the game still runs: ticks logged between the acknowledgement and the stop may follow it, with counters at least as large. The exit callbacks of a later clean exit write no second record.

Consumers:

- `oracle_regression.py normalize` never compares the record, independent of `--frame-limit`. It copies the counters into the normalized run (`<suite>-normalized.json`) as `exit_counters` (`{"hk_...": n}` in record order) plus `exit_record` (`reason`, `last_sequence`, `frame_counter`); the summary line gets `exit_counter_fields`. Both keys are absent when the log has no record, so such logs normalize to the same bytes as before.
- `compare_tick_logs.py` and `bisect_hooks.py` read only `tick` events.
- A script that takes counters from the last line containing `"hk_"` reads the exit record for runs that end by themselves.

## Validation

Fixtures:

- [tick_log_fixture.c](../../tools/reference-game/proxy/tests/tick_log_fixture.c), targets `//tools/reference-game/proxy:tick_log_*_test`: synthetic image with an own loop stub. Cases: valid (100 records, counter continues, patch bytes, all fields), limit, watch (pointer chain, counted length, whole/diff/unreadable records, syntax errors), signature mismatch (nothing patched, no file), existing output (refused, file untouched). The mission-map guard tests also pass.
- [exit_counters_fixture.c](../../tools/reference-game/proxy/tests/exit_counters_fixture.c), targets `//tools/reference-game/proxy:exit_counters_{shutdown,runner,no_hooks,limit,bound}_test`: ticks unchanged, one final record equal to the tick field set, the flush-request path before the acknowledgement, no record without `[hooks]` or after the limit, the maximum size.
- [test_exit_counters_scripts.py](../../tools/reference-game/proxy/tests/test_exit_counters_scripts.py), target `//tools/reference-game/proxy:exit_counters_scripts_test`: the record is ignored by `compare` in both directions and by `compare_tick_logs.py`, copied into the normalized run and the summary, and previous bytes are the same without it.

Live (`timing-r111`, prepared Alpha-sector mission launched with the original DBSIM arguments, window hidden, no input): the log has one record per counter from 0 without a gap; a run with pause phases (`loop_pause_flag=1`, no counter increments) and a run without a pause (clock deltas 46 and 47 ms) both log completely.

The log starts at counter 0, the first loop iteration of the mission, which an external observer cannot capture. `tick_log_installed` appears in `video-compat.jsonl` for the process. The live validation of the exit record is open.

### Determinism without virtual time

Two runs with identical mission inputs and an idle player: stream A is identical at counters 0-104 and first differs at counter 105, where one run measured a 46 ms clock delta and the other 47 ms. Stream B never changed (no presentation draws while idle). Player position, angles, speed and throttle are identical for all common counters. This supports the static finding that the RNG has a fixed seed and that the measured elapsed time changes the draw sequence, which virtual time removes. Whether the hidden-window pauses are caused by focus loss is not established.
