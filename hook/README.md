# x86 function hooks

`//hook` is a Windows x86 C17 library. The interface is `hook/hook.h`. It installs a function-entry jump only after checking the configured image base, PE32/I386 headers, exact image size, complete byte signature, and explicitly supplied instruction boundaries. Module-file SHA-256 verification belongs to the proxy integration, before this API is called.

The `es2_hook_install` API supports cdecl with zero to six 32-bit stack arguments and a 32-bit EAX result. The generated bridge calls the relocated original through a trampoline; EBP, EBX, ESI, EDI and caller cleanup retain their cdecl behavior. Incoming scratch registers and flags are not part of this legacy value contract. The context callback below can reproduce entry-dependent scratch results. Thiscall, varargs, floating-point/aggregate arguments, aggregate returns, exception unwinding and callbacks escaping through longjmp remain unsupported.

The prologue validator is a whitelist, not a general disassembler. It recognizes register push/pop, NOP, immediate pushes and register moves, `ENTER imm16, imm8` (C8, four bytes; position-independent, relocated unchanged), ordinary MOV/LEA/ADD/SUB/XOR/TEST, selected immediate arithmetic groups, MOVSX/MOVZX, MOV EAX from an absolute address (A1 moffs32, for DBSIM 0x4011a0), and 16-bit MOV r/m with the 0x66 operand-size prefix (only opcodes 89/8b, no immediate) and 66 A1 MOV AX, moffs32 (for DBSIM 0x413180), and 66 B8+r MOV r16, imm16 (four bytes; for DBSIM 0x45fe6c), and 66 83 /0, /5, /7 ADD/SUB/CMP r/m16, imm8 (the immediate stays one byte, so the prefix changes no length; for DBSIM 0x4240f4 `cmp word [moffs32],0`; 66 81 with imm16 stays refused), and 80 /7 CMP r/m8, imm8 (for DBSIM 0x45a498; the other 80 groups stay refused). Also 83 /1 OR r/m32, imm8 (DBSIM 0x411a44 `or eax,-1`; 81 /1 and 66 83 /1 stay refused) and a near RET (C3) as the last byte of the relocated range only (DBSIM's five-byte empty slot methods `push ebp; mov ebp,esp; pop ebp; ret`: the trampoline then returns by itself and its closing JMP is never reached). It decodes ModRM/SIB, displacement and immediate widths and checks every supplied instruction length. All other prefixes, branches, calls, returns and other instructions are refused. A branch-like byte inside a recognized immediate or displacement is not a branch. The relocated range must be 5–32 bytes and match a signature of at most 64 bytes.

## Modes and state

- `original`: the API succeeds without allocating or patching anything.
- `replaced`: only the replacement callback runs, with the real argument values.
- `compare`: take a private snapshot, call the original exactly once on real state, then run the replacement on the snapshot. The result returned to the caller is always the original result, including on a mismatch or skipped comparison. Replacement changes are never copied to real state.

A span identifies an argument index, byte offset and byte count. At most four combined spans/regions and six arguments are supported, with one span per argument. The copied private extent includes the prefix before the offset, so the replacement receives a valid base pointer. Only the declared offset/count is compared. The shared state budget is 16,384 bytes, including argument prefixes and region bytes. This accommodates the inverse lookup's full signed-word domain without embedding tables. Each cloned base preserves its original alignment modulo 16. Arguments requiring stronger alignment are outside the current contract.

Overlapping extents, repeated pointers and an undeclared argument pointing inside an extent cause comparison to be skipped. The original still runs exactly once. Unreadable spans, spans made unreadable by the original, and excessive hook nesting also skip comparison. `skipped` records these cases. This conservative policy avoids silently passing an alias to real state into the replacement.

Spans do not discover hidden globals or recursively rewrite embedded pointers. Both original and replacement must obey the declared lifetime and side-effect contract: fixed byte state, no external side effects from the replacement, and no dereferencing embedded pointers into mutable real state. A comparison callback may customize equality; it must not mutate its views. By default both EAX and every declared state byte must match.

While a comparison's original is running, **all nested installed hooks bypass their configured modes** and call their own original trampolines. Thus comparing a wrapper still obtains an original baseline when an inner function is configured as replaced. Nested sites increment `calls` and `bypassed`; they do not run replacements or generate comparisons. Other nested dispatch is limited to eight active dispatches. Private snapshots belong to one invocation (legacy: its stack frame; extended: the static buffer set of its compare level).

## Opt-in extended contracts

`es2_hook_install_ex` takes a borrowed `es2_hook_extension`; it must outlive
installation. A null extension selects the unchanged legacy path. Cdecl,
stdcall with callee cleanup, and Borland integer EAX/EDX/ECX plus up to three
remaining stack arguments with callee cleanup are supported. Borland's
remaining arguments are pushed left to right; cdecl/stdcall use right to left.
Noncommutative five/six-argument fixtures check the ordering in all modes.
Results can be EAX, EDX:EAX or exact 80-bit x87 ST0, supplied by typed callbacks.
Extended comparison checks CW, SW, tags and every occupied 80-bit register;
FIP/FDP, reserved bytes and empty slots are excluded. SSE/MXCSR is not captured.

Extension regions describe fixed-RVA or argument/global-indirect
storage. Argument spans and regions together share the four-span/16384-byte
budget. Compare snapshots pre-state, runs original, saves its post-state,
temporarily restores pre-state in the real region, runs replacement on private
argument clones, then restores the original region post-state. No replacement
region writes survive a normal return. This in-place isolation depends on the
single-thread/no-external-observer contract; it is not recursive graph cloning.

Read-only regions are verified unchanged in both phases. Unchanged protected
read-only pages need no write. Indirect pointers must remain stable, overlap
is refused/skipped, and allocation/free/protection changes are unsupported.
See [precise limits and evidence](../docs/reference-game/hooks.md#framework-limits-that-matter-for-new-hooks).
The extended fixture covers cross-ABI nested original bypass, pair/ST0
mismatches, FPU-state differences, global rollback, indirect/RO errors and
failed-install rollback. The game hooks use the cdecl path of this extension;
the Borland and stdcall bridges are exercised by fixtures only.

Multi-hop regions permit two or three pointer dereferences only when
read-only. `pointer_hops=0` preserves the old one-hop interpretation;
`pointer_offset` locates the first slot and `next_pointer_offsets` the next
two. Every slot must be readable, non-null and stable. An invalid initial
chain or original mutation skips the private phase; replacement mutation
counts a mismatch and restores writable pointer slots. Synthetic null and
PAGE_NOACCESS targets prove skip behavior when the original itself is valid.
Runtime indexing, allocation lifetimes and arbitrary graph cloning remain
unsupported.

`replacement_context` is an optional pair-result callback receiving entry
EAX/EDX/ECX. The original trampoline receives those same registers for stack
ABIs. Its full EDX:EAX is checked, including untouched incoming values.
`es2_hook_comparison` exposes both EDX values; a custom comparator owns pair
equality (including pointer remapping) and must check both registers and all
declared bytes. Without a custom comparator, both registers are compared
directly. Exact ST0 and x87 environment checks cannot be overridden.

### Compare-unsupported hooks

`es2_hook_extension.compare_unsupported = TRUE` declares that the
function changes state outside its spans and regions (for example because
its replacement calls unreplaced game code). Compare mode would run it twice
and corrupt that state, so `es2_hook_install_ex` refuses `ES2_HOOK_COMPARE`
with `ES2_HOOK_ERROR_ARGUMENT`; original and replaced modes install
normally. Used by the hooks whose replacement calls unreplaced game code;
[compare_mode.md](../docs/reference-game/compare_mode.md) "Why refused hooks
refuse" gives the classes and the per-hook table.
Fixture: three modes, compare refused.

## Lifecycle and failures

Zero-initialize each runtime and log. Descriptors, signature/length arrays, spans, callback context, log and runtime storage remain valid for the full installation lifetime. Install and uninstall only on the single simulation thread while no hook is in flight; uninstall refuses an in-flight hook. Other-thread calls pass through to the original as a defensive fallback, but this is not a multithreaded patching or accounting implementation. There is no thread suspension.

Executable bridge/trampoline memory is prepared and flushed before publishing runtime state. Runtime state is published before entry bytes are patched. On a patch flush/protection failure, the API rewrites the original bytes, flushes them and restores protection. Injected patch APIs affect entry-site operations only and must retain Win32 failure semantics.

If rollback itself fails, the runtime reports `ES2_HOOK_ERROR_ROLLBACK`, retains code/context, and enters pass-through state `installed == 2`. This intentionally keeps any stale cached jump safe. Keep the runtime and its containing module alive, correct the failure, and retry `es2_hook_uninstall`. Never treat an unsuccessful install as permission to free a runtime whose `code` remains non-null. A completed uninstall restores bytes/protection, flushes them, then frees code. Unknown third-party changes to entry bytes cause uninstall to refuse rather than overwrite them.

## Configuration, counters and logs

`es2_hook_read_mode` reads `[hooks] name=original|replaced|compare`; missing keys default to original. Values are case-insensitive. Empty, truncated and unknown values are refused. The proxy reads the mismatch path and limit and supplies the log to configured hooks.

`es2_hook_log_open` creates a new file and refuses an existing path or an already-open log. A zero limit selects 32 attempted mismatch records. The cap is shared by hooks using the same log; counters continue after it is reached. `calls`, `mismatches`, `skipped` and `bypassed` are per hook. Log `attempted`, `written` and `failed_writes` distinguish bounded attempts from successful writes. Close is idempotent.

Each JSONL mismatch records `event`, `hook`, `call`, optional `frame` (null if unavailable), `args`, `original`, `replacement`, `first_diff_span` and `first_diff_offset`. Arguments/results are unsigned 32-bit decimal values. The first differing offset is relative to the named span, not the object base. Return-only or custom-comparator-only mismatches have null state-difference fields. No state contents are dumped.

## Stack use

The extended compare dispatcher keeps its span/region buffers (about
64 KiB per level: the span clones and the before/after/candidate copies)
in static storage, one set per compare level (`ES2_HOOK_MAX_COMPARE_DEPTH`
sets, 512 KiB of BSS). The compare at nesting depth k uses set k: its
original phase bypasses every nested hook and its replacement phase runs
nested hooks one level deeper, so two active compares never share a set.
A compare level costs about 1.4 KiB of host stack (fixture
`hook_snapshot_test`, part 9). The generic replaced dispatcher reserves 208
bytes. The legacy dispatcher reserves about 16 KiB on every call, including
replaced calls: moving its buffer would change the stack layout of the replaced
path of the live legacy hooks. DBSIM's main thread reserves 1 MiB.
Compare starts only below `ES2_HOOK_MAX_COMPARE_DEPTH` (8): with the buffers
off the stack this is a policy, not a stack limit.

## Snapshot regions

For hot hooks whose state is too large for the four 16 KiB spans/regions
(an object pool, the actor arena), `es2_hook_bind_snapshot(extension, set)`
binds an `es2_hook_snapshot_set` to an extension. It is a binding, not an
extension member, because the adapters initialize extensions positionally;
an adapter binds from a load-time constructor and installation copies the
binding into the runtime (`es2_hook.snapshot`). The set is validated at
every non-original install; buffers are allocated only in compare mode.

- **Regions** (at most 8, 16 MiB together): a fixed range `image + rva`,
  `size` bytes, or a `resolve` callback that returns base and size per
  call (size 0: absent this call; `size` is then the largest accepted
  size). Regions must not overlap each other, spans or extension regions;
  an overlap, an unreadable or unwritable region, a size above the
  declared one or a region that moved during the original phase skips the
  comparison (`snapshot_refused`).
- **In place.** The replacement runs on the real addresses: a replacement
  reads guest memory natively (`math_native_memory`) and its game calls
  go through a guest bridge to original code that follows embedded
  pointers, so a remapped copy at other addresses cannot work without
  rewriting every pointer. Per call: S0 is copied; the original runs on
  real state; every 256-byte chunk the original changed is saved (S1) and
  restored to S0 (a dirty bitmap); the replacement runs; every chunk is
  compared with S1 (changed chunks) or S0 (unchanged chunks) and restored.
  The first differing byte is logged as span index `span_count +
  region_count + i` and its offset in region i. Snapshot differences
  always count as a mismatch (a custom comparator cannot hide them).
- **Guards.** `guard_rvas` name hooked functions whose effects lie outside
  the regions (message ports, clocks, sound). If the original phase enters
  one (its runtime's `calls` changed), the replacement phase is skipped:
  the call ran exactly once (`snapshot_guarded`). A guard site must hold an
  installed runtime of this framework at compare time (found in a table of
  patched hooks by site, `ES2_HOOK_MAX_INSTALLED`), otherwise every call is
  skipped (`snapshot_refused`). A correct replacement enters a guard only
  when the original did. A replaced or compare install that finds the
  table full patches nothing and fails with `ES2_HOOK_ERROR_TABLE_FULL`,
  so every installed hook can serve as a guard.
- **Levels and sampling.** Each compare-mode install allocates `levels`
  (1 to 4) buffer sets of 2 × the rounded region sizes plus the bitmap
  (`VirtualAlloc`, freed by uninstall). A compare of the same hook that
  re-enters through its replacement phase beyond `levels` is skipped
  (`snapshot_exhausted`). `every` = N compares one eligible call in N, the
  others run the original only (`snapshot_unsampled`). All skips also count
  in `skipped`; `snapshot_compared` counts compared calls.
- **Cost.** One copy and two vectorized compares of the regions per
  compared call plus copies of the changed chunks: about 0.3 us per KiB
  (fixture part 10).
- **Contract.** Single simulation thread: no other thread may write a
  region during a compared call (a whole-DATA region would revert, for
  example, an audio thread's stores). Stores of the reach outside the
  regions must be guarded or overwrite scratch with values that do not
  depend on its pre-state (then a correct replacement phase leaves them as
  the original did). Example: `contact_scan` (compare_mode.md, "snapshot regions").

`hook_snapshot_test` covers equal and deliberately different calls (state
and EDX, with the log record), sampling, nested compares of two hooks over
the same region, guards (entered, not installed, installed but not
entered), a full table of installed hooks, re-entry with one and two levels (exhaustion is a skip, not a
crash), resolved regions (absent, too large, moved), overlap, the binding
table and validation, no buffers outside compare mode, and the stack
depth of a compare.

## Replaced entry fast path

Extended replaced hooks default to the fast entry. `[hooks] fast_path=0`
selects the generic entry before installation; absent key means 1. Original,
compare and the legacy API use their own paths. The proxy accepts an explicit
override in `ES2_ORACLE_SPEED_CONFIG` for measurements. Invalid values log a refusal and use the
generic path. `es2_hook_set_fast_path` is a quiescent, pre-install selection.

The fast entry (checked by the synthetic differential fixtures) saves the same
GPR/flags frame and calls the existing cdecl callback through a small assembly
capture routine. Argument unpacking handles all three ABIs and 0..6 slots;
Borland's remaining stack slots are reversed. The callback's x87 state stays
live: there is no C floating return conversion, ST0 spill/pop, or x87
save/restore on success. Only declared EAX/pair/ST0 results are contractual;
FIP/FDP and undefined scratch results remain excluded as before. CW, SW, tags,
occupied 80-bit values, preserved frame slots and LastError are retained.

Foreign owners, inactive hooks, original-phase bypass and depth saturation
fall back before changing any counter/depth state. The generic dispatcher
then owns all accounting. Calls, skipped, bypassed, mismatches and in-flight
counts stay exact; no counters are delayed until a tick. Optional fast
observers provide the existing profiler with nested begin/end tokens and
must preserve x87/LastError. No observer runs when profiling is absent.
Unwinding/longjmp remain unsupported.

`hook_fast_differential_test` compares generic/fast dispatch and generated
entries over the cases of the dispatcher fixture, adding
rounding modes, empty/full x87 stacks, signed zero, profiling and real foreign
threads. `hook_fast_benchmark_test` alternates direct-model and installed-entry
trials; only the installed test measures the avoided entry x87 operations.
The live checks are the regressions of [hooks.md](../docs/reference-game/hooks.md#live-validation).

## Tests

The own-code fixtures in [hook/tests](tests/) exercise detours, comparison,
calling conventions, the instruction whitelist and rollback; `bazel test //hook/...`.
Game equivalence is established separately by the in-vitro harness and the
oracle regression ([hooks.md](../docs/reference-game/hooks.md#validation)).
