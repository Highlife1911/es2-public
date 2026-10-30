# Mutation witnesses

Every registered hook has at least one caught mutation: a deliberate change
in a scratch copy of our own sources that the in-vitro harness detects as a
named differential failure (a compiler error never counts). Two tools produce
them:

- `check_mutations.ps1`, the curated witness list: one or more named,
  explained mutations per hook. A run fails unless every selected mutation
  is caught. New registry entries without a mutation are rejected.
- `generate_mutations.py`, the generator: derives thousands of mutants from
  the marked core functions and reports the survivors. Its product is a
  survivor list, which does not fail CI by itself.

```powershell
$env:ES2_DATA_ROOT = 'build\extracted\cd'
& tools/reference-game/invitro/check_mutations.ps1 -OutputDirectory <scratch> [-Only <name>[,<name>]]
& <es2-dependencies>\python\python.exe -B tools/reference-game/invitro/generate_mutations.py --help
```

## Curated witnesses

Each entry of `check_mutations.ps1` names a hook (or an rtl selection, see
below), a source file under `core/src` (or a path relative to it, such as
`../../tools/reference-game/proxy/rtl_heap_own.h`), an exact `Old` text, its
`New` replacement and `Why`: the non-equivalent change, with the original's
address that makes it observable. `Why` is the record of what each witness
proves; this file does not repeat it.

The runner copies our sources and build files (`core`, `hook`, `rtl`,
`bazel`, `formats`, `sim`, `proxy`, `invitro`, the `tools/reference-game`
data-input rule MODULE.bazel loads) and the module lock into a fresh scratch
workspace. For each selected entry it applies the one replacement (the `Old`
text must occur exactly once), builds `:differential_harness` with Bazel and
runs the mutant with the entry's name (an isolated run of that hook or rtl
selection). The entry is caught only by a nonzero exit with a
`FAIL: <name> case <n>` line; a build failure or a pass stops the run. The
caught case, the line and `Why` are collected in
`mutation-results.json` of the workspace, the logs per entry in the output
directory. Options:

| Option | Effect |
| --- | --- |
| `-OutputDirectory <dir>` | existing scratch directory for the workspace and logs (required) |
| `-Only <name>[,<name>]` | only the entries with these names (all entries of a hook) |
| `-BazelOutputBase <path>` | the mutant builds' output base (default: one per workspace, shut down at the end) |
| `-RepoContentsCache <path>` | repository-contents cache in an allowed cache directory |
| `-Bazel <path>` | Bazel executable (default: `Resolve-Es2Bazel`) |

Scratch BUILD files are renamed `.inactive` on exit. Do not run
`bazel test //...` while a mutation workspace is active: recursive discovery
can include its temporary BUILD files.

**What a witness proves.** That the differential checks of its hook see that
change: the direct cases (pair, ECX, call log, arena and globals against the
original), the installed cases (registers, stack residue and frame through the
entry shim) or the real-callee pass, whichever reports the `FAIL:`. Entries
that change an adapter (`proxy/*_hooks.h`) rather than core prove that the
installed or frame checks see a plain host-stack bridge, a lost `POP ECX`
reload or another register path of the shim. One witness per hook is a lower
bound, not a measure of the harness; the generator is.

**rtl selections.** The CW3220 passes have no hook names; their entries are
selected as `rtl_<function>` (`core/src/rtl.c`, a direct case of `--rtl`),
`rtl_heap_<name>` (`core/src/rtl_heap.c`, a direct sequence of `--rtl-heap`;
the log names the sequence and the operation),
`rtl_heap_own_<name>` (`proxy/rtl_heap_own.h`, a replaced or mixed sequence),
`rtl_start_<name>` (`core/src/rtl_start.c` and `proxy/rtl_life.h`, a model
case of `--rtl-start` or a switched case, numbered `0x100000 + n`) and
`rtl_io_<name>` (`core/src/rtl_io.c`, a direct sequence of `--rtl-io`).

## Generator

`generate_mutations.py` derives mutants from our own core code instead of a
hand-written list.

**Scope and mapping.** Every function in `core/src/*.c` whose definition is
preceded by a `/* Original: DBSIM.EXE 0x... */` comment. The marker address
minus 400000h is matched with the registry's RVAs; the hook's group from
`hook_registry.h` (validated against `groups.bzl`) is the group the mutant
runs against. A marked helper that is not a hook takes the groups of its
callers in core (reverse call graph, nearest first) and of the adapter
headers (`proxy/*_hooks.h`, via their descriptor arrays) that name it.
`--unmarked` adds the older modules' unmarked global functions that an
adapter calls directly (group known, hook not).

**Operators** (one change per mutant, only inside function bodies; comments,
strings and preprocessor lines are never touched):

| Operator | Change |
| --- | --- |
| `const` | integer literal +1 / -1 (suffix and base kept); literals in the image VA range 400000h..6D7FFFh (callee addresses) and array dimensions of declarations are skipped (`--addresses` includes the VAs) |
| `bitflip` | a mask literal next to `&`, `\|`, `^`: lowest and highest set bit flipped; `^1u` on a returned integer, on the stored value of `W8/W16/W32`, `fx_w*`, `->write*`, `VW`/`VD`, and on an assigned value |
| `shift` | literal width of `<<`, `>>`, `<<=`, `>>=` or of a `sar`/`shr`/`shl` call +1 / -1 |
| `cmp` | `<` and `<=`, `>` and `>=`, `==` and `!=` swapped |
| `drop` | a store statement (store call, or an assignment to anything but a plain local) replaced by `(void)0;` |

**Runs.** The workspace copy (default `build/mutations/workspace`; our
sources, build files and the module lock, like `check_mutations.ps1`) gets
the mutated file; Bazel builds `:differential_harness` with its own
`--output-base` (`--remote_upload_local_results=false`, so mutants do not
enter the shared disk cache; `--bazel-flag` passes further build options
such as `--local_resources=cpu=4`) and the harness runs `--quick --group <g>`
for the mutant's groups only (direct hook group first, at most
`--max-groups`, default 2), after one unmutated baseline run per group whose
time sets the timeout (5 x baseline + 30 s, at least 60 s). Results per
mutant:

- `killed`: a named `FAIL:` line (recorded), a crash or a timeout;
- `survived`: every planned group passed;
- `build-error`: the compiler refused the mutant (-Werror); never a witness
  (constant indices past an array's end, duplicate `case` values, unused
  variables after a dropped store);
- `equivalent-suspect`: the linked harness is byte-identical to the
  unmutated build (the compiler folded the change away); nothing is run;
- `unmapped`: no group found.

**Incremental.** The state file (default `build/mutations/state.json`)
keeps each core file's SHA-256 and every mutant's result for that content.
A changed file loses its results and is due again; unchanged files are
skipped, and mutants that already have a result are skipped, so bounded runs
(`--limit`, default 100, round-robin over functions; `--per-function N`,
seeded sample with `--seed`) continue where the previous one stopped.
`--all` reruns everything; `--files`, `--group`, `--functions` (core name or
hook name), `--ops` and `--ids ID... | @FILE` (named mutants, e.g. an earlier
run's survivor list) narrow the run; `--list` prints the mutants and their
groups without building, `--report` prints the stored results. The working
tree is never written: the original file is written back into the workspace
after each file and in a `finally` (also on Ctrl-C), the workspace's BUILD
files are renamed to `BUILD.bazel.inactive` on exit (do not run
`bazel test //...` of the same checkout while a run is active), and the
mutant Bazel server is shut down at the end unless `--keep-server`. A full
`--unmarked` run over all groups takes days (6 to 10 s per mutant); use
`--group` and `--per-function` for bounded runs.

```powershell
$env:ES2_DATA_ROOT = 'build\extracted\cd'
$py = '<es2-dependencies>\python\python.exe'
& $py -B tools/reference-game/invitro/generate_mutations.py --list --group convoy      # what would run
& $py -B tools/reference-game/invitro/generate_mutations.py --group convoy --limit 50  # bounded run
& $py -B tools/reference-game/invitro/generate_mutations.py --report                   # stored results
```

Unit test: `//tools/reference-game/invitro:generate_mutations_test`
(operators, markers, statement analysis, mapping, state and sampling on
synthetic C; no game data).

## Survivors

A survivor is either equivalent (no input can tell it apart from the
original) or a gap in the cases. The classification is by hand, from the
diff and the core source.

**Equivalent classes** (expected survivors; not gaps):

| Class | Why it survives |
| --- | --- |
| frame layout of the adapter's scratch (offsets of frame stand-ins ±1) | our scratch layout is free; nothing outside reads it |
| stack-address register residue | the callees take stack arguments, and the stubs normalise stack addresses because they can never match the original frame |
| dead register initialisers or stores (`entry[i]` to `entry[j]`, a register overwritten before the next call or return) | the value is never read |
| zero store into the adapter's zero-filled scratch | equivalent under the adapter contract (`BYTE scratch[...]={0}` per call) |
| dead store into a frame word, past the frame, or into a static buffer overwritten before return | never read |
| redundant argument slot (`a[0]=o` again), self-copy store | the slot already holds the value |
| sentinel, clamp or comparison values that no reachable input can cross | e.g. a `best` sentinel above every reachable sum |
| a change behind a `PROVED` edge | the original cannot take that edge (it faults or the condition is impossible) |
| ECX at return where the hook's contract leaves ECX dead at the callers | not part of the result |

**What the cases do to avoid gaps.**

- **Bridge slot count.** `weapons_machine_call` (`proxy/weapons_hooks.h`)
  stores, in host memory only, the address of the first argument slot and
  the number of slots it pushed (`weapons_bridge_args`/`_count`; the
  address is cleared after the call; the guest stack layout is unchanged).
  `weapons_bridge_slots(a, original)` gives a logging stub the pushed count
  when it was called straight from the bridge, else the original's count
  (its call site; a stub entered from a real callee under a bridge call has
  a lower slot address). Stubs that record it in their call records turn a
  core call with one argument too few or too many into a record mismatch,
  even when the slot above the pushed arguments holds the missing value.
  mech_travel checks only too few: its core pre-pushes a later call's slot
  where the original does (PUSH ESI at 41da77 before 492934).
- **Placement.** The convoy arena places object+0Ch or +0Dh at the start of
  a 64 KiB page in half of the cases (`cv_place`), so the high word of
  `lo(o+0Ch, ...)` is checked.
- **Stubs that write.** A stub writes what the real callee writes when the
  core restores it afterwards (the convoy stub for `terrain_pose` 4029d8
  writes new pitch/roll words at object+0Ch).
- **Mixed-sign words.** Configuration and key words take values whose two
  bytes differ in sign, so a byte read at the wrong offset is seen.

**Known gaps.**

- Bridges that do not record the slot count: `ai_behaviour_stack_call`,
  the route/weapon_system ESI bridges and flyer_states' site bridge
  (flyer_states 4222fc 4→5 under flyer_follow survives); mech_travel's
  clipped count accepts one extra slot by design (416a04 4→5, 41c3c8 1→2,
  42313c 1→2).
- A static marked copy (pools.c's `debris_table`, same marker 4089bc as the
  debris_burst hook) is mapped to the hook's group first, not to its
  callers' groups.
- The EAX high word at `actor_pass.c:124`.
- Not every group has a generator result; `--group <rest>` continues
  incrementally (one state file per output directory).
