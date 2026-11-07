# Oracle suites

How oracle suites are prepared, recorded or scripted, verified and registered,
and what each suite drives. The registry is
`tools/reference-game/oracle-suites.json` (format 3); its per-suite fields
(script, watch set, `ticks`, `frame_limit`, `outcome`, launch set,
`game_arguments`, `hold`, `demo_tape`) are defined there, and the sections below
name them only where a suite needs a comment. Running and checking registered
suites is in [oracle_regression.md](oracle_regression.md).

## Launch boundary and storage

Static boundary: VSHELL prepares `script.dat`, `player.mec`,
`mission.var`, and `MISSION.STR`; DBSIM consumes them from `DATA`. Producer,
consumer addresses and example hashes are in
[mission.md](../formats/mission.md#the-four-launch-files). A launch set is an
immutable directory `es2-data/oracle/launch/<name>/` containing these
four files and `manifest.json` (schema 1, name, byte sizes and SHA-256 hashes,
capture process path/start time and provenance). These are original/prepared
assets, never repository fixtures or git content. A generated loadout creates
a different named launch set and a new manifest; do not silently edit one.

An optional suite `launch` string selects that name under `DataRoot/oracle/launch`.
Absent `launch` retains the prepared clone and existing suite arguments and
normalization. Launch-aware runs verify every hash before deployment, back up
the four inputs plus `results.dat` and `EXIT.CFG` (including absence), deploy,
run, retain changed outputs relative to the deployed inputs, and restore in
`finally`. Restoration attempts every file even after a capture/copy failure;
failed restoration keeps its backup and reports its location. Suite entries and
golden manifests record the verified launch hashes. Clone names, path ancestry
and reparse points are checked; `native-r111` is always refused.
If a still-running process cannot be proved to belong to the fresh launch,
the transaction refuses to stop it or restore beneath it and retains the backup.
This ownership failure requires manual recovery after the clone is closed.

`capture-launch-set.ps1` is armed before the mission is started from VSHELL
in a measurement clone. It observes a newly started DBSIM at the exact
clone executable path, records its start time, copies the four stable files,
and verifies source/copy hashes and process identity again. It neither starts
nor stops a process. Capture is not an atomic game-side snapshot: stay at
mission start until capture completes. An already-running DBSIM is refused. A timeout leaves no published manifest.

## Loadout generator

The C17 MEC API reads the writer grammar: four-byte header, N records of
`76+4*K` bytes, then count 33 and 33 flags. It preserves all unknown bytes.
The CLI shows vehicle IDs, K, item IDs and secondary words; `set` uses a
template, changes selected records and writes only a new output file. Record
count/order and secondary/condition data are preserved; changing K requires
an explicit policy for new secondary entries. Validation reads `HERC_INF`,
INI loadouts and shell `GAM/WEAPONS.DAT` from the supplied CD at runtime.
Unknown IDs and structurally impossible spans are errors. Hardpoint meanings,
mount compatibility and mission team capacity remain hypotheses until proved;
warnings must distinguish these from errors. No shell table is embedded in code.

Implemented API/CLI: [formats README](../../formats/README.md#playermec-templates).
The editor retains secondary words by ordinal when K changes; new words get 5.
It keeps N and record order. The reusable writer accepts an explicit new-word
value. Structural parsing permits arbitrary nonnegative counts that fit the
input; deployment validation separately refuses N=0 and K>10.

**Static evidence:** VSHELL:
`0x4113da–0x4113f9` initializes ten item-pointer slots at object `+0x50`
(`66 83 fe 0a` at `0x4113f5`). `0x411378–0x41139c` obtains capacity from
the executable's word table `0x46f87a` after ID lookup; the constructor stores
it at `+0x4c` (`66 89 43 4c` at `0x411621`). `0x410d1d–0x410d31` serializes
that capacity, and the two loops ending at `0x410d7e`/`0x410dca` serialize K
IDs and secondary words. Thus INI occupied count is not MEC capacity. K>10
is refused as outside shell slot storage; this is not a claim that DBSIM
performs that check. HERC_INF +6 is still only a comparison/warning source.

**Static evidence:** reference DBSIM,
`0x424544–0x424569`, reads `2*K` bytes into fixed offsets `+8` and `+0x20`
without a visible K bound in that block. `0x42456c` places the following word
at `+0x3a`, then reads 26/20/20 bytes at `+0x3c/+0x56/+0x6a`.
`0x4245a9` advances by 126. After closing, `0x4245c4–0x4245d0` dereferences
record zero and compares its vehicle ID with 8 (`66 83 78 04 08`), so N=0
is unsuitable for a generated launch. Checks elsewhere remain open.

## Recording and acceptance

1. Capture a launch set and review the player/team loadout.
2. Make a short discovery recording, propose/review a mission-specific watch
   set, then make the final recording with that watch set.
3. `record-suite.ps1` deploys the launch, records at virtual step **47 ms**,
   pace **1**, input mode `record`, interactive window, original hooks.
4. Verification replays the same journal twice at pace **0**, from the same
   launch set and watches. Both replay runs must agree with each other and
   with the recording under the explicitly documented recording comparator.
   Full outcomes must exit naturally and produce return files; bounded runs
   use a declared frame window, never a silently shortened common prefix.
5. The tool emits a ready-to-paste suite entry, not an automatic registry edit.
6. Record the canonical golden from a clean reviewed tree, then run a normal
   regression check.

Keyboard and mouse replay are verified
([input_journal.md](input_journal.md#journal-and-script-format)); scripted
joystick replay is verified ([Scripted joystick](#scripted-joystick)), replay
of a recorded real device is not. Focus loss pauses record mode and is not
journaled; keep focus throughout. Record mode does not serialize modal pump
delays; the verifier replays a derived journal with pump keys for modal frames
and the two-replay check still decides.
Never call a failed recording deterministic or promote it to a golden. The
one exception is `c5_05-moon-missiles`: it registers the replay of a
recording that its own replays do not match, with the cause measured
([below](#moon-missile-set-c5_05-moon-missiles)). Modal screens that stay
open longer than about a second mid-mission make a recording unreproducible;
only end dialogs are safe. Scripted suites answer modal
alerts with pump-keyed Enter lines ([Answering a modal alert](#answering-a-modal-alert)).

## Watches and coverage checklist

Keep the established names for decoded mission watches; compare pointer-free
position/flag/target ranges for actors. A discovery log must observe runtime
actor/team lists before proposing indices. The helper reports missing evidence,
unreadable chains and the 48-watch limit; a proposal requires review. Team count
from MEC is a candidate until it agrees with the observed group. Do not reuse
C1_01 hostile indices, route length or heap addresses for another mission.

`propose_watch_set.py --bootstrap` reads all three prepared actor counts plus
MEC N as an upper bound and emits discovery-only `actor_index`, `team_index`,
group and one-byte side watches. Selection/compaction makes equality with the
runtime count a hypothesis. The default takes as many initial indices as fit
48 watches and explicitly reports the omitted upper-bound indices;
`--actors 0,1,...` chooses a reviewed subset. Final proposal generation requires observed list membership, the
first team member equal to the tick's player pointer, and MEC N equal to the
observed group count. Different-side actors become hostile *candidates*;
review neutral/delayed actors and use `--hostiles 1,3,...` explicitly if needed.
Missing side evidence fails unless a reviewed selection is supplied. Team
chains follow the player's group; hostile chains use runtime pool indices.
No heap address is written to the final watch file. The report lists omitted
hostiles, missing sides and route points cut by the 48-watch budget. An
unreadable player route is omitted and reported. A final watch proposal is
not proof of complete actor coverage; review it, then record afresh.

The actions below are **coverage hypotheses**, not promises that branches run.
Confirm with valid per-site live reports and (in separate replaced-mode runs)
the counters documented in [hooks.md](hooks.md). The gaps come from
[live_coverage.md](live_coverage.md).

| Gap | Action / suite | Evidence to inspect |
| --- | --- | --- |
| Building collision/rollback | (a) hit the wall immediately, reverse, hit again | ground update/collision branches and rollback calls |
| Object contact and impact | (b) ram a vehicle head-on and obliquely | `ground_collide` contact/impact sites; `path_contact`, slots +68/+70 |
| Slope drift | (b) approach ramp across its slope, release throttle, descend | drift sites and `path_slope_drift` |
| Slide damage | (b) sustain a fast ramp descent; (c) try ice if available | damage sites, `path_slide_damage`, slot +74 |
| AI RNG recoil and clamp | (b) order team attack/destroy across obstacles; lure enemy into a vehicle/wall | `ground_update` RNG branch and speed clamp, RNG stream changes |
| `track_target` and AI transitions | (b) change team orders/targets; (c) turn/stop/fire with Razor | nine track-target sites; animation transitions (name alone does not mean targeting AI) |
| Effects | (a) continuous single-weapon fire; (b) fire every equipped weapon and self-destruct; (c) plasma | 30 ground-effects sites, impact/destruction effects |
| Hit +70 and part damage +74 | (b) vehicle ramming, sustained damage, ramp slide and destruction | virtual-slot counters and damage-hook evidence when available |
| Other one-way/unreached sites, track advance/heading | (a–c) varied headings, forward/reverse, stops, terrain and Hercs | full per-site difference from the coverage summary |
| All weapons/Hercs | maintain a reviewed inventory across (a–c), including team vehicles; each weapon actually fires | launch IDs plus observation and log/coverage evidence; equipped is not fired |

## Verification and open questions

The comparator retains the legacy deterministic tick fields, watches and
output hashes. Record/replay additionally excludes `in_injected` and
`in_fallbacks` only; replay/replay excludes neither. The legacy exclusions are
sequence/QPC/context and player pointers, discarded real desktop input,
input mode/recorded counters, late clock polls and hook diagnostics. Header
schema, record limit, actor-raw selection and watch count must match. All
frames must be unique and contiguous from zero, with no missing selected
fields; bounded runs must contain the entire requested window. Watch deltas
must be bounded and every configured watch readable at least once inside the
window. Output file sets and SHA-256 hashes must match. No tolerance, prefix
matching or fallback to replay-only acceptance is used. Full outcomes must
exit naturally; the tool clears stale `results.dat` and retains both return
files. The proxy, launch manifest, saved watch file, journal and recorded tick
hash are pinned between recording and verification.

Real round-trip cases (read-only, in memory; paths under
`build/installs/timing-r111`):

| File | Bytes / N / K |
| --- | --- |
| `DATA/player.mec`, `SAV/player10.mec` | 215 / 2 / 3,3 |
| `SAV/player0.mec` | 127 / 1 / 3 |
| `SAV/player1.mec` | 127 / 1 / 3 |
| `SAV/player11.mec` | 151 / 1 / 9 |

Open hypotheses: safe mission team sizes beyond observed examples and downstream
consequences of oversized rosters; DBSIM's runtime K/mount checks;
secondary/condition field meanings; whether the proposed actions close the
listed coverage gaps.

## Static team mapping

**Compiled behavior**, inspected statically in the reference DBSIM
identified above:

- The MEC reader stores signed N at `0x4aaa5e` and allocates N records of 126
  runtime bytes at `0x4aaa60` (`0x4244dd` onward). Let M be the script's
  134-byte actor-record count. At `0x425033..0x425086` the initialization pass
  marks M+i as selected for every MEC record i, takes its vehicle ID at
  runtime record +4, and assigns a compact pool index. `0x425088..0x42508e`
  adds N to the count (`66 01 45 aa`). Thus these are appended actors, not
  replacements for M authored records.
- The construction loop runs through M+N (`0x4259e0..0x4259f2`). Before M it
  reads a 134-byte script record; after M it uses MEC vehicle +4 and arrays
  +8/+32 (`0x42566b..0x425678`, `0x42582f..0x42585a`). It forwards MEC tails
  +60/+86/+106 to condition initialization and pilot word +0 to actor +668
  (`0x4258b0..0x4258f4`). MEC ordinal increments at `0x4258fb`
  (`66 ff 45 ba`). Player selection compares that ordinal with `0x4aaa64`
  at `0x4256c8..0x4256e7`; its general initialization is not resolved here.
- When reading group zero, `0x426024..0x42604f` overwrites its twenty actor
  references with the appended actors' compact indices for i<N, then -1.
  The loop bound is 20 (`66 83 fb 14` at `0x42604b`). Other groups follow
  their authored references through class-specific remapping. Constructor
  `0x423bee..0x423c13` counts nonnegative references and allocates the roster;
  class-0 resolution at `0x423c3a..0x423c7c` fills it in ordinal order.
- Consequently, fewer MEC records than the authored first-group references
  produce fewer roster entries in this path; more records extend the roster
  up to twenty. For N>20 the construction pass still attempts N appended
  actors but this roster loop names only the first twenty. This is not a
  demonstrated safe maximum: fixed remap arrays, allocation, downstream AI,
  mission conditions and return writing need separate validation. N=0 is
  already unsuitable because the MEC reader dereferences record zero.
- Group category is a byte: `8a 41 6e` at `0x423b4d` reads serialized +110,
  and `88 43 12` at `0x423b50` stores runtime +18. Runtime +19 is separately
  initialized at `0x423b89`. Discovery therefore reads one byte, not a word.

**Data cross-check:** timing `script.dat` is 12,286 bytes.
Its actor counts are 10/4/50 (payload offsets 1784/3126/3496), and group zero
at 6296 has class 0, references 3,4 followed by eighteen -1 words, category 0
at offset 6406. The current MEC has N=2. These values fit the
compiled mapping; they do not establish new live behavior for changed N.

The discovery helper uses the sum of all three script counts plus N as an
upper bound, since the initialization capacity includes the selected counts
of all three classes (`0x4252a6..0x4252c6`). Runtime roster/pool observations
remain mandatory. The template writer deliberately preserves N and order.
No oversized or undersized mission has been launched to test these
conclusions; changed team sizes are untested in the game.

## Authoring a recorded suite

The sequence for a recorded suite. Run the commands from the workspace with
the system PowerShell 7 (`pwsh`). Output names below must be unused. The
mission is prepared in the measurement VSHELL and played from the keyboard;
every other step is a script. The clone is `timing-r111`, never `native-r111`.

**Without a VSHELL session:** a generated, proved set such as
`c3_09-loadout-b` ([below](#published-loadout-sets)) replaces the VSHELL
preparation and the capture step. Set `$launch` to
`"$data\oracle\launch\c3_09-loadout-b"`, skip the two `capture-launch-set.ps1`
calls and use `-Launch c3_09-loadout-b` (and that name for the suite) in every
later command.

```powershell
$repo = '.'
$setup = "$repo\tools\reference-game\setup"
$python = 'python'
$data = $env:ES2_DATA_REPO
$launch = "$data\oracle\launch\omicron-b"
$work = "$data\logs\omicron-b-authoring"
New-Item -ItemType Directory -Path $work

# Prepare Omicron, the desired Herc/weapons and the team in the measurement VSHELL.
# Arm in another terminal BEFORE the mission is started.
& "$setup\capture-launch-set.ps1" -Name omicron-b -InstallName timing-r111 -DryRun
& "$setup\capture-launch-set.ps1" -Name omicron-b -InstallName timing-r111
# Start the mission, stay at its start until capture reports success,
# then exit normally. Capture never stops this process.

# Create the discovery watches; inspect counts and any omissions in its report.
& $python -B "$repo\tools\reference-game\scripts\propose_watch_set.py" `
    --launch $launch --bootstrap --output "$work\discovery.txt" --report "$work\discovery-review.json"
& "$setup\record-suite.ps1" -Name omicron-b-discovery -Launch omicron-b `
    -WatchFile "$work\discovery.txt" -Ticks 300 -FrameLimit 250 `
    -OutputDirectory "$work\discovery" -DryRun
& "$setup\record-suite.ps1" -Name omicron-b-discovery -Launch omicron-b `
    -WatchFile "$work\discovery.txt" -Ticks 300 -FrameLimit 250 `
    -OutputDirectory "$work\discovery"

# Resolve team/hostile chains from the discovery log; review the proposal.
& $python -B "$repo\tools\reference-game\scripts\propose_watch_set.py" `
    --launch $launch --log "$work\discovery\record.jsonl" `
    --output "$work\watches.txt" --report "$work\watch-review.json"

# Final play: all weapons, team attack/destroy, ramming, AI collision,
# cross-slope descent/slide, self-destruct and keyboard answer to the end dialog.
& "$setup\record-suite.ps1" -Name omicron-b -Launch omicron-b `
    -WatchFile "$work\watches.txt" -Outcome -Ticks 40000 `
    -OutputDirectory "$work\recording" -DryRun
& "$setup\record-suite.ps1" -Name omicron-b -Launch omicron-b `
    -WatchFile "$work\watches.txt" -Outcome -Ticks 40000 `
    -OutputDirectory "$work\recording"
& "$setup\record-suite.ps1" -VerifyRecording -RecordingDirectory "$work\recording" `
    -OutputDirectory "$work\verified" -DryRun
& "$setup\record-suite.ps1" -VerifyRecording -RecordingDirectory "$work\recording" `
    -OutputDirectory "$work\verified"
```

Capture checks fresh process identity and four stable file hashes. Discovery
checks runtime actor membership; review must account for every omitted actor.
If final proposal generation reports missing sides, repeat discovery with a
reviewed `--actors` selection, or explicitly select reviewed `--hostiles`.
Never infer hostile identities from the omitted upper-bound indices alone.
Recording checks timing, natural exit and return-file presence. Verification
checks two unpaced replays against each other and the recording, then writes
`verified/suite-entry.json` and `verified/draft-suites.json`. A failure produces
no draft. If modal replay fails, preserve the evidence and diagnose the input
journal; do not overwrite the recording or relax comparison rules.
`-ModalOpenerKeys 0x51,0x7A` leaves the keys that open a modal screen (Q, F11)
unkeyed in the derived journal
([input_journal.md](input_journal.md#journal-and-script-format)).

Once the watches and the loadout are accepted, add
`verified/suite-entry.json` to the registry and obtain a clean reviewed tree
containing the tools and the entry. From that reviewed tree (substitute its
actual path), create and check the canonical golden:

```powershell
$reviewed = '.' # must be the clean reviewed revision
$reviewSetup = "$reviewed\tools\reference-game\setup"
& "$reviewSetup\run-oracle-regression.ps1" -Mode record -Suite omicron-b `
    -RepositoryRoot $reviewed -OutputDirectory "$work\golden-run" -DryRun
& "$reviewSetup\run-oracle-regression.ps1" -Mode record -Suite omicron-b `
    -RepositoryRoot $reviewed -OutputDirectory "$work\golden-run"
& "$reviewSetup\run-oracle-regression.ps1" -Mode check -Suite omicron-b `
    -RepositoryRoot $reviewed -OutputDirectory "$work\golden-check"
```
For an optional generated loadout, inspect the captured template with
`player_mec_tool show`, write to a *new* file, then seal a distinct set with
`capture-launch-set.ps1 -Name omicron-b-custom -FromLaunch omicron-b -PlayerMec <new-file>`.
Use `omicron-b-custom` consistently for discovery, recording and the entry.
This path needs no game process, preserves the other three input hashes, and
records the parent manifest and generated-MEC hash. Generated conditions and
new item/slot combinations still require confirmation in game.
`player_mec_tool diff <captured-player.mec> <new-file>` shows per-record
changes and any opaque-block differences before sealing (exit 1 means different,
not a malformed file). Compare the shown IDs with the in-game loadout.

## Generated launch sets (headless preparation)

Both the independent `shell/` preparer and the original mapped
VSHELL worker produce complete launch sets without a template or game
session. CD-default MEC construction is part of `shell/`; SAV and frozen
MEC/loadout inputs are supported. All 61 CAREER entries match the original
with group-zero mode 0 / other groups mode 1, zero bank/seed zero, in
ENG/FRE/GER. C1_01 and DEMO real preparations are exact. Fixed mode 1 on
TRAIN1/TRAIN2 asserts in the original and is rejected by the safe API. See
[sweep results and limits](shell_invitro.md#differential-sweep).

**Sector mapping:** `0x444a12/19` and `0x444af5..0x444b02` select
ESTEXT entry `group-1+0x77`; entry `0x79` is Omicron. CAREER group 3 contains
C3_01..C3_10. This command selects C3_10 (3:9); its localized debrief also
names Omicron. The CD-default loadout does not establish every weapon/team
objective of the recorded suite.

From the repository root, build as documented in
[shell/README.md](../../shell/README.md#tools) (output base
`build/bazel/es2-shell`), then prepare an immutable
set in the ignored launch area:

```powershell
$python = 'python'
$bin = 'build/bazel/es2-shell/execroot/_main/bazel-out/x86_windows-fastbuild/bin'
& $python -B shell/tools/es2prep.py --core "$bin/shell/es2prep_core.exe" `
  --cd-root build/extracted/cd --campaign 3 9 --seed 0 --language GER `
  --output $env:ES2_DATA_REPO/oracle/launch/<new-name>
```

Omit `--output` for a hash-only report. Add `--save <SAV>` to restore saved
hangar/roster/variables, or `--variables <2000-byte-bank>` to override the bank.
Mode, difficulty, training bytes and preset flag/index are explicit raw inputs.
The schema-1 manifest records `state_source` (`cd-defaults`, `sav`, `template`
or `loadout`), controls, table/state hashes and returned RNG/selectors.
The independent tool records no original process identity. The reference worker
remains available for proof, with provenance `generated-vshell` and its actual
private worker identity:

```powershell
& $python -B tools/reference-game/shell-invitro/es2shellprep.py `
  --worker "$bin/tools/reference-game/shell-invitro/es2shellprep.exe" `
  --cd-root build/extracted/cd --campaign 3:9 --seed 0 --language G `
  --output $env:ES2_DATA_REPO/oracle/launch/<another-new-name>
```

Both publishers seal four files together, refuse existing destinations and
installation descendants, and require Git to positively report an output path
ignored if it is inside a repository. Never publish into a tracked worktree
path. `es2prep.py --loadout` JSON actors still describe a frozen MEC; for
loadouts proved against the original use the next section.

## Suites from a loadout

A loadout file (player Herc and weapons, up to three team members, CAREER
mission) becomes a SAV built by our `formats:sav` writer, is prepared by
`shell/` and by the mapped original from that same SAV file, and is sealed
only if the four files, the returned RNG/reset state and the original
reader/writer round trip of the SAV are identical. No VSHELL session and no
game process are involved. Loadout format and options:
[shell/README.md](../../shell/README.md#tools);
contract: [vshell_preparation.md](../reverse-engineering/vshell_preparation.md#loadout-sav-contract).

Step by step, from the main checkout:

```powershell
$python = 'python'
$bazel = 'bazel'   # system Bazel on PATH, version per .bazelversion
$base = 'build/bazel/es2-shell'
# 1. Build our core and the original worker once.
& $bazel --output_base=$base build //shell:es2prep_core //tools/reference-game/shell-invitro:es2shellprep `
  --repo_contents_cache="$base-contents" --lockfile_mode=error
$bin = "$base/execroot/_main/bazel-out/x86_windows-fastbuild/bin"
# 2. Write the loadout (see shell/loadouts/ for the published ones) and check
#    it: names resolve, hangar rules, proof against the original, no files.
& $python -B shell/tools/es2loadout.py --core "$bin/shell/es2prep_core.exe" `
  --cd-root build/extracted/cd --loadout shell/loadouts/c3_09-loadout-b.json `
  --report build/claude/c3_09-loadout-b.report.json
# 3. Seal it under a new name (the directory must not exist).
& $python -B shell/tools/es2loadout.py --core "$bin/shell/es2prep_core.exe" `
  --cd-root build/extracted/cd --loadout shell/loadouts/c3_09-loadout-b.json `
  --output $env:ES2_DATA_REPO/oracle/launch/c3_09-loadout-b
# 4. Optional independent re-proof of the sealed SAV with the original worker
#    (one-shot, original seed initializer) into an ignored scratch directory.
& $python -B tools/reference-game/shell-invitro/es2shellprep.py `
  --worker "$bin/tools/reference-game/shell-invitro/es2shellprep.exe" `
  --cd-root build/extracted/cd `
  --save $env:ES2_DATA_REPO/oracle/launch/c3_09-loadout-b/loadout.sav `
  --campaign 3:8 --mode 1 --seed 0 --language G --output build/claude/c3_09-loadout-b-reproof
# 5. Continue with discovery, recording and verification as in "Authoring a
#    recorded suite", with -Launch c3_09-loadout-b (Read-SuiteLaunch accepts the manifest).
```

`--base-sav <SAV>` starts from a real save (read-only) instead of the
CD-default career; the generated SAV drops that save's opaque tail. With the
CD-default career the persistent bank is zero, like the existing
`c3_09-default` set. Language and seed are loadout fields (default GER/0).

**Limits.** Byte identity with the original preparation does not prove that the
VSHELL UI would have allowed the loadout (checked: weapon list, ARM hardpoints,
availability flags; not modeled: money, inventory counts, Herc unlock and
construction) nor that DBSIM runs it. A live DBSIM smoke test of each set
comes first; every manifest lists these unchecked conditions.

### Published loadout sets

Sealed and proved into `es2-data/oracle/launch/` (ignored by
es2-data git), CD-default career (player "Commander", rank 0), German, seed 0,
mode 1; each directory holds the four files, `manifest.json`, `loadout.json`
and `loadout.sav`. Loadout sources: `shell/loadouts/<name>.json`. All read
with `Read-SuiteLaunch`. Slot:item pairs use WEAPONS.DAT IDs
([weapon_classes.md](../reverse-engineering/weapon_classes.md)); team pilots are
the selected pilots of roster groups 0/1/2 (BOWMAN, PHONG, LOGAN in the
CD-default roster).

| Name | Mission (world) | Player | Team (MEC records) |
| --- | --- | --- | --- |
| `c3_09-loadout-b` | C3_09 Omicron (4, volcanic textures) | Ogre: 0:25 1:12 2:24 3:5 4:23 5:15 6:30 7:11 8:4 9:31 | Apocalypse 0:18 1:22 2:17 3:9 4:2 5:6 6:7 7:14 8:29; Samson 0:3 1:32 2:8 3:13 4:1 5:10 6:7 7:25; Colossus 0:12 1:31 2:24 3:5 4:30 5:15 6:29 7:25 8:22 (4) |
| `c4_08-razor-plasma` | C4_08 Bravo (6, texture set `ice`) | Razor: 0:3 1:10 2:16 3:16 4:25 5:25 6:30 | none (mission capacity 1) |
| `c1_04-razor-plasma` | C1_04 (0, urban) | Razor: 0:2 1:2 2:15 3:15 4:25 5:25 6:18 | none (capacity 1) |
| `c3_10-loadout-hercs` | C3_10 Omicron (4) | Tomahawk: 0:18 1:22 2:9 3:31 4:25 | Raptor II 0:8 1:23 2:10 3:17 4:14; Maverick 0:24 1:13 2:6 3:32; Outlaw 0:1 1:3 2:7 (4) |
| `c3_09-loadout-unchecked` | C3_09 Omicron (4) | Ogre: 0:19 1:20 2:21 3:27 4:28 5:13 6:30 7:11 8:4 9:31 | Apocalypse 0:2 1:9 (2) |
| `c5_05-moon-missiles` | C5_05 moon (CAREER group 5, WORLD8) | Ogre: 0:25 1:12 2:24 3:5 4:23 5:13 6:14 7:11 8:4 9:15 (the three missile racks 13/14/15 at the rack hardpoints) | Apocalypse 0:18 1:22 2:17 3:9 4:2 5:6 6:7 7:13 8:15; Colossus 0:12 1:14 2:24 3:15 4:13 5:30 6:29 7:25 8:22; Samson 0:3 1:32 2:8 3:14 4:1 5:10 6:7 7:25 (4) |

Mission choice from the data: the C3_09 and C3_10 briefings recommend four
Ogre-class Hercs and both admit three team members. For (c), C4_08 is a Bravo mission (CAREER group 4,
sector ID 3), whose script header selects WORLD6, the world with the `ice`
texture set (WORLD6/7), and whose briefing recommends one Razor; C1_04 is the
Razor's test flight, whose briefing recommends twin plasma cannons. Plasma
cannons fit the Razor only at hardpoints 4 and 5. Whether these missions
contain the ramps the planned scenes need is not known from the data.

Together the sets fit every Herc and every item ID except 26 (LAEW), which
stopped DBSIM before tick 0 in probes
([LAEW](#laew-0x40ea04-no-retail-path)). `c3_09-loadout-unchecked`
carries the IDs the hangar never offers (19, 20, 21, 27, 28); its manifest
records them as `armory-list` findings, and it is meant for a DBSIM smoke test
only. The other sets satisfy every checked hangar rule.

### Kept sets: start tests and watch sets

The kept sets are `c3_09-loadout-b` (suite b), `c4_08-razor-plasma` (suite c)
and `c3_10-loadout-hercs` (Herc coverage); `c1_04-razor-plasma` and
`c3_09-loadout-unchecked` are not used. Each kept set starts in `timing-r111`
with `run-prepared-suite.ps1` (replay of a journal without input entries,
virtual time 47 ms, original hooks, discovery watches from
`propose_watch_set.py --bootstrap`) without assertion or stall, and the files
are restored.
At runtime the player's team list holds 4 / 1 / 4 actors, matching player plus
3 / 0 / 3 team members, and 48 / 39 / 41 actors are in the actor list. A dump
of 658 bytes of every team actor shows no byte or word field equal to the Herc
ID or the mounted item count, and no dword linear in the Herc ID, so the
runtime Herc type is an **open point** from actor memory; it rests on the
proved `player.mec` bytes.

Classification runs without input (23 actors each: actor bytes 0..0x48 and the
group header via +0x45) give every actor's vtable, position, group, group
count and side. The class comes from the vtable's +0x50 slot in the
hash-checked executable: 41f7b8 vehicle, 411aec building (vtables 49a282
vehicle; 4978ac, 497940, 497818, 4979d4 buildings). Across all runs of a set
the actor list has the same relative layout (only the heap base moves), so
index-based watches are stable. Per set there are a classification, the final
watch file and its review.

| Set | Actors | Own side | Hostile vehicles | Hostile buildings | Selected hostiles | Watches |
|---|---:|---|---|---:|---|---:|
| `c3_09-loadout-b` | 48 | player 12, team 13–15 | 0–2 unplaced at the player start (single groups), 3–5, 6–8, 9–11 (groups of three) | 32 (turrets and other classes) | 0, 3–11 | 48 (route point 1 of 2 omitted) |
| `c4_08-razor-plasma` | 39 | player 10 | 0–9 (groups of 1, 2, 2, 2, 2, 1) | 28 | 0–9 | 44 (6 route points) |
| `c3_10-loadout-hercs` | 41 | player 12, team 13–15, 25 own buildings | 0–2 unplaced, 3–6, 7–8, 9–11 | 0 | 0, 3–11 | 46 (no player route) |

The 48-watch limit leaves room for ten hostiles next to a three-member team.
All placed hostile vehicles are watched; of the three unplaced vehicles that
sit on the player's start position (probably later reinforcements) one is
watched so that their activation shows. Hostile buildings are not watched;
their effect on the player and team is visible through the watched state.
The bootstrap watches only 39 sides, so `propose_watch_set.py` lists actors
39..47 of C3_09 and 39, 40 of C3_10 as unknown sides; the classification runs
show them as hostile buildings (C3_09) and own buildings (C3_10). A final
proposal therefore needs a reviewed `--actors` selection or more side watches.

Recording names equal the launch names:

```powershell
$data = $env:ES2_DATA_REPO; $setup = 'tools\reference-game\setup'
$work = "$data\logs\<work>"
$set = 'c3_09-loadout-b'   # or c4_08-razor-plasma, c3_10-loadout-hercs
& "$setup\record-suite.ps1" -Name $set -Launch $set `
    -WatchFile "$work\$set\watches.txt" -Outcome -Ticks 40000 `
    -OutputDirectory "$work\$set-recording"
```

### Moon missile set `c5_05-moon-missiles`

C5_05 (CAREER group 5, position 4, WORLD8) is one of three moon missions that
admit a team of three. Loadout `shell/loadouts/c5_05-moon-missiles.json`
(table above): player Ogre with the three missile racks 13/14/15 at its rack
hardpoints 5/6/9, team Apocalypse, Colossus and Samson with racks plus other
weapons. Sealed and proved into `oracle/launch/c5_05-moon-missiles`, German, seed 0,
mode 1, CD-default career. Authoring:

- Bootstrap discovery run: 48 watches, actor upper bound 54, actors 39..53
  unobserved.
- Three classification runs over actors 0..53 (vtable and side); two
  identical runs without input (heap layout stable); start without stall;
  final watch set readable.
- The 47 watches (mission globals and variables, player, three team members,
  hostiles 0–6 and 15–16, three route points, camera); hostiles 7–10 omitted
  for the 48-watch limit; actors 39..47 unknown side in the bootstrap
  (classification runs decide).
- The recording the suite is built from (8,262 frames, mission
  won: `results.dat` Boolean 1). Two automatic alerts are answered by mouse
  (frames 164, 5985), the objectives screen is opened with F11 twice (8122,
  8141) and EINSATZALARM with Q three times (8219 and 8242 answered with Enter,
  8262 with →, click: end of mission).
- Verification with `-ModalOpenerKeys 0x51,0x7A`.
- Trap: a recording started from a sandboxed tool process loses sound
  (hypothesis: the game's audio driver gets too little scheduling time under
  the sandbox's process rights); record from an unrestricted PowerShell 7.

**The suite is the replay, not the recording.** Both replays exit naturally
after 8,262 ticks and are identical to each other, but **not to the
recording**. The recording's virtual clock advanced in two ways the replay
cannot reproduce. (1) Frame 163 has the pause flag (a focus loss; the click
at 164 resumed): 49 pause passes, each one limiter poll, added 2,303 ms. (2)
Every modal screen that stayed open longer than about one second ran the
modal loop past the stall limit of `virtual_time.h` (100,000 `GetTickCount`
calls without a limiter poll, then +1 ms per call): stall advances 131,189 at
5985, then +76,778 (8122), +21,652 (8141), +270,067 (8219). The replays
answer the screens after a few hundred peeks and advance nothing. The first
simulation difference is the RNG at frame 5986, right after the 131 s jump;
the outcome (success, tally, pairs) is the same, one team damage word differs
(5Bh/64h). Hypothesis: a clock delta, not the clock value, reaches simulation
state (the 2.3 s offset from frame 164 changed nothing for 5,800 frames). The
registered suite is the deterministic replay of the derived journal, like a
scripted one: the recorded input applied from frame 0, not a copy of the
recorded game after frame 5985. The record = replay rule stays for every
other recording.

**Watch set.** `camera_a` stops at +15 (`camera_a 0x4d24ea@0 22`), like the
camera watches of the other scripted suites: the word at +16 (not in
[camera.md](../reverse-engineering/camera.md)'s layout) is constant within a
run but differs per process (3AC0h, 4544h, 4588h, 467Ch in four runs),
probably uninitialised.

**Golden and replaced check.** With all hooks
`replaced` and the RTL configuration of the full check the suite is identical
to its golden (8,262 ticks, both return files) and every
skipped/bypassed/mismatch counter is 0.

**What it drives.** First live calls of `base_offsets`, `flyer_patrol` and
`flyer_part`; far more calls than any other suite of `actor_rollback`,
`base_attack` and `part_facing`. In edge mode against the union of the
other suites ([live_coverage.md](live_coverage.md#edge-mode)) it
adds outcomes mostly in `hud_key`, `trigger_scan`, `flyer_patrol` and
`flyer_part`. `ally_nearest` is live in this suite. Open: the entry probes
(ally notification `0x423974`).

## Scripted suites

Most suites are input scripts written from the
listing of the reference `VER95/DBSIM.EXE` (image
base `0x400000`; key, jump and vtable tables read from the file bytes) and
from golden tick logs of earlier suites. A script lives in
`tools/reference-game/input-scripts/<name>.jsonl` and must load without error
in the proxy's own journal parser (`ij_load` of `proxy/input_journal.h`) and
pass `recording_compare.validate_journal`: every `api` line accepted, sorted
by frame and pump key, no CR, every key and mouse button released at the end.
A script that starts with the entries of a registered suite's script up to
frame f (checked line by line) equals that golden up to frame f; a prefix of
recorded entries keeps the source recording's auto-repeat key downs. The
frames, distances and counters a script was planned with are hypotheses; the
golden and the replaced check decide. Each suite section below gives the
script, the registry fields, what it drives and what stays open.

Key combinations: modifiers down at f, the key at f+1..f+2, modifiers up at
f+3 (the replay's virtual key state answers `GetKeyState`). Keys go through
the key table `0x4a10ec` (VK → code). New launch sets come from a loadout
(`es2loadout.py --report`, then `--output`, see [Suites from a
loadout](#suites-from-a-loadout)); their watch sets from a bootstrap
discovery and `propose_watch_set.py`.

The workflow for a new scripted suite (each command takes `-DryRun` first;
the hook names in `-Hooks` are those of the [registry](hooks.md#registry);
`run-prepared-suite.ps1` writes `<RunName>.jsonl` in its output directory,
whose tick records carry the `hk_*` counters):

```powershell
$repo = '.'; $setup = "$repo\tools\reference-game\setup"
$python = 'python'
$data = $env:ES2_DATA_REPO; $work = "$data\logs\<work>"; $draft = "$work\draft-suites.json"
$scripts = "$repo\tools\reference-game\input-scripts"; $ws = "$repo\tools\reference-game\watch-sets"
$bin = 'build/bazel/es2-shell/execroot/_main/bazel-out/x86_windows-fastbuild/bin'
$cmp = "$repo\tools\reference-game\scripts\compare_tick_logs.py"

# 0. A new launch set: seal it (no game), bootstrap discovery, final watches.
& $python -B "$repo\shell\tools\es2loadout.py" --core "$bin/shell/es2prep_core.exe" `
  --cd-root build/extracted/cd --loadout "$repo\shell\loadouts\<launch>.json" `
  --output "$data\oracle\launch\<launch>"
& $python -B "$repo\tools\reference-game\scripts\propose_watch_set.py" --launch "$data\oracle\launch\<launch>" `
  --bootstrap --output "$work\<launch>-discovery.txt" --report "$work\<launch>-discovery-review.json"
& "$setup\run-prepared-suite.ps1" -OutputDirectory "$work\<launch>-discovery" -RunName <launch>-discovery `
  -Launch <launch> -InputJournal "$scripts\<suite>.jsonl" -WatchFile "$work\<launch>-discovery.txt" -Ticks 300 -SnapshotOnStall
& $python -B "$repo\tools\reference-game\scripts\propose_watch_set.py" --launch "$data\oracle\launch\<launch>" `
  --log "$work\<launch>-discovery\<launch>-discovery.jsonl" --output "$work\<launch>.txt" --report "$work\<launch>-watch-review.json"
# Review it, then commit it as tools/reference-game/watch-sets/<launch>.txt.

# 1. Discovery, original hooks, twice (replay = replay); optional extra watches in a scratch copy.
foreach ($k in 1, 2) {
  & "$setup\run-prepared-suite.ps1" -OutputDirectory "$work\<suite>-$k" -RunName "<suite>-$k" -Launch <launch> `
    -InputJournal "$scripts\<suite>.jsonl" -WatchFile "$work\<suite>-discovery.txt" -Ticks <ticks> -SnapshotOnStall }
& $python -B $cmp "$work\<suite>-1\<suite>-1.jsonl" "$work\<suite>-2\<suite>-2.jsonl"
# A script with a registered prefix: its first difference to that golden must not come before the prefix ends.
& $python -B $cmp "$work\<suite>-1\<suite>-1.jsonl" "$data\oracle\golden\<source-suite>\tick-log.jsonl"
# Review stalls (see "Answering a modal alert"), the watched state at the planned frames; move frames if needed.

# 2. Goldens from the clean reviewed tree, then checks with all hooks original and all replaced.
& "$setup\run-oracle-regression.ps1" -Mode record -SuitesFile $draft -Suite <suite> -RepositoryRoot $repo `
  -OutputDirectory "$work\golden-run"
& "$setup\run-oracle-regression.ps1" -Mode check -SuitesFile $draft -Suite <suite> -RepositoryRoot $repo `
  -OutputDirectory "$work\check-original"
& "$setup\run-oracle-regression.ps1" -Mode check -SuitesFile $draft -Suite <suite> -RepositoryRoot $repo `
  -OutputDirectory "$work\check-replaced" -Hooks '<the full selection line of hooks.md>'

# 3. Register: append the entry to oracle-suites.json and a row (launch set, covers) to the Suites table of oracle_regression.md.
```

The edge-mode coverage of a registered suite is measured as described in
[live_coverage.md](live_coverage.md#edge-mode).

## Suites with DBSIM arguments

A suite whose scene needs another DBSIM command line (the cheat switch
`-SPRUNKNOWN`, the tape option `-rc1tape`) names the extra arguments in its
registry entry (`game_arguments`). Mechanics and refusals:
[oracle_regression.md](oracle_regression.md#suite-dbsim-arguments-game_arguments).
The registered example (format 3 is only a label; the scripts do not read it):

```json
{
  "name": "c1_01-cheat-destroy",
  "script": "input-scripts/c1_01-cheat-destroy.jsonl",
  "watches": "watch-sets/c1_01-mission.txt",
  "ticks": 600,
  "outcome": true,
  "game_arguments": ["-SPRUNKNOWN"]
}
```

A suite on a launch set adds `"launch"` as usual. From a clean reviewed tree:

```powershell
# Dry plan: arguments.GameArguments is ['-SPRUNKNOWN'] for this suite only
& "$setup\run-oracle-regression.ps1" -Mode record -Suite c1_01-cheat-destroy -DryRun
# Golden: its manifest.json records game_arguments (and suite_game_arguments)
& "$setup\run-oracle-regression.ps1" -Mode record -Suite c1_01-cheat-destroy -RepositoryRoot $repo
# Check (run-oracle-parallel.ps1 reads the same field)
& "$setup\run-oracle-regression.ps1" -Mode check -Suite c1_01-cheat-destroy -Hooks 'group_assess=replaced,...'
```

Do not add `-SPRUNKNOWN` to the command line of such a run: it repeats the
suite's argument and is refused (`run-oracle-regression.ps1 -Mode record`
refuses a command-line `-GameArguments` altogether). Changing a registered
suite's `game_arguments` later needs a new golden (`-Mode record -Force`);
until then every check of that suite refuses and names both lists.
`run-prepared-suite.ps1` (discovery, no golden) takes `-GameArguments`.

## Answering a modal alert

A modal alert (the mission outcome alert, Q's EINSATZALARM, the pause and
quit dialogs) stops the logic frame until it is answered; an unanswered alert
stalls the run. Scripts answer it with pump-keyed lines inside the frame in
which it opens: the key lines carry `"p"`, the pump count inside that frame.
When the alert frame is not known statically (it depends on how damage or
the outcome evaluation develops):

1. Run a discovery without an answer (`run-prepared-suite.ps1 … -StallSeconds
   10 -SnapshotOnStall`). It stalls in the alert at frame F = last logged
   `frame_counter_before_increment` + 1 (as in the `Q` probe of
   [input_journal.md](input_journal.md#modal-dialogs-in-replay)).
2. Append `{"f":F,"p":300,"api":"keydown","vk":13}` and
   `{"f":F,"p":310,"api":"keyup","vk":13}` to the script (repository copy).
   Enter presses the first button; for the outcome alert the result is
   (button = word `0x49f5c0[status]`), and for status 3 and 12h
   `0x49f5c0[3]` = 0, so the first button ends the mission and DBSIM writes
   `results.dat` and `mission.var`.
3. Two original runs of the answered script must agree (`-KeepReturns`, then
   `compare_tick_logs.py`); then one replaced run.

`outcome` in the registry follows how the run ends; `ticks` of an answered
outcome suite is a bound, the run ends at the answer. Q answered with Enter
(first button) continues the mission; Tab, Enter (second button) ends it,
and DBSIM writes `results.dat`/`mission.var` and exits for statuses 4/5/9/10
(the `c1_01-success` pattern: the alert stands in the Q frame). Ctrl and Alt
must be released (p 290) before an Esc answer: Ctrl+Esc (401h) and Alt+Esc
(201h) are focus keys that are not queued. Answering by pump keys keeps a
dialog open for a few hundred peeks, far below the one-second stall advance
of `virtual_time.h` that makes long modal screens unreproducible
([Moon missile set](#moon-missile-set-c5_05-moon-missiles)).

## Smoke: no allocation site

The smoke hooks (`smoke_ball_update` `0x4091cc`, `smoke_update` `0x4092dc`,
`smoke_ball_init` `0x409178`) are **dead in the retail binary**: nothing
allocates from the `SMOKE` pool `[0x4a9684]`, so no suite can reach them.

The `SMOKE` emitter pool is created at `0x4090cc` (20 objects of 2Ah bytes,
pointer stored at `0x40910f`). The image has exactly five code references
to its pointer `0x4a9684`: the removal flush `0x408f9c`, the store at
`0x40910f`, the clear at `0x4094d2`, the destruction `0x4094f7` and the
update loop `0x45f665` (row 5 of the logic frame, which calls
`smoke_update`). The two DATA references (`0x497fc2`, `0x497fce`) are
{object, destructor} records of the static-object cleanup table next to the
frame descriptor `0x497f9a`. No `pool_alloc` (`0x471b30`) call receives the
pool, so no emitter is ever created and `smoke_update` never runs.
`SMOKE_BALL` objects (pool `0x4a966e`) are allocated only at `0x40941b`
inside `smoke_update` (all references: `0x408f84` flush, `0x408fff` store,
`0x40941b`, `0x4094bb` clear, `0x40950f` destruction, `0x4286e8` the draw
pass, `0x45f624` the update loop), and `smoke_ball_init` has the single call
`0x409442` in `smoke_update`. All three hooks are therefore dead code in the
retail game: no input, mission or recording reaches them, and they keep
their in-vitro evidence only
([function_verdicts.md](function_verdicts.md#dead-smoke-hooks)).
The SMOKE emitter pool is never filled in any run.

## Cockpit target keys

**`c1_01-cockpit-keys`** drives `cockpit_nearest` `0x4333c8` and
`cockpit_select` `0x4332dc` with the keys `'` (VK DEh → code 28h) and `;`
(VK BAh → code 27h); key table `0x4a10ec`: DEh → 28h, BAh → 27h.

- **Script** [c1_01-cockpit-keys.jsonl](../../tools/reference-game/input-scripts/c1_01-cockpit-keys.jsonl):
  the `c1_01-success` entries up to frame 960 unchanged (the same route,
  Enter/T at 546–552, fire bursts), plus `'` at frames 105, 858, 862, 870
  and `;` at 855, 866, each held one frame.
- **Registry** legacy prepared C1_01 (no `launch`), watches
  `watch-sets/c1_01-mission-hostiles.txt`, no DBSIM arguments.
- **Why these frames** (golden tick log of `c1_01-success`): at frame 105
  the player has not moved; hostiles h0–h2 still sit unplaced on its start
  point and h3 is 268,000 away, so the first `'` tests the nearest search at
  mission start. Between frames 850 and 875 hostile h3 is 25,000–21,000 away
  with `+99h`/`+A4h` clear while the player fires at it: `;` at 855 clears
  the target (`cockpit_select(H, 0)`), `'` at 858 picks h3 (new target: store
  and HUD record update), `'` at 862 finds the same winner (no update), `;`
  at 866 clears again and `'` at 870 sets it again. The inserted keys change
  the run from frame 105 on, so the later frames are this suite's own
  behaviour, not the `c1_01-success` golden's.
- **Live:** with the hooks replaced `hk_cockpit_nearest_calls` 4 and
  `hk_cockpit_select_calls` 2 at tick 1000, one per key press (`;` reaches
  `0x4332dc` only through the HUD key handler `0x432bc8`, which ignores keys
  while byte `H+20Fh` is set), so code 28h passes the HUD handler's default
  chain (MFD `0x446c38` and the panels' virtual `+0Ch`) to `0x460812`.
  Golden and replaced check identical. Sites: `cockpit_nearest` both edges of the
  "winner ≠ current target" test (858/870 against 862) and its
  valid/invalid tests; `cockpit_select` the fall edges of
  `0x4332eb`/`0x43338f` (its other seven sites are dead).

## Escort route

**`c1_08-escort-route`** drives `order_actor` `0x4238a0`: any mission whose
mode word `0x4a9e54` is 3 or 7 calls it every frame from `player_route`. No
input is needed; the script only gives the Herc some way.

- **Mission data.** The mission mode word `0x4a9e54` is script header +6,
  which VSHELL takes from the fourth parameter word (+8) of the MSN list-2
  environment overlay. Survey of all 62 MSN members of `VOL/ZONES.VOL`: mode
  3 unconditionally in C1_08, C2_04, C3_03, C5_01, C5_03, C5_06; mode 7 in
  C1_09 under condition 30; the training missions carry 2/3 there; mode 0
  in every other mission, including all missions of the recorded
  suites. The prepared C1_08 has header bytes `00 00 7b 00 01 00 03 00 …`
  (+6 = 3), and its player group 0 has the single order 7: type 3, scope 3
  (an object of the third pool), subject index 0.
- **Path.** `player_route` `0x41c194` (the player's state callback) calls
  `order_actor([player+45h])` at `0x41c2bc` every frame in modes 3/7 unless
  dword `0x4a9cec` = 4; `target_valid` `0x411e80` calls it at `0x411f67` in
  mode 3 for the player. `player_route` dereferences the returned actor
  without a null test (`0x41c2ca`).
- **Loadout** [c1_08-escort.json](../../shell/loadouts/c1_08-escort.json),
  launch set `c1_08-escort`: C1_08, player Tomahawk (2) with the
  `c3_10-loadout-hercs` player weapons 18, 22, 9, 31, 25, no team, GER, seed
  0. `es2loadout.py --report`: no findings, preparation equal to the
  original. The bootstrap discovery sees 16
  actors, 15 on the other side; the watch set `watch-sets/c1_08-escort.txt`
  keeps hostiles 0–6 and 8–13 (14 and 15 left out by the 48-watch limit).
- **Script** [c1_08-escort-route.jsonl](../../tools/reference-game/input-scripts/c1_08-escort-route.jsonl):
  ↑ held from frame 100 to 120 (throttle up, as in `acceptance-60s`),
  nothing else.
- **Live:** with the hooks replaced `hk_order_actor_calls` and
  `hk_player_route_calls` are equal and about one per logic frame, no bypass,
  no mismatch. Sites: `order_actor` `0x4238af` (order
  non-null), the scope tests for scope 3, the mode-3 sites of
  `player_route` (41c2a5 …).
- The launch set is also used by `c1_08-scan`
  ([Mission scenario suites](#mission-scenario-suites)).

## LAEW `0x40ea04`: no retail path

The LAEW emitter `0x40ea04` (item 26) has no retail path; it is an entry
probe, not a hook, and not a recording request (no player can mount it).

Weapons are created only by the factory `0x40fff8`, called only from
`0x4175dc` (`0x417634` via `0x4104ec`, and `0x417684`), whose only caller
is the actor construction `0x4258a2` for mission actor table A and the MEC
records. Survey of all MSN actor-table-A records (144 bytes; the prepared
134-byte record's vehicle word +28h and weapon words +2Ah sit at +30h/+32h
of the MSN record, calibrated on C3_09): **no record in any mission carries
item 26**; item 6 (ELF), for comparison, occurs in C4_08, C3_10, C5_02 and
others. The hangar list (`0x476a4c`) does not offer 26 either
([weapon_classes.md](../reverse-engineering/weapon_classes.md#hangar-mounting-vshell)).
So LAEW fire needs a hand-made MEC, and a player MEC with item 26 stopped
DBSIM before tick 0 in a probe.

Open: a single start probe, to learn whether the stop is specific to the
player record:
[c3_10-laew-team.json](../../shell/loadouts/c3_10-laew-team.json)
(`c3_10-loadout-hercs` with LAEW instead of EMP on team member 3, Outlaw
slot 2; `ui_rules` report; `--report`: findings `armory-list` and
`known-dbsim-stop`, preparation equal to the original). Seal it as `c3_10-laew-team` and start it once with the
`c3_10-loadout-hercs` watches, the `c1_08-escort-route` script and the entry
probe `40ea04` (`run-prepared-suite.ps1 -Launch c3_10-laew-team -Ticks 1000
-SnapshotOnStall`). If DBSIM starts, the probe shows whether the AI fires it;
if it stops again, LAEW is closed as unreachable.

## Wingman orders

Three suites drive the order paths of the player's team: `c3_09-wingman-orders`
(every group and map order once), `c3_09-orders-matrix` (order sequences on
its prefix) and `c5_05-orders-flyer` (an attack order on an AI flyer). Both
order paths have keyboard bindings, so the scripts need no screen
coordinates except one map point.

### Order keys (read from the bytes)

- **Group orders (MFD).** `0x446c38` (the MFD's key method, reached from
  the HUD key handler's default chain `0x432f92` in every display mode)
  takes the codes 212h–22Eh through the byte table `0x446c65` and the jump
  table `0x446c82`: Alt+A (21Eh) menu item 0, Alt+G (222h) item 1, Alt+H
  (223h) item 2, Alt+O (218h) item 3, Alt+C (22Eh) item 4 only while its
  code is 4, Alt+E (212h) item 4 only while its code is 7, Alt+F (221h)
  item 5. `0x447248` sends code `0x43f9c0(menu)` = item, plus 3 when bit 1
  of the item's flag byte (`menu+2Ch+item`) is set, through `0x43166c` to
  `0x4231a4([0x49b0f8], 0x4d03d4)` (the player's group); after a taken
  order `0x43f9f8` flips that bit for items 4 and 5 only. So the codes are
  0 (attack the cockpit target), 1 (break off), 2 (engage the nearest
  attacker of the player), 3 (rejoin), 4/7 (hold/release, toggling) and 5/8
  (engage at will/stop, toggling). `0x4231a4` calls each eligible member's
  virtual `+28h` with mode word 0 first, then 1 after a refusal and 2 after
  a taken order (`0x4232f9`, the "mode word 1" caller of `mech_order`
  421565/42156a); for codes 0 and 2 it stops at the first member that takes
  the order. Neither `0x447248` nor `0x4231a4` is hooked.
- **Per-wingman orders (HDD map, display mode 1).** In display mode 1
  (Esc from the cockpit) the HUD key handler forwards to the HDD gauge
  `[H+1F9h]` (vtable `0x49d64c`, key method `0x449ff4`); keys 1/2/3 press
  its buttons 0Ah/0Bh/0Ch (select wingman `0x4d03c8[0..2]`, `0x44a748`),
  and keys the gauge does not handle go to the active page's slot `+10h`,
  for the map page (page 0 from the constructor, `0x4498ca`) `0x44cc68`
  (HDD key table `0x44cc8c`/`0x44cccb`):
  D/A/F/T/G/O/C/E (codes 20h, 1Eh, 21h, 14h, 22h, 18h, 2Eh, 12h) press
  the map's mode buttons 0Ah (rejoin), 0Bh (attack an enemy), 0Ch (guard a
  point or an own actor), 0Dh (move, state 2), 0Eh (move, state 1), 0Fh
  (rejoin), 10h (hold), 11h (release) through `0x44d5c0`/`0x44d3cc` (a
  synthesized click on the button); X (2Dh) presses HDD button 0Dh, the
  send (`0x44a554`: commit `0x44db9c`, order record `0x4d03d4` ← map mode,
  `wingman_order` `0x431610`); Backspace presses button 0Eh (cancel).
  In a pick mode (0Bh–0Eh, map byte `M+103h` set) Enter (`0x44cded`)
  synthesizes a map click at the current cursor (`0x44d5c0(M, [M+39h],
  0)`, then `map_click` `0x44d6e0`), and Tab (`0x44ce4a`) picks the next
  actor of the wanted side without a click (`0x44ef30`: side byte, not out,
  inside the map view, byte `+98h`, for enemies `0x411c58(player, T)`).
  The map centre is the player's position plus a scroll offset
  (`0x44e3de..0x44e401`), which only the HDD's arrow buttons change.
- **HDD map picks** (`0x44cc68`, `0x44d5c0`, `0x44ef30`). Tab in mode 0Bh
  picks the next enemy, in 0Ch the next own actor (after the last pick
  `M+15Ch` when that one has the wanted side, else from the list start).
  Enter in mode 0Bh/0Ch with `M+15Ch` set synthesizes a click at that
  actor's own map position (`0x44d5c0(M, map, 1)`), so it re-picks the
  last picked actor, also after it went out (`map_click` has no out test);
  in 0Dh/0Eh Enter clicks at the cursor. Tab in 0Dh/0Eh stores `M+15Ch` as
  an actor and leaves the record point unchanged. Wingman slots: W0 =
  `team1`, W1 = `team2`, W2 = `team3` of the watch sets.
- **The HDD gauge** ignores buttons other than 0/1 while its byte `+76h` (a
  timed power or animation state, `0x438bc0`) is set.
- **Cross-check with the c3_09 recording.** Its three code-0Bh orders are
  the click groups 983–1049, 1060–1112 and 1122–1186 (wingman row at
  y≈330–348, mode column at x≈415–440, map pick near (181,178), send at
  (430,223)); the golden shows `team1/2/3_target` set at frames 1051, 1115
  and 1188, right after each send click. The fourth order (code 0Eh) is the
  5517–5697 group (map pick (246,163)); the five `map_click` calls are the
  five map picks.

### Order facts (read from the bytes)

- **The order record keeps its object.** `0x44db4c` copies a committed
  wingman record into `0x4d03d4`: kind 0 (point) writes only `+6`, kind 1
  (actor) only `+12h`; `0x44a1a0` and the MFD path `0x447248` write only
  the code word. No other instruction addresses `0x4d03d4`. So once any
  actor was picked, `R+12h` stays nonzero for the rest of the mission, and
  `A+24Ch` (written only at `0x4214d8`, from `R+12h`) can be nonzero only
  after such a pick. Hence `mech_order` `4213c1` taken and `4213de` taken
  (guarded object set, record object 0) cannot be reached by the player's
  orders; the other callers of the virtual `+28h` were not classified. A
  code above 11h (`420af1` taken) is not produced either: the map sends
  its mode `0Ah..11h`, the MFD `0x43f9c0(menu)` (at most 8).
- **Order states 3 and 5 are never written.** The 15 stores to word
  `+23Eh` write 0, 1, 2, 4 or 6 (`415f6a`, `41c496`, `41eccc`, `41fa44`,
  `420c20`, `420ce1`, `420ed1`, `4210a5`, `4210b7`, `42110f`, `421233`,
  `4212de`, `42136e`, `421496`, `421541`; block copies into the actor were
  not looked for). Dead candidates: `actor_destination` `41dbdc`/`41dbe1`
  taken, the `order_resume` table cases 3/5 and `41ebe2` taken.
- **State 4 implies an order target.** `A+248h` is written by codes 0, 2
  and 0Bh (each nonzero, in the path that stores state 4) and at `41e0cd`
  (zero), which lies in the unreachable second body of `drive_off`
  (41df1b..41e0d6). Dead candidates (one-way): `base_attack` `41c902`
  fall, `hunt` `41e2e4` taken, `mech_attacked` `41fa08` taken.
  `attack_plan` `41d0a6` taken needs state 6 with no earlier attack order.
- **Out branches, not state 2.** `order_scan` `41d8c8`, `41d8d1`, `41d8e8`,
  `41d908` are the out branch (`41d8b1` fall: the scanning actor is out,
  `target_select(A, 0, 0)` and the retreat table `499800h`).
  `retreat_check` `41cbf3`, `41cbf9`, `41cc08`, `41cc1e` lie behind
  `41cbdd` fall (the planning actor is out), so a building target alone
  does not reach them. Both need a disabled actor, not an order.
- **Group order call order** (`0x4231a4`). Each pass picks, among the
  members not yet called, not `+99h` and not the sender, the one with the
  highest priority word (codes 0, 2, 5: script flag +1 clear; 1, 8, 0Fh:
  that flag; 4: `B+4` clear; 7: `B+4`; other codes: a stale stack word,
  [input.md](../reverse-engineering/input.md#the-residue-reader-the-group-order-0x4231a4)),
  ties to the nearest to the sender; mode word 0 first, 1 after a refusal
  while nothing was taken, 2 after a take; codes 0 and 2 stop after the
  first take. So `42156a` fall (mode 1, taken) needs a refusal followed by
  a take within one group order.

### `c3_09-wingman-orders`

- **Script** [c3_09-wingman-orders.jsonl](../../tools/reference-game/input-scripts/c3_09-wingman-orders.jsonl),
  launch `c3_09-loadout-b` (team 13–15 in wingman slots 0–2), watches
  `watch-sets/c3_09-loadout-b.txt` (team targets and flags are in it).
- **Phase 1, mode 0, player standing at the start, no cockpit target:**
  Alt+O 150 (code 3, wingmen within 25,000: answer 1Eh), Alt+C 180 (4),
  Alt+E 210 (7), Alt+F 240 (5, no target in range), Alt+F 270 (8), Alt+H 300
  (2, no attacker: 1Dh), Alt+G 330 (1, T = 0: 12h), Alt+A 360 (0, T = 0:
  12h).
- **Phase 2, display mode 1 (Esc 600), player still standing so that the
  map centre is fixed; cursor (246,163) from 640** (the recording's 0Eh map
  point): W0 0Dh with an Enter pick (650), W1 0Eh (690), W2 0Ch on the point
  (730) and on the same point again (770, `same` → answer 20h), W0 10h hold
  (810), hold again (840, `B+4` set → 0Fh), 11h release (870), W0 0Ch with
  Tab (900: the first own actor, a guarded object), W2 0Bh with Tab (940),
  W1 0Eh on the same point at 1100 and 1400, W0 0Ah (1430) and W2 0Fh
  (1450), Esc 1470. Each order is: wingman key at f, mode key f+6, pick
  f+12, X f+18 (or f+12 without pick).
- **Phase 3, mode 0:** Alt+O 1480 (code 3 again, now with W0/W1 away from
  the leader: the far edge 421527), full throttle 1500–1520 on the start
  heading (32912, toward −y), throttle down 2270–2285 at about (440k,
  663k): hostile buildings 16 (421080, 618960) and 17 (472684, 617796) are
  then within the 60,000 radar range and the ±9000 arc. Enter 2300
  (`cockpit_next`), Alt+A 2310 (code 0 on a building: `A+250h` = building
  class), Alt+G 2400 (code 1 on the same target), Alt+A 2460, Alt+H 2520
  (code 2, turrets firing at the player), Alt+F 2560/2620 (codes 5 and 8
  with targets in range), then Esc 2650 and two Tab-picked 0Bh orders (2690
  W1, 2730 W0 second candidate), Esc 2780.
- **Golden facts** (used by the matrix suite): the player stands at
  (440324, 636479) from about 2290. `team3_target` is W2's 0Bh pick at 958,
  `team1` loses its target after W0's rejoin at 1430, `team2` takes W1's
  pick at 2708. The hostile group h3/h4/h5 (consecutive MECH objects)
  targets the player from 1992 and approaches from the west: h3 is 60,000
  away at 2750 and 50,000 at 2850, h4 58,000 at 2825; h4 turns to a wingman
  at 2861, h3 at 2887. h4 is out (`+0A4h`) at 2969 and destroyed at about
  3003, h3 destroyed at 3097, h5 out at 3133 (alive). At 2400 every wingman
  takes Alt+G (byte `+9Ah` set in all three), so the cockpit target is set
  at 2400; at 2460 all three refuse Alt+A (`+9Ah` cleared), so it is gone by
  then. Word `0x4a9e5e` is 0 in C3_09 and C5_05: the cockpit range is
  60,000. The cursor point (246,163) lies near the player (W0 stopped at
  about (436.3k, 731.8k) after its 0Dh order at 650, the player stood at
  (439.1k, 731.3k)).
- **Live:** golden and replaced check identical; with the hooks replaced
  `hk_mech_order_calls` reaches 3 at frame 152 (the first Alt+O),
  `hk_wingman_order_calls` 15, `hk_map_click_calls` 6, no skip or bypass.
  `turbo_engage` is live in this
  suite: `point_move` site 41fb34 (TURBO pointer `[A+317h]` non-null, call at
  41fb37) has its fall edge with first suite `c3_09-wingman-orders` (the
  Colossus wingman carries TURBO 31).
- Not driven: 4211dc (needs a destroyed object), the flyer branch
  (`flyer_attack`; C3_09 has no FLYER actor: all 16 vehicles use vtable
  `0x49a282`), the type-2 group-order branch of code 5.
- **Discovery watches** (one run, not for the golden): the set is full
  (48); replace the nine `h9`–`h11` lines with
  `hdd_map_view 0x49b088@0x1f9@0x101@0x18 12` (centre x, y, scale),
  `hdd_map_shift 0x49b088@0x1f9@0x101@0x14@0x1a 1`,
  `hdd_map_rect 0x49b088@0x1f9@0x101@0x71 16`,
  `hdd_map_mode 0x49b088@0x1f9@0x101@0x5d 4`,
  `hdd_map_pick 0x49b088@0x1f9@0x101@0x374 12`,
  `cockpit_target 0x49b088@0x210 4`,
  `team1_order 0x4d24e6@0x45@0xc@4@0x23e 18`,
  `team2_order 0x4d24e6@0x45@0xc@8@0x23e 18`,
  `team3_order 0x4d24e6@0x45@0xc@12@0x23e 18` (order state, point, target,
  guarded object). `hdd_map_pick` minus the player position at the pick
  frame calibrates the cursor-to-world mapping (`0x44d888`,
  [cockpit.md](../reverse-engineering/cockpit.md)).

### `c3_09-orders-matrix`

**Script** [c3_09-orders-matrix.jsonl](../../tools/reference-game/input-scripts/c3_09-orders-matrix.jsonl):
the 149 entries of `c3_09-wingman-orders.jsonl` with f ≤ 2403 unchanged,
then the tail below. Launch `c3_09-loadout-b`, watches
`watch-sets/c3_09-loadout-b.txt`. Map orders: wingman key f, mode key f+6, pick f+12, X
f+18 (no pick: X f+12); the first map order comes 40 frames after Esc.

| Frames | Input | Target outcome (reason) |
| --- | --- | --- |
| 2405 | Alt+G | `420cfa` fall: code 1 while `B+8` is set (all took code 1 at 2400, target still set) |
| 2412 | Esc (map) | |
| 2452, 2476 | W1 T, Enter, X twice | `42133e` fall and `421358` taken: state 2 already, W1 still ≥ 2,000 from the point |
| 2500 | W2 E, X | `420f6b` taken: release without a hold (all released at 210) |
| 2524, 2548 | W0 F, Tab, X twice | `4213b8` fall, `4213c1` fall (W0's `+24Ch` is set since 900); the second Tab gives the next own actor: `421480` taken (state 6, other object) |
| 2600 | W1 T, Enter, X | `421358` fall: W1 arrived within 2,000 of the point and is still in state 2 (hypothesis) |
| 2624, 2648 | W1 G, Enter, X twice | `4212c1` fall: state 1 and within 2,000 |
| 2700 | Esc (cockpit) | |
| 2765, 2780, 2795, 2810 | Alt+H ×4 | `420e6e` fall, `420ea0` taken (an attacker of the player inside 60,000), `420eb4` both and `420ec6` (a member already on the winner), `42156a` fall (a refusal before a take) |
| 2820 | Esc (map) | |
| 2860, 2884, 2908 | W2 A, Tab / Enter / Tab, X | E_a, E_a again (Enter re-pick): `4211ce` fall, `4211dc` fall; then the next enemy E_b: `4211dc` taken |
| 2932 | W1 A, Tab, X | W1 in state 4 on E_c (for 2975 and the fight) |
| 2960 | Esc (cockpit) | |
| 2970 | `'` | cockpit target = the nearest valid hostile (h3 at 39,000 in the golden) |
| 2975 | Alt+G | `420cdf` taken: members in state 4 on another target |
| 2987, 2999, 3011 | Alt+A ×3 | `420c15` taken (state 4 on another target), `420c0d` fall and `420c15` fall (state 4 on this target), `42156a` fall |
| 3020 | Esc (map) | |
| 3060, 3084 | W0 F, Tab, X twice | the guard again (group orders may have taken W0 out of state 6): `4213b8`/`4213c1` fall, `421480` taken |
| 3108 | W1 A, Tab, X | W1 on E_d, the first live enemy in list order; sets `M+15Ch` for the re-picks |
| 3140, 3360 | Esc, Esc | |
| 3400, 3600, 3800 | W2 / W0 / W1 A, Enter, X | 0Bh on E_d by re-pick after the fight: `421203` taken, `421228` fall once E_d is out (the ordered wingman must not hold E_d in state 4, hence three wingmen) |
| 3840 | Esc (cockpit) | then no input up to 4200 |

Fight outcomes this suite makes likely but cannot schedule (hypotheses):
`mech_attacked` step 9 with state 4 (`41f9ff` fall, `41fa08` fall,
`41fa1a`, `41fa2c`, `41fa3a`: W1/W2 under attack orders after 8,000
damage since their last order), then `attack_replan` with `A+1A4h` ≠
`A+248h` (`attack_plan` `41d086` fall, `41d08f`); the order target
destroyed in state 4 (`attack_plan` `41d0d4` fall, `41d0eb`, `41d0f4`,
`41d10b`, `41d131`; `order_resume` `41ecca` fall); W0 guarding during the
fight (`order_resume` `41ec7c`; `attack_plan` `41d0a6` taken only if W0
never had an attack order); `hunt` `41e2db` fall (state 4); and any out
path of `mech_order` if a wingman is disabled but not destroyed.

**Live:** replay = replay; the first
difference to the `c3_09-wingman-orders` golden is at tick 2471, after the
shared prefix. Golden identical with all hooks original and all replaced.
The cursor point's distance to W1 and the move script's stop radius were
inferred, not measured; whether `421358`/`4212c1` fall were reached is shown
by the edge-mode coverage of the suite.

### `c5_05-orders-flyer`

C3_09 has no FLYER actor. C5_05 has a team of three and two AI flyers:
actors 15 and 16 use the FLYER vtable `0x49a5e0`, side 1 (`classify0` of
the `c5_05-moon-missiles` authoring), both in
`watch-sets/c5_05-moon-missiles.txt` (`h15`, `h16`). In the golden, flyer
15 comes from (790k, 1171k) toward the player's start (999k, 1013k): 69,000
away at 280, 59,000 (2D; 60,400 3D) at 520, 46,000 at 540; the player's
missile (Space at 399) destroys it at 535. Flyer 16 patrols
300,000–400,000 away. No ground hostile is within 60,000 before 1400.

**Script** [c5_05-orders-flyer.jsonl](../../tools/reference-game/input-scripts/c5_05-orders-flyer.jsonl):
the 45 entries of `c5_05-moon-missiles.jsonl` with f ≤ 176 unchanged
(the automatic alert at 164 answered by the recorded click, pump keys
300–370), then no movement and no fire: `'` and Alt+A every 15 frames from
500 to 695 (14 pairs), Esc 715, W2 0Bh by Tab (760) and by Enter re-pick
(784), W1 0Bh by Tab (808), Esc 840, `'` and Alt+A at 900, 960, 1100,
1150, 1200. Launch `c5_05-moon-missiles`, watches
`watch-sets/c5_05-moon-missiles.txt`. The derived journal has no other modal frame before 5985.

**Drives:** `cockpit_nearest` (types 0 and 2 only, no arc) picks flyer 15
once it is the nearest valid target inside 60,000; the next Alt+A is taken
by one wingman: `attack_target` with `T+1A8h` = 2 (`41c14c` fall, `41c14f`
taken, table `0x499556`) and the MECH state "attacking flyer". Live:
replay = replay, first difference to the `c5_05-moon-missiles`
golden at tick 189; golden identical with all hooks original and all
replaced; `flyer_attack` is called (the first live calls in any suite).
Not driven: `attack_target` `41c152` and `41c14f` fall (`T+1A8h` ≥ 3: no
such target known).

### Not reached by orders

- The 33 out-of-A outcomes of `mech_order` (the ordered wingman is out:
  byte `A+0A4h` or `A+0A5h`; `A+99h` members are skipped by `0x4231a4` and
  `0x431610`) and the out branches of `order_scan` and `retreat_check`: they
  need a disabled actor that still takes orders. A cheat variant
  (`-SPRUNKNOWN`, Ctrl+N to a wingman, Alt+Ctrl+D) could disable one, but the
  cheat suites run legacy C1_01, which has no team; a `c3_09-loadout-b`
  cheat suite would need its own golden.
- The cockpit target or the attacked object out (`420bda`, `420bec`,
  `420bfa`; `421203`, `42121a`, `421228`): `421203`/`421228` by the Enter
  re-pick of the matrix suite; the cockpit target is validated every panel
  frame.
- Script flags (`420bbb` flag +4, `420d39`, `420d4f` flag +2, `420d6d`) and
  group order data (`421057`, `421076`, `4210a3`: slot empty or of type 2):
  decided by the flag bytes of the current table (filled by static
  initializers) and by the mission's group orders.
- A list actor with `+0A5h` set in the code-2 walk (`420e05`): combat.
- The dead candidates above (`420af1` taken, `4213c1`/`4213de` taken,
  `actor_destination` 2, `base_attack` `41c902` fall, `hunt` `41e2e4`
  taken, `mech_attacked` `41fa08` taken): material for PROVED notes of
  their hooks, not for a suite.

**Discovery watches** for the order suites (one run each, not for the
golden): `c3_09-loadout-b.txt` with its nine `h9_*`–`h11_*` lines replaced,
and `c5_05-moon-missiles.txt` (47 watches) with `camera_b`, `camera_a`,
`camera_c` and the six `h4_*`/`h5_*` lines replaced, by:

```text
cockpit_target 0x49b088@0x210 4
order_record 0x4d03d4 22
hdd_map_pick 0x49b088@0x1f9@0x101@0x374 12
team1_order 0x4d24e6@0x45@0xc@4@0x23e 18
team2_order 0x4d24e6@0x45@0xc@8@0x23e 18
team3_order 0x4d24e6@0x45@0xc@12@0x23e 18
team1_damage 0x4d24e6@0x45@0xc@4@0x281 4
team2_damage 0x4d24e6@0x45@0xc@8@0x281 4
team3_damage 0x4d24e6@0x45@0xc@12@0x281 4
```

(`teamN_order`: state word `+23Eh`, point `+240h`, order target `+248h`,
guarded object `+24Ch`; `teamN_damage`: the accumulated damage `+281h`
that gates `mech_attacked` step 5.) For C5_05 add
`hdd_map_mode 0x49b088@0x1f9@0x101@0x5d 4` as the tenth line.

## Own building fire

**`c3_10-own-building-fire`** drives the `sweep` notification path of a hit
own building (426737, 4267cc, 4267ec).

- **Script** [c3_10-own-building-fire.jsonl](../../tools/reference-game/input-scripts/c3_10-own-building-fire.jsonl):
  the 194 entries of `c3_10-loadout-hercs.jsonl` up to frame 200 unchanged,
  so the run equals that golden up to 200: the player at (258500, 691103),
  heading 63177, full throttle, 7,956 from own building 20 (260708, 698747;
  bearing about 62600, 3° off; the recording rams it at 230–240). Then Up
  released at 201, Down 202–216, nine Space shots 205–271 (2 frames down,
  6 up).
- **Registry** launch `c3_10-loadout-hercs`, watches
  `watch-sets/c3_10-loadout-hercs.txt`.
- **Live:** golden and replaced check identical. C3_10's failure
  objective is type 1 on group 78 (members not identified); the shots do not
  end the mission inside the window.

## Mission outcome outside C1_01

**`c1_02-probe`** is a deterministic outcome suite outside C1_01: the
mission ends with status 5 (incomplete) at the second Q and DBSIM writes
`results.dat` and `mission.var`. It is not a success; C1_02 success stays
an open recording request (combat around group 129).

- **Mission data** (objective records, MSN list 17: marker, type, scope,
  subject, actions; read from the 62 `ZONES.VOL` MSN members,
  [MSN](../formats/mission.md#objective-serialization)). C1_02 has one
  required objective, type 2 on group 129 with action (variable 50, 6 =
  increment), and a failure objective type 1 on the same group with (50, 7).
  Type 2 for a group is `0x413950`: the group status test `0x413920` passes
  and every member that is not out passes `0x4137b4`, which walks the actor
  list for hostile actors that threaten the member (detection `0x411c58`,
  the group order test `0x423918`). The protected group is not "safe" at
  the start, so Q gives status 5, not 9, and the action slot
  (`objective_eval` 41352f…) does not run. The other candidates are no
  shorter: required type 0 (routes) needs a driven route, type 1 (destroy)
  and 3/4 (player flag `+A0h`) combat or a trigger.
- **Loadout** [c1_02-probe.json](../../shell/loadouts/c1_02-probe.json),
  launch set `c1_02-probe`: C1_02, Tomahawk (2) with the `c1_08-escort`
  player weapons, no team, GER, seed 0. `es2loadout.py --report`: no
  findings, `ui_rules` enforce, preparation equal to the original. Two bootstrap discoveries (actors
  0–38, then 39–64: all 26 on the player's side) give 14 hostiles; the
  watch set `watch-sets/c1_02-probe.txt` keeps 13 (index 14 left out by the
  48-watch limit).
- **Script** [c1_02-probe.jsonl](../../tools/reference-game/input-scripts/c1_02-probe.jsonl):
  no movement; Q at 1200 answered with Enter (pump keys 300/400, first
  button: the mission continues; the forced status is word `0x4a9e4c`,
  inside `mission_globals` of the watch sets); Q at 1500 answered with Tab,
  Enter (pump keys 300–410, second button: DBSIM writes `results.dat`/
  `mission.var` and exits for statuses 4/5/9/10).
- **Live:** golden and replaced check identical; with the hooks
  replaced `objective_eval`, `status_eval` and `actor_safe` are called, no
  bypass, no mismatch. The action sites 41352f/41353d/413540 stay open
  (41353a/413537 fall edges only; actions 4/5 are data-dead).

## Mouse and key suites

Three legacy C1_01 suites drive the camera, mouse and weapon keys. All
three are registered with goldens, identical with all hooks replaced.
Site lists are targets; a site counts as reached only when the suite's
edge-mode coverage lists it.

### `c1_01-free-camera`

- **Path (bytes).** V (`0x4603d9`) changes the view word `0x4d24ee`; the
  start view is 2 and V requests view 3 (orbit) (see [view
  words](#what-the-cheat-damage-does-read-from-the-bytes)). Ctrl+N (431h,
  `0x460214`, needs the cheat flag): the view subject becomes the next list
  actor, and `0x45df34` sets the camera-input word `0x4d24f0` to 1 for a
  subject other than the player (`0x45e1ed`); with it `0x460870` feeds the
  arrows to `0x401c74` (yaw `C+30h`, pitch `C+2Ch`, or speed `C+24h` while
  record byte `R+0Dh` is set). Ctrl+F (421h, `0x4602e1`) in view 3 clears
  camera `+36h` (mode 0, free flight) and sets view 1. Ctrl+T (414h,
  `0x460377`) would toggle `0x4d24f0` directly; not used.
- **Script** [c1_01-free-camera.jsonl](../../tools/reference-game/input-scripts/c1_01-free-camera.jsonl):
  V 100, Ctrl+N 150 (attached view on another subject: `camera_update`
  40194c), V 200 (orbit), Down 230–290 and Up 300–360 (orbit pitch into the
  terrain and back: 401789), Ctrl+F 380, Up 400–470 and Down 480–620 (pitch
  attempts), Left 630–680, Right 690–740, Space with Down 750–900 (speed
  attempt: which key sets `R+0Dh` on a keyboard is open).
- **Registry** legacy C1_01, watches `watch-sets/c1_01-mission.txt`,
  `game_arguments` `["-SPRUNKNOWN"]`. The suite has its own golden (the flag changes the
  Alt/Ctrl key path of `0x45a274`).
- **Known difference to the plan.** V 100 goes 2 → 3 and V 200 back to 2,
  so Ctrl+F at 380 lands in view 2 and only requests view 3 again: the
  mode-0 sites of `camera_update` (4012b0, 4012b9, 4012c6, 4012d6, 401374,
  4013d8) stay "never" in this suite. `c1_01-cheat-keys` presses Ctrl+F
  while the view is 3 (V 400, Ctrl+F 442).
- **Discovery watches** (replace any five): `camera_pos 0x4d24ea@0x4 12`,
  `camera_angles 0x4d24ea@0x10 6`, `camera_ctl 0x4d24ea@0x22 22`
  (distance, speed, orbit angles, deltas, target), `camera_mode
  0x4d24ea@0x36 2`, `view_words 0x4d24ee 4`.

### `c1_01-mouse`

- **Path (bytes).** The mouse part of the window procedure, `0x4809f8`,
  handles 200h (move), 201h/202h, 204h/205h (right down/up) and no
  double-click message, so a double click is timed by the game. `0x460870`:
  with record byte `R+0Bh` (the second mouse byte) set and the cursor
  inside the HUD rectangle `[[H+1F5h]+4..13h]` the drag starts (anchor
  `0x4d2a76`); while it is held the squared offsets become twist
  (`0x49f6e0`, `>>6`) and pitch (`0x49f6e4`, `>>5`).
- **Script** [c1_01-mouse.jsonl](../../tools/reference-game/input-scripts/c1_01-mouse.jsonl)
  (raw `msg` entries: 516 down with `MK_RBUTTON`, 512 moves with it, 517
  up): anchor (320,150) at 110, drag left to (260,150) 112–140, right to
  (380,150) 172–200, up (320,110) 204, down (320,190) 222, release 242;
  `\` held 250–280 (align after the twist), reverse throttle 290–302 and
  `\` again 305–335, Up 340–350; right double click at (320,150) 400–403;
  Q at 450 answered inside the alert by a left click at (331,348) (pump
  keys 300–340; where the c3_09 recording's last alert was answered) with
  a pump-keyed Enter at 2000/2100 as fallback (if the click misses, Enter
  takes the first button; if it hits, the Enter arrives in the next frame
  as an ordinary Enter = next target).
- **Registry** legacy C1_01, watches `watch-sets/c1_01-mission.txt`.
- **Targets:** `player_input` 460981, 4609bd, 4609e1, 460a26, 460a2e,
  460a36, 460a3e (drag) and 460bb3, 460bb9, 460bc8, 460bd0, 460bdf
  (align); `mouse_dispatch` 452ec6, 452f74.
- **Reached:** the drag starts (460981 both); the right double click
  (`mouse_dispatch` 452f74) is complete.
- **Open: the drag sign edges** (`player_input` 4609bd/4609e1 fall,
  460a26..460a3e taken): dx stayed ≤ 0 and dy ≥ 0, so the record cursor
  `R+2`/`R+6` apparently did not follow the injected moves while the right
  button was held. One discovery run of the script with `ctl_cursor
  0x4d22c8 8`, `drag_anchor 0x4d2a76 8` and `drag_out 0x49f6e0 8` added to
  `c1_01-mission.txt` shows which. If (320,150) lies outside the HUD
  rectangle, add `hud_rect 0x49b088@0x1f5@0x4 16` and move the anchor.
  The alert's mouse handling is not understood (a click at (500,377) had no
  effect in the cockpit-keys discovery).

### `c1_01-keys`

- **Script** [c1_01-keys.jsonl](../../tools/reference-game/input-scripts/c1_01-keys.jsonl)
  (key table `0x4a10ec`): 1 (100; weapon 1 selected through the HUD panel
  element), B 110/120/130 (fire mode +1 mod 3; the third press wraps:
  410fef), `-` 140, `=` 150, Num− 160, Num+ 170 (selected weapon's virtual
  `+38h`: 41104d, 41107a), Alt+1 180 and Alt+2 200 (202h/203h →
  `0x4110ac`, `group_toggle`), `[` ×6 from 220 and `]` ×12 from 260 every
  6 frames (both clamps of the balance word: `shield_balance` 413b07,
  413b1c, 413b27), Shift 340, Ctrl 350, Alt 360 alone, Scroll Lock 370
  alone and with Alt 380 (unmapped keys: `key_message` 477d33 both edges),
  `\` held 400–430 standing and 470–500 with reverse throttle (450–462), Up
  520–530.
- **Registry** legacy C1_01, watches `watch-sets/c1_01-mission.txt`.
- **Reached:** `hk_group_toggle_calls` 2 in the replaced run; `shield_balance`
  is complete.
- **Not reached:** `fire_group` 410b06 taken. `weapon_keys` increments
  `W+1Ch` before it calls `fire_group(W, W+1Ch)`, which returns at 410af8;
  only `weapon_frame` step 4 calls it with the HUD record's mode word (a
  click on the panel's mode selector), so B never reaches it and the site
  stays open.
- **Not reachable from the keyboard:** `control_record` 45a86c. An unmapped
  key (Shift, Ctrl, Alt themselves, Scroll Lock) returns at `0x477d29`
  before the queue ([input.md](../reverse-engineering/input.md), keyboard
  contract step 3; `ret 10h` at `0x477d43` right after the `GetKeyState`
  test), and every mapped code has a nonzero low byte, so the keyboard
  queues no word with a zero low byte. Which other source writes such a
  word (tape, hot-key callback) is open.

## Cheat damage suites

With the DBSIM argument `-SPRUNKNOWN` (it toggles `0x49ef38`; without it
the codes 620h and 633h do nothing) the cheat keys damage the view subject
part by part. Five registered suites use it on legacy C1_01; a sixth,
`c1_06-cheat-disable`, is under [Mission scenario
suites](#mission-scenario-suites). All of them carry the argument in their
registry entry ([Suites with DBSIM arguments](#suites-with-dbsim-arguments));
the outcome suites answer the alert as in [Answering a modal
alert](#answering-a-modal-alert).

| Suite | Drives |
|---|---|
| `c1_01-cheat-destroy` | the player's death by cheat damage on slot 0 (outcome suite) |
| `c1_01-cheat-disable-leg` | `group_assess` status 3 (player disabled, not destroyed; outcome suite) |
| `c1_01-cheat-disable-late` | a later disable (outcome suite); see [live_coverage.md](live_coverage.md#member_assess) |
| `c1_01-cheat-leader` | the weapons and legs of the enemy leader h3 |
| `c1_01-cheat-keys` | `vehicle_keys` with the flag, `camera_update` mode 0, `flyer_part` |
| `c1_01-cheat-keys-plain` | the same script without the flag (no `game_arguments`) |

### What the cheat damage does (read from the bytes)

- **Keys.** Alt+Ctrl+, / Alt+Ctrl+. (codes 633h/634h) move the slot word
  `0x4d2500` down/up within 0…29; it has no other writer and is 0 in the
  image, so a script counts from 0. Alt+Ctrl+D (620h, `0x46016e`) calls
  `[0x4d2684]`'s damage method `+74h` with (slot, 300, player). Ctrl+N /
  Ctrl+P (431h/419h, `0x460214`) set `0x4d251c` to the next/previous entry
  of list `0x4a9af8` (index word `+4Bh` of the current subject, modulo the
  count `0x4a9afe`; entries with z below −99,000 are skipped) and then either
  call the subject setter `0x45df34` at once (view word `0x4d24ee` ≠ 2) or,
  in view 2, request display transition 2 and view 3, which `0x45de30`
  applies with `0x4d251c` as subject. In the scripts Ctrl (11h) and Alt
  (12h) are held around the presses (the replay's virtual key state answers
  `GetKeyState`); `,` is VK BCh → 33h, D is VK 44h → 20h.
- **View words.** The mission init `0x45cafb..` stores **2** in both the
  view word `0x4d24ee` and the requested view `0x4d251a`; the mission load
  (`0x461a64`) sets `0x4d251c` to the player. `0x45de30` (every logic
  frame, `0x45f872`) applies a requested view only while a display
  transition has started (`[[0x4cf99c]+1Ch]`): view 2 → `0x45df34`(player,
  or `0x4d251c` when word `0x4d2534` is set), camera `+36h` = 2; view 3 →
  `0x45df34(0x4d251c)`, camera `+36h` = 1; view 4 → player, `+36h` = 3.
  View 1 has no request writer: only Ctrl+F in view 3 writes it directly
  (`0x460369`, camera `+36h` = 0), and nothing reverts it until the next
  transition. So V from the start view goes 2 → 3 (orbit), and **Ctrl+F
  must come while the view is 3**. Ctrl+F in view 1 sets `0x4d2534` and
  requests view 2, which then attaches the camera to `0x4d251c` (mode 2).
- **Flight keys.** The hot-key callback `0x45a324` consumes the 15 flight
  keys (table `0x49eb0b`: keypad/arrows, M, J, K, I, Num−, Num+, Space;
  Space and Enter pass on) and returns 1, except when the cheat flag is set
  and Alt or Ctrl is down (return 2, the code reaches `0x45feb8`). Without
  the flag, Alt/Ctrl+arrow therefore never reaches `vehicle_keys`; in
  display mode 1 (Esc) the capture is off (`0x42a9c0`), so
  `c1_01-cheat-keys-plain` presses them there.
- **Damage model** (vehicle `+74h` = `vehicle_part`, [collision.md](../formats/collision.md#dmg-damage-limits-and-weighted-internal-links)).
  The slot is the DMG region. `region_hit` `0x40da38` adds the damage to the
  region until its threshold; the press that crosses it passes the excess,
  and every later press all 300, to a randomly weighted live linked capacity
  (`0x40cf44`, RNG `0x4d2599`); a full capacity passes its excess on; when
  no linked capacity is left and region flag bit 0 is set, the region breaks
  (`region_break` `0x40d434`: state −1, part word `[A+20Eh]` ← 0, then the
  child regions). `vehicle_part` (`0x417de4`) then tests: **disabled**
  (`+A4h`) when at least half of capacities 0, 1 (and 10, 11 when DAT word
  `+48h` > 2) are full; **destroyed** (`+99h`) when region 0 or 1 is broken
  with all its capacities full, or capacity 8 or 9 is full
  ([player_movement.md](../reverse-engineering/player_movement.md), phases 4
  and 6 of `417de4`). Weapons add region 19+slot (slot = GL byte `+23`)
  with the `WEAPONS.DAT` threshold (`+0`), one link to capacity 12+slot and
  capacity tail `+8` (runtime `+2Ah`, read at `0x41770e`); a weapon whose
  region ratio (`0x40dbc0`) reaches 256 is ejected (`weapon_eject`, byte
  `W+49h`), and `+5Ch` of every weapon class then reports it unusable.
- **Status 3.** Status 3 is "player byte `+A4h` set, `+99h` clear". The
  logic frame then calls `group_assess([player+45h])` once (`0x45f9bd`)
  and opens the outcome alert with status 3 (or 12h if `+99h` became set);
  the alert result is (button = word `0x49f5c0[3]` = 0), so the first
  button (Enter) ends the mission.
- **Data used.** Outlaw (the C1_01 player, `player.mec` vehicle 0):
  capacities 0/1 = 1,000, 5–8 = 400, 9 = 50; region 0 (2,500) and region 1
  (2,500) both link capacity 8 (region 0 also 9), so slots 0 **and 1**
  destroy the Herc; region 7 (2,200, flag 1) links only capacity 0; region
  6 (2,500, flag 0) links 5 and 6. Mongoose (h3, below): legs 1,500, region
  7 2,200 → capacity 0; its GL slots 0 and 2 carry item 6 (`WEAPONS.DAT`
  threshold 2,500, capacity 500). SKIMMER (the only FLYERS type with model
  data): one region (threshold 300, flag 1) and one capacity (200). All 21
  vehicle DATs have word `+48h` = 2 except PITBULL (4) and SPIDER (0), and
  only SPIDER has `+4Ah` ≠ 0 ([dat.md](../formats/dat.md)). Hence
  `vehicle_part` 4180e5/418128 need a PITBULL, 418615/418651 a SPIDER —
  neither occurs in C1_01.
- **C1_01 list order.** The mission builds the MECH actors first
  (`0x4259e0` loop: authored records, then the MEC records), then the four
  flyers (`0x425a71`). The prepared script has ten
  MECH records (0–2 Diablo, gate trigger 0; 3/4 the Outlaw placeholders of
  group 0, replaced by the MEC roster; 5, 6, 9 Mongoose; 7, 8 Mirimac;
  groups 33–37 are one-member groups with order type 0) and four flyers
  (groups 29–32). This gives list 0–7 = hostiles h0–h7 (records 0, 1, 2,
  5–9), **8 = player, 9 = wingman**, 10–13 = flyers (10 and 12 active, 11
  and 13 wait for triggers 6 and 7), which matches the `c1_01-success`
  golden (h0–h2 on the start point, h5–h7 on one route, h10/h12 airborne,
  h11/h13 at z 5,000, h5's target at frame 4461 is the actor 372h bytes
  after the player, presumably the wingman). h3 = record 5, the Mongoose
  leader of group 33.

### `c1_01-cheat-destroy`

- **Script** [c1_01-cheat-destroy.jsonl](../../tools/reference-game/input-scripts/c1_01-cheat-destroy.jsonl):
  Ctrl and Alt down at 100; Alt+Ctrl+, thirty times at 102–161 (slot word
  0 whatever its start value); Alt+Ctrl+D sixty times every four frames at
  170–407 (300 each on the player, slot 0); Alt/Ctrl up at 410; no
  movement; the outcome alert answered with the pump-keyed Enter at 430.
- **Live:** the damage
  **destroys** the Herc: player byte `+99h` is set at frame 222 (after 14
  presses), `+A4h` stays clear, so `group_assess` is not called and the
  path is the player death (`death_sequence`, `group_death`,
  `death_actions`). The run stalls in the outcome alert at F = 430 without
  the answer; with it two original runs and the replaced run are identical
  and `results.dat`/`mission.var` byte-equal. The
  golden's manifest records `game_arguments` `-SPRUNKNOWN`; the replaced check
  is identical.

### `c1_01-cheat-disable-leg`

- **Script** [c1_01-cheat-disable-leg.jsonl](../../tools/reference-game/input-scripts/c1_01-cheat-disable-leg.jsonl):
  Ctrl+Alt down 100–213; Alt+Ctrl+. ×6 (102–122, slot 6), Alt+Ctrl+D ×10
  (126–162; region 6 crosses at the 9th press, then 200 and 300 to capacity
  5 or 6), Alt+Ctrl+. (166, slot 7), Alt+Ctrl+D ×11 (170–210). Region 7
  crosses at the 8th press (excess 200 to capacity 0), capacity 0 reaches
  1,000 at the 11th (frame 210): `+A4h` set, `+99h` clear (no press touches
  region 0/1 or capacities 8/9), region 7 breaks with its children 9 and 11.
  The player stands still. The alert opens at F = 315 (`outcome_poll`
  reports a new status only after its evaluation countdown `4a9e63` and a
  grace countdown, [mission_runtime.md](../reverse-engineering/mission_runtime.md));
  the script answers it with the pump-keyed Enter at 315.
- **Live:** the player is disabled at
  210; `group_assess` is called once (status 3), `vehicle_part` 23,
  `region_break` 3, `death_actions` 1 in the replaced run.
- **One chance in four:** slot 6 puts 200 then 300 on capacity 5 or 6 by a
  50/50 draw each; only "6, then 5" gives capacity 5 = 300 (ratio 192), the
  `(128,192]` branch with the player (`vehicle_part` 41878c fall; 4187a4 is
  PROVED). Otherwise the `> 192` branch (`+ABh`) is taken. A variant would
  shift every entry from frame 126 by +1…+3 frames (other RNG positions).
- **Discovery watches** (scratch copy of `c1_01-mission.txt`):
  `player_flags2 0x4d24e6@0xa6 6`, `player_caps 0x4d24e6@0x206@0 44`,
  `player_regions 0x4d24e6@0x20a@0 58`, `cheat_slot 0x4d2500 2`.

### `c1_01-cheat-leader`

- **Why weapons and not the legs first.** `group_scan`'s out path
  (41d6a9…41d732) needs the scanning leader to be out while its script is
  still `49968c`. Disabling (`+A4h`) and destroying switch the script to
  `0x4998ba`/`0x49987c` (`dead_step`) inside `vehicle_part`, so they never
  reach it. The only other out byte for a MECH is `+A5h`, written only by
  `weapon_choose` (41f56d) when no weapon passes `+5Ch`. So the script
  shoots off both weapons of h3 before it engages, lets it attack the
  player (aim_fire → `weapon_choose`: nothing ready, `+A5h` = 1), and
  relies on the AI to come back to its order script (hypothesis: retreat
  `499800`, then `attack_plan`, then `order_resume`).
- **Script** [c1_01-cheat-leader.jsonl](../../tools/reference-game/input-scripts/c1_01-cheat-leader.jsonl):
  Up 100–120 (as `c1_01-success`); V 130 (view 3, subject = player);
  Ctrl+P ×5 at 142–182 (8 → 7 → 6 → 5 → 4 → 3 = h3; view 3, so each press
  is applied at once); Ctrl+Alt 190–356: Alt+Ctrl+. ×19 (192–264, slot 19),
  Alt+Ctrl+D ×10 (268–304: region 19 crosses at the 9th, capacity 12 full
  at the 10th → weapon 0 ejected), Alt+Ctrl+. ×2 (slot 21), Alt+Ctrl+D ×10
  (316–352, weapon 2 ejected); V 360 (view 2, subject player, cockpit
  controls back); brake 546–561 (as `c1_01-success`, no fire, no T); `'`
  700 (`cockpit_nearest`: h3 is the closest hostile there in the golden,
  about 45,000); Alt+A 710 (MFD item 0, group order code 0: the wingman
  attacks the cockpit target); V 1400 (view 3; `0x45de30` makes `0x4d251c`
  = h3 the subject again); Ctrl+Alt 1410–1520: Alt+Ctrl+, ×14 (slot 7),
  Alt+Ctrl+D ×13 (1468–1516: region 7 crosses at the 8th, capacity 0
  reaches 1,500 at the 13th → h3 disabled); V 1530. Watches
  `watch-sets/c1_01-mission-hostiles.txt`.
- **Live** (replaced run): `weapon_eject` 2, `vehicle_part`
  37, `region_break` 6, `cockpit_nearest` 1, `mech_order` 1,
  `weapon_choose` 200, `attack_plan` 174, `group_scan` 436.
- **Targets:** `weapon_choose` 41f559 (the nothing-ready edge), 41f565
  both, 41f579 and 41f58b one edge each; `group_scan` 41d6a9 taken, 41d6db
  fall, 41d6f2, 41d6fb, 41d712, 41d732 (hypothesis above); `attack_plan`
  41d0eb, 41d0f4, 41d10b, 41d131 (the wingman's order target out,
  hypothesis). Whether h3 returns to `49968c` with `+A5h` is AI timing; if
  the 41d6xx sites stay never, they are a recording request.
- **Discovery watches** (scratch copy of `c1_01-mission-hostiles.txt`,
  replace the nine `h11`–`h13` lines): `h3_flags2 0x4a9af8@12@0xa6 6`,
  `h3_caps 0x4a9af8@12@0x206@0 44`, `h3_script 0x4a9af8@12@0x4d 8`,
  `w_target 0x4a9af8@36@0x1a4 4`, `w_flags 0x4a9af8@36@0x99 13`,
  `w_order 0x4a9af8@36@0x23e 18`, `cockpit_target 0x49b088@0x210 4`,
  `view_subject 0x4d2684 4`, `cheat_slot 0x4d2500 2`.

### `c1_01-cheat-keys` and `c1_01-cheat-keys-plain`

- **Script** [c1_01-cheat-keys.jsonl](../../tools/reference-game/input-scripts/c1_01-cheat-keys.jsonl)
  (one file, two registry entries; watches
  `watch-sets/c1_01-mission-hostiles.txt`, h10/h12 flags show `+99h`):
  100–200 vehicle keys on the player in the cockpit: Alt+Ctrl+1 (602h,
  steps 500; both step words are 0 at start), Alt+Up/Down/Left/Right
  (248h/250h/24Bh/24Dh), Ctrl+Left/Right (44Bh/44Dh, they clear the
  pose-cache word) each followed four frames later by an Alt move,
  Alt+Ctrl+9 and Alt+Ctrl+1 again; throttle 220–240, Alt moves while
  driving 260–278, throttle back 290–320; Esc 330 (display mode 1,
  flight-key capture off), Alt+arrows 342–354, Ctrl+Left/Right 360/364,
  Alt+Ctrl+1 370, Esc 380; V 400 (view 3); Ctrl+N 422, 432 (9, then 10 =
  flyer h10); Ctrl+F 442 (view 1, camera mode 0); Up 450–570, Down 580–780
  (both pitch clamps), Left 790–820, Right 830–860; Ctrl+F 870 (view 2
  attached to `0x4d251c` = h10); Alt+Ctrl+D 1020, 1024 (h10: the first
  press reaches the region threshold 300, the second fills capacity 0 →
  destroyed); Ctrl+N 1062 (view 2: request, entry 11), 1072 (view 3, entry
  12 = h12); Alt+Ctrl+D 1112, 1116 (h12 destroyed).
- **Live** (`c1_01-cheat-keys` replaced run): `flyer_part` 4,
  `death_actions` 2, `region_break` 2.
- **Targets, with the flag:** `vehicle_keys` 41583d, 415848, 415851,
  41585f taken; 415889, 4158bc, 415933, 4159a7, 415a1b, 415a8f, 415ab2 fall;
  4158ca, 415941, 4159b5, 415a29 at least one edge (pose cache).
  `camera_update` 401233 taken (mode 0), 4012b0, 4012b9, 4012c6, 4012d6
  both edges, 4012e6 fall (PROVED one-way), 401374 and 4013d8 at least one
  edge (the camera does not move: speed `+24h` stays 0), 4017f7 taken and
  40194c both (FLYER `+24h` = `0x411a9c` returns 0). `flyer_part` 421be6
  both, 421bf2, 421bfa, 421c03 fall, 421c0b taken, 421c4f and 421c6e one
  edge each.
- **Targets, without the flag** (`c1_01-cheat-keys-plain`): `vehicle_keys`
  41585f and 415889 taken (Alt+Ctrl+1/9: digits are not flight keys), and
  41583d, 415848, 415851 plus 4158bc, 415933, 4159a7, 415a1b, 415a8f,
  415ab2 taken from the display-mode-1 block (hypothesis: the HDD page does
  not consume Alt/Ctrl+arrows); the `command_key` cheat cases take their
  flag-clear edges; no damage, the view stays 3 after V.

The four suites `-leg`, `-leader`, `-keys`, `-keys-plain` reach sites in
`command_key`, `logic_frame`, `flyer_part` and `member_assess`.

### Not reachable with the cheat

- `flyer_part` 421bf2 taken needs a slot ≠ 0 on a flyer, but SKIMMER has
  one region (slot ≥ 1 reads past the region array); 421bfa/421c03 taken
  need another damage source than the player (cheat damage always passes
  the player).
- `flight_model` 4670d7/467113: the AI flyer step passes four zero failure
  flags ([flyer.md](../reverse-engineering/flyer.md)), so only the player's
  Razor reaches them (recipe below).
- `mech_attacked` step 8 (41f931…41f99a): `mech_attacked` is the `+50h`
  notice of the sweep `0x426528` (real hits only); cheat damage calls `+74h`
  directly and never reaches it. Step 8 needs script flag byte
  `[A+4Dh]+0Ah` (41f914); a member of the player's group also returns below
  8,000 accumulated hit damage (41f87a). 41f918 has been seen only taken
  (flag clear); this needs hunters in C3_10/C4_08 or a recording.
- `engage_done` 41c516/41c51e: the branch runs only for an AI of a side-0
  group that is **not** in order state 4 with an order target (code 0 and
  0Bh orders set state 4, so step 1 of `engage_done` is taken) and whose
  group's current mission order names its target (`order_target`: scope 0
  group or scope 1–3 actor). In C1_01 the only side-0 AI group is the
  player's (order 8: type 3, scope −1), so the branch is unreachable there;
  it needs an allied group with a mission attack order, where the cheat can
  then disable or destroy that target.
- **`flight_model` recipe (not scripted, C4_08):** launch
  `c4_08-razor-plasma` with `-SPRUNKNOWN`; while the Razor flies, Alt+Ctrl+.
  ×7 (slot 7) and Alt+Ctrl+D ×10 (RAZOR region 7: threshold 3,000, flag 1,
  capacity 0 of size 0, so the 10th press breaks it and part word 7 → F+0:
  467113 both edges with the roll), then Alt+Ctrl+. (slot 8) and ×10 again
  (part word 8 → F+4: 4670d7). Regions 7/8 touch neither region 0/1 nor
  capacities 8/9, and the ground disable test is skipped for a flyer
  chassis (DAT `+4Eh` = 1).

## Dialog, weapon and flight suites

Five suites for the key outcomes `c1_01-keys` and `c1_01-mouse` do not
reach, the options dialog, the weapon keys without a selected weapon, the TARG/EO path and the
Razor's terrain contact. Live: goldens and checks
identical with all hooks original and all replaced,
every `*_skipped`/`*_bypassed` counter 0. Site lists are targets; a site
counts as reached when the suite's edge-mode coverage lists its outcome.

### Key outcomes left by `c1_01-keys` and `c1_01-mouse`

Outcomes never seen before these suites: `weapon_keys` 410fd0 taken
(Alt+W), 41104d and 41107a taken (`-`/`=`/Num± with no selected weapon);
`fire_group` 410b06 taken; `group_toggle` 4110c6 and 4110db taken, 411105
fall; `select_step` 41075d taken, 410773 fall; `player_input` 4609bd/4609e1
fall and 460a26..460a3e taken (drag), 460bb3 and 460bb9 taken, 460bc8 and
460bd0 fall (align); `key_message` 477c4f fall, 477c81/477c89 taken, 477c91
fall, 477cf8 taken; `hud_key` 432edb (never); `command_key` 45fff2/45fffd
taken (F12/Alt+P), 4603a5..4603b9 (Alt+S), 4607aa (P), 46071e/460745
(Ctrl+Q), 4607e7.. (Ctrl+1) and the flag tests of the cheat cases (46021b,
4602e8, 46037e, 460175, 4600c6, 4601a9, 4601d0 taken).

### Facts from the bytes

Sources: key table `0x4a10ec`, jump tables `0x45ff50`, `0x456eab`,
`0x40fb89`, vtables `0x49e3e4`, `0x49e1e4`, `0x498c64`, `0x498b74`, a
listing of `VER95/VSHELL.EXE` for the hangar missile-type buttons, and the
`PROJ.DAT`/`WEAPONS.DAT` members of `VOL/SIMVOL0.VOL`
([DAT](../formats/dat.md#simulator-weapons-datweaponsdat)).

- **The options dialog `0x45cff0`.** Allocates a 3C7h-byte dialog
  (`0x4566e0`, vtable `0x49e3e4`: run `+8` = `0x456d68`, button callback
  `+0Ch` = `0x4573c4`, key handler `+10h` = the base `0x454e2c`), saves the
  camera `[0x4d24ea]` fields +4..+4Dh and the freeze word `0x4d24f2`, sets
  the freeze word to 1 and word `0x4d267e` to 0 (not restored), shrinks the
  view (`0x42d89c`). With display mode `[[0x4d24c0]+14h]` ≠ 4 (cockpit) it
  requests transition 2 (`0x42a3f4(D, 2)`: the full-screen outside view;
  camera mode `+36h` ← 1) and loops presentation `0x45fba4`,
  `camera_update`, `render_root` until the transition ends (the slides run
  without the clock, display_modes.md). The run loop `0x456d68` calls
  `0x45a810` (control record) and the key handler once per iteration, then
  the idle callback `0x45cfc8` (`0x401c74(camera, −40h, 0, 0)`,
  `camera_update`, `render_root`). It ends only through button action 10
  (`0x457095`: `0x4574e8`, then `+2D1h` ← 3); actions 0–8 step prefs bytes
  0, 1, 2, 3, 7–11 (`0x45999c`/`0x4599d4`/`0x459a10`), action 9 opens a
  second dialog (`0x457d38`). Esc (code 1) presses the cancel button
  `+2FBh` = the last of the 11 buttons (index 10, `0x456ca7`), whose state
  word is 0 (`0x456aa3`); Enter would press the focused button 0 (action 0,
  a sound toggle). Afterwards the camera fields come back, the view is reset
  (`0x42d944`), and the layout is switched back directly (`0x42a1f0` twice,
  `render_root` twice, presentation once). Keys: F12 (VK 7Bh → 58h) and
  Alt+P (219h), both reaching `0x4607d7` (sites 45fff2, 45fffd); `hud_key`
  passes both to its default chain (MFD index 7 of `0x446c65` is 0).
- **Side effect: `data\prefs.cfg`.** Action 10 calls `0x459b94(9,
  0x49e2dc)`: read the 54-byte prefs file, overlay bytes 0, 1, 2, 3, 7, 8,
  9, 10, 11 from the block `0x4d1f38`, write it back
  ([cfg.md](../formats/cfg.md#prefscfg)). Unchanged in-memory bytes give the
  same file content, but bytes 2/3 are zeroed in memory when
  `vol\simvoic<lang>.vol` is absent from the install (`0x45a22a`), so on
  such a clone the dialog would change the file. `prefs.cfg` is not in the
  runner's restore set (`SuiteLaunch.psm1`); hash it before and after a
  dialog run.
- **Why the dialog replays deterministically.** The number of dialog
  iterations follows the pump count until the pump-keyed Esc (as in every
  modal suite), and each iteration runs `render_root` once more. At frames
  300 and 500 of a standing C1_01 run nothing that the root's simulation
  callees step per call is moving: no cockpit target (the `cockpit_sync`
  countdown `P+31Ch` is already 0 and clamps), no jolt pending
  (`panel_frame` draws stream B only while one runs), weapons idle, shield
  balance at rest (hypothesis; the verification replays agree).
- **Pause and quit dialogs** (`0x4561dc`, vtable `0x49e1e4`, generic loop
  `0x456000`): cancel `+2FBh` = 0 for both (`0x4566bc`), button 0 sets
  `+2D1h` ← 2 (`0x4561a0`), the run returns 2 & 1 = 0, so Esc continues the
  mission for P (`0x460759`) and for Ctrl+Q (`0x4606cd`: result 0, word
  `0x4d24fe` stays 0). Ctrl and Alt must be released before the Esc.
- **Weapon selection −1.** The weapon-system constructor sets `W+1Dh` = −1
  and selects the first weapon whose class `C+56h` is not 12h or 1Dh..20h
  (`0x410567..0x4105de`): an Outlaw that carries only an ECM keeps −1.
- **Equipment objects.** `0x40fb2c` files each equipment object of the
  vehicle by class: 18 ECM → `+307h`, 29 TARG → `+30Bh`, 30 SHIELD →
  `+30Fh`, 32 ENERGY → `+313h`, 31 TURBO → `+317h` (byte table `0x40fb7a`,
  targets `0x40fb89`). The player's ECM state word `D+7Dh` (and `+7Fh`) is
  written only by the ECM's virtual `+50h` (`0x40f184`) from byte 0 of its
  cockpit gadget's record (`[D+79h]`, made by `0x4321d4(H, hardpoint byte
  D+7, D)`); the digit keys press the panel elements (`hud_key` codes
  2..0Bh → `[H+70h+4(c−2)]` → `button_press`), so the ECM is switched with
  the digit of its hardpoint group (watch `player_flags` byte 8 = `P+A1h`).
- **The steerable missile.** Racks 13–16 have projectile selector 34
  (`WEAPONS.DAT` tail +1Ch), which `0x4100a0..0x4100c6` resolves as `PROJ`
  (class 0, the slot's secondary word; 5 → 0). `PROJ.DAT` has class-0
  records for indices 0–4 (records 10–13 and 18). Secondary 3 selects record
  13, whose index 3 the rocket constructor stores in projectile byte `+41h`
  (`0x40a988`); `projectile_flight` calls `missile_steer` (`0x40a63c`) for
  type 3 when the owner has byte `+A3h` (the player). The hangar writes the
  secondary of a mounted rack with its four missile-type buttons
  (`0x43e346`: types 2, 1, 0, 3 for `0x440626`, `0x44068b`, `0x4406f0`,
  `0x440755`), so type 3 is a retail choice; the plain mount gives 1. The
  guidance byte `R+0Dh` of the control record is, without a joystick button
  mapping, the Space flag `B+10h` of the keyboard block (`[0x4d2360]`), so
  holding Space after the launch keeps the missile steered (site 40a49b both
  ways) and the arrows bend it.
- **Live elsewhere.** `turbo_engage` is entered in `c3_09-wingman-orders`
  (`point_move` 41fb34) and `ally_nearest` in `c5_05-moon-missiles`
  (`sweep` 4267cc, the test right after the call at 4267bd);
  `member_assess` stays behind `group_assess` status 3
  (`c1_01-cheat-disable-leg`).

### `c1_01-options-dialog`

- **Script** [c1_01-options-dialog.jsonl](../../tools/reference-game/input-scripts/c1_01-options-dialog.jsonl):
  no movement; F12 down at 300 with the pump-keyed Esc (p 300/310) in the
  same frame, F12 up 301; Alt down 499, P down 500, in frame 500 Alt up
  (p 290) and Esc (p 300/310), P up 501.
- **Registry** legacy C1_01, watches `watch-sets/c1_01-mission.txt`.
- **Live.** The watches cannot show the dialog (it opens and closes inside
  one frame); the replaced run's counters do: `render_root`/`cockpit_sync`
  3,576 against `command_key` 2,975 calls, against 2,588/2,587 in
  `c1_01-keys-extra`. `prefs.cfg` of the clone is unchanged by the two
  dialogs. With `logic_frame` and `cockpit_sync` replaced, the nested root
  could overwrite the logic frame's host frames (main_loop.md); the replaced
  check is identical.
- **Targets:** `command_key` 45fff2 taken, 45fffd taken; `camera_update`
  with camera mode `+36h` = 1 during the dialog (which of its never sites
  that adds is open).

### `c1_01-keys-extra`

- **Script** [c1_01-keys-extra.jsonl](../../tools/reference-game/input-scripts/c1_01-keys-extra.jsonl)
  (modifier combos: modifiers down at f, key f+1..f+2, modifiers up from
  f+3): Tab 100; Alt+W 110; W 120; Alt+9 130; Shift+B 150; Ctrl+1 170;
  Alt+S 190; Ctrl+T 210, Ctrl+P 230, Ctrl+F 250, Ctrl+N 270; Alt+Ctrl+D
  290, Alt+Ctrl+N 310, Alt+Ctrl+, 330, Alt+Ctrl+. 350 (none of these keys is
  a flight key, so they reach `command_key`); J held 400–440 (torso twist,
  key-state slot 9), `\` 445; K held 520–580, `\` 585; P 700 with Esc
  (p 300/310); Alt+Tab 760, Ctrl+Esc 780, Alt+Esc 800; Ctrl 850, Q 851 with
  Ctrl up (p 290) and Esc (p 300/310) in frame 851, Q up 853.
- **Registry** legacy C1_01, watches `watch-sets/c1_01-mission.txt`.
- **Targets:** `hud_key` 432edb taken (no TARG); `weapon_keys` 410fd0
  taken; `select_step` 410773 fall (Alt+W from selection 0 wraps);
  `group_toggle` 411105 fall (no weapon in group 8); `key_message` 477c4f
  fall, 477c81 taken, 477c89 taken, 477c91 fall; `command_key` 4607e7 taken
  (no throttle lever), 4603a5 fall, 4603af fall, 4603b9 taken, 46037e,
  46021b, 4602e8, 460175, 4600c6, 4601a9, 4601d0 taken, 4607aa fall,
  46071e fall, 460745 taken; `player_input` 460bb3 and 460bb9 taken (U ≥ 30
  needs a twist of about 900 units at the align start), 460bc8 and 460bd0
  fall after one of the two twists (sign hypothesis).
- **Limits.** The focus keys call `0x4669b4`, which switches the display
  mode when `0x4d255e` is set (full screen); the oracle runs windowed
  (oracle_regression.md: the toggle `0x4667c8` is "not reached in the
  oracle"). Shift is stripped by the key mask `0x49eab8` before
  `command_key` (input.md), so Shift+B acts as B (a fire-mode step).

### `c1_01-unarmed-keys`

- **Loadout** [c1_01-unarmed.json](../../shell/loadouts/c1_01-unarmed.json),
  launch set `c1_01-unarmed`: C1_01, Outlaw (0) with only ECM 18 on slot 2,
  GER, seed 0. `--report`: no findings, equal.
- **Script** [c1_01-unarmed-keys.jsonl](../../tools/reference-game/input-scripts/c1_01-unarmed-keys.jsonl):
  `-` 100, `=` 110, Num− 120, Num+ 130, W 140, Alt+W 150, Alt+1 170, Alt+3
  190, Space 210 and 220 (two frames each), B 240, digits 3 (300), 1 (360),
  2 (420), 3 (480).
- **Registry** launch `c1_01-unarmed`, watches
  `watch-sets/c1_01-mission.txt` (mission globals and player fields only,
  valid for any C1_01 launch).
- **Live:** the ECM switches on with digit 1 (frame 360).
- **Targets:** `weapon_keys` 41104d and 41107a taken; `select_step` 41075d
  taken; `ready_step` 410a4d taken (every frame); `fire_selected` 410dd4
  taken (Space); `group_toggle` 4110c6 taken (null slots 0/1), 4110db taken
  (the ECM's range word 0) when the Alt digit matches its group, 411105
  fall; `weapon_frame` 410c0b taken (null slots, hypothesis). The ECM digit
  sets player byte `+A1h` (`player_flags` byte 8) through `device_state`.

### `c3_10-hunt-targ`

- **Loadout** [c3_10-targ-eo.json](../../shell/loadouts/c3_10-targ-eo.json),
  launch set `c3_10-targ-eo`: `c3_10-loadout-hercs` with the player
  Tomahawk carrying ECM 18 (slot 0), TARG 29 (slot 1), LAS200 9 (slot 2),
  rack 13 with secondary 3 (slot 3, the EO type) and PLAS 25 (slot 4); team
  unchanged. `--report`: no findings, equal; script.dat, MISSION.STR and
  mission.var equal to the sealed `c3_10-loadout-hercs`. The initial selection
  is slot 2 (ECM and TARG are never auto-selected), so W selects the rack.
- **Golden facts used (`c3_10-loadout-hercs`).** The player starts at
  (257294, 681538), heading 0 (north; heading h points to (−sin h, cos h),
  calibrated on `c3_10-own-building-fire` and `c4_08-razor-plasma`). The
  hostile group h3–h6 comes from (463682, 742142) westward at about 90 per
  frame along y ≈ 740,000 and targets another object at 900. The Tomahawk
  turns about 400 per frame with an arrow held and does about 123 per frame
  at full throttle.
- **Script** [c3_10-hunt-targ.jsonl](../../tools/reference-game/input-scripts/c3_10-hunt-targ.jsonl):
  Up 100–122, Right 130–160; `'` 870 (nearest valid target inside 60,000:
  the player's target raises its attacker count `T+1A2h`); Alt+F 880 (code
  5, the wingmen take the order and hunt with F = 1); Down 925–941 (throttle
  back); Tab 950, 960 (`targ_next` with the target); Alt+A 980 (code 0:
  state 4 with an order target); `1` 1000 (the ECM digit); W 1010 (rack);
  `'` 1050, Tab 1060, `'` 1080; Space held 1090–1120 with Left 1095–1105
  (EO missile launch and steering); Tab 1150, `'` 1200, Tab 1250, Alt+H
  1300 (code 2), `'` 1400, Tab 1410, Space 1420–1440, Alt+F 1600, `'` 1800,
  Tab 1810.
- **Registry** launch `c3_10-targ-eo`, watches
  `watch-sets/c3_10-loadout-hercs.txt`.
- **Live:** the player picks a target at 870, cycles TARG with Tab from 950
  and launches two EO missiles (`proj_view` 1091–1121 and 1420–1441); the
  actual heading is 57,610. First live calls in any suite: `targ_next` 7,
  `targ_aim` 104, `missile_steer` 50.
- **Targets:** `aim_point` 41b73c fall, 41b759 both ways, `targ_aim` lock
  and fallback paths; `hud_key` 432edb fall; `projectile_flight` 40a634 fall,
  `missile_steer` 40a49b both ways; `vehicle_update` 41abc4 fall (ECM word
  with a target: `T+A2h` ← 1) and `contact_scan` 412a99/412aca (`A+A2h`
  set); `target_search` 41e1ce fall, 41e1e6 fall, 41e1ec both (a candidate
  with attackers); `hunt` 41e2db fall (state 4).
- **Not targeted:** `vehicle_update` 41acae/41acbd (an attacker with TARG
  rolling against a target with an active ECM: an AI ECM is "active"
  through the fire byte `A+96h`, and which hostile carries one is not
  known); `fire_group` 410b06.

### `c4_08-slope-dive`

- **Script** [c4_08-slope-dive.jsonl](../../tools/reference-game/input-scripts/c4_08-slope-dive.jsonl):
  the 866 entries of `c4_08-razor-plasma.jsonl` with f ≤ 1099 unchanged (no
  key is held at 1099), so the run equals that golden up to frame 1099; then
  Up held 1110–1200 with Left 1110–1160, Right 1200–1260, Up 1260–1400; the
  outcome alert at 1319 is answered with the pump-keyed Enter. For the Razor
  the vertical arrows pitch (recording: Down 870–910 climbs from 40,440 to
  47,219, Up 919–951 descends to 44,337 by 1050) and Num+ sets the throttle
  (324 from 757).
- **Registry** launch `c4_08-razor-plasma`, watches
  `watch-sets/c4_08-razor-plasma.txt`.
- **Live:** the Razor crashes at about frame 1300 and the outcome alert
  opens at 1319; `mech_fly` is called 1,319 times.
- **Targets** (`mech_fly`): a wing probe below the terrain or in contact
  while banked low: 419c6f taken / 419c7e fall, then 419c87 and 419cf4 both
  ways (part 8), the same for part 7 (419d81, 419d90, 419d99, 419e06); the
  body below the terrain: 41a1fd fall (ground clamp, damage packet for part
  6), 41a253 both ways (part 6 region word +0Ch zero: the crash). The
  `c4_08-razor-plasma` golden's own end at 2050 (z 75,618 → 19,503, then
  frozen) never took 41a1fd, so it was not a terrain contact.

### Not covered

- **S-HUNT by hostile hunters** (`hunt` 41e3c6 taken: a hunter outside the
  player's group finds a target). In `c4_08-razor-plasma` (12
  `target_search` calls) and `c1_08-escort-route` (30) the searches find
  candidates, but all lie beyond R = 90,000 from the hunter's destination
  (41e1c4 taken only). Which actors hunt and where their destinations are is
  not readable from the watch sets; a discovery with the script state
  `A+4Dh` and the order pointer of the candidate actors comes first.
- **Drag sign edges** of `c1_01-mouse` ([Mouse and key suites](#c1_01-mouse)).
- **Gadget drag in `mouse_dispatch`** (452e07 fall, 452e11, 452fc1 fall,
  452fcb): needs a draggable screen gadget under the cursor; its rectangle
  is not known statically.
- **`key_message` 477cf8 taken** (keyboard ring full): needs 255 unread key
  messages; the ring is drained every frame.
- **`fire_group` 410b06 taken** ([`c1_01-keys`](#c1_01-keys)) and
  `vehicle_update` 41acae/41acbd.

### Discovery watches

One run each, not for the golden. `c1_01-dialog-discovery.txt` =
`c1_01-mission.txt` plus:

```text
prefs_block 0x4d1f38 54
freeze_word 0x4d24f2 2
display_mode 0x4d24c0@0x14 4
camera_a 0x4d24ea@0 22
word_4d267e 0x4d267e 2
panel_off 0x49b088@0x20f 1
player_target 0x4d24e6@0x1a4 4
```

`c1_01-unarmed-discovery.txt` = `c1_01-mission.txt` plus
`ecm_state 0x4d24e6@0x307@0x7d 4` and `weapon_sel 0x4d24e6@0x202@0x1c 2`
(fire mode, selection). `c3_10-targ-discovery.txt` =
`c3_10-loadout-hercs.txt` with its six `h7_*`/`h8_*` lines replaced by:

```text
targ_state 0x4d24e6@0x30b@0x7d 6
ecm_state 0x4d24e6@0x307@0x7d 4
player_target 0x4d24e6@0x1a4 4
weapon_sel 0x4d24e6@0x202@0x1c 2
proj_view 0x4d2526 4
team1_order 0x4d24e6@0x45@0xc@4@0x23e 18
```

(`targ_state`: part `+7Dh`, damage `+7Fh`, timer `+81h`; `proj_view`: the
projectile-view word `0x4d2526` and the word `0x4d2528`.)

### Other registered key and dialog suites

`c1_01-display-keys` (HDD page keys, the F11 objectives screen, display
modes), `c3_10-detail-options` (the options dialog twice, detail byte
`0x4d1f43`) and `c1_01-quit-dialog` (Ctrl+Q answered with Tab, Enter) are
described in the suite table of [oracle_regression.md](oracle_regression.md#suites).

## Mission scenario suites

Six suites in missions other than C1_01 drive the trigger, timer, escort and
objective paths (`trigger_scan`, `trigger_fire`, `mission_timer`,
`player_route`, `objective_eval`) and the objective predicates
`group_marked`/`group_flagged`
([mission_runtime.md](../reverse-engineering/mission_runtime.md#objective-predicates)).
Live: goldens and checks
identical with all hooks original and all replaced, every
`*_skipped`/`*_bypassed` counter 0.

| Suite | Launch | DBSIM arguments | Targets |
|---|---|---|---|
| `train1-timers` | `train1` (es2prep, TRAIN1) | — | `trigger_fire` 423455, 42345f fall, 423489 both; `mission_timer` 4230b8 fall; `player_route` 41c29f fall, 41c2a5 taken |
| `c4_05-convoy` | `c4_05-convoy` | — | `trigger_scan` 426c13, 426c39, 426c4b, 426c5d both, 426c25 fall; `follow_step` (type-6 order of group 1) |
| `c1_08-scan` | `c1_08-escort` | — | `player_route` 41c2b2 taken, 41c301 fall, 41c343, 41c35b, 41c38e; `objective_eval` 413529, 41352f, 413537, 41353a fall, 41353d taken |
| `c4_10-probe` | `c4_10-probe` | — | `group_flagged` 412c72/412c81; `objective_eval` 413430, 413438 fall |
| `c1_06-cheat-disable` | `c1_06-maverick` | `-SPRUNKNOWN` | `objective_eval` 413430 both, 413438 fall, 413503 fall, 41353d fall, 413540 taken, 4135ad fall, 4135b6 one edge |
| `c2_02-identify` | `c2_02-identify` | — | `group_marked` 412daa/412dbb; `objective_eval` action sites as `c1_08-scan` |

Each watch set is `watch-sets/<launch>.txt`. The outcome suites end at the
answered Q; `ticks` is a bound.

Loadouts: [c4_05-convoy.json](../../shell/loadouts/c4_05-convoy.json),
[c4_10-probe.json](../../shell/loadouts/c4_10-probe.json),
[c1_06-maverick.json](../../shell/loadouts/c1_06-maverick.json),
[c2_02-identify.json](../../shell/loadouts/c2_02-identify.json): player
Tomahawk (2) with the `c1_08-escort` weapons 18, 22, 9, 31, 25, no team,
GER, seed 0. `es2loadout.py --report`: no findings, preparation equal to
the original, for all four missions.
The watch sets come from `propose_watch_set.py` (the first hostile
candidates that fit 48 watches; C1_06 with the Maverick, pool index 3,
first).

### What the mission data allows

A survey of the 62 `ZONES.VOL` MSN members and the 61 CD-default
preparations of `es2prep.py`, read with a scratch reader of the [prepared
script framing](../formats/mission.md#prepared-script-exact-framing).

- **Trigger types 3 and 4** are covered both ways by `c5_05-moon-missiles`
  (426c81…426ccb, 426cf7…426d41; C5_05 has types 3 and 4). C4_03's only
  action slot sits on a type-2 "group safe" objective (group 90), the kind
  that never became true in `c1_02-probe`; C4_03 is not scripted.
- **Type 2 exists only in C4_05** (triggers 0, 2, 3). Its scan walks the
  groups (426c13 side byte, 426c25 pending group, 426c39 actor inside a
  region, 426c4b/426c5d the walk). No side-0 group of C4_05 (0, 1, 8, 9)
  has an activation trigger, so **426c25 taken is unreachable with the
  shipped data** (a candidate for a data-dead verdict, like types 5/6).
- **Trigger action slots** (`trigger_fire`): only TRAIN1–TRAIN4 have used
  slots, all `(0, 6)`, so 42345f taken and 423472 are data-dead. 423455
  fall needs a used slot; 423489 taken needs a fired trigger with a used
  slot and event selector −1: only TRAIN1 trigger 8 (serialized word +70 =
  0). TRAIN1 is CAREER group 0 position 0 (mode 0; `es2loadout.py` refuses
  group 0, `es2prep.py --campaign 0 0` prepares it). All four files equal
  the mapped original's (`es2shellprep.py --campaign 0:0 --mode 0`); the
  player is an Outlaw with items 8, 8, 8.
- **TRAIN1 timers.** Timer 0 (no gate, duration 10) fires trigger 0; timers
  1–6 (gated by triggers 0–5, durations 10/10/8/10/10/8) fire triggers 1–6
  one after the other; trigger 7 is the player inside region 0 (6,000
  around route point 1), and timer 7 (gate 7, 2 s) then fires trigger 8.
  The mission mode word (script header +6) is 2 in TRAIN1/3/4, the only
  missions with mode 2: `player_route` takes 41c29f fall and 41c2a5 taken
  there (mode neither 0, 5, 3 nor 7).
- **Objective actions.** 41352f…413540 run only on the first true
  evaluation of an objective with used slots (`objective_eval`,
  413510..413584); actions 4/5 occur nowhere (413537/41353a taken
  data-dead) and only 6 and 7 occur, so 413540 fall is data-dead too.
  C1_02 is not used for them: its objectives are type 2 and type 1 on
  building group 13 (ten buildings). The type-1 failure needs `group_state`
  ≥ 3 (`0x412c8c`: out × 1024 / count ≥ 650, seven buildings, or a mean
  damage level above 192), the type-2 objective needs every hostile that
  targets the group gone; in `c1_02-probe` the attackers h3/h8 are still
  100,000–200,000 away at frame 1,250 (golden), and trigger 1 (150,000
  around the base) fires at frame 0 because the player starts inside it.
  Instead: C1_08 (type 3 with `(321, 6)`), C2_02 (type 6 with `(360, 6)`)
  and C1_06 (type-1 failure on an actor with `(61, 7)`).
- **Mode-3 escort chain** (`player_route` 41c2ab..41c39a; mode 3 in C1_08,
  C2_04, C3_03, C5_01, C5_03, C5_06, TRAIN2): while the order subject
  (`order_actor`) is alive, its group active, the 3-D distance
  (`0x49288c`) below 10,000 and the player's heading within ±2000h of its
  bearing, the countdown `0x4a9cf0` is re-armed from table `0x49a318`
  (5,000, 5,000, −25,000) and counter `0x4a9cec` counts up (events
  34h–36h); at 4 player byte `+A0h` is set (41c396), the predicate of
  objective types 3/4 (inverse: 9/10). About 53 + 53 + 1 frames in range.
  Leaving the range resets the counter (event 38h).
- **`+9Eh` ("spotted").** `0x41277d..0x4127a7` sets the target's byte
  `+9Eh` when an observer of the other side is within 50,000 (detection
  table `+0C2h` below 100,000) and fires the target's trigger `+1B2h`. In
  the `c1_08-escort-route` golden the listening post gets it at frame 1758,
  the player 47,700 away. Objective types 6/8 (`group_marked`) occur in
  C1_04, C2_02, C3_08, TRAIN7 and DEMO_01; type 7 (`group_flagged`, every
  member `+A5h`) only in C4_10.
- **Actor-scope type-1 objectives** (413430 `+A4h`, 413438 `+99h`): C1_05,
  C1_06, C2_05, C2_08, C3_03, C4_10, C5_01, C5_06, DEMO_02, TRAIN6. A living
  subject gives both falls at every evaluation (about every 105 frames).
- **Outcome policy** (word table `0x49935c`): statuses 2, 3, 6, 7, 8, 9 are
  reported by `outcome_poll` (alert), 4, 5, 10 are not. Alert button table
  `0x49f5c0`: 0 for statuses 2/3 (Enter ends), 1 for 4/5/6/9/10 (Tab, Enter
  ends; Enter continues). Status 9 or 6 needs `actor_safe(player)` (no
  active hostile that detects the player within 80,000, 100,000 if it
  targets him), otherwise 10.
- **Not targeted:** type-1 group orders (`ram_step`, `ram_move`: C2_04,
  C4_07, C4_10, C5_04, C5_06, C5_09, C5_10), type-4 orders (`sleep_step`:
  C1_05, C2_05, C2_08, C4_02, C4_06, C5_04, C5_10, TRAIN2/3/6), actor-scope
  orders of MECH groups (`order_target` 423939, `order_point` 4238f1: TRAIN6
  group 1, C2_05, C4_02, C4_06), mode 5 (no mission) and mode 7 (C1_09 only
  under preparation condition 30).

Every leg of these scripts is straight. Turns use the Tomahawk profile of
the `c3_10-loadout-hercs` golden (first held frame +150, second +300, then
about +440 per frame, after release +290 and +140; Left increases the
heading; travel direction = heading + 90°) and, for C1_08, the
`c1_08-escort-route` golden, whose input equals `c1_08-scan`'s up to the
turn.

### `train1-timers`

- **Script** [train1-timers.jsonl](../../tools/reference-game/input-scripts/train1-timers.jsonl):
  a pump-keyed Enter at frame 0 closes the modal dialog at mission start
  (the main thread waits in the generic dialog loop `0x456000`, return
  addresses `0x456022`, `0x45fa2c`, `0x45f48c`); Up 100–120, then nothing.
  The player starts on route point 0 facing point 1 (217,334 due north).
  TRAIN1 reaches no MCI call, so the proxy needs early arming (the default)
  to arm at all.
- **Live** (discovery `train1-retry`): the timers fire triggers 0–6 at
  frames 213, 426, 639, 809, 1022, 1208 (trigger 5) and 1235, the player's
  region trigger 7 at 1208 and trigger 8 at 1251; variable 0
  (`mission_vars` +0) counts each one.
- **Watch set.** The runtime timer table holds heap pointers that move
  between runs, so `watch-sets/train1.txt` keeps the trigger table and does
  not watch the timers.

### `c4_05-convoy`

- **Script** [c4_05-convoy.jsonl](../../tools/reference-game/input-scripts/c4_05-convoy.jsonl):
  Up 100–120, Left 130–156 (from the bearing of route point 1, 111°, to that
  of point 2, 173°), then straight toward region 3 (100,000 around point 2,
  octagonal metric). Group 1 (three Mavericks, side 0) has order 1, type 6,
  subject group 0: the "following" state `0x499610` for the whole window.
- **Live:** the player does not reach region 3 inside the window;
  `follow_step` is called 13,659 times (its first live calls).
- **Limits.** Hostile groups 3 and 4 hunt group 1, so failure objective 2
  (type 1 on group 1) may become true: status 6 opens an alert (Enter
  continues, Tab, Enter ends), status 10 does not.

### `c1_08-scan`

- **Script** [c1_08-scan.jsonl](../../tools/reference-game/input-scripts/c1_08-scan.jsonl):
  the `c1_08-escort-route` input (Up 100–120) up to frame 1884, where that
  golden runs at heading 53332 and 163 per frame, 33,000 before route point
  1; Left 1885–1911 (planned heading 64772) toward the listening post (base
  0, list entry 12, `h12_pos` (3400915, 2686674, 15872); the subject of the
  player group's order 7: type 3, scope 3); Down 2059–2073; Q at 2600 with
  Tab, Enter ends the mission.
- **Live:** trigger 1 fires at 1646, but the player never scans the
  listening post (objective 0 stays false). The mode-3 chain needs the 3-D
  distance below 10,000; if a building footprint keeps it above, the chain
  stays at 41c301 taken and the escort predicate is a recording request.
- **Discovery watches:** `watch-sets/c1_08-escort.txt` with the three
  `h11_*` lines (flyer 3) replaced by `escort_count 0x4a9cec 4`,
  `escort_timer 0x4a9cf0 5` and `escort_seen 0x4a9cf8 4`.

### `c4_10-probe`

- **Script** [c4_10-probe.jsonl](../../tools/reference-game/input-scripts/c4_10-probe.jsonl):
  Up 100–120 along route 0, no other input. Objective 0 (required, type 7 on
  group 1: buildings 0–2, type 34) calls `group_flagged` at every
  evaluation; objectives 1–3 (failure, type 1, scope 3 on the same three
  buildings) test `+A4h`/`+99h`. No alert (statuses 4/5).
- **Live:** `group_flagged` 18 calls, its first live calls.

### `c1_06-cheat-disable`

- **Script** [c1_06-cheat-disable.jsonl](../../tools/reference-game/input-scripts/c1_06-cheat-disable.jsonl):
  player standing. V 130 (orbit, subject = player, list entry 15: the 15
  selected authored mechs 0–3, 5–15 come first); Ctrl held 140–255 with P
  ×12 at 142–252 (entry 3 = mech record 3, the Maverick of group 35, subject
  of objectives 0/1); Ctrl+Alt 260–333 with `.` ×7 (slot 7) and D ×11 at
  290–330. MAVERICK region 7 has threshold 2,200, flag 1 and one link to
  capacity 0 (1,000), the numbers of the Outlaw in `c1_01-cheat-disable-leg`:
  the 8th press crosses, the 11th (frame 330) fills capacity 0 and sets
  `+A4h` (disabled). V 340, Q at 700 with Tab, Enter.
- **Live:** the Maverick is disabled and objective 0 is true at 420
  (variable 61 − 1); `follow_step` 330 calls.
- **Limits.** 413438 taken needs the subject destroyed instead (slot 0,
  RNG-dependent press count): not in this suite. `watch-sets/c1_06-maverick.txt`
  keeps `maverick_flags`; the discovery replaced `actor35_side`…`actor38_side`
  by `view_subject 0x4d2684 4`, `maverick_ptr 0x4a9af8@12 4`,
  `maverick_flags 0x4a9af8@12@0x99 13` and `cheat_slot 0x4d2500 2`.

### `c2_02-identify`

- **Script** [c2_02-identify.jsonl](../../tools/reference-game/input-scripts/c2_02-identify.jsonl):
  Up 100–120 along route 1 (from point 1 straight to point 3, 272,600 away,
  the centre of the fifteen buildings of group 4); Down 1600–1614 stops
  about 29,000 short of the centre. Triggers 1 (200,000 around point 3) and
  2 (50,000) activate the hostile groups 9 and 10; flyers 5 and 7 hunt the
  player.
- **Live:** the first building within 50,000 gets `+9Eh` and objective 0
  (type 6, group 4) is true at frame 1680 (variable 360 + 1);
  `group_marked` 26 calls, its first live calls.

### Discovery watches for new launch sets

The new launch sets start from `propose_watch_set.py --bootstrap`; extra
lines: `train1` `timers 0x4a9e38@0 w0x4a9e34*49` (discovery only, see
above); `c4_05-convoy` `convoy0_pos 0x4a9af8@0@0x26 12`, `convoy0_state
0x4a9af8@0@0x4d 8`, `convoy1_pos 0x4a9af8@4@0x26 12`, `convoy2_pos
0x4a9af8@8@0x26 12` (list entries 0–2 = mech records 1–3, group 1). The
final sets come from `propose_watch_set.py --log` of that run.

## Tape record and playback

DBSIM records the per-frame control record to a tape with `-r<name>` and
plays one back with `-p<name>` or, in demo mode, `-D`
([file_io.md](../reverse-engineering/file_io.md#dbsim-recording-and-playback)).
One suite records; the two playback suites play the installation's CD tapes
and run with a tick limit and `hold` (fault below). The runner's tape transaction
(restore of the five `DATA` files and two `TAPES` files a playback
rewrites, the one-entry demo list, the tape size and hash in the run report)
is described in [oracle_regression.md](oracle_regression.md#tape-suites)
(`SuiteLaunch.psm1`, `run-prepared-suite.ps1`, `run-mission-outcome.ps1`,
`run-oracle-regression.ps1`, `suite_scripts_test` lane `lane_tapes`).
Registry entries carry their tape option in `game_arguments` and, for
`-D`, the field `demo_tape`; a tape option on the command line is refused
as a second tape option.

### Why the playback suites play the CD tapes

A tape that the record suite writes would make a playback suite depend on
the record suite having run before it in the same clone (the parallel
runner spreads suites over clones; a single `-Suite` check would fail on a
null FILE*), or on a sealed copy in the launch directory, which needs a
sealing step and a deployment member (the tape is game-written data that
embeds copies of installation files, so it can never be a golden output).
The CD tapes avoid both: the installation copies `TAPES` into every clone
([setup.md](setup.md#the-original-installer-and-the-portable-layout)), their
hashes are checked against the CD, and they cover more: a keyboard
recording varies only bits 0, 1 and 16 of the entry bit word (Space → R+16h
and R+0Dh, the in-frame tap U[0] → R+17h), because R+18h/R+19h come from
bytes the keyboard block always clears (`0x45a5a7`/`0x45a5aa`),
R+1Eh..+21h are the hat bytes and bits 8–15 the joystick buttons. The CD
tapes were recorded with a joystick: bits 1–3 and 8–11 vary, and
DEMO1/DEMO3 contain mouse events.

### Facts from the bytes

Sources: option parser `0x45e758`, header writer/reader `0x45cca4`/`0x45cd5c`,
demo pick `0x45ceb8`, `control_record` `0x45a810`, main loop
`0x45f430..0x45f54b`, the strings at `0x49f2a4..0x49f3f6` and the key mask
`0x49eab8`; the CD tapes `TAPES/DEMO1..3.TAP` and `TAPES/DEMOLIST.STR` of
`build\extracted\cd`; bytes 11h..18h of the CD's
`DATA/PREFS.CFG`.

- **Option parser** (`0x45e758`, cases from the jump table `0x45e7a3`):
  `-p`, `-r` and `-d` store their name in `0x4d24da` only if no name is
  there yet, so with two tape options the second uses the first's name;
  `0x492b90(name, "tap")` adds the extension. `-p` opens `<name>.tap` `rb`
  relative to the working directory (`0x45e9e8`) and restores the header
  without testing the FILE* (`0x45cd5c` reads through it); `-r` opens it `wb`
  and writes the header without a test either (`0x45ec0e`, `0x45cca4`). `-D`
  sets the demo and playback words before it builds the path, so
  `0x45eec0` prefixes `tapes\` (`0x49f3ef`) to the picked name.
- **Demo pick** (`0x45ceb8`): reads group 0 of root + `tapes\demolist.str`
  (`0x47ad40`, `0x4539a0`; the CD file has three strings `DEMO1..3`); a
  count of 1 returns entry 0 without the `_time` call (`0x45cf7f..0x45cf8b`),
  otherwise entry `time() % count` (`0x45cf91..0x45cfa4`; not virtualized:
  `_time` reads the host clock).
- **Per call in playback** (`0x45a892..0x45ab1b`): the live key word k is
  still read (`0x45a848`); with the demo word, k ≠ 0 sets the quit request
  `0x4d2532` (`0x45a93a`); `0x45fe6c(k)` (`playback_key`) runs for every
  record; a short read or k = 412h (Ctrl+E; the mask 47FFh keeps Ctrl)
  closes the tape, clears the playback word, sets `0x4d1dd6` (live mouse
  events are posted again, `0x452ce4`; it was cleared at mission start
  because playback ran, `0x461f38`) and, with the demo word, sets the quit
  request again (`0x45a966`). Otherwise N events (`0x453078`, `mouse_queue`)
  and M queued keys are read. The playback path skips only the keyboard axes
  and the button sources (`0x45ad06` → `0x45ad4c`) and step 6 (`0x45aef6` →
  `0x45b2ca`): the tape's queued keys still go through `0x45feb8`
  (`command_key`) and `0x452d40` (`mouse_dispatch`) dispatches the reposted
  events in the same call.
- **After the tape.** With `-p` the mission continues with live input; with
  `-D` the main loop leaves at `0x45f47e`, `0x461ff0` writes `results.dat`
  and `mission.var` unconditionally (`0x462029`) and sets the exit code 6
  for VSHELL (`0x462036`). The exit tail deletes `tapes\prefs.cfg` and
  `tapes\keyjoy.cfg` only while the playback word is still set (`0x45f50a`),
  i.e. never after Ctrl+E or the end of the tape.
- **Clock.** Playback skips the frame limiter (`0x45f465`) and copies the
  tape's time-scale word (`0x45f593`). Under virtual time the clock then
  moves only by intra-frame reads (at most step − 1 per limiter poll,
  [virtual_time.h](../../tools/reference-game/proxy/virtual_time.h)), stall
  advances and one step at the frame boundary for a frame without a poll
  (`vt_frame_boundary`). Clock-gated code (stream B at `0x4340b0`,
  [rng.md](../reverse-engineering/rng.md#demo-path)) therefore sees a
  nearly standing clock, so a playback is not a copy of the recorded
  session.
- **Recording bits** (`0x45b0cd..0x45b2c4`, `0x45a586..0x45a5aa`): the first
  four button targets R+16h..+19h take the joystick button if set, else the
  keyboard block bytes `0x4d23d1..d4` (Space D[14], U[0], 0, 0); R+0Dh is the
  first button whose map byte (prefs bytes 11h..18h) is 1, which is then
  cleared, else Space. The CD `PREFS.CFG` has all eight map bytes 0; the
  prefs inside the demo tapes have byte 11h = 1, which is why bit 0 never
  appears in them.
- **The CD tapes** (records; header lengths script/player/var/prefs/
  restore/object/keyjoy; first..last record with each set bit; events;
  queued keys):

  | Tape | Records | Header | Bits set (records) | Events (N = 1) | Queued keys |
  |---|---:|---|---|---|---|
  | DEMO1 | 1,499 | 13,520/151/2,000/54/42/0/518 | 1, 9: 341..780; 2, 10: 743..916; 8, 16: 435..950 | 1427, 1498 | 71 in 69 records |
  | DEMO2 | 1,901 | 13,520/495/2,000/54/42/0/518 | 1, 9: 129..1255; 2: 660..1002; 10: 660..1003; 3, 11: 663; 8: 644..951; 16: 331..1529 | none | 73 |
  | DEMO3 | 3,660 | 13,520/143/2,000/54/42/0/518 | 1, 9: 186..3260; 2, 10: 3007; 3, 11: 296..3271; 8, 16: 310..3268 | 3525, 3552, 3630, 3659 | 60 in 57 records |

  Bits 0, 4–7 and 12–15 are clear in every record of all three; no record
  has N ≥ 2. Each tape parses exactly to its end. The tapes' embedded
  `script.dat` (13,520 bytes each) match no preparation of the 61 CAREER
  missions or DEMO_01/DEMO_02; their mission-string indices (7, 8) fit
  C1_01's `MISSION.STR` (11 strings).

### `c1_01-tape-record`

- **Script** [c1_01-tape-record.jsonl](../../tools/reference-game/input-scripts/c1_01-tape-record.jsonl):
  `=` 30, `-` 40 (queued keys, M = 1); Up 60–90; Space 100–104 and 120; Num7
  down and up inside frame 140 (U[0] → R+17h, hypothesis: both messages are
  pumped before the frame reads the block); `[` 160, `]` 170; right clicks
  at (320,150) 200/203 and 240/243 (button messages → mouse events, N = 1);
  Ctrl+B 260–263 (430h); Alt+S 299–302 and 330–333 (freeze on and off while
  recording; frozen frames are counted, [launcher.md](../reverse-engineering/launcher.md));
  Left 360–380, Space 420–423, Down 450–470.
- **Registry** legacy C1_01, watches `watch-sets/c1_01-mission.txt`,
  `game_arguments` `["-rc1tape"]`. The run writes `<clone>\c1tape.tap` (a header of seven
  files, then one record per `control_record` call, appended with `ab` each
  time); the tape transaction reports its size and hash (`tape` in the run
  report and the entry's `launch`) and removes it. The run is stopped
  asynchronously, so the tape's length varies from run to run; the golden
  is the tick log only.
- **Live:** golden identical with all
  hooks original and all replaced.
- **Targets** (`control_record`): 45b2d2 fall; 45b305 taken (the fall edge
  is the fopen failure); 45b32d both; 45b39e both (Space; R+16h is not
  cleared because the clone's prefs map bytes are 0, hypothesis: the CD
  values); 45b3b1 both if the Num7 tap sets U[0], else fall; 45b518 both;
  45b575 both; 45b598 both; fall only (zero bytes without a device) 45b3c6,
  45b3db, 45b3f0, 45b405, 45b41a, 45b42f, 45b446, 45b461, 45b47b, 45b496,
  45b4b1, 45b4cb, 45b4e6, 45b501. `command_key` 4603b9 fall (Alt+S with the
  recording word; `c1_01-keys-extra` gives the taken edge); `playback_key`
  45fe8c taken (Ctrl+B through `command_key` 45ff08).

### Tape playback suites

`c1_01-tape-play` and `c1_01-tape-demo` are registered with tick limits and
`"hold": true`; the limit of `c1_01-tape-demo` lies below the original's
playback fault at `0x453127` (registration below).

- **`c1_01-tape-play`** ([script](../../tools/reference-game/input-scripts/c1_01-tape-play.jsonl)):
  live Ctrl+B 200–203 during playback (k = 430h reaches `0x45fe6c` directly
  and does not end playback); Up 1950–1980 after the end of the tape.
  Entry: legacy C1_01, watches `watch-sets/c1_01-mission.txt` (the
  pointer watches of a route the demo mission lacks are logged as
  unreadable, deterministically), `game_arguments` `["-ptapes\\demo2"]`,
  `outcome` false, `ticks` 650, `hold` true. The registered run ends at
  frame 650, before the Up keys. Without the limit the mission ends before
  the tape does: the player's dead byte (actor `+0x99`) is set from frame
  903, the mission ends in frame 946, and DBSIM exits with 3 after writing
  `results.dat` and `mission.var`.
- **`c1_01-tape-demo`** ([script](../../tools/reference-game/input-scripts/c1_01-tape-demo.jsonl)):
  Ctrl down 1459, E 1460–1461, Ctrl up 1462. In frame 1460 k = 412h: the
  quit request (45a938 fall), the end of playback (45a95a fall) and the
  demo branch of the end (45a964 fall); the main loop leaves after that
  frame and DBSIM exits. Without the key the tape's end at record 1499 ends
  the demo the same way (45a951 taken instead). Entry: legacy C1_01,
  `game_arguments` `["-D"]`, `demo_tape` `"DEMO1"` (the runner writes the
  one-entry list), `outcome` false, `ticks` 880, `hold` true. The player's
  dead byte is set from frame 871, the mission end's GeneralAlert runs from
  frame 919, and the fault comes after 920 frames, before the quit key; the
  registered run therefore ends at frame 880, and the key frames,
  `results.dat` and the rewritten `mission.var` stay unreached.
- **Both** start from the legacy C1_01 state of the clone and then run the
  demo's mission from the tape header (C1_01's `MISSION.STR` stays, as when
  VSHELL starts a demo after any mission).

**The fault.** Without a hold and with all hooks original, `c1_01-tape-demo`
ends with an access violation at DBSIM `0x453127` (exit code `0xC0000005`,
Windows application log), with no exit record and no outputs, after 920
frames, both with the clock standing (during playback the loop skips the
limiter in every frame: `limiter_bypassed` true, `vt_limiter_polls` 0,
`completed_clock_ms` 16,777,216 throughout) and with one 47 ms step at the
frame boundary (`vt_frame_boundary`). With the clock standing,
`c1_01-tape-play` faults after 686 frames; with the 47 ms step it does not
fault and runs to its mission end in frame 946. The fault is in `0x4530fc`,
the timed-widget list of a panel object (`+0x356`, eight 8-byte entries
{widget, release time}, count `+0x396`): `button_release` `0x4530bc`
compares the count with 7 but appends anyway, so a ninth pending entry
overwrites the count and the scan reads a garbage widget (`cmp word
[ebx+1Bh], 1`). In `DEMO1` the list is the GeneralAlert's at the mission
end: the tape's click run from record 919 presses its focused gadget in
every modal pass ([hud.md](../reverse-engineering/hud.md#headless--d-runs)).

**Release timing** (listing of `VER95/DBSIM.EXE`). An entry leaves the list
only in `0x4530fc`: per entry it reads the clock wrapper `0x467828`
(`GetTickCount` through the thunk `0x4968ec`, minus the base
`[0x4d3b78]`, `shr 4`: 16 ms units) and releases when `now >= time` (`cmp
eax,[entry+4]`; `jae 0x453145`, unsigned). `0x4530bc` stores `time =
0x467828() + 10`, so an entry lives 145–160 ms of `GetTickCount` time,
independent of frames. The capacity check `cmp word [eax+0x396],7` at
`0x4530c3` sets flags that no branch reads, and there is no check for a
widget already in the list. During playback the scan runs once per render
frame from the panel draw `0x4327ac` (`0x4328bc`, from `render_root`
`0x45fd4f`); it is skipped while the panel is off (cockpit byte `+0x20f`)
or during the extra render that sets `0x49b104`. In a mission `0x4530bc`
has one caller, `button_press` `0x438d9c` (`0x438dd0`); the other two
callers (`0x454e20`, `0x459000`/`0x45903f`) belong to the dialogs.
`button_press` has 25 call sites: the HUD, MFD, HDD and map key handlers
(`hud_key` `0x432bc8`, `mfd_key` `0x446a65`…, `hdd_key` `0x44a07f`…,
`map_key` `0x44cdb3`), the vehicle keys `0x415ae7`, and the sim-driven
`blip_scan` `0x43ec94` and `mfd_page` `0x446f9f` (from `mfd_auto_page`
`0x44718c`, every frame). The tape keys reach them through `command_key`
`0x45feb8`; the button presses are therefore fixed by the tape and by the
sim, which takes each frame's time from the tape (the logic frame copies
the record's scale word into `0x4d3b64`, `0x45f595`). Which sites fill the
list in the frozen-clock fault of `c1_01-tape-play` is open.

**Verdict: the `DEMO1` fault belongs to the original, not to the virtual
clock.** The number of pending entries at a scan is the number of presses
in the last 145–160 ms of wall time. The presses do not depend on wall
time, so a faster machine holds at least as many entries as a slower one.
The frozen clock is the fastest case; the 47 ms step corresponds to 21 fps;
`DEMO1` faults after 920 frames with both. Playback has
no limiter, so any machine that renders `DEMO1` faster than 21 fps faults
at the latest where the 47 ms step faults. For `DEMO2` the two clocks
bound the case from both sides: the frozen clock faults, the 47 ms step
does not; where between them the original on present hardware lies is
open. Only at the recording machine's own frame times does the list stay
short: the tapes' scale words (`elapsed = scale·125/256` ms, clamped to
64..450 by the limiter `0x4678c0`) give a median of 110 ms (`DEMO1`) and
133 ms (`DEMO2`), so almost every entry expires before the next scan. A
virtual clock that plays the tapes at their recorded frame times would
model that machine, not the original on present hardware; it is not part
of the oracle.

**Full playbacks.** Without a hold and with the 47 ms step, the original
plays each CD tape until the run ends by itself, and es2sim replays each
run with identical ticks, watch changes and return files:

| Run | Frames | End | Player's dead byte |
|---|---:|---|---|
| `-ptapes\demo2` with the `c1_01-tape-play` journal | 947 | mission end in frame 946, exit code 3, `results.dat` and `mission.var` written | from frame 903 |
| `-D`, `DEMO1`, the `c1_01-tape-demo` journal | 920 | fault at `0x453127` (es2sim: refusal at `0x4530bc`) | from frame 871 |
| `-D`, `DEMO3`, no input | 3,349 | end of the tape, exit code 6, `results.dat` and `mission.var` written | never |

The tapes hold more records than these playbacks read (`DEMO1` 1,499,
`DEMO2` 1,901): in `DEMO1` and `DEMO2` the player's herc is destroyed long
before the recorded input ends, so the playbacks do not follow the
recorded sessions (hypothesis: the tapes were recorded with another
version of DBSIM).

**Registering them.** `c1_01-tape-demo` needs a tick limit below its 920
frames to the fault; the playback sites stay unreached until then.
`c1_01-tape-play` is registered with the same scheme at 650 frames. A tick
limit alone does not end the run there: playback is unpaced and the runner
stops DBSIM from outside after a 500 ms poll. With `-Ticks 650` / `880` and
no hold, `c1_01-tape-play` reached frames 866 and 777 (different per run)
and `c1_01-tape-demo` 919 both times, one frame before its fault. Both
entries therefore need `"hold": true`
([oracle_regression.md](oracle_regression.md#tape-suites)): the proxy parks
the game thread after exactly `ticks` records, before the next frame starts
([tick_log.md](tick_log.md#hold-at-a-record)).

**Live with the hold:** two runs
per suite on `timing-r111` saw exactly 650 / 880 tick records, the hold line
last, and were identical to each other; the goldens were
recorded from a clean tree, and a check run of the original
is identical to them. es2sim replays both (frame limit =
`ticks`, the one-entry `DEMOLIST.STR` for `-D`) IDENTICAL to the goldens,
as does `c1_01-tape-record`.

**Edge coverage:**
`run-oracle-parallel.ps1 -Suite c1_01-tape-play,c1_01-tape-demo
-LiveCoverage run -CoverageMode edge -CoverageBaseline` against the union of
the other suites ([live_coverage.md](live_coverage.md#comparing-the-next-union)); the held
suites run with the proxy's `frame_limit` = `ticks`
([live_coverage.md](live_coverage.md#configuration-and-measurement)). Both
suites were identical to their goldens, so the union is valid. Beyond
`control_record`, the demo mission of `DEMO1` reaches `order_target`
423933 fall and 423939 taken (an actor-scope group order), `sleep_step`
41c427 taken, `subject_status` 412dd9 taken, 412dfe fall, 412e13 taken and
412e2f fall (an actor subject); `c1_01-tape-play` adds `button_press`
438db2 taken, `hud_key` 432c35 taken, `leader_route` 41fb81 taken,
`order_done` 423a1d fall, `torso_pitch` 41a92a fall, `torso_twist` 41a675
fall and `vehicle_keys` 41581d taken. es2sim has no edge-coverage
measurement, so the result is original-side only.

**Playback targets** (`control_record`, 46 sites in all; Seen = the union
with these suites):

| Sites | Suite | Expected | Seen |
|---|---|---|---|
| 45a8a7, 45a930 | play, demo | both ways | both ways |
| 45a938, 45a95a, 45a99e | play, demo | both ways | taken only: the fall edges (quit request, end of playback) come with Ctrl+E or the end of the tape, after the hold |
| 45a951 | play, demo | both ways | fall only: the short read at the end of the tape lies after the hold |
| 45a964 | play, demo | both ways | never: the end of playback lies after the hold |
| 45aa3d, 45aa4a, 45aaa1, 45aaaf, 45aabd, 45ab11 (bits 1, 2, 8, 9, 10, 16) | play, demo | both ways | both ways |
| 45aa57, 45aacb (bits 3, 11) | play | both ways | fall only: `DEMO2` sets bits 3 and 11 in record 663, after the hold at 650 |
| 45a9e2 | demo | fall only (no record with N ≥ 2) | never: the events of `DEMO1` (records 1427, 1498) lie after the hold at 880 |
| 45aa64, 45aa71, 45aa7e, 45aa8b, 45aad9, 45aae7, 45aaf5, 45ab03 (bits 4–7, 12–15) | play, demo | fall only | fall only |
| 45b32d, 45b39e, 45b518, 45b575, 45b598 | record | both ways | both ways |
| 45b3b1 | record | both ways (hypothesis), else fall | both ways |
| 45b305 | record | taken only (fall = fopen failure) | taken only |
| 45b3c6, 45b3db, 45b3f0, 45b405, 45b41a, 45b42f, 45b446, 45b461, 45b47b, 45b496, 45b4b1, 45b4cb, 45b4e6, 45b501 | record | fall only (need a joystick or hat) | fall only |
| 452dd4 (`mouse_dispatch`) | demo | one way (which edge depends on the cursor-follow byte `0x49dbdf`) | never: no event before the hold |

Also in play: 45a89a fall, 45ad06 taken and 45aef6 taken
during playback (their other edges were already in the baseline),
`logic_frame` 45f593 fall; `playback_key` 45fe8c taken comes from the
baseline (`c1_01-tape-record`), and 45fe9d/45fea7 stay never (no Alt+Enter).

### Not covered by the tape suites

- **N ≥ 2 events in one record** (45a9e2 taken), **bits 4–7 and 12–15**
  (hat bytes, joystick buttons 5–8) and the **recording edges of R+18h,
  R+19h, the hat and the second button set**: none of the CD tapes has them,
  and a keyboard cannot produce them. A record suite with the [scripted
  joystick](#scripted-joystick) (`joypos` buttons and hat while `-r` runs,
  plus two button messages in one frame) and a second suite that plays its
  tape back would; that playback needs the sealed tape in the launch
  directory and a launch-set member that the transaction deploys to the
  clone root (not implemented).
- **45b305 fall** (the tape cannot be opened): defensive; DBSIM would write
  through a null FILE*.
- **`flight_key` 45a338 fall** (a flight key in demo mode sets the quit
  request): exclusive with the Ctrl+E path in one demo run.
- **`playback_key` 45fe9d** (Alt+Enter, the display switch `0x4667c8`) and
  **45fe8c fall** (the option word `0x4d252c`, set only by an option at
  `0x45e858`, turns Ctrl+B into an `int3`): not for the oracle.
- **`command_key` 4603af taken** (Alt+S during playback): none of the CD
  tapes queues Alt+S; only a self-recorded tape would.
- **`-d`**: the dump file only (`0x401e5c`); its producer was compiled out
  ([file_io.md](../reverse-engineering/file_io.md#dbsim-recording-and-playback)).

## Scripted joystick

The input journal answers `joyGetDevCapsA`/`joyGetPosEx` from a script, so
a suite can drive the joystick paths without a device
([input_journal.md](input_journal.md)). Scripted replay is verified: two
runs of the same synthetic journal agree over every compared tick. Replay of
a recording made with a real device is not verified.

### Facts from the bytes

- **Init** `0x477674` (from `0x4775dc`, from `0x45c298`): for devices 0 and 1
  `joyGetDevCapsA(id, 0x6bb378 + 404·id, 0x328)` (`0x4776ba`; the size is
  808, twice `sizeof(JOYCAPSA)`), result in `0x6bb6a0 + 4·id`; for a device
  that answers 0 one `joyGetPosEx(id, 0x6bb310 + 52·id)` (`0x4776e4`) with
  `dwSize` 52 and `dwFlags` `0xccf`. A device 0 that answers (result 0,
  `cmp [0x6bb6a0], 0` at `0x4776fa`) sets bit 0 of the object's flags.
- **Poll** `0x477720` (only from `0x45c544`, from the device poll `0x45baa8`
  of `control_record`, once per frame): `joyGetPosEx` for devices 0 and 1
  (`0x477754`); a failing device gets centred axes (`0x8000`) and clears its
  ok byte; with device 0 ok the summary takes X and Y (`+8`, `+0Ch`), Z and R
  from device 0 when its `wCaps` (`0x6bb3d8`) has bit 0/1, else device 1's
  X/Y, the buttons (`+20h`, plus device 1's shifted by device 0's button
  count) and, with `wCaps` bit 4, the point of view (`+28h`: 0, 9000, 18000,
  27000 → up, right, down, left).
- **Journal:** the caps blob is 808 bytes; the journal's blob limit is 960
  bytes (`input_journal.h`, fixture mode `joystick`).

### Suites

`tools/reference-game/scripts/joystick_script.py` writes the journal from a
program (`input-scripts/programs/*.json`): device 0 with ranges 0..65535,
8 buttons and `wCaps` Z|R|POV, device 1 answering 165 (as recorded on a
machine without devices; its polls fall back, one `in_fallbacks` per frame,
deterministically); frame 0 carries two device-0 entries (the init poll and
the first frame's poll).

| Suite | Launch | Program |
|---|---|---|
| `c1_01-joystick` | legacy C1_01 | centre to 99; Y forward 100–199; X left, right, back 200–439; Z down/up 440–639; R both ways 640–879; hat four ways from 880; buttons 1–8 from 1000; Y back 1160–1259 |
| `c4_08-joystick` | `c4_08-razor-plasma` | outcome suite: the same program for the Razor; it hits the ground and the outcome alert stalls frame 1040, answered with the pump-keyed Enter |

**Live:** in C1_01 Y forward drives
the throttle to 1,024 and Y back to −1,024, X turns the Herc (heading
63,524 → 63,992 → 13,538 → 62,166); the Z and R sweeps leave throttle and
heading unchanged in the default mapping set (`0x4d2577` = 13). The Razor
climbs, banks and dives under the same program until the crash. Goldens
identical with all hooks original and all replaced, every
`*_skipped`/`*_bypassed` counter 0.
