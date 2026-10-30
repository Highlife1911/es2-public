# VSHELL function map

The instrument that counts entries of `VSHELL.EXE` functions: entry probes in
the proxy. The DBSIM counterpart is [dbsim_map.md](dbsim_map.md). Addresses
refer to the reference `VER95\VSHELL.EXE` (image base
`0x00400000`, [reference_version.md](../reference_version.md)).

## The probes

`tools/reference-game/proxy/vshell_probes.h` runs the probe engine of the
DBSIM map (`entry_probe_engine.h`) on the known function starts of
`VSHELL.EXE`: the first byte of every start becomes `INT3`; the vectored
handler counts the entry and emulates the original first instruction, so no
byte is restored temporarily. At the end of the process the counts go to a
JSON report. Nothing else in VSHELL changes. DBSIM's paths return in every
process but `DBSIM.EXE`, this one in every process but `VSHELL.EXE`, so the
engine instance is never shared.

**Sites.** `tools/reference-game/proxy/vshell_probe_inventory.h` is generated
by `scripts/probe_instruction_inventory.py --module vshell` from the reference
file (the script refuses any other SHA-256). It holds the same two tables as
the DBSIM header under the `vshell_probe_` prefix, plus a third:

| Table | Rows | What |
|---|---|---|
| `vshell_probe_instructions` | 14,188 | one-byte `PUSH r32` boundaries (linear sweep plus synchronized descent): 14,046 linear, 148 added, 6 removed |
| `vshell_probe_entry_kinds` | 300 | known starts whose first instruction is one of the emulated kinds: 130 import thunks `jmp [abs32]`, 64 `enter`, 21 `call rel32`, 17 `mov eax,[abs32]`, … |
| `vshell_probe_function_starts` | 2,139 | every probed known start (1,839 push, 300 kinds): the default site list |

Known starts (2,161) come from the entry point, the three exports, 765
relocated `push ebp; mov ebp, esp` targets, 1,490 direct call targets of the
linear sweep and 8 operand pointers, as in DBSIM. **22 starts are not
probed**: their first instruction is `push dword [abs32]` (`ff 35`, 21 starts)
or `mov dword [abs32], imm32` (`c7 05`, 1 start, `0x4325cc`), which the engine
does not emulate: `0x431718`, `0x4317c3`, `0x4325cc`, `0x433a7b`, `0x436da7`,
`0x43b758`, `0x43b8cd`, `0x43bd09`, `0x43c349`, `0x43fc62`, `0x44218a`,
`0x4446d1`, `0x44483e`, `0x444fa4`, `0x447e25`, `0x4482ad`, `0x44c322`,
`0x44c343`, `0x44d1b9`, `0x44d5fe`, `0x44da87`, `0x44e217`. A new engine kind
(shared with DBSIM) would cover them. Starts reached only by a jump (for
example the `__startup` thunk `0x46b9be`) are not known starts.

Checks: `vshell_probes_reference_test` checks every row against the file's
bytes; `probe_instruction_inventory_unit_test` regenerates the header byte for
byte.

**Arming** (own code and own import slots by `vshell_probes_engine_test`; the
slot addresses by `vshell_probes_reference_test` on the file mapped as an
image resource):

1. `DLL_PROCESS_ATTACH`, only in `VSHELL.EXE` and only when
   `ES2_LIVE_COVERAGE_CONFIG` names an INI with `[vshell_probes] enabled=1`:
   the loaded image is checked (PE header, base `0x400000`, SizeOfImage
   `0x1ea000`) and VSHELL's import slot of cw3220 `__startup` (`0x5de2ec`) is
   pointed at the proxy. The entry `0x401000` ends in `jmp 0x46b9be`, the
   thunk `jmp [0x5de2ec]`.
2. At that `__startup` call (all DLLs initialised, outside the loader lock)
   the slot gets its original value back, then the INI is read, the module
   file's SHA-256 checked, the report registered at cw3220 `_atexit` (normal
   end: WinMain returns, [vshell_process.md](../reverse-engineering/vshell_process.md),
   section 9) and VSHELL's `__exit` slot (`0x5de2dc`, the assertion ends of
   section 9) taken over; the probes are installed and a first report
   (`"reason":"startup"`) written. Then the original `__startup` runs with
   every register and flag as before.

The entry `0x401000`, its callee `0x46b68d` and the thunk `0x46baae` run
before step 2; their start-up entries are not counted. For more than 512
sites the engine's `ep_validate` checks them in chunks of 512, the sorted
order covering the overlaps between chunks. The handler has no limit; its
lookup is linear over the sites, so `mode=saturate` is the cheap mode for a
pure "was it entered" map.

## Switching the probes on

An INI file (any path) named by the environment variable
`ES2_LIVE_COVERAGE_CONFIG` of the process that starts the game (the
launcher's children inherit it):

```ini
[vshell_probes]
enabled=1
; one report per VSHELL process: VSHELL ends at every mission start and returns
report=<run-dir>\probes-{pid}.json
; count (default) or saturate (stop counting a site after saturate_after entries)
mode=count
saturate_after=1
; all (default) or a comma list of hex VAs (known starts or push rows)
entries=all
```

Without the variable, or with `enabled=0` or no `[vshell_probes]` section, the
proxy does nothing for VSHELL. A malformed section, an unknown VA, a report
file that already exists, another `VSHELL.EXE` or a changed import slot refuse
the arming: the game runs unprobed and the event goes to the proxy's
`[video] log` when one is configured. The same INI may hold DBSIM's
`[coverage]`/`[probes]` sections; they act only in `DBSIM.EXE`. With
`[compat] mission_map_lifetime=1` in the proxy's `es2-oracle.ini` the bytes
the mission-map guard checks or patches stay without probes: `entries=all`
drops the three starts `0x40bc48`, `0x40bd22` and `0x445095`, and a list
naming one is refused
([media_compatibility.md](media_compatibility.md#mission-map-lifetime-guard)).

## Report

`schema` 1. Header: `module`, `module_sha256`, `mode`, `saturate_after`,
`pid`, `install_thread`, `first_thread`, `other_thread`, `error` (the
engine's error code: 6 = a second thread entered a probe, then the sites are
not restored at exit; 7 = report write failed), `complete` (probes restored at
exit), `reason` (`startup`, `exit`, `immediate_exit`, `install_failed`),
`sites`. Then one row per site: `va`, `kind` (0 = push, otherwise the `EP_*`
index of `entry_probe_engine.h`), `count`, `saturated`. A process that is
killed keeps the `startup` report.

## Regenerating the inventory

Only if the rule of the script changes (the reference file is fixed):

```powershell
python -B tools\reference-game\scripts\probe_instruction_inventory.py `
    build\extracted\cd\VER95\VSHELL.EXE tools\reference-game\proxy\vshell_probe_inventory.h --module vshell
```

The default module (`dbsim`) writes `probe_entry_inventory.h`; the same unit
test checks it byte for byte.

## The map

No map is recorded: the report format above is what a run of a real session
produces (entry counts per function and screen, caller/callee relations and
the list of handlers it enters).
