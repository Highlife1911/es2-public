# CLAUDE.md

Rules for every contributor, human or AI agent. What this branch holds: [README.md](README.md).

## 1. Original binaries

- Never modify the original EXEs/DLLs on disk. Runtime changes come from our own DLLs, in memory, after checking the module hash and byte signatures.
- Never launch, stop or modify `build\installs\native-r111` for experiments; use another installation (`prepare-native.ps1 -InstallName`). Before stopping a game process, verify its executable path and start time; never reuse old PIDs.

## 2. Dependencies

- Ask the owner before installing or downloading any new tool, library or runtime, portable ones included. Use only tools from `<es2-dependencies>`, the system or the shared runtime, never tools, files or build outputs of other projects. Pin versions and checksums (`tools/setup-dependencies.ps1`, [docs/dependencies.md](docs/dependencies.md)).
- Approved: Bazel modules from the Bazel Central Registry (pinned in `MODULE.bazel`, `MODULE.bazel.lock` committed). Other large sources and SDKs need approval.

## 3. Working style

- The game stays the original. A measure changes only what the current Windows breaks (colour depth, the missing Indeo mapping, the mission-map fault), and only in memory of the game's own processes.
- Claims about the original are hypotheses until confirmed (live, listing). Keep evidence paths in the docs; do not overstate results.
- Addresses refer to the binaries in [docs/reference_version.md](docs/reference_version.md) at image base `0x00400000`.
- C17; parsers check bounds.

## 4. Documentation and comments

- Document the current state; history lives in git. No dates, rounds, "owner decided", "since …", "was removed".
- A comment stays only if it says something the code does not: an original address, layout or register, a non-obvious reason, a trap.
- Commit messages: a subject line and at most a few lines of body.
