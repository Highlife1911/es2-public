# DBSIM function index

[index.md](index.md) and the `dbsim-<range>.md` pages are **generated** by
`tools/reference-game/scripts/function_index.py`; do not edit them by hand. One row per
function entry of the reference `VER95/DBSIM.EXE` (see
[reference_version.md](../reference_version.md)), one page per 64 KiB
address range, with the address, name, area, instruction count, direct callers
and callees, our implementation (hook, adapter group, core file and
function, compare capability, contract section), the verdict of
[function_verdicts.md](../reference-game/function_verdicts.md), the DBSIM map's
entry probes and the hook counters of a regression. The index page lists
the sources, the cross-check against the map (map entries without a
listing entry and the reverse) and the report of the area column (below).

The pages hold addresses, sizes, names, call relations and our own
descriptions only: no instruction text and no code bytes.
The listing they are generated from is never committed.

The pages go stale quickly: every change to the hook registry, the verdict
file, the map or a cited document's headings changes rows, and between
regenerations only anchors are patched by hand. Regenerate before relying on a
row's verdict, counters or links.

## Regenerating

One command regenerates everything that is derived from the hook registry,
the DBSIM map and the verdict file: these pages, [hook_candidates.md](../reference-game/hook_candidates.md),
[hook_frontier.md](../reference-game/hook_frontier.md) and the bridged-call tables in
it, and the own process's gap list [sim_gap.md](../reference-game/sim_gap.md). From the repository root, with the extracted CD at
`build\extracted\cd` (or `ES2_DATA_ROOT`) and the pinned tools of
[dependencies.md](../dependencies.md):

```powershell
.\tools\reference-game\scripts\regenerate_function_docs.ps1
```

It first checks that the `## Verdicts` table of
[function_verdicts.md](../reference-game/function_verdicts.md) holds every non-hook
function that a per-package section classifies (`hook_candidates.py
--check-verdicts`; those rows are otherwise invisible to the candidate and
frontier lists) and that `areas.py` agrees with
[dbsim_gap.md](../reference-game/dbsim_gap.md#1-method); `-CheckOnly` then runs
`sim_gap.py --check` (below) and stops. Otherwise it writes the listing of `llvm-objdump -d -M intel` to an ignored
scratch directory (`build\function-docs`, deleted again), runs `sim_gap.py`
(which also writes the JSON of the Sim column there), `function_index.py`, `hook_candidates.py`, `hook_candidates.py --frontier` and
`hook_candidates.py --bridge-table`. The newest `dbsim-map-*\map.json` and the
newest `check-*\hook-counters.json` under `<es2-data>\logs` are used
unless `-MapJson` / `-Counters` name others; the generated headers and the
index page name both. The output is deterministic (same inputs, same bytes) and
LF-terminated. `--full` of `function_index.py` also lists entries without any
information beyond the address (none at present: every entry has a direct call
relation). Regenerate after a hook package or a verdict row was merged, and
after a new map or regression. The unit tests are
`//tools/reference-game/scripts:function_index_unit_test`,
`:areas_unit_test`, `:sim_gap_unit_test` and `:hook_candidates_unit_test` (synthetic inputs, plus the
committed verdict file and `dbsim_gap.md` against the real registry).

## How the columns are derived

- **Entries**: the listing's direct `call` targets at an instruction
  start, its symbol labels (exports), the registry's hooks, the map's rows
  with their callers and callees, and the classified VAs. The linear sweep
  of `llvm-objdump` loses instruction boundaries behind data in CODE until
  it resynchronizes; known entries without an instruction start are still
  listed (count from the map or `?`) and counted in the cross-check.
- **Area**: the module-map range of [areas.py](../../tools/reference-game/scripts/areas.py)
  that contains the address (the table of
  [dbsim_gap.md section 1](../reference-game/dbsim_gap.md#1-method), checked
  against it by `areas.py --check-document`); empty when no range covers the
  address. It names the source module the Borland linker placed there, not the
  function's role, and applies none of the document's verdict or
  caller-majority overrides. The index page's **Areas** section lists, per
  area, the entries, hooks, live map rows and instructions; the distinct
  direct calls between areas; every range boundary with the function entries
  on both sides (is a function entry exactly at the boundary, which areas call
  the neighbours); and the gaps between the ranges with the areas of their
  callers and callees. A boundary without an entry or a large call count
  across a boundary is the signal to look at the map.
- **Instr.**: the map's count (synchronized descent of the probe
  inventory) where the map has a row, else `~N`, the linear span to the
  next known entry without trailing `nop`/`int3` padding (an
  approximation: a function with an unknown neighbour inside its span is
  counted short, padding inside is counted).
- **Callers/Callees**: direct `call rel32` sites only, attributed to the
  nearest entry at or below the site. Tail jumps, vtable slots and
  callbacks are not included (the map's rows reached only through them are
  counted in the cross-check).
- **Ours**: the adapter group of `hook_registry.h`; the core function by
  name (`es2_<hook>`, `es2_<hook>_registers`, `es2_<hook>_*`, `*_<hook>`),
  else the first core declaration whose leading comment names the hook's
  VA, with the `core/src` file that defines it (fallback: the core module
  column of [hooks.md](../reference-game/hooks.md#registry)); compare from
  the same column; the contract section as the first heading naming the VA,
  else the hook, else a table row of the adapter's specification document
  naming it, else the section naming it most often. These links are
  heuristics; the index page lists hooks where one is missing.
- **Sim**: the own process (`sim/`), from the JSON that
  `sim_gap.py --json` writes. A function with a service row shows the row's
  name, the fragment of `sim/dispatch/services/` and the `sim/src` file of the
  implementing function. An original function without row or hook that own code
  still reaches shows `gap:` and the kind of
  [sim_gap.md](../reference-game/sim_gap.md): `called` (a call site or a hook
  adapter passes the address to the dispatch), `behind <root>` (reached from a
  called root in the static call graph; `virtual behind` from a virtual
  target), `virtual` (a slot of a function-pointer table that `sim/src` names),
  `referenced only` (the address is named but not passed to a call). Rows with
  nothing but a Sim cell are listed.
- **Role**: the "Role" column of a subsystem document's function table,
  else the verdict reason of `function_verdicts.md`, shortened. Roles are
  hypotheses unless the linked document confirms them.

## The sim gap list

[sim_gap.md](../reference-game/sim_gap.md) is generated by
`tools/reference-game/scripts/sim_gap.py` from the committed
sources and the user's `DBSIM.EXE`; it holds addresses, names and counts only.
The own DBSIM never runs original code: the dispatch answers a call to an
original address with a hook or a service row and refuses anything else, so the
run stops there. The list names the addresses that are still refused, per area
with instruction counts, in four groups: **called** (a guest-call site of
`sim/src`, resolved through the wrapper functions and macros of every file, or a
call that a hook adapter bridges), **behind** (original functions a called root
reaches in the static call graph without crossing a row or a hook; an upper
bound for what an own replacement may still have to call), **virtual** (slots of
function-pointer tables that `sim/src` names, which no call site shows; the
replay's stop `0x440cb8` is listed as a slot of the table at `0x49c91c`) and **referenced only**. Calls through
pointers are not followed and are counted per root; dynamic guest-call sites
(the VA is read from an object) are counted per file. Counts are facts of the
sources, not claims about the functions' roles.

```powershell
.\tools\reference-game\scripts\regenerate_function_docs.ps1 -CheckOnly   # coordinator, after each merge
```

`-CheckOnly` runs `sim_gap.py --check`: it fails when the committed document is
stale and names the roots that appeared and disappeared, when a service-row VA
occurs twice or when a source construct could not be read. The list changes
with every merged package, so the coordinator regenerates it (the full command
above) after the merges and commits the changed `sim_gap.md` and
`docs/functions` pages; a package branch need not. The unit test
`//tools/reference-game/scripts:sim_gap_unit_test` uses synthetic sources and
does not depend on the committed document.

A gap VA whose refusal a document explains (the slot is never called, the
caller is replaced, it is reached only behind other refused VAs, own code
serves it elsewhere) has an entry in the verdict list
`tools/reference-game/scripts/sim_gap_verdicts.json`: the VAs, the kind, the
document section (`path#anchor`) and a short reason. `sim_gap.md` links the
verdict in a column of each table and counts the **open** VAs (no row, no hook,
no verdict) in its section "Verdicts". `--check` fails on a stale verdict (the
VA has a row or hook, or is no longer in the gap), on a missing document or
heading, and on a `behind` verdict that an open root still reaches.

A bridged `call_<va>` key is not always a function: an adapter that forwards a
call through a pointer counts it under the address of its call instruction
(`0x420a8d` in `cockpit_notice`, `0x420acb` in `radio_notice`, both
`call dword ptr [reg]`). Both generators read the instruction at such a key from
`DBSIM.EXE`: when the key is no function start (`sim_gap.py`: not in the call
graph of the descent; `hook_candidates.py`: neither a hook nor a map row) and its
bytes are `FF /2` (an indirect near call), it is a call site, no root and no
callee. `sim_gap.md` names such keys in its sources line, and the
bridged-call tables of `hook_frontier.md` list them in their own section
"Indirect call sites"; `--bridge-table` needs `--image` for that, which
`regenerate_function_docs.ps1` passes.
