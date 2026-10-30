# CLAUDE.md

Rules for every contributor, human or AI agent. Overview: [README.md](README.md); open work:
[docs/work_packages.md](docs/work_packages.md).

## 1. Public and private

- `es2` is public. Never commit game data, files from the CD, recordings, goldens or evidence
  logs; those live in the private `es2-data` or locally. The pre-commit guard refuses CD files.
- The only bytes from the original executables in `es2` are the generated C sources of
  `sim/data` and `shell/data` (tables, constants, strings, the two icons, with their VAs).
  Their generators under `tools/reference-game/scripts/` read the user's file and never run in
  a build; a test under `ES2_DATA_ROOT` compares them byte for byte with the original.

## 2. Original binaries

- Never modify the original EXEs/DLLs on disk. Runtime changes come from our own DLLs, in
  memory, after checking the module hash and byte signatures.
- Never use the owner's installation `build/installs/native-r111` for experiments; use a
  clone. Before stopping a game process, verify its executable path and start time.

## 3. Dependencies

- Ask the owner before adding any tool, library or runtime. Use only the pinned tools of
  [docs/dependencies.md](docs/dependencies.md), the system and the shared runtime.
- The REA MCP server is an analysis aid for the CD's executables, not a dependency; its
  results are hypotheses until checked against the `llvm-objdump` listing and in vitro.

## 4. Working style

- Behaviour must match the original; structure need not.
- Reverse-engineering claims are hypotheses until confirmed against the original (oracle,
  in vitro, listing).
- Addresses refer to the binaries of [docs/reference_version.md](docs/reference_version.md)
  at image base `0x00400000`.
- C17; explicit little-endian readers; never `fread` into structs; parsers check bounds.

## 5. Documentation and comments

- Document the current state only. No history, dates, progress counts, log references,
  hashes, local paths or package IDs; package IDs appear only in `docs/work_packages.md` and
  commit messages.
- A documented statement is confirmed; never mark it so. Mark only hypotheses and open points.
- A comment stays only if it says something the code does not: an original address, layout
  or register, a non-obvious reason, a trap.
- Docs, files and targets are named by topic, never by package.
- When work completes, update `docs/work_packages.md` and whatever README.md says about it.
- Commit messages: a subject line and at most a few lines of body.
