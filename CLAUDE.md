# CLAUDE.md

Rules for every contributor, human or AI agent. Goal, architecture and status: [README.md](README.md). Functionality: [docs/overview.md](docs/overview.md).

## 1. Original binaries

- Never modify the original EXEs/DLLs on disk. Runtime changes come from our own DLLs, in memory, after checking the module hash and byte signatures.
- Never launch, stop or modify `build\installs\native-r111` for experiments; use a clone. Before stopping a game process, verify its executable path and start time; never reuse old PIDs.

## 2. Dependencies

- Ask the owner before installing or downloading any new tool, library or runtime, portable ones included. Use only tools from `<es2-dependencies>`, the system or the shared runtime, never tools, files or build outputs of other projects. Pin versions and checksums (`tools/setup-dependencies.ps1`, [docs/dependencies.md](docs/dependencies.md)).
- Approved: Bazel modules from the Bazel Central Registry (pinned in `MODULE.bazel`, `MODULE.bazel.lock` committed) and the SDL3 3.4.18 binaries. Other large sources and SDKs need approval.
- REA MCP server ([setup](docs/dependencies.md#rea-mcp-server-approved-2026-10-10)): an analysis aid, not a dependency; no script, Bazel target or test calls it. Its only targets are the verified files in `build\extracted\cd` or copies of them. Its decompiled code may be a basis for our code once every register, width, sign extension and read/write order is checked against the `llvm-objdump` listing and confirmed in vitro. Its results are hypotheses (§3); names and types go into our docs, exported evidence to `es2-data`.

## 3. Working style

- Behaviour must match the original; structure need not.
- Reverse-engineering claims are hypotheses until confirmed against the original (oracle, in vitro, listing). Keep evidence paths in the docs; do not overstate results.
- Addresses refer to the binaries in [docs/reference_version.md](docs/reference_version.md) at image base `0x00400000`.
- C17; explicit little-endian readers; never `fread` into structs; parsers check bounds.
- Data from the executables: the bytes the own executables need from `DBSIM.EXE` and `VSHELL.EXE` (tables, constants, strings, the two icons) are committed as generated C sources with their VAs. The generator under `tools/reference-game/scripts/` reads the user's file once and never runs in a build or test; a test under `ES2_DATA_ROOT` compares every run byte for byte with the original. Nothing else from the CD is committed.
- A change that alters what README.md's Status or Verification says updates it.

## 4. Documentation and comments

- Document the current state; history lives in git. No dates, rounds, package IDs, "owner decided", "since …", "was removed".
- A comment stays only if it says something the code does not: an original address, layout or register, a non-obvious reason, a trap.
- Docs, files and targets are named by topic or function, never by package. Package IDs appear only in commit messages.
- Commit messages: a subject line and at most a few lines of body.
