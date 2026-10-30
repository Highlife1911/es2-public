# launcher

Our replacement for the original `ES.EXE`. It parses the same
command line, reads `data\language.cfg` and `error.str`, performs the same
memory check and runs the same VSHELL/DBSIM loop, in which the exit code of
the last child selects the next one. Contract and evidence:
[docs/reverse-engineering/launcher.md](../docs/reverse-engineering/launcher.md).

| File | Role |
| --- | --- |
| `src/launch_logic.{h,c}` | Portable C17 decision logic: options, language, memory decision, start loop, argv, `error.str` framing (through `formats/src/str.c`, docs/formats/str.md). No Win32. |
| `src/es2launch.c` | Win32 glue: file reads, `GlobalAlloc`/`GlobalMemoryStatus`, `MessageBoxA`, `CreateProcessA` and waiting. |
| `tests/test_launch_logic.c` | Unit tests of every rule; with `ES2_DATA_ROOT` also the CD's four error string files. |
| `tests/stand_in_child.c`, `tests/test_launch_chain.c` | Chain test: synthetic `vshell.EXE`/`dbsim.EXE` record their command line and return scripted exit codes (or run the command in `<name>.delegate`, used by `//shell:chain_real_test`); also the `ES2_LAUNCH_SHELL` replacement. |

`es2launch.exe` is a GUI program, not large-address-aware (like ES.EXE, so
that `GlobalMemoryStatus` reports the same values; the chain test checks the
flag) and does not need `CW3220.DLL`. It embeds no original text: messages
come from the game's `error.str`; only the "error.str could not be loaded"
box uses our own wording.

Our additions are environment variables (and `es2launch.ini`) only, so the command line keeps its
original meaning:

| Variable | Effect |
| --- | --- |
| `ES2_LAUNCH_LOG` | Append a time-stamped line for the start, every child start (program and command line) and exit code, messages and the end of the loop. |
| `ES2_LAUNCH_SCRIPT` | Dry run: comma-separated exit codes are used instead of starting children. |
| `ES2_LAUNCH_NO_UI` | Write messages to the log instead of showing a message box (tests). |
| `ES2_LAUNCH_SHELL` | Shell replacement: a command prefix started instead of VSHELL, with the VSHELL arguments appended and no console window. Exit code 2 starts DBSIM; any other code ends the loop. Used by the campaign chain `shell/tools/es2chain.py`, see [launcher.md](../docs/reverse-engineering/launcher.md#campaign-chain-through-the-own-launcher). |
| `ES2_LAUNCH_SHELL_CONSOLE` | Nonempty and not `0`: the shell replacement gets a console window of its own (`CREATE_NEW_CONSOLE`) instead of none; for the interactive text menu `es2menu.exe`, see [launcher.md](../docs/reverse-engineering/launcher.md#own-menu-es2menu). |
| `ES2_LAUNCH_SIM` | Command prefix of the own DBSIM process (es2sim), started instead of `DBSIM.EXE` with ` --arguments "<DBSIM argv without dummy>"` appended and no console window; its exit code is used like DBSIM's (es2sim's own 64..71 end the loop). See [launcher.md](../docs/reverse-engineering/launcher.md#the-own-dbsim-process-behind-the-launcher). |
| `ES2_LAUNCH_SIM_OUTPUT` | A file that receives es2sim's stdout and stderr (appended). |
| `es2launch.ini` | `[sim] command` and `[sim] output` in the working directory (the installation) stand in for the two variables when they are unset or empty; `tools/reference-game/setup/configure-sim.ps1` writes and removes it. |

Build and test:

```powershell
bazel test //launcher/...
```

(`.bazelrc` sets `ES2_DATA_ROOT` for the real `error.str` check and enforces the lock file.)

Start the game through it in a measurement clone (never `native-r111`):
`tools/reference-game/setup/start-own-launcher.ps1` (default `calibration-r111`;
clones with oracle settings in `es2-oracle.ini` are refused; `-DryRun` prints
the plan only). The script copies the launcher as
`ES2LAUNCH.EXE` next to the original files and writes the log to
`<es2-data>\logs\own-launcher-*.log`.
