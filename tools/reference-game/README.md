# Reference-game tooling

Everything that runs, instruments or measures the original EarthSiege II
(the oracle). Behavior and evidence are documented in
[docs/reference-game](../../docs/reference-game/); this file
only describes the layout.

| Directory | Contents | Build / run |
| --- | --- | --- |
| `proxy/` | Own `ddraw.dll`: media bridge, mission-map guard, tick log, memory watches, virtual time, input journal, hook adapters, registry (`hook-names.txt`) and opt-in original-Jcc coverage. Fixtures in `proxy/tests/`. | `bazel build //tools/reference-game/proxy:ddraw.dll`, `bazel test //tools/reference-game/proxy/...` |
| `invitro/` | Differential harness: runs the original DBSIM functions behind every hook in a private mapping against `core/` ([README](invitro/README.md)). | `bazel test //tools/reference-game/invitro:differential_test` (quick tier), `:differential_full` (full tier; both need `ES2_DATA_ROOT`) |
| `helpers/` | Standalone x86 helpers outside the game process: `capture_crash`, `hang_snapshot`, `probe_media`, `session_volume`. Process-level tests in `helpers/tests/*.ps1`. | `bazel build //tools/reference-game/helpers:all` |
| `setup/` | PowerShell: install, configure, launch and regression scripts. `Es2Bazel.psm1` is their only way to build. | see [setup.md](../../docs/reference-game/setup.md), [oracle_regression.md](../../docs/reference-game/oracle_regression.md) |
| `scripts/` | Python (standard library only): regression comparison and normalization, bisecting, live coverage and probe reports, the H4 candidate ranking, the compare-mode and RTL audits, the mission/route pilots for authoring suites. Unit tests in `scripts/tests/`. | `bazel test //tools/reference-game/scripts/...` (pinned Python) |
| `input-scripts/`, `watch-sets/`, `oracle-suites.json` | Inputs of the oracle regression suites. | read by `setup/run-oracle-regression.ps1` |
| `setup/run-live-coverage.ps1`, `scripts/live_coverage_*.py` | Checked live coverage measurements, merged JSON/Markdown reports and a pasteable hooks column; synthetic mock mode. | see [live_coverage.md](../../docs/reference-game/live_coverage.md) |

The hook framework itself is in [hook/](../../hook/README.md) and the
replacement simulation code in [core/](../../core/README.md).

Scripts never deploy to the primary installation `native-r111` for experiments;
measurement work uses separate clones.
